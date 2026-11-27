# GridDyn command interface

GridDyn actions configure a case, advance a simulation, and produce results.
The same text commands work in C++, Python, XML, and the executable.

## Entry points

| Entry point | How to invoke actions                                                                                                                                                      |
| ----------- | -------------------------------------------------------------------------------------------------------------------------------------------------------------------------- |
| C++         | `simulation.execute(text)` runs one action. `add(text)` queues one, `addActions(text)` queues a list, and `addScriptFile(path)` queues a script. `run()` drains the queue. |
| Python      | `sim.execute(text)` runs one action. `sim.set("action", text)` queues a list. `sim.time_domain.run()` drains the queue.                                                    |
| XML         | Place `<action>…</action>` at the simulation level. Each element may hold a list. `<action file="study.gds"/>` queues a file.                                              |
| Executable  | `-P "action=…"` queues a list. `--script study.gds` queues a script. Repeat `--script` to run files in order.                                                              |

The executable loads and initializes the case before draining the queue.
It runs XML actions first, then actions from `--param`, then `--script` files.
Put settings needed during that initial solve in the case or in ordinary
`--param name=value` overrides. Direct C++ and Python callers can set up a
freshly loaded simulation before its first solve.

## Syntax and selectors

- Write one action per line in a file, or separate actions with semicolons.
  Blank lines, comment-only lines, and `ignore` are no-ops. `ignore` may
  be followed by text.
- A `#` at the start of a command or after whitespace starts a comment through
  the end of its line. An embedded `#` such as in `load#0` stays in the operand.
- Verbs and command keywords are case-insensitive. Object names, registered
  solver names, paths, and string values retain their spelling.
- Enclose an operand containing whitespace, semicolons, or a leading `#` in
  double quotes. Inside quotes, a backslash escapes a following double quote
  or backslash. Other backslashes are preserved. Quote a string that would
  otherwise be parsed as a number. Numeric operands must be complete,
  finite numbers.
- A bare `field` addresses the simulation. `object:field` selects a field on
  an object. Use `parent::child:field` or `/parent/child?field` for nested
  objects. Object components can be names, zero-based `type#index` selectors,
  or `type$ID` selectors using user IDs.
- Append `(unit)` to a numeric field where the model supports conversion.
  For example, `bus1::load#0:p(MW)` addresses the first load's real power in
  megawatts. Use `get --help [object]` to discover declared fields.

### Script files

`action <file>` executes a file at that point in the queue. With an immediate
`execute()` call it runs the file in a separate local queue. A relative
include resolves from the containing script or XML input file. Top-level
`--script` paths and output operands such as `save powerflow results.csv`
resolve from the current working directory.

An empty script completes without advancing time. Missing files and include
cycles fail when the queue reaches the reference. File diagnostics include
the script filename and line number for commands read from a script.

## Setup and time advancement

| Command                            | Effect                                                                                                                                                                                                                                                                                                                                                                                                        |
| ---------------------------------- | ------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------- |
| `set <field-selector> <value>`     | Set a numeric or string field. The model determines its valid values and units.                                                                                                                                                                                                                                                                                                                               |
| `setall <type> <field> <number>`   | Apply a numeric assignment through nested areas. The field can include a unit. Supported types are `area`, `bus`, `link`, `relay`, `gen` or `generator`, `load`, `secondary` (generators and loads), and `all` (areas and primary objects). Models without the field are skipped. The action fails if no assignment succeeds or a model rejects the value. Earlier successful assignments keep their effects. |
| `setsolver <role> <name-or-index>` | Select an already registered solver by name or index. Roles are `powerflow`, `dae`, `algebraic` or `dynalg`, and `differential` or `dyndiff`.                                                                                                                                                                                                                                                                 |
| `setsolver <name> <field> <value>` | Change a numeric or string setting on a registered solver.                                                                                                                                                                                                                                                                                                                                                    |
| `initialize [time]`                | Prepare power flow on a fresh case. After `powerflow` has solved the case, prepare dynamics. The default is the configured start time, bounded by the current time when initializing dynamics. A call without a time is a no-op when that stage is already initialized.                                                                                                                                       |
| `powerflow`                        | Solve the steady-state operating point at the current time, with initialization as needed.                                                                                                                                                                                                                                                                                                                    |
| `reset [level]`                    | Reset component state at the current time. Levels are 0 for minimal (default), 1 for voltage, 2 for angle, 3 for voltage and angle, and 4 for full component reset.                                                                                                                                                                                                                                           |
| `run [until]`                      | Advance using the selected dynamic method. A case without differential states uses event-driven power flow. Under `--powerflow-only`, `run` solves power flow.                                                                                                                                                                                                                                                |
| `dynamic [until [interval]]`       | Advance using the selected dynamic method, which defaults to DAE. The interval is accepted for partitioned dynamics.                                                                                                                                                                                                                                                                                          |
| `dynamic dae [until]`              | Advance with DAE. Alias `dynamicdae [until]`.                                                                                                                                                                                                                                                                                                                                                                 |
| `dynamic part [until [interval]]`  | Advance with partitioned dynamics. `partitioned` is an alias for `part`, and `dynamicpart [until [interval]]` is the standalone alias.                                                                                                                                                                                                                                                                        |
| `step [dae] [interval]`            | Advance DAE by a relative interval. A state-changing event may end the step early.                                                                                                                                                                                                                                                                                                                            |
| `eventmode [until [interval]]`     | Advance by event-driven power flow.                                                                                                                                                                                                                                                                                                                                                                           |
| `iterate [interval [until]]`       | Advance by event-driven power flow using interval-first operand order.                                                                                                                                                                                                                                                                                                                                        |

