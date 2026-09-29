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

At the time this batch was planned, GridDyn had `REGCA1`, `REECA1`, `REECB1`,
`REPCA1`, `WTDTA1`, `WTDS`, `WTARA1`, `WTPTA1`, `WTTQA1`, `REECA1E`, and both
`REGCP1` paths. The outstanding models registered under `andes.models.renewable`
were `WTARV1`, `REECA1G`, `REGCV1`, `REGCV2`, and `REGF1` through `REGF3`. Andes also
registers `PVD1` under `andes.models.distributed`; it is an integrated PV
generator relevant to renewable coverage. These names are distinct model
implementations, not aliases for the existing GridDyn classes.

| Order | Models                                      | GridDyn fit and prerequisite                                                                                                                                                                                                                                                                                                                                                                                                             | Validation source                                                                                                                                                                    |
| ----- | ------------------------------------------- | ---------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------- | ------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------ |
| 1     | `REECA1` speed branch and `WTDS`            | Add the `PFLAG=1` speed-dependent active-power path, with a declared generator-speed input and its Jacobian. Then add `WTDS` in the existing drivetrain role: one speed state with electrical power, mechanical power, and speed-reference inputs; generator and turbine speed outputs can refer to the same state. Do not copy ANDES' unused dummy shaft state.                                                                         | `andes/cases/kundur/kundur_wtds.xlsx`; compare an initialized state and a power or pitch disturbance.                                                                                |
| 2     | `REGCP1` without PLL                        | A separate terminal electrical class can share `REGCA1` current dynamics. Without a PLL its d/q frame reduces to bus voltage and reproduces `REGCA1`. The named PLL case is completed in the measured-angle step.                                                                                                                                                                                                                        | `andes/cases/ieee14/ieee14_regcp1_nopll.json`; check equivalence to `REGCA1` for the same parameters.                                                                                |
| 3     | `REECA1E`, `REECA1G`, and `REGCP1` PLL path | Add external measurement binding with state locations and Jacobian terms; the host resolves terminal signals, component ports, and named sensors. Separate electrical-control classes can reuse `REECA1`: `REECA1E` adds `-Kf*df-Kdf*dfdt` to active reference from bus frequency/ROCOF; `REECA1G` adds `-Kf*(omega-1)` from a named synchronous machine. Finish `REGCP1` with measured angle, rotated d/q voltage, and P/Q derivatives. | Construct small ANDES frequency and machine-speed event cases, check zero-gain equivalence to `REECA1`, and use `andes/cases/ieee14/ieee14_regcp1.xlsx` for PLL behavior.            |
| 4     | `PVD1`                                      | A self-contained `TerminalElectricalModel` with current lags, P/Q priority, voltage and frequency response, and trip/recovery logic. It needs the frequency measurement and optional remote-bus binding from the preceding step.                                                                                                                                                                                                         | `andes/cases/ieee14/ieee14_pvd1.json` and associated cases, including voltage/frequency trip and recovery events.                                                                    |
| 5     | `REGCV1/2`, then `REGF1/2/3`                | Self-controlled grid-forming terminal models. Reuse terminal P/Q and d/q network algebra where equations agree, while keeping VSG, droop, VSM, oscillator, and inner-loop variants separate. `REGF2` additionally needs a PLL frequency input. These models do not require `REECA1` current-command ports.                                                                                                                               | Create small ANDES cases because this checkout contains no dedicated case files for these five models; compare initialization, voltage steps, frequency response, and DAE Jacobians. |

The current batch implements `REECA1G` as an independent electrical-control
class with a named synchronous-machine speed input. The five `REGCV*`/`REGF*`
models share `GridFormingConverter` for terminal P/Q and d/q network algebra;
each variant has its own state and control branches and a separate factory
identity. GridDyn's local DYR schemas, ANDES two-bus references, and the
current trajectory comparison limits are documented in
`test/reference/renewable_fault/README.md`. The 0.2 ms ANDES references cover
both constant P/Q and constant impedance demand, resolve the fast `REGF*`
loops, and match the GridDyn fault trajectories within the same documented
tolerances for all five models.

`GridFormingConverter` currently forms its DAE Jacobians by finite differences.
That is covered by the component Jacobian tests, but large-network performance
has not been benchmarked for this batch. `REECA1G` resolves its named machine
speed source through the area tree during solver evaluation; that lookup may
also warrant caching if many such controllers are used in a large case.

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
model using the `REGCA1` equations for its no-PLL case. Its named PLL path is
described below.
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

