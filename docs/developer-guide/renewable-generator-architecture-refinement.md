# RenewableGenerator architecture refinement

## Implemented scope (September 2026)

`RenewableGenerator` hosts independent `RenewableComponent` instances. The
first eight concrete models are `REGCA1`, `REECA1`, `REECB1`, `REPCA1`,
`WTDTA1`, `WTARA1`, `WTPTA1`, and `WTTQA1`. Each has its own factory identity, parameters,
states, residual, Jacobian, and DYR reader entry. A renewable DYR record
converts a steady-state `Generator` to a `RenewableGenerator` in its existing
bus position. Records may appear in any order. Duplicate roles, missing
signal providers, and unsupported model modes fail before dynamics start.

The implemented converter path is `REPCA1` incremental P/Q references to
`REECA1` or `REECB1` current commands to `REGCA1` terminal P/Q. `REECB1` has
flat current limits. The wind path connects
`REGCA1` electrical power to `WTDTA1`, `WTTQA1` torque reference to the
selected electrical controller,
and optionally `WTPTA1` pitch to `WTARA1` mechanical power. The host converts
terminal P/Q between machine and system bases and reconciles the wind shaft
and torque controller at a nonunity initial operating speed. The component
tests cover DYR assembly in either order, steady residuals, DAE Jacobians
across the electrical and mechanical connections, voltage and pitch response,
invalid assemblies, and solar/wind network integration on the IEEE 14-bus case.

The present equations cover the main local modes. `REECA1` accepts its
constant-Q branch (`PFFLAG=QFLAG=PFLAG=0`); `REECB1` accepts the corresponding
branch (`PFFLAG=QFLAG=0`). Unsupported branches are rejected.
`REPCA1` accepts local voltage/reactive control and rejects frequency,
compensated remote, and monitored-line modes. `REGCA1` rejects nonzero
`Accel`. `WTDTA1` requires a two-mass shaft (`0 < Htfrac < 1`); a one-mass
alternative is still needed. `WTARA1` uses a linear pitch-to-mechanical-power
response, and `WTPTA1`/`WTTQA1` implement their PI and limit behavior in
GridDyn state form. They have not yet been benchmarked against an ANDES
transient case. Remote measurement bindings, Type-1/Type-2 wind generator
electrical models, and turbine-base conversion are future extensions.

## Decision

Use `RenewableGenerator : Generator` as a small composition host. Give it one
required terminal electrical model and a collection of optional components.
Keep `REGCA1`, `REECA1`, `REPCA1`, and every wind dynamics-file model as separate
concrete `GridSubModel` objects and factory identities.

The earlier proposal had seven abstract role bases and a long `dynamic_cast`
chain in `add`. Most of those bases supplied no equations or reusable behavior;
each new role would enlarge the host. A more useful hierarchy is:

```text
GridSubModel
└── RenewableComponent                  role and typed port contract
    ├── TerminalElectricalModel          required terminal P/Q contract
    │   ├── REGCA1, REGCP1               current-command family
    │   ├── REGCV*, REGF*, PVD1           where their terminal contract fits
    │   └── future WT1G, WT2G            induction-generator family
    ├── REECA1, REECB1, ...               electrical control
    ├── REPCA1                            plant control
    ├── WTDS, WTDTA1, future WT1T/WT2T   drivetrain/turbine
    ├── WTARA1, WTARV1                    aerodynamics
    ├── WTPTA1, future WT1P/WT2P         pitch control
    ├── WTTQA1                            torque/reference control
    └── future WT2E                       rotor-resistance control
```

`RenewableComponent` earns its place by giving the host a stable role key and
typed connection declarations. `TerminalElectricalModel` earns its place by
enforcing the one electrical boundary every renewable assembly shares:
terminal network conditions in, terminal P/Q out, solved-operating-point
initialization, and output derivatives. It must not assume current commands
internally. A grid-forming voltage-source model can satisfy the same terminal
boundary while owning different internal states and equations.
`RenewableComponent` ports handle signals between attached components;
frequency/ROCOF, PLL angle, and synchronous-machine speed require explicit
external measurement-provider bindings. Other role classes should be
introduced only if multiple implementations actually share code or a
substantial invariant. Models with shared equations may have private family
bases or helpers without losing their separate factory names and dynamics
records.

