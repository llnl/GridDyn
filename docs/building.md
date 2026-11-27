# Building GridDyn

GridDyn uses CMake 3.28 or newer and requires a 64-bit C++23 toolchain. The
minimum compiler versions checked by CMake are GCC 14 and Clang 18. On Windows,
use a Visual Studio toolset with C++23 support. The project CI also builds with
Visual Studio; the exact generator depends on the installed Visual Studio
version.

The repository tracks SUNDIALS and HDF5 as submodules. Initialize submodules
when cloning and install the platform development package for Boost.
The default sparse solver configuration uses SuiteSparse/KLU. CMake can use an
installed copy or build the bundled copy with
`-DGRIDDYN_SUITESPARSE_FORCE_SUBPROJECT=ON`.
The continuous integration configuration is another reference for the
currently exercised Linux dependencies and options.

## Configure and build on Windows

From a Visual Studio developer command prompt or a shell where CMake can find
the Visual Studio installation:

```powershell
cmake -S . -B build -G "Visual Studio 17 2022" -A x64 -DGRIDDYN_ENABLE_FMI=OFF -DGRIDDYN_BUILD_TESTS=OFF
cmake --build build --config Release --parallel 4
```

Select the generator that matches the Visual Studio version installed on your
system. CMake may also configure the project through Visual Studio's CMake
workflow.

## Configure and build on Linux

With CMake, Ninja, a supported compiler, Boost, and SuiteSparse/KLU installed:

```bash
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release -DGRIDDYN_ENABLE_FMI=OFF -DGRIDDYN_BUILD_TESTS=OFF
cmake --build build --parallel
```

Optional features such as FMI, HELICS, networking, optimization, and extra
solvers are controlled by `GRIDDYN_*` CMake options. Check the top-level
CMakeLists.txt and the relevant integration documentation before enabling an
optional component; available settings can change as integrations evolve.

## Install and find the command-line executable

After building `gridDynMain`, install GridDyn with `cmake --install build`
(add `--config Release` for a multi-configuration generator). The installed
`GRIDDYN` CMake package exports `Griddyn::gridDynMain` as an imported executable
target. A downstream CMake project can locate it without linking to GridDyn's
internal static libraries:

```cmake
find_package(GRIDDYN 0.13 CONFIG REQUIRED)
add_custom_target(show_griddyn_version
    COMMAND $<TARGET_FILE:Griddyn::gridDynMain> --version
    VERBATIM)
```

Set `CMAKE_PREFIX_PATH` to the installation prefix if CMake does not find the
package automatically. The executable and its package files are in the
`applications` install component. The Python wheel contains the Python extension;
it does not install this command-line executable.

## Run the C++ test suite

For a test-enabled build, configure with `-DBUILD_TESTING=ON` and
`-DGRIDDYN_BUILD_TESTS=ON`, build, then run the registered continuous tests:

```bash
ctest --test-dir build --output-on-failure -L Continuous
```

The CI workflow is the reference for compiler-specific flags and the test
labels used in automation.

## Build the Python package from source

The published package requires Python 3.13 or newer. From the repository root,
install the Python build frontend and build a wheel:

```bash
python -m pip install build
python -m build --wheel
```

Source wheel builds use the C++ and CMake dependencies described above.
PyPI wheels avoid local compilation when a compatible wheel is available.
The [Python installation guide](installation.md) covers the normal user path.

## Generate the C++ API reference

The Doxygen target produces HTML. Set an output directory outside the source
tree, configure with Doxygen enabled, then build the `doc` target. For example,
from PowerShell:

```powershell
$doxygenOutput = Join-Path (Get-Location) "build\doxygen-html"
cmake -S . -B build -DGRIDDYN_ENABLE_DOXYGEN=ON "-DDOXYGEN_OUTPUT_DIR=$doxygenOutput"
cmake --build build --config Release --target doc --parallel 4
```

Open `build\doxygen-html\html\index.html` after the build. Doxygen warnings
remain enabled and are written to `build\doxygen_warnings.log` so the complete
audit can be reviewed without filling the build console. The warning inventory
and planned cleanup are tracked in the
[documentation release plan](documentation-release-plan.md).

## Build the user guides

Read the Docs builds the Sphinx guides from `docs/` using Python 3.14.
In a Python 3.14 environment, run the same pinned tools from the repository root.

```sh
python -m pip install -r docs/requirements.txt
python -m sphinx -W -n -b html docs build/sphinx
```

Open `build/sphinx/index.html` for the generated user guides. Use the
Doxygen target above for the C++ reference.
