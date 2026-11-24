# IEEEG3, WPIDHY, and MNLEX2

GridDyn imports PSS/E DYR records for `IEEEG3`, `WPIDHY`, and `MNLEX2`.
The first two have dedicated native governors; MNLEX2 uses the existing
native underexcitation limiter, with additional exciter signal routes.
Independent PSS/E or Modelica trajectory comparisons remain pending.

## Source and record verification

The November 2026 review checked OpenIPSL commit
`9a595ebfe42696650108dbaa47b112cf7afb24e1` and the records in
`griddyn_test_cases`. `Base.dyr` has 43 IEEEG3 records (14 parameters),
24 WPIDHY records (21 parameters), and seven MNLEX2 records (seven parameters).

PSS/E IEEEG3's hydraulic partial derivatives match the linear turbine in
[OpenIPSL PSAT TGTypeIII](https://github.com/OpenIPSL/OpenIPSL/blob/9a595ebfe42696650108dbaa47b112cf7afb24e1/OpenIPSL/Electrical/Controls/PSAT/TG/TGTypeIII.mo).
It is a separate model from GridDyn's nonlinear `HYGOV4`. The catalog's
HYGOV4/IEEEG3 alias does not establish equation compatibility. The public
[PowerWorld IEEEG3_GE description](https://www.powerworld.com/WebHelp/Content/TransientModels_HTML/Governor%20IEEEG3_GE.htm)
is a PSLF variant with a different turbine parameter suffix; that schema is
not used for PSS/E DYR.

[OpenIPSL WPIDHY](https://github.com/OpenIPSL/OpenIPSL/blob/9a595ebfe42696650108dbaa47b112cf7afb24e1/OpenIPSL/Electrical/Controls/PSSE/TG/WPIDHY.mo)
and [OpenIPSL MNLEX2](https://github.com/OpenIPSL/OpenIPSL/blob/9a595ebfe42696650108dbaa47b112cf7afb24e1/OpenIPSL/Electrical/Controls/PSSE/UEL/MNLEX2.mo)
provide the corresponding block equations. DYR order, especially WPIDHY's
time constants and interleaved curve points, differs from Modelica declaration
order. GridDyn maps these orders explicitly:

```text
IEEEG3: Tg Tp Uo Uc Pmax Pmin Rperm Rtemp Tr Tw a11 a13 a21 a23
WPIDHY: Treg Reg Kp Ki Kd Ta Tb Velmax Velmin Gatmax Gatmin Tw
        Pmax Pmin D G0 G1 P1 G2 P2 P3
MNLEX2: Kf2 Tf2 Km Tm Melmax Q0 Radius
```

Readers reject incorrect field counts, nonnumeric/nonfinite parameters,
missing generator targets, and targets without a dynamic generator.
Native governor names are `ieeeg3` and `wpidhy`. PSLF DYD variants are not
implemented by these additions.

## IEEEG3 equations

With limited gate `g`, droop-lag state `z`, pilot state `v`, and water state `w`:

```text
e = 1 - omega + Rperm/a23 * command - Rperm*g - Rtemp*(g-z)
z' = (g-z)/Tr
v' = (e-v)/Tp
raw_gate' = clamp(v/Tg, Uc, Uo)
g = clamp(raw_gate, Pmin, Pmax)
w' = (g-w)/(a11*Tw)
Pm = (a23-a13*a21/a11)*g + (a13*a21/a11)*w
```

The raw gate integrator is unbounded, as in TGTypeIII; limited gate feedback
and accumulated overtravel determine release from position limits. Initialization
uses `g=Pm0/a23`, nominal speed, zero pilot error, and equilibrium lag states.
`Tp=0` bypasses the pilot lag, `Tr=0` removes transient-droop feedback, and
`Tw=0` gives `Pm=a23*g`. These exact bypass extensions avoid artificial small
time constants. `Tg`, `Rperm`, `a11`, and `a23` must be positive; velocity
limits must allow opening and closing. `Pmax/Pmin` apply to gate position.

## WPIDHY equations and scope

WPIDHY filters `Reg*(PELEC-PREF)`, subtracts speed deviation, and applies a
parallel PID, two `Ta` cutoff lags, and a derivative servo `s/(1+s*Tb)`.
The PID derivative filter uses Modelica's default `0.01 s` time constant.
`Reg` accepts either sign. Velocity limits precede an unbounded gate
integrator and hard `Gatmin/Gatmax` output limits.

The gate-power table is `(G0,0), (G1,P1), (G2,P2), (1,P3)` with linear
interpolation and endpoint extrapolation. Its output passes through
`(1-s*Tw)/(1+s*Tw/2)`, `Pmin/Pmax` limits, and then `-D*(omega-1)` damping.

Terminal electrical power is on machine base and comes from the existing
generator signal route. `PREF` initializes from terminal electrical power.
An initial command offset preserves that reference when GridDyn's mechanical
power command includes machine losses; later command changes pass through.
Equilibrium gate and turbine states use the requested mechanical power.

This implementation requires positive `Treg`, `Ta`, `Tb`, and `Tw`, ordered
gate and power limits, and strictly increasing gate-power curve coordinates.
Twenty-two of the 24 Base.dyr curves satisfy these conditions. Bus **82160**,
IDs **1 and 2**, has `G0=.18`, both intermediate gate/power points zero, and
`P3=.99`. OpenIPSL's table does not define a disabled-breakpoint convention
for this unordered curve. GridDyn rejects it at initialization rather than
inventing intermediate points. Those two records need an authoritative
PSS/E convention or corrected source data before they can run.

## MNLEX2 signal routes

The existing circular P-Q limiter and rate feedback are retained. Native
conditional integration matches current OpenIPSL's default
`useWindupGuard=true` (`LagLimWindupGuard`); its older optional `SimpleLagLim`
branch is not implemented. Machine-base P/Q and independent limit-transition
trajectories still need external validation.

`EXST1` and `EXAC1` now accept additive `VUEL` at the voltage-error summing
point. `ESST1A` accepts it with `UEL=1`, before its input limits; selectors
2 and 3 remain unsupported. Existing SCRX support covers the other observed
MNLEX2 pairing. Initialization subtracts initial VUEL from the reference bias,
and analytic Jacobians include the new input paths and active limit slopes.

## Validation

Component tests check source block calculations, equilibrium residuals,
analytic state/input Jacobians, zero-time bypasses, gate windup, signed REG,
curve segments, cloning, snapshots, and malformed parameters. Explicit
governor stepping uses RK4 substeps bounded by the fastest block time;
water-lag trajectories are checked against analytic exponential solutions.

Reader tests check every DYR parameter position with distinct values and
reject malformed records. Coupled IEEE14 tests check DAE residuals and
DAE/partitioned Jacobians, governor command disturbances, and MNLEX2 activation
through EXST1, EXAC1, and ESST1A. These are native regression checks; they do
not establish independent simulator trajectory equivalence or full-corpus
network compatibility.

The affected `GeneratorComponentTests`, `FileReaderTests`, and
`ModelComparisonTests` executables are registered with CTest's `Continuous`
label and run in GitHub CI. The added fixtures and tests are contained in
this repository; they do not require a checkout of `griddyn_test_cases`.
