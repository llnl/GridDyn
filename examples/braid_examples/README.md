# GridDyn + XBraid Examples

## 179busDynamicTest_BraidWithEvents

This test has an event applied to the system at every half-second, that is at
t = 0.5, 1.5, 2.5, ..., 9.5. This event is a load applied at a specific bus with
a magnitude of 0.1 megawatts. The load and affected bus can be changed by editing
the file `179busDynamicTest_BraidWithEvents.xml`.

## 179busDynamicTest_BraidNoEvents

The is the same as the 179busDynamicTest_BraidWithEvents examples but with no
events.

## XBraid options

In a GridDyn XML input file, parallel-in-time integration with XBraid is enabled
with the option

```xml
<solver name="braid">  </solver>
<defdae>braid</defdae>
```

All the relevant XBraid parameters are set in the XBraid parameter file. The
default name for this file is `braid_params.ini`. An alternative input file
name can be specified in the GridDyn XML input file with

```xml
<solver name="braid">
   <configfile>braid_params.ini</configfile>
</solver>
<defdae>braid</defdae>
```

The set of event locations can be set with

```xml
<solver name="braid">
   <configfile>braid_params.ini</configfile>
   <discontinuities>0.5,1.5,2.5,3.5,4.5,5.5,6.5,7.5,8.5,9.5</discontinuities>
</solver>
<defdae>braid</defdae>
```

The time domain size and the time-step size are controlled with

```xml
<timestart>0</timestart>
<timestop>10</timestop>
<timestep>0.048</timestep>
```
