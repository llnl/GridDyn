# Model declarations and field snapshots

The current `ModelDescription` field declarations have four stored-data roles:

| Declaration       | Meaning                                               | Storage                     |
| ----------------- | ----------------------------------------------------- | --------------------------- |
| `parameter(...)`  | Numeric, enum, time or string setting                 | Existing member             |
| `flag(...)`       | Boolean control or option, without numerical units    | Boolean member or named bit |
| `state(...)`      | Solver-managed variable, including algebraic unknowns | Active solver vector        |
| `localState(...)` | Retained model memory that cannot be reconstructed    | Existing member             |

The declaration determines its role, default member access and capture behavior.
There are no per-field access or capture options. Physical parameters and local
configuration both use `parameter`; calculated outputs, cached coefficients and
copies of solver vectors need no checkpoint declaration. `command(...)` is
optional discovery metadata for existing actions such as `alpha` and `p+`;
commands have no stored value or snapshot record.

The [SEXS exciter example](exciter-model-description.md) applies these declarations
to differential states, adjustable limits and retained limiter history.
The [component migration notes](component-model-descriptions.md) cover loads,
network components, generator containers, limiters, compensators, and renewables,
including structured parameter storage and behavior that stays in the models.

## One declaration per value

[ModelParameter.hpp](../../src/core/ModelParameter.hpp) contains the member-binding
templates. [ModelDescription.h](../../src/core/ModelDescription.h) defines metadata
and snapshots; ordinary implementation lives in
[ModelDescription.cpp](../../src/core/ModelDescription.cpp).

Each class owns one function-local `static const ModelDescription`. All instances
share the metadata, while values and defaults stay in existing C++ members.
The table owns its names and descriptions. Parameter and flag declarations take
a name list: the first name is canonical and the others are aliases. Aliases do
not duplicate stored values.

Descriptions follow semantic ownership, not the location of storage. CoreObject
therefore does not describe its spare `extra_bool`/`extra_bool2` slots. A child
using a slot declares its actual meaning when that model is migrated.

For example, ExponentialLoad declares an exponent:

```cpp
parameter({"alphap", "ap"}, &ExponentialLoad::alphaP, units::one,
          "Voltage exponent for real power")
```

Numeric parameters reject NaN and infinity before any optional validator runs.
Common range rules are named callables:

```cpp
parameter({"tr"}, &ExciterESST3A::Tr, units::second,
          "Voltage measurement time constant", parameterValidators::positive)
parameter({"tr"}, &ExciterEXAC1::Tr, units::second,
          "Voltage measurement time constant", parameterValidators::nonnegative)
```

Load power uses the same declaration, without extra policy:

```cpp
parameter({"p", "load p"}, &GridLoad::P, units::puMW,
          "Real power at nominal voltage")
```

Fixed-size indexed parameters use the same declaration with an element index.
Each named element is captured and restored separately:

```cpp
parameter({"t4"}, &GovernorIeeeG1::turbineTime, 0, units::second,
          "Turbine stage 4 time", parameterValidators::nonnegative)
```

Governor tables follow the class hierarchy. Direct scalar and indexed setters
use `trySetLocal`; setters that update a block, use a machine-specific base, or
recalculate dependent limits remain in the model class. Solver vector entries
use `state(...)`, while initialized reference offsets and transition history
use `localState(...)`.

## Ordinary access and class-specific handling

Public callers continue to use `set(name, value, units)`, `get(name, units)`,
`setFlag` and `getFlag`. Model classes handle their special cases before offering
a name to the default table helper. The declaration does not need to know
whether the class handled a call.

For example, GridLoad handles P/Q through its existing power-base conversion and
`setP`/`setQ` methods before calling `trySetLocal`. Its `qratio` setter uses the
common converted assignment, then enables power-factor control after success.
GridComponent retains base propagation in its own setters. Corresponding getters handle model-specific
units or calculated meanings before default dispatch.

A direct parameter, such as an exponent, can use the common helper immediately:

```cpp
if (isSetHandled(ExponentialLoad::getModelDescription().trySetLocal(*this, param, val, unitType))) {
    return;
}
// Handle alpha/a, then delegate to GridLoad::set.
```

The qualified accessor selects this class's table. The helpers deliberately do
not redispatch to a derived table or a parent. An unknown local name returns
unhandled; invalid direct values throw immediately. Getter helpers return empty
optionals for unhandled names. Parents continue through the existing C++ methods.

