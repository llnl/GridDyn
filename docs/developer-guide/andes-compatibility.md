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

| ANDES model(s)                                     | GridDyn mapping                 | Status             | Notes                                                                                                                                                                                                         |
| -------------------------------------------------- | ------------------------------- | ------------------ | ------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------- |
| `Bus`                                              | `AcBus`                         | Implemented        | Base voltage, initial voltage, angle, and numeric ANDES `idx` as the GridDyn user ID are imported. Preserving the user ID allows a matching DYR file to resolve its bus records.                              |
| `PQ`                                               | `ZipLoad`                       | Implemented        | Constant-power portion is imported.                                                                                                                                                                           |
| `PV`, `Slack`                                      | `AcBus` plus `DynamicGenerator` | Implemented        | Active-power and voltage targets are imported. Dynamic generator objects preserve the same power-flow behavior and accept machine/control models from a subsequently loaded DYR file.                         |
| `Line`                                             | `AcLine`                        | Partial            | `r`, `x`, `b`, tap, phase shift, and status are imported. ANDES line-rated-voltage (`Vn1`/`Vn2`) base conversion, including its implicit 110 kV default, still needs an equivalent mixed-base implementation. |
| `Area`                                             | `GridArea`                      | Partial            | A GridDyn analogue exists; ANDES `Area` is not yet imported.                                                                                                                                                  |
| `Shunt`                                            | fixed-admittance `ZipLoad`      | Implemented        | Device `Sn`/`Vn` admittance-base conversion, status, conductance, and capacitive susceptance sign are mapped and numerically tested.                                                                          |
| `ShuntSw`                                          | `loads::Svd`                    | Partial            | Both provide switched reactive support; block/control semantics need mapping and numerical tests.                                                                                                             |
| `ShuntTD`                                          | fixed shunt for power flow      | Partial            | Its steady-state behavior follows `Shunt`; its time-domain phase-voltage outputs are not represented.                                                                                                         |
| `Jumper`                                           | `links::ZBreaker`               | Implemented        | End buses and status are imported; active jumpers merge the bus solutions. Network voltages and angles are numerically tested. ANDES jumper `p`/`q` transfer reporting has no direct `ZBreaker` output.       |
| `Motor3`, `Motor5`                                 | `MotorLoad3`, `MotorLoad5`      | Partial            | Candidate GridDyn models exist; parameter mapping and initialization comparisons remain.                                                                                                                      |
| `Fortescue`                                        | none                            | No direct analogue | Requires a multi-terminal positive-/negative-/zero-sequence interface model.                                                                                                                                  |
| `Node`, `Ground`                                   | `DcBus`                         | Implemented        | Ground is imported as a DC swing reference.                                                                                                                                                                   |
| `R`, `L`, `C`, `RLs`, `RCp`, `RLCp`, `RCs`, `RLCs` | `links::DcLink`                 | Implemented        | ANDES current-balance convention is retained. Dynamic-form validation remains open.                                                                                                                           |
| `VSCShunt`                                         | `links::VSCShunt`               | Implemented        | Three-terminal algebraic converter; impedance-base conversion is applied during import.                                                                                                                       |

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
GridDyn's current DYR reader includes the models marked implemented below,
including `GENCLS`, `GENSAL`, `ESST4B`, `HYGOV`, and native `GGOV1`. Recognition alone is not compatibility: the existing
adapters still have attachment, parameter, equation, and validation gaps.