The first measurement-binding path now serves `REECA1E` and the `REGCP1` PLL
variant. `REECA1G` can reuse its source resolution and solver-location rules.
These models must remain
independently selectable in dynamics files; a missing measurement or machine
reference is a load/assembly error, not a reason to substitute `REECA1` or
no-PLL `REGCP1`.

| Step | Implementation               | Model behavior and validation                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                          |
| ---- | ---------------------------- | -------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------- |
| 1    | Measurement binding contract | Resolve an explicitly named bus or machine measurement to a value and DAE state location, including partitioned solver modes. Add provider identity, uniqueness, initialization order, base/unit checks, and missing-provider diagnostics. `RenewableGenerator` already sees terminal frequency in bus inputs, but its port mapper only connects voltage and angle. Review whether `AcBus`'s continuous frequency filter or `Pmu` can supply the required filtered frequency and ROCOF with correct Jacobians; do not rely on a sampled output without a continuous solver dependency. |
| 2    | `REECA1E`                    | Add a separate electrical-control class with `Kf`, `Kdf`, and an optional bus ROCOF device reference. Share `REECA1` limits, dip logic, and states through a protected active-reference hook. ANDES adds `-Kf*df-Kdf*dfdt` **after** `Pref0/wg`; preserve that placement, test each gain alone and together, and compare a two-bus frequency disturbance against ANDES `BusROCOF`.                                                                                                                                                                                                     |
| 3    | `REECA1G`                    | Reuse the active-reference hook for `-Kf*(omega-1)`, binding `sg` to a specific synchronous generator speed state. Reject missing, ambiguous, or renewable-only machine references. Test a generator-speed event, DAE Jacobian, DYR loading in either record order, and zero-gain equivalence to `REECA1`.                                                                                                                                                                                                                                                                             |
| 4    | `REGCP1` with PLL            | Keep `REGCP1` as a separate terminal model. Bind a named measured-angle provider, then use `vd=V cos(a-am)` and `vq=-V sin(a-am)` for the current-frame P/Q equations; retain the present no-PLL path when no provider is selected. Factor the shared `REGCA1` current lag and limiter code instead of copying it. Compare angle-step and fault trajectories with ANDES `PLL1` and `ieee14_regcp1.xlsx`, including angle derivatives in the DAE Jacobian.                                                                                                                              |

The generator-local portion of step 1 is implemented, as is step 2. An
area-owned `BusROCOFSensor` reads terminal angle and exposes filtered
frequency deviation and ROCOF as continuous DAE algebraic outputs. `REECA1E`
is a separate electrical control with named input ports and adds
`-Kf*df-Kdf*dfdt` to the active reference after `Pref0/wg`. The host resolves
provider name, signal, and base, checks required ports during assembly, and
passes solver state locations for analytic Jacobian terms. The area initializes
the sensor before its consuming generator, and the host advances named sensors
before the electrical control in the explicit path. Terminal frequency is
also available to future components as a host input.

`BUSROCOF` and `REECA1E` load independently from DYR records in either order.
The REECA1E record extends REECA1 with `Kf`, `Kdf`, and a quoted measurement
name. Missing or incorrectly named measurements fail assembly, and nonzero
remote `BUSR` fails loading. Tests cover each gain alone and together, zero
gain behavior, measurement and control Jacobians, DYR order, and ANDES
comparison. The two-bus fault checks voltage, P/Q, current commands,
frequency deviation, and ROCOF. A separate test replays the exact ANDES bus
angle waveform through `BusROCOFSensor`, avoiding network angle differences when
checking the filter equations; see
`test/reference/renewable_fault/README.md`.

The zero-gain test now compares `REECA1E` directly with `REECA1` through a
varying-voltage trajectory while supplying nonzero frequency inputs. A paired
solver-mode test checks measurement values across the algebraic/differential
state split and verifies that the differential-only Jacobian does not assign
algebraic measurement columns to unrelated differential states.

The binding uses a named `BusMeasurementSensor` on the generator's bus. `REECA1E` uses an
area-owned `BusROCOF` sensor; provider type, name, source bus, and uniqueness
are checked during assembly. `REECA1G`'s synchronous-machine speed reference
is still separate work. `REGCP1` now binds a named PLL sensor through a
separate measured-angle port while retaining the terminal bus angle. Its
current-frame P/Q rotation and angle Jacobian use the shared `REGCA1` current
lag and limit equations.
Their DYR records must retain explicit model selection. `PVD1` follows because
it also needs frequency measurements; the grid-forming `REGCV*` and `REGF*`
families remain after that.

### Area-owned frequency and phase measurements