## Model roles and ports

One component advertises a `Role` such as `electrical`, `electrical_control`,
`plant_control`, `drivetrain`, `aerodynamics`, `pitch`, `torque`, or
`rotor_resistance`. The role selects an attachment slot and its cardinality;
it does not select equations. A concrete component also declares its required
and provided signals. A small typed descriptor extends GridDyn's existing
`inputNames()`, `outputNames()`, `inputUnits()`, and `outputUnits()` with signal
meaning and per-unit base:

```cpp
struct RenewablePort {
    SignalId signal;        // e.g. ActiveCurrentCommand, ElectricalPower
    PortDirection direction;
    BaseKind base;          // system, machine, turbine, or physical unit
    bool required;
    index_t ioIndex;
};

class RenewableComponent : public GridSubModel {
  public:
    virtual RenewableRole role() const = 0;
    virtual std::span<const RenewablePort> ports() const = 0;
};

class TerminalElectricalModel : public RenewableComponent {
  public:
    // getOutputs()[0] and [1] are terminal generation P and Q on machine base;
    // outputPartialDerivatives supplies their solver derivatives.
    // Implementations may declare additional model-specific ports.
};
```

This is a design sketch, not an implementation patch. Use typed signal IDs,
not string equality or concrete model names, for automatic binding. Define
separate IDs for a baseline `Pref` and the incremental `Pext` from `REPCA1`;
likewise distinguish electrical power from mechanical power and torque.
Declare optional inputs only where a model actually uses them. For example,
ANDES `WTARA1` is a simplified pitch-to-mechanical-power model; it does not
require an explicit wind-speed input. A richer aerodynamic model could declare
one later.

External dependencies fit the same declaration: `REECA1G` can request speed
from another generator, while `REPCA1` can request a remote bus voltage or
line-flow measurement. Such bindings need both current values and solver
derivatives; a scalar grabber alone is insufficient for the Jacobian. They
do not require new `RenewableGenerator` subclasses.