The helpers perform direct member access and are implementation tools for model
classes. External tools and GUIs use the model's public methods so class-specific
behavior runs. There are no custom-setter callbacks or inferred unit bases.

Direct numeric access follows:

```text
name/alias -> unit conversion -> typed value/validator -> member assignment
```

`units::defunit` retains native-unit behavior. Validators receive only the
converted proposed value, without a model object. Captureless lambdas and
static/free functions fit the contract; validators must not capture an instance
or depend on other parameters. Numeric values must be finite and representable
in their integer, real or clock storage before a validator runs. Snapshot
restoration uses the stored value directly and does not call parameter setters.
Validator logic cannot be introspected for GUI ranges.

Empty names and names beginning with `#` are accepted as no-op inputs by both
`trySetLocal` overloads, without examining the value or delegating to a parent.
Other numeric calls check finiteness before field-name lookup, including names
that would otherwise be delegated to a parent.

### Setter dispatch results

`trySetLocal()` returns the single `SetResult` enumeration in every model:

| Result            | Value | Meaning                                             |
| ----------------- | ----- | --------------------------------------------------- |
| `NO_MATCH`        | 0     | Continue class-specific handling or parent dispatch |
| `PARAMETER`       | 1     | Conversion, validation, and assignment completed    |
| `FLAG`            | 2     | Boolean dispatch through `setFlag()` completed      |
| `COMMAND`         | 3     | A declared operation for the class to execute       |
| `INDEXED_COMMAND` | 4     | An indexed operation for the class to execute       |
| `IGNORED`         | 10    | An empty/comment name, handled before value checks  |

`isSetHandled()` recognizes the completed results. Exact numeric Boolean commands
return `FLAG` after their existing virtual flag dispatch. Setters that only need
to distinguish completed operations use `isSetHandled(trySetLocal(...))`. Setters
with custom operations can branch directly on `COMMAND` or `INDEXED_COMMAND`.
A pending command must execute before returning; it is not a completed assignment.
Invalid inputs continue to throw existing exceptions.

`indexedCommand({"output"}, units::defunit, "Output mapping")` describes a
command prefix followed by one or more decimal digits that fit `index_t`.
Exact names within the same table take precedence. The prefix is exposed by
`names()`, and the field kind `INDEXED_COMMAND` identifies the matching rule for
external consumers.
An unsuffixed operation can have a separate exact declaration with the same name.
The class extracts the index and checks its own bounds; the result carries no
index or field object. Indexed commands are excluded from field snapshots.
Declarations include supported underscore and hash separators as
prefix aliases, for example `a`, `a_`, and `a#`.

## Flags

Flags are named Boolean controls and take no unit argument:

```cpp
flag({"fixedshunt"}, &GridLoad::fixedShunt, "Fixed-shunt classification")
flag({"usepowerfactor"}, &GridLoad::opFlags, USE_POWER_FACTOR_FLAG,
     "Power-factor control enabled")
```

`trySetFlagLocal`/`tryGetFlagLocal` supply default Boolean member access.
Numeric `trySetLocal` calls also recognize declared flags: zero means false,
and any other finite value means true. They dispatch through the model's
`setFlag` method so class-specific flag behavior still runs.
Numeric `tryGetLocal` calls return `1.0` or `0.0` for declared flags.
A class with additional behavior, such as enabling power-factor control or
notifying an update scheduler, handles that case first in its public method.
Packed controls are read and restored one bit at a time; unrelated bits remain
untouched.

Flags have a separate `flagNames()` discovery list and `hasUnits()` is false.
The flag declaration itself has neither numerical values nor units. Classes
may keep special numeric flag validation, such as requiring exactly zero or one.

## Default numeric setters

Simple numeric setters inherit dispatch instead of repeating the same table lookup
and parent call. `Class::getModelDescription().trySet(object, name, value, units)`
uses the same derived-to-declaring-table boundary as getters. It retains the
`SetResult` enumeration: parameters and flags are applied, ignored inputs complete,
and only commands declared in the receiving table are returned for that class to
execute. Commands in derived tables fall through as their removed forwarding
setters did; they must not be mistaken for a parent's own command.

`GridPrimary`, `GridSecondary` and `GridSubModel` provide shared dispatch above
GridComponent's base-setting propagation and assignment restrictions. Setters with
custom conversions, validation, limit updates, synchronization or deliberately
skipped base classes keep those behaviors. In particular, internal writes with
normalized names, values or units continue to use `trySetLocal`, so they cannot
accidentally target a shadowing derived field or apply conversion twice.

