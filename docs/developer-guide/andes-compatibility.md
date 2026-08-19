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

| Work item                                      | Status  | Completion evidence                                                                                           |
| ---------------------------------------------- | ------- | ------------------------------------------------------------------------------------------------------------- |
| DC `L`/`C`/combined branch dynamics            | Planned | ANDES and GridDyn trajectories from the same initialized DC case.                                             |
| `VSCShunt` controls, losses, limits, and droop | Partial | Core algebraic controls are present; loss coefficients and limit enforcement need model and trajectory tests. |
| Motors (`Motor3`/`Motor5`)                     | Planned | State-name/initialization mapping plus disturbance trajectory comparisons.                                    |
| Switched shunts                                | Planned | Block selection and switching-event comparisons.                                                              |
| Fortescue interface                            | Planned | New GridDyn component and unbalanced/interface regression cases.                                              |
| Remaining ANDES dynamic families               | Planned | Maintain a model-by-model inventory before adding importer mappings.                                          |

## Dynamic-model inventory

This inventory is based on the 100 registered models in the local ANDES
installation. Rows intentionally retain the ANDES class names so that a row
can be split and checked off as soon as models in the same family diverge. A
model is not compatible merely because a similarly named GridDyn model exists:
it also needs native-input mapping, initialization, and a trajectory test.

| ANDES model(s)                                                                                                                                         | GridDyn mapping / next action                                                                                     | Status    |
| ------------------------------------------------------------------------------------------------------------------------------------------------------ | ----------------------------------------------------------------------------------------------------------------- | --------- |
| `GENCLS`                                                                                                                                               | `GenModelClassical`; parameter and trajectory comparison required.                                                | Partial   |
| `GENROU`                                                                                                                                               | `GenModelGENROU`; PSS/e DYR reader has a mapping, but native ANDES mapping and numerical validation are absent.   | Partial   |
| `TGOV1`                                                                                                                                                | GridDyn governor; PSS/e DYR reader has a mapping, but native ANDES mapping and numerical validation are absent.   | Partial   |
| `EXDC2`                                                                                                                                                | GridDyn DC exciter; PSS/e DYR reader has a mapping, but native ANDES mapping and numerical validation are absent. | Partial   |
| `ZIP`, `FLoad`                                                                                                                                         | GridDyn static/dynamic load models; identify exact parameter and frequency-response equivalence.                  | Partial   |
| `Motor3`, `Motor5`                                                                                                                                     | `MotorLoad3`, `MotorLoad5`; parameter mapping and trajectory comparisons required.                                | Partial   |
| `ACE`, `ACEc`, `COI`                                                                                                                                   | Map area-control and center-of-inertia services.                                                                  | Untriaged |
| `PLBVFU1`, `IEEEVC`                                                                                                                                    | Map playback and voltage-compensation controls.                                                                   | Untriaged |
| `TG2`, `TGOV1DB`, `TGOV1N`, `TGOV1NDB`, `IEEEG1`, `IEESGO`, `GAST`, `HYGOV`, `HYGOVDB`, `HYGOV4`                                                       | Governor-family inventory and model-by-model equivalence review.                                                  | Untriaged |
| `SHAFT5`                                                                                                                                               | Multi-mass shaft model; map states and mechanical interfaces.                                                     | Untriaged |
| `IEEEX1`, `ESDC1A`, `ESDC2A`, `EXST1`, `ESST3A`, `SEXS`, `IEEET1`, `EXAC1`, `EXAC2`, `EXAC4`, `ESST4B`, `AC8B`, `IEEET3`, `ESAC1A`, `ESST1A`, `ESAC5A` | Exciter-family inventory; review the remaining classes individually.                                              | Untriaged |
| `IEEEST`, `ST2CUT`                                                                                                                                     | Power-system stabilizer models; identify GridDyn analogue or implement.                                           | Untriaged |
| `BusFreq`, `BusROCOF`, `PMU`, `PLL1`, `PLL2`, `FreqDiv`                                                                                                | Measurement and frequency-estimation models.                                                                      | Untriaged |
| `REGCA1`, `REGCP1`, `REECA1`, `REECA1E`, `REECA1G`, `REECB1`, `REPCA1`, `REGCV1`, `REGCV2`, `REGF1`, `REGF2`, `REGF3`                                  | Renewable/inverter-controller family; map plant, electrical, and frequency-control blocks.                        | Untriaged |
| `WTDTA1`, `WTDS`, `WTARA1`, `WTPTA1`, `WTTQA1`, `WTARV1`                                                                                               | Wind-turbine drive-train and control models.                                                                      | Untriaged |
| `PVD1`, `ESD1`, `EV1`, `EV2`, `DGPRCT1`, `DGPRCTExt`                                                                                                   | Distributed energy-resource and protection models.                                                                | Untriaged |
| `Fault`, `Alter`, `TimeSeries`, `Toggle`                                                                                                               | Event/action semantics and time-series input mapping.                                                             | Untriaged |
| `Summary`, `Output`                                                                                                                                    | Reporting configuration; define output-channel mapping after model compatibility.                                 | Untriaged |

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

## Current reference cases

| Case                       | Coverage                                                                |
| -------------------------- | ----------------------------------------------------------------------- |
| `andes_kundur_vsc_pflow`   | 10-bus AC network, DC resistor, PQ/VQ VSC controls, and AC/DC coupling. |
| `andes_two_bus_pflow`      | Minimal AC Slack/PQ/Line power flow.                                    |
| `andes_shunt_pflow`        | Fixed conductance/susceptance shunt with non-system `Sn`/`Vn` bases.    |
| `andes_jumper_pflow`       | Active and inactive zero-impedance jumpers in a loaded AC network.      |
| `andes_vsc_resistor_pflow` | Minimal AC/DC `VSCShunt` plus DC resistance power flow.                 |