Time operands are in seconds. `until` values are absolute, default to
`timestop`, and cannot precede the current time or the applicable start time.
Intervals must be positive and default to `timestep`. Explicit `dynamic`
and `step` commands require differential states. Use
`set dynamicsolvermethod partitioned` or `set dynamicsolvermethod dae` to
configure those solver roles before an action needs them. A case can also
declare `<dynamicsolvermethod>partitioned</dynamicsolvermethod>`.

A bounded C++ `run(until)` or Python `run_until(until)` pauses the queue if an
advance would exceed that time. On resumption, a paused relative `step` keeps
its original endpoint. With an empty queue, a run advances to its explicit
bound or the configured stop time using the behavior of the `run` action.
Read the actual clock with `get time`,
`getSimulationTime()` in C++, or `sim.time` in Python.

## Field queries

- `get <field-selector> [file]` writes one number or quoted string per line.
  It writes to standard output by default and appends when a file is supplied.
  A missing target, unavailable field, invalid unit, or failed write stops
  execution.
- `get --help [object]` prints the syntax and, with an object, lists its
  declared readable parameters and flags. Models may also expose fields
  through legacy getters.

```text
get time(ms)
get bus1:name
get bus1::load#0:p(MW) "load readings.txt"
get --help bus1::load#0
```

Legacy string getters use `NA` for unavailable values. Core names,
descriptions, parent names, and declared string fields can contain a literal
`NA`.

## Scheduling events

`add event <description>` schedules an inline event using the same description
syntax as `--event` and XML event text. Trigger times are absolute simulation
seconds, at or after the configured start time or current time.

```text
add event @0.15|bus1::load#0:p(MW)=110
add event bus1::load#0:p=1.1 @0.15
add event @0.2,0.3+0.5|bus1::load#0:p=1.1,1.2
add event "@0.4|bus1::load#0:p=1.3;bus1::load#0:q=0.2"
```

Use increasing comma-separated trigger times and supply one value per time
or one value for all times. A repeat period follows `+` and must exceed the
span of the schedule. Quote a compound description containing semicolons.
Compound descriptions apply multiple assignments at one time. Configure
file-based playback in the case input.

## Results and checks

| Command                           | Output                                                                                                                                                                                                                                                                                           |
| --------------------------------- | ------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------ |
| `check powerflow [file]`          | Report violations on standard output or append them to a file. The report may contain violations and still succeed.                                                                                                                                                                              |
| `check powerflow --assert [file]` | Write the same report, then fail the action if any violations were found.                                                                                                                                                                                                                        |
| `save powerflow [file]`           | Write solved results as CSV, text (`.txt` or `.out`), XML, or CDF. With no path, use `powerflowfile`, the source filename plus `.csv`, or `<simulation-name>.csv` for an in-memory case. Use the executable's `--powerflow-output` option with `.raw`, `.m`, or `.py` for a network-case export. |
| `save recorder`                   | Flush configured recorder destinations. A failed write leaves pending samples available for retry. A failed append to a regular CSV or legacy binary file attempts to restore the previous length.                                                                                               |
| `save jacobian [file]`            | Write an HDF5 Jacobian from the current initialized solver to the given path or `jacobian_<simulation-name>.bin`.                                                                                                                                                                                |

### Checkpoint commands

| Command                            | Effect                                                                                                                                                                                                                                                |
| ---------------------------------- | ----------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------- |
| `checkpoint <file> [solver]`       | Replace the file with an HDF5 checkpoint containing solver vectors and declared component history. The optional solver selects an initialized mode such as `dae` or `dyndiff`. The default is the current mode.                                       |
| `rollback <file> [level]`          | Restore the last frame and set the simulation clock to the captured time. Level `local` (default) restores declared component history as well. Level `minimal` publishes the restored solver state to components and can use a `save state` snapshot. |
| `save state [file]`                | Append an HDF5 solver snapshot. With no path, use the configured `statefile`.                                                                                                                                                                         |
| `load state <file>`                | Copy the last frame's vectors into compatible initialized solver buffers. The clock and component state stay at their current values.                                                                                                                 |
| `save powerflow-checkpoint <file>` | Write a JSON power-flow solution.                                                                                                                                                                                                                     |
| `load powerflow-checkpoint <file>` | Restore a JSON power-flow solution into an initialized, compatible model.                                                                                                                                                                             |

