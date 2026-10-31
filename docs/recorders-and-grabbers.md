# Recorders and grabbers

Grabbers select values from GridDyn objects. Collectors arrange those values in
columns and schedule sampling. Recorders add an in-memory time series and
optional file output. The same field expressions are used by XML recorders and
the C++ measurement API. The [expression reference](grabber-expressions.md)
lists every supported operator and function signature; the
[field and selector reference](grabber-fields.md) describes source values,
aliases, and object-selection rules.

## Configuring a recorder

```xml
<recorder name="bus_signals" target="bus3" period="0.05">
  <field>voltage as terminal_voltage; angle(deg) as phase</field>
  <start>0</start>
  <stop>10</stop>
  <file>results/bus_signals.csv</file>
</recorder>
```

`target` selects the object relative to the element containing the recorder.
An individual field can name another object, for example `bus2:angle(deg)` or
`area1::bus2:voltage`. Object names retain their case. Bare field names are
normalized to lower case. Commas and semicolons separate fields outside
parentheses and brackets; commas inside function calls separate arguments.
` as name` assigns a column label. A label on a vector becomes `name[0]`,
`name[1]`, and so on.

| Setting                   | Meaning                                                                                                                                                                                      |
| ------------------------- | -------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------- |
| `period`                  | Sampling interval in seconds; finite, positive, and representable by the simulation clock. Default: 1.                                                                                       |
| `frequency`               | Sampling rate in samples per second; its reciprocal becomes the period.                                                                                                                      |
| `period_resolution`       | Round the requested period to a positive resolution, with a minimum of one resolution interval.                                                                                              |
| `start`, `starttime`      | First scheduled sample time.                                                                                                                                                                 |
| `stop`, `stoptime`        | Last allowed sample time, inclusive.                                                                                                                                                         |
| `gain`, `bias`            | Apply `value * gain + bias` to every selected signal, including expanded fields and vectors.                                                                                                 |
| `units`, `unit`           | Requested units for a field; conversion precedes gain and bias. A field can also specify units as `angle(deg)`.                                                                              |
| `column`                  | Zero based column for the first selected signal. Following signals append after it, including its full vector width.                                                                         |
| `offset`                  | Zero based local state index, including index 0. A nonempty `field` takes precedence.                                                                                                        |
| `file`, `filename`        | Output path; XML also accepts `sink`. `.h5` and `.hdf5` select HDF5; `.csv` and `.txt` select comma-separated text; other extensions select GridDyn binary. Extension matching ignores case. |
| `directory`               | Base directory for relative output paths. Parent directories are created when saving.                                                                                                        |
| `precision`               | Text data precision, from 1 to 17 significant digits. Values 0 and -1 select the writer default of 8.                                                                                        |
| `reserve`, `reservecount` | Preallocate storage for a nonnegative integer number of samples.                                                                                                                             |
| `autosave`                | Save and release the in-memory buffer after this many samples. Zero disables automatic saving. Requires a file.                                                                              |
| `vector_name` flag        | Use the vector selection name with numeric indexes instead of individual object names.                                                                                                       |

Recorders use delayed event execution so samples normally include model changes
made by other events at the same time. Direct C++ calls to `trigger(time)`
sample the supplied time; the event queue determines when scheduled triggers
are called. A delayed trigger advances to the next point on the original
sampling grid rather than replaying missed samples.

Intervals are rounded to the simulation clock's nanosecond resolution. Settings
that round to zero or reach its maximum-time sentinel are rejected. Invalid
settings leave the previous interval in effect. XML declarations with the same
output filename share a recorder; a file-only declaration does not select an
unrelated recorder whose output filename is empty.
Externally driven collectors, such as FMI outputs, can use the constructor pair
`Collector(maxTime, maxTime)` to disable periodic scheduling.

## Field expressions and selections

Use expressions to transform fields or reduce an area vector to one column:

| Expression                                            | Result                                                     |
| ----------------------------------------------------- | ---------------------------------------------------------- |
| `bus2:angle(deg)-bus1:angle(deg) as phase_difference` | Difference after converting both angles to degrees.        |
| `clamp(voltage,0.9,1.1)`                              | Bound a scalar or each element of a vector.                |
| `hypot(p,q)`                                          | Two-component magnitude.                                   |
| `angle*180/pi()`                                      | Convert radians using the named mathematical constant.     |
| `avg(voltage)`                                        | Mean bus voltage when the target is an area or simulation. |
| `vecprod(voltage-1,voltage-1)`                        | Sum of squared bus-voltage deviations.                     |

