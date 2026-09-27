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

## Next ANDES renewable batch

GridDyn has `REGCA1`, `REECA1`, `REECB1`, `REPCA1`, `WTDTA1`, `WTDS`,
`WTARA1`, `WTPTA1`, `WTTQA1`, and the no-PLL `REGCP1` path. The remaining
models registered under `andes.models.renewable` are `WTARV1`, `REECA1E`,
`REECA1G`, `REGCV1`, `REGCV2`, and `REGF1` through `REGF3`; the `REGCP1`
PLL path is also pending. Andes also
registers `PVD1` under `andes.models.distributed`; it is an integrated PV
generator relevant to renewable coverage. These names are distinct model
implementations, not aliases for the existing GridDyn classes.

| Order | Models | GridDyn fit and prerequisite | Validation source |
| --- | --- | --- | --- |
| 1 | `REECA1` speed branch and `WTDS` | Add the `PFLAG=1` speed-dependent active-power path, with a declared generator-speed input and its Jacobian. Then add `WTDS` in the existing drivetrain role: one speed state with electrical power, mechanical power, and speed-reference inputs; generator and turbine speed outputs can refer to the same state. Do not copy ANDES' unused dummy shaft state. | `andes/cases/kundur/kundur_wtds.xlsx`; compare an initialized state and a power or pitch disturbance. |
| 2 | `REGCP1` without PLL | A separate terminal electrical class can share `REGCA1` current dynamics. Without a PLL its d/q frame reduces to bus voltage and reproduces `REGCA1`. Explicitly reject a nonempty PLL reference until the measurement connection in step 3 is available. | `andes/cases/ieee14/ieee14_regcp1_nopll.json`; check equivalence to `REGCA1` for the same parameters. |
| 3 | `REECA1E`, `REECA1G`, and `REGCP1` PLL path | Add external measurement binding with state locations and Jacobian terms; the current host only resolves terminal voltage/angle and component ports. Separate electrical-control classes can reuse `REECA1`: `REECA1E` adds `-Kf*df-Kdf*dfdt` to active reference from bus frequency/ROCOF; `REECA1G` adds `-Kf*(omega-1)` from a named synchronous machine. Finish `REGCP1` with measured angle, rotated d/q voltage, and P/Q derivatives. | Construct small ANDES frequency and machine-speed event cases, check zero-gain equivalence to `REECA1`, and use `andes/cases/ieee14/ieee14_regcp1.xlsx` for PLL behavior. |
| 4 | `PVD1` | A self-contained `TerminalElectricalModel` with current lags, P/Q priority, voltage and frequency response, and trip/recovery logic. It needs the frequency measurement and optional remote-bus binding from the preceding step. | `andes/cases/ieee14/ieee14_pvd1.json` and associated cases, including voltage/frequency trip and recovery events. |
| 5 | `REGCV1/2`, then `REGF1/2/3` | Self-controlled grid-forming terminal models. Reuse terminal P/Q and d/q network algebra where equations agree, while keeping VSG, droop, VSM, oscillator, and inner-loop variants separate. `REGF2` additionally needs a PLL frequency input. These models do not require `REECA1` current-command ports. | Create small ANDES cases because this checkout contains no dedicated case files for these five models; compare initialization, voltage steps, frequency response, and DAE Jacobians. |

`WTARV1` is not in the implementation queue yet: its ANDES class says work is in
progress and defines a pitch algebraic variable but no wind-velocity to
mechanical-power equation. Require a completed equation source before giving
it an executable GridDyn mapping. The separate `WT3*` models in ACTIVSg25k
remain a separate case-coverage task; the newer ANDES wind models do not
provide their equations.

### Batch 1 implementation notes

`WTDS` uses one speed state for both turbine and generator speed, with
electrical-power and optional mechanical-power and speed-reference ports. If
mechanical power is absent, it holds its initialized electrical power. The
`REECA1` `PFLAG=1` branch now requires a generator-speed provider, filters
`Pref / wg`, and selects `wg * pfilt` for the power-order lag. The host
initializes the shaft before the speed-dependent control and reconciles the
control again after mechanical initialization. `PFLAG=1` with `Tpord=0`
remains explicitly rejected because the current bypass output has no
speed-dependent output Jacobian. `REGCP1` is a distinct loadable terminal
model using the `REGCA1` equations for its no-PLL case; a nonempty PLL
selection is rejected until the measured-angle path is implemented.
The DYR reader accepts `REGCP1` with the 15 numeric `REGCA1` parameters
(implicitly no PLL) and `WTDS` with `H`, `D`, and `w0`.

