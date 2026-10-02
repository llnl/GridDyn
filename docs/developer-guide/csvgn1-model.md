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
T4 dx2  = u1 - x2
u2      = (T2/T4)u1 + (1 - T2/T4)x2
u       = clip(K u2, VMIN, VMAX)
rB      = (u - B)/T5
```

The thyristor state follows `rB` until it reaches a reactor limit. At the
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
requires `K > 0`, `T1,T2 >= 0`, `T3,T4,T5 > 0`, and `0 <= RMIN <= MBASE`.

## Initialization

Given initial voltage `V0` and GridDyn model-output target `q0` in per unit on
MBASE, the reactor command needed to reproduce the power-flow output is

```text
B0 = CBASE/MBASE - q0/V0^2
e0 = B0/K
Vref = V0 - e0
x1 = e0
x2 = e0
B  = B0
```

`q0` follows GridDyn's model-output sign convention; an SVC injecting positive
reactive power has a negative `q0`. The lead-lag blocks have unity DC gain, so
these states give zero derivatives when the command lies within its limits.
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
  comparison test checks all DYR parameters, power-flow initialization,
  residuals, Jacobian, and a short run. An external PSS/E trajectory comparison
  remains future validation.

The implementation is the [GenModelCSVGN1 class](classgriddyn_1_1genmodels_1_1GenModelCSVGN1.html) in
`src/griddyn/genmodels/GenModelCSVGN1.h` and
`src/griddyn/genmodels/GenModelCSVGN1.cpp`; the positional DYR mapping is in
`src/fileInput/gridDynReadDYR.cpp`.