| PSS/e DYR record(s)                    | ANDES destination       | Existing GridDyn candidate                                | Current compatibility and required action                                                                                                                                                                                                                                                                                                                                                                                                                           |
| -------------------------------------- | ----------------------- | --------------------------------------------------------- | ------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------- |
| `GENCLS`                               | `GENCLS`                | `GenModelClassical`                                       | **Implemented; validate externally.** The DYR adapter transfers RAW `ZSOURCE`, maps `H` and `D`, and has factory, equation, initialization, Jacobian, and ANDES/PSS/E reader coverage. A captured disturbed trajectory and broader machine/system-base cases remain.                                                                                                                                                                                                |
| `GENSAL`                               | `GENROU`                | `GenModelGENSAL`                                          | **Implemented natively.** GridDyn reads the original PSS/e GENSAL record instead of reproducing ANDES's GENROU approximation. Exact field mapping, initialization/residual, controller-signal, and analytic-Jacobian tests are present; an external disturbed trajectory remains.                                                                                                                                                                                   |
| `GENROU`                               | `GENROU`                | Registered `GenModelGENROU`; DYR uses the dedicated model | **Partial.** The adapter and dedicated model match all eight initialized machine states, field voltage, and mechanical power for five IEEE 14-bus machines, including quadratic saturation. Exact alphanumeric machine-ID lookup, arbitrary machine-base cases, native ANDES import, and disturbed-trajectory validation remain.                                                                                                                                    |
| `IEEEVC`                               | `IEEEVC`                | None                                                      | **No direct analogue.** Add a voltage-compensator input to the exciter path, including `rc`/`xc` terminal-current compensation.                                                                                                                                                                                                                                                                                                                                     |
| `SEXS`                                 | `SEXS`                  | `ExciterSEXS`                                             | **Partial.** The adapter exists; verify lead-lag convention, limits, initialization, and trajectories.                                                                                                                                                                                                                                                                                                                                                              |
| `ESDC1A`                               | `ESDC1A`                | `ExciterDC1A` / `ExciterIEEEtype1`                        | **Partial.** The current adapter ignores `TR`, switch behavior, and `E1`/`SE1`/`E2`/`SE2`; its fallback to type 1 when `TB` is zero must be compared with ANDES.                                                                                                                                                                                                                                                                                                    |
| `EXDC2`                                | `EXDC2`                 | `ExciterDC2A`                                             | **Partial.** The adapter exists but ignores `TR`, switch behavior, and saturation points. Confirm whether `DC2A` equations exactly represent ANDES `EXDC2`.                                                                                                                                                                                                                                                                                                         |
| `IEEET1`                               | `IEEET1`                | `ExciterIEEEtype1`                                        | **Implemented; external trajectory open.** The dedicated factory and exact PSS/e DYR mapping are available; retain case-level initialization and trajectory validation as follow-up work.                                                                                                                                                                                                                                                                           |
| `IEEET3`, `ESDC2A`                     | Same named ANDES models | `ExciterIEEEtype2` and `ExciterDC2A` are candidates       | **Partial.** `ESDC2A` has a dedicated DYR adapter; complete equation, limiter, and trajectory audits before claiming full compatibility.                                                                                                                                                                                                                                                                                                                            |
| `ESST3A`                               | `ESST3A`                | `ExciterESST3A`                                           | **Implemented.** Exact PSS/e field mapping, ANDES-equation initialization, residual, and Jacobian coverage with GENROU; reduced-order synchronous generators use the documented controller-signal approximations.                                                                                                                                                                                                                                                   |
| `EXST1`                                | `EXST1`                 | `ExciterEXST1`                                            | **Implemented.** PSS/e field order, captured initialization and perturbed-equation values, corrected regulator-output limiter/root tests, analytic-Jacobian finite-difference checks, and GENROU attachment are covered. The documented limiter-selector divergence from frozen ANDES is intentional. Zero `TR`, `TB`, or `TA` records and a captured disturbed trajectory remain unsupported.                                                                      |
| `EXAC1`, `EXAC2`, `EXAC4`              | Same named ANDES models | `ExciterEXAC1`, `ExciterEXAC2`, `ExciterEXAC4`            | **Implemented.** Exact PSS/e DYR field mapping, initialization, residual/Jacobian, limiter/root, GENROU attachment, and IEEE-14 load-step coverage are present. EXAC1/EXAC2 intentionally use the sensed-voltage transducer output instead of the disconnected frozen-ANDES path documented below. EXAC1 supports the specified zero-`TR` transducer bypass; EXAC2 still requires positive `TR`, and other zero-time algebraic bypasses remain model-specific work. |
| `ESST4B`                               | `ESST4B`                | `ExciterESST4B`                                           | **Implemented natively.** Exact DYR mapping, native PI/lag/rectifier equations, bounded-integrator limits, initialization, GENSAL coupling, and analytic Jacobians are covered. External UEL/OEL routing and a captured trajectory remain.                                                                                                                                                                                                                          |
| `ESST1A`, `ESAC1A`, `AC8B`, `IEEEX1`   | Same named ANDES models | None exact                                                | **No direct analogue.** Implement model-specific exciter blocks and DYR schemas, then add initialization and trajectory tests.                                                                                                                                                                                                                                                                                                                                      |
| `ESAC6A`, `SCRX`                       | `SEXS`                  | `ExciterSEXS`                                             | **Planned compatibility approximations only.** ANDES marks these conversions as TODO/approximate and currently discards most source parameters. GridKit supplies model documentation for both, but no runnable implementation.                                                                                                                                                                                                                                      |
| `EXPIC1`                               | `SEXS`                  | `ExciterEXPIC1`                                           | **Implemented; external trajectory open.** Native equations, bypasses, limits, initialization, residuals, source/rectifier Jacobians, and exact DYR mapping are tested against the GridKit specification. ANDES's SEXS conversion is not an equation reference.                                                                                                                                                                                                     |
| `TGOV1`                                | `TGOV1`                 | `GovernorTgov1`                                           | **Implemented.** Matches the ANDES v2.0.0 equations and PSS/e schema `R, T1, VMAX, VMIN, T2, T3, Dt`; DYR attachment, initialization, limiter, analytic-Jacobian, and isolated speed-step trajectory regressions are covered.                                                                                                                                                                                                                                       |
| `HYGOV`                                | `HYGOV`                 | `GovernorHygov`                                           | **Implemented.** Exact PSS/e DYR order, OpenIPSL/PowerDynamics temporary-droop lead, gate velocity/position limits, servo, nonlinear water-column equations, initialization, residual, and analytic-Jacobian coverage are present. ANDES deliberately ignores the input lead-lag, so direct ANDES trajectories are not an equation-parity reference for nonzero `Tr`; capture an OpenIPSL/PowerDynamics trajectory.                                                 |
| `IEESGO`                               | `IEESGO`                | `GovernorReheat`                                          | **Implemented; external trajectory pending.** The five-state IEEE/PSS/e realization includes controller and governor lead-lag dynamics, the valve limiter, steam-chest/reheater/crossover stages, and the K2/K3 output fractions. The DYR reader maps all 11 parameters; initialization, residual, and Jacobian coverage are present.                                                                                                                               |
| `IEEEG1`                               | `IEEEG1`                | `GovernorIeeeG1`                                          | **Implemented.** Frozen-ANDES equations and DYR order, initialization and perturbed equations, valve rate/position limits, analytic Jacobian, one- and two-machine attachment, and mixed synchronous-machine coupling are covered. Native ANDES import, alphanumeric DYR IDs, unequal machine bases, and a captured disturbed trajectory remain open.                                                                                                               |
| `GAST`                                 | `GAST`                  | `GovernorGast`                                            | **Implemented; external trajectory open.** Native low-value temperature gate, limited valve, turbine/temperature lags, damping-aware initialization, time-constant floors, residual/Jacobian checks, and DYR mapping follow OpenIPSL, ANDES, and GridKit `GASTPTI`.                                                                                                                                                                                                 |
| `GGOV1`                                | `TGOV1`                 | `GovernorGgov1`                                           | **Implemented natively.** GridDyn reads the original PSS/e GGOV1 record rather than reproducing ANDES's lossy TGOV1 conversion. Selectable droop, PID, limits, valve/turbine/temperature paths, initialization, machine-power coupling, and Jacobians are covered; nonzero `TENG` remains unsupported.                                                                                                                                                              |
| `ST2CUT`                               | `ST2CUT`                | `StabilizerST2CUT`                                        | **Partial.** Exact frozen-ANDES local modes 0/1/3/4/5, dual transducers, washout/lag, three lead-lag stages, output limits, voltage gating, DYR mapping, generator/exciter coupling, and dynamic load-step coverage are implemented. Remote `BUSR` inputs and ANDES modes 2/6 require cross-bus/frequency-derivative measurement routing and are rejected rather than approximated.                                                                                 |
| `IEEEST`                               | `IEEEST`                | `StabilizerIEEEST`                                        | **Partial.** Exact frozen-ANDES local modes 0/1/3/4/5, filter/lead-lag/washout zero-bypass semantics, output limits, voltage gate, DYR mapping, and GENROU/exciter coupling are implemented. Modes 2/6 and nonzero `BUSR` require unavailable cross-bus/frequency-derivative measurement routing and are rejected rather than approximated. A captured ANDES trajectory remains open.                                                                               |
| `REGCA1`, `REECA1`, `REECB1`, `REPCA1` | Same named ANDES models | `GenModelInverter` is not equivalent                      | **No direct analogue.** Add the coordinated renewable generator, electrical-control, and plant-control chain rather than flattening these records into the generic inverter.                                                                                                                                                                                                                                                                                        |
| `WTDTA1`, `WTARA1`, `WTPTA1`, `WTTQA1` | Same named ANDES models | None                                                      | **No direct analogue.** Add drive-train, aerodynamic, pitch, and torque-control submodels with their shared interfaces.                                                                                                                                                                                                                                                                                                                                             |
| `Toggle`, `Fault`                      | ANDES event models      | GridDyn event/action and fault mechanisms                 | **Partial conceptually.** Define DYR record schemas and translate target resolution, timing, status changes, and fault clearing semantics; no adapter exists.                                                                                                                                                                                                                                                                                                       |

