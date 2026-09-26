# Renewable generator components and `add` design

**Status:** The [architecture refinement](renewable-generator-architecture-refinement.md)
supersedes this note's proposed inheritance tree and `add` dispatch. This
earlier draft remains as a detailed model inventory and record of the
role-specific alternative.

## Design rule

`RenewableGenerator` is a `Generator` sibling of `DynamicGenerator`. It owns
replaceable dynamic submodels and routes signals between them. Each named
dynamics model remains an independent object with its own parameters, state,
factory type, and `clone` implementation. GridDyn's `GridSubModel` lifecycle,
`IOdata`, state offsets, and Jacobian interfaces remain the implementation
foundation. The host does not contain `REGCA1`, `REECA1`, or `REPCA1` equations.

The aim is model equivalence with the ANDES checkout, not its Python class
hierarchy. Match each supported model's equations and public behavior, while
using GridDyn's own power signs, bases, object ownership, and file readers.

## Shared bases and concrete model identity

GridDyn already uses `GenModel`, `Exciter`, and `Governor` as `GridSubModel`
subclasses that define an attachment role and shared behavior. The renewable
equivalent should follow that pattern. Define an abstract role base when at
least two concrete models can honor the same host-facing contract:

```text
GridSubModel
├─ RenewableElectricalModel        required terminal electrical role
│  ├─ RenewableConverterModel
│  │  ├─ REGCA1             current-command converter
│  │  ├─ REGCP1             REGCA1 family, with PLL behavior
│  │  └─ REGCV*/REGF*/PVD1  alternative converter behavior
│  └─ InductionGeneratorModel       future WT1G/WT2G family
├─ RenewableElectricalController
│  └─ REECA1/REECB1/...      independent current-command controls
├─ RenewablePlantController
│  └─ REPCA1                 independent plant control
├─ WindDriveTrainModel       WTDS/WTDTA1
├─ WindAerodynamicsModel     WTARA1/WTARV1
├─ WindPitchController       WTPTA1
├─ WindTorqueController      WTTQA1
└─ RotorResistanceController future WT2E family
```

The role bases define the common port layout, terminal P/Q convention or
reference convention, model capabilities, and any genuinely shared
initialization checks. They should not contain a generic set of renewable
equations. Concrete classes retain their own parameters, states, equations,
factory registrations, DYR mappings, and clone methods. Closely related
models can add a further _implementation_ base where equations really are
shared, such as the `REGCA1`/`REGCP1` current-controlled family. A model remains
independently selectable even when it shares that implementation.

The earlier count of seven described the roles in the ANDES Type-3/Type-4
assembly; it is not a limit on host roles. The first milestone needs the
electrical-model, converter, electrical-controller, and plant-controller
bases. Add induction-machine and resistance-control roles only when their
concrete models are implemented. `RenewableGenerator` is an additional host
class, and existing `Source`/`Scheduler` objects can supply resource or
setpoint signals where their contract fits.

A single `RenewableSubModel : GridSubModel` layer across every role is optional
and currently adds little: `GridSubModel` already provides lifecycle,
ownership, state offsets, and solver callbacks, while electrical, plant, and
wind-mechanical models have different ports and rating bases. Put common
base-conversion and port helpers in small utilities until repeated code
justifies a common ancestor. In particular, do not impose one machine MVA
field on every role; a plant controller or turbine may have a different base.

## Mapping the model roles

