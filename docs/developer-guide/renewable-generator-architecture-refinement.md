# RenewableGenerator architecture refinement

## Current implementation scope (reviewed 2026-10-04)

`RenewableGenerator` hosts independent `RenewableComponent` instances. The
native converter/control family includes `REGCA1`, `REGCP1`, `REGCV1/2`,
`REGF1/2/3`, `REECA1`, `REECA1E/G`, `REECB1`, `REECC1`, and `REPCA1`. Wind
components include `WTDS`, `WTDTA1`, `WTARA1`, `WTPTA1`, and `WTTQA1`, plus
dedicated `WT3G1`/`WT3E1` and `WT4G1`/`WT4E1` electrical models. The DYR reader
also accepts legacy `WT3P1`/`WT3T1` records through provisional translations
to existing pitch, aerodynamic, and shaft components; those translations are
not verified as equivalent to dedicated legacy-model equations.

`PVD1`, `ESD1`, `EV1`, and `EV2` are aggregate distributed-converter models
loaded from ANDES JSON, not PSS/E DYR records. `REECC1` implements aggregate
battery state-of-charge and charge/discharge behavior and accepts both
`REECC1` and `REECCU1` DYR names. These models do not represent cell-level
electrochemistry.

The composition host supports named measurement bindings, machine-base to
system-base conversion, renewable-control assembly checks, and DAE Jacobian
propagation. Main remaining gaps include dedicated WT3P1/WT3T1 equations and
validation, Type-1/Type-2 induction-wind electrical models, WTARV1 pending an
equation source, unsupported remote/monitored-line modes in some controls,
and full-case/external trajectory coverage. Implemented model names alone do
not imply complete parameter-mode or simulator equivalence.

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

## Remaining renewable validation and extension work

Focused equation, assembly, and DAE-Jacobian checks cover many native
components. Remaining work is to compare full disturbance trajectories and
case configurations against independent references, exercise multiple
ratings and longer event windows, and validate the provisional WT3P1/WT3T1
translations against authoritative legacy-model equations. Type-1/Type-2
induction-wind models and WTARV1 remain extension candidates; WTARV1 needs a
complete equation source before implementation.

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

### WTDS, speed branch, and REGCP1 implementation notes

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

Named measurement bindings and solver-location propagation are implemented
for `REECA1E`, `REECA1G`, and the `REGCP1` PLL path. DYR records preserve
these model identities; missing or ambiguous measurement/machine references
fail assembly rather than silently falling back to a different control mode.

| Model/path        | Current implementation and remaining validation                                                                                                                        |
| ----------------- | ---------------------------------------------------------------------------------------------------------------------------------------------------------------------- |
| `REECA1E`         | Uses an area-owned `BUSROCOF` sensor for frequency deviation and ROCOF. Focused DYR-order, response, and Jacobian checks exist; external/full-case validation remains. |
| `REECA1G`         | Resolves a named synchronous-machine speed source. Validate scaling and representative multi-machine case behavior.                                                    |
| `REGCP1` with PLL | Binds a named `PLL1`/`PLL2` sensor while retaining terminal voltage/angle inputs. Focused response/Jacobian checks exist; broader trajectory validation remains.       |
| `BusFreq`         | Imported from supported local ANDES JSON references; this is not a standalone PSS/E DYR measurement model.                                                             |

An area-owned `BusROCOFSensor` reads terminal angle and exposes filtered
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

The binding uses named area-owned sensors and synchronous-machine sources;
provider type, name, source bus, and uniqueness are checked during assembly.
`REGCP1` keeps a separate measured-angle port while retaining terminal bus
voltage and angle. Its current-frame P/Q rotation and angle Jacobian use the
shared `REGCA1` current-lag and limit equations. PVD and grid-forming models
are implemented in separate component families; their remote-mode and
scheduled-reference limitations are documented below.

### Area-owned frequency and phase measurements

`PLL1Sensor`, `PLL2Sensor`, `BusROCOFSensor`, and `FreqDivSensor` use the
existing `Sensor`/area-relay DAE path. A sensor is owned by the area containing
its measured bus, and named outputs can be consumed by renewable controls.
ANDES `BusFreq` references are currently imported for supported local JSON
cases; there is no standalone PSS/E DYR `BusFreq` record.

`FreqDivSensor` represents one algebraic frequency estimate per bus. Its
equation is a row of
`(B_BB + B_B0)(f_B - 1) + B_BG(omega_G - 1) = 0`. The row uses the bus's
admittance connections and attached synchronous-machine data. An AC tie to a
different area contributes a coefficient for the neighboring area's bus
frequency. Area ownership therefore does not truncate the electrical equation:
the simulation's DAE solver couples all connected rows.
If an area must estimate frequency from only information available inside its
boundary, use a measured local tie-bus frequency as an explicit boundary
condition. That area-limited variant has different equations and should be
selected explicitly. Dropping tie terms would not implement `FreqDiv`.

Combined and partitioned solver paths, inter-area residual/Jacobian coupling,
status changes, shunt updates, and tie trips have focused checks. External
ANDES trajectories with a synchronous generator and full connected-island
coverage remain open. AGC ownership and automatic participation remain
separate integration work.

#### Sensor equations and integration details

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
enabled `PLLSensor` on the generator's bus. ANDES JSON `BusFreq` associations
are supported for local references; broader frequency/AGC participation
remains separate integration work.

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

### Scheduled active power

`RenewableGenerator` owns an attached `Scheduler` and exposes it through both
`find("sched")` and `find("pset")`. A scheduler output is in per unit on the
simulation base; the host converts it to the machine base before supplying
the `activeReference` input. `REECA1`/`REECB1` electrical controls and the
distributed `PVD1`/`ESD1`/`EV1`/`EV2` converters consume that input. Their
existing power filters, current lags, droop, and limits still determine the
electrical response. For distributed converters, the scheduled value replaces
`pref`; `paux` remains an additive adjustment.

The area AGC can create a `SchedulerReg` at the ISW bus for a renewable
generator with an available active reference, or use an explicitly attached
participant. A scheduler is rejected when another renewable component already
provides the absolute active reference, including the `WTTQA1` wind torque
path. `REGCV1`/`REGCV2` and `REGF1`/`REGF2`/`REGF3` do not yet expose a
scheduled power input, so attaching a scheduler to them fails during assembly
validation. Their dispatch controls need model-specific equations before
automatic AGC participation can be enabled.

Focused tests cover factory creation, XML and ANDES JSON loading,
player-driven setpoints, charging and SOC limits, latching and timed
protection, combined and partitioned converter DAE Jacobians, and short IEEE 14-bus runs for all four
converter types. A full ANDES-to-GridDyn disturbance trajectory for these
distributed models remains to be added. Remote `igreg` currently selects the
voltage used for droop and recovery while the generator remains electrically
connected at its declared terminal bus, consistent with GridDyn's generator
ownership. This can differ from an ANDES case that treats `igreg` as the
injection bus, so such a case needs an explicit topology review.
