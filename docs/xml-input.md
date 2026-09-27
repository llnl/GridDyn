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
change that load during a run. A recorder selects fields and a sampling period
and writes samples to a file. The current
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
