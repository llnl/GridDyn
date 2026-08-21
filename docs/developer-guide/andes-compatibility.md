# ANDES compatibility roadmap

This document records the work required for GridDyn to load and solve ANDES
cases. It is intentionally a living checklist: a model is not complete until
its importer mapping and a numerical regression test are both present.

## Status terms

| Status             | Meaning                                                                                      |
| ------------------ | -------------------------------------------------------------------------------------------- |
| Implemented        | GridDyn model, ANDES importer mapping, and a numerical test exist.                           |
| Partial            | A GridDyn analogue exists, but the mapping, controls, or numerical validation is incomplete. |
| Planned            | No implementation work has started.                                                          |
| Untriaged          | Included in the ANDES inventory, but its GridDyn-equivalence review has not started.         |
| No direct analogue | Requires a new GridDyn model rather than reader-only work.                                   |

## Power-flow model mapping

| ANDES model(s)                                     | GridDyn mapping            | Status             | Notes                                                                                                                                                                                                         |
| -------------------------------------------------- | -------------------------- | ------------------ | ------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------- |
| `Bus`                                              | `AcBus`                    | Implemented        | Base voltage, initial voltage, and angle are imported.                                                                                                                                                        |
| `PQ`                                               | `ZipLoad`                  | Implemented        | Constant-power portion is imported.                                                                                                                                                                           |
| `PV`, `Slack`                                      | `AcBus` plus `Generator`   | Implemented        | Active-power and voltage targets are imported.                                                                                                                                                                |
| `Line`                                             | `AcLine`                   | Partial            | `r`, `x`, `b`, tap, phase shift, and status are imported. ANDES line-rated-voltage (`Vn1`/`Vn2`) base conversion, including its implicit 110 kV default, still needs an equivalent mixed-base implementation. |
| `Area`                                             | `GridArea`                 | Partial            | A GridDyn analogue exists; ANDES `Area` is not yet imported.                                                                                                                                                  |
| `Shunt`                                            | fixed-admittance `ZipLoad` | Implemented        | Device `Sn`/`Vn` admittance-base conversion, status, conductance, and capacitive susceptance sign are mapped and numerically tested.                                                                          |
| `ShuntSw`                                          | `loads::Svd`               | Partial            | Both provide switched reactive support; block/control semantics need mapping and numerical tests.                                                                                                             |
| `ShuntTD`                                          | fixed shunt for power flow | Partial            | Its steady-state behavior follows `Shunt`; its time-domain phase-voltage outputs are not represented.                                                                                                         |
| `Jumper`                                           | `links::ZBreaker`          | Implemented        | End buses and status are imported; active jumpers merge the bus solutions. Network voltages and angles are numerically tested. ANDES jumper `p`/`q` transfer reporting has no direct `ZBreaker` output.       |
| `Motor3`, `Motor5`                                 | `MotorLoad3`, `MotorLoad5` | Partial            | Candidate GridDyn models exist; parameter mapping and initialization comparisons remain.                                                                                                                      |
| `Fortescue`                                        | none                       | No direct analogue | Requires a multi-terminal positive-/negative-/zero-sequence interface model.                                                                                                                                  |
| `Node`, `Ground`                                   | `DcBus`                    | Implemented        | Ground is imported as a DC swing reference.                                                                                                                                                                   |
| `R`, `L`, `C`, `RLs`, `RCp`, `RLCp`, `RCs`, `RLCs` | `links::DcLink`            | Implemented        | ANDES current-balance convention is retained. Dynamic-form validation remains open.                                                                                                                           |
| `VSCShunt`                                         | `links::VSCShunt`          | Implemented        | Three-terminal algebraic converter; impedance-base conversion is applied during import.                                                                                                                       |

## Dynamic-model roadmap

The present numerical suite covers power flow. Dynamic compatibility requires
matching model states, initialization, events, limits, and trajectories; not
only the final power-flow point.

There are two separate compatibility axes:

1. **PSS/e RAW plus DYR input:** reproduce the model conversion performed by
   ANDES's `psse-dyr.yaml`, including the few conversions that ANDES currently
   labels as approximations.
2. **Native ANDES input:** construct the same GridDyn dynamic submodels and
   control connections when the source is ANDES JSON/XLSX. Native support is
   not implied by a working DYR adapter.

| Work item                                      | Status  | Completion evidence                                                                                           |
| ---------------------------------------------- | ------- | ------------------------------------------------------------------------------------------------------------- |
| DC `L`/`C`/combined branch dynamics            | Planned | ANDES and GridDyn trajectories from the same initialized DC case.                                             |
| `VSCShunt` controls, losses, limits, and droop | Partial | Core algebraic controls are present; loss coefficients and limit enforcement need model and trajectory tests. |
| Motors (`Motor3`/`Motor5`)                     | Planned | State-name/initialization mapping plus disturbance trajectory comparisons.                                    |
| Switched shunts                                | Planned | Block selection and switching-event comparisons.                                                              |
| Fortescue interface                            | Planned | New GridDyn component and unbalanced/interface regression cases.                                              |
| Remaining ANDES dynamic families               | Planned | Maintain a model-by-model inventory before adding importer mappings.                                          |

## PSS/e DYR compatibility baseline

The local ANDES DYR conversion table recognizes the 41 record names below.
GridDyn's current DYR reader recognizes only `GENROU`, `ESDC1A`, `EXDC2`,
`TGOV1`, and `SEXS`. Recognition alone is not compatibility: the existing
adapters still have attachment, parameter, equation, and validation gaps.

