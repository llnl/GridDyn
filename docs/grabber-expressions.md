# Grabber expression reference

This reference lists the operators and callable functions accepted by
`makeGrabbers()` and `makeStateGrabbers()`. Use
[Recorders and grabbers](recorders-and-grabbers.md) for XML configuration,
sampling, saving, and reset, and [Grabber fields and selectors](grabber-fields.md)
to choose the source values.

## Choosing an interface

| Interface                                             | Input                                        | Reads                                                                                |
| ----------------------------------------------------- | -------------------------------------------- | ------------------------------------------------------------------------------------ |
| XML recorder `field`; C++ `makeGrabbers(text,target)` | Expressions or a field list                  | Current object values; results can be scalar or vector.                              |
| C++ `makeStateGrabbers(text,target)`                  | Expressions or a field list                  | Supplied `StateData` and solver mode where the selected field supports state access. |
| C++ `createGrabber(field,target)`                     | One field name or numeric local-state offset | One object-field selection; it does not parse a formula or list.                     |
| C++ `createStateGrabber(field,target)`                | One field name                               | One scalar or supported area vector; it does not parse a formula or list.            |
| C++ `GrabberSet(text,target)`                         | One measurement expression                   | Paired object/state access. A list retains only its first result.                    |

Function names and bare field names are case-insensitive in the string
interpreters: `HYPOT(3,4,12)` is accepted. Object names retain their case.
Use the documented lower-case selector keywords and the literal lower-case
`as` alias separator.

## Fields, lists, and labels

`voltage` reads from the supplied target. `bus2:voltage` selects another
object; `area1::bus2:voltage` descends through an area. A unit annotation
belongs to a field: `bus2:angle(deg)`.

Commas and semicolons separate measurements outside parentheses and brackets:

```text
pi(); clamp(bus1:voltage,0.9,1.1) as bounded_voltage; bus2:angle(deg)
```

Commas inside calls separate arguments. Nested calls and empty-parentheses
constants can appear anywhere an operand is accepted.

`expression as label` sets an object-grabber column label. A vector gets
`label[0]`, `label[1]`, and so on. A label applies to a single grabber, not
collectively to a selector that expands into several grabbers.
`makeStateGrabbers()` accepts and strips the label for expression parity,
but does not retain it as custom state-grabber metadata.

Names containing arithmetic characters or list delimiters are ambiguous;
quoted-name syntax is not supported. Prefer simple object names.

## Operators and precedence

The following table is ordered from strongest binding to weakest.

| Syntax                         | Meaning                                      | Association                                                   |
| ------------------------------ | -------------------------------------------- | ------------------------------------------------------------- |
| `(expression)`, function calls | Grouping and function evaluation             | Innermost first.                                              |
| `x^y`                          | Power, equivalent to `pow(x,y)`              | Right: `2^3^2 = 2^(3^2)`.                                     |
| `+x`, `-x`                     | Unary sign                                   | A leading sign applies after exponentiation: `-x^2 = -(x^2)`. |
| `x*y`, `x/y`, `x%y`            | Multiplication, division, floating remainder | Left: `x/2/3 = (x/2)/3`.                                      |
| `x+y`, `x-y`                   | Addition and subtraction                     | Left: `x-y-z = (x-y)-z`.                                      |

`x^-2` is accepted. Numeric literals include decimal and scientific forms,
such as `0.95`, `-2`, and `1e-3`. Use `^`, not `**`, for powers.
The remainder uses `fmod` semantics: `mod(-5,2)` is `-1`, not a positive
modulo. Division by zero and invalid math domains do not have an automatic
fallback value.

### URI references

A URI reference separates the path and field with `?`, for example
`area1/bus2?voltage`. When an expression contains `?`, slash belongs to the
object path and the legacy `%` spelling means **division**:

```text
div(area1/bus2?voltage,2)
area1/bus2?voltage%2
```

Use `div(x,y)` for division and `mod(x,y)` for remainder when URI references
are involved. This avoids the context-dependent meaning of `%`.

## Constants: zero arguments

Parentheses are required; a bare name still refers to an object field.

| Call    | Value                                                                                   | Object / state                                          |
| ------- | --------------------------------------------------------------------------------------- | ------------------------------------------------------- |
| `pi()`  | Mathematical pi, stored as a double.                                                    | Both.                                                   |
| `nan()` | IEEE quiet NaN.                                                                         | Both; it is not a missing-value substitution operation. |
| `inf()` | The existing GridDyn math-library sentinel `1e48`. **It is finite**, not IEEE infinity. | Both.                                                   |

