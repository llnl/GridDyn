# Installation

## Python package

The upcoming GridDyn PyPI release requires Python 3.13 or newer. Once it is
available on PyPI, install or upgrade it with:

```bash
python -m pip install --upgrade griddyn
```

Run an included power-flow example to check the installation:

```bash
python -m griddyn.examples.power_flow
```

The package includes runnable examples for power flow, DC optimal power flow,
load changes, and dynamic recorder output. See the
[Python interface guide](python-interface.md) for API details and example
commands.

If pip cannot find a compatible wheel for your platform, it may try to build
from source. A source build requires the C++ toolchain and native dependencies
described in [Building GridDyn](building.md).

PYPOWER and Pandas are not installed as GridDyn dependencies. Install Pandas
separately to use the optional data-frame conversion. PYPOWER is only needed if
you choose to load or run exported PYPOWER case files.

## Build from source

Clone the repository with its submodules initialized, then follow the
[source build guide](building.md). This route is intended for C++ development,
optional integrations, and platforms without a published Python wheel.