### GAST equation and initialization contract

`GovernorGast` uses the common hard low-value-selector realization found in
OpenIPSL and ANDES, with GridKit `GASTPTI` as an independent C++ and
initialization reference. With GridDyn's absolute-speed input
`dw = omega - 1`, the implemented equations are:

@f[
V_D=P_{ref}-\frac{dw}{R},\qquad
V_T=A_T+K_T(A_T-x_T),\qquad V=\min(V_D,V_T),
@f]

@f[
\dot x_V=\frac{V-x_V}{T_1},\qquad
\dot x_F=\frac{x_V-x_F}{T_2},\qquad
\dot x_T=\frac{x_F-x_T}{T_3},\qquad
P_m=x_F-D_{turb}dw.
@f]

The valve equation uses directional anti-windup at its response bounds. An
initial flow outside the entered `VMIN`/`VMAX` range expands the corresponding
response bound to the initial value, as GridKit does explicitly and as ANDES's
state anti-windup permits; this prevents an artificial initialization jump but
allows motion back toward the entered range. Accepted zero time constants use
GridKit's 1 ms evaluation floor. Initialization includes turbine damping:

@f[
x_{F0}=P_{m0}+D_{turb}(\omega_0-1),\qquad
x_{V0}=x_{T0}=x_{F0},\qquad
P_{ref,0}=x_{F0}+\frac{\omega_0-1}{R}.
@f]

The start is rejected if the temperature selector cannot reproduce this
operating point. GridKit's optional `TRATE` base conversion is not part of the
PSS/e GAST record; GridDyn uses the machine/system-base handling already
provided by `DynamicGenerator`. PowerDynamics has only a GAST checklist entry
in the reviewed local source and therefore is not an equation reference.

### EXPIC1 equation and compatibility decisions

GridKit's EXPIC1 README and source block diagram are the equation authority.
OpenIPSL has no EXPIC1 model, and ANDES maps the record approximately to SEXS;
neither is used as an equation oracle. GridDyn nevertheless follows ANDES's
24-field PSS/e DYR schema exactly.

The PI block `KA(1+s TA1)/s` is realized with integral state `xA`:

@f[
e_V=V_{ref}+V_S-E_T-V_F,\qquad
V_A=\operatorname{clamp}(x_A+K_AT_{A1}e_V,V_{R2},V_{R1}),
\qquad \dot x_A=\operatorname{antiwindup}(x_A,K_Ae_V).
@f]

The regulator and feedback paths are:

@f[
T_{A2}\dot x_{R1}=V_A-x_{R1},\qquad
T_{A4}\dot V_R=x_{R1}+T_{A3}\dot x_{R1}-V_R,
@f]

@f[
\bar V_R=\operatorname{clamp}(V_R,V_{RMIN},V_{RMAX}),\qquad
T_{F1}\dot V_{F1}=\bar V_R-V_{F1},\qquad
T_{F2}\dot V_F=K_F\dot V_{F1}-V_F.
@f]

The shared static-exciter rectifier computes the GridKit source multiplier
`VB` from terminal dq voltage/current and field current using GridDyn's sign
convention. Both its value and analytic input derivatives feed EXPIC1. The
field equation is:

@f[
E_0=\operatorname{clamp}(V_B\bar V_R,E_{fd}^{min},E_{fd}^{max}),\qquad
T_E\dot E_{fd}=E_0-(K_E+S_E(E_{fd}))E_{fd}.
@f]

`SE1` and `SE2` fit the coefficient `SE(E)` itself; multiplying the points by
their field voltages before fitting would apply the field factor twice. At a
steady PI operating point `eV=0` and `xA=VA`. This follows the transfer
function and block diagram; the GridKit README's initialization line
`eV0=VA0/KA` conflicts with its own differential equation and is not used.
The current GridDyn exciter interface provides the voltage-reference and PSS
signals but no separate UEL/OEL inputs, so those two optional GridKit diagram
terms are zero. Its voltage input is likewise the compensated-voltage signal
available from `DynamicGenerator` rather than a separate compensator model.

The supported exact reductions are `TR=0` (direct terminal-voltage sensing),
`TA2=TA3=TA4=0` (regulator-filter bypass), `KF=0` (feedback path omitted), and
`TE=0` (the source-diagram algebraic output `Efd=E0`). Partially degenerate
regulator filters and nonzero `KF` with a zero feedback time constant are
rejected instead of silently changing the model order. Component tests cover
the full perturbed equations, saturation, both regulator limits, bypasses,
factories/cloning, and validation. The IEEE-14 DYR integration test covers
initialization, residuals, and analytic Jacobians with nonzero `KP`, `KI`, and
`KC`. An independent external trajectory remains the outstanding validation
item. PowerDynamics contains no applicable EXPIC1 implementation in the
reviewed local source.

#### Follow-up stability-test TODO

- Add dedicated disturbed-trajectory cases for `ExciterEXPIC1` and
  `GovernorGast` using representative DYR parameters rather than the generic
  controller defaults.
- Exercise EXPIC1 PI and final-regulator limits, saturation, source/rectifier
  loading, and the documented zero-time bypass configurations over time.
