# IDA initial-condition diagnostics

This note records the workflow for diagnosing a GridDyn DAE initialization failure. The
immediate reference case is ACTIVSg2000, but the checks are intended to become a reusable
diagnostic capability for future dynamic models.

## Reproduction

Use the PSS/E RAW and DYR files together. The DYR records attach dynamic models to the
generators loaded from the RAW case; they are not PowerWorld-specific files.

```powershell
$case = 'C:\Users\phlpt\Documents\test_cases\ACTIVSg2000'
& .\build\bin\Debug\gridDynMain.exe `
  "--input=$case\ACTIVSg2000.RAW" `
  "--import=$case\ACTIVSg2000_dynamics.dyr" `
  '--param=dynamicsolvermethod=dae' '--param=timestop=5' --verbose
```

Use a short stop time while narrowing the first failure. A nonzero return code is required
for a failed run; a zero-time run can still execute initialization work without exercising
the same integration path.

## Diagnostic sequence

1. Confirm the input pair and the actual case path. Record the file formats and the model
   reader used.
2. Run the power flow by itself. This distinguishes a network/RAW problem from a dynamic
   initialization problem.
3. Run dynamic initialization with a very short stop time and normal warning output. Record
   every automatic limit adjustment, unsupported model, and initialization warning.
4. Repeat with `strict_exciter_limits` and `strict_governor_limits`. These are data-quality
   checks, not solver checks; use them to distinguish permitted compatibility adjustments from
   invalid lower-limit or parameter conditions.
5. Keep IDA on the sparse/KLU path for large systems. A dense Jacobian scales quadratically
   with the state count and is not an appropriate first diagnostic for a 2000-bus case.
6. Identify the exact IDA phase and return code. `IDA_LSETUP_FAIL` means the linear solver
   setup failed; `IDA_LINESEARCH_FAIL` means the nonlinear IC correction could not find a
   sufficiently reducing step. Both occur inside `IDACalcIC` and are distinct from a failure
   after the first successful time step.
7. On an IC failure, capture a compact state diagnostic: state size, IC mode, IDA return code,
   residual norms split into algebraic and differential entries, non-finite values, the largest
   residual entries with their GridDyn state names, and the largest differential derivatives.
   DAE consistency alone is insufficient for a no-disturbance stability run: a substantial
   solved derivative means the initialized state is not an equilibrium.
8. Check the Jacobian supplied at the failed IC point. At minimum report its evaluated nonzero
   count, non-finite entries, empty rows/columns, and zero or non-finite diagonal entries. A
   full finite-difference Jacobian comparison is useful on small cases but is too expensive as
   the default for ACTIVSg2000.
9. If the residual/Jacobian checks are internally consistent, isolate the model family or
   generator group by staged loading or a reduced case. If they are inconsistent, fix the
   model equation, reader mapping, or state-offset construction before changing IDA settings.
10. After a fix, rerun model residual checks, Jacobian checks on a representative small case,
    the full ACTIVSg2000 initialization, and a short dynamic integration.
11. For a no-disturbance run, first require the *raw* initialized algebraic residual to be
    small. Then distinguish a consistent DAE trajectory from a stationary operating point by
    inspecting the largest differential derivatives. A nonzero derivative in an inactive
    limiter may be physically permissible, but it must not alter the selected governor or
    exciter output.
12. Increase the stop time gradually (for example 0.1, 1, then 2 seconds). When a failure
    occurs after a root return, record the root name and direction, whether any roots remain
    zero immediately after the restart, the per-call IDA nonlinear-convergence statistics, and
    the bounded integration-failure residual report. Repeated returns for a root that remains
    zero identify event chatter; a negative IDA return with a large residual identifies a
    failed post-event DAE correction.
13. Compare one normal tolerance with one materially looser tolerance. This is a diagnostic,
    not a fix: fewer residual evaluations with the same root sequence implicate numerical
    accuracy, whereas the same root sequence or a minimum-step failure implicates model/event
    behavior.
14. Repeat the short no-disturbance run with limiter roots enabled and disabled. A stationary
    case should have the same state drift in both runs; a large difference identifies root
    handling rather than the continuous DAE as the next diagnostic target. Record the number of
    IDA intervals, residual evaluations, root returns, nonlinear-convergence failures, and the
    maximum state change from the initialized point.

## ACTIVSg2000 investigation log

### Current evidence

#### Current no-disturbance result

- The corrected ACTIVSg2000 case now completes a five-second IDA DAE run with roots enabled,
  sparse/KLU, and configured tolerance `1e-4`. The run takes about 1.04 s, uses one successful
  `IDASolve` interval, and has maximum state drift `2.68e-9`; no limiter root is returned.
- The same five-second run with `roots_disabled` also completes in about 1.01 s, with 11 IDA
  steps, 13 residual evaluations, no nonlinear-convergence failures, and maximum state drift
  `2.86e-9`. This is consistent with a stationary operating point, not a physical instability
  or a tolerance-driven slowdown.
- A first-pass fixed-masked IC probe can still report `IDA_LSETUP_FAIL` with a maximum residual
  of only `6.63e-12`. The bounded Jacobian report is finite and structurally complete; the normal
  recovery ladder succeeds. This is a recoverable IC linear-solver trial, not the current run
  failure.
- The 500-bus regression suite passes all nine stability and operating-point tests. Its prior
  repeated `CVodeSetMinStep hmin < 0` messages came from forwarding the solver's unset `-1`
  sentinel during runtime step updates; CVODE and ARKode now guard optional max/min/init-step
  updates before calling SUNDIALS, and the focused partitioned tests run without those warnings.
- The initial ACTIVSg2000 CVODE/KINSOL partitioned failure was not a bad paired-state offset or
  model derivative. The first algebraic callback and the first two differential RHS evaluations
  succeed with valid dimensions; the native access violation occurred later in SuiteSparse KLU
  numeric factorization. The assembled algebraic sparse pattern is valid and stable at that
  point. KLU is now configured to stop on a detected singular factorization, allowing that
  condition to return to KINSOL instead of continuing with a partial pivot permutation.
- The sparse adapter also previously used `SUNMatZero` when refreshing a Jacobian. That routine
  erases compressed row/column indices as well as values. The adapter now clears values only,
  and GridDyn resets KLU's symbolic factorization whenever a rebuilt sparse Jacobian has a new
  pattern. This avoids reuse of a symbolic factorization with incompatible indices.
- CVODE's original partitioned start step was the requested maximum step (`0.05 s`), so even a
  `0.001 s` probe evaluated a `0.05 s` trial state. CVODE now begins from the existing
  `probeStepTime` (`0.001 s`) and may grow to the requested maximum. The 2000-bus
  roots-disabled partitioned run now completes through `0.01 s` with the ordinary `0.05 s`
  maximum and no access violation. A `0.05 s` run remains much slower than the DAE control and
  needs further convergence/performance work before it is a practical five-second path.

#### Partitioned CVODE diagnostic procedure

- Add `partitioned_diagnostics` (or `partitioned_trace`) to retain a bounded callback trace in
  `partitioned-diagnostics.log`, including differential/algebraic dimensions, KINSOL callbacks,
  and sparse-pattern checks. It works even if console logging is disabled.
- Start with a roots-disabled short run, then increase stop time gradually. A normal exit through
  `0.01 s` is a useful startup check; inspect each KLU symbolic reset and any KINSOL iteration
  failure before attempting a multi-second run.
- Keep the DAE control run alongside every partitioned change. For this case, sparse IDA DAE at
  tolerance `1e-4` completes the five-second no-disturbance run cleanly, so a slow or failed
  partitioned run should be diagnosed in the partitioned algebraic/integrator path rather than
  treated as evidence of physical instability.

#### GGOV1 inactive-temperature branch

- ACTIVSg2000 contains 288 in-service GGOV1 records with an active load limiter. The previous
  implementation integrated the load-limiter state even when the normal or acceleration request
  was selected. At Dallas 5042/1 this hidden derivative was about `0.640506 pu/s`, so a
  no-disturbance run could drift even though the selected governor output was initially correct.
- GGOV1 now initializes the inactive load-integral state to track fuel, freezes that state unless
  the temperature request is the selected minimum, and uses `PMAX` consistently in the request
  equation. This follows the selector/limiter behavior described in the
  [NERC turbine governor guide](https://www.nerc.com/globalassets/who-we-are/standing-committees/rstc/ppmvtf/reliability_guideline-application_guide_for_turbine-governor_modeling.pdf)
  and [PowerWorld GGOV1 documentation](https://www.powerworld.com/WebHelp/Content/TransientModels_HTML/Governor%20GGOV1%20and%20GGOV1D.htm).

#### Governor root-equilibrium handling

- The permissive governor-limit policy can set `GMAX` or `PMAX` exactly at the initial dispatch.
  Treating a zero-rate state at that artificial upper boundary as an entering event caused root
  chatter around `0.4096` s and `0.413` s, followed by a Windows access violation during restart.
- HYGOV and IEEEG1 now treat a position exactly at a limit with zero limited rate as an
  equilibrium branch. Definite outward rates still enter the limiting branch, and definite inward
  rates still release it. The focused governor root tests pass, and the ACTIVSg2000 root-enabled
  five-second run no longer returns these artificial roots.
- A future built-in root diagnostic should record the owner, local root index, direction, state
  value, limit, limited rate, and pre/post branch flags for every return. That would make event
  chatter distinguishable from a genuine model transition without reconstructing the trace by hand.

#### Superseded no-disturbance observations

- The original `0.23536` per-unit algebraic residual at `Coast::FREEPORT 2 0:voltage` was
  not an exciter error. The bus dynamic initializer included the `23.536 Mvar` output of an
  out-of-service generator when allocating reactive-power adjustments. It now consistently
  requires both `isConnected()` and `isEnabled()` for links, loads, and generators.
- With that correction, the raw initial algebraic residual is `1.77e-12`; the successful
  fixed-differential IC correction has an algebraic residual below `5.7e-13`. The prior large
  EXAC2 amplifier derivatives disappear, confirming that they were a response to the bus
  mismatch rather than an EXAC2 equation error.
The entries below describe intermediate states of the investigation. They are retained because
they show how the failure was narrowed, but the current result is recorded above. In particular,
the old one-second timing, post-root failure, and repeated-root observations predate the GGOV1
inactive-branch and zero-rate root-equilibrium fixes.

#### Earlier investigation record

- Default exciter-limit policy now permits upper-limit adjustments with warnings. Strict mode
  stops at the first SCRX upper-limit violation, as intended.
- With the default policy, ACTIVSg2000 reaches IDA initial-condition correction. The next
  blocker is SUNDIALS `IDACalcIC`: `IDA_LSETUP_FAIL` followed by `IDA_LINESEARCH_FAIL`.
- The failure is reported from `ThirdParty/sundials/src/ida/ida_ic.c`, first at the linear
  solver setup diagnostic and then at the IC line-search diagnostic. No model-specific
  parameter exception is reported at this stage.
- A dense comparison on this case produced a Windows access-violation dialog. Until dense
  allocation and failure handling are guarded explicitly, dense IDA should not be used as the
  large-case diagnostic path.
- The bounded pre-`IDACalcIC` snapshot shows that the large DC2A `vr` value was already in
  the initializer state (`17.51671405379377`), not created by an IDA trial step. The DC2A
  initializer now applies the same permissive/strict upper-limit policy as the other
  exciters, with the limit normalized by terminal voltage because DC2A uses `VRMAX * Vt`.
- After that policy is applied, the zero-stop-time ACTIVSg2000 run still reports the
  expected fixed-masked GGOV1 `load_int` residuals, but the fixed-differential IC correction
  completes without an IDA failure. This confirms that DC2A was a real initialization-data
  blocker rather than a pure Jacobian sparsity failure.
- At that intermediate stage a short positive-time run remained computationally expensive after
  IC succeeded, and the first `roots_disabled` experiment exposed a root-count assertion. The
  root-disabled path has since been repaired and is now a valid A/B diagnostic; the current
  five-second comparison is recorded above.
- The command-line runner accepts repeated `--param` options, so tolerance and stop time can
  be set in the same run. This is required for a valid tolerance comparison; a duplicate
  `--param` was previously rejected during parsing and could look like a fast simulation.
- The successful fixed-differential IC snapshot has `max_residual=1.5042e-6`,
  `max_algebraic_residual=5.2695e-7`, and `max_differential_residual=1.5042e-6`, with finite
  Jacobian entries and no empty rows, empty columns, or missing diagonals. The largest
  remaining entries are EXAC2 `va` and ESAC6A `ve`; their nonzero `yp` values are consistent
  with IDA solving for differential derivatives. This establishes a consistent DAE point but
  does not establish an equilibrium; the derivative probe must be used before concluding that
  a no-disturbance run is stable.
- The correct command-line pairing is `--input` for RAW and `--import` for DYR. The previously
  documented second `--input` could suppress normal runner diagnostics and was not an adequate
  reproduction command. Use `--verbose`, not `--verbose=1`, when the summary-level IDA trace is
  needed.
- The DC2A warning at bus 5298 is data-driven. The reader maps the PSS/E ESDC2A fields in
  the documented order `E1, SE1, E2, SE2`, but this record contains `2.36, 2.36, 0.2246,
  3.1467`, unlike the canonical two-point saturation pattern in comparable cases. An
  isolated override to `2.36, 0.2268, 3.1467, 0.9072` removes the extreme initial warning;
  this is consistent with the ESDC2A field semantics described in the
  [PowerWorld ESDC2A model documentation](https://www.powerworld.com/WebHelp/Content/TransientModels_HTML/Exciter%20ESDC2A.htm).
- After the saturation data is corrected, the next failure is the DC2A limiter transition.
  Its active residual is `-dot(VR)`, but the supplied limiter Jacobian had the opposite `cj`
  sign and an invalid terminal-voltage term. The limiter Jacobian now matches the residual,
  and a focused DAE Jacobian regression test passes.
- The first positive-time slowdown was root handling, not tolerance. HYGOV and IEEEG1
  position-limit roots were exactly zero at accepted initial upper-limit states. Their root
  handling now marks the initial limiter branch, offsets non-eventful inward-bound roots,
  and refreshes `yp` after every limiter branch transition. The trace changed from repeated
  microsecond root returns to ordinary limiter events and advanced past the DC2A event.
- With the default configured tolerance of `1e-6` (IDA relative tolerance `1e-8`, absolute
  tolerance `1e-6`), the corrected case advances through roughly `0.67 s` before an IDA
  minimum-step corrector failure. The failure snapshot has a maximum residual of about
  `3.5e-9`, concentrated in algebraic bus-angle rows with large `yp`, rather than in a
  DC2A or governor residual. A relaxed configured tolerance of `1e-4` did not complete even
  a bounded `0.1 s` run within the same wall-time budget, so relaxing tolerance is not yet
  a useful workaround.

### Planned reusable capability

The solver should provide an opt-in IC diagnostic mode that automatically performs steps 6--8
above when `IDACalcIC` fails. The normal path should remain compact; the opt-in report should
be bounded, state-name aware, and available for both dense and sparse linear-solver paths.
The current integration trace also maps roots before IC and after each root transition, which
is useful for distinguishing event chatter from an equation or linear-solver failure.

The initial implementation is enabled with the `ida_initial_condition_diagnostics` flag (or
the shorter `ida_ic_diagnostics` alias):

```powershell
& .\build\bin\Debug\gridDynMain.exe `
  "--input=$case\ACTIVSg2000.RAW" `
  "--import=$case\ACTIVSg2000_dynamics.dyr" `
  '--param=dynamicsolvermethod=dae' '--param=timestop=0.001' --verbose `
  '--flags=sparse,ida_ic_diagnostics'
