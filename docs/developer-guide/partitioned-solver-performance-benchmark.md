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

| Test                                                   | Previous Debug | Current Debug | Release before setup reuse | Release with setup reuse |
| ------------------------------------------------------ | -------------: | ------------: | -------------------------: | -----------------------: |
| `CvodeKinsolPartitionedPreservesInitialOperatingPoint` |              — |       1.916 s |                    0.195 s |                        — |
| `CvodeKinsolLoadStep5msRemainsStable`                  |      133.244 s |      76.914 s |                   11.232 s |                  3.692 s |

The initial partitioned-solver changes reduced the recorded Debug load-step time
by 56.330 s, or approximately 42.3% (1.73x). The solver-level factorization reuse
prototype then reduced the same Release run from 11.232 s to 3.692 s, approximately
3.04x faster. The previous-code Release time has not been measured, so an exact
Release-to-Release comparison is not available.

When `partitioned_diagnostics` is enabled, the partitioned driver also prints
opt-in solver counters for total solve time, residual/RHS/derivative calls,
Jacobian time, and model-Jacobian time. These counters are intended for
profiling and are reset when the CVODE or KINSOL solver is initialized; normal
runs do not take the timing calls.

The previous Debug result was captured before the KINSOL Jacobian-reuse and
partitioned sparse-union changes. Both current Release runs passed.

### OpenMP NVector trial

An isolated Release build was configured with OpenMP enabled for GridDyn and
SUNDIALS, including the SUNDIALS OpenMP NVector module. The focused test was
then run once with the normal serial NVector path and twice with the solver
`omp` flag enabled at the solver level:

| Configuration                        | Test time |
| ------------------------------------ | --------: |
| OpenMP-capable build, serial NVector |   3.642 s |
| OpenMP NVector enabled               |   6.708 s |
| OpenMP NVector enabled               |   6.992 s |

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

| Configuration                                 | Runs |                               Wall time |
| --------------------------------------------- | ---: | --------------------------------------: |
| Release, residualparallelmode=off             |    3 | 5.187, 5.086, 5.275 s (5.183 s average) |
| Release, residualparallelmode=on, 7 threads   |    3 | 2.958, 3.102, 2.993 s (3.018 s average) |
| Release, residualparallelmode=auto, 7 threads |    3 | 3.072, 2.990, 2.956 s (3.006 s average) |

The initialized residual-only path reduced this short run by approximately
42.0% (1.72x). The result is a solver/container change only; no model
callbacks or model terms were added.

The same residual-only path also helps the IDA DAE solve. On the same Release
build, the five-second roots-disabled ACTIVSg2000 run measured:

| Configuration                             | Runs |                               Wall time |
| ----------------------------------------- | ---: | --------------------------------------: |
| IDA, residualparallelmode=off             |    3 | 1.260, 1.212, 1.248 s (1.240 s average) |
| IDA, residualparallelmode=auto, 7 threads |    3 | 1.187, 1.165, 1.113 s (1.155 s average) |

This is approximately a 6.9% reduction (1.07x). The smaller benefit than the
partitioned solve is expected because IDA performs substantially fewer residual
evaluations and has less repeated algebraic-solve overhead to amortize.

### ARKode trial on ACTIVSg500 and ACTIVSg2000

An additional Release build was configured with ARKode enabled, GridDyn residual
OpenMP enabled, and the SUNDIALS OpenMP NVector disabled. An earlier command-line
sweep reported sub-second ACTIVSg2000 “passes,” but those runs were not valid:
the reconstructed command used a dynamics-file path outside the
`ACTIVSg2000` directory, and the generic root-level
`--param=maxiterations=...` option is not a valid solver-scoped setting. The
runner stops processing that parameter callback and exits before running the
simulation, leaving only the version line in the output.