| PSS/e DYR record(s)                                                                          | ANDES destination       | Existing GridDyn candidate                                                    | Current compatibility and required action                                                                                                                                                                                                                                                                                        |
| -------------------------------------------------------------------------------------------- | ----------------------- | ----------------------------------------------------------------------------- | -------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------- |
| `GENCLS`                                                                                     | `GENCLS`                | `GenModelClassical`                                                           | **Partial.** Add a DYR adapter, verify machine/system-base conversion, initialization, and rotor trajectory.                                                                                                                                                                                                                     |
| `GENSAL`                                                                                     | `GENROU`                | No verified exact equivalent                                                  | **Planned.** ANDES explicitly marks this as an approximation and fills GENROU q-axis values from d-axis data. Decide whether compatibility mode should reproduce that conversion before considering a distinct salient-pole model.                                                                                               |
| `GENROU`                                                                                     | `GENROU`                | Registered `GenModelGENROU`; DYR uses the dedicated model                     | **Partial.** The adapter and dedicated model match all eight initialized machine states, field voltage, and mechanical power for five IEEE 14-bus machines, including quadratic saturation. Exact alphanumeric machine-ID lookup, arbitrary machine-base cases, native ANDES import, and disturbed-trajectory validation remain. |
| `IEEEVC`                                                                                     | `IEEEVC`                | None                                                                          | **No direct analogue.** Add a voltage-compensator input to the exciter path, including `rc`/`xc` terminal-current compensation.                                                                                                                                                                                                  |
| `SEXS`                                                                                       | `SEXS`                  | `ExciterSEXS`                                                                 | **Partial.** The adapter exists; verify lead-lag convention, limits, initialization, and trajectories.                                                                                                                                                                                                                           |
| `ESDC1A`                                                                                     | `ESDC1A`                | `ExciterDC1A` / `ExciterIEEEtype1`                                            | **Partial.** The current adapter ignores `TR`, switch behavior, and `E1`/`SE1`/`E2`/`SE2`; its fallback to type 1 when `TB` is zero must be compared with ANDES.                                                                                                                                                                 |
| `EXDC2`                                                                                      | `EXDC2`                 | `ExciterDC2A`                                                                 | **Partial.** The adapter exists but ignores `TR`, switch behavior, and saturation points. Confirm whether `DC2A` equations exactly represent ANDES `EXDC2`.                                                                                                                                                                      |
| `IEEET1`, `IEEET3`, `ESDC2A`                                                                 | Same named ANDES models | `ExciterIEEEtype1`, `ExciterIEEEtype2`, and `ExciterDC2A` are only candidates | **Planned.** Perform equation and limiter audits before selecting an analogue; similar names are insufficient.                                                                                                                                                                                                                   |
| `ESST1A`, `ESAC1A`, `AC8B`, `EXAC1`, `EXAC2`, `EXAC4`, `IEEEX1`, `EXST1`, `ESST3A`, `ESST4B` | Same named ANDES models | None exact                                                                    | **No direct analogue.** Implement model-specific exciter blocks and DYR schemas, then add initialization and trajectory tests.                                                                                                                                                                                                   |
| `ESAC6A`, `SCRX`, `EXPIC1`                                                                   | `SEXS`                  | `ExciterSEXS`                                                                 | **Planned compatibility approximations.** ANDES marks these conversions as TODO/approximate and currently discards most source parameters. Reproduce this only as an explicit compatibility mode and emit a diagnostic; do not describe it as exact model support.                                                               |
| `TGOV1`                                                                                      | `TGOV1`                 | `GovernorTgov1`                                                               | **Partial.** The adapter exists, but currently reads `T2`, `T3`, and `Dt` from the wrong token positions. Correct the schema and then audit limits, initialization, and trajectories.                                                                                                                                            |
| `HYGOV`                                                                                      | `HYGOV`                 | `GovernorHydro` is a candidate                                                | **Planned.** Compare equations, water-column dynamics, gate/rate limits, and parameter units before mapping it.                                                                                                                                                                                                                  |
| `IEESGO`, `IEEEG1`                                                                           | Same named ANDES models | `GovernorReheat` and steam-governor classes are only candidates               | **Planned.** No exact equivalence has been established; audit block diagrams before choosing reuse versus new models.                                                                                                                                                                                                            |
| `GAST`                                                                                       | `GAST`                  | None exact                                                                    | **No direct analogue.** Add a gas-turbine governor implementation and DYR adapter.                                                                                                                                                                                                                                               |
| `GGOV1`                                                                                      | `TGOV1`                 | `GovernorTgov1`                                                               | **Planned compatibility approximation.** ANDES currently retains only `R` when converting this record. If reproduced, emit a diagnostic and keep exact GGOV1 support as a separate task.                                                                                                                                         |
| `IEEEST`, `ST2CUT`                                                                           | Same named ANDES models | Base `Stabilizer` only                                                        | **No direct analogue.** The base class is not a usable PSS implementation; add the two models and their generator/exciter signal connections.                                                                                                                                                                                    |
| `REGCA1`, `REECA1`, `REECB1`, `REPCA1`                                                       | Same named ANDES models | `GenModelInverter` is not equivalent                                          | **No direct analogue.** Add the coordinated renewable generator, electrical-control, and plant-control chain rather than flattening these records into the generic inverter.                                                                                                                                                     |
| `WTDTA1`, `WTARA1`, `WTPTA1`, `WTTQA1`                                                       | Same named ANDES models | None                                                                          | **No direct analogue.** Add drive-train, aerodynamic, pitch, and torque-control submodels with their shared interfaces.                                                                                                                                                                                                          |
| `Toggle`, `Fault`                                                                            | ANDES event models      | GridDyn event/action and fault mechanisms                                     | **Partial conceptually.** Define DYR record schemas and translate target resolution, timing, status changes, and fault clearing semantics; no adapter exists.                                                                                                                                                                    |

