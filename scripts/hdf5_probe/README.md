# Standalone HDF5 compatibility probe

This evaluation is independent of GridDyn's production build. It includes the
actual `GridDynSimulationFileOps.h` and uses `BinaryStateData` and `CoreTime`,
but does not link or run a simulation. Its file layout is not a production schema.

The reviewed source is the official HDF5 2.2.0 release archive:

- [Download and published checksum](https://support.hdfgroup.org/releases/hdf5/2.2.0/downloads/)
- `hdf5-2.2.0.tar.gz` SHA256:
  `1a1ab8209b35586fbc1aa279ba76d102130b95badcb20ca329587219112d8c16`

Build/install it in a separate directory with these CMake options:

```text
-DCMAKE_BUILD_TYPE=Release
-DBUILD_STATIC_LIBS=ON
-DBUILD_SHARED_LIBS=ON
-DHDF5_BUILD_CPP_LIB=ON
-DHDF5_BUILD_HL_LIB=OFF
-DHDF5_BUILD_TOOLS=OFF
-DHDF5_BUILD_EXAMPLES=OFF
-DBUILD_TESTING=OFF
-DHDF5_ENABLE_THREADSAFE=OFF
-DHDF5_ENABLE_PARALLEL=OFF
-DHDF5_ENABLE_ZLIB_SUPPORT=OFF
-DHDF5_ENABLE_SZIP_SUPPORT=OFF
-DHDF5_ENABLE_PLUGIN_SUPPORT=OFF
-DCMAKE_INSTALL_PREFIX=<isolated-install>
```

Build targets `hdf5_cpp-static` and `hdf5_cpp-shared`, then run `cmake --install`.
Upstream builds its C++ implementation with its own C++11 setting. The consuming
probe uses C++23 by default, independently of that setting.

From the repository root, configure the probe with the same target architecture
and compatible runtime/ABI as that installation:

```text
cmake -S scripts/hdf5_probe -B build/hdf5-evaluation/probe \
  -DHDF5_DIR=<isolated-install>/cmake \
  -DGRIDDYN_BOOST_INCLUDE_DIR=<boost-header-root> \
  -DCMAKE_CXX_STANDARD=23
cmake --build build/hdf5-evaluation/probe --config Release --parallel 4
ctest --test-dir build/hdf5-evaluation/probe -C Release --output-on-failure
```

The backslashes above are shell-dependent continuation notation; PowerShell
users can put each command on one line. Set the generator/toolchain as needed.
For clang-cl, `-DCMAKE_CXX_SCAN_FOR_MODULES=OFF` avoids requiring a dependency
scanner for this project, which contains no C++ modules.

Both static and shared consumers test state/derivative arrays, exact signed
nanosecond time beyond double's consecutive-integer range, nonfinite values and
signed zero, rectangular sparse-array metadata and an explicit zero, empty
datasets, chunked result append, close/reopen and a selected column/time slice.
The file uses an HDF5 1.8 feature ceiling. Reading it with an actual older
library or MATLAB remains a separate test.

Probe source is compiled with warnings as errors; included GridDyn, Boost and
HDF5 headers are treated as system headers. This does not establish that the
upstream libraries themselves build without warnings. Compression, failures,
crash recovery, concurrency, full GridDyn linkage and performance are not tested.

See the [integration assessment](../../docs/developer-guide/hdf5-integration-assessment.md)
for findings and the remaining acceptance matrix.