| Model(s)                                                     | Main responsibility and exchanged signals                                                                                                                               | Existing GridDyn class considered                                                                                                                                                                             | Recommended GridDyn type                                                                                                                                                  |
| ------------------------------------------------------------ | ----------------------------------------------------------------------------------------------------------------------------------------------------------------------- | ------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------- | ------------------------------------------------------------------------------------------------------------------------------------------------------------------------- |
| `REGCA1`                                                     | Current-command lag/limits and terminal `Pe`, `Qe` injection from bus voltage and `Ipcmd`, `Iqcmd`.                                                                     | `GenModel`/`GenModelInverter` share P/Q output but assume `Eft`, `Pmech`, internal voltage, and impedance.                                                                                                    | Concrete converter under `RenewableConverterModel : RenewableElectricalModel`; the latter defines the common terminal P/Q contract.                                       |
| `REGCP1`                                                     | `REGCA1` behavior with an optional PLL-derived angle/voltage frame.                                                                                                     | No GridDyn PLL model was found under `src/griddyn`; machine angle from `GenModel` is not a PLL.                                                                                                               | Separate converter model, sharing `REGCA1` internals; add an explicit PLL/measurement component when implementing this variant.                                           |
| `REECA1`, `REECA1E/G`, `REECB1`                              | Electrical controls using voltage, `Pe`, `Qe`, speed/frequency and plant references to produce active/reactive current commands and `Pord`.                             | `Exciter` expects to output field voltage, and `Governor` expects to output mechanical power. `ControlSystem` is currently a mostly empty shell.                                                              | New `RenewableElectricalController : GridSubModel` role base; each file-selectable model remains a concrete object. Reuse `GridBlock`/`blocks::*` inside implementations. |
| `REPCA1`                                                     | Plant P/Q and voltage/frequency control, possibly with remote bus/line measurements; sends active/reactive reference _increments_ to the electrical controller.         | `Source` is a signal generator, and `Stabilizer` supplies a synchronous exciter supplement. Neither has this measurement and two-reference contract.                                                          | New `RenewablePlantController : GridSubModel` role base; concrete `REPCA1`. Use existing bus/line measurement interfaces.                                                 |
| `WTDS`, `WTDTA1`                                             | One- or two-mass shaft dynamics: take converter `Pe` and mechanical `Pm`; produce `wg`, `wt`, and shaft states.                                                         | `Governor` takes speed/setpoint and outputs `Pmech`; it does not own this reverse electrical-power-to-speed linkage.                                                                                          | New wind drivetrain role base with separate concrete models.                                                                                                              |
| `WTARA1`, `WTARV1`                                           | Convert pitch/aerodynamic inputs into turbine mechanical power. ANDES `WTARA1` has no explicit wind-speed input.                                                        | `Source` may provide wind availability to a model that requests it but does not represent pitch feedback.                                                                                                     | Separate aerodynamic submodels; connect a wind source only when the concrete model declares that input.                                                                   |
| `WTPTA1`                                                     | Pitch controller driven by turbine speed and the electrical controller's `Pord` and `Pref`; outputs pitch angle to aerodynamics.                                        | `GridBlock` is one-input/one-output and is useful for its internal lag/limiter/PI blocks.                                                                                                                     | Separate pitch-control submodel composed from GridDyn blocks.                                                                                                             |
| `WTTQA1`                                                     | Torque/reference controller driven by electrical power and shaft speed; supplies the active-power reference to electrical control and speed reference to pitch control. | A synchronous `Governor` produces `Pmech`, which is the wrong output contract.                                                                                                                                | Separate torque-control submodel composed from GridDyn blocks.                                                                                                            |
| `REGCV1/2`, `REGF1/2/3`                                      | Grid-forming converter with internal voltage/current and frequency states.                                                                                              | Existing `GenModelInverter` is an impedance/power-angle approximation with a different control structure.                                                                                                     | Alternative concrete converter models under the converter role; they may contain their own controllers and therefore need no `REECA1` slot.                               |
| `PVD1`                                                       | Integrated distributed-PV electrical and control behavior.                                                                                                              | `VariableGenerator` models resource availability rather than this complete electrical response.                                                                                                               | A standalone converter-role model if its terminal P/Q contract fits; keep its integrated controls inside its own independent object.                                      |
| Future Type-1/Type-2 `WT1G`/`WT2G`                           | Induction-machine electrical behavior, with slip and electrical torque coupled to the turbine. Type 2 additionally accepts controlled external rotor resistance.        | `MotorLoad`/`MotorLoad3`/`MotorLoad5` contain induction-machine calculations but inherit `GridLoad`, have load-sign and motor initialization assumptions, and cannot be attached directly as `GridSubModel`s. | New induction-generator electrical models under `RenewableElectricalModel`; extract reusable numerical helpers from motor code only if appropriate.                       |
| Future Type-1/Type-2 turbine, pitch, and resistance controls | Turbine/shaft and optional pitch behavior; Type 2 has an external rotor-resistance controller.                                                                          | Existing wind roles may be reusable if their signal and equation contracts match; Type-3 `WTTQA1`/`REECA1` do not provide rotor-resistance control.                                                           | Concrete drivetrain/pitch variants and a separate resistance-controller role if needed.                                                                                   |