### DYR reader infrastructure plan

- Replace the model-name `if` chain and unchecked numeric vector with a
  table-driven record registry. Each adapter should declare token count,
  parameter names, supported variants, and its exact/approximate status.
- Parse quoted machine identifiers, continuation records, comments, commas,
  and the terminating slash without assuming that every field is numeric.
- Resolve a machine by RAW bus number plus exact PSS/e machine ID. The current
  `stoi(ID)` followed by `getGen(ID - 1)` is a positional lookup and cannot
  safely handle quoted or alphanumeric IDs.
- Validate bus, generator, factory result, token count, and numeric conversion
  before attaching a model. Unsupported, approximate, malformed, duplicate,
  and conflicting controller records need structured diagnostics rather than
  an `stdout` message followed by continued loading.
- Ensure the target is a `DynamicGenerator`, preserve the RAW machine base and
  stator data needed by the DYR model, and define how a second machine,
  exciter, governor, or stabilizer record on the same generator is handled.
- Use the same model-construction functions from the DYR reader and native
  ANDES reader so the two input paths cannot drift to different equations.
- Add parser/attachment tests for quoted and alphanumeric IDs, multiple
  machines at one bus, continuation lines, comments, malformed records,
  unsupported models, and model conflicts.

## Six-PR IEEE 14 dynamic compatibility execution plan

This is the authoritative implementation order for fully exercising
`GenModelGENROU` with the complete ANDES IEEE 14-bus RAW/DYR case. It is
written as a handoff contract: after any PR is merged, a new developer or LLM
should be able to start the next PR using only this document, the GridDyn
repository, and the frozen ANDES source identified below. Do not silently move
work between PRs. If a prerequisite defect is found, fix it in the PR that owns
that subsystem and record the change here.

This six-PR sequence covers the PSS/e RAW/DYR path and the models required by
this case. Native ANDES JSON/XLSX dynamic-model import and unrelated ANDES
models remain separate roadmap work; they are not hidden requirements for
these merge gates.

### Frozen comparison source and target behavior

- ANDES source repository: `C:\Users\phlpt\Documents\andes` in the original
  development environment.
- ANDES version and commit: `v2.0.0`,
  `eda5163c9ee8d19945a1dd5d1771fec5da608c27`.
- Full inputs: `andes/cases/ieee14/ieee14.raw` and
  `andes/cases/ieee14/ieee14.dyr`.
- Upstream input SHA-256 values:
  - RAW: `539c35fd57f72079206d5b92b3eb5be2e0fbdbd6dc9037a2e0b99f9c6a774eb3`
  - DYR: `1f97507a01df1c73c897092fb928a460d4dc5e5ba2263c2bac2cd63b8e97a888`
- DYR conversion schema: `andes/io/psse-dyr.yaml`.
- Existing ANDES regression: `tests/integration/test_known_good.py`, with its
  two-second reference in `tests/pkl/ieee14_2s.pkl`. The upstream
  test checks final GENROU `omega`, `tm`, and `vf` at two decimal places. The
  GridDyn regression must retain those checks but capture more signals and use
  explicit per-signal absolute and relative tolerances. Its ANDES TDS settings
  are `tf=2`, fixed `tstep=1/30`, nonlinear tolerance `1e-4`, `fixt=1`, and
  `shrinkt=0`; use the same settings when deliberately regenerating the
  comparison reference.
- Relevant ANDES equations:
  - `andes/models/synchronous/genrou.py`
  - `andes/models/governor/tgov1.py`
  - `andes/models/governor/ieeeg1.py`
  - `andes/models/exciter/esst3a.py`
  - `andes/models/exciter/exst1.py`
  - `andes/models/pss/st2cut.py`
  - `andes/models/pss/ieeest.py`

The target run is a converged power flow followed by dynamic initialization,
an equilibrium interval, opening `Line_1` at 1.0 seconds, reclosing it at 1.1
seconds, and continuing through 2.0 seconds. The five controller chains are:

| Generator bus | Machine  | Exciter  | Governor | Stabilizer |
| ------------- | -------- | -------- | -------- | ---------- |
| 1             | `GENROU` | `ESST3A` | `TGOV1`  | `ST2CUT`   |
| 2             | `GENROU` | `EXST1`  | `IEEEG1` | `ST2CUT`   |
| 3             | `GENROU` | `ESST3A` | `IEEEG1` | `IEEEST`   |
| 6             | `GENROU` | `ESST3A` | `TGOV1`  | none       |
| 8             | `GENROU` | `ESST3A` | `TGOV1`  | none       |

### PR status and dependency order

| PR  | Deliverable                                                                                                   | Status                                         | Depends on |
| --- | ------------------------------------------------------------------------------------------------------------- | ---------------------------------------------- | ---------- |
| 1   | Complete GENROU equations, saturation utility, DYR mapping, and five-machine initialization reference         | Current changes; treat as complete after merge | none       |
| 2   | Robust RAW/DYR identity and base handling, IEEE 14 power-flow parity, and a controller-free GENROU trajectory | Planned                                        | PR 1       |
| 3   | Generator/controller signal plumbing, PSS-to-exciter routing, and validated `TGOV1`                           | Planned                                        | PR 2       |
| 4   | Complete `ESST3A` and `EXST1` models plus DYR adapters                                                        | Planned                                        | PR 3       |
| 5   | Complete `IEEEG1`, `ST2CUT`, and `IEEEST` models plus DYR adapters                                            | Planned                                        | PR 4       |
| 6   | DYR `Toggle`, complete IEEE 14 initialization/equilibrium, and the two-second trajectory regression           | Planned                                        | PR 5       |