- Exercise GAST speed-droop/temperature-selector transitions, valve
  anti-windup in both directions, damping, and initialized flow outside the
  entered response limits.
- Compare at least one trajectory for each model against an independently run
  GridKit, OpenIPSL, or PSS/e case where an exact model is available. GridKit
  is the preferred EXPIC1 specification source; OpenIPSL, ANDES, and GridKit
  are applicable GAST references.

### EXAC1 and EXAC2 voltage-transducer compatibility decision

The frozen ANDES v2.0.0 `EXAC1Model` constructs the voltage-transducer lag
`LG = Lag(v, TR)`, but its regulator-input equation uses raw terminal voltage
`v` rather than `LG_y`. Consequently `TR` creates an unconsumed state and has
no effect on the frozen ANDES EXAC1 response. `EXAC2Model` derives from the
same implementation and inherits this behavior. This appears inconsistent
with the PSS/E/IEEE AC1 block diagram, where the sensed terminal voltage is
the transducer output.

GridDyn will implement the physically intended connection for both models:

@f[
T_R\dot V_m=V_t-V_m,\qquad V_i=V_{ref}-V_m-V_F.
@f]

`ExciterEXAC1` and `ExciterEXAC2` Doxygen comments and tests name this
intentional difference. When `TR` is nonzero, tests derived solely from frozen
ANDES values must be adjusted or separately labeled; PSS/E or a second
independent implementation is required to validate the corrected trajectory.
This is a documented compatibility correction, not a claim that ANDES
behavior is exact.

#### Copy-ready ANDES issue draft: EXAC1/EXAC2 `TR` transducer is disconnected

```text
Title: EXAC1 and EXAC2 do not apply the TR voltage-transducer output

In ANDES v2.0.0, `andes/models/exciter/exac1.py` creates the EXAC1 voltage
transducer as

    self.LG = Lag(self.v, T=self.TR, K=1, info='Voltage transducer')

but the regulator summing input is defined using raw terminal voltage:

    self.vi.e_str = 'ue * (-v + vref - WF_y - vi)'

`LG_y` is not referenced by the EXAC1 regulator path. Therefore changing TR
adds an unconsumed lag state but does not change the EXAC1 response. EXAC2
inherits EXAC1Model, so it has the same issue.

Expected AC1/PSS/E block-diagram connection:

    TR * d(LG_y)/dt = v - LG_y
    vi = vref - LG_y - WF_y

Suggested minimal correction:

    self.vi.e_str = 'ue * (-LG_y + vref - WF_y - vi)'
    self.vi.v_str = '-LG_y + vref'

Suggested regression: run two otherwise identical EXAC1 cases with TR = 0
and TR > 0 after a terminal-voltage or reference-voltage step. Before the
fix, their regulator trajectories are identical apart from the unused LG
state; after the fix, the nonzero-TR case should show the expected sensing
delay. Repeat the same regression for EXAC2.

This report concerns the regulator sensing path only. It does not propose a
change to EXAC1/EXAC2 saturation, FEX, field-current feedback, or limiter
equations.
```

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

| PR  | Deliverable                                                                                                   | Status                                              | Depends on |
| --- | ------------------------------------------------------------------------------------------------------------- | --------------------------------------------------- | ---------- |
| 1   | Complete GENROU equations, saturation utility, DYR mapping, and five-machine initialization reference         | Current changes; treat as complete after merge      | none       |
| 2   | Robust RAW/DYR identity and base handling, IEEE 14 power-flow parity, and a controller-free GENROU trajectory | Planned                                             | PR 1       |
| 3   | Generator/controller signal plumbing, PSS-to-exciter routing, and validated `TGOV1`                           | Implemented                                         | PR 2       |
| 4   | Complete `ESST3A` and `EXST1` models plus DYR adapters                                                        | Models/adapters implemented; trajectories open      | PR 3       |
| 5   | Complete `IEEEG1`, `ST2CUT`, and `IEEEST` models plus DYR adapters                                            | `IEEEG1` implemented; `ST2CUT` and `IEEEST` partial | PR 4       |
| 6   | DYR `Toggle`, complete IEEE 14 initialization/equilibrium, and the two-second trajectory regression           | Planned                                             | PR 5       |

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
- Complete `TGOV1` DYR mapping. The schema `R, T1, VMAX, VMIN, T2, T3, Dt`
  is mapped in that order, with three-record attachment coverage.
- Complete the `GovernorTgov1` audit against ANDES v2.0.0: droop, damping,
  limiting/root behavior, initialization, and machine-base operation are
  verified; limiter equations and the analytic Jacobian are covered.

Independent tests:

- A probe submodel verifies every new signal's value, sign, base, and Jacobian
  location at initialization and a perturbed GENROU state.
- A test stabilizer produces a known `Vss`; verify it changes the exciter
  summing input and ultimately GENROU field voltage with the correct Jacobian.
- DYR parameter/attachment tests for all three IEEE 14 `TGOV1` records.
- Isolated TGOV1 initialization, limit transitions, and speed-step trajectory
  against the ANDES equations, plus a GENROU+TGOV1 initialization/Jacobian
  integration test.

Likely touchpoints include `GenModel`, `GenModelGENROU`, `DynamicGenerator`,
`Exciter`, `Stabilizer`, `GovernorTgov1`, the DYR adapters, and component plus
ANDES compatibility tests. Do not implement the two exciters or three remaining
governor/PSS classes in this PR.

#### Implemented synchronous-machine controller signals

The exciter input contract preserves the legacy inputs at indices 0 through 3
and appends the following machine/controller signals:

| Index | Signal   | Definition                                                       |
| ----: | -------- | ---------------------------------------------------------------- |
|     4 | `Id`     | Direct-axis stator current on the machine base                   |
|     5 | `Iq`     | Quadrature-axis stator current on the machine base               |
|     6 | `Vd`     | Direct-axis terminal voltage in GridDyn's dq convention          |
|     7 | `Vq`     | Quadrature-axis terminal voltage in GridDyn's dq convention      |
|     8 | `Te`     | Electrical torque including stator copper loss                   |
|     9 | `XadIfd` | Air-gap field-current quantity or documented reduced-order proxy |
|    10 | `Vss`    | Stabilizer output                                                |

