# Optimizer framework work plan

## Purpose

This plan takes GridDyn's distributed optimization objects from the current
two-bus DC regression to a small integrated DC optimal-power-flow solver. It
also defines the solver-neutral boundary for a later backend such as HiGHS.

The sequence is intentional: model identity, indexing, row definitions, and
initialization must be reliable before numerical solver work starts. Every work
package has an independent test gate.

The first supported problem is continuous, single-period DC-OPF. AC-OPF,
integer decisions, security constraints, and multi-period scheduling are later
extensions, but these interfaces must not prevent them.

## Current baseline

The library already has distributed optimization objects for areas, buses,
generators, links, loads, and relays. It also has:

- MATPOWER/PYPOWER generator-cost loading into `GridGenOpt`;
- direct use of physical generator limits and branch reactance;
- direct aggregation of passive physical load in `GridBusOpt`;
- DC bus-balance residuals and Jacobian contributions;
- fixed-angle constraints for active `SLK` and `AFIX` buses; and
- a two-bus PYPOWER regression for branch flow and both nodal balances.

It is not yet a safe assembled optimization problem. Foundation work remains
in adapter identity, idempotent initialization, offsets, constraint metadata,
DC branch semantics, safe cost handling, guesses, and lifecycle allocation.

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

| Milestone                    | Packages | Result                                                      |
| ---------------------------- | -------- | ----------------------------------------------------------- |
| A. Assembly foundation       | 1-5      | A unique, active, correctly indexed, solver-neutral problem |
| B. Complete DC model         | 6-8      | Verified DC equations, economics, and public initialization |
| C. Native solution           | 9        | Small DC-OPF cases solved with KKT diagnostics              |
| D. Equivalence and extension | 10-11    | MATPOWER/PYPOWER agreement and a proven backend/AC seam     |

Milestone A is the next implementation chunk. Solver work begins only after
its structural exit gates pass; Milestone B then completes the numerical model
the solver will consume.

### 1. Establish the optimization test boundary and invariants

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

### 10. Validate MATPOWER/PYPOWER equivalence

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

- Use `case2.py` as the exact end-to-end regression, `case9.m` as the first
  multi-generator case, `case14.m` for a larger topology, and `case30pwl.m`
  when PWL cost is supported.
- Include status, fixed taps/shifts, branch ratings, generator bounds, and
  fixed-angle behavior rather than comparing only objective value.
- Separate model-assembly tolerances from solver tolerances so formulation
  errors cannot be dismissed as numerical noise.

**Exit gate**

- Supported cases agree with the selected MATPOWER/PYPOWER DC formulation
  within documented tolerances; unsupported features stop before solve.

### 11. Prove the external-solver and AC-extension seams

**Implementation**

- Add a mock external adapter that consumes the same contract and returns a
  controlled result without adding a dependency.
- Add backend capability queries for LP, QP, nonlinear, integer, and PWL forms.
- Start the optional HiGHS backend only after the native and mock backends pass
  one conformance suite. Continue its details in
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
- the mock external backend consumes exactly the native solver's contract.

Only after this gate should an external backend or AC-OPF equations become the
primary implementation focus.
