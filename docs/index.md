# GridDyn documentation

GridDyn is an open-source power-system simulation framework for steady-state,
time-domain, and coupled studies. The Python package provides an installable
interface for common simulation workflows; the C++ project provides the core
simulation engine and additional integrations.

For users of the Python package, start with [installation](installation.md),
then follow the [quick start](quickStart.md) or the detailed
[Python interface guide](python-interface.md). Source-build instructions and
the C++ documentation workflow are in [Building GridDyn](building.md).
For GridDyn-specific network files, see [GridDyn XML inputs](xml-input.md).
For measurement configuration, start with
[Recorders and grabbers](recorders-and-grabbers.md), then consult the
[expression reference](grabber-expressions.md) and
[field and selector reference](grabber-fields.md).
Release history is available in the
[GridDyn changelog](https://github.com/LLNL/GridDyn/blob/main/CHANGELOG.md).

```{toctree}
:maxdepth: 2
:caption: User guides

overview
installation
quickStart
xml-input
recorders-and-grabbers
grabber-expressions
grabber-fields
python-interface
capabilities
building
```

```{toctree}
:maxdepth: 1
:caption: Maintainer notes

documentation-release-plan
developer-guide/wecc-model-coverage-audit
developer-guide/tiocr1-dyr-compatibility
developer-guide/fskit-retirement
developer-guide/powerworld-aux-reader
```

The Doxygen API reference is generated from the C++ source separately. Its
build command and current documentation work plan are described in
[Building GridDyn](building.md) and the
[documentation release plan](documentation-release-plan.md).
