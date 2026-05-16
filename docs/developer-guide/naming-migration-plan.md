# GridDyn Naming Migration Plan

This document defines the step-by-step plan for bringing the first-party
GridDyn code base into full compliance with the naming conventions adapted from
HELICS.

This plan is intended to be executed through a series of small, reviewable pull
requests. Each phase and subsystem can be worked independently once its
dependencies are ready. The checklist structure is designed so progress can be
tracked directly in this file over time.

## Scope

Included in scope:

- `src/`
- `interfaces/`
- `test/`
- first-party code examples and supporting code in `python/`, `matlab/`,
  `scripts/`, `config/`, and relevant docs where code identifiers are part of
  the maintained project surface

Explicitly out of scope:

- `ThirdParty/`
- generated code that is not maintained directly in this repository
- imported external wrappers unless GridDyn owns the generator and chooses to
  regenerate them as part of the migration

## Target Standard

The target naming standard is defined in
[style.md](C:\Users\phlpt\Documents\griddyn\docs\developer-guide\style.md:1).

The required end state is:

- classes and types use `PascalCase`
- free functions use `camelCase`
- class methods use `camelCase`
- enum types use `PascalCase`
- enum values use `CAPITAL_SNAKE_CASE`
- local variables use `camelCase`
- function parameters use `camelCase`
- member variables use `mPascalCase`
- global variables use `gPascalCase`
- constants and macros use `CAPITAL_SNAKE_CASE`
- filenames match their primary class or type name where practical

## Guiding Rules

- Naming-only pull requests should not make behavioral changes.
- One subsystem or one tightly related type family should be handled per PR.
- Foundational type renames should not be mixed with unrelated local cleanups.
- Temporary compatibility aliases or forwarding wrappers are allowed when needed
  to stage broad API renames.
- No PR should increase the naming violation count.
- `ThirdParty` code must remain untouched.

## Migration Strategy

The migration should proceed in layers of risk:

1. Inventory and tooling
2. Low-risk local and private naming cleanup
3. Internal functions and enums
4. Leaf classes and contained public headers
5. Core public types and cross-cutting APIs
6. Filenames and include cleanup
7. Interfaces, tests, docs, and final enforcement

This sequencing is important because broad public type renames such as
`coreObject`, `gridBus`, and `gridSimulation` will cascade across a large part
of the repository. Those should happen only after lower-risk cleanup reduces the
ambient noise.

## Progress Tracking

Status values to use while updating this file:

- `[ ]` not started
- `[~]` in progress
- `[x]` complete
- `[-]` intentionally deferred or not applicable

## Phase 0: Standard Lock

Goal: freeze the exact target and keep it stable for the duration of the
migration.

- [x] Add a repo-local naming style guide aligned with HELICS
- [x] Add a first-pass naming audit document
- [ ] Confirm any intentional deviations from HELICS naming rules
- [x] Document handling policy for generated wrappers
- [ ] Document handling policy for public API compatibility aliases

## Phase 1: Inventory And Baseline

Goal: establish the measurable list of remaining work.

- [x] Create a first-party-only naming inventory report
- [x] Categorize violations by kind
  - class and type names
  - enum type names
  - enum constant names
  - free functions
  - methods
  - member variables
  - local variables and parameters
  - globals and statistics
  - filename mismatches
- [x] Categorize violations by subsystem
- [ ] Tag violations by risk
  - low risk: local/private/internal
  - medium risk: subsystem public or widely included within a subsystem
  - high risk: repository-wide or external-facing API
- [x] Save the baseline report in a reproducible form
- [x] Record the initial violation totals in this document

### Phase 1 Notes

- First-party source file count observed during initial planning: about 769
  files
- Baseline naming inventory report:
  [naming-inventory.md](C:\Users\phlpt\Documents\griddyn\docs\developer-guide\naming-inventory.md:1)
- Baseline heuristic finding total: 13,907
- Baseline findings by category:
  - `class_type`: 576
  - `enum_constant`: 6,263
  - `enum_type`: 179
  - `filename_mismatch`: 239
  - `function_like`: 6,591
  - `member_variable`: 59
- Large source areas include:
  - `src/griddyn`: about 330 files
  - `src/extraSolvers`: about 64 files
  - `src/fmi`: about 45 files
  - `src/utilities`: about 41 files
  - `src/fileInput`: about 41 files
  - `src/networking`: about 35 files