Use the existing `Sensor` DAE contract for independent `PLL1`, `PLL2`,
`BusFreq`, and `BusROCOF` measurements. `Sensor` is a `Relay`, and `GridArea`
already owns relays through `add(Relay*)` and `m_Relays`; those objects also
enter `primaryObjects` for state offsets, residuals, derivatives, and
Jacobians. Use that collection and execution path for sensors. The area
containing a sensor's measured bus should own it. A named sensor may serve
multiple controls.
`BUSROCOF` now uses the area-owned sensor exclusively; its trajectory is
checked against the ANDES measurement. Model-specific sensor subclasses supply PLL
feedback equations and their own output derivatives where the generic filter
block path is insufficient. A new general measurement-provider hierarchy is
not required for this work.

`FreqDiv` should also be represented by one algebraic frequency estimate per
measured bus, owned by that bus's `GridArea` through the same Relay collection.
Its equation is a row of
`(B_BB + B_B0)(f_B - 1) + B_BG(omega_G - 1) = 0`. The row uses the bus's
admittance connections and attached synchronous-machine data. An AC tie to a
different area contributes a coefficient for the neighboring area's bus
frequency. Area ownership therefore does not truncate the electrical equation:
the simulation's DAE solver couples all connected rows.
If an area must estimate frequency from only information available inside its
boundary, use a measured local tie-bus frequency as an explicit boundary
condition. That area-limited variant has different equations and should be
selected explicitly. Dropping tie terms would not implement `FreqDiv`.

Implement this in the following order:

1. Prove area-owned `Sensor` state allocation, named output lookup, and
   cross-object Jacobian propagation in combined and partitioned solver modes.
   Extend the renewable input resolver to use those outputs, and place sensors
   from dynamics records in the area containing their referenced bus.
2. Add area-owned `BusFreq`/`BusROCOF` and `PLL1`/`PLL2` sensors. Check the
   existing `AcBus` frequency filter and `Pmu` blocks for exact reusable
   transfer functions. Preserve the existing `BUSROCOF` DYR behavior. Compare
   standalone measurements and `REGCP1`'s PLL input with ANDES trajectories;
   test `REGF2`'s PLL input when that control model is implemented.
3. Add per-bus `FreqDiv` algebraic rows. Derive susceptance coefficients from
   supported network elements, including inter-area AC ties and generator
   reactances; reject unsupported elements instead of approximating them.
   Refresh affected rows and exact Jacobians after link, shunt, or generator
   status changes. Validate coverage and solvability per connected AC island.

The acceptance case is two areas joined by an AC tie, with local frequency
sensors and one or more synchronous generators. Compare `FreqDiv` outputs with
ANDES before and after a disturbance and tie trip; check inter-area Jacobian
entries and the new electrical islands. Include a case where one area has no
synchronous generator, since its bus-frequency estimate still depends on the
tie while connected. Review AGC ownership, tie-flow measurement, and its use
of sensor outputs separately after the sensor models and bindings are validated.

#### Implemented sensor stage

`BusMeasurementSensor` is a `Sensor` subclass for continuous bus measurements
with local DAE states and explicit output locations. `PLL1Sensor` implements
the filtered-angle PI loop, `PLL2Sensor` the voltage-phase PI loop, and
`BusROCOFSensor` the existing angle-lag/washout/ROCOF equations. Their
independent instances can be owned by the bus's area; `GridArea`'s existing
relay and primary-object lists size and execute them. The generic `Sensor`
also propagates direct and processed grabber output Jacobians and rejects
empty input expressions and inconsistent output configuration. Because area
relay stepping follows bus stepping, `RenewableGenerator` advances any named
dynamic bus sensor before its own explicit control step; the later area relay
visit has zero elapsed time. The combined DAE path uses the shared solver
states directly.

`BUSROCOF` DYR records create an area-owned sensor. The generator-attached
duplicate model and measurement role were removed; the ANDES angle waveform
and fault trajectory now test the sensor directly. The DYR record shapes are:

```text
bus 'PLL1' 'name' Kp Ki Tf Tp fn /
bus 'PLL2' 'name' Kp Ki fn /
bus 'FREQDIV' 'name' /
bus 'BUSROCOF' 'machine' 'name' Tf Tw Tr fn /
```

`FreqDivSensor` owns one algebraic frequency row per bus. It includes AC line
terms across area boundaries, line shunts, and dynamic-machine reactance
weights. The residual and Jacobian use current line and generator status;
line impedance and shunt values are read again after parameter changes.
Unsupported link classes, transformer taps, and phase shifts fail explicitly.
All adjacent buses need `FreqDivSensor` instances, including buses in another
area of the same simulation. A disconnected line remains in the topology
catalogue so reconnection restores its terms. If the local diagonal vanishes,
the explicit algebraic update reports a missing frequency reference.