The two-bus fault fixture now includes an ANDES-generated trajectory for
`REGCP1` (no PLL), `REECA1 PFLAG=1`, and `WTDS`. It samples terminal voltage,
converter P/Q, current commands, and shaft speed before, during, and after
the fault. The fixture and regeneration command are documented in
`test/reference/renewable_fault/README.md`. The fault produces about 0.003 pu
of shaft-speed excursion, enough to exercise speed feedback; a separate
mechanical-power disturbance should later cover a larger operating range.

### Measurement binding and frequency controls

The first measurement-binding path now serves `REECA1E`. `REECA1G` and the
`REGCP1` PLL variant can reuse its source resolution and solver-location rules.
These models must remain
independently selectable in dynamics files; a missing measurement or machine
reference is a load/assembly error, not a reason to substitute `REECA1` or
no-PLL `REGCP1`.

| Step | Implementation | Model behavior and validation |
| --- | --- | --- |
| 1 | Measurement binding contract | Resolve an explicitly named bus or machine measurement to a value and DAE state location, including partitioned solver modes. Add provider identity, uniqueness, initialization order, base/unit checks, and missing-provider diagnostics. `RenewableGenerator` already sees terminal frequency in bus inputs, but its port mapper only connects voltage and angle. Review whether `AcBus`'s continuous frequency filter or `Pmu` can supply the required filtered frequency and ROCOF with correct Jacobians; do not rely on a sampled output without a continuous solver dependency. |
| 2 | `REECA1E` | Add a separate electrical-control class with `Kf`, `Kdf`, and an optional bus ROCOF device reference. Share `REECA1` limits, dip logic, and states through a protected active-reference hook. ANDES adds `-Kf*df-Kdf*dfdt` **after** `Pref0/wg`; preserve that placement, test each gain alone and together, and compare a two-bus frequency disturbance against ANDES `BusROCOF`. |
| 3 | `REECA1G` | Reuse the active-reference hook for `-Kf*(omega-1)`, binding `sg` to a specific synchronous generator speed state. Reject missing, ambiguous, or renewable-only machine references. Test a generator-speed event, DAE Jacobian, DYR loading in either record order, and zero-gain equivalence to `REECA1`. |
| 4 | `REGCP1` with PLL | Keep `REGCP1` as a separate terminal model. Bind a named measured-angle provider, then use `vd=V cos(a-am)` and `vq=-V sin(a-am)` for the current-frame P/Q equations; retain the present no-PLL path when no provider is selected. Factor the shared `REGCA1` current lag and limiter code instead of copying it. Compare angle-step and fault trajectories with ANDES `PLL1` and `ieee14_regcp1.xlsx`, including angle derivatives in the DAE Jacobian. |

The generator-local portion of step 1 is implemented, as is step 2. A
`BusROCOF` renewable component takes terminal angle and exposes filtered
frequency deviation and ROCOF as continuous DAE algebraic outputs. `REECA1E`
is a separate electrical control with named input ports and adds
`-Kf*df-Kdf*dfdt` to the active reference after `Pref0/wg`. The host resolves
provider name, signal, and base, checks required ports during assembly, and
passes solver state locations for analytic Jacobian terms. It initializes and
steps the measurement before the electrical control. Terminal frequency is
also available to future components as a host input.

`BUSROCOF` and `REECA1E` load independently from DYR records in either order.
The REECA1E record extends REECA1 with `Kf`, `Kdf`, and a quoted measurement
name. Missing or incorrectly named measurements fail assembly, and nonzero
remote `BUSR` fails loading. Tests cover each gain alone and together, zero
gain behavior, measurement and control Jacobians, DYR order, and ANDES
comparison. The two-bus fault checks voltage, P/Q, current commands,
frequency deviation, and ROCOF. A separate test replays the exact ANDES bus
angle waveform through `BusROCOF`, avoiding network angle differences when
checking the filter equations; see
`test/reference/renewable_fault/README.md`.

The zero-gain test now compares `REECA1E` directly with `REECA1` through a
varying-voltage trajectory while supplying nonzero frequency inputs. A paired
solver-mode test checks measurement values across the algebraic/differential
state split and verifies that the differential-only Jacobian does not assign
algebraic measurement columns to unrelated differential states.

The binding currently supports one generator-attached measurement role. It
does not yet resolve a separately attached bus PMU or a shared remote
measurement. The next set should extend the binding to an external named
source with explicit ambiguity and remote-bus checks, then implement
`REECA1G`'s synchronous-machine speed reference and `REGCP1`'s PLL path.
Their DYR records must retain explicit model selection. `PVD1` follows because
it also needs frequency measurements; the grid-forming `REGCV*` and `REGF*`
families remain after that.
