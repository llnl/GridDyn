# Partitioned solver performance benchmark

This note records the current ACTIVSg500 partitioned-solver benchmark and the
next performance investigations.

## Benchmark case

- Model: ACTIVSg500, 500 buses, 597 links, 644 differential states, and 1170 algebraic states.
- Solver: CVODE differential solver paired with KINSOL algebraic solver, sparse KLU linear solves.
- Load event: 5 ms load-step case, `BUS$4::LOAD#0`, at `t = 1.0 s`.
- Test: `ActivsG500Tests.CvodeKinsolLoadStep5msRemainsStable`.
- Platform: Windows, Visual Studio 2022 MSBuild Release/Debug configurations.
- Measurements are single-run wall-clock timings on the current working tree. They are useful for
  tracking changes, but are not a controlled cross-machine performance study.

## Results

| Test | Previous Debug | Current Debug | Release before setup reuse | Release with setup reuse |
|---|---:|---:|---:|---:|
| `CvodeKinsolPartitionedPreservesInitialOperatingPoint` | — | 1.916 s | 0.195 s | — |
| `CvodeKinsolLoadStep5msRemainsStable` | 133.244 s | 76.914 s | 11.232 s | 3.692 s |

The initial partitioned-solver changes reduced the recorded Debug load-step time
by 56.330 s, or approximately 42.3% (1.73x). The solver-level factorization reuse
prototype then reduced the same Release run from 11.232 s to 3.692 s, approximately
3.04x faster. The previous-code Release time has not been measured, so an exact
Release-to-Release comparison is not available.

The previous Debug result was captured before the KINSOL Jacobian-reuse and
partitioned sparse-union changes. Both current Release runs passed.

### OpenMP NVector trial

An isolated Release build was configured with OpenMP enabled for GridDyn and
SUNDIALS, including the SUNDIALS OpenMP NVector module. The focused test was
then run once with the normal serial NVector path and twice with the solver
`omp` flag enabled at the solver level:

| Configuration | Test time |
|---|---:|
| OpenMP-capable build, serial NVector | 3.642 s |
| OpenMP NVector enabled | 6.708 s |
| OpenMP NVector enabled | 6.992 s |

For this 644-differential-state/1170-algebraic-state case, enabling OpenMP
NVector operations made the partitioned solve approximately 1.9x slower. The
OpenMP-capable build by itself did not change performance. This is consistent
with the current workload being dominated by repeated algebraic residual/KINSOL
work, while the vector operations are too small to amortize OpenMP scheduling
overhead.

The initial trial also exposed a CMake naming mismatch: this option was
unprefixed even though GridDyn options use the `GRIDDYN_` prefix. It is now
consistently named `GRIDDYN_ENABLE_OPENMP_SUNDIALS`; CMake reports
`SUNDIALS_ENABLE_OPENMP=ON` and `Added NVECTOR_OPENMP module`. The test input
was restored after the trial.

### GridDyn residual OpenMP trial on ACTIVSg2000

An isolated Release build was configured with GridDyn OpenMP enabled, the
SUNDIALS OpenMP NVector disabled, and the residual parallel mode selected at
initialization. The 2000-bus case was run through `t = 0.01 s` with
`roots_disabled`, because the longer partitioned run is still substantially
slower than the IDA control.

The runtime parameter `residualparallelmode` accepts `off`, `on`, or `auto`.
`auto` enables residual parallelism for 1200 or more buses and selects
`min(omp_get_max_threads(), ceil(total_buses / 300), 8)` threads once during
dynamic initialization. The `threads` flag remains an alias for `on`. Parallel Jacobian
assembly fails during the initial KINSOL sparse setup on this case, and
enabling all list operations fails the same way. Jacobian assembly updates
shared sparse storage; derivative and algebraic update paths also need separate
output partitioning before they can safely be parallelized.

| Configuration | Runs | Wall time |
|---|---:|---:|
| Release, residualparallelmode=off | 3 | 5.187, 5.086, 5.275 s (5.183 s average) |
| Release, residualparallelmode=on, 7 threads | 3 | 2.958, 3.102, 2.993 s (3.018 s average) |
| Release, residualparallelmode=auto, 7 threads | 3 | 3.072, 2.990, 2.956 s (3.006 s average) |

The initialized residual-only path reduced this short run by approximately
42.0% (1.72x). The result is a solver/container change only; no model
callbacks or model terms were added.