Empty/comment names are ignored and nonfinite values rejected before even looking
up the object's derived table. Field validators and conversion remain in the
existing local assignment mechanism. Custom string setters are preserved;
inheritance uses `using Parent::set` where needed to keep both overloads visible.
Pure string forwarders are removed only when the inherited method is equivalent.

Cache or limit updates following dispatch use `SetResult::IGNORED` to exclude
ignored input; they do not repeat empty-name or comment-prefix checks. Keep name
comparisons that select which cache to refresh, and keep input gates before
class-specific conversions that must run before ordinary member assignment.
Where the same update applies to local assignments and parent forwarding, keep
that work in one place, with ignored input returning first. Commands and indexed
commands continue through the class handler; they are not completed assignments.

## Default string setters

The string overload of `Class::getModelDescription().trySet(object, name, value)`
uses the same derived-to-declaring-table boundary as numeric dispatch. Declared
string parameters, including aliases and validators, are assigned through their
bindings. Only string commands declared in the receiving table are returned as
`COMMAND` or `INDEXED_COMMAND`; pending commands from derived tables are left for
the class that implements them. Parent handlers retain custom assignments and
side effects because the lookup stops before their tables.

GridSubModel and GridLoad provide the common string dispatch for the exciter and
load hierarchies. Empty/comment names are ignored before description lookup.
Flags and exact Boolean commands fall through to existing public string handlers,
preserving Boolean words and status changes. Declared numeric parameters and
commands reject string values. The strict local `trySetLocal` string overload is
unchanged. `using Parent::set` exposes inherited
overloads when a class retains a custom numeric setter.

## Default flag access

Models with simple flag assignments inherit `setFlag` and `getFlag` rather than
repeating local-table lookup and parent forwarding. Custom handlers use their
qualified `Class::getModelDescription().trySetFlag` and `tryGetFlag` at the existing
default-dispatch point. These helpers use the same derived-to-declaring-table
boundary as numeric getters, leaving parent behavior to ordinary method dispatch.
A stored false flag is a successful read, so it cannot fall through to a true
parent flag with the same name.

Both `flag` and `inverseFlag` declarations use default flag access. Boolean
commands, notifications, linked controls and initialization-dependent changes
continue through the class-specific handlers. Numeric setters still dispatch
flags through virtual `setFlag`, preserving those handlers. Packed flags change
only their declared bit; retained local bits are not settable flags. The local
helpers remain available for explicitly local access and standalone views.

## Inverse flag bindings

`inverseFlag(names, bitsetMember, bit, description)` has the same arguments as the
packed-bit `flag` overload. For example, `absolute` uses `flag`, while `relative`
uses `inverseFlag` on the same `USE_ABSOLUTE_TIME_FLAG` bit. Existing `flag`
declarations stay unchanged; both helpers produce ordinary Boolean `FLAG` fields.
The private bit binding selects its polarity at compile time. Readers return the
logical value, and writers and snapshot restorers convert that value back to the
stored bit. Ordinary bindings and retained local bits use the normal specialization.

Inverse controls support flag access, numeric get/set, Boolean string setters and
flag-name enumeration. Snapshots store their logical values; normal and inverse
views of the same bit restore consistently. Each binding changes only its own bit.
Custom actions remain in the models, including three-phase input/output sizing,
relay/sensor sampling changes and random-source derivative updates. Helpers based
on HelperObject and the deferred FMI/HELICS integrations are outside this migration.

## Default numeric getters

Simple models inherit numeric `get` rather than overriding it just to read their
declarations and delegate to a parent. A class with custom getter behavior uses
its qualified `Class::getModelDescription().tryGet(object, name, units)` at its
existing default-dispatch point. This helper reads from the object's most-derived
table down through that declaring table, then stops. The class continues through
its existing parent `get` when there is no declared numeric value.

Stopping at the declaring table preserves parent-specific conversions and
computed values. For example, a load's per-unit power still passes through its
power-base conversion rather than a generic read of the parent's member. Direct
parameters retain their declared units and flags return `1.0` or `0.0`. Local
memory and solver-state declarations remain outside ordinary numeric reads.

`tryGetLocal` continues to query only the receiving table. A standalone table
outside the object's description chain also uses local behavior with `tryGet`.
External clients continue to call the public `get`, so class-specific behavior
runs. Custom branches that precede the default helper retain their precedence;
GovernorGPWSCC and GovernorTG2 keep explicit getters because their `r` and
`deadband` fields override different meanings in Governor's custom getter.
GovernorGast and GovernorGgov1 also retain their valve-limit alias remapping.
Base aliases now participate in default reads; callers that require physical
units must request them. The RAW writer requests base frequency explicitly in Hz,
so both 50 Hz and 60 Hz cases export without relying on a missing-alias fallback.

