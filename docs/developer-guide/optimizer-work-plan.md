# Optimizer framework work plan

## Purpose

This plan takes GridDyn's distributed optimization objects from the current
solver-ready DC-OPF assembly path to a small integrated DC optimal-power-flow
solver. It also defines the solver-neutral boundary for later backends such as
HiGHS and for later AC-OPF and unit-commitment extensions.

The sequence is intentional: model identity, indexing, row definitions,
initialization, and write-back must be reliable before numerical solver math is
trusted. Every work package has an independent test gate.

The first supported problem is continuous, single-period DC-OPF. AC-OPF,
integer decisions, security constraints, and multi-period scheduling are later
extensions, but these interfaces must not prevent them.

## Current baseline

The library already has distributed optimization objects for areas, buses,
generators, links, loads, and relays. The optimization path now includes:

- MATPOWER/PYPOWER generator-cost loading into `GridGenOpt`;
- direct use of physical generator limits, generator dispatch, branch
  reactance, topology, and bus type from the power-system model;
- direct aggregation of passive physical load in `GridBusOpt`;
- DC bus-balance residuals and Jacobian contributions;
- fixed-angle constraints for active `SLK` and `AFIX` buses;
- documented DC equations adjacent to the relevant model code;
- branch-flow/source wiring through the physical link path;
- public optimization initialization that mirrors the power-flow/dynamics
  staged setup more closely than the original prototype;
- contiguous zero-based offset propagation and exact-size optimizer storage;
- `OptimizationData` as the callback-facing view object, with storage owned by
  the optimizer interface;
- factory/config creation for built-in optimizer implementations;
- an explicit `writeBack()`/`applySolution()` stage that keeps solve results
  separate from physical generator set points until committed;
- a basic dry-run optimizer for lifecycle and data-path testing;
- an `EconomicDispatchOptimizer` that performs a simple heuristic merit-order
  dispatch and can write the resulting dispatch back to generators; and
- a solver-neutral `NativeQpProblem` snapshot that owns the materialized
  columns, bounded affine rows, objective coefficients, and diagnostics needed
  by multiple backends;
- a dependency-free `NativeDenseSolver` that solves supported continuous
  linear and convex diagonal-quadratic DC-OPF problems with scaling,
  presolve, Phase-I feasibility, and deterministic active-set iterations; and
- a `NativeOptimizer` integration that validates candidates through the
  original GridDyn callbacks and stages results for explicit write-back.

The regression suite now covers the two-bus model and related data-path checks,
synthetic solver diagnostics, three-bus limit activation, PYPOWER comparisons
through IEEE-118, the 89-bus PEGASE case, and an Illinois200 native solve. A
setup-only case13659 PEGASE gate also exercises the distributed model at a
larger scale without attempting dense numerical solution. The next major gap
is a sparse high-performance backend, with HiGHS as the planned continuation.

The native-solver effort described below is one end-to-end pull request. The
stages are implementation and verification gates within that pull request, not
separate PR boundaries. The PR is complete only when an operational native
optimizer can solve the supported small DC-OPF cases and pass the corresponding
failure, write-back, and regression tests.

## Completed work

This section records the optimization-framework work completed before starting
the native dense solver effort.

- Added MATPOWER/PYPOWER generator-cost import support and preserved generator
  active-power min/max data needed by OPF.
- Established the ownership rule that the physical power-system model remains
  the source of truth for topology, active status, bus type, load, branch
  parameters, generator limits, and current set points.
- Kept economic data in the optimization layer because cost curves, penalties,
  commitment data, and future scheduling forecasts are not needed by ordinary
  power-flow or dynamics solves.
- Removed passive `GridLoadOpt` creation from normal initialization and moved
  fixed physical-load aggregation to `GridBusOpt`.
- Built out distributed optimization adapters for buses, generators, links,
  areas, loads, and relays so individual objects can contribute variables,
  objectives, rows, derivatives, and write-back behavior.
- Added DC nodal-balance equations in `GridBusOpt` and documented the equations
  in the implementation.
- Added DC branch flow/source-reactance wiring through `GridLinkOpt`, including
  branch-flow contributions to bus-balance rows.
- Added fixed-angle rows for all active buses that fix angle in the physical
  model, including slack buses and `AFIX` buses.
- Added two-bus DC residual/Jacobian regression coverage, including direct
  checks of both nodal balances at a known vector.
- Aligned optimization initialization and offset propagation with the staged
  patterns used by power-flow and dynamics initialization.
- Refactored `OptimizationData` so it is the object passed through callbacks,
  while optimizer interfaces/subclasses own the underlying storage arrays.
- Updated `OptimizerInterface` to allocate and manage primal values, bounds,
  row data, gradient/Jacobian data, tolerances, multipliers, scratch storage,
  and callback-facing views.
- Added factory/config aliases for built-in optimizers so the optimizer can be
  selected similarly to solver-interface selection.
- Added an explicit result-commit stage through `writeBack()`/`applySolution()`
  rather than automatically mutating the physical model during solve.
- Added the simple economic dispatch optimizer as an included, dependency-free
  heuristic optimizer.
- Added the native dense optimizer shell and `prepareProblemData()` path so the
  future solver can be developed behind the same interface.
- Added regression tests for factory selection, data views, lifecycle
  construction, problem loading, basic dry-run solve, economic dispatch
  solve/write-back, branch source/flow wiring, and a larger `case9` dry-run.