PRs must merge in this order. Each PR must pass without relying on production
code from a later PR. Minimized fixtures and test-only probe models are
preferred when a downstream controller is not yet available.

### PR 1: GENROU model and DYR initialization baseline

This is the current change set. Once merged, do not reopen the generic
`GenModel6` implementation to add GENROU behavior; `GenModelGENROU` is the
canonical PSS/e/ANDES-compatible class.

Required contents:

- Complete sixth-order GENROU algebraic, differential, initialization, output,
  and analytic Jacobian equations with GridDyn's documented dq sign mapping.
- Reusable cutoff-scaled quadratic saturation in `utilities::Saturation`.
- Factory registration under `genrou`; generic model `6` remains unchanged.
- `GENROU` DYR records construct `GenModelGENROU`. Attach the model before
  applying DYR reactances so RAW stator resistance is retained without the RAW
  source reactance overwriting DYR `Xd`.
- Focused unsaturated/saturated initialization, derivative, residual,
  algebraic, Jacobian, invalid-parameter, factory, and saturation tests.
- A dedicated `AndesCompatibilityTests` target and an IEEE 14 test that loads
  the RAW plus the five extracted GENROU DYR records, asserts the model type,
  and compares all eight initialized states, field voltage, and mechanical
  power with ANDES at `1e-6`.

Primary GridDyn artifacts after merge:

- `src/griddyn/genmodels/GenModelGENROU.{h,cpp}`
- `src/utilities/Saturation.{h,cpp}`
- `src/fileInput/gridDynReadDYR.cpp`
- `test/componentTests/testGenModels.cpp`
- `test/libraryTests/testSaturation.cpp`
- `test/andesTests/testAndesDyrReader.cpp`
- `test/test_files/andes_tests/ieee14.raw`
- `test/test_files/andes_tests/ieee14_genrou.dyr`
- `test/test_files/andes_tests/andes_ieee14_genrou_reference.json`

Merge gate: `LibraryTests`, `GeneratorComponentTests`, and
`AndesCompatibilityTests` pass. The current implementation has passed all
three targets. Full network/controller trajectory parity is intentionally not
part of PR 1.

### PR 2: Reader foundation, power-flow parity, and GENROU-only trajectory

Fresh-context task statement: make the real IEEE 14 RAW/DYR generator identity
and bases reliable, make GridDyn's RAW-only operating point agree with ANDES,
and then prove that the five GENROU models remain in equilibrium and respond
correctly without any controller models.

Required production work:

- Refactor `src/fileInput/gridDynReadDYR.cpp` into validated record adapters or
  a small schema-backed registry. Preserve continuation-line handling while
  validating token count and numeric fields.
- Resolve generators by RAW bus number plus the exact quoted PSS/e machine ID;
  remove the current `stoi(ID)` plus `getGen(ID - 1)` positional assumption.
- Diagnose missing buses/generators, duplicate machine models, malformed
  records, unsupported records, and factory failures without null dereferences.
- Verify transfer of RAW `MBASE`, stator resistance, rated voltage, and status
  into the dynamic generator/model. Add a non-100-MVA machine-base regression.
- Correct the IEEE 14 RAW power-flow mismatch. Audit PV voltage targets,
  reactive-limit behavior, transformer taps/shunts, and generator setpoints;
  do not hide a modeling mismatch by loosening tolerances.

Independent tests:

- DYR parser/attachment cases for quoted and alphanumeric IDs, two generators
  on one bus, continuation lines, malformed records, and duplicate GENROU.
- RAW-only IEEE 14 comparison of every bus voltage/angle and generator P/Q
  against captured ANDES values with reviewed per-signal tolerances.
- Load the RAW plus `ieee14_genrou.dyr`, perform GridDyn power flow and dynamic
  initialization without supplying captured terminal inputs, and compare the
  initialized GENROU states to ANDES.
- A no-disturbance run with bounded drift and a separate small field-voltage or
  mechanical-power perturbation compared to a captured ANDES time series.

Likely touchpoints include `src/fileInput/gridDynReadRAW.cpp`,
`src/fileInput/gridDynReadDYR.cpp`, generator identity/storage classes, and
`test/andesTests/`. Do not implement governors, exciters, stabilizers, or
`Toggle` in this PR.

Merge gate: the controller-free case reaches dynamic completion, initialization
matches ANDES, the equilibrium case does not drift beyond its declared
tolerance, and the perturbed GENROU trajectory agrees at common sample times.

### PR 3: Controller signal contract, PSS routing, and TGOV1

Fresh-context task statement: establish the reusable signal connections needed
by the missing IEEE 14 controllers, then make the three existing TGOV1 records
numerically compatible with ANDES.

Required production work:

- Define named, documented generator/controller signals for terminal voltage,
  rotor speed, `Id`, `Iq`, `Vd`, `Vq`, electrical power/torque, mechanical
  power, and `XadIfd`. Specify dq signs and machine/system-base scaling.
- Expose signal values and state/Jacobian locations from GENROU through
  `DynamicGenerator`; avoid model-specific downcasts in controller classes.
- Extend the exciter input contract to include field-current/machine feedback
  needed by `ESST3A` and `EXST1`.
