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

| Extension               | Input format                                                          |
| ----------------------- | --------------------------------------------------------------------- |
| `.xml`                  | GridDyn XML                                                           |
| `.csv`                  | GridDyn CSV                                                           |
| `.raw`, `.psse`, `.pti` | PSS/E RAW                                                             |
| `.aux`                  | PowerWorld static power-flow network data                             |
| `.dyr`                  | PSS/E dynamic models                                                  |
| `.dyd`                  | PSLF dynamic models                                                   |
| `.cdf`, `.txt`          | IEEE Common Data Format                                               |
| `.m`, `.matlab`         | MATLAB-style cases; content selects MATPOWER, PSAT, or MatDyn readers |
| `.py`                   | PYPOWER                                                               |
| `.psp`                  | PSP                                                                   |
| `.epc`                  | EPC                                                                   |
| `.sav`                  | PSLF SQLite saved case                                                |
| `.json`                 | ANDES JSON or GridDyn JSON elements                                   |
| `.yaml`, `.yml`         | GridDyn YAML elements                                                 |
| `.gdz`                  | Compressed GridDyn case                                               |

Pass `format` when the file uses a different extension or has none. Use the
reader name, such as `format="sav"`. Format names are case-insensitive; a
leading period is accepted. Missing or unsupported formats raise
`FileLoadError`. The `.sav` reader expects the PSLF SQLite schema; it does not
load legacy binary PSS/E saved cases.

For a PowerWorld network, use `gd.load("network.aux")` or
`gd.load("network.data", format="aux")`. The AUX reader imports power-flow
network data; PowerWorld dynamics and scripts are outside its current scope.
See the [AUX reader guide](developer-guide/powerworld-aux-reader.md) for
supported objects and limitations.

`Simulation` owns the loaded system. Use it to load input, inspect or edit
system-level properties and objects, write files, and execute general GridDyn
actions. Direct solver and time-advance operations belong to their domains:

| Interface          | Responsibility                                                                        | Operations                                                                   |
| ------------------ | ------------------------------------------------------------------------------------- | ---------------------------------------------------------------------------- |
| `sim`              | Load or reset the system, access or edit it, write files, and execute general actions | `load()`, `write_file()`, `get()`, `set()`, `find()`, `reset()`, `execute()` |
| `sim.power_flow`   | Solve steady-state power flow                                                         | `run()`                                                                      |
| `sim.optimization` | Solve OPF                                                                             | `opf()`                                                                      |
| `sim.time_domain`  | Initialize and advance dynamic simulation                                             | `initialize()`, `run()`, `run_until()`, `step()`                             |

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

Use `recorder.set(field, value)` to configure output and sampling settings.
`file`, `filename` and `directory` accept strings or path-like objects. A `.h5`
or `.hdf5` filename selects the shared HDF5 format; `.csv` and `.txt` select text,
and other extensions retain the existing recorder binary format. For example:

```python
recorder = sim.recorders["signals"]
recorder.set("file", "results.h5")
recorder.set("compression", 3)  # Optional lossless compression, 0 off or 1-9.
recorder.set("autosave", 1000)  # Bound the in-memory sample buffer.
```

`recorder.flush()` or `recorder.save_file()` writes pending rows to the configured
file. `recorder.save_file(path)` exports the current buffer to another file;
it does not export earlier autosave chunks or change the configured sink.
`recorder.reset()` discards buffered samples and restarts the output session and
sampling schedule, retaining selected signals. A reset recording replaces its
configured file on its first write. File I/O failures raise `FileLoadError` and
retain buffered samples for retry. Reacquire recorder handles after a simulation
reset; handles belonging to the previous simulation raise `InvalidObjectError`.

```python
sim.time_domain.initialize()
sim.time_domain.run_until(10.0)
samples = sim.recorders["signals"].as_dicts()
frame = sim.recorders["signals"].as_dataframe()
```

## Write output files

Use `Simulation.write_file()` for case and result output. GridDyn selects the
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

## Power-flow solution checkpoints

After initializing and solving a power flow, save its solution and restore it
later into the same initialized simulation or another initialized instance of
the same model:

```python
sim.power_flow.run()
sim.save_power_flow_checkpoint("operating-point.json")

sim.loads[0].set("p", 1.2)
sim.power_flow.run()
sim.load_power_flow_checkpoint("operating-point.json")
```

The JSON file contains the power-flow solver state and bus voltages and angles.
Bus voltages are in per unit and angles are in radians. Values can be edited;
where a bus value also occurs in the solver state, the bus entry takes
precedence. GridDyn checks the file structure and state layout, but does not
check whether edited values form a valid solution.