- Current largest finding buckets by subsystem include:
  - `src/griddyn`: 7,466
  - `src/extraSolvers`: 857
  - `test/componentTests`: 803
  - `src/fmi`: 642
  - `test/libraryTests`: 592
  - `src/fileInput`: 504
  - `src/optimization`: 439
  - `src/helics`: 333
  - `test/systemTests`: 314
  - `src/utilities`: 309
  - `src/networking`: 258

## Phase 2: Tooling And Non-Blocking Enforcement

Goal: make the naming work visible without blocking all development.

- [ ] Configure `clang-tidy` naming checks for first-party code paths
- [ ] Define the final `clang-tidy` naming rule set that matches the GridDyn
      style guide and HELICS-aligned target conventions
- [ ] Keep naming enforcement report-only at first
- [ ] Exclude `ThirdParty` and generated code from naming checks
- [ ] Add CI reporting for naming violations
- [ ] Prevent new naming violations from being introduced
- [ ] Document how suppressions or temporary exceptions are handled

### Phase 2 Exit Criteria

- A developer can see current naming violations in CI or a reproducible local
  report
- The team has a stable way to measure whether a PR reduces the remaining work

### Phase 2 Notes

- The static-analyzer workflow now fetches submodules for the `clang-tidy` job
  and configures the optional networking feature set so changed networking files
  are included in the compilation database during naming and warning cleanup PRs

## Phase 3: Low-Risk Naming Cleanup

Goal: reduce the majority of easy violations without destabilizing public APIs.

Types of work in this phase:

- local variables to `camelCase`
- parameters to `camelCase`
- private and protected members to `mPascalCase`
- internal helper functions to `camelCase`
- private methods to `camelCase`

### Phase 3 PR Rules

- Do not rename foundational public types in this phase
- Keep PRs subsystem-scoped
- Prefer rename-only changes
- Run targeted tests for the affected subsystem

### Phase 3 Checklist By Subsystem

- [x] `src/networking`
- [x] `src/griddyn/comms`
- [x] `src/griddyn/measurement`
- [x] `src/griddyn/relays`
- [x] `src/griddyn/blocks`
- [x] `src/utilities`
- [x] `src/optimization`
- [x] `src/runner`
- [x] `src/fileInput`
- [x] `src/plugins`
- [x] `src/formatInterpreters`
- [x] `src/fskit`
- [x] `src/fmi_export`
- [x] `src/extraModels`
- [x] `src/gridDynLoader`
- [x] `src/gridDynMain`
- [x] `src/gridDynServer`
- [x] `src/griddyn_shared`

### Phase 3 Closeout

Phase 3 is complete for the planned low-risk naming-cleanup scope.

Completed Phase 3 work covered:

- subsystem-scoped local-variable and parameter normalization
- low-risk private/internal helper cleanup
- targeted `clang-tidy` cleanup needed to keep touched files moving forward
- completion of the remaining `src/fileInput` and `src/plugins` Phase 3 batches

Phase 4 can now proceed from a cleaner baseline with the remaining work focused
on internal enums, internal types, and other contained type-level renames.

### Initial Candidate Examples

These are examples of the kinds of renames expected early in the program:

- `make_block`
- `send_var`
- `get_devices`
- `enable_updates`
- `should_log`
- `log_to`

These examples are not an exhaustive task list and should be validated before
each PR.

## Phase 4: Internal Enums And Internal Types

Goal: bring subsystem-internal types into compliance before touching the largest
cross-cutting APIs.

Types of work in this phase:

- internal enum types to `PascalCase`
- enum constants to `CAPITAL_SNAKE_CASE`
- leaf classes to `PascalCase`
- contained public types with limited include impact

### Phase 4 Checklist By Subsystem

- [x] `src/networking`
- [x] `src/fmi`
- [x] `src/fmi_export`
- [x] `src/optimization`
- [x] `src/griddyn/measurement`
- [x] `src/griddyn/relays`
- [x] `src/griddyn/blocks`
- [x] `src/fileInput`
- [x] `src/helics`
- [x] `src/utilities`

### Examples Of Known Non-Compliant Enum Patterns

- `solver_print_level`
- `contingency_mode_t`
- `recovery_return_codes`
- `dist_type_t`
- `satType_t`

These examples illustrate the kind of work expected, but the exact rename order
should follow subsystem boundaries and dependency analysis.

### Phase 4 Closeout

Phase 4 is complete for the planned internal enum and contained-type cleanup
scope.

Completed Phase 4 work covered:

- subsystem-scoped internal enum normalization
- contained helper and leaf-type PascalCase renames
- limited public-type cleanup where include fallout stayed subsystem-bounded
- associated build and `clang-tidy` cleanup needed to land the renames safely

## Phase 5: Subsystem Public APIs

