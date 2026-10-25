# Grabber fields and selectors

A grabber selects a field from a target object. The target determines whether
the result is a scalar, a vector, or a model-specific output. Combine those
values using the [expression reference](grabber-expressions.md), or configure
sampling and output using [Recorders and grabbers](recorders-and-grabbers.md).

## Target and field syntax

| Example                  | Selection                                                                              |
| ------------------------ | -------------------------------------------------------------------------------------- |
| `voltage`                | Field on the current target.                                                           |
| `bus2:voltage`           | Field on a named object.                                                               |
| `area1::bus2:angle(deg)` | Descend into an area and request an angle in degrees.                                  |
| `area1/bus2?voltage`     | URI-style object path and field; see the expression reference for division syntax.     |
| `bus#0:voltage`          | Indexed bus lookup, using zero-based indexing. The target determines the lookup scope. |
| `gen1::genmodel:id`      | A named machine-model state, if the model exposes `id` and has initialized it.         |

Object names retain their case. Field expressions normalize field names to
lower case. Indexed references follow `getSubObject(type,index)`; they are
not guaranteed to have the same scope or ordering as a recorder vector.
Use named references when a measurement must identify a specific object.

The tables below describe the registered measurement fields and commonly
used aliases. They are not an enumeration of every parameter supported by
every object's `get()` method.

## Scalars by target type

### Buses

| Fields                     | Meaning                                | Native measurement units                          |
| -------------------------- | -------------------------------------- | ------------------------------------------------- |
| `voltage`                  | Bus voltage magnitude.                 | Per unit.                                         |
| `angle`, `busangle`        | Bus voltage angle.                     | Radians.                                          |
| `freq`, `busfreq`          | Bus frequency.                         | Per unit on the configured system base frequency. |
| `gen`, `general`           | Active generation at the bus.          | Per-unit power.                                   |
| `genreactive`              | Reactive generation at the bus.        | Per-unit power.                                   |
| `load`, `loadreal`         | Active load at the bus.                | Per-unit power.                                   |
| `loadreactive`             | Reactive load at the bus.              | Per-unit power.                                   |
| `linkreal`, `linkreactive` | Bus summaries of connected link power. | Per-unit power.                                   |

Voltage, angle, and frequency fields have state readers and derivative
callbacks. A derivative entry appears only when the requested solver mode
has the corresponding state location. Bus generation/load summaries can
read cached object values and do not provide equivalent bus-summary
Jacobians merely because they are used in a state expression.

### Loads and generators

| Target                   | Fields                                                     | Meaning                                               |
| ------------------------ | ---------------------------------------------------------- | ----------------------------------------------------- |
| Load or generator        | `p`, `real`; `q`, `reactive`                               | Device active and reactive power, in per unit.        |
| Load or generator        | `voltage`, `busvoltage`, `busangle`, `busfreq`             | Its connected bus's voltage, angle, and frequency.    |
| Load                     | `loadreal`, `loadreactive`                                 | Load-specific power fields.                           |
| Generator                | `gen`, `general`, `genreactive`                            | Generator-specific power fields.                      |
| Generator                | `angle`, `freq`                                            | Generator angle in radians and frequency in per unit. |
| Generator, object access | `pset`, `adjup`, `adjdown`, `pmax`, `pmin`, `qmax`, `qmin` | Setpoint, adjustable capacity, and limits.            |

The secondary-device `p`/`q` state readers use the device's output and
partial-derivative interfaces. Other fields, especially operating limits and
cached summaries, need not have equivalent state/Jacobian access. Power sign
and base conventions belong to the selected device; a short field name does
not impose a universal load/generation sign convention.

### Links

The following are the canonical registered names. Object getters can accept
additional spellings such as terminal-numbered power parameters; do not
assume every getter spelling is also a state-grabber alias.

| Fields                                                       | Meaning                                                                     |
| ------------------------------------------------------------ | --------------------------------------------------------------------------- |
| `p`, `real`, `linkreal`; `q`, `reactive`, `linkreactive`     | Terminal-1 active/reactive power.                                           |
| `real2`, `linkreal2`; `reactive2`, `linkreactive2`           | Terminal-2 active/reactive power.                                           |
| `loss`, `lossreactive`                                       | Active/reactive loss.                                                       |
| `voltage`, `voltage2`                                        | Terminal voltage magnitudes.                                                |
| `busangle`, `busangle2`                                      | Terminal bus angles in radians.                                             |
| `angle`                                                      | The link's angle measurement; distinct from an absolute terminal bus angle. |
| `current`, `current2`                                        | Terminal current magnitudes.                                                |
| `realcurrent`, `realcurrent2`; `imagcurrent`, `imagcurrent2` | Real/imaginary terminal current components.                                 |
| `currentangle`, `currentangle2`                              | Current angles in radians, for object access.                               |
| `z`, `z2`; `r`, `r2`; `y`, `y2`; `x`, `x2`                   | Historical impedance-related mappings, described below.                     |
| `attached`                                                   | Enabled and with at least one terminal switch closed.                       |

