# Quick start

## Try the installed Python package

Install GridDyn as described in [Installation](installation.md), then run the
included nine-bus power-flow example:

    python -m griddyn.examples.power_flow

The example loads a bundled MATPOWER case, solves the power flow, and prints
the bus-voltage range and individual bus voltages.

## Load your own network

The Python API accepts a path-like value. GridDyn selects a reader from the
file extension; pass an explicit format when that is not suitable.

    from pathlib import Path
    import griddyn as gd

    sim = gd.load(Path("network.xml"))
    sim.PFlow.run()
    print(sim.Bus.as_dicts())

For dynamic simulations, initialize the time-domain simulation and advance it
to the required time. The packaged dynamic example demonstrates configuring a
recorder and reading its CSV output:

    python -m griddyn.examples.dynamic_time_series

See [Python interface](python-interface.md) for file readers, object access,
units, exports, the DC OPF interface, and current limitations.
