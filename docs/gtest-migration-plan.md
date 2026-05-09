# GoogleTest Migration Plan

This document tracks the active plan for converting GridDyn's test suite from
Boost.Test to GoogleTest. It started as an initial action plan and is now also
the running migration log for the `testLibrary` work.

## Summary

GridDyn already contains reusable GoogleTest CMake support, but the primary
test tree is still organized around Boost.Test mega-binaries and Boost-specific
test filtering. The migration should be done in phases so that build-system
modernization and compile fixes happen before broad source conversion.

## Current status

The build-system groundwork for GoogleTest is now in place.

- `config/cmake/AddGoogletest.cmake` and `config/cmake/AddGooglebenchmark.cmake`
  build from local submodules using the same layout used in HELICS.
- A dedicated `LibraryTests` GoogleTest executable exists alongside the legacy
  Boost executables.
- A dedicated `FileReaderTests` GoogleTest executable now exists for
  reader/configuration-oriented tests.
- Legacy Boost executables remain grouped under the Visual Studio folder
  `tests_boost`.
- The new GoogleTest executable is grouped under the Visual Studio folder
  `tests`.
- A separate GoogleTest helper layer exists in:
  `test/gtestHelper.h` and `test/gtestHelperFunctions.cpp`.

The first migrated `testLibrary` files are:

1. `test/libraryTests/testCore.cpp`
2. `test/libraryTests/testGridParameter.cpp`

The legacy Boost `testLibrary` executable remains intact for all files that
have not yet been moved.

## Phase 1: Baseline and prerequisite fixes

1. Get a clean configure/build/test baseline on the current branch and record
   existing failures.
2. Modernize Boost discovery for the libraries GridDyn still needs outside the
   test framework.
3. Decouple test linkage from `Boostlibs::test` so the test framework can be
   swapped without destabilizing the rest of the build.
4. Fix any compile blockers exposed by newer compilers or newer CMake while the
   project is still on Boost.Test.

## Phase 2: Introduce GoogleTest infrastructure

1. Reuse `config/cmake/AddGoogletest.cmake` as the integration point.
2. Update it toward the HELICS structure where helpful: shared setup,
   `gtest_discover_tests`, and standard GoogleTest target linkage.
3. Add a new GoogleTest-oriented test base target while preserving the current
   compile definitions for test file locations and executable locations.
4. Replace the current Boost global fixture usage with a GoogleTest global
   environment hook.

## Phase 3: Reshape test executables carefully

1. Keep the existing top-level executables at first: `testComponents`,
   `testLibrary`, `testSystem`, `testExtra`.
2. Convert the Boost-owned `main` files for those executables into GoogleTest
   bootstrap files.
3. Move shared test setup into common fixture and environment sources.
4. Split the executables further only if the migration exposes clear pain
   points or CI/runtime benefits.
5. Convert the shared-library tests as a separate sub-step because they are
   wired slightly differently today.

### Current decision on executable structure

Migration work started in a single GoogleTest executable, `LibraryTests`, but
the first focused split has now happened:

- `LibraryTests` for core/value-oriented library tests
- `FileReaderTests` for reader/configuration-oriented tests

The likely future grouping is:

- core/value-layer tests
- reader/configuration tests
- runner/integration-oriented library tests

## Phase 4: Source conversion order

1. Convert helper infrastructure first:
   `test/testHelper.h` and `test/testHelperFunctions.cpp`.
2. Convert simpler suites that mostly use `BOOST_AUTO_TEST_CASE` and
   straightforward assertions.
3. Convert fixture-heavy suites using `BOOST_FIXTURE_TEST_SUITE`.
4. Convert Boost data-driven tests later using `TEST_P`,
   `INSTANTIATE_TEST_SUITE_P`, or explicit loops where appropriate.
5. Convert `src/fmi_export/testFMIExport.cpp` after the main `test/` tree is in
   good shape.

## Assertion and fixture mapping

- `BOOST_AUTO_TEST_CASE` -> `TEST`
- `BOOST_FIXTURE_TEST_SUITE` -> `class ... : public ::testing::Test`
- `BOOST_CHECK_EQUAL` -> `EXPECT_EQ`
- `BOOST_REQUIRE_EQUAL` -> `ASSERT_EQ`
- `BOOST_CHECK_CLOSE` / `BOOST_CHECK_SMALL` -> `EXPECT_NEAR`
- `BOOST_CHECK_MESSAGE` / `BOOST_REQUIRE_MESSAGE` -> GoogleTest assertions with
  stream output