With the actual files `ACTIVSg2000/ACTIVSg2000.RAW` and
`ACTIVSg2000/ACTIVSg2000_dynamics.dyr`, earlier Release ARKode runs failed
near the start of the simulation with either a KINSOL scaled-step error or an
unrecoverable KLU setup failure. Raising the ARKode constructor default from
1500 to 10000 did not resolve those failures. The later endpoint-derivative
fix and the conservative-step experiments below allow the no-root diagnostic
run to complete, but the default 50 ms explicit step still produces a large
nonphysical drift.

### ARKode equilibrium trace

A solver-level trace of the stable ACTIVSg2000 case separated the ARKode fault
from the algebraic solve. After the DAE-consistent initial state was copied into
the partitioned vectors, the initial largest differential RHS was `6.6e-12`.
CVODE remained at equilibrium through `t = 3.2 s`, with an accepted endpoint
derivative of `4.8e-12` and algebraic residuals of about `1.8e-12`.

The trace also found a wrapper consistency bug: ARKode writes each RHS into an
internal SUNDIALS vector, but `ArkodeInterface::solve` had never copied the
accepted derivative into GridDyn's public `derivData()` vector. The partitioned
driver subsequently exposed a zero/stale derivative to the algebraic solve and
model caches. `ArkodeInterface::solve` now refreshes that vector with
`ARKodeGetDky(tret, 1, ...)`, matching the existing CVODE behavior. This is a
required correctness fix, although it is not the main source of the early
drift: the ARKode RHS callback itself begins to grow before the endpoint vector
is consumed.

With roots disabled, the default 50 ms ARKode run grew from the initial
`6.6e-12` RHS to about `8.0e-8` by `t = 0.1 s`, with the largest component in
`Far West::ODESSA 1 8::ODESSA 1 8_Gen_1::exciterEXAC2_7474:va`; by `t = 4 s`
the endpoint derivative reached about `2.0`. KINSOL continued to return
successful algebraic solves, so this is not an algebraic Newton failure.

Reducing the explicit ARKode maximum step keeps the same equilibrium stationary:

| Maximum step | Endpoint at `t = 0.1 s` | Endpoint at `t = 4 s` |
| -----------: | ----------------------: | --------------------: |
|    `0.050 s` |                `5.8e-7` |                 `2.0` |
|    `0.020 s` |                `5.2e-8` |                     — |
|    `0.015 s` |               `2.3e-11` |                     — |
|    `0.010 s` |               `7.7e-12` |             `6.0e-12` |

This is the signature of an explicit-method stability boundary in the stiff
reduced ODE, not a model event or an algebraic residual that is slowly drifting.
The EXAC2 `va` state is the first visible unstable component; its high-gain
regulator dynamics are coupled to the network algebraic solve. IDA and CVODE
remain stable because their implicit treatment handles that stiff mode. Future
ARKode work should either select an implicit/IMEX ARK configuration or derive a
solver-side maximum-step policy from the reduced-system stiffness. No new model
terms are indicated by this trace.

### ARKode explicit-table and summation experiment

The ARKode wrapper now enables compensated summation by default and accepts the
solver-level parameters `arkodecompensatedsums=on|off` and
`arkodetable=<name>`. The supported table names for this experiment are
`sofroniou`, `ark324l2sa`, `ssp4`, `ark436l2sa`, `ark437l2sa`, `ssp10`,
`ark548l2sa`, `ark548l2sab`, `cashkarp`, `dormandprince`, and `fehlberg`.
The `step` and `maxstep` parameters now set the initial ARKode step to half the
maximum step unless `initialstep` (or `initstep`) is supplied explicitly. This
gives the explicit solver a short settling period without changing the maximum
allowed internal step.

Release runs on ACTIVSg2000, with roots disabled, `maxstep = 20 ms`,
`initialstep = 10 ms`, and compensated summation enabled produced these single
run timings:

| Explicit table  | Time to `t = 4 s` | Result    |
| --------------- | ----------------: | --------- |
| `sofroniou`     |           16.45 s | completed |
| `ark437l2sa`    |            6.87 s | completed |
| `cashkarp`      |           25.18 s | completed |
| `dormandprince` |           26.27 s | completed |
| `fehlberg`      |           21.33 s | completed |