- Moved Visual Studio `MSB8028` warning demotion into CMake so the repository
  does not need a root `Directory.Build.props` file for this local build-log
  cleanup.

## Architecture rules

### One authoritative owner for each datum

| Information                                           | Owner                     | Optimizer treatment                                  |
| ----------------------------------------------------- | ------------------------- | ---------------------------------------------------- |
| Topology and in-service status                        | Physical model            | Read through a non-owning source reference           |
| Fixed demand and passive injections                   | Physical bus and children | Evaluate at the bus; create no passive `GridLoadOpt` |
| Branch `x`, fixed tap/shift, rating, and angle limits | Physical link             | Read for current assembly/evaluation                 |
| Bus type and specified angle                          | Physical bus              | Add a row for every active `SLK`/`AFIX` bus          |
| Generator dispatch, capability limits, and status     | Physical generator        | Use for guesses, bounds, and participation           |
| Generator costs and purely economic data              | `GridGenOpt`              | Own and evaluate in the optimization layer           |
| Offsets, sparse structure, and buffers                | Assembled problem         | Derive; never make a second network model            |
| Primal/dual result and diagnostics                    | Optimization result       | Apply only through an explicit commit                |

Data needed by power flow, dynamics, and optimization belongs to the physical
model. An optimization object may cache derived numerical data only with an
explicit invalidation rule; it must not expose an independently editable copy.

### Distributed formulation

The hierarchy mirrors the physical hierarchy and the recursive initialization
used by power flow and dynamics:

- an area discovers and initializes child optimization objects;
- a bus owns nodal equations and its reference-angle row;
- a generator owns dispatch variables and economic objective terms;
- a link supplies terminal-flow contributions and branch-limit rows; and
- a specialized object may add variables, objectives, rows, and derivatives
  without changing a central DC-OPF builder.

The root coordinates discovery, offsets, storage, validation, solve, and result
application. It does not reimplement equations owned by children.

### Solver-neutral boundary

Every solver consumes the same assembled problem contract and does not
traverse physical objects. The contract exposes:

- variable count, types, names, initial values, and bounds;
- constraint count, names, types, and bounds;
- objective value, gradient, and Hessian callbacks or assembled data;
- constraint residual and Jacobian callbacks or assembled data;
- a model generation/version used to detect stale assembly; and
- a result destination for values, duals, status, residuals, and diagnostics.

This is a derived view of distributed objects, not another data owner.

## DC equations

For each active bus `i`:

```text
g_i(x) = sum(Pg at i) - Pd_i - sum(P_ij leaving i) = 0.
```

For a branch oriented from `f` to `t`, with fixed tap `tau`, phase shift `phi`,
and reactance `x`:

```text
P_ft = (theta_f - theta_t - phi) / (x * tau),  P_tf = -P_ft.
```

Units and shift orientation must match physical-link and MATPOWER/PYPOWER
conventions. One flow function serves residuals, Jacobians, limits, results,
and tests.

For every active angle-fixing bus:

```text
h_i(x) = theta_i - theta_i_specified = 0,  bus type in {SLK, AFIX}.
```

Multiple rows may be independent across islands, additional physical
constraints within one island, duplicated/rank-redundant, or mutually
infeasible. Validation and solve must report those outcomes; the optimizer
must not silently discard a physical angle constraint.

The objective is `minimize sum(C_g(Pg_g))`, subject to balance, fixed-angle
rows, generator bounds, and supported branch bounds. Costs are
optimizer-owned; capability and network limits come from the physical model.

## Ordered work packages

| Milestone                    | Packages | Status                    | Result                                                      |
| ---------------------------- | -------- | ------------------------- | ----------------------------------------------------------- |
| A. Assembly foundation       | 1-5      | Complete for native scope | A unique, active, correctly indexed, solver-neutral problem |
| B. Complete DC model         | 6-8      | Complete for native scope | Verified DC equations, economics, and public initialization |
| C. Native solution           | 9        | Complete                  | Dependency-free dense DC-OPF solver with KKT diagnostics    |
| D. Equivalence and extension | 10-11    | Next chunk                | Larger sparse solves, HiGHS conformance, and future AC seam |

Milestones A and B now have enough implementation and regression coverage to
support the native-solver effort. Some polish remains in validation diagnostics
and in broader topology/feature tests, but those can be added alongside the
native solver as long as the solver continues to consume only the optimizer
problem contract.

### 1. Establish the optimization test boundary and invariants

**Status:** substantially complete for the current DC-OPF assembly path.

**Implementation**

- Add a dedicated `OptimizationTests` target and move or mirror the current
  PYPOWER regressions without weakening them.
- Add helpers for object counts, offsets, sparse entries, bounds, and
  finite-difference derivatives.
- Add assembly validation that reports multiple structural errors in one pass.
- Keep the equations above adjacent to their implementations in `GridBusOpt`,
  `GridLinkOpt`, and `GridGenOpt`.

**Tests**

- Preserve `case2.py` import, cost, branch-flow, and two-bus residual checks.
- Deliberately supply duplicate/out-of-range offsets and require an actionable
  validation failure.
- Confirm that the optimization-disabled build remains valid.

**Exit gate**

- Optimization tests run independently and structural validation requires no
  solver.

### 2. Make adapter mapping unique and initialization idempotent

**Status:** partly complete. The current path has practical source-object
wiring and stable test coverage for the two-bus/case9 cases. A formal root
identity registry and stronger reinitialization diagnostics remain useful.

**Implementation**