The initial electrical, electrical-controller, and plant-controller roles are
needed now because they have different, stable input/output contracts and must
be independently swapped. Wind role bases can be added as their models land.
`REGCA1` need not inherit `GenModel` merely
because it occupies the electrical role: `GenModel` initializes `Eft` and
`Pmech`, stores `Xd`/`Rs`, and advertises those inputs. Inheriting it would
require overriding its meaningful defaults. Similarly, inheriting `Exciter`
for `REECA1` would label current commands as field voltage.

Keep `RenewableElectricalModel` small: terminal-voltage input, terminal P/Q
output, initialization, and solver/Jacobian callbacks. Expose current-command
ports as a capability of `REGCA1`-style converters, not a requirement of every
electrical model. A `REGCV`/`REGF` implementation can own its internal controls,
while an induction model exposes slip/torque and, for Type 2, external rotor
resistance. Electrical controllers should advertise whether they accept plant
increments and turbine-speed/reference inputs. The host checks these
capabilities when validating an assembled model set.

## Host slots and signal contracts

Use unique `locIndex` values for: electrical model, electrical controller,
plant controller, drivetrain, aerodynamics, pitch, torque, optional
rotor-resistance control, and optional availability source. Maintain one owned
pointer per slot. Provide `find` aliases such as `electrical_model`,
`electrical_control`, and `plant_control`, while retaining each object's own
name and factory type. One `RenewableElectricalModel` is required for a dynamic
renewable generator; it may be a converter or an induction generator. Other
slots are optional subject to capability checks:
`REPCA1` requires an electrical controller with external-reference inputs;
wind components require the signals they consume; an integrated grid-forming
converter or Type-1 induction generator can operate without a separate
`REECA1`-style electrical controller.

