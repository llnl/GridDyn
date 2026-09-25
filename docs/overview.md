# Overview

GridDyn models electric power networks and can run steady-state power flow and
time-domain simulations. The C++ application and libraries also provide
optional optimization, FMI, HELICS, and communications integrations, depending
on build configuration.

The published Python package focuses on simulation control, network
inspection and editing, power-flow export, and a continuous DC optimal
power-flow interface. It requires Python 3.13 or newer. See
[Capabilities and limits](capabilities.md) for the currently documented
Python surface and its boundaries.

GridDyn uses CMake and requires a 64-bit C++23 toolchain. See
[Installation](installation.md) for the Python package and
[Building GridDyn](building.md) for source builds, optional components, and
Doxygen generation.
