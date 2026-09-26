# Branch Retirement Audit

## Current status (2026-09-26)

The pending findings in the earlier audits have now been resolved for
`paradae-updates`, `braid-unscheduled-events`, `braid_work_update`, and
`message_serialization`. The useful Braid and ParaDAE root handling and the
message serialization work are already in current `main`; the remaining limit
framework and one-off workaround are obsolete or superseded. These four
branches can be retired without losing behavior the project currently intends
to keep. Preserve any unrelated uncommitted worktree edits before cleanup that
could overwrite them. The detailed reviews are recorded at the end of this
document.

Initial audit date: 2026-04-27 (historical findings below are superseded by the
current status and the later audits at the end of this document.)

Branches audited:

- `origin/paradae-updates`
- `origin/braid-unscheduled-events`

Merge base for both branches against `main`:

- `339db96e1891fff3cd2acac6544fadbbaa34506d`

## Executive Summary

These branches are **not yet safe to delete if the goal is to preserve every substantive code idea they contain**.

What is already covered:

- The important XBraid/ParaDAE **unscheduled root propagation** work was partially recovered into the current working tree:
  - root-mask persistence in ParaDAE
  - `root_action` callback plumbing
  - GridDyn root trigger handoff from ParaDAE
  - Braid-side root extraction and double-step follow-through

What is not fully covered:

- A larger **limit-handling prototype** that spans `src/extraSolvers/paradae` and several `src/griddyn` classes is still only present on the old branches.
- Some old branch build-system tweaks for ParaDAE/XBraid are also still branch-only.
- `braid-unscheduled-events` contains many debug-output cleanup commits that are obsolete and should not block retirement, but it also contains a few remaining semantic commits beyond the root-handling work already recovered.

Bottom line:

- **Do not delete either branch yet** if you want a strict “no meaningful code left behind” standard.
- If you are willing to intentionally drop the old experimental limit-handling work, then most of the remaining branch content becomes archival rather than actionable.

## Scope and Structure

### `paradae-updates`

Unique commits on top of `main`:

1. `5b9c4a19` `update finding LAPACKE`
2. `89b81dde` `minor formatting fixes, add comments`
3. `00a62b26` `update Equation class`
4. `cb1099fc` `sync changes`
5. `3da244e9` `update time integrator and driver`

### `braid-unscheduled-events`

This branch contains all five `paradae-updates` commits plus a long follow-on series.

Its commit history splits cleanly into:

- **semantic Braid/ParaDAE event-handling work**
- **experimental limit-handling work**
- **debug-print churn / cleanup**

The large majority of the branch’s commit count is debug-related churn, not durable feature work.

## Findings by Branch

### `paradae-updates`

### Already preserved or intentionally superseded

- `89b81dde` is comments / formatting only.
- `cb1099fc` is effectively placeholder/comment sync work with no durable functional change.

### Partially preserved

- `00a62b26` introduced Equation-class scaffolding for:
  - root bookkeeping
  - limit bookkeeping
  - output hooks

  Current status:
  - **Preserved:** root bookkeeping needed for Braid root handoff.
  - **Not preserved:** the limit-manager/output-hook portions (`LimitManager`, `PostProcess`, `PrepareOutput`) remain branch-only.

- `3da244e9` introduced:
  - Braid-aware root handling in the ParaDAE time integrator
  - Braid driver support for root follow-through and limit checks

  Current status:
  - **Preserved:** the core root-handling portions needed for unscheduled events.
  - **Not preserved:** the old limit-check pipeline and root-strategy framework (`ROOT_STRAT`, add-root-point path, limit buffers threaded everywhere).

### Still branch-only

- `5b9c4a19` LAPACKE discovery tweak in `src/extraSolvers/CMakeLists.txt`

  Assessment:
  - This is still branch-only.
  - It may be obsolete under the current modernized build, but it has **not** been ported or consciously replaced as part of this audit.

### Retirement status for `paradae-updates`

- **Not fully retired**
- To fully retire it, we still need an explicit decision on:
  1. whether to keep or drop the old ParaDAE limit-handling framework
  2. whether the LAPACKE CMake tweak still matters in the modern build

### `braid-unscheduled-events`

#### Category 1: Already recovered in the current working tree

These are the main branch-only solver behaviors that this audit recovered:

- unscheduled-root sizing instead of the old single-root assumption
- root-mask persistence in `Equation`
- `EquationGridDyn::root_functions`
- `EquationGridDyn::root_action`
- Braid solver `getRoots()` population
- Braid double-step continuation after a root on level 0
- re-evaluation flow needed to hand GridDyn root actions back into ParaDAE/Braid state