Examples include `sin(pi()/2)` and `angle*180/pi()`.
Deterministic constant subexpressions are evaluated during setup. Constants
have no solver-state derivatives. Random zero-argument calls are listed
[separately](#random-functions-object-grabbers-only).

## One-argument elementwise functions

These calls accept a scalar or apply independently to every vector entry.
They are available in both interpreters, subject to the source field's
state/Jacobian capabilities.

| Calls                           | Operation and domain                                                   |
| ------------------------------- | ---------------------------------------------------------------------- |
| `sin(x)`, `cos(x)`, `tan(x)`    | Circular trigonometry; input is in radians.                            |
| `asin(x)`, `acos(x)`            | Inverse circular functions; real input in `[-1,1]`, output in radians. |
| `atan(x)`                       | Inverse tangent, output in radians.                                    |
| `sinh(x)`, `cosh(x)`, `tanh(x)` | Hyperbolic functions.                                                  |
| `abs(x)`                        | Absolute value.                                                        |
| `sign(x)`                       | `-1`, `0`, or `1` for negative, zero, or positive input.               |
| `sqrt(x)`                       | Square root; real domain `x >= 0`.                                     |
| `cbrt(x)`                       | Real cube root, including negative inputs.                             |
| `exp(x)`                        | Natural exponential.                                                   |
| `exp2(x)`, `pow2(x)`            | `2^x`. **`pow2` does not mean squaring.**                              |
| `pow10(x)`                      | `10^x`.                                                                |
| `log(x)`, `ln(x)`               | Natural logarithm; real domain `x > 0`.                                |
| `log10(x)`, `log2(x)`           | Base-10 and base-2 logarithms; real domain `x > 0`.                    |
| `ceil(x)`, `floor(x)`           | Round toward positive or negative infinity.                            |
| `round(x)`                      | Nearest integer, with halfway cases away from zero.                    |
| `trunc(x)`                      | Remove the fractional part, rounding toward zero.                      |
| `dec(x)`                        | Signed fractional part: `dec(-1.25) = -0.25`.                          |
| `erf(x)`, `erfc(x)`             | Error function and complementary error function.                       |
| `none(x)`                       | Identity: return `x`.                                                  |

`(x)` is also identity through grouping. The general function library's
inverse-function lookup helpers are not additional expression syntax;
for example, `asinh(x)` is not a registered grabber call.

## Two-argument elementwise functions

These accept two scalars, or broadcast scalar operands over a vector. Two
vector operands must have the same length.

| Calls                         | Operation                                                               |
| ----------------------------- | ----------------------------------------------------------------------- |
| `add(x,y)`, `plus(x,y)`       | `x+y`.                                                                  |
| `subtract(x,y)`, `minus(x,y)` | `x-y`.                                                                  |
| `mult(x,y)`, `product(x,y)`   | `x*y`.                                                                  |
| `div(x,y)`                    | `x/y`.                                                                  |
| `pow(x,y)`                    | `x^y`; real-valued domain follows the underlying power function.        |
| `mod(x,y)`                    | Floating remainder, independent of URI syntax.                          |
| `min(x,y)`, `max(x,y)`        | Smaller or larger operand.                                              |
| `hypot(x,y)`, `mag(x,y)`      | Magnitude `sqrt(x*x+y*y)`, using the library's hypot implementation.    |
| `atan2(y,x)`                  | Angle of the pair `(x,y)`, in radians. Argument order is **y, then x**. |

`vecprod` is a two-vector **reduction**, not an elementwise operation.

## Three-argument elementwise functions

These are available in both interpreters. Scalar broadcasting works in every
argument position; all vector arguments must have equal lengths.

| Calls                        | Operation                                                        |
| ---------------------------- | ---------------------------------------------------------------- |
| `clamp(x,low,high)`          | Return `x` inside the bounds, otherwise the corresponding bound. |
| `min(x,y,z)`, `max(x,y,z)`   | Minimum or maximum of the three operands.                        |
| `hypot(x,y,z)`, `mag(x,y,z)` | Three-component magnitude.                                       |
| `sum(x,y,z)`                 | `x+y+z`.                                                         |
| `product(x,y,z)`             | `x*y*z`.                                                         |
| `median(x,y,z)`              | Middle of the three operands.                                    |

Clamp requires ordered, non-NaN bounds: `low <= high`. Equal bounds are
allowed. Constant invalid bounds raise `InvalidParameterValue` during setup;
variable bounds are checked when evaluated. Bounds are not swapped.

Arity is significant. `sum(v)` reduces a vector; `sum(x,y,z)` adds three
operands elementwise. **`sum(x,y)` is not registered**; use `x+y`.
Similarly, `median` accepts one or three arguments, `hypot` accepts two or
three, and `clamp` accepts exactly three. Calls are not variadic.

## Reductions over objects in one sample

A one-argument reduction treats a scalar as a vector containing one value.
All of these calls support state expressions when the input selection is
available to state grabbers.

| Call                     | Operation                                                                     |
| ------------------------ | ----------------------------------------------------------------------------- |
| `sum(v)`                 | Sum all entries.                                                              |
| `avg(v)`                 | Arithmetic mean.                                                              |
| `min(v)`, `max(v)`       | Minimum or maximum.                                                           |
| `absmin(v)`, `absmax(v)` | Minimum or maximum **absolute magnitude**; the result is nonnegative.         |
| `product(v)`             | Multiply all entries.                                                         |
| `median(v)`              | Middle sorted value, or the mean of the two middle values for an even length. |
| `stdev(v)`               | Population standard deviation: square root of the mean squared deviation.     |
| `vecprod(v,w)`           | Dot product: sum corresponding products. Requires two equal-length vectors.   |

An empty reduction returns `kNullVal` and emits no Jacobian entries. Dot
products do not broadcast scalar inputs. Reductions preserve any unit
conversion and gain/bias applied to their operands.

These are spatial or cross-object reductions at one instant. They do not
accumulate earlier recorder samples. For example,
`sqrt(avg((voltage-1)^2))` is the RMS voltage deviation **across buses in
the current sample**, not a time-window RMS.

## Random functions: object grabbers only

Random functions generate a new value on every evaluation, even when the
simulation time has not changed. Constant arguments do not cause a random
call to be folded. Every random call, including a nested one, is rejected by
`makeStateGrabbers()`.

| Zero-argument call | Distribution                                                      |
| ------------------ | ----------------------------------------------------------------- |
| `rand()`           | Uniform real on `[0,1)`.                                          |
| `randn()`          | Normal, mean 0 and standard deviation 1.                          |
| `randexp()`        | Exponential, mean 1.                                              |
| `randlogn()`       | Lognormal with underlying normal mean 0 and standard deviation 1. |

| Two-argument call           | Parameters                                                                                                                   |
| --------------------------- | ---------------------------------------------------------------------------------------------------------------------------- |
| `rand(low,high)`            | Uniform real on `[low,high)`; use `low < high`.                                                                              |
| `randn(mean,sigma)`         | Normal; positive standard deviation `sigma`.                                                                                 |
| `randexp(mean,unused)`      | Exponential with positive mean. The required second argument is ignored by the current implementation.                       |
| `randlogn(mean,sigma)`      | Lognormal; parameters describe its underlying normal distribution, not the mean and standard deviation of the final samples. |
| `randint(low,high)`         | Uniform integer, inclusive endpoints; use ordered integer bounds. The implementation casts bounds to integers.               |
| `randexval(location,scale)` | Extreme-value distribution; positive scale.                                                                                  |
| `randgamma(shape,scale)`    | Gamma distribution; both parameters positive.                                                                                |

There is no one-argument `randexp` call in the grabber grammar.
Supply valid distribution parameters; the expression layer does not normalize
or repair them. C++ callers can seed the shared generator with
`utilities::GridRandom::setSeed(seed)`. The generator is shared, so evaluation
order affects which sample each grabber receives.

## Units and result shape

Trigonometric functions operate on numbers in radians; they do not inspect
the unit annotation of their inputs. Use `sin(angle)`, not
`sin(angle(deg))`, for an angle stored in radians.

A field annotation such as `angle(deg)` converts that field before expression
evaluation. Convert both operands explicitly when needed:

```text
bus2:angle(deg)-bus1:angle(deg) as phase_difference
```

Expressions do not perform dimensional analysis. A scalar/vector function does
not infer an output unit, and unit suffixes cannot be attached to an arbitrary
computed expression. Native units of model-specific named states may not have
registered conversion metadata.

An elementwise call returns a vector if any operand is a vector. Reductions
return a scalar. Read scalar object results with `grabData()`, object vectors
with `grabVectorData(values)`, and state results with the corresponding
`StateData`/solver-mode overloads. Scalar reads of vector grabbers return
`kNullVal`; they do not implicitly select the first element.

## State values and Jacobians

State expressions use the same arithmetic and function grammar, with these
differences:

- Random calls are rejected.
- The source must be supported by state grabbers. Object-only selectors and
  generic object `get()` parameters do not automatically gain solver access.
- Cached object summaries can be readable without supplying solver
  derivatives. `getJacobianMode() == JacobianMode::NONE` means no Jacobian
  is available for that expression; it does not mean the value is zero.
- State selections bind membership at setup. Recreate or retarget them after
  topology changes.

Compiled expressions apply the chain rule. Binary and ternary calls and the
listed reductions have analytic local partials. Unary functions use registered
analytic derivatives where available, otherwise a numerical approximation.
Unit-conversion scale and gain multiply the derivatives; bias contributes none.
For Jacobians, use affine unit conversions and express logarithms as explicit
functions such as `log10(x)`.

At points without a unique derivative, the implementation defines these
conventions:

| Operation                        | Jacobian convention                                                                                     |
| -------------------------------- | ------------------------------------------------------------------------------------------------------- |
| `min`, `max`, `absmin`, `absmax` | Select the first tied entry; absolute extrema also apply its sign, with the nonnegative branch at zero. |
| `median`                         | Stable middle entry; for an even vector, equally weight the two middle entries.                         |
| `clamp`                          | Follow the input at either bound; with equal bounds, follow the lower bound.                            |
| `hypot` / `mag`                  | Zero partials when every component is zero.                                                             |
| `stdev`                          | Zero partials for a constant vector.                                                                    |

These conventions do not make discontinuous functions smooth. In particular,
numerical derivatives of `sign` and rounding functions at jumps should not be
treated as smooth control equations or substitutes for event conditions.
Invalid domains, NaN, infinity, and mathematical singularities are not
automatically sanitized.

For a running solver, with `simulation`, `stateData`, and `solverMode`
already supplied:

```cpp
auto measurements = griddyn::makeStateGrabbers(
    "vecprod(voltage-1,voltage-1)", simulation);
if (measurements.size() != 1 || !measurements.front()->loaded) {
    throw std::runtime_error("invalid voltage measurement");
}
auto& measurement = *measurements.front();
double value = measurement.grabData(stateData, solverMode);
MatrixDataSparse<double> partials;
if (measurement.getJacobianMode() != griddyn::JacobianMode::NONE) {
    measurement.outputPartialDerivatives(stateData, partials, solverMode);
    partials.compact(); // merge repeated coordinates before indexed inspection
}
```

The relevant headers are `griddyn/measurement/StateGrabber.h`,
`utilities/MatrixDataSparse.hpp`, and `<stdexcept>`. Objects must outlive
their grabbers. These expressions are instantaneous readers; temporal
derivatives, integration, moving averages, and unwrapping belong in explicit
dynamic blocks or recorder postprocessing.

## Invalid input and unsupported operations

| Situation                                                            | Behavior                                                                                                       |
| -------------------------------------------------------------------- | -------------------------------------------------------------------------------------------------------------- |
| Unknown field, unknown function, malformed syntax, unsupported arity | No loaded expression is produced. Object interpretation may also log a warning.                                |
| Invalid entry in a field list                                        | The entry can be skipped while valid entries remain. Validate result count and `loaded`; lists are not atomic. |
| Reading an unloaded scalar/vector                                    | `kNullVal` / empty vector.                                                                                     |
| Mismatched vector lengths or invalid clamp bounds                    | `InvalidParameterValue`; validate configuration before a simulation run.                                       |
| Invalid mathematical domain or division by zero                      | Underlying math behavior; no universal exception or replacement policy.                                        |

The grammar has no comparison or logical operators, conditional `?:` or
`select`, assignment, string-valued expressions, vector indexing/slicing,
user-defined function syntax, or variadic argument lists. Square brackets in
`per_bus[...]` select fields for multiple objects; `voltage[0]` does not
index a voltage vector. Select a particular bus explicitly instead.

## Implementation and regression references

The callable registry is in
[`functionInterpreter.cpp`](../src/utilities/functionInterpreter.cpp).
[`GrabberInterpreter.hpp`](../src/griddyn/measurement/GrabberInterpreter.hpp)
defines the shared grammar, and
[`MeasurementExpression.hpp`](../src/griddyn/measurement/MeasurementExpression.hpp)
and [`MeasurementMath.hpp`](../src/griddyn/measurement/MeasurementMath.hpp)
implement evaluation and local partials. C++ registry helpers such as
`getInverse1ArgFunction()` are separate from the callable-name registry.

[`testGrabberFunctions.cpp`](../test/componentTests/testGrabberFunctions.cpp)
checks object/state parity, independent finite-difference Jacobians, branch
conventions, broadcasting, empty vectors, malformed calls, and random-call
policy. The [XML fixture](../test/test_files/recorder_tests/grabber_functions.xml)
contains recorder examples using clamp, nested constants, and reductions.