- Give every adapter a uniform non-owning `sourceObject()` contract.
- Maintain a root identity registry from `CoreObject*` to exactly one
  `GridOptObject*` for the active model generation.
- Route discovery, `getOptimizationObject()`, path creation, branch-terminal
  binding, and cost loading through the registry.
- Define ownership and rebuild behavior. Reinitializing an unchanged physical
  model reuses adapters instead of appending them.
- Ensure costs loaded before discovery attach to the same generator adapter
  later used by assembly.

**Tests**

- On the two-bus case, require exactly two bus, one branch, and one generator
  adapter, with no passive load adapter.
- Initialize twice and require stable counts, mappings, pointers, and offsets.
- Load cost before and after discovery and prove neither order duplicates the
  generator adapter.
- Repeat with `case9.m`, duplicate-like names, and a nested-area tie line.

**Exit gate**

- One physical object maps to at most one adapter per model generation, and
  source identity rather than optimizer ID or name determines the mapping.

### 3. Propagate active topology and define invalidation

**Status:** partly complete. Active participation is read from the physical
model in the implemented DC pathways. More explicit invalidation generation
tracking and unsupported zero-reactance diagnostics remain.

**Implementation**

- Derive participation from current physical enabled, connected, and
  in-service state.
- Retaining an inactive adapter with zero active size is acceptable, but it
  contributes no variables, rows, objective, load, or flow.
- Invalidate sizes, offsets, sparse structure, guesses, and bounds when
  topology or participation changes.
- Define a zero/near-zero reactance policy: jumper merge, a dedicated equality,
  or a clear unsupported diagnostic. Never silently return zero flow.

**Tests**

- Cover offline generators/branches, disconnected buses, disabled passive
  loads, and status change followed by reinitialization.
- Require no stale variable, row, cost, or Jacobian entry from inactive
  equipment.
- Cover zero reactance and assert the selected behavior and diagnostic.

**Exit gate**

- Assembly represents the physical model's active topology at a known
  generation, and evaluation rejects stale assembly.

### 4. Define one canonical zero-based problem layout

**Status:** substantially complete for the current solver path. The optimizer
storage is exact-sized and zero-based; extra mixed-integer and advanced layout
cases still need broader coverage before unit commitment work.

**Implementation**

- Replace integer setup modes with a named layout policy following the intent
  of simulation `OffsetOrdering`.
- Use zero-based contiguous solver storage; keep the objective constant outside
  the decision vector.
- Compute category bases at the root and recursively propagate child offsets
  using the power-flow/dynamics accumulation pattern.
- Correct all category accumulation, including extra continuous and integer
  variables, and implement link offset propagation.
- Define `objSize()` and `constraintSize()` as exact storage sizes and validate
  uniqueness, range, category, and ownership for every index.

**Tests**

- Run the default two-bus layout without manual `+1` allocation.
- Check all indices on two-bus, nine-bus, nested-area, multi-generator, extra
  continuous, and integer cases.
- Repeat size/offset loading and require identical results. If multiple layouts
  remain, run the same semantic suite against each.

**Exit gate**

- `values.size() == objSize()` and `rows.size() == constraintSize()` are safe,
  with no reserved entry or overlapping category offset.

### 5. Complete the solver-neutral problem contract

**Status:** substantially complete for DC LP/QP preparation. `OptimizationData`
is the callback view and `OptimizerInterface` owns the storage. The remaining
work is to harden row metadata diagnostics, duplicate sparse-entry policy, and
backend capability reporting.

**Implementation**

- Introduce an `OptimizationProblem` or equivalent non-owning view containing
  variable, row, objective, derivative, and model-version contracts.
- Represent every row with explicit lower and upper bounds. Equality rows have
  equal bounds; inequalities do not rely on backend sign conventions.
- Require every object reporting a row to supply bounds, value, Jacobian
  entries, and a diagnostic name.
- Separate objective gradient, objective Hessian, constraint Jacobian, and
  optional linear/quadratic export APIs.
- Define accumulation so independent children cannot overwrite sibling
  contributions.
- Validate finite coefficients, `lower <= upper`, complete row authorship,
  valid columns, and the documented handling of duplicate sparse entries.

**Tests**

- Use synthetic objects that independently add a variable, objective term, and
  constraint; verify child order does not change the result.
- Omit row metadata or return a bad sparse index and require rejection before
  solver initialization.
- Compare callbacks with any assembled linear/quadratic export at several
  points.

**Exit gate**

- A complete problem is inspectable and numerically evaluable without a
  backend, and a mock solver can consume it without traversing physical models.

### 6. Finish and verify the DC network formulation

**Status:** complete for the supported native DC scope. The implementation and
tests cover taps, phase shifts, thermal limits, angle limits, parallel
branches, active status, conservation, and invalid branch parameters. More
advanced island and rank policies remain future extensions.

**Implementation**

- `GridBusOpt` publishes one zero-bounded balance row per active bus and a
  separate zero-bounded angle row for every active `SLK`/`AFIX` bus.
- Seed bus angles from the physical bus state.
- Use one `GridLinkOpt` flow calculation for residuals, derivatives, limits,
  and reporting, including status, fixed tap, and phase shift.
- Add symmetric thermal bounds for finite positive ratings and angle-difference
  bounds where defined.
- An unconstrained branch reports no unused constraint row.

**Tests**

- Keep the known-vector two-bus test and verify both balances directly.
- Check the exact analytic and central finite-difference Jacobians.
- Cover a nonzero reference angle, multiple fixed angles, tap, phase shift,
  offline and parallel branches, thermal/angle limits, and a nested tie line.