Representative commits behind that work include:

- `e0d76ae9` `remove const from rootactionfunction`
- `01ed87a4` `re-eval root function before double step`
- `f8bc0c17` `do not ignore root if previously found`
- `44f38cf6` `Additional setState call before a time step with XBraid`

Notes:

- Some of these exact patches were not cherry-picked verbatim.
- The behavior was recovered in a modernization-friendly way into the current `src/extraSolvers/braid` and `src/extraSolvers/paradae` code.

#### Category 2: Appears obsolete or already superseded by `main`

This covers the long run of commits whose purpose was removing or muting debug output, for example:

- `f72d6271` through `284c8c6f`
- many `remove ... debugging output` commits across old GridDyn classes

Assessment:

- These commits should **not** block branch retirement.
- Current `main` has already moved far enough that those patches are not useful as a retirement blocker.

Also likely already present independently in modern `main`:

- the general “set state before root-trigger handling” behavior in dynamic solves
- root-trigger method signatures that already include `time`

#### Category 3: Still branch-only and substantively unreviewed

This is the main reason the branch is **not yet safe to delete** under a strict preservation standard.

### 1. Old experimental limit-handling framework

Representative commits:

- `d9d122da` `override base class implementation of limitTest`
- `023197ee` `update limit trigger functions`
- `5b05f3b7` `fixing limit checking functions`
- `bf8fbeb7` `add time input to limitTrigger`
- `6e2d4820` `add derivative call in limitTrigger`
- `ce2dad42` `relocate check limits`
- `f0652ed7` `swap order of limit checks`
- `d39b8e8f` `only limit ustop on output on level 0`

Affected areas include:

- `src/extraSolvers/paradae`
- `src/griddyn/gridComponent.*`
- `src/griddyn/GridDynSimulation.h`
- `src/griddyn/primary/Area.cpp`
- `src/griddyn/primary/gridBus.cpp`
- `src/griddyn/sources/BlockSource.cpp`
- `src/griddyn/exciters/ExciterIEEEtype1.*`

Assessment:

- This framework is **not in current `main`**.
- It is also **not fully recovered** by the current working-tree patch.
- It looks experimental and tightly coupled to old ParaDAE/XBraid development.
- It needs an explicit keep/drop decision before the branch can be retired confidently.

### 2. Model-specific ExciterIEEEtype1 limiter behavior

Representative commits:

- `194c3e87` `copy updated state back to ParaDAE, adjust vrmin and vrmax`
- `9c9087d1` `use hard max or min`

Assessment:

- This code is still branch-only.
- It may matter for a specific limiter/root-handling scenario.
- It has not been ported during this audit.

### 3. A few isolated one-off hacks

Example:

- `1405ba43` `HACK to work around lastCycle update` in `src/griddyn/sources/SineSource.cpp`

Assessment:

- Still branch-only.
- Looks local and experimental.
- Should be reviewed individually rather than auto-merged.

### Retirement status for `braid-unscheduled-events`

- **Not fully retired**
- Safe to retire only after:
  1. explicitly dropping the old limit-handling prototype, or
  2. porting the parts of that prototype that are still desired

## Current Recommended Decision

### Safe conclusions now

- The branches do **not** need to be kept around for their debug-output cleanup commits.
- The branches do **still** contain unmerged semantic work, especially around the old limit-handling path.
- Therefore, deleting them right now would mean losing code that has not yet been either:
  - ported, or
  - explicitly rejected

### Recommended retirement sequence

1. Keep the branches for now.
2. Decide whether the old ParaDAE/XBraid **limit-handling** experiment is still wanted.
3. If yes, port it deliberately in a second focused pass.
4. If no, record that decision and treat those commits as intentionally retired.
5. After that, delete the branches or archive them with a tag.

## Practical Next Step

The next best follow-up is a second pass focused only on the remaining semantic commits in:

- `src/griddyn/exciters/ExciterIEEEtype1.*`
- `src/griddyn/gridComponent.*`
- `src/griddyn/GridDynSimulation.h`
- `src/griddyn/primary/Area.cpp`
- `src/griddyn/primary/gridBus.cpp`
- `src/griddyn/sources/BlockSource.cpp`
- `src/extraSolvers/CMakeLists.txt`

That pass should end with one of two outcomes for each item:

- **port**
- **intentionally drop with rationale**

## Audit: `rtd` and `helics_updates_meson`