The terminal contract must not require current commands. `REGCA1` declares
`Ipcmd`/`Iqcmd`; it can hold commands initialized from the power-flow point
when no electrical controller is attached, if that supported operating mode
is implemented explicitly. An integrated grid-forming converter may not
expose these ports at all. A Type-1/Type-2
induction model declares mechanical coupling, and a Type-2 model additionally
declares an external rotor-resistance input. WECC describes Type-1/Type-2
generator, turbine, pitch, and resistance-control modules in its
[wind plant dynamic modeling guide](https://www.wecc.org/sites/default/files/documents/meeting/2024/WECC%20Wind%20Plant%20Dynamic%20Modeling%20Guidelines.pdf).
GridDyn's `MotorLoad` family is a `GridLoad`, with motor/load sign and
initialization assumptions; useful calculations can be extracted later, but
the class cannot be attached directly as a generator submodel.

## Host responsibilities

The host owns local components, keeps one `TerminalElectricalModel*`, and
indexes all components by role. It owns the single bus-facing sign and base
conversion. It does not contain any named model's equations or Type-1 through
Type-4 switches. `add` is an ownership and role operation; connection binding
is a separate step after dynamics records have been loaded:

```cpp
void RenewableGenerator::add(GridSubModel* object)
{
    if (object == nullptr || ownedByAnotherHost(object)) {
        throw ObjectAddFailure(this);
    }
    auto* component = dynamic_cast<RenewableComponent*>(object);
    if (component == nullptr) {
        throw ObjectAddFailure(this);
    }
    const auto role = component->role();
    if (role == RenewableRole::electrical &&
        dynamic_cast<TerminalElectricalModel*>(component) == nullptr) {
        throw ObjectAddFailure(this);
    }
    replaceOwnedRole(role, component); // ownership, locIndex, terminal pointer
    bindingsDirty = true;
}
```

No `REGCA1`, `REECA1`, or wind model name appears in the host's `add`,
initialization, residual, or Jacobian branches. A new model in an existing
role only needs its concrete implementation, factory entry, file parser, and
port declaration. A genuinely new signal or role extends the vocabulary and
binding policy, while keeping the host's traversal loop the same. Existing
`Source`/`Scheduler` can remain special inputs or be adapted as signal
providers; do not force their inheritance into the renewable hierarchy.

At binding time, resolve each required input to exactly one compatible
provider, an explicit external source, or a documented constant. Convert
bases in one place. Explicit connections override defaults when several
providers could supply a signal; ambiguity is an error. Keep component
ownership separate from non-owning connections to network measurements or
other externally owned controllers. This permits a later shared plant
controller without making it a requirement of the first implementation.
`remove` clears the role index and terminal pointer; `clone` rebuilds both
from cloned children and resolves non-owning references by stable object
identity, rather than copying raw pointers.

The host builds per-component `IOdata` and `IOlocs` from a compiled binding
table. `GridComponent` already traverses child state sizes and offsets, but
its default residual/Jacobian traversal passes the _same_ input array to every
child. Renewable components need different input arrays, so the host still
needs one generic evaluation loop. Outputs must be taken from the current
`StateData`, including partitioned algebraic states, and derivatives of
computed signals must be propagated into the Jacobian. Merely calling
`timestep` in signal order would break algebraic feedback.

## Validation and initialization

At `add`, reject a null or cross-owned object and an invalid role. Apply the
role's cardinality and replacement policy; direct replacement is allowed during
configuration; dynamics-file readers report accidental duplicate records
before calling `add`. Defer cross-component checks because file record order
is arbitrary and two compatible models may be replaced in separate calls.

Before dynamic initialization, require one terminal electrical model and
check every required port, source uniqueness, units/base conversion, remote
bus or line reference, and any model-specific prerequisites. Check that every
attached control component has a path to a terminal or mechanical effect, so
an accepted dynamics record cannot be silently inert. Examples:

| Assembly                   | Result                                                                                                   |
| -------------------------- | -------------------------------------------------------------------------------------------------------- |
| `REGCA1` + `REECA1`        | Bind active/reactive current commands to the converter.                                                  |
| `REGCA1` alone             | Use an explicit initialized-command hold mode if supported; never substitute an undocumented zero input. |
| `REGCV` without `REECA1`   | Valid when its terminal model declares autonomous controls.                                              |
| `REGCV` + `REECA1`         | Reject unless that converter accepts current commands.                                                   |
| `REPCA1` + `REECA1`        | Bind incremental active/reactive references only if the controller accepts them.                         |
| Future `WT1G` with turbine | Bind mechanical torque/power and speed; do not demand converter controls.                                |
| Future `WT2G` + `WT2E`     | Bind external rotor resistance; reject `WT2E` with an electrical model lacking that port.                |

Initialization uses the power-flow P/Q as the terminal target, then solves
or iterates the coupled controls and mechanics to a consistent equilibrium.
Replacing or removing a component invalidates the binding table and dynamic
solver layout. Diagnostics should name the bus, machine ID, component type,
and missing or conflicting signal.

## Why this hierarchy

| Option                                                       | Assessment                                                                                                                                    |
| ------------------------------------------------------------ | --------------------------------------------------------------------------------------------------------------------------------------------- |
| Seven abstract role subclasses and seven typed host pointers | Clear but repetitive; each new role changes the host and many subclasses have no shared implementation.                                       |
| One `RenewableComponent` plus one `TerminalElectricalModel`  | Recommended: a small common composition contract and a real electrical invariant; concrete controls stay independent.                         |
| Fully general signal-graph framework                         | More flexibility than the present model set requires; raises solver and Jacobian complexity across GridDyn. Keep a local binding table first. |

## Next verification and extension work

Run transient cases against independently known renewable responses,
including voltage dips, torque and pitch events, and multiple generator
ratings. Add partitioned solver tests with both algebraic and differential
state arrays, and extend the network integration check to events and longer
time horizons. Compare the simplified aerodynamic and
controller equations with the intended source model over those cases before
claiming parameter-level equivalence. A test-only induction electrical model
would then exercise the same host with Type-1/Type-2 mechanical ports without
converter current commands.