All synchronous generator models provide `Id`, `Iq`, `Vd`, `Vq`, and `Te`
from their existing electrical states. `GENROU` provides the full-order
`XadIfd` equation. Third- through eighth-order machines derive `XadIfd` from
the retained transient q-axis state and direct-axis current. The classical
model has no field-winding state and therefore uses excitation voltage as a
coupled per-unit proxy. This approximation is intended to keep detailed
exciters usable with reduced-order machines; it is not a claim that the
reduced-order machine reproduces full GENROU field-current dynamics.

Non-synchronous and trivial generator models retain `kNullVal` for unsupported
machine signals. Exciters that fundamentally require synchronous-machine dq
quantities reject those combinations during initialization. Existing exciters
remain on the four-input contract and do not enter the extended-signal or
Jacobian path.

### PR 4: ESST3A and EXST1 excitation systems

`ESST3A` is merged and complete. The remaining PR 4 work is `EXST1`; retain
the ESST3A requirements below as the completion record for the merged model.

Fresh-context task statement: implement the remaining excitation-system model
used by the IEEE 14 generators, using the PR 3 signal contract and exact ANDES
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

#### EXST1 implementation and parameter contract

`ExciterEXST1` follows ANDES 2.0.0
`andes/models/exciter/exst1.py` except for the output-limiter selector
documented below. The DYR adapter follows the frozen `psse-dyr.yaml` order
exactly:

| DYR position after `BUS, ID` | ANDES / GridDyn parameter |
| ---------------------------: | ------------------------- |
|                            1 | `TR` / `tr`               |
|                         2, 3 | `VIMAX`, `VIMIN`          |
|                         4, 5 | `TC`, `TB`                |
|                         6, 7 | `KA`, `TA`                |
|                         8, 9 | `VRMAX`, `VRMIN`          |
|                   10, 11, 12 | `KC`, `KF`, `TF`          |

The output-limiter selector intentionally differs from frozen ANDES 2.0.0.
ANDES passes `WF_y` to its output `HardLimiter` but applies the resulting
flags to `LR_y`. This can leave `LR_y` outside `VRMIN`/`VRMAX` without
limiting it, or clamp it because an unrelated washout excursion crossed a
bound. The published PSS/E EXST1 block diagram places these limits around the
regulator output, so GridDyn selects the output-limiter branch from `LR_y`.
The input and output limiters remain hard algebraic limiters and do not freeze
a differential state, so EXST1 has no anti-windup approximation. Both limiter
surfaces are exposed as roots. The terminal-voltage scaling shown on some
published EXST1 output-limit diagrams remains unresolved; GridDyn retains the
frozen ANDES bounds `VRMAX - KC * XadIfd` and `VRMIN - KC * XadIfd` pending
future cross-model trajectory comparisons.

All synchronous-machine families accepted by the PR 3 controller-signal
contract can use EXST1. GENROU supplies the exact `XadIfd`; reduced-order
synchronous machines retain the already documented `XadIfd` proxy. Models
that return no physical/defined `XadIfd` signal are rejected at
initialization. No additional generator approximation was added for EXST1.

Current unsupported cases are nonpositive `TR`, `TB`, or `TA`. ANDES can
represent a zero-time-constant block as an algebraic/bypass equation, whereas
this GridDyn class currently validates those three time constants as strictly
positive. `TF` is also strictly positive, matching the frozen ANDES parameter
constraint. A minimized DYR mapping fixture, captured IEEE 14 initialization,
perturbed equations, corrected regulator-output limiter roots, clone/factory
checks, synchronous-family coupling, and finite-difference Jacobian checks are
complete. A captured disturbed GENROU+EXST1 trajectory and native ANDES
JSON/XLSX construction are still open and are not claimed by this change.

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

#### IEEEG1 implementation and parameter contract

`GovernorIeeeG1` follows frozen ANDES v2.0.0
`andes/models/governor/ieeeg1.py`. Its DYR adapter uses the exact frozen
`psse-dyr.yaml` order `BUS, ID, BUS2, ID2, K, T1, T2, T3, UO, UC, PMAX,
PMIN, T4, K1, K2, T5, K3, K4, T6, K5, K6, T7, K7, K8`. The eight power
coefficients are normalized by their sum, as in ANDES. `T1=0` and zero
turbine-stage time constants are exact algebraic bypasses and do not allocate
unnecessary differential states.

The valve rate is limited to `[UC, UO]`; valve-position anti-windup holds at
`[PMIN, PMAX]` only while requested motion points farther into the active
limit. No equation discrepancy from frozen ANDES is known. ANDES marks
governor scheduling as unsupported, so GridDyn likewise holds the initialized
reference and does not claim a dynamic `paux`/dispatch input.

With `BUS2=0`, the adapter uses the high-pressure output on the primary
generator and requires `K2`, `K4`, `K6`, and `K8` to be zero. With a second
machine, the governor remains owned and evaluated once by the primary
generator, while the generic indexed mechanical-power connection routes its
low-pressure output to the secondary. The secondary may use a different
synchronous-generator model class and may retain an unrelated local governor;
the explicit mechanical source selects which output drives its machine.
`GenModel4` plus `GenModel6` is covered. Exact two-machine operation currently
requires equal machine MBASE values; base conversion for unequal machines,
alphanumeric DYR machine IDs, native ANDES input, and a captured disturbed
trajectory remain open.

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

#### ST2CUT implementation and parameter contract

`StabilizerST2CUT` follows frozen ANDES v2.0.0
`andes/models/pss/st2cut.py`; its DYR adapter uses the exact frozen
`psse-dyr.yaml` order `BUS, ID, MODE, BUSR, MODE2, BUSR2, K1, K2, T1, T2,
T3, T4, T5, T6, T7, T8, T9, T10, LSMAX, LSMIN, VCU, VCL`. The two input
transducers feed the ANDES washout-or-lag block, three lead-lag blocks, the
`[LSMIN, LSMAX]` output limiter, and the initialized-voltage-relative
`[VCL, VCU]` output gate. As in ANDES, zero `VCU`/`VCL` is mapped to
`+999`/`-999` for an effectively disabled voltage gate.