```

The report is intentionally bounded to the eight largest residual entries, the eight largest
differential derivatives, and a compact Jacobian-structure summary. It is an investigation aid,
not a replacement for a full finite-difference Jacobian check.

For positive-time performance, `ida_integration_diagnostics` (alias `ida_integration_trace`)
reports the tolerance passed to IDA, the effective relative tolerance used by GridDyn's IDA
adapter (`tolerance/100`), the absolute tolerance, each `IDASolve` interval, solver statistics
when an interval returns, and every 1000th residual evaluation. This distinguishes a slow
first nonlinear solve from repeated short calls caused by roots or event handling:

```powershell
& .\build\bin\Debug\gridDynMain.exe `
  "--input=$case\ACTIVSg2000.RAW" `
  "--import=$case\ACTIVSg2000_dynamics.dyr" `
  '--param=dynamicsolvermethod=dae' '--param=tolerance=1e-4' '--param=timestop=5' --verbose `
  '--flags=sparse,ida_integration_diagnostics'
```

Each reported residual also includes the pre-`IDACalcIC` state and derivative when available.
This distinction is important: a large value in the failed nonlinear-correction snapshot can
be an IDA trial point rather than a value produced by the model initializer.

For a large case, add `ida_ic_stop_on_failure` to prevent the normal recovery ladder from
running after the first failed fixed-differential IC correction:

```powershell
& .\build\bin\Debug\gridDynMain.exe `
  "--input=$case\ACTIVSg2000.RAW" `
  "--import=$case\ACTIVSg2000_dynamics.dyr" `
  '--param=dynamicsolvermethod=dae' '--param=timestop=0.001' --verbose `
  '--flags=sparse,ida_ic_diagnostics,ida_ic_stop_on_failure'
