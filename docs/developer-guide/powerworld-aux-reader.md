# PowerWorld AUX static network reader

GridDyn accepts `.aux` through `loadFile` and `loadAux`, using the same
`BasicReaderInfo` interface as the RAW reader. It reads PowerWorld `DATA
(Object, [fields])` sections and the compact `Object (fields)` form. Fields
are selected by name, so their order and the presence of unrelated fields do
not affect the network mapping. Large files are scanned in three streaming
network passes: metadata, buses, then equipment. Optimization simulations make
one additional pass for generator costs.

## Loading an AUX network

The C++ `loadFile` overloads select `loadAux` for `.aux` paths, ignoring
extension case. An explicit format is useful for a file without that suffix;
either `"aux"` or `".aux"` is accepted. Direct `loadAux` calls take a
`CoreObject*` and optional `BasicReaderInfo` settings, including base power,
name prefix, and the power-flow-only flag.

```cpp
griddyn::loadFile(simulation.get(), "network.AUX");
griddyn::loadFile(simulation.get(), "network.data", nullptr, "aux");
```

The Python `griddyn.load(path)` and `Simulation.load(path)` methods use the
same extension dispatch. They also accept `format="aux"` for an explicit
format:

```python
import griddyn as gd

sim = gd.load("network.aux")
sim.power_flow.run()
```

An AUX file containing network data can create a standalone power-flow model.
An AUX file can also supplement a simulation already loaded from another
format; existing buses and generators are matched by their user IDs and names
where available. Load the base network first in that case.

For case hierarchy and geography, C++ callers can separately read
`AuxCaseMetadata` without changing the simulation:

```cpp
auto metadata = griddyn::readAuxCaseMetadata("network.aux");
auto substation = metadata.mSubstations.at(20);
auto bus = metadata.mBuses.at(101);
```

The metadata result includes named owners, zones, balancing authorities,
substations, and superareas. It also gives each bus's area, zone, owner,
substation, and balancing authority numbers, plus available coordinates.
Repeated `Bus` sections update only the fields they supply. These records are
read-only metadata; GridDyn does not model their control or reporting behavior.

## OPF cost data

When the parent is a `GridDynOptimization` and optimization is enabled in the
build, `loadAux` also reads generator costs into the optimization model. Cubic
PowerWorld input/output records with a zero `GenIOD` term become polynomial
cost curves in MW and dollars per hour. The mapping includes `GenIOA`, `GenIOB`,
`GenIOC`, `GenFuelCost`, `GenFixedCost`, `GenVariableOM`, and
`GenCostMultiplier`. Records with a nonzero cubic term or nonzero
`GenExtraFuelCost`, `GenExtraOMCost`, or `GenCostShift` are left without an
imported curve because GridDyn's DC OPF cannot represent those adjustments
faithfully. A regular `GridDynSimulation` still imports the physical network
without economic data.

In Python, request an optimization simulation to retain the curves:

```python
sim = gd.load("network.aux", type="optimization")
result = sim.optimization.opf(optimizer="highs")
```

PowerWorld generator `<SUBDATA BidCurve>` rows contain MW breaks and marginal
prices. The reader integrates those prices into total-cost breakpoints, using
the first price below the first break and the last price through the generator
maximum. These become GridDyn piecewise-linear cost curves. Convex curves can
be solved by the native DC OPF; nonconvex curves require the HiGHS backend.
HiGHS is recommended for large AUX OPF cases and mixed quadratic/piecewise
objectives; the native active-set solver can fail on a singular KKT system for
such a combination even when HiGHS solves the same model.
This import does not enable PowerWorld's commitment, reserve, area, or interface
OPF controls.

## Scope

The `loadAux` network reader currently imports:

- Case MVA base, areas, bus identifiers/names/voltage bases, starting voltages
  and angles, slack status, voltage limits, zones, and fixed bus shunts.
- Generator status, P/Q dispatch, limits, machine base, voltage setpoint, and
  local or remote voltage regulation. Local regulation uses the supplied bus
  voltage as its initial target, matching the RAW import; remote regulation
  uses the generator's target. A local `0/0` reactive-limit pair is treated as
  unspecified, as in the RAW reader. The power-flow-only generator option is
  honored.
- ZIP loads and fixed or stepped/continuous shunts.
- AC lines and two-winding transformers, including status, series impedance,
  charging, ratings, tap, phase angle, magnetizing admittance, and practical
  automatic controls.
- Three-winding transformers through the explicit PowerWorld star bus. When
  `TransformerWinding` branch rows are present, `3WXFormer` augments those
  three legs rather than creating duplicates. Winding taps are normalized from
  nominal kV to each terminal bus's base kV.

The reader skips unrelated PowerWorld data and script sections. It does not
import dynamics, DC transmission lines, FACTS devices, generator step-up
impedances, or the full range of PowerWorld control and contingency semantics.
Those should be added as separate, focused extensions.

## Case comparisons

The supplied ACTIVSg500, ACTIVSg2000, and ACTIVSg10k AUX files import and solve.
ACTIVSg10k imports 10,000 buses and 12,706 links, matching its RAW topology.
Both imports completed power flow in the October 2026 comparison. Across 9,700
buses with common IDs, the solved voltage magnitudes differ by at most
`0.00073` pu and by `0.000020` pu on average. The 300 explicit AUX star buses
use different IDs from the RAW reader's synthetic star buses and are excluded
from those voltage statistics.

The 10k convergence failure exposed several import differences. The decisive
one was the active phase shifter at 28737–28745: its AUX `XFRegMin/Max` values
were passed as per-unit power instead of MW, and its default from-side meter
had the wrong control direction. Its model limits were 2500–2800 pu instead of
25–28 pu on a 100 MVA base. Normalizing that control made the full AUX case
converge. The investigation also aligned local zero/zero generator reactive
limits, local voltage targets, and one three-winding physical tap (`0.4` on a
345/138 kV winding) with the RAW reader.

The solved models still differ in a few controls. For example, the AUX export
marks two LTCs near buses 40704 and 40783 as automatic while their RAW records
are manual. The AUX reader also infers PQ at bus 50458 where RAW explicitly
marks PV. These differences do not prevent the 10k power flow from converging,
but the AUX and RAW solutions should not be treated as identical.

The Memphis AUX file imports 993 buses and 1,381 links; its matching RAW import
also fails a GridDyn power flow. The larger ACTIVSg25k and Midwest24k AUX
files have been checked for successful network import, not for convergence.

## Reader tests

`FileReaderTests` contains portable AUX fixtures for buses, areas, generators,
ZIP loads, shunts, branches, remote voltage regulation, three-winding star
legs, phase-shifter MW controls, and case metadata. The small network fixture completes a
power flow. A routing test covers an explicit uppercase `.AUX` format through
the `std::unique_ptr` overload of `loadFile`. Run them with
`--gtest_filter=SolverReaderTests.Aux*`.

The full ACTIVSg10k power-flow and metadata tests run when
`GRIDDYN_ACTIVSG10K_AUX` is set to the path of `ACTIVSg10k.aux`; they are
skipped when that external case is not available. They check the 10,000-bus
and 12,706-link import, convergence, and hierarchy references.
