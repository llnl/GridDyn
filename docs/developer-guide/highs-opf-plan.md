# HiGHS-based optimal-power-flow plan

## Purpose

This note records the initial direction for making GridDyn's existing
optimization library useful for power-system optimization.  It is deliberately
a planning baseline, not a design specification.  The first deliverable is a
reliable, cross-platform **DC optimal power flow (DC-OPF)** implementation;
AC-OPF and discrete optimization are follow-on work.

## Why begin with HiGHS

[HiGHS](https://highs.dev/) is an open-source, MIT-licensed C++ solver for
large sparse LP, QP, and MIP problems.  It is a strong first backend because:

- DC-OPF is naturally an LP or convex QP: power balance and line-flow limits
  are linear, while the existing polynomial generator-cost representation can
  be linear or quadratic.
- Its native C++ API fits GridDyn directly; no Python or MATLAB/Julia runtime
  is needed for the core solver.
- It is CMake-friendly and supports Windows, macOS, and Linux, making it a
  realistic candidate for GridDyn's native builds and Python wheel builds.
- A DC-OPF provides immediate value: economic dispatch, constrained flows,
  generator limit enforcement, and dual prices/LMPs.  It establishes the
  optimization-object and result-reporting interfaces needed by future work.
- The first solver integration avoids the nonlinear sparse-linear-algebra and
  packaging complexity of AC-OPF solvers such as Ipopt.

HiGHS is not an AC nonlinear solver.  Its MIP capability may later help with
discrete scheduling decisions, but unit commitment, switching, and
mixed-integer AC optimization are explicitly outside the first scope.

## Current starting point

The optional `optimization` static library is enabled by
`GRIDDYN_ENABLE_OPTIMIZATION_LIBRARY`.  It already contains useful model-side
concepts: optimization objects for buses, generators, links, loads, and areas;
variable and constraint allocation; and generator cost/bound data.

It does **not** yet contain a solver backend.  `OptimizerInterface::solve()`
returns an unimplemented status, and `makeOptimizer()` creates only the basic
placeholder for `FlowModel::NONE`; DC and AC modes have no optimizer.  The
first implementation should preserve this separation: GridDyn describes the
network model, while a HiGHS adapter performs solver-specific matrix assembly
and calls.

## Initial DC-OPF scope

The first end-to-end problem should use a fixed, connected AC network under
the standard DC approximation.

| Include | Defer |
| --- | --- |
| Generator active-power variables, limits, and linear/quadratic costs | AC voltage magnitude and reactive-power optimization |
| One reference-bus angle and bus-angle variables | Generator PV/slack switching behavior |
| Nodal active-power balance | Controllable transformer taps, phase shifters, and switched shunts |
| In-service branch DC flow and thermal limits | Losses, contingencies, topology switching, and integer commitments |
| Objective value, dispatch, flows, constraint status, and dual prices | AC-OPF, security-constrained OPF, and multi-period scheduling |

The input network must first solve in GridDyn's existing power-flow path.  A
clearly reported unsupported condition is preferable to silently applying a
different approximation.

## Major work chunks

### 1. Establish the model contract

Define the supported DC-OPF semantics before binding to a solver:

- Select system base, angle units, branch-flow sign convention, and treatment
  of transformer taps/phase shifts.
- Specify how fixed loads, negative generation, outages, generator cost curves,
  and zero/infinite limits are represented.
- Decide the public operation/API and result locations (for example an
  `OptimizationMode` selection plus queryable objective, dispatch, prices, and
  diagnostics).
- Add small hand-checkable cases that state expected dispatch, flow, and price
  results.

### 2. Make the DC model complete and inspectable

Complete or adapt the existing optimization objects so they consistently
allocate variables, bounds, objective coefficients, and sparse constraint
entries for the initial scope.  Build a solver-independent intermediate model
(columns, row bounds, sparse matrix, linear cost, optional quadratic cost), so
tests can inspect it without requiring HiGHS.

This is also the right point to add clear diagnostics for unsupported network
features and invalid/infeasible input.

### 3. Integrate HiGHS as an optional CMake dependency

Add a narrowly scoped CMake option, such as
`GRIDDYN_ENABLE_HIGHS_OPTIMIZATION`, which is only meaningful when the
optimization library is enabled.  Support a system-installed package first;
evaluate vendoring, FetchContent, or package-manager acquisition separately.

Implement a `HighsOptimizer` derived from `OptimizerInterface` that translates
the intermediate DC model to HiGHS sparse LP/QP data, configures tolerances,
executes the solve, and maps status and primal/dual results back to GridDyn.
Keep HiGHS headers and types out of general GridDyn model headers.

### 4. Connect results to normal GridDyn use

Provide a deliberate result-application policy.  Initially, expose the
optimal point as an optimization result and allow explicit application to
generator setpoints; do not silently overwrite a solved power-flow case.

After applying an optimal dispatch, run the existing AC power flow as a
feasibility and reporting check.  The DC optimum will not in general exactly
match the AC solution because it omits voltage and reactive constraints.

### 5. Validate, package, and document

Test the model algebra independently, then solve small cases with known
answers.  Compare larger MATPOWER-imported cases against MATPOWER DC-OPF for
objective, dispatch, branch flow, and LMP tolerance.  Use the existing
cross-platform CI and Python-wheel workflow to verify Windows/MSVC, macOS,
and Linux packaging.

Document solver availability, build flags, supported features, numerical
tolerances, status meanings, and a small C++/Python example.

## Rough delivery sequence

1. **Foundation:** model contract, intermediate sparse problem, and
   solver-independent unit tests.
2. **Useful minimum:** HiGHS LP DC-OPF with linear costs, P limits, balance,
   line limits, results, and infeasibility diagnostics.
3. **Economic DC-OPF:** quadratic costs, dual/LMP reporting, transformer
   approximation rules, MATPOWER comparison cases, and user documentation.
4. **Distribution:** optional dependency configuration and tested native and
   Python-wheel builds on all supported platforms.
5. **Extensions:** multi-period/commitment experiments using HiGHS MIP, or a
   separate Ipopt backend for continuous AC-OPF.  These should not delay the
   stable DC-OPF baseline.

## Decision points to revisit

- Whether HiGHS is obtained from an installed package, a pinned source
  dependency, or a vendored copy.  Reproducible CI and wheel builds matter more
  than minimizing initial CMake work.
- Whether quadratic generator costs are required in the first public release
  or are introduced immediately after the LP baseline.
- How closely branch ratings and transformer behavior should match the existing
  RAW, EPC, and MATPOWER reader semantics.
- The intended Python API: direct optimization objects versus a compact
  `solve_opf()` result object.  It should expose solver status and diagnostics,
  not only a vector of values.
- Licensing and distribution review for the selected HiGHS build configuration
  and any transitive numerical-library dependencies.

## Non-goals for this plan

This plan does not claim AC feasibility from a DC-OPF result, replace GridDyn's
power-flow solver, or select a long-term mixed-integer nonlinear solver.  It
creates a clean, tested route from the existing optimization abstractions to a
useful first operational capability.