This host boundary also accommodates Type-1/Type-2 wind in a later phase.
Their grid-facing model is an induction generator rather than an
`Ipcmd`/`Iqcmd` converter. WECC describes `WT1G`/`WT2G` electrical models,
`WT1T`/`WT2T` turbine models, optional Type-1 pitch control, and a Type-2
`WT2E` external rotor-resistance controller. A Type-1 stall-controlled
turbine may need no pitch model. These are future concrete model candidates,
not requirements for the first ANDES milestone. See the
[WECC dynamic modeling guide](https://www.wecc.org/sites/default/files/documents/meeting/2024/WECC%20Wind%20Plant%20Dynamic%20Modeling%20Guidelines.pdf).

```text
wind/resource → turbine and shaft → mechanical torque → induction model → bus P/Q
                                               ▲                ▲
                                               │        Type 2 rotor resistance
                                               └──── electrical torque/speed ────
```

`RenewableGenerator` should bind these paths from port contracts, without
branches that check a concrete model name such as `REGCA1` or `WT1G`.
Reactive compensation for an induction-generator plant can use normal
GridDyn network components rather than a fabricated current-command
controller slot.

At the generator boundary, positive `P` and `Q` mean generation, as in
`Generator`. GridDyn's bus-facing `getOutputs` uses negative injections; the
host applies that sign and the machine-to-system-base scale once. Submodels
exchange named, machine-base signals. For the first converter chain:

```text
bus V, angle/frequency ──────────────┐
                                     ▼
plant REPCA1 ── ΔPref/ΔQref ──> electrical REECA1 ── Ipcmd/Iqcmd ──> REGCA1
      ▲                                    ▲                         │
      └──── measured P/Q, bus/line V ─────┴──── Pe/Qe, optional wg ─┘
```

The electrical controller owns its baseline `Pref`/`Qref`; plant outputs are
increments applied through explicit reference ports. For the wind chain,
`WTDTA1` receives converter `Pe` and aerodynamic `Pm`, and exposes `wg`/`wt`.
`WTARA1` receives pitch angle and provides `Pm`; other aerodynamic models may
declare a wind/resource input.
`WTTQA1` provides the active reference to `REECA1` and a speed reference to
`WTPTA1`; `WTPTA1` also reads `Pord` and `Pref` and sends pitch angle to
`WTARA1`. Define how a torque-model reference and a plant-control increment
compose at the electrical controller before implementing both together. The
wind components remain independent objects even though their ports form a
coupled loop.

The diagram names model-level signals, not a new general-purpose signal-graph
framework. The host can build fixed `IOdata` arrays and `IOlocs` for each
slot, just as `DynamicGenerator` does. It must evaluate outputs from the
current `StateData` and provide the cross-submodel locations to the Jacobian.
Sequential `timestep` calls or cached scalar `getOutput()` values alone cannot
represent the algebraic feedback loops.

## `RenewableGenerator::add` behavior

Use role-base `dynamic_cast` dispatch, as `DynamicGenerator::add(GridSubModel*)`
does for machine, exciter, governor, and PSS. `Generator::add(CoreObject*)`
already forwards a `GridSubModel*` to the virtual submodel overload. Declare
`using Generator::add` (or provide a forwarding `add(CoreObject*)`) so direct
calls with other pointer types remain visible.

```cpp
void RenewableGenerator::add(GridSubModel* object)
{
    // Reject null and an object owned by another generator; ignore re-adding
    // the exact object already in its slot.
    if (auto* model = dynamic_cast<RenewableElectricalModel*>(object)) {
        replaceSlot(electricalModel, model, ELECTRICAL_MODEL_LOC);
    } else if (auto* model = dynamic_cast<RenewableElectricalController*>(object)) {
        replaceSlot(electricalControl, model, ELECTRICAL_CONTROL_LOC);
    } else if (auto* model = dynamic_cast<RenewablePlantController*>(object)) {
        replaceSlot(plantControl, model, PLANT_CONTROL_LOC);
    } else if (auto* model = dynamic_cast<WindDriveTrainModel*>(object)) {
        replaceSlot(driveTrain, model, DRIVE_TRAIN_LOC);
    } else if (auto* model = dynamic_cast<WindAerodynamicsModel*>(object)) {
        replaceSlot(aerodynamics, model, AERODYNAMICS_LOC);
    } else if (auto* model = dynamic_cast<WindPitchController*>(object)) {
        replaceSlot(pitchControl, model, PITCH_LOC);
    } else if (auto* model = dynamic_cast<WindTorqueController*>(object)) {
        replaceSlot(torqueControl, model, TORQUE_LOC);
    } else if (auto* model = dynamic_cast<RotorResistanceController*>(object)) {
        replaceSlot(rotorResistanceControl, model, ROTOR_RESISTANCE_LOC);
    } else if (auto* source = dynamic_cast<Source*>(object)) {
        addSourceByPurpose(source);  // resource or setpoint purpose is explicit
    } else {
        throw UnrecognizedObjectException(this);
    }
}
```

This is a dispatch sketch, not a proposed implementation patch. `replaceSlot`
should use `replaceSubObject(new, old)`, assign a unique `locIndex`, resize the
host's input/location arrays, update the typed pointer, and invalidate cached
signal values. `GridComponent::replaceSubObject` already manages parent and
owning references and invalidates initialized offsets when appropriate.
Source dispatch should require a defined resource/setpoint purpose; a
`Scheduler` remains a distinct source subtype and its base `Generator` pointer
must be kept consistent.
Override `remove` to clear the corresponding typed pointer before the base
removes the owned object; otherwise a later evaluation can dereference a
stale pointer. `clone` must reattach cloned submodels through `add` so its
typed pointers refer to cloned children. Replacement is supported during
configuration; changing a model after dynamic initialization requires a
fresh initialization and solver-state layout.

`add` checks role and ownership, then attaches the model. It should not
require neighboring slots immediately: DYR and other dynamics records can
arrive in any order. Validate the complete assembly in `dynObjectInitializeA`
or a pre-initialization validation step, with specific errors for missing
electrical model, incompatible controller ports, ambiguous wind connections, and
invalid duplicate records. A direct `add` can deliberately replace its role;
a file reader should reject accidental duplicate records for the same machine
and role unless replacement is explicitly requested.

### Binding and compatibility checks

Each role base should expose a small, typed port/capability description:
required inputs, provided outputs, signal quantity, and per-unit base. Use
fixed `IOdata` indices within each role rather than a general-purpose runtime
graph. The host builds a deterministic binding table from those descriptions
after all models have been attached, then uses that same table for
initialization, residuals, outputs, and Jacobians. A missing required input
must be satisfied by an explicit external source or reported as an error;
the host should not silently fabricate a default connection.

| Assembly                                           | Required compatibility check                                                                                                                                                           |
| -------------------------------------------------- | -------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------- |
| `REGCA1` + `REECA1`                                | Electrical controller provides active/reactive current commands; converter accepts them with compatible or explicitly converted per-unit bases.                                        |
| `REGCV`/`REGF` + `REECA1`                          | Reject unless that converter explicitly exposes current-command inputs; integrated grid-forming controls do not imply those ports.                                                     |
| `REPCA1` + electrical controller                   | Controller accepts active/reactive _reference increments_ and every selected plant measurement resolves to a bus or line.                                                              |
| Type-1/Type-2 induction generator                  | Electrical model accepts mechanical torque/speed or power through its declared ports; do not require converter current commands or a `REECA1` slot.                                    |
| Type-2 induction generator + resistance controller | Generator exposes external rotor-resistance input and controller provides it; reject the controller with a Type-1 machine lacking that input.                                          |
| Wind drivetrain + aerodynamics                     | Mechanical-power and speed ports match; an absent aerodynamic model requires an explicit mechanical input source.                                                                      |
| Wind torque + pitch + electrical controller        | Torque reference, speed reference, `Pord`, and pitch-angle ports resolve without two providers claiming the same baseline reference. Plant increments are separate from that baseline. |

At `add` time, reject null objects, an object owned by another host, and an
unrecognized role. The file reader rejects an unintended duplicate record
before calling `add`; a deliberate direct `add` replaces only that slot and
marks the binding table stale. After
all DYR/DYD/XML records are loaded, validate required roles, port and base
compatibility, unique providers, referenced network objects, and feasible
initial operating points. Diagnostics should name the bus, machine ID, model
types, and the missing or conflicting port. Replacing a model after dynamic
initialization requires a fresh solver-state layout and initialization.

## Reader consequences and focused tests

The existing DYR reader creates one model per record using
`CoreObjectFactory`, finds its machine by bus and ID, sets parameters, and
calls `gen->add(model)`. Add a factory entry and field map for each renewable
record. If the steady-state reader created a basic or synchronous-style
generator, the dynamics reader must obtain a `RenewableGenerator` host while
preserving its bus, machine ID, operating P/Q, status, and rating. `REGCA1`,
`REECA1`, and `REPCA1` then attach independently. Extend the DYD dispatcher
only for records whose field schema is defined; its current direct-model list
does not include renewables. For GridDyn XML, `loadSubObjectsElement.cpp` has
an explicit loader map, so new model tags/categories must be registered there
as well as in `CoreObjectFactory`.

The first focused tests should construct each of the three model objects by
factory, attach them in every record order, replace each slot independently,
clone and remove the host, and reject unknown or cross-owned submodels. They
should then load separate DYR records, check parameter mapping and duplicate
diagnostics, and run full and partitioned dynamic modes to confirm that
cross-model Jacobian locations and bus P/Q signs are correct.

Add one host-contract test with a minimal non-converter electrical test model:
it should provide terminal P/Q and mechanical torque/speed ports without
`Ipcmd`/`Iqcmd`, attach successfully, and reject `REECA1` as an incompatible
control. A Type-2-shaped test model should accept a resistance controller;
the same controller should be rejected with a Type-1-shaped model. This tests
extensibility before implementing the future induction equations.