## Quick-test replacement

GridDyn currently uses Boost labels and `--run_test=@quick`. GoogleTest does
not have a direct label equivalent at runtime, so the migration should replace
this with a deliberate naming and filtering convention based on
`--gtest_filter`, similar to HELICS CI slices.

## Recommended implementation order

1. Build baseline and modernize Boost discovery.
2. Add GoogleTest infrastructure in parallel with Boost.Test.
3. Convert `testLibrary` first.
4. Validate CTest registration and quick-test replacement.
5. Convert `testComponents`.
6. Convert `testSystem`.
7. Convert `testExtra`.
8. Convert shared-library tests.
9. Convert FMI export tests.
10. Remove Boost.Test entirely.

## Active `testLibrary` migration order

The `testLibrary` migration is intentionally being done one file at a time so
that failures are easy to isolate and helper-layer gaps are obvious.

Completed:

1. `test/libraryTests/testCore.cpp`
2. `test/libraryTests/testGridParameter.cpp`
3. `test/libraryTests/testReaderInfo.cpp` -> `FileReaderTests`
4. `test/libraryTests/testMatrixData.cpp`
5. `test/libraryTests/testZipUtilities.cpp`
6. `test/libraryTests/testOperatingBounds.cpp`
7. `test/libraryTests/testValuePredictors.cpp`

Checkpoint after those six files:

1. Reevaluate whether additional focused executables are warranted beyond
   `LibraryTests` and `FileReaderTests`.
2. Decide whether to split off a runner/integration-oriented test executable.
3. If we do split further, create additional test entry files in
   `test/libraryTests/` for those new executables at that time rather than
   trying to predict the final structure too early.
4. Then continue with the remaining `testLibrary` files:
   `testXML.cpp`, `testJsonReader.cpp`, `testElementReaders.cpp`, and
   `testGridDynRunner.cpp`.

## Notes from early migration

- A stricter clone-type check in `testCore.cpp` exposed that `Link::clone()`
  with a null destination may produce a more specific object type through the
  registered factory path. The migrated test now checks exact dynamic type by
  cloning into a pre-created object of the same factory type.
- This is a good example of why the one-file-at-a-time approach is valuable:
  the migration can expose questionable legacy test assumptions without forcing
  unrelated files to move at the same time.

## Main risks

- Boost data tests will require manual conversion.
- Global fixture behavior must be preserved carefully.
- The current mega-binary structure may hide test-order coupling.
- Newer Boost/CMake support may surface unrelated build issues.
- Quick-test semantics need an intentional replacement to keep CI practical.

## `testComponents` migration scan

The `testComponents` suite currently contains these source files:

- `test/componentTests/ConditionTests.cpp`
- `test/componentTests/simulationTests.cpp`
- `test/componentTests/testAdjustableTX.cpp`
- `test/componentTests/testArea.cpp`
- `test/componentTests/testBlocks.cpp`
- `test/componentTests/testEvents.cpp`
- `test/componentTests/testExciters.cpp`
- `test/componentTests/testExtraModels.cpp`
- `test/componentTests/testFMI.cpp`
- `test/componentTests/testGenerators.cpp`
- `test/componentTests/testGenModels.cpp`
- `test/componentTests/testGovernors.cpp`
- `test/componentTests/testGridLab.cpp`
- `test/componentTests/testLinks.cpp`
- `test/componentTests/testLoads.cpp`
- `test/componentTests/testRecorders.cpp`
- `test/componentTests/testRelays.cpp`
- `test/componentTests/testSource.cpp`
- `test/componentTests/testdcLinks.cpp`
- `test/componentTests/testComponents.cpp` (legacy Boost bootstrap)

The heaviest files are:

1. `testLoads.cpp`
2. `testRecorders.cpp`
3. `testExciters.cpp`
4. `testFMI.cpp`
5. `testBlocks.cpp`

Almost all files use `testHelper.h` fixtures, primarily
`gridDynSimulationTestFixture` or `gridLoadTestFixture`, so the existing
GoogleTest helper layer should transfer well.

## Recommended `testComponents` executable split

Instead of replacing `testComponents` with one giant GoogleTest executable, the
component suite should be split into focused executables.

Recommended grouping:

### `GeneratorComponentTests`

Dynamic model and control behavior:

- `test/componentTests/testExciters.cpp`
- `test/componentTests/testGovernors.cpp`
- `test/componentTests/testGenModels.cpp`
- `test/componentTests/testGenerators.cpp`
- `test/componentTests/testBlocks.cpp`

