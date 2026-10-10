# Python interface

GridDyn provides a compiled `griddyn` package built with nanobind. The PyPI
package requires Python 3.13 or newer. Install it with:

```sh
python -m pip install griddyn
```

## Load and run a network

Use `griddyn.load()` to create and load a simulation from a path or any
`os.PathLike` object. GridDyn selects a reader from the file extension unless
you pass `format` explicitly. To load a file into an existing simulation,
call that object's `load()` method.

```python
from pathlib import Path
import griddyn as gd

sim = gd.load(Path("network.xml"))
sim.power_flow.run()

print(sim.buses["bus1"].voltage)
print(sim.generators.as_dicts())
```

GridDyn selects input readers from these file extensions (matching is
case-insensitive):

| Extension | Input format |
| --- | --- |
| `.xml` | GridDyn XML |
| `.csv` | GridDyn CSV |
| `.raw`, `.psse`, `.pti` | PSS/E RAW |
| `.dyr` | PSS/E dynamic models |
| `.dyd` | PSLF dynamic models |
| `.cdf`, `.txt` | IEEE Common Data Format |
| `.m`, `.matlab` | MATLAB-style cases; content selects MATPOWER, PSAT, or MatDyn readers |
| `.py` | PYPOWER |
| `.psp` | PSP |
| `.epc` | EPC |
| `.sav` | PSLF SQLite saved case |
| `.json` | ANDES JSON or GridDyn JSON elements |
| `.yaml`, `.yml` | GridDyn YAML elements |
| `.gdz` | Compressed GridDyn case |

Pass `format` when the file uses a different extension or has none. Use the
reader name, such as `format="sav"`. Format names are case-insensitive; a
leading period is accepted. Missing or unsupported formats raise
`FileLoadError`. The `.sav` reader expects the PSLF SQLite schema; it does not
load legacy binary PSS/E saved cases.

`Simulation` owns the loaded system. Use it to load input, inspect or edit
system-level properties and objects, write files, and execute general GridDyn
actions. Direct solver and time-advance operations belong to their domains:

| Interface | Responsibility | Operations |
| --- | --- | --- |
| `sim` | Load or reset the system, access or edit it, write files, and execute general actions | `load()`, `write_file()`, `get()`, `set()`, `find()`, `reset()`, `execute()` |
| `sim.power_flow` | Solve steady-state power flow | `run()` |
| `sim.optimization` | Solve OPF | `opf()` |
| `sim.time_domain` | Initialize and advance dynamic simulation | `initialize()`, `run()`, `run_until()`, `step()` |

The system clock is available as `sim.time`. Failures are reported with
`GridDynError` subclasses such as `FileLoadError`, `SolveError`, and
`ExecutionError`.

For dynamic models, call `sim.time_domain.initialize()` and then
`sim.time_domain.run_until(seconds)`. `sim.execute(action)` runs a general
GridDyn action string and returns its status code (`0` for success, a nonzero
value for failure). The action language can also invoke solvers; `execute()`
remains available for commands without a dedicated Python method. For example,
one can apply a parameter change between simulation advances:

```python
sim.time_domain.run_until(1.0)
sim.execute("set bus1:voltage 1.04")
sim.time_domain.run_until(2.0)
```

The time-domain interface also provides `run()` to reach the configured stop
time and `step(next_time)` to advance toward an absolute time, possibly
stopping earlier at an event. Both `run_until()` and `step()` return the time
actually reached. The current simulation clock is available as `sim.time`.
`sim.reset()` reloads a default-type simulation from its original command-line
arguments. It is available when the system was initially loaded through
`load_from_string()` or `load_from_args()` and no other input was loaded later.
File-loaded and optimization simulations cannot be reset through the runner.
Reacquire model and recorder handles after a reset; older handles raise
`InvalidObjectError`.

For command-line-style input, `sim.load_from_string(arguments)` and
`sim.load_from_args(arguments)` accept GridDyn command-line options and load
the system described by those options. Use `gd.load(path)` to create a
simulation from a file, or `sim.load(path)` when loading into an existing
simulation.