- Check conservation: summed bus residuals cancel internal lossless flows and
  equal total generation minus fixed demand.
- Treat one reference per island as normal, multiple feasible angle fixes as
  physical constraints, duplicate rows as a reported rank condition,
  incompatible fixes as infeasible, and an island without a reference as
  singular/invalid.

**Exit gate**

- Residuals, bounds, and derivatives match the documented equations for all
  supported branch features, and every DC row is inspectable before solve.

### 7. Complete generator variables and economic objectives

**Status:** complete for the native linear/convex diagonal-quadratic scope.
Polynomial generator costs, bounds, gradients, Hessians, and constant terms
are loaded and exercised. Piecewise-linear costs remain intentionally
unsupported, with explicit rejection and diagnostics.

**Implementation**

- Allocate active-power variables only for participating generators.
- Read bounds and initial dispatch from the physical generator; clamp or
  diagnose a guess outside its physical bounds.
- Make absent cost safe and explicit. An allowed zero-cost generator may emit a
  diagnostic but never causes an out-of-range coefficient access.
- Define coefficient order, per-unit/MW conversion, system-base scaling, and
  objective constant handling in one place.
- Implement consistent objective value, gradient, and Hessian for supported
  polynomial costs.
- Model MATPOWER piecewise-linear cost with epigraph variables/rows, or reject
  it until complete. Never silently accept a cost that is not assembled.
- Defer startup, shutdown, and multi-period data until temporal decisions exist.

**Tests**

- Preserve the `case2.py` check: `0.01 * Pg^2 + Pg` is `11.0` at `10 MW` on a
  `100 MVA` base.
- Test linear, quadratic, absent, and malformed costs at multiple system bases.
  If a generic higher-order callback is retained, test its derivatives and
  require a native-QP capability rejection before solve.
- Compare analytic objective gradients and Hessians with finite differences.
- If PWL is enabled, test breakpoints, slopes, epigraph rows, and objective.
- Validate MATPOWER `gencost` row counts of `ng` and `2*ng`, stable source
  mapping, and deterministic rejection of incomplete rows.
- Load `case9.m` and ensure every cost remains attached to its generator.

**Exit gate**

- Each participating generator has safe bounds, a valid guess, and an explicit
  objective representation whose values and derivatives pass independent tests.

### 8. Mirror the simulation initialization lifecycle

**Status:** substantially complete for constructing and loading a DC problem
through the public optimizer path. More explicit lifecycle-state and
invalidation tests remain desirable.

**Implementation**

Give `GridDynOptimization` a public path that parallels power-flow and dynamic
setup:

1. Prepare/check the physical model and current event state.
2. Perform optimization discovery/phase A.
3. Validate active topology and supported features.
4. Load sizes and propagate offsets.
5. Allocate exact-size problem and optimizer storage.
6. Perform phase B and assemble names, guesses, bounds, and structure.
7. Run structural and numerical validation.
8. Initialize the selected optimizer.

Also:

- resize `mOptimizerData` and all primal/dual vectors before indexed use;
- make the basic internal optimizer available for DC mode through the normal
  factory;
- define lifecycle states, legal transitions, invalidation, and reinitialization;
  and
- use a solved power flow as a useful seed when available, not as a hidden
  mathematical prerequisite for assembling DC-OPF.

**Tests**

- Build the two-bus problem through only the public lifecycle, with no manual
  child sizing, offsets, or over-allocation.
- Verify phase order, exact allocation, repeated initialization, multiple
  optimizer slots, and failure for an unsupported mode.
- Change topology or a limit after initialization and verify invalidation.
- Require invalid input to return diagnostics before assertion, invalid memory
  access, or solver entry.

**Exit gate**

- One public call produces a complete validated DC problem and initialized
  optimizer, using a lifecycle recognizable beside normal simulation setup.

### 9. Add the compact native DC-OPF solver

**Status:** complete for the defined native-solver scope. The dense solver is
operational behind `NativeOptimizer::solve()` and remains intentionally
limited to continuous single-period DC LPs and convex diagonal-QPs.

**Implementation**

- Limit version one to small continuous convex DC problems with linear rows,
  box bounds, and linear or convex quadratic cost.
- Implement a dense bounded-QP method for regression use. A small active-set
  method with a pivoted KKT solve is a reasonable first choice; scaling,
  tolerances, and iteration limits must be explicit.
- Normalize row and variable bounds into the solver's equality and inequality
  representation, then run a bounded Phase-I feasibility step.
- In the active-set loop, solve the scaled KKT system, select blocking bounds,
  update the working set from multiplier signs, and terminate only when primal
  and dual residuals meet their documented tolerances.
- Consume only the solver-neutral problem contract.
- Return distinct statuses for optimal, infeasible, unbounded,
  singular/rank-deficient, unsupported/nonconvex, numerical failure, and
  iteration limit.
- Report objective, primal feasibility, bound violation, stationarity,
  complementarity, and iteration count.
- Keep results separate from the physical model. Commit with `setValues()` or
  equivalent only after success and only when requested.

**Tests**

- Solve an analytic two-bus dispatch with known generation, angle, flow, and
  objective.
- Add a three-bus/two-generator case for uncongested dispatch, a binding
  generator bound, and a binding branch rating.
- Cover multiple feasible angle fixes, duplicate/rank-deficient rows,
  incompatible fixed angles, insufficient generation, a missing island
  reference, nonconvex cost, and an iteration limit.
