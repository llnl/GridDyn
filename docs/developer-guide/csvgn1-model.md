@page csvgn1_model_equations CSVGN1 model equations

# CSVGN1 model equations and implementation scope

`GenModelCSVGN1` implements the PSS/E CSVGN1 static var source through GridDyn's
generator-model interface. The DYR record attaches it to a `DynamicGenerator`,
but its electrical behavior is a controlled shunt susceptance. It does not
represent a synchronous rotor, mechanical-power input, or exciter field.

## Equation source

The equations below are a continuous-time realization of the CSVGN1 block
diagram in [PowerWorld Simulator 17 Block Diagrams, page 256](https://www.powerworld.com/files/Block-Diagrams-17.pdf),
under “Machine Model CSVGN1.” The diagram identifies the model as supported by
PSS/E, labels its states as regulator 1, regulator 2, and thyristor, and notes
that the voltage is measured on the high side of an integrated generator step-up
transformer when present. The source gives a block diagram rather than numbered
equations; the equations here spell out GridDyn's selected state realization
and sign/base conventions.

The limited regulator follows OpenIPSL `CSVGN1` and `LeadLagLim` at commit
`9a595ebf` (2026-10-25), including the correction in `be1629ed` for `T2 = 0`.
The output limit feeds the regulator state back, which changes recovery from
saturation compared with clipping an otherwise unlimited lead-lag.

The unsaturated regulator and thyristor transfer functions shown by the diagram
are

```text
Gr(s) = K (1 + s T1)(1 + s T2) / ((1 + s T3)(1 + s T4))
Gt(s) = 1 / (1 + s T5)
```

The state equations below realize these blocks, with the voltage and reactor
limits applied explicitly.

## Differential equations

Let `V` be the voltage input in per unit, `Vref` the voltage reference, `x1` and
`x2` the regulator states, and `B` the reactor susceptance command on the
machine base. GridDyn currently uses

```text
e       = V - Vref
T3 dx1  = e - x1
u1      = (T1/T3)e + (1 - T1/T3)x1
u2      = K (T2/T4)u1 + (1 - T2/T4)x2
u       = clip(u2, VMIN, VMAX)
T2 dx2  = u - x2
rB      = (u - B)/T5
```

These equations apply for `T2,T4,T5 > 0`. The gain `K` appears once, before
the limited regulator. Its internal state is on the regulator output base.
With `T2 = 0` and `T4 > 0`, the second block is a non-windup limited lag:

```text
u       = clip(x2, VMIN, VMAX)
r2      = (K u1 - x2)/T4
```

Its derivative is zero at a limit when `r2` would push outward, and follows
`r2` as soon as the input turns back. With `T2 = T4 = 0`, the second block is
the algebraic equation `x2 = clip(K u1, VMIN, VMAX)` and `u = x2`. A positive
lead `T2` with zero lag `T4` is rejected.

With `T5 = 0`, the reactor is algebraic: `B = clip(u, Bmin, 1)`. Both bypasses
retain algebraic states so the DAE and partitioned solvers can represent the
output dependencies without artificial fast poles. Local state vectors and
state names put algebraic states first, followed by differential states.

With `T5 > 0`, the thyristor state follows `rB` until it reaches a reactor limit. At the
minimum `Bmin = RMIN/MBASE`, its derivative is zero when `rB < 0`; at the
maximum `Bmax = 1`, its derivative is zero when `rB > 0`. The implementation
also clamps `B` to `[Bmin, 1]` when calculating shunt output.

## Reactive output and sign convention

The fixed capacitor contributes `CBASE/MBASE` pu susceptance. The reactor
subtracts `B`, so positive capacitive injection on the machine base is

```text
Qsvc = (CBASE/MBASE - clip(B, Bmin, 1)) V^2
```

GridDyn's `GenModel` output convention is negative for machine injection.
Therefore the value returned by this model is `-Qsvc`. The surrounding
generator/network interface applies the MBASE-to-system-base scaling.

The model's DYR record order is:

```text
BUS, 'CSVGN1', machine-ID, K, T1, T2, T3, T4, T5,
RMIN, VMAX, VMIN, CBASE
```

`K` is the regulator gain; `T1` through `T5` are seconds; `RMIN` and `CBASE`
are Mvar; `VMIN` and `VMAX` are per-unit limits. `MBASE` comes from the RAW
generator card and is used as the reactor base (`RBASE = MBASE`). The model
requires `K > 0`, `T1,T2,T4,T5 >= 0`, `T3 > 0`, `T4 > 0` when `T2 > 0`,
and `0 <= RMIN <= MBASE`.

## Initialization

Given initial voltage `V0` and reactive injection target `q0` in per unit on
MBASE, the reactor command needed to reproduce the power-flow output is

```text
B0 = CBASE/MBASE - q0/V0^2
e0 = B0/K
Vref = V0 - e0
x1 = e0
x2 = B0
B  = B0
```

`q0` is positive for capacitive injection; the model returns `-q0` in GridDyn's
network load convention. These states give zero derivatives and satisfy any
algebraic bypass equations when the command lies within its limits.
Initialization rejects a power-flow reactive output that cannot be represented
inside the reactor and regulator limits.

## Current scope and validation

- The PSS/E block diagram includes a supplementary `VOTHSG` input. GridDyn does
  not currently connect that signal, so it is assumed to be zero.
- The implementation uses the voltage supplied at the generator-model input.
  A separate remote or high-side voltage measurement is not currently wired.
- The model is a positive-sequence RMS controlled-susceptance model; it does not
  simulate thyristor firing waveforms or switching harmonics.
- The DYR loader maps CSVGN1 onto the PSS/E machine record. The current
  tests check DYR parameters, power-flow initialization, saturation feedback,
  residual and output Jacobians, zero-time-constant branches, and short DAE
  and partitioned runs. An external PSS/E trajectory comparison
  remains future validation.

The implementation is the [GenModelCSVGN1 class](classgriddyn_1_1genmodels_1_1GenModelCSVGN1.html) in
`src/griddyn/genmodels/GenModelCSVGN1.h` and
`src/griddyn/genmodels/GenModelCSVGN1.cpp`; the positional DYR mapping is in
`src/fileInput/gridDynReadDYR.cpp`.