Checkpoint solver mode aliases include `ac`, `acflow`, and `pflow` for power
flow, `dynamic` for DAE, `algebraic_only` for `dynalg`, and
`differential_only` for `dyndiff`. The command lowercases this operand.
HDF5 restores require a matching initialized solver and a compatible layout.
The `local` level also requires matching models, parameters, component
identities, and topology. Supply a compatible event queue when replaying
consumed events. See [Checkpointing](https://github.com/LLNL/GridDyn/blob/main/docs/developer-guide/checkpointing.md) for
the validation and restart boundaries.

## Power-flow study commands

### Contingency analysis

Use `contingency [simplified] <mode> [file [count]]` to run all scenarios or
the first `count`. Use `contingency [simplified] <mode> <file> <start> <count>`
for an exact range with a zero-based start and positive count. Modes are
`line`, `gen`, `load` (alias `GridLoad`), `bus`, `n-1`, `n-1-1`, `n-2`,
`n-2-line`, and `n-3-line`. The numbered modes also accept underscores in
place of hyphens.

The output is CSV. `simplified` omits bus and branch columns. A failed
scenario leaves its numerical results blank and records the convergence
diagnostic under `violations`. The report still includes the other scenarios.
Omit the file to write to standard output. `file:/` is an optional file
prefix.

### Continuation and optimal power flow

`continuation <load-scale>` traces an AC loading curve on a copy of the
case. The scale is the constant-power load multiplier at lambda 1.
The optional operands may appear in any order and each can appear once.

| Operand                 | Effect                                                                                                     |
| ----------------------- | ---------------------------------------------------------------------------------------------------------- |
| `target <lambda>`       | Stop at a nonnegative loading parameter. The default is to trace to the first loading limit.               |
| `monitor <id>,<id>,...` | Record voltage and angle for the listed bus user IDs in that order. Per-bus traces require a monitor list. |
| `qlimits`               | Enforce generator reactive-power limits.                                                                   |
| `archive <file>`        | Append to an HDF5 continuation archive, including any available partial trace on failure.                  |

The command reports its continuation status and available loading
parameters. An unmet target or numerical failure stops the queue.

`opf [backend] [preview]` solves single-period DC optimal power flow. The
default backend is `native`. Select `highs` when the build includes HiGHS.
The command reports the objective and applies the dispatch by default.
Use `preview` to report the result without applying dispatch.
In the executable, choose an optimization-enabled simulation with
`--optimization`. In Python, load the case with `type="optimization"`. See
[Python DC OPF](python-interface.md#dc-optimal-power-flow)
for cost models and backend capabilities.

## Status and failures

C++ `execute(text)` and Python `sim.execute(text)` return zero on success
and retain nonzero statuses from the underlying action. For example,
`check powerflow <file>` returns `-3` when writing the report fails.
The C++ text interface also raises `ActionError` with a command diagnostic
and a `SYNTAX`, `TARGET`, `UNSUPPORTED`, or `EXECUTION` category.
The Python interface maps invalid syntax or values to `InvalidParameterError`,
unresolved targets to `InvalidObjectError`, and unsupported commands and
execution errors to `ExecutionError`.

A queue stops at the first exception or nonzero status. Earlier commands
retain their effects. On a retained C++ or Python simulation, another run
continues from the next queued action. Nonzero queued C++ statuses have
context in `getLastActionError()`. Python `run_until()` translates those
statuses to `SolveError`, and `run()` reports them through the runner as
`ExecutionError`. The executable exits with a nonzero process status.

Unsupported forms include `print <field> <destination>`, `add recorder`,
`dynamic decoupled` and its `dynamicdecoupled` alias, `save voltage`,
`save jacstate`, and contingency database output or custom scenario files.

## Runnable study

From the repository root, run the bundled two-bus study.

```sh
gridDynMain test/test_files/pFlow_tests/continuation_two_bus.xml --script test/test_files/runnerTests/action_study.gds
```

The study script sets the load to 30 MW, solves power flow, and advances to
0.2 s. It appends the actual time in milliseconds, load in megawatts, and
demand voltage in per unit to
`study-readings.txt` in that order. It writes the solved case values to
`study-results.csv`. Start with an empty readings file when comparing runs.