- Check KKT residuals, not only primal values, and require repeatable outcomes.

**Exit gate**

- Small supported cases converge without an external library, while failure
  cases return useful statuses and leave the physical model unchanged.

**Current checkpoint**

`NativeDenseSolver` now provides the dependency-free numerical core and
`NativeOptimizer` integrates it with GridDyn lifecycle, callback validation,
diagnostics, and explicit write-back. The native solver is intentionally a
small and understandable backend, not the expected large-scale production
solver. Its dense KKT factorization and one-threaded active-set iterations are
appropriate for regression cases through IEEE-118 and a useful Illinois200
scale probe, but they grow superlinearly with problem size.

### 10. Validate MATPOWER/PYPOWER equivalence

**Status:** substantially complete for the supported native DC formulation.
Reference comparisons are in place through IEEE-118; larger-scale validation
now belongs with the sparse/HiGHS backend.

**Implementation**

- Store reference results with tool version, formulation options, base, and
  explicit tolerances.
- Compare objective, generator dispatch, aligned bus angles, branch flows,
  active bounds, and solver status.
- Add economic import/export checks so round trips cannot detach or reorder
  costs.
- After explicitly applying a DC optimum, optionally run GridDyn AC power flow
  and report expected AC-feasibility differences as such.

**Tests**

- Use `case2.py` as the exact end-to-end regression, followed by `case9.m`,
  `case14.m`, `case39.m`, `case57.m`, and `case118.m` for progressively larger
  multi-generator comparisons. Keep PWL cases such as `case30pwl.m` outside
  the native scope until PWL support is deliberately added.
- Include status, fixed taps/shifts, branch ratings, generator bounds, and
  fixed-angle behavior rather than comparing only objective value.
- Separate model-assembly tolerances from solver tolerances so formulation
  errors cannot be dismissed as numerical noise.

**Exit gate**

- Supported cases agree with the selected MATPOWER/PYPOWER DC formulation
  within documented tolerances; unsupported features stop before solve.

**Current checkpoint**

The native regression ladder compares objective and dispatch against the
corresponding PYPOWER/MATPOWER formulations for cases 9, 14, 39, 57, and 118,
with additional feasibility, bound, and aggregate checks for the 89-bus
PEGASE case. These comparisons use the same per-unit DC assumptions and do not
claim AC feasibility.

### 11. Prove the external-solver and AC-extension seams

**Status:** next chunk. The distributed model and native wrapper preserve the
intended seam, but an independently exercised backend conformance layer and a
real sparse backend remain to be added.

**Implementation**

- Add a mock external adapter that consumes the same contract and returns a
  controlled result without adding a dependency.
- Add backend capability queries for LP, QP, nonlinear, integer, and PWL forms.
- Start the optional HiGHS backend after the solver-neutral contract has a
  backend conformance suite. Continue HiGHS-specific details in
  [`highs-opf-plan.md`](highs-opf-plan.md).
- Keep DC bus/link abstractions extensible so AC subclasses can add voltage,
  reactive power, nonlinear balance, and derivatives while reusing identity,
  hierarchy, offsets, lifecycle, validation, and result handling.

**Tests**

- Run one backend-conformance suite against native, mock, and enabled external
  backends.
- Prove no backend reaches into physical GridDyn objects.
- Add a test subclass that contributes an extra variable, objective term,
  nonlinear row, and derivatives without modifying the root assembler.

**Exit gate**

- A second solver can connect without rebuilding equations or duplicating
  data, and an AC object can extend the distributed contracts without changing
  the DC solver or lifecycle architecture.

## Native solver chunk delivery boundary

This chunk delivers the complete operational native optimizer in one PR. The
scope is intentionally compact and dependency-free:

- continuous single-period DC-OPF only;
- linear rows, variable box bounds, and linear or convex diagonal-quadratic
  generator costs;
- active topology, fixed angles, branch status, fixed taps, phase shifts,
  thermal limits, and supported angle-difference limits;
- a dense bounded-QP method with explicit feasibility, optimality, scaling,
  tolerance, and iteration diagnostics; and
- explicit result staging and write-back, with no physical-model mutation on
  solve or failure.

The native mathematical representation is intentionally solver-neutral. The
dense implementation is only the first backend: a later HiGHS integration must
be able to consume the same columns, rows, bounds, objective coefficients,
affine normalization, and stable ordering without re-deriving the GridDyn
constraints. HiGHS integration is outside this PR, but compatibility with its
standard bounded-row LP/QP model is part of this PR's contract.

AC-OPF, nonlinear and nonconvex costs, piecewise-linear costs, integer
variables, multi-period scheduling, sparse numerical linear algebra, and an
external solver backend remain outside this PR. IEEE-118 is the primary larger
correctness gate. A 240-bus case is an optional performance/robustness check and
does not replace the required smaller-case regressions.

The following stages are internal gates in the single PR.

### Stage 0: Complete the DC formulation boundary

- Make one signed DC flow calculation cover reactance, fixed tap, phase shift,
  active status, residuals, Jacobians, branch limits, and result checks.
- Add flow-limit rows for finite positive physical `ratingA` values.
- Add meaningful angle-difference rows while treating MATPOWER/PYPOWER
  `-360/360` limits as unconstrained.
- Propagate link constraint offsets in both flat and grouped layouts.
- Reject or diagnose zero/near-zero reactance and invalid tap values instead of
  silently producing zero flow.