GridDyn currently provides local rotor speed, terminal voltage, mechanical
power, and electrical power. Therefore local modes 0, 1, 3, 4, and 5 are
implemented and tested. ANDES mode 2 (remote bus-frequency), mode 6 (terminal
voltage derivative), and nonzero `BUSR`/`BUSR2` need a generic cross-bus
measurement-routing interface that GridDyn does not yet have; these records
are rejected at parsing/initialization rather than silently using a local
signal. `T3=0` uses the exact ANDES lag form. The nonzero lag denominators
`T1`, `T2`, `T4`, `T6`, `T8`, and `T10` are required; zero-denominator block
bypasses remain unsupported.

Focused tests cover DYR mapping, initialization and perturbed equations,
analytic-Jacobian finite differences, limiter and voltage-gate root
transitions, GENROU plus exciter coupling, and a load-step dynamic run whose
ST2CUT states must evolve. A captured ANDES trajectory and support for the
remote/derivative measurement modes remain open.

#### IEEEST implementation and parameter contract

`StabilizerIEEEST` follows frozen ANDES v2.0.0
`andes/models/pss/ieeest.py`. Its DYR adapter uses the exact frozen
`psse-dyr.yaml` order `BUS, ID, MODE, BUSR, A1, A2, A3, A4, A5, A6, T1,
T2, T3, T4, T5, T6, KS, LSMAX, LSMIN, VCU, VCL`. The detailed, buildable
Doxygen equations and source references are in
`src/griddyn/stabilizers/StabilizerIEEEST.h`. In summary, the selected signal
passes through the second-order lag `F1`, second-order lead-lag `F2`, two
lead-lag stages, gain `KS`, and ANDES `WashoutOrLag`, before the
`[LSMIN, LSMAX]` output limit and initialized-terminal-voltage-relative
`[VCL, VCU]` gate. Zero `VCU`/`VCL` maps to `+999`/`-999`, as in ANDES.

The exact ANDES `zero_out` forms are retained: `A4=0` bypasses `F2`, `T2=0`
bypasses the first lead-lag, and `T4=0` bypasses the second lead-lag. `A2=0`
is represented as the exact first-order `F1` reduction when `A1>0`, or as a
bypass when both are zero; no singular dummy state is allocated. The actual
IEEE 14 IEEEST record exercises these reductions. `T5=0` selects ANDES's
low-pass output rather than the washout output.

GridDyn currently provides local rotor speed, terminal voltage, mechanical
power, and machine electrical torque/power. Therefore local modes 0, 1, 3, 4,
and 5 are implemented and tested. ANDES mode 2 (bus-frequency), mode 6
(voltage derivative), and nonzero `BUSR` require a general cross-bus
measurement-routing interface and are rejected rather than silently
substituted. Focused tests cover DYR mapping, initialization and perturbed
equations, analytic-Jacobian finite differences, limiter/gate roots, bypass
semantics, GENROU plus exciter coupling, and a load-step dynamic run. A
captured ANDES trajectory remains open.

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

| Area                       | Expected files / change                                                                                                                                                                                                                                                                                                          |
| -------------------------- | -------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------- |
| Canonical machine model    | **Done:** `src/griddyn/genmodels/GenModelGENROU.{h,cpp}` is complete at the equation/initialization level and `genrou` is registered in `src/griddyn/genmodels/GenModel.cpp`; generic model `6` remains unchanged.                                                                                                               |
| DYR parsing and attachment | **Partial:** GENROU creates the dedicated model and applies parameters in the correct RAW/DYR order. Refactor `src/fileInput/gridDynReadDYR.cpp` into schema-backed adapters and add exact bus-plus-machine-ID lookup support.                                                                                                   |
| Native ANDES import        | Extend the ANDES JSON reader's dynamic-object dispatch to build the same registered machine/controller classes and control connections used by DYR adapters.                                                                                                                                                                     |
| Model tests                | **Mostly done:** focused equation, Jacobian, initialization, saturation, invalid-parameter, and factory tests are present; add a disturbed-trajectory reference and clone regression as the model is integrated.                                                                                                                 |
| Reader tests               | **Partial:** `test/andesTests/testAndesDyrReader.cpp` is a dedicated RAW/DYR GENROU attachment and initialization test. Add parser edge cases and disturbed trajectories as support expands.                                                                                                                                     |
| Numerical references       | **Partial:** the IEEE 14-bus RAW input, minimized GENROU DYR input, captured GENROU initialization reference, and first GENROU+TGOV1 trajectory reference are stored under `test/test_files/andes_tests/` and run without importing ANDES. Add trajectories for the remaining controllers and cleared network disturbances next. |

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