Audit date: 2026-09-26

### `rtd`

- The branch's Read the Docs setup, contributor list, and style guidance are
  already represented by current files on `main`.
- Its XML input guide contained the remaining useful documentation intent. A
  current, example-based guide is now in `docs/xml-input.md` and linked from
  `docs/index.md`.
- The old installation, build, component, and design descriptions predate the
  current C++23/CMake project and were not copied verbatim.

### `helics_updates_meson`

- Its standard-library `std::variant` conversion, namespaced utility calls,
  JSON-reader modernization, solver API fixes, and library-loader namespace
  handling are already present in current code.
- The branch's removal of the unused custom `src/utilities/assert.h` is now
  applied to `main`.
- Its Meson build and bundled third-party dependency snapshot are obsolete for
  the current CMake-based project and were not ported.
- The branch also attempted to install GridDyn as a C++ library SDK. This is
  intentionally retired: the project no longer offers the C library, and
  future library support would require a significant redesign. Do not treat
  the old Meson packaging work as a retained requirement.

### Retirement decision

After the current `main` worktree changes are preserved in its history, these
two branches can be deleted without losing code or documentation the project
currently intends to keep. The SDK-install attempt is an explicit product
decision to retire, not an unreviewed omission. This audit does not delete the
branch refs.

## Audit: `paradae-updates`, `braid-unscheduled-events`, and `braid_work_update`

Audit date: 2026-09-26

This review supersedes the initial “not fully retired” status above. The branch
refs remain untouched.

### `paradae-updates`

- Its ParaDAE root bookkeeping and solver callbacks are represented in current
  `main` through `Equation` root masks, `EquationGridDyn` callbacks, and the
  ParaDAE time integrator.
- The branch's LAPACKE discovery change has been superseded. Current CMake uses
  `find_package(LAPACKE)` and validates LAPACKE, Braid, and MPI before enabling
  the solver sources.
- The old `LimitManager` and output-hook scaffold belongs to the retired
  experimental limit pipeline described below; no current feature depends on
  it.

### `braid-unscheduled-events`

- The useful unscheduled-event work is already in current `main`: root counts
  and masks, GridDyn root callbacks, Braid root extraction, and root-step
  follow-through. Current `main` also reevaluates roots around continuation and
  synchronizes component state before applying root actions.
- The branch's extra `setState` call was in its old `limitCheckingFunction`.
  That callback was removed with the limit pipeline. Current root evaluation
  builds `StateData` from the evaluated solver vectors and passes it to
  `rootTest`, while root actions synchronize component state before triggers
  run.
- The branch's `ExciterIEEEtype1` hard clipping is superseded by the current
  root-based upper/lower limit and release handling, including derivative
  reset and Jacobian notifications.
- The branch's `SineSource` early-return workaround disables output updates.
  Current `SineSource` retains its update path, so the workaround is not
  useful to preserve.
- The broader `limitTest`/`limitTrigger` and `LimitManager` pipeline is an old
  solver-specific prototype. Current components use the root API for model
  transitions; carrying both systems forward would duplicate behavior.
- Debug-output cleanup commits are already irrelevant to current `main`.

### `braid_work_update`

- Its Braid/ParaDAE root-action changes are represented by current `main`'s
  root callbacks and solver continuation. The same intent was merged into
  `main` in commit `f18076f3` and has since been kept in the current code.
- Its `ThirdParty/units` pointer (`3ec79a8`) is an ancestor of the current
  pointer (`22550ff`), so its dependency update is included.

### Retirement decision

No additional source changes need to be ported from these branches. The
behavior worth keeping is present in current `main`, and the remaining branch
content is obsolete, superseded, or intentionally dropped with the rationale
above. These three branch refs can be deleted safely with respect to code
preservation. This review does not delete them; preserve unrelated worktree
edits before cleanup operations that could overwrite them.

## Audit: `message_serialization`

Audit date: 2026-09-26

- The branch tip (`e898ec64`) is an ancestor of current `main`; `main` is 369
  commits ahead and the branch has no commits absent from `main`.
- Its message serialization implementation is present in the current
  communication layer: `CommMessage` retains Cereal save/load support and
  byte, vector, and data-string conversion; control and scheduler payloads
  retain Cereal serialization registration; communicator transports use the
  data-string conversion methods.
- Current code has since modernized names, file casing, and APIs around that
  behavior. No additional source changes need to be ported from this branch.

### Retirement decision

The branch can be deleted without losing its serialization work. This review
does not delete the branch ref.
