# COMP dynamic-file mapping

GridDyn imports the single-reactance `COMP` voltage-compensator record into
the existing `VoltageCompensatorIeeeVC`. It retains that native class and
factory name; no additional physical model or differential states are added.

## Equations and source

The [PowerWorld COMP reference](https://www.powerworld.com/WebHelp/Content/TransientModels_HTML/Voltage%20Compensator%20COMP.htm)
defines one parameter, `Xe`, and the equation

```text
Vc = Abs(Vt - j*Xe*It)
```

The existing IEEEVC equation is `Abs(Vt + (Rc + j*Xc)*It)`, so COMP imports
as `Rc=0`, `Xc=-Xe`. Both signs of Xe are accepted; zero gives uncompensated
terminal-voltage magnitude. The sign reversal is part of the mapping.

The model consumes machine-base d/q currents through the existing generator
signal route. The reader preserves the magnitude of Xe and does not rescale
it using the system MVA base. Independent confirmation of the original
PSS/E parameter-base convention remains pending; the native regression
tests verify this implementation's machine-base convention with different
machine ratings on the same 100-MVA network.

## Record schemas

PSS/E DYR:

```text
BUS 'COMP' ID Xe /
```

The compatible compact DYD form uses the normal bus/machine header:

```text
comp BUS "BUS NAME" BASE_KV "ID" : Xe /
```

DYD model names are case-insensitive, and the payload may continue on the
next line. Both forms require exactly one finite numeric parameter and an
existing dynamic generator. Malformed numbers, missing or extra parameters,
and missing bus/machine targets fail with source-record context. The COMP
DYD mapping rejects named parameters, including `mva=` and `mwcap=`; general
PSLF parameter/default variants have not been established by this mapping.

## Integration and validation

The existing compensator output replaces the exciter's terminal-voltage
measurement. Generator initialization calculates compensated voltage before
initializing the exciter, allowing its reference bias to retain the original
equilibrium. Existing algebraic equations and analytic Jacobians handle the
compensator in DAE and partitioned solver modes.

The reader regressions use checked-in DYR/DYD fixtures with positive,
negative, and zero Xe, independent complex-voltage calculations from terminal
P/Q, nondefault machine ratings, equilibrium residuals, and DAE/partitioned
Jacobians. They also cover malformed records and static-generator rejection.
External PSS/E trajectories and full-corpus network compatibility remain
separate validation work.