Object collections support integer and name lookup, `names`, `as_dicts()`, and
`as_dataframe()`. Bus rows use `voltage`, `angle`, and `frequency` keys, matching
their object properties. `sim.recorders.names` lists configured recorder names;
recorders also support integer and name lookup. Each recorder exposes its
samples through `as_dicts()` and `as_dataframe()`.
Data-frame conversion imports `pandas` on demand; install Pandas separately if
you use that method. Collection and model edits use GridDyn parameter names
through `get()` and `set()`. Numeric `set()` calls may include an optional
GridDyn unit string; GridDyn converts the value to the parameter's expected
units. Omitting `unit` preserves the default behavior.

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

The dynamic example reads the voltage, generator-power, and link-power samples
from the XML-configured recorder after the simulation runs. Use
`sim.recorders[name].as_dicts()` for a list of rows with numeric `time` and
signal values, or `as_dataframe()` for a Pandas data frame. Repeated signal
names receive `_2`, `_3`, and so on, so no columns are lost. The recorder can
also write those samples to a file when its XML configuration specifies one.
Recorders configured with `autosave` clear their in-memory buffer after each
automatic save.

```python
sim.time_domain.initialize()
sim.time_domain.run_until(10.0)
samples = sim.recorders["signals"].as_dicts()
frame = sim.recorders["signals"].as_dataframe()
```

## Write output files

Use `Simulation.write_file()` for every supported output. GridDyn selects the
writer from the output extension: `.py` writes a PYPOWER case, `.m` writes a
MATPOWER case, and `.csv` or `.xml` writes the current power-flow result
summary. You can pass `type` to choose the writer explicitly, including when
the output filename has no recognized extension. Type names are
`"pypower"`, `"matpower"`, `"csv"`, and `"xml"`; their file extensions are
accepted as type values too.

Export the loaded network as a version 2 static power-flow case:

```python
warnings = sim.write_file(Path("network.py"))
sim.write_file("network.m")
sim.write_file("network.case", type="matpower")
```

Case export returns warnings for unsupported data that the exporter detects;
CSV and XML exports return an empty list. File-write failures raise
`ExecutionError`. The PYPOWER output is a Python case function whose name is
derived from the filename (invalid identifier characters are replaced with
underscores). For example, `network.py` defines `network()` and can be passed
to PYPOWER's `loadcase()` or `runpf()`. Imported active and reactive generator
cost curves are retained and written to the `gencost` table in the same order
as the exported generators. If only some generators have a curve in a costed
case, the missing rows are written as zero-cost curves and reported in the
returned warnings.

Generator cost curves belong to the optimization model. Load the case with
`type="optimization"` to retain them on import and include them on export.
Curves are accessed through `sim.optimization` and the generator user ID.
Coefficients use MATPOWER order: highest-degree first for a polynomial, or
alternating power/cost values for a piecewise-linear curve. A simulation loaded
without optimization support has `sim.optimization is None`.

```python
gen = sim.generators[0]
optimization = sim.optimization
print(optimization.get_generator_cost_curve(gen.uid))
optimization.set_generator_cost_curve(gen.uid, 2, [0.01, 2.0, 10.0], startup_cost=40.0)
```

Pass `reactive=True` to `get_generator_cost_curve()` or
`set_generator_cost_curve()` for a reactive
power curve. Set curves before the first OPF call. Startup and shutdown values
are preserved on export; the current OPF objective uses supported polynomial
curve coefficients and does not include startup or shutdown costs.

After a successful `sim.power_flow.run()` call, `sim.write_file("powerflow.csv")` or
`sim.write_file("powerflow.xml")` writes the corresponding result summary.

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
    print(sim.generators.as_dicts())
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
steady-state power flow, time-domain simulation, in-memory recorder results,
generator cost curves, and the native continuous DC-OPF interface. A
network-construction API and piecewise-linear cost support in OPF are useful
future additions.