Focused tests cover independent PLL states, finite-difference PLL and ROCOF
Jacobians, ROCOF trajectory equivalence, named DYR binding, partitioned state
values, inter-area FreqDiv residuals and Jacobians, changed shunts, and a tie
trip. The next validation step is an ANDES trajectory with a synchronous
generator and a tie trip, plus a full connected-island reference check. The
current FreqDiv neighbor lookup scans area relays during initialization;
index those references before applying FreqDiv to very large networks.
`REGCP1` accepts an optional trailing PLL name in its DYR record. The
no-PLL record retains its existing 15 numeric fields. A named PLL must be an
enabled `PLLSensor` on the generator's bus. `BusFreq` and AGC integration
remain separate work.

`REGCP1` tests cover no-PLL equivalence, initialization in a rotated current
frame, finite-difference power Jacobians, host coupling to both bus and PLL
angles, DYR model order, and rejection of missing, wrong-type, or wrong-bus
PLL references. The IEEE 14-bus integration test checks that area-owned PLL
initialization precedes generator initialization and that the combined DAE
residual, Jacobian, and short run succeed. A full network fault trajectory
against ANDES with PLL enabled remains a validation step.

### Distributed PV, storage, EV, and protection models

`PVD1`, `ESD1`, `EV1`, and `EV2` are `TerminalElectricalModel` instances hosted
by `RenewableGenerator`. They share a current-source converter with active and
reactive current lags, frequency-active-power droop, voltage-reactive-power
droop, voltage and frequency response curves, P/Q current priority, and a
current magnitude limit. `ESD1` adds SOC and bidirectional power. `EV1` adds
the charging power floor `pmn`, and `EV2` adds the upper power cap
`pcap * pmx`. `EV2` defaults to `pcap=0` and `ddn=1`, matching the ANDES
input defaults. Storage and EV charging uses negative power. `En` is in MWh;
SOC changes use the system MVA base and charge/discharge efficiencies.

`DGPRCT1` and `DGPRCTExt` are area-owned `Relay` instances. Each targets a
distributed converter and uses timed voltage and frequency threshold
conditions to set its `blocked` property. `DGPRCTExt` reads the numeric
`external_voltage` property, which a GridDyn `Player` or other event can
update. `DGPRCT1` reads its source bus voltage. A trip holds output current
commands at zero; when all conditions clear, the relay releases the block
after `Tres` seconds of continuous healthy measurements. A new violation
restarts that interval. The severe outer thresholds trip immediately.

The converter's `vrflag` and `frflag` are implemented as latch controls. Zero
holds the respective trip until a `reset=1` event; one releases the latch once
the measurement returns between the inner thresholds. A value between zero
and one permits that fraction of current command while latched. `recflag=1`
enables the continuous voltage and frequency recovery curves; zero bypasses
those curves but does not override a latched trip. These state changes are
handled by converter roots and explicit steps, rather than relying on the
currently unimplemented ANDES latching branch.

Native GridDyn XML uses the ordinary factories. For example:

```xml
<generator name="battery" type="renewable_dynamic" p="-0.2" q="0" mbase="100">
  <renewable_model name="battery_electrical" type="esd1" socinit="0.6"
                   en="20" pmx="1" pref="-0.2" />
</generator>
```

`pref`, `qref`, `paux`, `pcap`, and `blocked` are numeric properties available
to GridDyn `Player` and `Event` targets. Converter references use the machine
MVA base; the generator's `p` and `q` use the simulation base. For a relay in
an XML case, set its `source` to the measured bus and `sink` to the
converter. ANDES JSON `PVD1`/`ESD1`/`EV1`/`EV2` records create one generator
per device, including when multiple devices reference the same static PV
record; `DGPRCT1`/`DGPRCTExt` records link by their `dev` index. These are
not PSS/E DYR model names, so no DYR mappings are registered. ANDES case
loading here covers its JSON format.

Focused tests cover factory creation, XML and ANDES JSON loading,
player-driven setpoints, charging and SOC limits, latching and timed
protection, combined and partitioned converter DAE Jacobians, and short IEEE 14-bus runs for all four
converter types. A full ANDES-to-GridDyn disturbance trajectory for these
distributed models remains to be added. Remote `igreg` currently selects the
voltage used for droop and recovery while the generator remains electrically
connected at its declared terminal bus, consistent with GridDyn's generator
ownership. This can differ from an ANDES case that treats `igreg` as the
injection bus, so such a case needs an explicit topology review.