## Parameter enumeration

`CoreObject::getParameterStrings` uses the object's virtual `getModelDescription`
and a shared `ModelDescription::getParameterStrings` implementation. Native
models inherit this method; they do not maintain name arrays or enumeration
overrides. Tools can also enumerate a particular declaring description directly.

- `LOCAL_NUM`, `LOCAL_STR` and `LOCAL_FLAGS` replace the supplied vector with names
  declared by that table, including aliases and command prefixes.
- `NUMERIC`, `STR` and `FLAGS` append names from that table and then its ancestors.
- `ALL` appends numeric names, one `#` separator, then string names. Flags remain a
  separate category, matching the existing discovery convention.

Ordering follows declarations within each category and the child-to-parent
chain. Repeated names, including shadowed parent names, remain repeated as in the
existing enumeration API. Solver state and retained local memory are excluded.
An empty declaring table has empty local lists and still enumerates its parents
for inherited queries.

The declarations are now the source of names and categories throughout the native
hierarchy, including CoreObject and GridComponent. Their former hand-maintained
lists no longer omit aliases or list flags as ordinary numeric/string settings.
Special set/get behavior continues in the class. FMI keeps its runtime-variable
enumeration overrides and delegates native names to the inherited implementation.

## Solver state and retained local memory

Solver-state declarations own canonical names, aliases, units and descriptions.
A bound declaration can identify a slot in the object's local state vector.

```cpp
state({"pmech", "pm"}, units::puMW, 0, "Mechanical power")
state({"valve", "v"}, units::one, 1, "Valve position")
```

These indices follow local storage order, with voltage, angle, algebraic, and
differential groups in that sequence. GridComponent translates them through the
initialized layout for the selected solver mode. A recognized state absent from
that mode returns `kNullLocation`. An inactive declaration returns `kInvalidLocation`.
Before layout initialization, a bound active state can also return `kNullLocation`.

For configuration-dependent layouts, the same declaration accepts a typed index
resolver. It receives the model and returns a local slot, or `kInvalidLocation`
when the state is inactive.

```cpp
state<MyModel>({"vmeas"}, units::one,
    [](const auto& model) {
        return model.Tr > 0.0 ? model.measurementSlot() : kInvalidLocation;
    }, "Measured voltage")
```

The resolver handles numerical layout. The declaration supplies the names.
Resolvers can call existing layout methods, as EXAC1 does for its optional
transducer, lead-lag and algebraic/differential regulator configurations. If a
model already stores the assigned index, `state` can bind its member (or an
array element) directly with a local group prefix. Inactive indices are omitted.

`component.solverStateNames()` uses these bindings to enumerate canonical names
in local vector order, excluding child objects. `findIndex(name, activeMode)`
uses the same bindings for names and aliases, without constructing that vector.
Diagnostic `getStateName` applies the active solver offsets and object prefixes.
If a solver state shares a name with an editable field, use
`find(name, ModelFieldKind::STATE)` to select the solver declaration.

Inherited declarations remain available. A derived canonical declaration replaces
its ancestor, and inherited aliases follow that replacement. The most-derived
name wins when two declarations describe the same slot. An inactive replacement
hides its ancestor. Gaps retain empty entries to preserve later indices.
Diagnostics supply generic labels for unnamed slots.

Native GridDyn models share the `solverStateNames()` implementation and reuse
inherited declarations. Machine formulations
that replace an inherited variable explicitly bind that variable to
`kInvalidLocation`, so it cannot become an alias for a different state sharing
its old slot.

Some primary models use different numerical layouts in power flow and dynamics,
or keep cached values in ordinary members rather than the object's local vector.
They use a mode-aware `state<Model>` resolver with `(model, mode)` arguments,
returning a global solver index. These owned states are named by the shared
`getStateName(names, mode)` diagnostic traversal. `solverStateNames()` enumerates
the bindings that have local vector slots.

References to another object's solver output use `stateReference<Model>`.
For example, Governor declares `pm`, `dbo`, and `w` against its internal blocks.
These references participate in `findIndex` and use the child's diagnostic label.
Object-field snapshots cover parameters, flags, and retained local memory.