- Add direct tests for tap, phase shift, status, thermal limits, angle limits,
  parallel branches, and conservation of internal flows.

Current checkpoint: Stage 0 is complete and covered by the optimization
formulation suite. It includes active-status filtering, fixed tap and
phase-shift flow semantics, thermal and angle-limit rows, affine bound
handling, link offset propagation, and invalid reactance/tap diagnostics. The
parallel-branch and flow-conservation coverage is also included. The native
solver consumes this boundary without adding a second network formulation.

### Stage 1: Freeze and materialize the native QP contract

- Materialize values, bounds, objective coefficients, row bounds, affine row
  constants, Jacobian entries, names, types, tolerances, and model version into
  a solver-only dense problem structure.
- Define a solver-neutral LP/QP intermediate representation using standard
  column bounds, row bounds, a single `A` matrix, linear objective terms, and
  diagonal quadratic terms. Equality rows use equal lower and upper bounds;
  unbounded sides use explicit infinity semantics.
- Normalize every affine row as `lower <= A*x + offset <= upper`, with a
  documented conversion to the equivalent bounded-row form expected by both
  the dense backend and HiGHS. Phase-shifted branch rows must be represented by
  this same normalization rather than a backend-specific special case.
- Use the callback/Jacobian representation as the canonical GridDyn input and
  verify any explicit linear export rather than double-counting it. The native
  dense matrix is a materialized view of this representation, not a second
  formulation.
- Preserve stable column and row ordering and optional names so a future HiGHS
  adapter can load the same model and map primal/dual results back to GridDyn.
- Classify supported LP/convex-QP problems before numerical solve and reject
  unsupported modes, integer variables, PWL costs, nonconvex costs, malformed
  bounds, and non-finite coefficients.

Phase 1 is complete when the same materialized problem can be consumed by the
dense solver and expressed directly as a HiGHS-style bounded-row LP/QP model
without changing any physical constraint definition.

Current checkpoint: Phase 1 is complete. `NativeQpProblem` is the frozen
solver-neutral LP/QP
contract. `NativeOptimizer::prepareProblemData()` materializes stable columns,
box bounds, affine bounded rows, a dense row-major Jacobian, objective constant
and coefficients, gradients, names, types, tolerances, and a monotonically
increasing model version. It validates callback/Jacobian consistency and
explicit linear-row agreement, classifies continuous linear and convex
diagonal-quadratic cases, and rejects unsupported or malformed input. The
contract is covered by two-bus callback/Jacobian equivalence and
phase-shift tests, plus finite deterministic assembly tests for the two-bus,
case9, and IEEE-118 cases. Repeated preparation is checked for identical
solver data, and physical-model mutations after preparation are checked not to
change the snapshot. A future HiGHS adapter can convert each row using
`lower - offset` and `upper - offset` without touching the physical model or
re-deriving constraints.

### Stage 2: Implement the dense feasibility and active-set core

- Implement a small pivoted dense KKT factorization using standard-library
  containers only.
- Add bounded Phase-I feasibility using nonnegative violation variables.
- Implement equality and active-inequality working-set updates, blocking-step
  selection, multiplier-sign releases, deterministic tie-breaking, and
  explicit iteration limits.
- Distinguish optimal, infeasible, unbounded, singular/rank-deficient,
  numerical-failure, unsupported, nonconvex, and iteration-limit outcomes.

Current checkpoint: `NativeDenseSolver` provides the dependency-free Stage 2
core. It consumes the frozen `NativeQpProblem`, scales rows and variables into
bounded internal coordinates, eliminates fixed variables, removes redundant
consistent equalities while reporting inconsistent ones as infeasible, and
uses a pivoted dense KKT solve with deterministic active-set updates. Its
Phase-I model keeps initially feasible sides as ordinary constraints and adds
nonnegative violation slacks only for initially violated sides. Active-set
convergence rechecks feasibility before reporting optimality, including
scale-aware blocking-step tie-breaking for degenerate starts. Synthetic tests
cover bounded convex QP optimality, Phase-I equality feasibility,
infeasibility, unbounded linear directions, fixed variables, row scaling,
redundant/inconsistent equalities, and the three-bus dispatch/limit fixture.

### Stage 3: Integrate, diagnose, and protect write-back

- Replace the current `NativeOptimizer::solve()` placeholder with the dense
  solver call.
- Validate the candidate through the original objective, constraint, and
  Jacobian callbacks before accepting it.
- Report objective, primal violation, bound violation, stationarity,
  complementarity, active-set size, and iteration count.
- Keep failed or unvalidated candidates out of the committed optimizer state.
- Permit `writeBack()` only after a validated optimal result and only when the
  caller explicitly requests it.

Current checkpoint: `NativeOptimizer::solve()` now runs the dense solver for
supported continuous DC LP/diagonal-QP problems, validates the candidate
objective, gradient, constraints, bounds, and Jacobian through the original
callbacks, records solver diagnostics, and leaves the GridDyn model untouched
until `writeBack()`. Failed solves and failed callback validation cannot be
written back. The two-bus integration test exercises this complete sequence.

### Stage 4: Run the complete regression ladder

- Keep the two-bus analytic case as the smallest proof.
- Add a three-bus/two-generator fixture covering uncongested dispatch,
  generator-bound activation, and branch-limit activation.
- Solve and compare `case9.m`, `case14.m`, `case39.m`, `case57.m`, and `case118.m` against stored
  PYPOWER/MATPOWER references with documented formulation and unit tolerances.