Goal: clean up public names that are not yet foundational across the entire
repository.

Types of work in this phase:

- public class renames in contained subsystems
- public method renames
- public enum renames
- short-lived source compatibility aliases where needed

### Phase 5 Checklist By Subsystem

- [ ] `src/networking`
- [ ] `src/fmi`
- [ ] `src/fmi_export`
- [ ] `src/optimization`
- [ ] `src/helics`
- [ ] `src/fileInput`
- [ ] `src/coupling`
- [ ] `src/fskit`

### Phase 5 Exit Criteria

- The majority of contained subsystem APIs are compliant
- Compatibility wrappers exist only where they are intentionally preserving
  staged migration

## Phase 6: Foundational Type Families

Goal: complete the most invasive renames in carefully scoped, dedicated PR
series.

This phase should be split into separate epics. Each epic may take multiple PRs.

### Epic A: Core Object Family

- [ ] Audit all references to `coreObject`
- [ ] Define compatibility strategy
- [ ] Rename `coreObject` and related core object family identifiers
- [ ] Update dependent headers and implementations
- [ ] Update tests
- [ ] Remove temporary compatibility shims when safe

### Epic B: Grid Object Hierarchy

- [ ] Audit all references to `gridComponent`
- [ ] Audit all references to `gridSubModel`
- [ ] Audit all references to `helperObject`
- [ ] Rename type families to compliant names
- [ ] Update dependents
- [ ] Update tests
- [ ] Remove temporary compatibility shims when safe

### Epic C: Topology And Network Model Types

- [ ] Audit all references to `gridBus`
- [ ] Audit all references to `acBus`
- [ ] Audit all references to `dcBus`
- [ ] Audit all references to `Area`
- [ ] Audit all references to `Link`
- [ ] Rename type families to compliant names
- [ ] Update dependents
- [ ] Update tests
- [ ] Remove temporary compatibility shims when safe

### Epic D: Simulation Types

- [ ] Audit all references to `gridSimulation`
- [ ] Audit all references to `gridDynSimulation`
- [ ] Rename simulation classes and closely related API surface
- [ ] Update dependents
- [ ] Update tests
- [ ] Remove temporary compatibility shims when safe

### Epic E: Solver Types

- [ ] Audit solver interface families
- [ ] Rename non-compliant solver types and enums
- [ ] Update dependents
- [ ] Update tests
- [ ] Remove temporary compatibility shims when safe

### Epic F: Communication Core Types

- [ ] Audit all references to `commMessage`
- [ ] Audit all references to related communication core types
- [ ] Rename types and API surface
- [ ] Update dependents
- [ ] Update tests
- [ ] Remove temporary compatibility shims when safe

## Phase 7: Filenames And Include Paths

Goal: align filenames with the final compliant type names.

This phase should usually follow the relevant type renames so filenames only
move once.

- [ ] Rename filenames for core types
- [ ] Rename filenames for subsystem types
- [ ] Update include directives
- [ ] Update build system references
- [ ] Update code generators or wrapper configs where applicable
- [ ] Verify case-sensitive path correctness for all platforms

### Known Filename Mismatch Examples

- `gridSimulation.h` with `gridSimulation`
- `coreObject.h` with `coreObject`
- `commMessage.h` with `commMessage`

## Phase 8: Interfaces, Tests, Docs, And User Surface

Goal: ensure the entire first-party repository surface reflects the compliant
names.

- [ ] Update first-party maintained interface code in `interfaces/`
- [ ] Update tests in `test/`
- [ ] Update first-party support code in `python/`, `matlab/`, and `scripts/`
- [ ] Update docs and examples
- [ ] Update Doxygen comments to match renamed identifiers
- [ ] Normalize user-facing option names to one canonical spelling where naming
      drift exists

## Phase 9: Final Enforcement

Goal: make compliance durable.

- [ ] Remove temporary naming suppressions or allowlists
- [ ] Update `.clang-tidy` so the final naming convention settings match the
      migrated GridDyn code base
- [ ] Make naming checks blocking in CI for first-party code
- [ ] Verify zero remaining non-compliant first-party identifiers
- [ ] Record final completion summary in this document

## Subsystem Tracker

Use this section to track PR-by-PR progress at a higher level.

### Core Source Areas

