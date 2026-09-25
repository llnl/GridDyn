# Python interface

GridDyn provides a compiled `griddyn` package built with nanobind. The PyPI
package requires Python 3.13 or newer. Install it with:

```sh
python -m pip install griddyn
```

## Load and run a network

Use `griddyn.load()` or `Simulation.from_file()` with a path or any
`os.PathLike` object. GridDyn selects a reader from the file extension unless
you pass `format` explicitly.

```python
from pathlib import Path
import griddyn as gd

sim = gd.load(Path("network.xml"))
sim.PFlow.run()

print(sim.Bus["bus1"].voltage)
print(sim.Gen.as_dicts())
```

`Simulation.powerflow()` is also available. For dynamic models, call
`sim.TDS.init()` and then `sim.TDS.run_until(seconds)`. Failures are reported
with `GridDynError` subclasses such as `FileLoadError`, `SolveError`, and
`ExecutionError`.

`Simulation.execute(action)` executes a GridDyn action string immediately and
returns its status code (`0` for success, a negative value for failure). This
can apply a simple parameter change at a chosen point between simulation
advances:

```python
sim.TDS.run_until(1.0)
sim.execute("set bus1:voltage 1.04")
sim.TDS.run_until(2.0)
```

Collections support integer and name lookup, `names`, `as_dicts()`, and
`as_dataframe()`. Data-frame conversion imports `pandas` on demand; install
Pandas separately if you use that method. Collection and model edits use
GridDyn parameter names through `get()` and `set()`. Numeric `set()` calls may
include an optional GridDyn unit string; GridDyn converts the value to the
parameter's expected units. Omitting `unit` preserves the default behavior.

```python
gen.set("pmax", 350.0, unit="MW")
sim.set("period", 2.0, unit="s")
```

The optional `unit` argument applies only to numeric values. Unknown unit
strings raise `InvalidParameterError`.

## Runnable examples

The package includes four runnable modules and the small case files they use.
Run them with Python's module option:

```sh
python -m griddyn.examples.power_flow
python -m griddyn.examples.optimal_power_flow
python -m griddyn.examples.modify_load
python -m griddyn.examples.dynamic_time_series
```

The first example solves a power flow and reports the bus-voltage range. The
OPF example solves the included case and prints the dispatch. The load-change
example edits a load object, solves again, and compares voltage and generation
results.

The dynamic example uses a GridDyn recorder configured in its XML case. It runs
the simulation, has the recorder write voltage, generator-power, and link-power
samples to CSV, and reads those rows back into Python with `csv.DictReader`.
Recorder data can be exported as a time series this way today; the Python API
does not yet return recorder history as an in-memory result object.

## Export PYPOWER and MATPOWER cases

Export the loaded network as a version 2 static power-flow case:

```python
warnings = sim.save_pypower_case(Path("network.py"))
sim.save_matpower_case("network.m")
```

Both methods return warnings for unsupported data that the exporter detects.
They raise `ExecutionError` if the file cannot be written. The PYPOWER output
is a Python case function whose name is derived from the filename (invalid
identifier characters are replaced with underscores). For example,
`network.py` defines `network()` and can be passed to PYPOWER's `loadcase()` or
`runpf()`. Imported active and reactive generator cost curves are retained and
written to the `gencost` table in the same order as the exported generators.
If only some generators have a curve in a costed case, the missing rows are
written as zero-cost curves and reported in the returned warnings.

Generator cost curves belong to the optimization model. Load the case with
`type="optimization"` to retain them on import and include them on export.
Curves are accessed through `sim.optimization` and the generator user ID.
Coefficients use MATPOWER order: highest-degree first for a polynomial, or
alternating power/cost values for a piecewise-linear curve. A simulation loaded
without optimization support has `sim.optimization is None`.

```python
gen = sim.Gen[0]
optimization = sim.optimization
print(optimization.get_generator_cost_curve(gen.uid))
optimization.set_generator_cost_curve(
    gen.uid, 2, [0.01, 2.0, 10.0], startup_cost=40.0
)
```

Pass `reactive=True` to `get_generator_cost_curve()` or
`set_generator_cost_curve()` for a reactive
power curve. Set curves before the first OPF call. Startup and shutdown values
are preserved on export; the current OPF objective uses supported polynomial
curve coefficients and does not include startup or shutdown costs.

After a successful `powerflow()` call, `save_powerflow_csv(path)` and
`save_powerflow_xml(path)` write the corresponding result summaries. These
methods require an explicit filename.

These exporters represent a static AC network. They do not include dynamic
generator/control models, relays, or events. PYPOWER is a separate package and
is not installed as a dependency of GridDyn.

## DC optimal power flow

When GridDyn is built with its optimization library, load a network with
`type="optimization"` to enable its `Optimization` interface.
`sim.optimization.opf()` runs a DC OPF using the imported generator cost curves,
applies the dispatch by
default, and returns solver status and diagnostics. Set `apply=False` to inspect
the result without changing generator setpoints. The default built-in solver
handles continuous linear and convex quadratic problems; use
`sim.optimization.opf(optimizer="highs")` to select HiGHS when it is available.

```python
sim = gd.load("case9.m", type="optimization")
result = sim.optimization.opf()
if result["success"]:
    print(result["objective_value"], result["status"])
    print(sim.Gen.as_dicts())
```

The result dictionary includes `success`, `status`, `message`, `solver`,
`objective_value`, iteration count, feasibility/stationarity diagnostics, and
whether the dispatch was applied. This entry point handles DC OPF with
polynomial costs through quadratic order. AC OPF and integer commitment are
outside its current scope. Piecewise-linear curves are retained and exported;
the current OPF solvers return an unsupported status for them.

## Capability notes

PYPOWER's Python API includes AC and DC power flow, continuation power flow,
and AC/DC optimal power flow. GridDyn's native C++ library adds dynamic
time-domain simulation and control/device models. The Python package exposes
steady-state power flow, time-domain simulation, generator cost curves, and
the native continuous DC-OPF interface. A time-indexed result recorder, a
network-construction API, and piecewise-linear cost support in OPF are useful
future additions.
