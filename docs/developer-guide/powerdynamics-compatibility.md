# PowerDynamics.jl dynamic-model assessment

This document identifies dynamic models implemented by the local [PowerDynamics.jl](https://github.com/JuliaEnergy/PowerDynamics.jl) project that GridDyn lacks, plus recently implemented GridDyn models for which PowerDynamics can supply external validation. It complements the [OpenIPSL assessment](openipsl-compatibility.md): several PowerDynamics models are explicit Julia ports of OpenIPSL PSS/e models, so PowerDynamics supplies implementation and regression-test evidence rather than a separate physical-model family.

## Scope and evidence

The inspected checkout is `C:\data\Documents\codeProjects\PowerDynamics.jl`, commit `908306c67b85fb24249955277b97e2e4f3d9b837` (2026-08-28).

The primary inventory is the public model list in `src/Library/Library.jl`. The project registers OpenIPSL comparison tests for `GENCLS`, `GENROU`, `GENROE`, `GENSAL`, `GENSAE`, `IEEET1`, `SCRX`, `ESST1A`, `ESST4B`, `EXST1`, `IEEEG1`, `GGOV1`, `HYGOV`, and `IEEEST` in `test/runtests.jl`. Its `test/OpenIPSL_test/ModelTransferRules.md` also documents the source-model-to-Julia translation and reference-generation workflow.

That is strong source material, but it is not GridDyn validation. A C++ port still needs an equation audit, GridDyn initialization test, and checked-in reference trajectory. Do not run Julia or a Modelica compiler from the regular GridDyn test suite.

## Priority definitions

| Priority                            | Meaning                                                                                                                                     |
| ----------------------------------- | ------------------------------------------------------------------------------------------------------------------------------------------- |
| **P0 — port and validate**          | PowerDynamics has a model-specific implementation and OpenIPSL test path; GridDyn has no exact model.                                       |
| **P1 — high-value extension**       | A commonly useful GridDyn capability is missing, although PowerDynamics validation is incomplete or the work requires a reusable interface. |
| **P2 — targeted extension**         | Useful but specialized, experimental, or dependent on a P0/P1 design decision.                                                              |
| **P3 — different simulation scope** | The model retains fast electromagnetic/filter states or needs structural changes beyond GridDyn's present positive-sequence dynamic path.   |

## Model gaps and recently implemented validation targets

### 1. Synchronous machines

| PowerDynamics model      | GridDyn status                                       | Priority | Why it matters                                                                                                                                                                                                               |
| ------------------------ | ---------------------------------------------------- | -------- | ---------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------- |
| `PSSE_GENROE`            | No exact round-rotor exponential-saturation machine  | **P0**   | PowerDynamics has a registered OpenIPSL regression. It is the direct route to PSS/e `GENROE`, not a justification to substitute GridDyn `GENROU` (quadratic saturation).                                                     |
| `PSSE_GENSAL`            | `GenModelGENSAL`                                     | **Validation** | Native equations, DYR mapping, initialization, controller signals, and analytic Jacobians are implemented; use the PowerDynamics/OpenIPSL path for a captured trajectory. |
| `PSSE_GENSAE`            | No exact salient-pole exponential-saturation machine | **P0**   | Has an OpenIPSL test path; naturally follows `GENSAL` once the salient-pole structure exists.                                                                                                                                |
| `SauerPaiMachine`        | No verified exact equivalent                         | **P1**   | A detailed synchronous-machine formulation with explicit dq flux and torque variables. Audit against `GenModel6`/`GenModel8` before deciding whether it is a new model or a validation target for an existing generic model. |
| `Swing`                  | No standalone swing-injection component              | **P2**   | A reusable mechanical-power-to-angle/frequency block with a prescribed voltage magnitude. GridDyn generator models overlap physically, but not necessarily as a composable network injection.                                |
| `VariableFrequencySlack` | No equivalent dynamic slack model identified         | **P2**   | Supplies a rotating slack voltage driven by a configurable frequency. Useful for benchmark/reference cases and grid-forming studies.                                                                                         |

### 2. Excitation systems and governor controls

| PowerDynamics model                                   | GridDyn status                                            | Priority          | Why it matters                                                                                                                                                                                                         |
| ----------------------------------------------------- | --------------------------------------------------------- | ----------------- | ---------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------- |
| `PSSE_SCRX`                                           | No exact static exciter                                   | **P0**            | PowerDynamics has an OpenIPSL test. Requires its lead-lag, bus-/solid-fed switching, crowbar, and field-current behavior—not merely a generic high-gain AVR.                                                           |
| `PSSE_ESST1A`                                         | No exact static exciter                                   | **P0**            | OpenIPSL-tested model with limiter, rectifier-loading, and feedback behavior; a useful common PSS/e exciter target.                                                                                                    |
| `PSSE_ESST4B`                                         | `ExciterESST4B`                                           | **Validation**    | Native equations and DYR/Jacobian coverage are implemented; use the OpenIPSL-tested Julia path for an external trajectory. UEL/OEL routing remains open. |
| `PSSE_GGOV1_EXPERIMENTAL`                             | `GovernorGgov1`                                           | **Validation**    | Native GGOV1 is implemented and independently equation-audited. PowerDynamics remains secondary evidence because its model is experimental and its reference documents known issues. |
| `PSSE_IEEET1`                                         | `ExciterIEEEtype1` is a candidate                         | **P1 validation** | The PowerDynamics/OpenIPSL test is a concrete starting point for proving or rejecting equivalence.                                                                                                                     |
| `AVRFixed`, `AVRTypeI`, `GovFixed`, `TurbineGovTypeI` | Existing GridDyn AVR/governor classes are only candidates | **P2**            | Simple reusable controls; audit equations and ports before mapping by generic type name.                                                                                                                               |

### 3. Inverters and converter controls

| PowerDynamics model(s)                                                                     | GridDyn status                                                    | Priority  | Why it matters                                                                                                                                                                                                  |
| ------------------------------------------------------------------------------------------ | ----------------------------------------------------------------- | --------- | --------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------- |
| `IdealDroopInverter`                                                                       | No model-specific grid-forming droop inverter                     | **P1**    | RMS-friendly voltage-source inverter with filtered P/Q droop, voltage/frequency setpoints, and angle state. A strong first grid-forming implementation target.                                                  |
| `DroopOuter`, `DroopInverter`                                                              | No equivalent composable grid-forming controller                  | **P1**    | Separates the outer droop law from the electrical plant, which is a useful GridDyn architecture for testing and reuse.                                                                                          |
| `SimpleGFL`, `SimpleGFLDC`                                                                 | No grid-following inverter with PLL/current-control/DC-link model | **P1/P3** | The AC current-control/PLL portion is **P1** converter-interface work; the DC-link capacitor and fast control details in `SimpleGFLDC` are **P3** unless GridDyn explicitly expands beyond phasor RMS dynamics. |
| `SimplePLL`, `PLL_LPF`                                                                     | No reusable PLL component identified                              | **P1**    | Needed by grid-following renewable models and by the OpenIPSL `REGCA1`/`REEC*`/`REPCA1` roadmap.                                                                                                                |
| `LFilter`, `LCFilter`, `LCLFilter`, `VC`, `CC1`, `CC2`, controlled voltage/current sources | No equivalent composable filter/controller library                | **P3**    | These retain converter/filter electromagnetic states in a global dq frame. Use them as a design reference or FMI pilot unless a deliberate EMT-like GridDyn extension is approved.                              |

### 4. Dynamic branches, shunts, and faults

| PowerDynamics model      | GridDyn status                                            | Priority          | Why it matters                                                                                                                                                                                                     |
| ------------------------ | --------------------------------------------------------- | ----------------- | ------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------ |
| `DynamicSeriesRLBranch`  | No AC series-RL branch with current states                | **P1**            | A positive-sequence dq branch with explicit R-L current dynamics and optional transformer ratios. This is distinct from GridDyn's algebraic `AcLine`; GridDyn's dynamic `DcLink` does not cover AC dq line states. |
| `DynamicCShunt`          | No AC capacitor shunt with voltage/current states         | **P2**            | A dynamic capacitor branch; useful for converter/filter and fast-transient studies, but outside ordinary RMS phasor network models.                                                                                |
| `DynamicParallelRCShunt` | No AC parallel RC shunt with states                       | **P2**            | Extends the capacitor case with explicit resistor current. Prioritize only with a clear dynamic-network use case.                                                                                                  |
| `RXGroundFault`          | No separately attachable R-X ground-fault injection model | **P2**            | GridDyn has line-fault and relay mechanisms, but this model is a controllable bus-ground impedance injection with active/reactive fault output.                                                                    |
| `PiLine_fault`           | No exact Pi-line fault component                          | **P2 validation** | GridDyn `AcLine` has fault-location support, so first compare semantics (fault impedance, location, clearing, and topology) rather than adding a duplicate class.                                                  |

## Existing GridDyn overlap — use as validation targets, not new ports

These PowerDynamics models should not be counted as clear GridDyn gaps. Their value is the available Julia/OpenIPSL formulation and tests.

| PowerDynamics model(s)                                                                           | GridDyn candidate                                         | Recommended action                                                                                                 |
| ------------------------------------------------------------------------------------------------ | --------------------------------------------------------- | ------------------------------------------------------------------------------------------------------------------ |
| `PSSE_GENCLS`, `PSSE_GENROU`, `ClassicalMachine`                                                 | `GenModelClassical`, `GenModelGENROU`                     | GENCLS has corrected standard equations, DYR/RAW mapping, initialization, and Jacobian tests; capture PowerDynamics/OpenIPSL trajectories for both machines before further equation changes. |
| `PSSE_HYGOV`                                                                                     | `GovernorHygov`                                           | Native equations and DYR mapping are implemented; capture the PowerDynamics/OpenIPSL trajectory for direct parity. |
| `PSSE_EXST1`, `PSSE_IEEEST`, `PSSE_IEEEG1`                                                       | `ExciterEXST1`, `StabilizerIEEEST`, `GovernorIeeeG1`      | Reuse PowerDynamics' tested Modelica cases to obtain another independent reference path.                           |
| `TGOV1`                                                                                          | `GovernorTgov1`                                           | Validate instead of porting.                                                                                       |
| `PQLoad`, `ZIPLoad`, `VoltageDependentLoad`, `ConstantYLoad`, `ConstantCurrentLoad`, `PSSE_Load` | `ZipLoad`, `ExponentialLoad`, `SourceLoad` are candidates | Audit the voltage/current and low-voltage semantics; GridDyn has related static-load classes.                      |
| `PiLine`, `Breaker`, `StaticShunt`                                                               | `AcLine`, `ZBreaker`, fixed-admittance `ZipLoad`          | Treat as power-flow/event and base-conversion validation cases.                                                    |

## Recommended sequence

1. Use PowerDynamics' OpenIPSL-tested source and test structures to capture external trajectories for the implemented `GENCLS`, `GENSAL`, and `ESST4B`, then create GridDyn P0 references for `GENROE`, `GENSAE`, `SCRX`, and `ESST1A`.
2. Capture the registered PowerDynamics/OpenIPSL `HYGOV` reference for the implemented `GovernorHygov`, and audit the existing GridDyn candidate for `IEEET1`.
3. Decide whether GridDyn will support positive-sequence converter control as a first-class model family. If so, implement the P1 `IdealDroopInverter`/PLL/controller interface and align it with the OpenIPSL `REGCA1`/`REEC*`/`REPCA1` plan.
4. Add `DynamicSeriesRLBranch` only with a clear solver and initialization design for AC current states. Keep dynamic shunts and dq filter models scoped to the same decision.
5. Use PowerDynamics' `GGOV1` only as a secondary comparison for the native implementation until its documented reference issues are independently resolved.

## Regression policy

Keep a minimized source case and captured trajectory in GridDyn. Record the PowerDynamics commit, original OpenIPSL commit, Julia/Modelica tool versions if used, solver settings, event definition, variable names/units, sampling times, and signal tolerances. Compare initialization, no-disturbance equilibrium, trajectories, and event times separately.

PowerDynamics is a useful independent implementation, but GridDyn must not claim compatibility merely because a Julia model or a similar C++ class exists.