- [ ] `src/core`
- [ ] `src/coupling`
- [ ] `src/extraModels`
- [ ] `src/extraSolvers`
- [ ] `src/fileInput`
- [ ] `src/fmi`
- [ ] `src/fmi_export`
- [ ] `src/formatInterpreters`
- [ ] `src/fskit`
- [ ] `src/griddyn`
- [ ] `src/gridDynLoader`
- [ ] `src/gridDynMain`
- [ ] `src/gridDynServer`
- [ ] `src/griddyn_shared`
- [ ] `src/helics`
- [ ] `src/networking`
- [ ] `src/optimization`
- [ ] `src/plugins`
- [ ] `src/runner`
- [ ] `src/utilities`

### Interface And Test Areas

- [ ] `interfaces/java`
- [ ] `interfaces/matlab`
- [ ] `interfaces/octave`
- [ ] `interfaces/python`
- [ ] `interfaces/test`
- [ ] `test/componentTests`
- [ ] `test/extraTests`
- [ ] `test/libraryTests`
- [ ] `test/systemTests`
- [ ] `test/testSharedLibrary`

## PR Log

Use this table to log each naming migration PR as it lands.

| PR / Branch | Area                                                                                                                    | Phase   | Summary                                                                                                                                                                                                         | Compatibility Needed | Tests Run                        | Status   |
| ----------- | ----------------------------------------------------------------------------------------------------------------------- | ------- | --------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------- | -------------------- | -------------------------------- | -------- |
| merged      | `src/utilities` + `src/griddyn/blocks` + `src/griddyn/links` + `src/griddyn/sources` + `test/libraryTests`              | Phase 4 | Internal enum/type cleanup covering function-interpreter and sparse-ordering renames, block and source enum normalization, selected link-side control enum fallout, and the associated `clang-tidy` cleanup     | No                   | CI compile and `clang-tidy` run  | Complete |
| merged      | `src/networking`                                                                                                        | Phase 3 | DIME client naming cleanup and baseline inventory tooling                                                                                                                                                       | No                   | Inventory script run             | Complete |
| merged      | `src/utilities` + `src/griddyn/blocks` + `src/griddyn/relays` + `src/griddyn/sources` + `src/extraModels`               | Phase 4 | Internal enum/type cleanup covering `DistributionType`, `SaturationType`, relay and block enum normalization, source-side communication enum fallout, and the dependent `txThermalModel` output-mode update     | No                   | CI compile and `clang-tidy` run  | Complete |
| merged      | `src/networking` + `src/griddyn/measurement`                                                                            | Phase 4 | Internal enum/type cleanup covering networking loop/reactor/socket operation enums plus measurement comparison, compound-condition, and Jacobian-mode enums                                                     | No                   | CI compile and `clang-tidy` run  | Complete |
| merged      | `src/fileInput`                                                                                                         | Phase 4 | Internal enum/type cleanup covering reader match/config enums, file-input reader flags, and contained CSV/RAW section-mode enums                                                                                | No                   | CI compile and `clang-tidy` run  | Complete |
| merged      | `src/griddyn/simulation` + `src/griddyn/solvers` + `src/extraSolvers/braid`                                             | Phase 4 | Internal enum/type cleanup covering solver print, step, sparse-reinit, contingency, network-check, and recovery enums, plus the associated simulation-side `clang-tidy` cleanup                                 | No                   | CI compile and `clang-tidy` run  | Complete |
| merged      | `src/griddyn/comms`                                                                                                     | Phase 3 | Low-risk comms cleanup in `Communicator`, `communicationsCore`, and related communicator fixes                                                                                                                  | No                   | Not yet run                      | Complete |
| merged      | `src/griddyn/measurement`                                                                                               | Phase 3 | Low-risk measurement member and parameter cleanup, plus collector and grabber warning fixes                                                                                                                     | No                   | CI `clang-tidy` run              | Complete |
| merged      | CI tooling                                                                                                              | Phase 2 | Updated `clang-tidy` workflow checkout and configure flags so optional networking files are analyzed                                                                                                            | No                   | CI workflow run                  | Complete |
| merged      | `src/utilities`                                                                                                         | Phase 3 | Low-risk utilities cleanup batches covering member naming in `valuePredictor`, `gridRandom`, and `dataDictionary`, plus local helper cleanup in `zipUtilities` and `GlobalWorkQueue`                            | No                   | No `clang-tidy` issues to report | Complete |
| merged      | `src/griddyn/relays`                                                                                                    | Phase 3 | First low-risk relay cleanup batch covering member naming across `breaker`, `busRelay`, `differentialRelay`, `loadRelay`, `pmu`, and `zonalRelay`                                                               | No                   | CI `clang-tidy` run              | Complete |
| merged      | `src/griddyn/blocks`                                                                                                    | Phase 3 | Low-risk block cleanup batch covering member naming and local variable normalization in `controlBlock`, `deadbandBlock`, `delayBlock`, `derivativeBlock`, `filteredDerivativeBlock`, and `functionBlock`        | No                   | CI `clang-tidy` run              | Complete |
| merged      | `src/optimization`                                                                                                      | Phase 3 | Low-risk optimization cleanup batch covering member naming and local variable normalization in `gridDynOpt`, `gridOptObjects`, `optHelperClasses`, `optimizerInterface`, and `optObjectFactory`                 | No                   | Not yet run                      | Complete |
| merged      | `src/optimization`                                                                                                      | Phase 4 | Internal enum/type cleanup covering optimization helper-type PascalCase renames, contained optimization object/model renames, factory string-view/span updates, and the associated `clang-tidy` cleanup         | No                   | CI compile and `clang-tidy` run  | Complete |
| merged      | `src/fmi_export`                                                                                                        | Phase 4 | Internal enum/type cleanup covering FMI export collector/coordinator/event/runner/builder PascalCase renames, supporting alias cleanup, and the associated build and `clang-tidy` fixes                         | No                   | CI compile and `clang-tidy` run  | Complete |
| merged      | `src/fmi`                                                                                                               | Phase 4 | Internal enum/type cleanup covering FMI import/library PascalCase renames, remaining FMU wrapper and submodel type-family renames, plugin/test fallout cleanup, and the associated build and `clang-tidy` fixes | No                   | CI compile and `clang-tidy` run  | Complete |
| merged      | `src/formatInterpreters`                                                                                                | Phase 3 | Low-risk formatter cleanup batch covering reader-wrapper state and local naming across JSON, YAML, XML, INI, and TOML element/reader adapters                                                                   | No                   | CI compile and `clang-tidy` run  | Complete |
| merged      | `src/fskit`                                                                                                             | Phase 3 | Low-risk FSKIT cleanup batch covering simulator, scheduler, runner, communicator, and protection/process wrapper naming                                                                                         | No                   | Not yet run                      | Complete |
| merged      | `src/fmi_export`                                                                                                        | Phase 3 | Low-risk FMI export cleanup batch covering collector, coordinator, event, runner, builder, and export-loader naming                                                                                             | No                   | CI compile and `clang-tidy` run  | Complete |
| merged      | `src/runner` + `src/extraModels` + `src/gridDynLoader` + `src/gridDynMain` + `src/gridDynServer` + `src/griddyn_shared` | Phase 3 | Low-risk runtime/bootstrap cleanup batch covering runner state, transformer helper models, loader/main entrypoints, PMU server state, and shared-library wrapper local naming                                   | No                   | CI compile and `clang-tidy` run  | Complete |
| merged      | `src/fileInput`                                                                                                         | Phase 3 | Low-risk reader cleanup batch covering file-input reader helpers and element loaders with local naming normalization across the area/bus/link/relay/simulation/econ/event/collector reader path                 | No                   | CI compile and `clang-tidy` run  | Complete |
| merged      | `src/plugins`                                                                                                           | Phase 3 | Low-risk plugin cleanup batch completing the remaining contained Phase 3 naming work in the plugin entrypoints and support code                                                                                 | No                   | CI compile and `clang-tidy` run  | Complete |