```

This switch is diagnostic-only and is not enabled by the normal compatibility path. It keeps
the report focused on the first reproducible IC failure instead of mixing it with later
voltage-reset and convergence-recovery attempts.

### Exciter initial-limit policy audit

The permissive default for an initialized value above a control-output upper limit is now
used by the clear cases in AC7B, AC8B, ESST1A, ESST2A, and ESST3A. These models retain hard
failures for lower limits, nonfinite values, rectifier inconsistencies, and other physical
constraints. IEEEX1 now also applies the common policy before selecting its lead/lag or
non-lead/lag initialization path, so `strict_exciter_limits` has consistent behavior in both
branches.

The remaining exciter limit checks need model-specific review before being relaxed:

* EXAC4 and EXST1 use field-current-offset regulator bounds.
* EXPIC1 combines field-voltage limits with the `VR1`/`VR2` selector range.
* IEEET3 uses a terminal-voltage-dependent regulator bound.
* AC7B and AC8B still retain separate exciter/field-feedback bounds after their control
  amplifier limits are handled by the common policy.

Those cases should not simply raise every upper limit. The implementation must identify which
bound is a control-element initialization limit, update the underlying model parameter used by
the dynamic equations, and leave equipment or physical field limits strict. Each future change
should add an interior, upper-limit, lower-limit, and strict-policy regression case.