The same residual-only path also helps the IDA DAE solve. On the same Release
build, the five-second roots-disabled ACTIVSg2000 run measured:

| Configuration | Runs | Wall time |
|---|---:|---:|
| IDA, residualparallelmode=off | 3 | 1.260, 1.212, 1.248 s (1.240 s average) |
| IDA, residualparallelmode=auto, 7 threads | 3 | 1.187, 1.165, 1.113 s (1.155 s average) |

This is approximately a 6.9% reduction (1.07x). The smaller benefit than the
partitioned solve is expected because IDA performs substantially fewer residual
evaluations and has less repeated algebraic-solve overhead to amortize.

### ARKode trial on ACTIVSg500 and ACTIVSg2000

An additional Release build was configured with ARKode enabled, GridDyn residual
OpenMP enabled, and the SUNDIALS OpenMP NVector disabled. The partitioned runs
used `defdyndiff=arkode`, `dynamicsolvermethod=partitioned`,
`roots_disabled`, `OMP_NUM_THREADS=32`, and `maxiterations=10000`. The latter
setting is important for this workload: the default ARKode limit of 1500
internal steps failed on ACTIVSg500 at `t = 8.5 s` with `ARK_TOO_MUCH_WORK` and
failed on ACTIVSg2000 near `t = 0.002 s` through an unrecoverable RHS/KINSOL
failure. Both cases completed after increasing the solver limit.

The following measurements are five-second simulations, repeated five times
in each mode. Initialization and model loading remain a substantial part of
these short wall-clock runs.

| Case | Residual mode | Runs | Mean wall time | Standard deviation |
|---|---|---:|---:|---:|
| ACTIVSg500 | `off` | 5 | 0.620 s | 0.048 s |
| ACTIVSg500 | `auto` (serial; below 1200 buses) | 5 | 0.608 s | 0.075 s |
| ACTIVSg2000 | `off` | 5 | 0.701 s | 0.070 s |
| ACTIVSg2000 | `auto` (7 residual threads) | 5 | 0.669 s | 0.056 s |

The 500-bus result confirms that `auto` does not enable residual parallelism
below the threshold. The 2000-bus result is only about 4.6% faster in this
measurement and is within the run-to-run variability, so ARKode does not show
the approximately 42% gain observed with the same residual path in the short
CVODE partitioned benchmark. The dominant ARKode-specific finding is that its
default internal-step limit is too small for these partitioned cases; that
limit should be treated separately from the OpenMP performance question.

### Power-flow scaling on larger cases (diagnostic)

Before scoping residual parallelism to dynamic initialization, the Release
executable was run with `--powerflow-only` on the larger cases. With
`OMP_NUM_THREADS=32`, the experimental `auto` rule selected 32 threads for all
three cases. Each run completed successfully, but the results did not justify
parallelizing power flow.

| Case | Serial | Auto, 32 threads | Auto change | OpenMP, 4 threads | OpenMP, 8 threads |
|---|---:|---:|---:|---:|---:|
| ACTIVSg10k | 4.323 s | 4.564 s | +5.6% | 4.407 s | 3.874 s |
| ACTIVSg25k | 11.934 s | 14.189 s | +18.9% | 11.892 s | 11.915 s |
| ACTIVSg70k | 38.029 s | 39.983 s | +5.1% | 37.327 s | 36.059 s |

These are single-run measurements. A 70k-bus sweep also measured 37.594 s at
16 threads, so eight threads was the best tested setting. The dynamic residual
path now caps its automatically selected team at eight threads; using every
logical processor was not consistently best for power flow.

Residual parallelism is now explicitly disabled during `pFlowInitialize` and
configured only during `dynInitialize`, so these power-flow measurements are
diagnostic rather than the default runtime behavior.

## Reproduction

Build the Release test executable:

```powershell
cmake --build build --config Release --parallel 4
```

Run the focused benchmark:

```powershell
& ".\build\bin\Release\DynamicSystemTests.exe" `
  --gtest_filter="ActivsG500Tests.CvodeKinsolLoadStep5msRemainsStable" `
  --gtest_color=no
```

Run the Debug comparison by replacing `Release` with `Debug` in both commands.

## Current optimizations