`indexedState<Model>({"x"}, units, countResolver, indexResolver, description)`
describes variable-order families such as `x0` and `x1` in transfer functions.
The index resolver receives the model and zero-based element. Lookup checks the
complete decimal suffix, integer range and current family length. The descriptor
supplies the prefix for both name generation and parsing.
`namedState<Model>` binds an instance-provided label, such as a block's
`outputName`, and its index. Both the configured name and declared canonical
aliases resolve through the same field.

Effective state declarations are cached once per immutable description. Named
lookup resolves a declaration once and translates its local index through the
selected layout without constructing a name vector. Modes with unusual numerical
partitioning, such as the motor models, retain small overrides for numeric mapping.
Limit-state labels follow the allocated layout counts.

The migration corrects PID output/derivative/integral labels, ControlBlock's
`m1` alias in differential-output mode, and motor diagnostic labels in the
algebraic-only partition. Diagnostic prefixes now follow the same traversal for
links, buses, protection relays, generators and submodels. The FMI adapter remains
outside this native-model migration, consistent with its deferred integration.

The metadata-only `state(name, units, description)` overload remains available
for external descriptions. Callers of `localStateNames()` should use
`solverStateNames()`.

Solver values belong to the shared solver/HDF5 payload. The state bindings
supply names and locations for diagnostics and future checkpoint coordination.

Local state is memory needed to continue, such as a latch, hysteresis branch,
previous sample or scheduling history:

```cpp
localState("nextUpdateTime", &CoreObject::nextUpdateTime,
           "Next scheduled update", units::second)
```

These are retained values, not ordinary editable controls. Existing model
commands can still change them explicitly. Adjustable settings such as `qratio`
remain parameters even when they cannot be reconstructed from other values.

GridComponent does not dump `m_state`, `m_dstate_dt`, structural sizes or all of
`opFlags` into local-state records. Solver-backed copies and structural sizes
are reconstructed or provided by the active layout. Models declare named flags
and the retained history they actually own.

## Capture and restoration

Snapshot membership follows the declaration, with no model-specific overrides.
These are per-object field snapshots; the solver vector is a separate payload,
and a simulation-level coordinator must collect and identify every object:

| Data         | State-level field snapshot  | Parameter-level field snapshot |
| ------------ | --------------------------- | --------------------------- |
| Parameters   | Keep current values         | Restore                     |
| Flags        | Restore                     | Restore                     |
| Solver state | Restore from solver payload | Restore from solver payload |
| Local state  | Restore                     | Restore                     |

Commands and reconstructible derived values are omitted. Thus a state-only
restore keeps changed P/Q, exponents and scheduling parameters; a full parameter
snapshot restores those settings too. Flags follow one consistent rule in both
levels.

```cpp
auto saved = ModelFieldSnapshot::capture(load); // full parameter level by default
load.set("p", 0.9);
load.set("alpha", 3.0);
saved.restore(load);
```

Object-field snapshots retain exact clocks, integers, vectors and Boolean flags.
Restore checks the exact class, layout, storage types, native ranges and retained
vector lengths, staging all values before nonthrowing assignment. It bypasses
normal setters, validators, notifications and physical consistency checks.
Edited values remain the user's responsibility.

A complete coordinator must restore the selected parameters, flags, solver state
and local state, synchronize solver-backed copies through `setState`, then
rebuild derived caches with normal inputs and the active mode, reusing
`updateLocalCache` where appropriate. Refresh must not advance time, initialize
the model, reset limits/latches or overwrite restored history.

ExponentialLoad already computes power on demand from P/Q and exponents, so its
outputs reflect restoration without being saved. This object-field layer does
not yet implement the full simulation coordinator, solver history, event queues,
topology or persistent manifest. Compatible structure must remain in place;
persistent numerical storage should reuse the shared HDF5 writer.

## Inheritance and coverage

The worked description chain is CoreObject -> GridComponent -> GridLoad ->
ExponentialLoad; GridSecondary retains its existing bus relationship and parent
delegation. `find` searches the local table before ancestors. Child names may
shadow parent names; snapshot keys include the declaring class. Duplicate names
within one table are rejected. Exact dynamic-type checks prevent an undescribed
subclass from silently receiving an incomplete snapshot through its parent's
table.

`ModelParameterTests` covers roles, aliases, flags, packed bits, units, value
bounds, special setter precedence, inheritance, exact clocks and malformed
restore. `ExponentialParameterTests` exercises the model's equations, power units,
control targets, fault notifications, role-based restoration and state lookup
through changing solver layouts.

