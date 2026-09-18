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

## ACTIVSg2000 investigation log

### Current evidence

#### Latest no-disturbance re-run

- The original `0.23536` per-unit algebraic residual at `Coast::FREEPORT 2 0:voltage` was
  not an exciter error. The bus dynamic initializer included the `23.536 Mvar` output of an
  out-of-service generator when allocating reactive-power adjustments. It now consistently
  requires both `isConnected()` and `isEnabled()` for links, loads, and generators.
- With that correction, the raw initial algebraic residual is `1.77e-12`; the successful
  fixed-differential IC correction has an algebraic residual below `5.7e-13`. The prior large
  EXAC2 amplifier derivatives disappear, confirming that they were a response to the bus
  mismatch rather than an EXAC2 equation error.
- The case reaches 0.1 s and 1.0 s cleanly with IDA/KLU. At the default configured tolerance
  (`1e-6` absolute, `1e-8` relative), a one-second run took about 13 s. At `1e-4` configured
  tolerance (`1e-4` absolute, `1e-6` relative), IDA used 120 rather than 199 residual
  evaluations through one second but encountered the same limiter-event class; tolerance is
  therefore not the primary cause of the slow five-second run.
- The first reproducible long-run failure is a post-root IDA corrector failure near 1.47 s,
  not an IC failure. The integration trace records numerous HYGOV, IEEEG1, IEEE Type 1, and
  DC2A limiter events. Several HYGOV position roots remain exactly zero after a restart and
  re-fire, which is event chatter.
- Immediately after the `FALCON HEI~2` HYGOV position-root transition at about 1.474 s, IDA
  reaches its minimum step with repeated nonlinear-corrector failures. The largest residuals
  are several EXAC2/EXAC1 `va` rows, headed by `South::PEARSALL 4` EXAC2. This is a
  system-level consequence of the failed post-event correction, not evidence that that
  particular exciter initiated the failure.
- The integration trace now includes root direction (`+1` release or `-1` entry according to
  the component root convention), in addition to the mapped owner. This is needed to validate
  limiter transitions rather than only count them.

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
- A short positive-time run remains computationally expensive after IC succeeds. The
  diagnostic flag now also reports successful IC corrections, so the next pass can separate
  IC cost from time-integration cost. The `roots_disabled` experiment currently exposes an
  existing `IdaInterface::solve` root-count assertion and is not a valid large-case path.
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
