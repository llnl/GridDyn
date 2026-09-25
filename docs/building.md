# Building GridDyn

GridDyn uses CMake 3.28 or newer and requires a 64-bit C++23 toolchain. The
minimum compiler versions checked by CMake are GCC 14 and Clang 18. On Windows,
use a Visual Studio toolset with C++23 support. The project CI also builds with
Visual Studio; the exact generator depends on the installed Visual Studio
version.

The repository tracks SUNDIALS as a submodule. Initialize submodules when
cloning, and install the platform development packages for Boost and
SuiteSparse/KLU when they are not available through the build environment.
The continuous integration configuration is another reference for the
currently exercised Linux dependencies and options.

## Configure and build on Windows

From a Visual Studio developer command prompt or a shell where CMake can find
the Visual Studio installation:

    cmake -S . -B build -G "Visual Studio 17 2022" -A x64 -DGRIDDYN_ENABLE_FMI=OFF -DGRIDDYN_BUILD_TESTS=OFF
    cmake --build build --config Release --parallel 4

Select the generator that matches the Visual Studio version installed on your
system. CMake may also configure the project through Visual Studio's CMake
workflow.

## Configure and build on Linux

With CMake, Ninja, a supported compiler, Boost, and SuiteSparse/KLU installed:

    cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release -DGRIDDYN_ENABLE_FMI=OFF -DGRIDDYN_BUILD_TESTS=OFF
    cmake --build build --parallel

Optional features such as FMI, HELICS, networking, optimization, and extra
solvers are controlled by `GRIDDYN_*` CMake options. Check the top-level
CMakeLists.txt and the relevant integration documentation before enabling an
optional component; available settings can change as integrations evolve.

## Run the C++ test suite

For a test-enabled build, configure with `-DBUILD_TESTING=ON` and
`-DGRIDDYN_BUILD_TESTS=ON`, build, then run the registered continuous tests:

    ctest --test-dir build --output-on-failure -L Continuous

The CI workflow is the reference for compiler-specific flags and the test
labels used in automation.

## Build the Python package from source

The published package requires Python 3.13 or newer. From the repository root,
install the Python build frontend and build a wheel:

    python -m pip install build
    python -m build --wheel

The C++ compiler, CMake, Boost, and SuiteSparse/KLU are still required for a
source wheel build. PyPI wheels avoid this local compilation when a compatible
wheel is available. The [Python installation guide](installation.md) covers
the normal user path.

## Generate the C++ API reference

The Doxygen target produces HTML. Set an output directory outside the source
tree, configure with Doxygen enabled, then build the `doc` target. For example,
from PowerShell:

    $doxygenOutput = Join-Path (Get-Location) "build\doxygen-html"
    cmake -S . -B build -DGRIDDYN_ENABLE_DOXYGEN=ON "-DDOXYGEN_OUTPUT_DIR=$doxygenOutput"
    cmake --build build --config Release --target doc --parallel 4

Open `build\doxygen-html\html\index.html` after the build. Doxygen warnings
remain enabled and are written to `build\doxygen_warnings.log` so the complete
audit can be reviewed without filling the build console. The warning inventory
and planned cleanup are tracked in the
[documentation release plan](documentation-release-plan.md).

Read the Docs builds the Sphinx guides from `docs/`; it does not currently
publish the generated Doxygen HTML. Keeping user guides in Sphinx and the C++
API in Doxygen avoids adding a bridge or extra documentation toolchain before
the API scope and warning backlog have been reviewed.