The ARK437L2SA table was substantially faster in this run. With root finding
enabled, ARK437L2SA also completed the same 4-second run in 7.20 s, while the
default Sofroniou–Spaletta run did not complete cleanly. This makes ARK437L2SA
the most promising explicit table for follow-up, although the root-path failure
still needs separate diagnosis before changing the default table.

Compensated summation did not show a clear performance or stability benefit in
the default-table comparison: the measured times were 17.07 s with it enabled
and 17.97 s with it disabled. That difference is small enough to be run-to-run
variation, so it is retained as a low-risk numerical safeguard rather than a
primary fix for the stiff-mode instability.

### ARKode transient table sweep on ACTIVSg500

The candidate tables were then run through the 500-bus 5 ms load-step test,
which applies a 10 MW step to `BUS$4::LOAD#0` at `t = 1.0 s` and advances to
`t = 30 s`. These are Release runs using the partitioned ARKode/KINSOL path;
the 500-bus automatic residual-parallel threshold leaves residual OpenMP off.
Each run also passed the GoogleTest trajectory checks, including finite states,
bounded voltage/frequency response, and a decaying frequency envelope.

| Explicit table | Stages |     Time | Result |
| -------------- | -----: | -------: | ------ |
| `sofroniou`    |      5 | 12.396 s | passed |
| `ssp4`         |      4 | 13.163 s | passed |
| `ark436l2sa`   |      6 | 18.174 s | passed |
| `ark437l2sa`   |      7 | 21.664 s | passed |
| `ssp10`        |     10 | 30.514 s | passed |
| `ark548l2sa`   |      8 | 24.769 s | passed |
| `ark548l2sab`  |      8 | 24.737 s | passed |

This transient ranking is the opposite of the equilibrium ranking: the tables
with the larger negative-real stability intervals were slower after the load
step. The likely explanation is that the transient is controlled more by
complex oscillatory modes and embedded-error adaptation than by a single
negative-real stiff mode. The wider real-axis stability region still improves
margin, but it does not guarantee fewer stages, fewer rejected steps, or better
overall runtime for a nonlinear transient.

The four-stage `ssp4` table also passed the trajectory checks, but was about
6.2% slower than the default at 5 ms. Its advantage on the 2000-bus case is
therefore workload- and scale-dependent rather than a general improvement for
small transient cases.

The real-axis stability estimates remain useful diagnostically: approximately
2.79 for Sofroniou–Spaletta, 4.23 for ARK436L2SA, 6.76 for ARK437L2SA, and
13.9 for SSP10. For this load-step case, however, the default table is the
fastest tested option while all candidates are stable at the 5 ms outer step.

### ARKode step-size crossover for the ACTIVSg500 load step

The default Sofroniou–Spaletta and ARK437L2SA tables were also compared at
larger requested timesteps. These are Release GoogleTest runs of the same
30-second trajectory; every run passed the transient checks.

| Requested timestep | Sofroniou–Spaletta | ARK437L2SA | Faster table       |
| -----------------: | -----------------: | ---------: | ------------------ |
|              10 ms |            7.373 s |   11.507 s | Sofroniou–Spaletta |
|              20 ms |            3.845 s |    6.262 s | Sofroniou–Spaletta |
|              50 ms |            3.109 s |    2.914 s | ARK437L2SA         |

This places the performance crossover between 20 ms and 50 ms for this case.
At 5–20 ms, ARK437's extra stages cost more than its stability margin saves.
At 50 ms, the wider stability region lets ARK437 avoid enough extra adaptation
work to become slightly faster. The result supports keeping the default table
for normal power-system timesteps while retaining ARK437 as a useful option
when larger explicit steps are intentionally allowed.

