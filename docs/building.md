# Building GridDyn

GridDyn uses CMake for configuration and build generation.

At a high level:

1. Configure a 64-bit build directory with CMake.
2. Enable the components you need with cache options.
3. Build with your platform generator or native build tool.
4. Run the registered tests with CTest.

Detailed build guidance is still being refreshed for the current dependency set and CI environment. Until that migration is complete, the repository's CMake files and CI workflows are the most up-to-date reference for supported configurations.

## Language and compiler requirements

GridDyn requires a 64-bit build and C++23 or newer. The current minimum
compiler versions enforced by CMake are GCC 14 and Clang 18. On Windows, use
a Visual Studio toolset with C++23 support.

C++23 library support is not uniform across compiler releases. In particular,
the code uses feature-test checks for library facilities that arrived after
the C++23 language mode became available; GCC 14 therefore remains supported
even where a newer standard library provides a more direct implementation.