- Define selectable stabilizer inputs and route the stabilizer output `Vss`
  into the exciter reference summing junction. The current code initializes a
  PSS but does not use its output in `generateSubModelInputs`.
- Correct `TGOV1` DYR mapping. The schema is `R, T1, VMAX, VMIN, T2, T3, Dt`;
  the existing adapter incorrectly takes `T2` from the `VMIN` position.
- Audit `GovernorTgov1` droop, damping, limit/root behavior, initialization,
  units, and machine-base conversion against ANDES rather than assuming the
  class name establishes equivalence.

Independent tests:

- A probe submodel verifies every new signal's value, sign, base, and Jacobian
  location at initialization and a perturbed GENROU state.
- A test stabilizer produces a known `Vss`; verify it changes the exciter
  summing input and ultimately GENROU field voltage with the correct Jacobian.
- DYR parameter/attachment tests for all three IEEE 14 `TGOV1` records.
- Isolated TGOV1 initialization, limit transitions, and speed/load-step
  trajectory against ANDES, followed by a GENROU+TGOV1 equilibrium test.

Likely touchpoints include `GenModel`, `GenModelGENROU`, `DynamicGenerator`,
`Exciter`, `Stabilizer`, `GovernorTgov1`, the DYR adapters, and component plus
ANDES compatibility tests. Do not implement the two exciters or three remaining
governor/PSS classes in this PR.

### PR 4: ESST3A and EXST1 excitation systems

Fresh-context task statement: implement the two excitation-system models used
by all five IEEE 14 generators, using the PR 3 signal contract and exact ANDES
equations.

`ESST3A` must include its voltage transducer, terminal-current compensation,
`VE`, piecewise `FEX`, `XadIfd` feedback, `VB` calculation, regulator and
feedback lags, lead-lag compensation, anti-windup, and input/output limits.
Map all DYR fields in the order declared by `psse-dyr.yaml`. It is used at
buses 1, 3, 6, and 8.

`EXST1` must include its voltage transducer, input limiter, lead-lag,
regulator, washout feedback, `XadIfd`-dependent output bounds, and output
limiter. It is used at bus 2. Do not approximate either model with an existing
generic exciter merely because some blocks are similar; reuse GridDyn block or
saturation utilities only when their equations and limiter semantics match.

Each model must provide:

- A normal GridDyn class, factory registration, clone support, parameter
  validation, state names, Doxygen equations, and citations to the frozen
  ANDES source and an applicable standard/manual.
- Complete DYR mapping with an attachment/parameter regression.
- Captured ANDES initialization values, a perturbed equation/derivative test,
  analytic-versus-finite-difference Jacobian checks, and explicit limiter/root
  transition tests.
- A minimized GENROU+exciter voltage-reference-step trajectory. ESST3A and
  EXST1 tests must pass independently of each other.

Cross-simulator fixtures belong in `test/andesTests/` and
`test/test_files/andes_tests/`; generic equation tests may live with exciter
component tests. Do not add IEEEG1 or stabilizer implementations in this PR.

Merge gate: all five IEEE 14 generators can attach and initialize the correct
exciter type in a controller subset case, and both minimized trajectories
match ANDES within declared tolerances.

### PR 5: IEEEG1, ST2CUT, and IEEEST

Fresh-context task statement: complete the remaining governor and stabilizer
models so every controller record in the IEEE 14 DYR has an exact GridDyn
destination.

Required models:

- `IEEEG1`, used at buses 2 and 3: implement speed droop, valve-position and
  rate limits, turbine-stage time constants, eight power fractions, and the
  optional second-machine input defined by the DYR schema.
- `ST2CUT`, used at buses 1 and 2: implement both selectable inputs, local or
  remote buses, transducers, washout/lag selection, three lead-lag stages,
  output limits, and voltage gating.
- `IEEEST`, used at bus 3: implement selectable input, second-order filters,
  two lead-lag stages, gain, washout/lag, output limiting, and voltage gating.

Do not map `IEEEG1` to `GovernorReheat` or either stabilizer to the base
`Stabilizer` unless an equation-by-equation audit proves exact equivalence.
The actual IEEE 14 `ST2CUT` records use rotor-speed input, while its `IEEEST`
record uses generator electrical-power input; complete model support should
also test the other documented modes so the implementation is not
case-specific.

Each model has the same completion requirements as PR 4: class/factory/clone,
full DYR schema, Doxygen equations and sources, initialization and perturbed
equations, Jacobian, limit/root transitions, and a minimized coupled ANDES
trajectory. Add explicit remote-bus and machine/system-base tests where the
schema permits them.

Merge gate: a no-event IEEE 14 controller subset loads all five governors and
all three stabilizers with no unsupported-record diagnostics; each controller
also passes its independent minimized numerical comparison. Do not add the
line `Toggle` or final full-case regression in this PR.

### PR 6: Toggle events and complete IEEE 14 regression

Fresh-context task statement: translate the two ANDES DYR `Toggle` records and
add the permanent end-to-end regression proving that the complete unmodified
IEEE 14 RAW/DYR case behaves like ANDES.

Required production work:

- Add a validated `Toggle` DYR adapter for the schema `model, dev, t`.
- Resolve the target by stable imported model/device identity, not incidental
  container position. For this case, toggle `Line_1` at exactly 1.0 and 1.1
  seconds.
- Use GridDyn's event queue and topology-change notifications so opening and
  reclosing a line causes the required solver/Jacobian reinitialization.
- Unsupported DYR records must produce structured diagnostics. This exact case
  must load with no unsupported or silently ignored records.