## Open Decisions

These decisions should be resolved before the high-risk phases begin.

- [ ] Whether temporary type aliases are acceptable for major public type
      renames
- [ ] Whether filename renames should happen with type renames or in follow-up
      PRs
- [ ] Whether generated wrapper code will be renamed directly or regenerated
- [ ] Whether some legacy externally visible names must remain for compatibility

## Exit Criteria For Full Compliance

The migration is complete when all of the following are true:

- every first-party maintained identifier matches the project naming standard,
  unless explicitly documented as a permanent exception
- every first-party maintained filename is aligned with its primary type where
  practical
- temporary compatibility shims introduced for the migration have been removed,
  unless explicitly retained as long-term compatibility surface
- tests, interfaces, examples, and docs all reflect the final names
- `.clang-tidy` naming settings match the final enforced GridDyn naming
  conventions
- naming checks are enforced in CI for first-party code

## Suggested First PR Series

If a starting sequence is needed, use this order:

1. Naming inventory and report-only tooling
2. `src/networking` low-risk cleanup
3. `src/griddyn/comms` low-risk cleanup
4. `src/griddyn/measurement` low-risk cleanup
5. `src/utilities` low-risk cleanup
6. `src/griddyn/relays` low-risk cleanup
7. `src/griddyn/blocks` low-risk cleanup
8. internal enums in contained subsystems
9. contained public APIs
10. foundational type family epics