This is a **solution-only** checkpoint: model parameters and inputs, simulation
time, events, control modes, and time-dependent component history are not
restored. In particular, restoring it after advancing an iterated power flow
does not rewind the iteration or its load profiles. A subsequent solve uses
the current model inputs and the restored solution as its starting point. It is
not a dynamic or full-simulation checkpoint.

This API is intentionally separate from the per-object `ModelFieldSnapshot`
and the HDF5 solver-vector snapshots. The current capture and restore boundaries
are summarized in the [checkpointing developer guide](developer-guide/checkpointing.md).

## Continuation power flow

Use `sim.power_flow.continuation(load_scale=1.5)` to trace an AC loading
curve from the current network settings toward 150% of the constant-power
demand. Both real and reactive demand scale together. The solver works on a
copy, so the original simulation's operating point stays available.

```python
demand = sim.buses["demand"]
result = sim.power_flow.continuation(load_scale=1.5, monitor_buses=[demand.name])
print(result["message"])
print(result["limiting_lambda"])
voltage_curve = list(zip(result["lambda_values"], result["bus_voltages"][demand.uid]))
```

The loading parameter is zero at the base case and one at the specified
target. With the default `stop_at="nose"`, the trace can continue beyond one.
Pass `stop_at=1.0` to check the exact target. Like the OPF result, the dictionary
provides `success`, a machine-readable `status`, and a human-readable `message`.
`success` is true when the requested target is reached or when the default
study finds a loading limit. A numeric target that lies beyond a loading
limit has `success=False` and still returns the confirmed limit.

| Field                             | Meaning                                                                       |
| --------------------------------- | ----------------------------------------------------------------------------- |
| `target_lambda`                   | Requested numeric stop, or `None` for a loading-limit study.                  |
| `last_lambda`                     | Last accepted point, or `None` when the base case cannot be solved.           |
| `limiting_lambda`                 | Confirmed limit at a smooth nose or a turn at a generator Q limit, or `None`. |
| `nose_lambda`                     | Smooth nose only, or `None`.                                                  |
| `lambda_values`, `residual_norms` | Aligned lists for all accepted points.                                        |
| `bus_voltages`, `bus_angles`      | Aligned traces in per unit and radians, keyed by bus user ID.                 |

`monitor_buses` accepts bus names or integer user IDs (`bus.uid`).
`nose_before_target` indicates a confirmed nose before a numeric target.
`base_solve_failed`, `numerical_failure`, and `step_limit` identify an incomplete
trace. Any returned points passed the residual tolerance.

For selected loads, pass absolute targets such as
`load_targets={"load5": {"p_mw": 60, "q_mvar": 20}}` instead of
`load_scale`. Include only `p_mw` or only `q_mvar` to hold the other component
at its base value. Optionally specify non-slack generator MW with
`generator_targets={"gen2": 150}`. Use unique or qualified object names.

Continuation supports one connected AC island with one angle reference
and uses the simulation's dense or sparse solver setting.
Uniform scaling applies to the constant-power portion of standard loads and
leaves fixed shunts and other load models at their base settings. Tap, shunt,
and PV/PQ assignments stay fixed by default. Generator reactive limits and
equipment ratings can impose earlier operating limits than the fixed-control
nose.

Pass `enforce_q_limits=True` to enforce fixed reactive limits for local and
remote generator voltage control, including shared groups imported from
PSS/E RAW. At a bound the solver locates the crossing and fixes that
generator's reactive output. The remaining generators regulate the bus
using the group's participation factors. The regulated voltage becomes
free to change when the group runs out of participating generators.

Each accepted switch appears in `q_limit_events` with the generator's
terminal bus user ID (`bus_id`), the regulated bus user ID
(`regulated_bus_id`), generator name, loading parameter, bound
(`"upper"` or `"lower"`), and reactive output in MVAr. The two bus IDs
are the same for a local controller.
`q_limit_turn` means no increasing-load path continues from the switch with
the generator held at that bound. `limiting_lambda` gives the loading level
at that turn.
The trace holds a binding limit through the end of the study. Reactive
limits at a fixed-voltage slack bus, generator capability curves, and
limits for non-generator voltage controllers are outside this option's
current scope.

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
outside its current scope. Convex piecewise-linear generator costs are also
supported by the native DC OPF; nonconvex piecewise curves require HiGHS because
their formulation contains binary segment selectors. A nonzero cubic cost term
remains unsupported by this DC OPF.

## Capability notes

PYPOWER's Python API includes AC and DC power flow, continuation power flow,
and AC/DC optimal power flow. GridDyn's native C++ library adds dynamic
time-domain simulation and control/device models. The Python package exposes
steady-state power flow, time-domain simulation, in-memory recorder results,
generator cost curves, and the native continuous DC-OPF interface. A
network-construction API and piecewise-linear cost support in OPF are useful
future additions.
