# Building GridDyn

GridDyn uses CMake for configuration and build generation.

At a high level:

1. Configure a 64-bit build directory with CMake.
2. Enable the components you need with cache options.
3. Build with your platform generator or native build tool.
4. Run the registered tests with CTest.

Detailed build guidance is still being refreshed for the current dependency set and CI environment. Until that migration is complete, the repository's CMake files and CI workflows are the most up-to-date reference for supported configurations.