| ANDES model(s)                                                                                                        | GridDyn mapping / next action                                                                                                                                                                                           | Status                                |
| --------------------------------------------------------------------------------------------------------------------- | ----------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------- | ------------------------------------- |
| `GENCLS`                                                                                                              | `GenModelClassical`; corrected standard equations, DYR/RAW parameter transfer, initialization, reader, residual, and Jacobian tests are present. A captured external trajectory remains.                                | Implemented                           |
| `GENROU`                                                                                                              | `GenModelGENROU` equations, DYR mapping, and five-machine initialization match ANDES references; fix exact machine identity/base handling, native ANDES import, and trajectory parity.                                  | Partial                               |
| `GENSAL`                                                                                                              | `GenModelGENSAL`; native salient-pole equations, quadratic saturation, exact DYR mapping, initialization, controller interfaces, and analytic Jacobians are covered. A captured external trajectory remains.            | Implemented                           |
| `TGOV1`                                                                                                               | `GovernorTgov1`; ANDES-equation, DYR-order, limiter, initialization, Jacobian, and isolated trajectory regressions are complete, including a captured IEEE 14 GENROU+TGOV1 `Alter(pref0)` trajectory.                   | Implemented                           |
| `EXDC2`                                                                                                               | `ExciterDC2A` candidate; add omitted transducer/switch/saturation behavior and prove equation equivalence.                                                                                                              | Partial                               |
| `ZIP`, `FLoad`                                                                                                        | GridDyn static/dynamic load models; identify exact parameter and frequency-response equivalence.                                                                                                                        | Partial                               |
| `Motor3`, `Motor5`                                                                                                    | `MotorLoad3`, `MotorLoad5`; parameter mapping and trajectory comparisons required.                                                                                                                                      | Partial                               |
| `ACE`, `ACEc`, `COI`                                                                                                  | Map area-control and center-of-inertia services.                                                                                                                                                                        | Untriaged                             |
| `PLBVFU1`, `IEEEVC`                                                                                                   | No direct voltage-compensator/playback analogue; define exciter input and playback interfaces.                                                                                                                          | No direct analogue                    |
| `IEEEG1`                                                                                                              | `GovernorIeeeG1`; frozen equations, limits, DYR mapping, one-/two-machine connections, initialization, and Jacobian checks are complete; unequal MBASE, native import, and disturbed trajectory remain.                 | Implemented                           |
| `HYGOV`                                                                                                               | `GovernorHygov`; exact DYR order, temporary-droop lead, gate limits, servo, nonlinear water column, initialization, residual, and Jacobian coverage are present. An external trajectory remains.                        | Implemented                           |
| `TG2`, `TGOV1DB`, `TGOV1N`, `TGOV1NDB`, `IEESGO`, `HYGOVDB`, `HYGOV4`                                                 | Existing hydro/reheat/steam classes are candidates only; each needs an equation audit or a new model.                                                                                                                   | Planned                               |
| `GAST`                                                                                                                | `GovernorGast`; native equations, initialization, selector/limit behavior, DYR mapping, and Jacobians are covered. Capture a GridKit/independent disturbed trajectory.                                                  | Implemented; external trajectory open |
| `GGOV1`                                                                                                               | `GovernorGgov1`; all PSS/e fields are mapped and the native controller/turbine equations, limits, initialization, machine-power signal, and Jacobians are covered. Nonzero `TENG` remains unsupported.                  | Implemented                           |
| `SHAFT5`                                                                                                              | Multi-mass shaft model; map states and mechanical interfaces.                                                                                                                                                           | Untriaged                             |
| `ESDC1A`, `ESDC2A`, `SEXS`, `IEEET1`, `IEEET3`                                                                        | Existing DC/IEEE/SEXS exciters are candidates; complete model-specific audits and DYR/native mappings.                                                                                                                  | Partial                               |
| `ESST3A`                                                                                                              | `ExciterESST3A`; exact GENROU path plus documented reduced-order synchronous-machine signal approximations.                                                                                                             | Implemented                           |
| `EXST1`                                                                                                               | `ExciterEXST1`; positive-time-constant equations and DYR mapping, with the documented corrected regulator-output limiter; zero-time-constant blocks, native import, and a captured trajectory remain.                   | Implemented                           |
| `ESST4B`                                                                                                              | `ExciterESST4B`; exact DYR mapping, native equations, initialization, GENSAL coupling, and analytic Jacobians are covered. External UEL/OEL routing and a captured trajectory remain.                                   | Implemented                           |
| `IEEEX1`, `EXAC1`, `EXAC2`, `EXAC4`, `AC8B`, `ESAC1A`, `ESST1A`, `ESAC5A`                                             | Implement missing models individually rather than mapping by family name; note that EXAC1/2/4 already have native implementations described above.                                                                      | Mixed                                 |
| `EXPIC1`                                                                                                              | `ExciterEXPIC1`; native GridKit-specified PI/filter/feedback/source/rectifier equations, DYR adapter, whole-case initialization, and Jacobians are covered. Capture an independent external trajectory.                 | Implemented; external trajectory open |
| `SCRX`, `ESAC6A`                                                                                                      | GridKit supplies model documentation; no GridDyn implementation or DYR adapter yet. Add model-specific limit, initialization, and saturation behavior rather than substituting a similarly named exciter.               | Source-backed                         |
| `ST2CUT`                                                                                                              | `StabilizerST2CUT`; local input modes, exact filters, limits, DYR mapping, and dynamic load-step coverage are implemented. Remote/frequency-derivative measurements and captured ANDES trajectories remain.             | Partial                               |
| `IEEEST`                                                                                                              | `StabilizerIEEEST`; local input modes, exact ANDES zero-bypass filters, limits, DYR mapping, and dynamic load-step coverage are implemented. Remote/frequency-derivative measurements and captured trajectories remain. | Partial                               |
| `BusFreq`, `BusROCOF`, `PMU`, `PLL1`, `PLL2`, `FreqDiv`                                                               | Measurement and frequency-estimation models.                                                                                                                                                                            | Untriaged                             |
| `REGCA1`, `REGCP1`, `REECA1`, `REECA1E`, `REECA1G`, `REECB1`, `REPCA1`, `REGCV1`, `REGCV2`, `REGF1`, `REGF2`, `REGF3` | Generic `GenModelInverter` is insufficient; add composable generator, electrical, plant, and frequency controls.                                                                                                        | No direct analogue                    |
| `WTDTA1`, `WTDS`, `WTARA1`, `WTPTA1`, `WTTQA1`, `WTARV1`                                                              | Add wind-turbine drive-train, aerodynamic, pitch, torque, and renewable-voltage submodels and interfaces.                                                                                                               | No direct analogue                    |
| `USRBUS`, `USRMDL`                                                                                                    | PSS/E user-written model records require the supplied compiled-model equations or equivalent documentation. The Texas7k records reference `PLNTBU1`, `REAX3BU1`, and `REAX4BU1`; DYR parameters alone are insufficient. | External dependency                   |
| `PVD1`, `ESD1`, `EV1`, `EV2`, `DGPRCT1`, `DGPRCTExt`                                                                  | Distributed energy-resource and protection models.                                                                                                                                                                      | Untriaged                             |
| `Fault`, `Alter`, `TimeSeries`, `Toggle`                                                                              | Event/action semantics and time-series input mapping.                                                                                                                                                                   | Untriaged                             |
| `Summary`, `Output`                                                                                                   | Reporting configuration; define output-channel mapping after model compatibility.                                                                                                                                       | Untriaged                             |

### Texas7k dynamic-model demand

`C:\Users\phlpt\Downloads\Texas7k_20210804_Plus2023\Texas7k_20210804.dyr`
was inspected statically and was not loaded or run. It has 2,705 records in
24 model families. The current DYR reader recognizes 1,777 records (`GENROU`,
`IEEEST`, `EXAC2`, `IEEEG1`, `ESDC1A`, `ESDC2A`, `EXAC1`, `GGOV1`, `ESST4B`,
`GENSAL`, `HYGOV`, `IEEET1`, and `EXPIC1`); 928 records require
additional support.