Link powers are registered as per-unit power fields. State link-power/loss
readers do not currently provide analytic field Jacobians; check
`getJacobianMode()` rather than assuming that successful value access implies
available derivatives.

Two legacy mappings require care when interpreting old configurations:

- `z` and `r` call the link's total- and real-impedance getters. `y` is the
  reciprocal of total impedance; `x` is the reciprocal of imaginary impedance,
  **not a raw-reactance getter**. The terminal-2 variants use the same rules.
- The object-field mappings `busfreq` and `busfreq2` both select the terminal-1
  bus's frequency and declare Hz units. For an unambiguous frequency with the
  correct per-unit conversion context, use an explicit bus reference such as
  `bus2:freq(Hz)`.

These notes describe current registrations, not alternate definitions of
reactance or terminal numbering.

### Areas and simulations

| Scalar fields                      | Meaning                              |
| ---------------------------------- | ------------------------------------ |
| `avgfreq`                          | Area average frequency, in per unit. |
| `gen`, `general`, `genreactive`    | Aggregate generation.                |
| `load`, `loadreal`, `loadreactive` | Aggregate load.                      |
| `loss`                             | Aggregate active loss.               |
| `tieflow`                          | Active tie flow.                     |

An area also exposes the following vectors in both expression interpreters:

| Vector fields                          | One entry per           |
| -------------------------------------- | ----------------------- |
| `voltage`, `angle`, `freq`, `busfreq`  | Bus.                    |
| `busgenreal`, `busgenreactive`         | Bus generation summary. |
| `busloadreal`, `busloadreactive`       | Bus load summary.       |
| `linkreal`, `linkreactive`, `linkloss` | Link power/loss.        |

Vectors visit nested areas first, then direct buses or links, in each area's
stored order. For example, `voltage` on a bus is one scalar, while `voltage`
on an area is a vector. These vectors include the selected objects' returned
values; they are not a filter that drops inactive objects.

Object vector readers inspect current membership when read. State vector
readers bind membership during setup or retargeting. Rebuild or retarget a
state selection after topology changes, then reset/recheck the recording
layout as appropriate.

## Common aliases

These are object-grabber aliases; the area-vector state factory shares this
alias registry. Scalar state grabbers have a separate legacy alias table.
Use the canonical field when portability between access paths matters.

| Aliases                                            | Canonical field                                 |
| -------------------------------------------------- | ----------------------------------------------- |
| `v`, `vol`, `voltages`                             | `voltage`                                       |
| `a`, `phase`, `angles`                             | `angle`                                         |
| `f`, `frequency`, `omega`                          | `freq`                                          |
| `generation`, `genp`, `genpower`, `realgen`, `gen` | `general`                                       |
| `genq`, `reactivegen`                              | `genreactive`                                   |
| `load`, `loadp`, `loadpower`                       | `loadreal`                                      |
| `loadq`, `loadreactivepower`                       | `loadreactive`                                  |
| `busp`, `buspower`, `busgen`, `busgenerationreal`  | `busgenreal`                                    |
| `busq`, `busgenerationreactive`                    | `busgenreactive`                                |
| `busload`, `busloadp`                              | `busloadreal`                                   |
| `busloadq`                                         | `busloadreactive`                               |
| `p`, `power`                                       | `real`, where applicable to the target type     |
| `q`                                                | `reactive`, where applicable to the target type |

An alias does not create a field on every object type. In particular, an area's
`busp` is a generation-summary vector, not a generic net-injection field.
Likewise, `gen` on a bus is a scalar but `busgenreal` on an area is a vector.
Spellings such as `realpower` and `reactivepower` can be supported by a
particular target's getter or state reader; they are not common object-registry
aliases.

## Generic fields, named outputs, and states

Registered component fields include `connected`, `enabled`, `armed`,
`output`, and `deriv`. Boolean status values are numeric 0/1. Output and
derivative suffixes, such as `output0`, `output1`, `deriv0`, and `deriv1`,
select numbered outputs. Named output lookup depends on the component.
Object access also supports `outputs`, a vector of the component's returned
outputs; this does not automatically provide a matching state-vector selection.