Why this grouping:

- These files mostly exercise component-level dynamic equations and controls.
- They share similar fixture patterns and solver/jacobian expectations.
- They are likely to benefit from staying together when debugging dynamic-model
  regressions.

### `NetworkComponentTests`

Grid connectivity and equipment behavior:

- `test/componentTests/testLoads.cpp`
- `test/componentTests/testLinks.cpp`
- `test/componentTests/testdcLinks.cpp`
- `test/componentTests/testAdjustableTX.cpp`

Why this grouping:

- These tests focus on buses, links, relays, transformers, and load behavior in
  network context.
- They are more electrically/topologically coupled than the pure model tests.

### `EventComponentTests`

Component infrastructure, orchestration, and observer-style behavior:

- `test/componentTests/ConditionTests.cpp`
- `test/componentTests/testEvents.cpp`
- `test/componentTests/testRelays.cpp`
- `test/componentTests/testSource.cpp`
- `test/componentTests/testRecorders.cpp`

Why this grouping:

- These files test conditions, events, source-driven behavior, relays, and
  recorder/observer-style infrastructure.
- They are more about infrastructure around components than the components'
  internal model equations.

### `SystemComponentTests`

System assembly and simulation behavior:

- `test/componentTests/testArea.cpp`
- `test/componentTests/simulationTests.cpp`

Why this grouping:

- These files are more about assembled system structure and simulation behavior
  than about individual component models.
- They form a small, coherent group that is useful to keep separate from the
  event/observer-style tests.

### `OptionalComponentTests`

Optional or external-integration-heavy features:

- `test/componentTests/testExtraModels.cpp`
- `test/componentTests/testGridLab.cpp`
- `test/componentTests/testFMI.cpp`

Why this grouping:

- These are the most environment-sensitive or feature-gated tests.
- `testFMI.cpp` is already conditionally meaningful.
- `testGridLab.cpp` is already gated by `GRIDDYN_ENABLE_MPI`.
- Keeping optional integrations together makes it easier to manage feature
  flags and longer-running test behavior.

## Recommended `testComponents` migration order

For `testComponents`, migration order should follow object complexity more than
raw file size. The best sequence is to start with smaller building-block
components, then move upward toward more coupled system behavior and optional
integrations.

Suggested order:

1. `test/componentTests/testLoads.cpp`
2. `test/componentTests/testSource.cpp`
3. `test/componentTests/testBlocks.cpp`
4. `test/componentTests/testGovernors.cpp`
5. `test/componentTests/testGenerators.cpp`
6. `test/componentTests/testGenModels.cpp`
7. `test/componentTests/testExciters.cpp`
8. `test/componentTests/testdcLinks.cpp`
9. `test/componentTests/testLinks.cpp`
10. `test/componentTests/testAdjustableTX.cpp`
11. `test/componentTests/testRelays.cpp`
12. `test/componentTests/ConditionTests.cpp`
13. `test/componentTests/testEvents.cpp`
14. `test/componentTests/testRecorders.cpp`
15. `test/componentTests/testArea.cpp`
16. `test/componentTests/simulationTests.cpp`
17. `test/componentTests/testExtraModels.cpp`
18. `test/componentTests/testGridLab.cpp`
19. `test/componentTests/testFMI.cpp`

## Recommended first step for `testComponents`

Create the new GoogleTest executable `NetworkComponentTests` first and start by
moving:

1. `test/componentTests/testLoads.cpp`

Then continue within the building-block direction:

2. `test/componentTests/testSource.cpp`
3. `test/componentTests/testBlocks.cpp`

This gives a better staged rollout for the component suite:

- the first migrated files exercise lower-level component behavior
- failures are more likely to indicate local model/fixture issues
- higher-level orchestration tests can move later, after the component-side
  GoogleTest helper layer is already proven

## Current `testComponents` migration status

- `NetworkComponentTests`
  - `test/componentTests/testLoads.cpp`
  - `test/componentTests/testdcLinks.cpp`
  - `test/componentTests/testLinks.cpp`
  - `test/componentTests/testAdjustableTX.cpp`

- `GeneratorComponentTests`
  - `test/componentTests/testBlocks.cpp`
  - `test/componentTests/testGovernors.cpp`
  - `test/componentTests/testGenerators.cpp`
  - `test/componentTests/testGenModels.cpp`
  - `test/componentTests/testExciters.cpp`