The largest missing demands are the coupled renewable groups: `REGCA1` (189),
`REECA1` (182), `REPCA1` (174), `WTARA1`
(121), `WTTQA1` (121), and `WTPTA1` (105). The remaining missing records are
`USRMDL` (15), `SCRX` (9), `USRBUS` (6), and three each of `ESAC1A` and
`ESAC6A`.

A paired `RAW` plus `DYR` import was attempted on the unmodified base files.
The reader correctly identifies the unsupported families, but it is not yet a
strict loader: it prints an unknown-model line and continues. The first
`EXAC1` record (`BUS=111208`, machine ID `1`) and the other two `EXAC1`
records use `TR=0`. GridDyn now treats this as the specified bypassed
measurement/transducer lag: it uses terminal voltage directly and omits the
transducer differential state. The zero-`TR` path is covered by a DYR-loaded
IEEE-14 regression with residual and Jacobian checks. `EXAC2` remains
positive-`TR` only because its specialized equations still assume the
five-state layout.

Full Texas7k dynamics still requires support for the 928 unsupported records.
The merged model and EXAC1 bypass work reduces the recognized-model gap but
makes no end-to-end Texas7k trajectory claim.

### Texas7k RAW support required

The base `Texas7k_20210804.RAW` is PSS/E v33 and contains 6,717 buses, 731
generators, 7,173 branches, 1,967 two-winding transformers, 205 fixed shunts,
and 429 switched shunts. Its transformer records use only `CW=1`, `CZ=1`,
`CM=1`, and `COD=0`; no three-winding transformer support is required for that
base case.

### Texas7k base-case execution results

The unmodified `Texas7k_20210804.RAW`, `.m`, and `.EPC` files each complete a
GridDyn Debug power-flow run. RAW and EPC load 6,717 buses, 9,140 links, 731
generators, and 5,729 load objects. The MATPOWER reader loads the same buses,
links, and generators but aggregates its demand into 4,648 load objects. The
maximum reported bus residuals are:

| Input          | Maximum active residual (MW) | Maximum reactive residual (MVAr) |
| -------------- | ---------------------------: | -------------------------------: |
| RAW            |                            0 |                          0.00472 |
| MATPOWER `.m`  |                            0 |                          0.51257 |
| PowerWorld EPC |                      0.00005 |                          0.00822 |

RAW and MATPOWER retain all 6,717 common bus IDs; their exported solved states
differ by at most `0.00005` pu in voltage magnitude and `0.0022` degrees in
angle at the report precision. The EPC reader assigns sequential internal bus
IDs, but preserves source record order. Matching all 6,717 EPC records to RAW
records by that order gives a maximum voltage difference of `0.000004` pu and
a maximum angle difference of `0.0001` degrees. This correct pairing is needed
because PSS/E's 12-character bus-name limit produces duplicate truncated names
in RAW; GridDyn disambiguates those names differently from the EPC reader.
The previous name-only comparison therefore paired 12 duplicate names to the
wrong buses and falsely reported a `0.029486` pu state difference. The EPC
reader also reports nonfatal unsupported `injgroup` and `injgrpelem` metadata
tokens.

The 2022 and 2030 files are PSS/E v35. Before they can be safely loaded, the
RAW reader needs to skip `@!` field-header comments and implement v35 schemas
for transformer and switched-shunt records. v35 transformer winding records
place control and correction-table fields after twelve rating columns; the
reader currently uses v33 positions, which would lose the two `COD=-3` fixed
phase shifters in each file. v35 switched shunts add an ID field, shifting all
voltage-control and block fields; this affects 429 shunts in the 2022 file and
479 in the 2030 file.

Both v35 files additionally contain unsupported nonempty PSS/E sections:
two-terminal DC, VSC DC, multi-terminal DC, FACTS, multisection line,
induction-machine, and system-switching-device data. They also contain 99
impedance-correction cards; the current correction interpolation can be reused
once v35 parsing reaches the transformer records. Zone, owner, area, and
inter-area-transfer entries remain metadata/transfer-control gaps rather than
ordinary AC-network elements.

### ACTIVSg70k dynamic-model demand and RAW support

`C:\Users\phlpt\Downloads\ACTIVSg70k\ACTIVSg70k_dynamics.dyr` was inspected
statically and was not loaded or run. It has 40,418 records in 22 model
families. The current DYR dispatch recognizes 34,790 records in `GENROU`,
`IEEEST`, `IEEEG1`, `ESDC1A`, `ESDC2A`, `EXAC1`, `EXAC2`, `GGOV1`, `ESST4B`,
`GENSAL`, `HYGOV`, `IEEET1`, and `EXPIC1`; 5,628 records remain unsupported.
The highest-demand missing model is `SCRX` (1,053). The coupled renewable
requirement is `REGCA1` and `REECA1` (571 each)
plus `WT3G1`, `WT3E1`, `WT3P1`, and `WT3T1` (576 each).

For an executable full-dynamics reference, treat the DYR as plant assemblies,
not independent records: `GENSAL` and `HYGOV` must be available together;
`REGCA1` and `REECA1` must be available together; and all four `WT3*` models
must be available together. `GGOV1` must retain its selectable governor and
turbine modes and must not be replaced with `TGOV1`. The complete remaining
unsupported set is `SCRX`, `ESAC6A`, `WT3P1`, `WT3T1`, `WT3E1`, `WT3G1`,
`REGCA1`, `REECA1`, and `ESAC1A`; reject a full
DYR run until all are supported rather
than silently dropping a controller. The companion `.PWB`, `.aux`, `.pwd`,
and `.EPC` files are PowerWorld references, while the GridDyn import/validation
pair is `ACTIVSg70k.RAW` plus `ACTIVSg70k_dynamics.dyr`.

`ACTIVSg70k.RAW` is PSS/E v33 and contains 67,900 terminal buses, 71,352
ordinary branches, 10,555 two-winding transformers, and 2,100 three-winding
transformers. Delta-to-star expansion gives the same 70,000 buses and 88,207
links as `case_ACTIVSg70k.m`. All transformers use `CW=1`, `CZ=1`, and `CM=1`;
2,083 winding records use fixed/manual `COD=-1`. There are no nonempty DC,
FACTS, multisection-line, induction-machine, or system-switching-device
sections. No additional RAW record family is needed, but whole-case validation
of three-winding and fixed-control transformer behavior is required before
accepting a power-flow/dynamic reference.

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