Independent event test: in a minimized network, open and reclose one line and
verify trigger times, link status, topology/solver refresh, and pre-/post-event
power transfer. This test must not depend on the IEEE 14 numerical reference.

Full-case tests, in increasing order:

1. Load the unmodified RAW/DYR and assert the exact five machine/controller
   chains listed above.
2. Compare power-flow and every selected controller/machine initialization
   value with ANDES.
3. Run without events over the same interval and verify bounded equilibrium
   drift.
4. Run the original events through 2.0 seconds and compare common-time samples
   before, at, between, and after the events.

At minimum capture bus voltage/angle, line status/flow, GENROU `delta`,
`omega`, all transient/subtransient states, `vf`, `tm`, `Pe`, and `Qe`, plus
selected state/output signals from each governor, exciter, and stabilizer.
Store units, state names, sample times, ANDES commit, input hashes, disturbance
definition, and per-signal absolute/relative tolerances with the reference.
Event times are exact and are not relaxed for solver differences.

Merge gate: the unmodified case completes through 2.0 seconds, reproduces both
line status changes, retains the upstream final `omega`/`tm`/`vf` checks, and
passes the stronger GridDyn time-series comparisons. Register the test as a
regular CTest target; use a slower label only if measured runtime justifies it.

### Fresh-context restart and validation checklist

For any new task working on PR 2 through PR 6:

1. Read this six-PR section and the detailed compatibility tables below it.
2. Confirm every earlier PR is present in the branch and run its focused test
   targets before editing.
3. Inspect the working tree and preserve unrelated user changes. Work only on
   the next unmerged PR's scope.
4. Read the corresponding frozen ANDES model source and `psse-dyr.yaml`
   section completely. Capture reference values deliberately; never import or
   execute ANDES from the regular GridDyn C++ test.
5. Keep ANDES comparison tests out of the generic element-reader fixture
   folder. Use `test/andesTests/` and
   `test/test_files/andes_tests/`; keep reusable component equation tests with
   their normal GridDyn component test target.
6. Compare initialization before trajectories. A successful solve alone is
   not evidence of compatibility.
7. Run formatting, `git diff --check`, the new focused tests, and all earlier
   affected test targets. Update the PR status and evidence in this document
   before merging.

The normal Windows build entry point is:

```text
cmake --build build --config Debug --parallel 4
ctest --test-dir build -C Debug --output-on-failure
```

Only if MSBuild reports the duplicate `Path`/`PATH` `MSB6001` error, use the
repository workaround for the same target:

```text
cmd /v:on /c "set PATH=& set Path=& ""C:\Program Files\CMake\bin\cmake.exe"" --build build --config Debug --parallel 4"
```

## GENROU first-target plan

### Current state

- `GenModelGENROU` is registered as `genrou` and implements the ANDES gamma
  coefficients, algebraic current equations, six differential equations,
  electrical torque including stator copper loss, quadratic saturation,
  initialization, and analytic Jacobian in GridDyn's established dq sign
  convention.
- Focused tests compare unsaturated and saturated initialization against
  captured values from the first GENROU machine in ANDES's
  `kundur/kundur_full.json` case. A perturbed-state test compares all six
  differential equations against direct ANDES-equation reference values.
  Residual, derivative, algebraic, and Jacobian finite-difference checks
  exercise the saturated model. Invalid transient/subtransient ordering and
  singular gamma-coefficient denominators are rejected during initialization.
  As in ANDES, leakage reactance above subtransient reactance produces a
  warning but remains runnable; this is required by the bundled IEEE 39-bus
  DYR case.
- The DYR reader creates the registered `GenModelGENROU` and applies the DYR
  machine parameters after attachment so RAW stator data is retained without
  allowing the RAW source reactance to overwrite `Xd`. A dedicated
  `AndesCompatibilityTests` regression loads the ANDES IEEE 14-bus RAW file
  and five GENROU records, then compares all eight initialized machine states,
  field voltage, and mechanical power with captured ANDES v2.0.0 values at
  `1e-6`. The test supplies the captured ANDES terminal operating points to
  isolate DYR/model compatibility from the remaining RAW AC-network and
  controller gaps. Exact machine identity/base handling and disturbed
  trajectory parity remain required before compatibility is called complete.

### Parameter contract

| PSS/e GENROU value | ANDES value                             | Intended GridDyn value / note                                                                                  |
| ------------------ | --------------------------------------- | -------------------------------------------------------------------------------------------------------------- |
| `Td10`, `Td20`     | `Td10`, `Td20`                          | `Tdop`, `Tdopp`                                                                                                |
| `Tq10`, `Tq20`     | `Tq10`, `Tq20`                          | `Tqop`, `Tqopp`                                                                                                |
| `H`                | `M = 2 H`                               | Store `H`; the perturbed derivative reference verifies equivalence to ANDES's `M` convention.                  |
| `D`                | `D`                                     | `D`; the perturbed derivative reference verifies the speed-deviation convention on the machine base.           |
| `Xd`, `Xq`         | `xd`, `xq`                              | `Xd`, `Xq` on the machine base.                                                                                |
| `Xd1`, `Xq1`       | `xd1`, `xq1`                            | `Xdp`, `Xqp` on the machine base.                                                                              |
| `Xd2`              | `xd2`, and `xq2 = xd2`                  | `Xdpp = Xqpp = Xd2`; this is the PSS/e GENROU single-subtransient-reactance convention.                        |
| `Xl`               | `xl`                                    | `Xl`; use it in the same gamma-factor formulation as ANDES, not as an extra reactance added twice.             |
| `S10`, `S12`       | `S10`, `S12`                            | Fit and apply the same quadratic saturation characteristic used by ANDES.                                      |
| Not in DYR record  | `ra`, `Sn`, `Vn`, static generator link | Obtain stator resistance, machine MVA base, rated voltage, and machine identity from the paired RAW generator. |