- Solve `case89pegase.m` as a larger native dense-solver gate. Because all
  generators have the same linear cost, verify aggregate dispatch, objective,
  feasibility, and bounds rather than expecting a unique generator dispatch.
- Add deterministic repeated-solve, lifecycle, invalidation, unsupported-mode,
  infeasibility, rank, and write-back tests.
- Add a setup-only large-case gate using `case13659pegase.m`. Load the case,
  initialize the optimization hierarchy, materialize bounds/objective data,
  evaluate callbacks, and validate sparse Jacobian indices, finiteness, timing,
  and callback storage. This gate must not call `solve()` or require dense
  Jacobian materialization before the sparse/HiGHS backend is integrated.
- Measure a 240-bus case when an input is available, recording solve time and
  memory behavior without making it the first acceptance gate.

Current checkpoint: the three-bus/two-generator native solve covers
uncongested dispatch, a generator upper limit, and a binding branch limit.
The native optimizer solves the standard `case9.m`, `case14.m`, `case39.m`,
`case57.m`, and `case118.m` DC-OPFs and compares their per-unit dispatch and
objective values against the corresponding PYPOWER/MATPOWER reference
formulations. It also solves the 89-bus PEGASE case with its canonical
aggregate checks. The case13659 PEGASE setup-only gate also passes without
calling the dense solver: the Release baseline materializes 17,751 variables,
13,660 rows, and 55,002 sparse Jacobian entries with about 2.94 MB of callback
storage (approximately 6.3 seconds total on the local Windows build).

The Illinois200 scale probe demonstrates the current dense limit: 249
variables, 446 rows, an approximately 0.85 MiB dense constraint matrix, and 41
active-set iterations take about 5.9 seconds in Release and about 100 seconds
in Debug. The work is CPU-bound and single-threaded; setup and callback
assembly are negligible by comparison. This is sufficient evidence that the
model contract scales beyond IEEE-118, but not that dense numerical solution
will scale to thousands of buses. No exact local 240-bus costed case is
currently available, so 240 buses remain an optional future measurement.

### Single-PR acceptance gate

The PR is accepted only when the native optimizer is operational, the dense
solver unit tests and GridDyn integration tests pass, the relevant optimization
and non-optimization regressions pass, and the native definition of done below
is satisfied. No intermediate stage is intended to be merged independently.

## Native solver chunk closeout

This single PR has completed the native-solver chunk as one end-to-end change;
the stages above were implementation and verification gates, not merge
boundaries. The completed result is a compact, understandable native backend
that is useful for small and medium regression cases while retaining a
solver-neutral contract for the next backend.

- The DC model has one canonical affine row form,
  `lower <= A*x + offset <= upper`, including phase-shifted branch limits.
- `NativeQpProblem` is a solver-owned snapshot. It is populated from the
  existing GridDyn callbacks, validates constant Jacobians and supported
  continuous LP/convex diagonal-QP structure, and preserves stable columns,
  rows, names, bounds, objective coefficients, and offsets.
- `NativeDenseSolver` provides pivoted dense KKT solves, variable and row
  scaling, fixed-variable elimination, redundant-equality presolve, bounded
  Phase-I feasibility, deterministic active-set updates, and explicit
  diagnostics.
- `NativeOptimizer::solve()` validates the candidate with the original
  callbacks and does not mutate physical GridDyn objects until an explicit
  `writeBack()`.
- The 44-test `OptimizationTests` regression ladder passes in Release for the
  synthetic solver cases, three-bus dispatch/limit cases, case9, case14,
  case39, case57, case89 PEGASE, IEEE-118, and the Illinois200 scale probe.
  The case13659 PEGASE test is setup-only by design.

The simple economic stacker remains a separate dependency-free heuristic
optimizer. It exercises lifecycle and write-back behavior, while the native
solver provides actual KKT-based optimization for its supported problem class.

## Next chunk: sparse HiGHS backend and large-case performance

The next chunk should add a production-oriented sparse backend without
changing GridDyn's physical data ownership or re-deriving the DC equations.
HiGHS is the planned backend. The native dense solver remains valuable as a
small deterministic reference implementation and contract test oracle.

### Objectives

1. **Add a sparse solver-owned representation.** Preserve the callback and
   `MatrixData<X>` representation as the canonical GridDyn input, but add a
   sparse snapshot/translation path that stores row/column/value entries in a
   HiGHS-compatible format. Do not materialize a dense `A` matrix for large
   problems. Define duplicate-entry summation, zero-entry removal, stable
   ordering, and index validation once at this boundary.
2. **Add optional HiGHS integration.** Introduce a narrowly scoped CMake option
   such as `GRIDDYN_ENABLE_HIGHS_OPTIMIZATION`, keep HiGHS headers and types
   out of general GridDyn model headers, and start with the bounded-row LP/QP
   form already used by `NativeQpProblem`. Map HiGHS statuses, primal values,
   row/column duals, objective, and residual diagnostics into the existing
   `NativeSolveResult`/optimizer result path.
3. **Prove backend conformance.** Add a mock or translator-level backend test
   that consumes the same problem snapshot as the native solver. Run common
   objective, gradient, affine-row, bound, status, repeatability, and
   write-back tests against the dense and HiGHS paths. A HiGHS adapter must not
   access physical GridDyn objects.
