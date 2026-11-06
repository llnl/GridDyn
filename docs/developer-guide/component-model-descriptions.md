# Component model descriptions

The native loads, links and transformers, buses, generator containers, excitation
limiters, voltage compensators, and renewable/converter models now provide
`ModelDescription` tables. Relays, general-purpose blocks, and controllers remain
for a later migration. The shared `GridPrimary`, `GridSecondary`, and `GridSubModel`
layers also identify their own types and preserve the existing parent dispatch.

The public interface remains `set(name, value, units)` and `get(name, units)`.
Simple scalar assignments, aliases, unit conversion, and value-only validation
use the declarations. `getParameterStrings()` uses the same tables. Leaf classes
with no additional fields still declare their type so object-field snapshots do
not silently treat a derived object as its parent.

## Structured parameter storage

Small parameter structures can stay in the model. A declaration names the owning
member and the member inside that structure:

```cpp
parameter({"p1"}, &WSCCLoad::parameters, &WSCCParameters::p1,
          units::one, "WSCC constant-impedance real-power coefficient")
parameter({"a7", "pfrq"}, &ElectronicLoad::parameters,
          &IEELParameters::coefficients, 6, units::one,
          "Real-power frequency coefficient")
```

These use the same conversion, typed validation, staged restoration, and alias
rules as direct members. Array bounds are checked when constructing the table.
The declarations do not introduce a second parameter store. Direct arrays and
arrays in parameter structures share the numeric binding implementation.

`parameterValidators::unitInterval` describes values in `[0, 1]`;
`parameterValidators::binary` describes numeric selectors restricted to 0 or 1.
Integer storage already rejects fractional and out-of-range values. A selector
may still need a small range validator, but does not need another integer check.

## Behavior retained in the classes

The declarations do not replace algorithms or infer object-specific unit bases.
The following operations intentionally keep class code:

| Family | Behavior |
| --- | --- |
| Loads | Base-dependent power/current/impedance conversion; power-factor changes; component allocation; scheduling and source forwarding; motor torque and saturation updates; switched-shunt bank operations. |
| Links and transformers | Admittance refresh after impedance changes; faults and breaker changes; tap clamping and initial tap bookkeeping; winding and subsystem forwarding; converter control-block updates. |
| Buses and generator containers | Voltage/power propagation to attached models; control targets; topology and remote-bus selection; governor/machine parameter forwarding; structural changes. |
| Excitation limiters | Role checks; explicit-threshold bookkeeping; configured curve length; delayed limiter activation. |
| Renewables | Input-layout and root-count changes; initialization-reference bookkeeping; variant-specific restrictions and dependent limits; external measurement source selection. |

For a directly bound value that also needs a small update, the model performs
that update after successful table dispatch. For example, REECA1 updates its
input layout after a voltage/reactive/speed selector changes. Its derived models
still receive control through their existing setters and can apply additional
restrictions or layout adjustments. Empty and comment names must return without
performing these updates.

Compound operations use `command(...)` discovery entries instead of a fictitious
stored field. String commands can declare `ModelFieldType::STRING`, for example
the limiter role or bus type. They appear in string discovery, while the class
continues to implement their interpretation. Commands are not snapshot records.
Variable-length indexed interfaces still belong to their models; a command such
as `fraction` describes that interface, not a fixed array length.

Declared Boolean flags support `setFlag`, `getFlag`, and numeric `set`/`get`.
Their numeric values are 0.0 and 1.0, with any finite nonzero input meaning true.
Existing flag side effects run through virtual `setFlag`. Inverse convenience
aliases, such as file-load `relative` and `interpolate`, retain their handwritten
behavior. Binary numeric selectors remain parameters so invalid values such as
0.5 are rejected rather than coerced into a different model configuration.

Renewable setters retain case-insensitive names. The common lowercase path first
uses table dispatch; a normalized retry handles uppercase names and custom
operations. Consequently the numeric finiteness check still precedes string
normalization, and empty/comment names remain no-ops.

## Snapshot boundaries and follow-up review

The tables distinguish solver variables from retained local history. Conditional
solver-state declarations list possible names; `localStateNames()` and
`findIndex(name, activeMode)` remain authoritative for the active layout and order.
Held renewable commands, limiter timers, motor protection latches, and configured
reference bookkeeping are examples of retained model history.

Object-field snapshots are still not complete component or simulation restart
images. In particular:

- Compound commands do not automatically capture their arguments, referenced
  objects, variable-length configuration, or child objects.
- Restoring raw coefficients does not refresh admittance, saturation objects,
  frequency-dependency flags, input layouts, or other derived data. Restoration
  needs a later reconstruction step before continuing a simulation.
- Solver integration history, active partitions, network assembly, external
  files, and event queues remain outside object-field snapshots.

A later systemic review should address discovery of inherited aliases without
duplicating captured values, inverse flag aliases, overloaded numeric/string
names, parameter-dependent layouts, and reconstruction after raw restoration.
That review follows the relay/block/controller migration rather than being
folded into this component conversion.

`testComponentDescriptions.cpp` checks the concrete types across these families,
alias/getter agreement, Boolean dispatch, no-op keys, and field snapshots.
Focused tests exercise units, structured coefficients, layout side effects,
limiter curves, and transformer forwarding. `testModelParameters.cpp` covers the
structured binding and atomic restoration error paths.