The target determines shape: bus `voltage` is scalar, while area `voltage` is
vector-valued. Scalar operands broadcast over vectors; vector operands must
have equal lengths. Reductions operate across objects in the current sample,
not across recorded time. Unit annotations convert individual fields before
evaluation; expressions do not perform dimensional analysis.

See the [expression reference](grabber-expressions.md) for precedence,
function arities, constants, random functions, URI syntax, domain errors,
and solver derivative conventions.

Object selections also support `auto`, `all`, and selectors such as
`per_bus[voltage,angle]`. Their expansion depends on the target's object
lookup; an area's indexed buses can include nested areas. Generator selectors
such as `all_model` and `all_state` expose model-state vectors. These
expansions are object-only and are separate from the area field vectors
supported in state expressions. See [fields and selectors](grabber-fields.md)
for the supported field sets, aliases, scope, and ordering.

## C++ sampling, cloning and lifetime

`createGrabber(field, object)` selects an object field. `makeGrabbers()` expands
an expression or field list. A grabber's `loaded` flag indicates whether it can
produce data. Unloaded scalar reads return `kNullVal`; unloaded vector reads
clear the output vector. Objects must outlive their grabbers.

`createStateGrabber(field, object)` selects one scalar or supported area
vector. `makeStateGrabbers()` parses expressions or field lists into readers
that accept a supplied `StateData` and solver mode. Named states and numeric offsets resolve
against the target object's state names. Read vectors with
`grabVectorData(stateData, solverMode, values)`, or reduce them to a scalar:

```cpp
auto measurements = makeStateGrabbers(
    "avg(voltage); sum(clamp(voltage,0.9,1.1)); vecprod(voltage-1,voltage-1)",
    simulation);
```