Object grabbers also have `currenttime`, `nextupdatetime`,
`lastupdatetime`, and `constant` fields; `constant` reads zero before
gain/bias. These are not additional state-expression math functions.
Object fallback can read fields implemented by an object's `get()`; that
fallback does not automatically provide state access or unit metadata.

Submodels and other components can expose named local states, such as the
classical machine's `id`. Their availability and native units belong to the
model. Configure them after the model exposes its state names. Numeric state
offsets are zero-based C++/XML selections, not vector indexing syntax:
`createGrabber(0,object)`, `StateGrabber(0,object)`, or a recorder's
`offset="0"`. A nonempty XML `field` overrides `offset`.

Relays and sensors add indexed fields. Here `N` is a zero-based decimal
index written directly after the name, for example `condition0` or `block1`:

| Target | Fields                              | Meaning                                |
| ------ | ----------------------------------- | -------------------------------------- |
| Relay  | `condition`, `cN`                   | Condition value.                       |
| Relay  | `statusN`                           | Condition status returned as a number. |
| Sensor | `blockN`, `bN`                      | Block output.                          |
| Sensor | `blockderivN`, `dblockdtN`, `dbdtN` | Block derivative output.               |
| Sensor | `inputN`, `iN`                      | Sensor input.                          |

Use an explicit suffix for condition status: bare `status` is a common alias
for `connected` on the object path. Valid index ranges, model-specific fields,
and Jacobian support depend on the concrete relay or sensor. These names do
not create signals on other component types.

## Object-only expansion selectors

Selectors expand before ordinary expression evaluation. They are accepted by
`makeGrabbers()` and XML recorders, not by `makeStateGrabbers()`.
They are not arguments that can be nested inside a math call:
`sum(all_model)` is not a solver-state selector expression.

| Selector                                                    | Target and result                                                                             |
| ----------------------------------------------------------- | --------------------------------------------------------------------------------------------- |
| `auto`                                                      | Built-in useful fields for the target type; see the table below.                              |
| `all`                                                       | Built-in field set for the target type, not every `get()` parameter.                          |
| `per_bus[voltage,angle]`                                    | One pair of field grabbers for each indexed bus.                                              |
| `per_gen[p,q]`, `per_generator[p,q]`                        | Generator fields on targets that provide generator child lookup, such as a bus.               |
| `per_load[p,q]`                                             | Load fields on targets providing load child lookup.                                           |
| `per_link[loss]`, `per_area[avgfreq]`, `per_relay[output0]` | Other categories, according to the target's indexed child lookup.                             |
| `all_gen_angle`                                             | On an area, expand the suffix field for its indexed generators, when that lookup is provided. |
| `all_model`, `all_gov`, `all_ext`, `all_pss`                | On a generator, state vectors of its machine, governor, exciter, and stabilizer.              |
| `all_state`                                                 | On a generator, states of the generator and its submodels.                                    |

`per_...` iterates `getSubObject(type,index)` until it returns no object.
For an area, indexed bus lookup can include nested areas; it is not limited
to direct children. Generator lookup is not implemented uniformly by every
area, so select a bus or name generators explicitly when necessary. An
unsupported category can produce no grabbers; validate the expansion count.

Model/controller state selectors can be configured before dynamic
initialization and discover internal submodels created during initialization.
Absent submodels produce no columns. Their columns use component/state names.

| Target            | `auto` selection                                                | Difference for `all`                  |
| ----------------- | --------------------------------------------------------------- | ------------------------------------- |
| Bus               | Voltage, angle, generation, load, frequency.                    | Same built-in set.                    |
| Load or generator | Active and reactive power.                                      | Same built-in set.                    |
| Link              | Terminal-1 active/reactive power and active loss.               | Also terminal-2 power and link angle. |
| Area              | Scalar generation, load, loss, and tie-flow summaries.          | Same built-in set.                    |
| Simulation        | Bus voltage, angle, active generation, and active load vectors. | Area-style scalar built-in set.       |

Use explicit field lists when a stable file schema matters rather than relying
on a built-in expansion's field set.

## Implementation references

[`ObjectGrabbers.cpp`](../src/griddyn/measurement/ObjectGrabbers.cpp) contains
the object field/alias registries;
[`stateGrabbers.cpp`](../src/griddyn/measurement/stateGrabbers.cpp) contains
scalar state access; and
[`StateVectorGrabber.cpp`](../src/griddyn/measurement/StateVectorGrabber.cpp)
defines supported state vectors.
[`GrabberInterpreter.cpp`](../src/griddyn/measurement/GrabberInterpreter.cpp)
defines expansion selectors. Exact model-specific output/state names remain
part of each model's API.