- `EventComponentTests`
  - `test/componentTests/testSource.cpp`
  - `test/componentTests/testRelays.cpp`
  - `test/componentTests/ConditionTests.cpp`
  - `test/componentTests/testEvents.cpp`
  - `test/componentTests/testRecorders.cpp`

- `SystemComponentTests`
  - `test/componentTests/testArea.cpp`
  - `test/componentTests/simulationTests.cpp`

- `OptionalComponentTests`
  - `test/componentTests/testExtraModels.cpp`
  - `test/componentTests/testGridLab.cpp`
  - `test/componentTests/testFMI.cpp`

## `testSystem` migration scan

The `testSystem` suite currently contains these source files:

- `test/systemTests/faultTests.cpp`
- `test/systemTests/testCloning.cpp`
- `test/systemTests/testConstraints.cpp`
- `test/systemTests/testContingency.cpp`
- `test/systemTests/testDyn1.cpp`
- `test/systemTests/testDyn2.cpp`
- `test/systemTests/testGridDynRunner.cpp`
- `test/systemTests/testInputs.cpp`
- `test/systemTests/testMainExe.cpp`
- `test/systemTests/testOutputs.cpp`
- `test/systemTests/testpFlow.cpp`
- `test/systemTests/testRoots.cpp`
- `test/systemTests/testSolverModes.cpp`
- `test/systemTests/testSystem.cpp` (legacy Boost bootstrap)
- `test/systemTests/validationTests.cpp`

Notable structure observations:

- `testMainExe.cpp` is the only clearly active system test that currently
  requires `exeTestHelper`.
- `testGridDynRunner.cpp` still includes `exeTestHelper`, but its primary case
  is currently disabled behind `#ifdef ENABLE_THIS_CASE`.
- `testInputs.cpp`, `testpFlow.cpp`, and `validationTests.cpp` are the heaviest
  files and should move later in the migration.
- `testInputs.cpp`, `testpFlow.cpp`, and `validationTests.cpp` also contain
  Boost data-driven tests that will need either parameterized GoogleTests or
  explicit indexed loops.

## Recommended `testSystem` executable split

Instead of replacing `testSystem` with one giant GoogleTest executable, the
system suite should be split into focused executables.

### `PowerflowSystemTests`

Powerflow and steady-state solve behavior:

- `test/systemTests/testConstraints.cpp`
- `test/systemTests/testpFlow.cpp`
- `test/systemTests/validationTests.cpp`

### `DynamicSystemTests`

Dynamic simulation and fault/root behavior:

- `test/systemTests/testDyn1.cpp`
- `test/systemTests/testDyn2.cpp`
- `test/systemTests/testRoots.cpp`
- `test/systemTests/faultTests.cpp`
- `test/systemTests/testSolverModes.cpp`

### `WorkflowSystemTests`

System assembly, cloning, inputs, outputs, and contingencies:

- `test/systemTests/testCloning.cpp`
- `test/systemTests/testContingency.cpp`
- `test/systemTests/testInputs.cpp`
- `test/systemTests/testOutputs.cpp`

### `ExecutableSystemTests`

External executable and runner-facing behavior:

- `test/systemTests/testMainExe.cpp`
- `test/systemTests/testGridDynRunner.cpp`

## Recommended `testSystem` migration order

The migration order should start with the smaller building-block system tests
and delay the large data-driven and executable-facing files until later.

1. `test/systemTests/testConstraints.cpp`
2. `test/systemTests/testOutputs.cpp`
3. `test/systemTests/testContingency.cpp`
4. `test/systemTests/testCloning.cpp`
5. `test/systemTests/testRoots.cpp`
6. `test/systemTests/testSolverModes.cpp`
7. `test/systemTests/testDyn1.cpp`
8. `test/systemTests/testDyn2.cpp`
9. `test/systemTests/faultTests.cpp`
10. `test/systemTests/testpFlow.cpp`
11. `test/systemTests/testInputs.cpp`
12. `test/systemTests/validationTests.cpp`
13. `test/systemTests/testMainExe.cpp`
14. `test/systemTests/testGridDynRunner.cpp`

## Recommended first step for `testSystem`

Create `PowerflowSystemTests` first and start with
`test/systemTests/testConstraints.cpp`.

Why this is the best opening move:

- it is small and low-risk,
- it exercises the shared simulation fixture without bringing in the heavy
  powerflow and validation matrices yet,
- and it gives us a clean place to accumulate the powerflow-oriented files as
  the migration proceeds.