Successful value access does not imply solver derivatives are available;
check `getJacobianMode()`. The
[state/Jacobian reference](grabber-expressions.md#state-values-and-jacobians)
explains derivative support, branch conventions, and sparse assembly.
Composite state grabbers report their targets through `getObjects()`.

Vector selections can change width after topology changes. Recreate or
retarget state vectors to refresh bound membership, then recheck/reset the
collector layout as needed.

`GrabberSet` pairs object and state access for one measurement expression.
A field list retains only its first result; use the `make...Grabbers()`
functions when several measurements are intended. A set can also contain
only one access form or be empty. Empty reads return `kNullVal`; an empty
vector read clears the output. Cloning copies measurement configuration and
callbacks. Retargeting a clone leaves the original object's association
intact. A recorder clone starts with an empty sample buffer. Configure a
different file or directory when the clone should write independent output.

`Collector::grabData(buffer, count)` writes only inside the supplied buffer.
Its return value is the extent written, not the number of nonempty columns.
Explicit column gaps retain the caller's existing buffer values; gaps in a
collector's own storage start at zero. If explicit columns overlap, later
points overwrite earlier values. `recheckColumns()` recalculates vector
widths and automatic positions after a target or topology change. Unexpected
vector-width changes raise an exception until the columns are rechecked.

A recorder rejects sampling after signals are added or its targets are changed,
even if the column count stays the same. Reset before continuing with the new
configuration so headers remain consistent. Do not mutate shared grabbers
externally during a recording. `getData(column)` rejects an invalid column with
`std::out_of_range`. `getTime()` and `getTimeSeries()` expose the
current buffer; autosave can release earlier samples from that buffer.

## Saving and resetting

Use a `.h5` or `.hdf5` filename to request HDF5 output:

```xml
<recorder name="bus_signals" target="bus3" period="0.05">
  <field>voltage as terminal_voltage; angle(deg) as phase</field>
  <file>results/bus_signals.h5</file>
  <autosave>1000</autosave>
  <compression>3</compression>
</recorder>
```

The equivalent C++ settings are `recorder.set("file", "results/bus_signals.h5")`
and `recorder.set("compression", 3.0)`. Compression is optional: 0 (the default)
disables it, and integer levels 1-9 select lossless shuffle/deflate. Reset the
recorder before changing compression after its HDF5 session has started. Other
output formats do not use this setting.

HDF5 output uses the same [storage schema](developer-guide/binary-state-output.md)
as snapshots and diagnostics. Source 3 identifies recorder records: code 0 is a
sample vector, code 1 contains channel names, and code 2 contains the recorder
name and description. Sample `time_ns` preserves exact signed integer
nanoseconds; `index` counts samples across flushes and autosave chunks. Channel
names are stored once, including empty column gaps and full UTF-8 labels.
The complete pending buffer is published as one frame and the file is closed
before the save returns. Autosave bounds memory as with the other file formats.

The MATLAB reader can inspect these files without another binary reader:

```matlab
samples = readStateFile('results/bus_signals.h5', 'Source', 3, 'Code', 0);
channels = readStateFile('results/bus_signals.h5', 'Source', 3, 'Code', 1);
values = cell2mat(cellfun(@(v) v(:).', samples.data(:), 'UniformOutput', false));
```

`flush()` and `saveFile()` write new buffered rows to the configured file.
Repeated flushes without new samples do no work. New recordings replace an
existing file on their first write, then append new rows. Autosave appends
chunks and clears the in-memory samples after each successful save.

`saveFile("snapshot.dat")` exports the current buffer to an independent file
with its own header. It does not change the configured sink or its append
position. After autosave, a snapshot contains only the samples still in memory.
An explicit path that identifies the configured sink acts as `flush()`, including
existing file aliases and paths containing `.` or `..`; it preserves earlier
autosave chunks. A failed save retains buffered samples for a later retry.

The shared text writer currently formats timestamps with five significant
digits, independently of the data-column `precision` setting. Binary output
preserves the stored numeric timestamps and is preferable when that text time
resolution would merge nearby samples.

`Recorder::reset()` clears recorded samples, preserves the selected signals,
and restarts the recording and output-file session. `Collector::reset()` on a
plain collector clears its selected signals and column layout. The recorder
behavior also applies when resetting through a `Collector` reference.
Scheduling restarts at the configured origin, including a nonzero constructor
time or an explicit `start`/`time` setting. Externally driven collectors remain
unscheduled after reset.

With FMI export enabled, `FmiCollector` registers each scalar or vector column
using the collector's resolved column descriptions. State-only points are also
supported. Registration uses the layout available when a point is added, so
initialize models before adding their state-vector outputs. FMI variable
registration is a setup operation; keep the published output layout fixed.

The recorder destructor attempts to save remaining samples when a filename is
configured. For applications that need to handle file errors, call `flush()`
explicitly while the target objects are still alive.

## Remaining extensions worth considering

The most useful follow-up work is:

1. **Time-based reductions and windows:** min/max, RMS, integral and rate of
   change over recorded time. Current reductions operate across a vector at
   one instant. A time-window API needs defined reset and irregular-sample rules.
2. **Runtime schema changes:** support adding or removing signals during a
   recording with explicit file-schema/version handling. The current reset
   requirement keeps existing rows and file headers consistent.
3. **Broader solver-state selection:** extend the model/controller `all_state`
   selectors to solver expressions, and add state-aware power-flow derivatives
   for aggregate fields that currently expose only cached object summaries.
4. **Discoverable fields:** a public enumeration of scalar/vector fields,
   units and Jacobian availability would make configuration validation and
   editor assistance much easier. Object and state alias tables currently
   contain overlapping information that could share a declarative registry.
5. **Structured errors and quoted names:** report expression locations and
   distinguish an unknown field from malformed syntax. Quoted object names
   would avoid ambiguity for names containing arithmetic characters.
6. **Text writer precision:** add an explicit timestamp-precision setting to
   the shared time-series writer so long runs with fine sampling retain their
   time resolution in text output as well as binary output.

The focused regression cases in `test/componentTests/testMeasurements.cpp`
cover arithmetic and state parity, units, retargeting, vector operations,
finite-difference Jacobian comparisons, column layout, scheduling, validation,
cloning, reset, binary/text output, snapshots and autosave. Existing XML
recorder tests continue to cover integration with the simulation event queue.
`test/componentTests/testGrabberFunctions.cpp` adds object/state parity for
three-argument calls and constants, independent finite-difference Jacobian
checks, nested-area ordering, branch conventions, malformed input, randomness
policy, and a read-count regression for reduction Jacobian cost.
