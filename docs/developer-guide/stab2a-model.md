@page stab2a_model_equations STAB2A model equations

# STAB2A model equations and implementation scope

`StabilizerStab2a` implements the ASEA power-sensitive stabilizing unit in
[OpenIPSL STAB2A](https://github.com/OpenIPSL/OpenIPSL/blob/9a595ebf/OpenIPSL/Electrical/Controls/PSSE/PSS/STAB2A.mo).
The reference is pinned to commit `9a595ebf`. The factory type is `pss/stab2a`.
The generator supplies terminal electrical power `PELEC` on its machine MVA
base, including stator resistance losses, and routes the output to the
exciter's stabilizing-signal input.

## Equations

The unlimited transfer function is

```text
W(s) = K2*T2*s / (1 + T2*s)
H(s) = -W(s)^3 * (K3/(1 + T3*s) + K4) * (K5/(1 + T5*s))^2
```

The minus sign belongs to the first of the three washouts. Each washout has
the same `K2` and `T2`; both output lags have gain `K5`.
With positive time constants, GridDyn uses this six-state realization:

```text
T2*x1' = PELEC - x1       w1 = -K2*(PELEC - x1)
T2*x2' = w1 - x2          w2 =  K2*(w1 - x2)
T2*x3' = w2 - x3          w3 =  K2*(w2 - x3)
T3*x4' = K3*w3 - x4
T5*x5' = K5*(x4 + K4*w3) - x5
T5*x6' = K5*x5 - x6
VSS = clip(x6, -HLIM, HLIM)
```

The output is an algebraic state. Its clamp does not freeze or limit the
internal filters. Initialization sets `x1 = PELEC0`, the other filter states
to zero, and `VSS = 0`, matching OpenIPSL's steady-state initialization.

`T3 = 0` replaces its lag with `x4 = K3*w3`. `T5 = 0` replaces both output
lags with their gain equations. `T2 = 0` makes each washout transfer function
identically zero and removes the three washout states. These are exact
branches; no small substitute time constant is introduced.
All parameters must be finite; time constants and `HLIM` must be nonnegative.
Signed gains are accepted, and `HLIM = 0` holds the output at zero.

State names are `vs`, `washout1`, `washout2`, `washout3`, `lowpass`,
`outputlag1`, and `outputlag2`, omitting bypassed states. Analytic residual
Jacobians and explicit algebraic updates support DAE and partitioned solvers.
The generator host reads a stabilizer's algebraic output from the paired
solver state during differential callbacks, and excludes it from that
callback's differential Jacobian columns. This also corrects the previous
fallback to a stabilizer's first internal filter state.
Output-limit roots include a tolerance margin scaled by any direct
electrical-power feedthrough gain to avoid repeated events from small input
noise near a limit.

## Input records

Both readers attach STAB2A to the matching dynamic generator's `pss` slot.
Load the static network and its machine dynamics before the stabilizer record.
`BUS` and `ID` identify that generator; they are not model parameters.

The DYR record has three identity fields followed by exactly eight model
parameters in this order:

```text
BUS 'STAB2A' ID K2 T2 K3 T3 K4 K5 T5 HLIM /
```

The existing DYD reader also accepts a compact record with the same payload:

```text
models
stab2a BUS "BUSNAME" KV "ID" : K2 T2 K3 T3 K4 K5 T5 HLIM /
```

This documents GridDyn's compact DYD mapping, not arbitrary PSLF variants.
The DYD dispatcher recognizes `stab2a` case-insensitively, preserves these
eight payload values, and invokes the shared DYR model loader. The DYR model
spelling is `'STAB2A'`.
Both readers reject missing, extra, nonnumeric, and invalid parameter values.
The `disable_stabilizers` diagnostic sets `K2 = 0` in either input format.

The native defaults match the pinned OpenIPSL reference:

| Record parameter | GridDyn parameter | Default | Meaning |
| ---------------- | ----------------- | ------- | ------- |
| `K2` | `k2` | 1 | Gain of each input washout; the first has a negative sign |
| `T2` | `t2` | 1 s | Time constant of each input washout |
| `K3` | `k3` | 1 | Gain of the parallel low-pass path |
| `T3` | `t3` | 1 s | Time constant of the parallel low-pass path |
| `K4` | `k4` | 1 | Gain of the direct parallel path |
| `K5` | `k5` | 1 | Gain of each of the two output lags |
| `T5` | `t5` | 1 s | Time constant of each output lag |
| `HLIM` | `hlim` or `h_lim` | 5 pu | Symmetric stabilizing-voltage limit |

The reader regression cases use these corresponding records:

```text
2 'STAB2A' '1' 0.5 0.51 0.31 0.23 0.13 0.81 0.17 0.07 /

models
stab2a 2 "BUS2" 69 "1" : 0.5 0.51 0.31 0.23 0.13 0.81 0.17 0.07 /
```

## Validation

Regression coverage includes independent evaluations of the OpenIPSL block
equations, a closed-form step response of the composed transfer function,
finite-difference Jacobian checks with a nonzero solver derivative
coefficient, all eight time-constant combinations, output-limit entry and
release, factory construction/cloning, invalid input records, and terminal
electrical-power routing with unequal system/machine bases and stator losses.
Coupled generator/exciter tests exercise DAE and partitioned solvers, and a
load-pulse test checks settling. A trajectory captured from an executed
OpenIPSL Modelica case remains pending; equation and analytic-response tests
do not establish direct cross-tool trajectory parity.