### GENROU implementation status

The six-PR plan above is the authoritative merge sequence. This checklist only
records the lower-level GENROU work already completed or assigned to PR 2.

1. **Done:** Create equation-level reference notes from ANDES `GENROU`, including its
   `gd1`, `gq1`, `gd2`, `gq2`, and `gqd` coefficients, air-gap flux magnitude,
   quadratic saturation, algebraic current equations, and all initialization
   equations.
2. **Done:** Complete `GenModelGENROU`, register it under an unambiguous
   `genrou` factory name, and leave generic model `6` behavior unchanged for
   existing users.
3. **Done:** Implement every state, residual, derivative, Jacobian, initialization, and
   output equation consistently. Add singular-parameter checks for invalid
   reactance differences and time constants.
4. **Partial:** The `GENROU` DYR adapter now creates `GenModelGENROU`, preserves
   RAW stator data, and maps all PSS/e GENROU fields. Complete exact quoted or
   alphanumeric bus-plus-machine-ID lookup, verify non-unity machine-base
   conversion through the RAW path, and have native ANDES input construct the
   same registered class.
5. Validate in layers: parameter/attachment tests, t=0 state and algebraic
   comparison, no-disturbance equilibrium, then disturbed trajectories. Only
   after generator-only agreement add governor, exciter, and PSS controls.

### Expected GridDyn implementation touchpoints

| Area                       | Expected files / change                                                                                                                                                                                                           |
| -------------------------- | --------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------- |
| Canonical machine model    | **Done:** `src/griddyn/genmodels/GenModelGENROU.{h,cpp}` is complete at the equation/initialization level and `genrou` is registered in `src/griddyn/genmodels/GenModel.cpp`; generic model `6` remains unchanged.                |
| DYR parsing and attachment | **Partial:** GENROU creates the dedicated model and applies parameters in the correct RAW/DYR order. Refactor `src/fileInput/gridDynReadDYR.cpp` into schema-backed adapters and add exact bus-plus-machine-ID lookup support.    |
| Native ANDES import        | Extend the ANDES JSON reader's dynamic-object dispatch to build the same registered machine/controller classes and control connections used by DYR adapters.                                                                      |
| Model tests                | **Mostly done:** focused equation, Jacobian, initialization, saturation, invalid-parameter, and factory tests are present; add a disturbed-trajectory reference and clone regression as the model is integrated.                  |
| Reader tests               | **Partial:** `test/andesTests/testAndesDyrReader.cpp` is a dedicated RAW/DYR GENROU attachment and initialization test. Add parser edge cases and disturbed trajectories as support expands.                                      |
| Numerical references       | **Partial:** the IEEE 14-bus RAW input, minimized GENROU DYR input, and captured ANDES initialization reference are stored under `test/test_files/andes_tests/` and run without importing ANDES. Add time-series references next. |

### Planned GENROU reference cases

| Case                                     | Purpose                                       | Required comparisons                                                                                                                             |
| ---------------------------------------- | --------------------------------------------- | ------------------------------------------------------------------------------------------------------------------------------------------------ |
| Single-machine/infinite-bus `GENROU`     | Isolate machine equations and bases.          | Initial `delta`, `omega`, d/q currents, transient and subtransient states, field voltage, mechanical/electrical power, terminal P/Q.             |
| Same case with nonzero `S10`/`S12`       | Exercise saturation independently.            | Air-gap flux, saturation output, field-current term, and resulting trajectory.                                                                   |
| `GENROU` + `TGOV1`                       | Add mechanical control after machine parity.  | Speed, valve/governor states, mechanical power, and limiter activity after a small load step.                                                    |
| `GENROU` + `SEXS`, then `ESDC1A`/`EXDC2` | Add excitation controls one at a time.        | Terminal voltage, field voltage, exciter states, reactive power, and limiter activity after a voltage-reference step.                            |
| Multi-machine RAW/DYR case               | Verify identity, bases, and network coupling. | Correct model attachment plus selected machine angles, speeds, bus voltages, P/Q, and relative-angle trajectories through a cleared disturbance. |

## Dynamic-model inventory

This inventory is based on the 100 registered models in the local ANDES
installation. Rows intentionally retain the ANDES class names so that a row
can be split and checked off as soon as models in the same family diverge. A
model is not compatible merely because a similarly named GridDyn model exists:
it also needs native-input mapping, initialization, and a trajectory test.