4. **Exercise scale safely.** Keep the case13659 PEGASE test setup-only until
   the sparse path is active. Use IEEE-118 as the first native/HiGHS numerical
   equivalence gate, then Illinois200 and any available 240-bus costed case for
   timing, memory, and solution-quality measurements. The 13,659-bus case is a
   model-assembly and sparse-loading gate, not a dense-solver target.
5. **Document numerical policy and packaging.** Define how model tolerances
   map to HiGHS tolerances, whether HiGHS or GridDyn owns scaling, how duals
   and LMPs are reported, and how optional dependency discovery behaves across
   MSVC, Linux, macOS, and Python-wheel builds.

### Suggested implementation order

1. Extract and test a sparse snapshot/HiGHS row-bound translator without
   linking HiGHS. Verify that `lower - offset` and `upper - offset` reproduce
   every dense bounded row, including phase-shifted thermal and angle rows.
2. Add the optional HiGHS CMake target and a minimal LP adapter. Reuse the
   existing lifecycle, `NativeQpProblem` metadata, candidate validation, and
   explicit write-back.
3. Add convex diagonal-QP support and dual/result mapping after the LP path
   is stable. Compare HiGHS and native results on the two-bus, case9, case14,
   and IEEE-118 suite.
4. Run Illinois200 and the case13659 setup gate with measured wall time,
   peak/working memory, sparse nonzero counts, and solver diagnostics. Add a
   240-bus case only when a costed input is available.
5. Revisit threading only after profiling the sparse backend. The current
   dense implementation is CPU-bound and single-threaded, but parallelizing
   callback loops will not address its dominant dense KKT factorization cost;
   sparse solver factorization and HiGHS configuration are the higher-value
   performance path.

### Explicit non-goals for the next chunk

AC-OPF, nonlinear/nonconvex costs, PWL costs, unit commitment, topology
switching, and a replacement of the compact native solver remain separate
efforts. The HiGHS chunk should first establish a fast, solver-compatible DC
LP/QP backend and large-case path.

## Verification plan for the next chunk

| Layer       | What to verify                                               | Example checks                                                                   |
| ----------- | ------------------------------------------------------------ | -------------------------------------------------------------------------------- |
| Translation | Sparse and dense paths describe the same bounded-row problem | `lower - offset`, `upper - offset`, stable indices, duplicate-entry policy       |
| Conformance | Backends consume the same solver-neutral contract            | native/mock/HiGHS objective, rows, bounds, statuses, and write-back              |
| Algebra     | Sparse rows preserve the GridDyn equations                   | phase-shifted branch flow, thermal limits, angle limits, and nodal balances      |
| Equivalence | HiGHS agrees with the native/reference formulation           | two-bus, case9, case14, and IEEE-118 objective, dispatch, flows, and feasibility |
| Scale       | Large assembly avoids dense allocation                       | case13659 counts, sparse nonzeros, callback time, and memory                     |
| Performance | Sparse solve scales beyond the dense reference               | Illinois200 and an available 240-bus costed case, Release timings and memory     |
| Results     | Duals and statuses are mapped without changing write-back    | row/column duals, LMPs, failed solve isolation, explicit `writeBack()`           |

## Verification ladder

Use the narrowest applicable level first:

1. **Structural:** identity, counts, sizes, offsets, row authorship, bounds,
   sparse indices, and lifecycle state.
2. **Direct algebra:** hand-calculated objective, flow, residual, and bounds at
   selected vectors.
3. **Derivatives:** analytic gradient, Jacobian, and Hessian versus central
   finite differences at nonzero points.
4. **Native numerical:** known optima, KKT residuals, and deterministic failure
   statuses on small networks.
5. **Reference:** MATPOWER/PYPOWER comparison with identical formulation
   assumptions and explicit tolerances.
6. **Regression:** optimization-enabled and disabled builds plus reader,
   library, component, and power-flow tests touched by the change.

Once `OptimizationTests` exists, local verification should include commands
equivalent to:

```text
cmake -S . -B build -DGRIDDYN_ENABLE_OPTIMIZATION_LIBRARY=ON
cmake --build build --config Debug --target OptimizationTests LibraryTests FileReaderTests PowerflowSystemTests --parallel 4
ctest --test-dir build -C Debug -R "OptimizationTests|LibraryTests|FileReaderTests|PowerflowSystemTests" --output-on-failure
```

Run a normal build first. Use the documented Windows `Path`/`PATH` workaround
only for the specific duplicate-environment-key MSBuild failure.

## Native DC-OPF definition of done

The milestone is complete only when:

- physical and economic data have the authoritative owners defined above;
- initialization is idempotent and mirrors the simulation lifecycle;
- every variable and row uses validated zero-based contiguous storage;
- variables, objective, bounds, rows, and derivatives are independently
  inspectable without a solver;
- DC equations cover status, fixed taps/shifts, fixed angles, generator limits,
  thermal limits, and supported angle limits;
- the native solver reaches known solutions and KKT tolerances for the small
  regression suite;
- infeasible, singular, unsupported, and nonconvex cases produce distinct
  diagnostics without corrupting the physical model;
- `case2.py`, `case9.m`, and selected larger cases agree with
  MATPOWER/PYPOWER within formulation-specific tolerances;
- optimization-enabled and disabled builds pass relevant regressions; and
- the solver-neutral contract is sufficiently explicit for a second backend
  to consume it without reaching into physical GridDyn objects. A mock/external
  backend conformance test is part of the next HiGHS chunk, not a prerequisite
  for this native-solver merge.

Only after this gate should an external backend or AC-OPF equations become the
primary implementation focus.