Two lower-order, four-stage tables are plausible additional candidates for
larger requested steps: `ark324l2sa` (ARK 3(2)) and `ssp4` (SSP ERK 3(2)).
They have one fewer stage than the default table and estimated negative-real
stability intervals of approximately 3.66 and 5.15, respectively. The lower
formal order can increase adaptive work at small steps, so these should be
treated as benchmark options rather than default changes. The useful test is
whether the saved stage evaluations outweigh any additional step rejection at
20–50 ms.

### ACTIVSg2000 load-step benchmark

The repository does not include the large external ACTIVSg2000 RAW/DYR files.
The benchmark therefore keeps the case data outside the repository and applies
the same 10 MW step used by the 500-bus test to `BUS$4::LOAD#0` at `t = 1.0 s`.
The checked-in runner is
[`scripts/benchmark_activsg2000_arkode_load_step.ps1`](../../scripts/benchmark_activsg2000_arkode_load_step.ps1).
It runs the Release executable, uses the partitioned ARKode/KINSOL path, and
defaults residual parallelism to `auto`.

Example from the standard local case-data location:

```powershell
pwsh -File .\scripts\benchmark_activsg2000_arkode_load_step.ps1 `
  -Table sofroniou -Timestep 0.005 -StopTime 30
```

The table can be changed without editing the case, for example:

```powershell
pwsh -File .\scripts\benchmark_activsg2000_arkode_load_step.ps1 `
  -Table ark437l2sa -Timestep 0.005 -StopTime 30
```

The event target was verified against the external case. Release runs at a 5 ms
requested step produced the following results:

| Explicit table | Stop time |     Time | Result    |
| -------------- | --------: | -------: | --------- |
| `sofroniou`    |       5 s |  6.062 s | completed |
| `ark324l2sa`   |       5 s |  6.748 s | completed |
| `ssp4`         |       5 s |  5.974 s | completed |
| `ark437l2sa`   |       5 s | 10.521 s | completed |
| `sofroniou`    |      30 s | 35.769 s | completed |
| `ssp4`         |      30 s | 34.261 s | completed |
| `ark437l2sa`   |      30 s | 58.755 s | completed |

For this 2000-bus load step, `SSP4` is the only tested alternative that is
faster than the default: about 1.5 s, or 4.2%, over 30 s. ARK437's wider
stability interval does not compensate for its additional stages here. These
command-line runs verify completion and timing, while the 500-bus GoogleTest
provides the stronger trajectory checks.

### Power-flow scaling on larger cases (diagnostic)

Before scoping residual parallelism to dynamic initialization, the Release
executable was run with `--powerflow-only` on the larger cases. With
`OMP_NUM_THREADS=32`, the experimental `auto` rule selected 32 threads for all
three cases. Each run completed successfully, but the results did not justify
parallelizing power flow.

| Case       |   Serial | Auto, 32 threads | Auto change | OpenMP, 4 threads | OpenMP, 8 threads |
| ---------- | -------: | ---------------: | ----------: | ----------------: | ----------------: |
| ACTIVSg10k |  4.323 s |          4.564 s |       +5.6% |           4.407 s |           3.874 s |
| ACTIVSg25k | 11.934 s |         14.189 s |      +18.9% |          11.892 s |          11.915 s |
| ACTIVSg70k | 38.029 s |         39.983 s |       +5.1% |          37.327 s |          36.059 s |

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

| Area                                       |  Calls |    Time |
| ------------------------------------------ | -----: | ------: |
| CVODE RHS callbacks                        |  5,818 |  3.77 s |
| CVODE algebraic solves                     |  5,916 |  3.50 s |
| CVODE differential derivative evaluation   |  5,818 |  0.30 s |
| KINSOL algebraic solves                    |  6,182 |  3.13 s |
| KINSOL residual callbacks                  | 17,614 |  2.84 s |
| KINSOL Jacobian callbacks                  |      2 | 0.003 s |
| Model Jacobian assembly                    |      — | 0.001 s |
| KINSOL remainder (`other_linear_solver_s`) |      — |  0.29 s |

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