| ANDES model(s)                                                                                                        | GridDyn mapping / next action                                                                                                                                                          | Status             |
| --------------------------------------------------------------------------------------------------------------------- | -------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------- | ------------------ |
| `GENCLS`                                                                                                              | `GenModelClassical`; parameter and trajectory comparison required.                                                                                                                     | Partial            |
| `GENROU`                                                                                                              | `GenModelGENROU` equations, DYR mapping, and five-machine initialization match ANDES references; fix exact machine identity/base handling, native ANDES import, and trajectory parity. | Partial            |
| `TGOV1`                                                                                                               | `GovernorTgov1`; correct the DYR token mapping, then audit equations, limits, initialization, and trajectories.                                                                        | Partial            |
| `EXDC2`                                                                                                               | `ExciterDC2A` candidate; add omitted transducer/switch/saturation behavior and prove equation equivalence.                                                                             | Partial            |
| `ZIP`, `FLoad`                                                                                                        | GridDyn static/dynamic load models; identify exact parameter and frequency-response equivalence.                                                                                       | Partial            |
| `Motor3`, `Motor5`                                                                                                    | `MotorLoad3`, `MotorLoad5`; parameter mapping and trajectory comparisons required.                                                                                                     | Partial            |
| `ACE`, `ACEc`, `COI`                                                                                                  | Map area-control and center-of-inertia services.                                                                                                                                       | Untriaged          |
| `PLBVFU1`, `IEEEVC`                                                                                                   | No direct voltage-compensator/playback analogue; define exciter input and playback interfaces.                                                                                         | No direct analogue |
| `TG2`, `TGOV1DB`, `TGOV1N`, `TGOV1NDB`, `IEEEG1`, `IEESGO`, `GAST`, `HYGOV`, `HYGOVDB`, `HYGOV4`                      | Existing hydro/reheat/steam classes are candidates only; each needs an equation audit or a new model.                                                                                  | Planned            |
| `SHAFT5`                                                                                                              | Multi-mass shaft model; map states and mechanical interfaces.                                                                                                                          | Untriaged          |
| `ESDC1A`, `ESDC2A`, `SEXS`, `IEEET1`, `IEEET3`                                                                        | Existing DC/IEEE/SEXS exciters are candidates; complete model-specific audits and DYR/native mappings.                                                                                 | Partial            |
| `IEEEX1`, `EXST1`, `ESST3A`, `EXAC1`, `EXAC2`, `EXAC4`, `ESST4B`, `AC8B`, `ESAC1A`, `ESST1A`, `ESAC5A`                | No exact named GridDyn implementations; implement individually rather than mapping by family name.                                                                                     | No direct analogue |
| `IEEEST`, `ST2CUT`                                                                                                    | Base `Stabilizer` is not a functional equivalent; implement both models and signal connections.                                                                                        | No direct analogue |
| `BusFreq`, `BusROCOF`, `PMU`, `PLL1`, `PLL2`, `FreqDiv`                                                               | Measurement and frequency-estimation models.                                                                                                                                           | Untriaged          |
| `REGCA1`, `REGCP1`, `REECA1`, `REECA1E`, `REECA1G`, `REECB1`, `REPCA1`, `REGCV1`, `REGCV2`, `REGF1`, `REGF2`, `REGF3` | Generic `GenModelInverter` is insufficient; add composable generator, electrical, plant, and frequency controls.                                                                       | No direct analogue |
| `WTDTA1`, `WTDS`, `WTARA1`, `WTPTA1`, `WTTQA1`, `WTARV1`                                                              | Add wind-turbine drive-train, aerodynamic, pitch, torque, and renewable-voltage submodels and interfaces.                                                                              | No direct analogue |
| `PVD1`, `ESD1`, `EV1`, `EV2`, `DGPRCT1`, `DGPRCTExt`                                                                  | Distributed energy-resource and protection models.                                                                                                                                     | Untriaged          |
| `Fault`, `Alter`, `TimeSeries`, `Toggle`                                                                              | Event/action semantics and time-series input mapping.                                                                                                                                  | Untriaged          |
| `Summary`, `Output`                                                                                                   | Reporting configuration; define output-channel mapping after model compatibility.                                                                                                      | Untriaged          |

## Numerical-regression policy

- Keep minimized ANDES-derived input cases and captured reference results in
  `test/test_files/andes_tests/`.
- The regular C++ tests must not execute ANDES. Refresh a reference only by
  running the documented ANDES case deliberately and reviewing the diff.
- Each reference file stores its tolerance. Start with `1e-6` p.u. for
  well-conditioned power-flow comparisons; relax only with a documented
  solver-conditioning reason.
- Compare selected voltages, angles, injections, and flows. A successful
  solve by itself is not sufficient evidence of compatibility.

For dynamic cases, capture an ANDES reference time series from the same
RAW/DYR pair (or the same native ANDES case) and retain the ANDES version,
input checksum, disturbance definition, output names, units, sampling times,
and tolerance metadata beside the values. Compare in the following order:

1. **Initialization:** selected state and algebraic values at `t = 0`, before
   accepting any trajectory comparison.
2. **Equilibrium:** a no-disturbance run with bounded state/output drift.
3. **Trajectory:** state and output samples at common times around a small
   reference/load step and a cleared network disturbance.

Dynamic tolerances must be per signal and include absolute and relative terms;
angle, speed, voltage, power, and controller-state scales are different. Event
times must be compared separately. Solver differences can justify reviewed
tolerance changes, but cannot justify a phase shift, wrong limiter transition,
wrong model attachment, or a different post-event equilibrium.

## Current reference cases

| Case                       | Coverage                                                                 |
| -------------------------- | ------------------------------------------------------------------------ |
| `andes_kundur_vsc_pflow`   | 10-bus AC network, DC resistor, PQ/VQ VSC controls, and AC/DC coupling.  |
| `andes_two_bus_pflow`      | Minimal AC Slack/PQ/Line power flow.                                     |
| `andes_shunt_pflow`        | Fixed conductance/susceptance shunt with non-system `Sn`/`Vn` bases.     |
| `andes_jumper_pflow`       | Active and inactive zero-impedance jumpers in a loaded AC network.       |
| `andes_vsc_resistor_pflow` | Minimal AC/DC `VSCShunt` plus DC resistance power flow.                  |
| `andes_ieee14_genrou`      | Five PSS/e DYR GENROU attachments and initialized machine states/inputs. |