- Partitioned KINSOL defaults to five nonlinear iterations between Jacobian setups,
  while standalone KINSOL retains exact-Newton behavior. The value can be overridden
  with `maxsetupcalls`.
- Partitioned KINSOL tries the previous linear setup before rebuilding it. The first
  solve after initialization or a structural reset still forces setup, and KINSOL can
  fall back to a fresh setup when the previous factorization is not usable.
- Partitioned sparse Jacobians retain the union of observed nonzero locations and
  only reset KLU symbolic/numeric factors when the union expands.
- Sparse setup detection no longer treats a matrix whose used nonzeros equal its
  allocated capacity as uninitialized.

## Temporary solver-level instrumentation

The existing `partitioned_diagnostics` flag now enables opt-in wall-clock counters in the
CVODE/KINSOL interfaces. The instrumentation does not add model callbacks or model terms.
It reports CVODE RHS/algebraic/derivative time and KINSOL residual/Jacobian time. For KINSOL,
the reported `other_linear_solver_s` value is the remainder of total KINSOL time after the
residual and Jacobian callbacks; it includes KLU setup/factorization and other KINSOL overhead,
so it is not a direct KLU-only timer.

For the Release 5 ms test, the final cumulative counters were:

| Area | Calls | Time |
|---|---:|---:|
| CVODE RHS callbacks | 5,818 | 3.77 s |
| CVODE algebraic solves | 5,916 | 3.50 s |
| CVODE differential derivative evaluation | 5,818 | 0.30 s |
| KINSOL algebraic solves | 6,182 | 3.13 s |
| KINSOL residual callbacks | 17,614 | 2.84 s |
| KINSOL Jacobian callbacks | 2 | 0.003 s |
| Model Jacobian assembly | — | 0.001 s |
| KINSOL remainder (`other_linear_solver_s`) | — | 0.29 s |

This confirms that the dominant cost is repeated algebraic work, not the differential model
evaluation. Reusing the previous KINSOL factorization removed nearly all repeated Jacobian
setups in this case while preserving the test result. A direct KLU timer would still be useful
if the split between numeric factorization and ordering is needed; the current remainder combines
those costs.

## Next optimization work

The recommended order is:

1. Validate setup reuse across additional event-heavy and larger cases. If the exact KLU split
   is needed, add temporary timers inside the KLU linear-solver
   implementation for symbolic ordering versus numeric factorization. The current application-
   level remainder is sufficient to prioritize work without changing model code.
2. Supply CVODE with a solver-generated reduced Jacobian of the partitioned differential
   system. Models should not gain new derivative terms or a new callback contract. If
   `F_a(x_d, x_a) = 0` is the algebraic residual and `G(x_d, x_a)` is the differential
   right-hand side, the required Jacobian is `G_d - G_a F_a^-1 F_d`; the solver can
   approximate this through existing residual/RHS callbacks. A practical first prototype
   would use solver-side directional finite differences, with sparse coloring to avoid
   one algebraic solve per differential state. The solver must restore the base algebraic
   state after each rejected or perturbed trial. A later implementation could reuse the
   KINSOL `F_a` factorization internally, still without requiring model changes.
3. Make KINSOL Jacobian reuse adaptive. Start with the current value of five, then
   force an earlier setup after poor residual reduction, a line-search failure, or a
   detected active-pattern expansion.
4. Add a rollback-safe algebraic predictor for CVODE trial states. KINSOL already
   retains its last algebraic state; extrapolating from the previous accepted trial
   can reduce residual iterations, but the predictor must not leak rejected CVODE
   trial states into the accepted history.
5. Investigate consistency-preserving algebraic-solve caching. Repeated identical
   trial states can be served from a cache, but state/time/tolerance keys must prevent
   returning an algebraic state that is stale relative to CVODE's requested accuracy.
6. Reduce Jacobian assembly overhead after the sparse union stabilizes. A topology-derived
   structural pattern or a lower-cost pattern probe could avoid rebuilding and sorting a
   temporary sparse matrix on every partitioned Jacobian callback.
7. Parallelize component residual and Jacobian assembly only after verifying that model
   components do not share mutable scratch state and that thread overhead is lower than
   the serial assembly cost.

IDA remains faster in some workloads because it solves the coupled differential-algebraic
system in one nonlinear iteration. The partitioned path should therefore be judged against
both total wall time and the number/cost of algebraic solves required per accepted CVODE step.