For the current whole-simulation checkpoint boundary and implementation
options, see [Checkpointing and restore boundaries](checkpointing.md).

### Dispatch and declaration audit

Both setter overloads now return `SetResult` throughout the library. There is one
`trySetLocal` API; the temporary result helper and Boolean overloads are removed.
The common helper still examines only the declaring class. `NO_MATCH` permits
existing custom handling or parent dispatch; it does not justify skipping those
paths unless all local operations are described.

The source review covered 215 declaring tables and 690 explicit `set`, `get`,
`setFlag`, and `getFlag` implementations, including helper hierarchies. Literal
branch comparisons were checked against local and inherited declarations;
indexed parsing and forwarding aliases were examined separately. This is an
inventory, not proof of complete checkpoint coverage.

Declarations now include previously omitted core identity/scheduling operations,
connection notifications, governor and exciter forwarding aliases, saturation
selection, inverse load flags, load scaling operations and motor torque settings.
Indexed declarations cover Subsystem terminals, AggregateLoad subloads and
fractions, CompositeLoad components and fractions, FileLoad column mappings and
GridLabDLoad instance files and working directories, alongside Sensor and
TransferFunctionBlock. Existing class parsers still determine model-specific
index bases, bounds, structural changes and side effects.

The following require further work before descriptions can claim complete
external-interface and checkpoint coverage:

| Area                     | Remaining issue                                                                                                                                                                                                                                               |
| ------------------------ | ------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------- |
| Computed getters         | Counts, solver statistics, controller outputs and diagnostic values are often absent. They need a read-only description distinct from solver `state` and retained `localState`.                                                                               |
| Legacy suffix parsing    | AcLine accepts redundant numeric suffixes; ZonalRelay parses only one digit and maps other suffixes to zone zero; ThreeWindingTransformer dispatches a fixed set of suffixed leg settings. These interfaces need consistent declarations and bounds behavior. |
| Helper hierarchies       | Solvers, recorders, players, events and optimizer interfaces derive from `HelperObject` rather than `CoreObject`; they have no shared description table. The reuse design for helper descriptions is in [checkpointing.md](checkpointing.md). |
| Command-backed data      | Describing a command does not save its values. Fractions, transfer-function coefficient arrays, relay zones, internal block settings and external-file configuration need underlying parameter/state bindings and restore coordination.                       |
| Retained runtime history | Controller integrals, relay pending actions, file cursors and random-source state require a separate history audit.                                                                                                                                           |
| Inherited restrictions   | Some models reject settings advertised by a parent table, such as REECB1 restrictions on REECA1 settings. Effective external descriptions must account for that.                                                                                              |
| Overloaded command types | CoreObject accepts numeric IDs and also uses a string `id` as a rename operation. A command currently advertises one value type.                                                                                                                              |
| External integrations    | FMI and HELICS remain deferred.                                                                                                                                                                                                                               |

Do not declare computed outputs as retained memory merely to list their getters.
Likewise, a command declaration must not be taken as evidence that the operation's
configuration or runtime history is included in a field snapshot. Restore tests
must verify the resulting model behavior as those bindings are added.

## Optimization model objects

GridOptObject and its area, bus, generator, link, load and relay adapters use the
same description hierarchy and inherited dispatch as the simulation models.
Even adapters without local settings declare their exact type so object-field
snapshots can distinguish them. The physical model returned by `sourceObject()`
is a separate object; its settings are not copied into the adapter's table.

Generator heat rate and fuel cost are ordinary parameters with shared set/get,
alias enumeration and field capture. Indexed `pN`/`qN` cost edits, named cost
coefficients, forecast, penalty cost and forwarded generation limits remain
commands. Their conversions use the attached generator's system power base
(or a base of one for an unattached adapter). Physical cost coefficients are
scaled by the power base raised to the polynomial order, matching MATPOWER
imports. Explicit native per-unit coefficients retain their existing scale.
These commands describe the existing interface; they do not capture cost curves
or their backing arrays.
Restoring cost curves needs bindings for the source-format curve and coordination
with the converted coefficients. Neither these commands nor the adapter tables
claim a complete optimization checkpoint or an optimizer warm restart.

The reserved voltage/angle tolerance commands on area, bus, link and relay
adapters remain explicitly documented as ignored. Computed object counts remain
in the classes. Offsets, object relationships, optimizer-owned decision vectors,
multipliers and backend history need their own restore coordination;
OptimizerInterface still uses the separate HelperObject hierarchy.
