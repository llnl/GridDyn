# GridDyn XML inputs

GridDyn XML is the project-specific input format for building a simulation from
GridDyn objects. It describes the model hierarchy and supports GridDyn-specific
reader features. The Python package also reads supported MATPOWER cases; see
[Capabilities and limits](capabilities.md) for that interface. For Python file
loading, see
[Load and run a network](python-interface.md#load-and-run-a-network).

## Network structure

The root `<griddyn>` element represents the simulation. Buses are named with a
`name` attribute. Loads and generators are nested under their bus, while links
connect named buses from the root. Model values are commonly written as child
elements:

```xml
<?xml version="1.0" encoding="utf-8"?>
<griddyn name="two_bus" version="0.0.1">
  <bus name="bus1">
    <type>SLK</type>
    <angle>0</angle>
    <voltage>1.05</voltage>
    <generator name="gen1"></generator>
    <load name="load1">
      <P>1.15</P>
      <Q>0.31</Q>
    </load>
  </bus>
  <bus name="bus2">
    <load name="load2">
      <P>0.45</P>
      <Q>0.2</Q>
    </load>
  </bus>
  <link name="bus1_to_bus2" from="bus1" to="bus2">
    <b>0.1273164</b>
    <r>0.083931984</r>
    <x>0.518336712</x>
  </link>
  <flags>powerflow_only</flags>
</griddyn>
```

This example follows the current
[two-bus case](https://github.com/LLNL/GridDyn/blob/main/examples/two_bus_example.xml).
The `from` and `to` values refer to bus names. `powerflow_only` asks the
simulation to stop after the power-flow solve. Component and model fields vary
by object type, so use an example for the model you are configuring as a guide
to supported settings.

## Dynamic models and output

Dynamic configuration is nested under the component it belongs to. For
example, a generator can contain a dynamic model, and an event on a load can
change that load during a run. A recorder selects fields and a sampling period;
Python can read its samples in memory, and an optional output setting writes
them to a file. The current
[two-bus dynamic example](https://github.com/LLNL/GridDyn/blob/main/examples/two_bus_dynamic_example.xml)
shows these pieces together:

```xml
<griddyn name="2bus_test" version="0.0.1">
  <bus name="bus1">
    <type>SLK</type>
    <angle>0</angle>
    <voltage>1.05</voltage>
    <generator name="gen1">
      <dynmodel>typical</dynmodel>
      <pmax>4</pmax>
    </generator>
    <load name="load1">
      <P>1.15</P>
      <Q>0.31</Q>
      <event>@1|p=1.1</event>
    </load>
  </bus>
  <bus name="bus2">
    <load name="load2">
      <P>0.45</P>
      <Q>0.2</Q>
    </load>
  </bus>
  <link from="bus1" name="bus1_to_bus2" to="bus2">
    <b>0.1273164</b>
    <r>0.083931984</r>
    <x>0.518336712</x>
  </link>
  <timestop>10</timestop>
  <recorder period="0.05" field="auto">
    <file>twobusdynout.csv</file>
  </recorder>
</griddyn>
```

See [Recorders and grabbers](recorders-and-grabbers.md) for field expressions,
sampling settings, unit conversion, file output, autosave, and reset behavior.
The [expression reference](grabber-expressions.md) lists supported functions
and operators; [fields and selectors](grabber-fields.md) describes target
fields, aliases, vectors, and selection scope.

## Imports and reader directives

An XML input can import other files, including supported power-system data
files. For example, the repository's
[179-bus dynamic case](https://github.com/LLNL/GridDyn/blob/main/examples/179busDynamicTest.xml)
imports network and dynamic data before adding GridDyn-specific events and
recorders.

GridDyn XML also provides reader directives for reusable custom fragments,
arrays, parameter definitions, and conditional inclusion. These are
GridDyn-specific extensions; they are not part of standard XML. Their syntax
and the accepted component properties are implemented by the current reader
and model classes, so older manuals may describe options that have since
changed.

## Scripted actions

Use `<action>` elements at the simulation level to queue commands. One element
may contain several commands separated by semicolons. The executable runs
commands in order until completion or the first error, including a syntax
error at the command's position. An error reports the input filename and
failing command and exits with a nonzero status. Earlier commands keep their
effects. Blank and comment-only actions are skipped.

`dynamic <until>` uses the configured dynamic method. Set
`<dynamicsolvermethod>partitioned</dynamicsolvermethod>` in the case or queue
`set dynamicsolvermethod partitioned` before running partitioned dynamics.
An explicit mode such as `dynamic dae <until>` requires its solver to be
configured already. Cases without a dynamic method selection use the built-in
DAE default.

### Script files

Load the case normally and supply one or more script files to the executable.
The runner initializes the case as it does for XML actions. It then executes
case actions, actions supplied through `--param`, and files supplied through
`--script`. Repeated `--script` options run in the order given.

```sh
gridDynMain case.xml --script "study actions.gds"
```

A script is a text file with one command per line. Separate multiple commands on
a line with semicolons. Blank lines, tabs between operands, and `#` comments
are supported. A comment starts at the beginning of a command or after
whitespace and runs to the end of the line. Use double quotes for filenames
with spaces or semicolons.

```text
set timestop 0.5
action "steps/first stage.gds"
run 0.5
```

`action <file>` runs the referenced script at that position. A relative include
is resolved from the script that references it. In XML, either
`<action>action "steps/first stage.gds"</action>` or
`<action file="steps/first stage.gds"/>` resolves the path from the containing
case file. Top-level `--script` paths and ordinary file operands such as
checkpoint and report paths resolve from the working directory.

A script error reports the filename, line number, and command. Missing scripts
and include cycles fail when execution reaches the reference. The executable
stops at the first failure. An empty script completes without advancing time.

### Field queries

`get <field-selector> [file]` reads one value at the current simulation time.
Use the same selectors as `set`. A bare field reads from the simulation, and
`object:field` reads from the selected object. Numeric fields can take a unit
suffix such as `(MW)` or `(ms)`.

```text
get time(ms)
get bus1:name
get bus1::load#0:p(MW) "load readings.txt"
get --help bus1::load#0
```

Each query writes a number or quoted string on its own line. Without a file,
the value goes to standard output. With a file, the command appends the value.
Double-quote selectors and paths that contain spaces. `get --help` shows the
syntax, and specifying an object also lists declared parameters and flags
from its model description. Models may expose additional fields through
their getters, which also determine which units they support. An unknown
target, unavailable field, or failed write stops the script.

Legacy string getters reserve `NA` for unavailable values. Object names,
descriptions, parent names, and declared string parameters can contain a
literal `NA`.

### Checkpoint commands

| Command | Effect |
| --- | --- |
| `checkpoint <file> [solver]` | Writes an HDF5 checkpoint with solver vectors and declared component history at an accepted simulation point. Uses the current solver mode by default. An optional mode such as `dae` or `dyndiff` selects an initialized solver. Replaces the file. |
| `rollback <file> [local|minimal]` | Restores the last HDF5 frame into a compatible initialized simulation and returns its clock to the captured time. The default `local` also restores declared component history from a `checkpoint` frame. Use `minimal` to restore solvers and publish the solver state to components, including from a `save state` snapshot. |
| `save state <file>` / `load state <file>` | Appends an HDF5 solver snapshot or copies the last frame's vectors into initialized solver buffers. Loading leaves the clocks and component state at their current values. |
| `save powerflow-checkpoint <file>` / `load powerflow-checkpoint <file>` | Saves or restores a readable JSON power-flow solution in an initialized model with a matching layout. |

Use double quotes around a file path containing spaces. Semicolons separate
commands in action lists. HDF5 restores require a unique matching initialized
solver and a compatible layout. The `local` level also requires the same model,
parameters, component identities, and topology. Event progress, recorder
positions, and model parameters remain outside the checkpoint. Replaying
consumed events requires a compatible event queue supplied by the caller.
See the [checkpointing guide](developer-guide/checkpointing.md) for the
validation and restart boundaries.

### Power-flow study commands

```text
continuation 6 target 0.5 monitor 20,10 qlimits archive "loading trace.h5"
opf
opf highs preview
```

`continuation <load-scale>` traces an AC loading curve on a copy of the
case. The load scale is the constant-power load multiplier at lambda 1.
Add `target <lambda>` to stop at a nonnegative loading parameter. With no
target, the study stops at the first loading limit. `monitor <id>,<id>,...`
records voltage and angle in the specified bus user-ID order.
`qlimits` enforces generator reactive-power limits. `archive <file>`
appends the study to an HDF5 continuation archive, including an available
partial trace on failure.

The command reports a reached target or loading limit. An unmet target
or numerical failure stops the script with its continuation status.

`opf [native|highs] [preview]` solves single-period DC optimal power flow
using the case's generator costs and network constraints. The default
backend is `native`. The command reports the objective and applies the
optimal generator dispatch unless `preview` is supplied. Select the
optimization simulation before loading the case in the executable.

```sh
gridDynMain --optimization case9.m -P "action=opf"
```

The `--optimization` option requires an optimization-enabled build, and
`opf highs` requires HiGHS support. The [Python DC OPF guide](python-interface.md#dc-optimal-power-flow)
describes the cost models and backend capabilities.
