# GridDyn documentation

GridDyn is an open-source power-system simulation framework for steady-state,
time-domain, and coupled studies. The Python package provides an installable
interface for common simulation workflows; the C++ project provides the core
simulation engine and additional integrations.

For users of the Python package, start with [installation](installation.md),
then follow the [quick start](quickStart.md) or the detailed
[Python interface guide](python-interface.md). Source-build instructions and
the C++ documentation workflow are in [Building GridDyn](building.md).

```{toctree}
:maxdepth: 2
:caption: User guides

overview
installation
quickStart
python-interface
capabilities
building
```

```{toctree}
:maxdepth: 1
:caption: Maintainer notes

documentation-release-plan
```

The Doxygen API reference is generated from the C++ source separately. Its
build command and current documentation work plan are described in
[Building GridDyn](building.md) and the
[documentation release plan](documentation-release-plan.md).
