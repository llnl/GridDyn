# GPWSCC reference and implementation basis

GridDyn's `GPWSCC` governor implementation is based on the PSLF model
description supplied with the case-development materials:

> _Governor Model: GPWSCC_, PID Governor-Turbine Model, Powertech Labs.

The source PDF is not vendored in this repository. The working source artifact
is currently:

`C:\Users\top1\Downloads\Governor Model_ GPWSCC.pdf`

## Implemented model

The block diagram defines an intentional speed deadband, `Paux` summation,
droop/power feedback `R`, input filter `1/(1+s Td)`, PID controller
(`Kp`, `Ki/s`, and `s Kd/(1+s Tf)`), optional electrical-power lag `Tt`, gate
servo `Kg/(1+s Tp)`, gate velocity and position limits, a nonlinear `N_GV`
curve, and the turbine transfer function

\[
\frac{1+s A_{turb}T_{turb}}{1+s B_{turb}T_{turb}}.
\]

The turbine lead-lag is represented without differentiating the gate curve:
for `Tturb > 0`, GridDyn stores an internal lag state `z`, with

\[
B_{turb}T_{turb}\dot z=P_{GV}-z,
\qquad
P_{turb}=\frac{A_{turb}}{B_{turb}}P_{GV}+
\left(1-\frac{A_{turb}}{B_{turb}}\right)z.
\]

This is exactly the source transfer function. The final GridDyn mechanical
power is `MWCap/MVABase * Pturb`, because GridDyn exchanges governor signals on
the generator machine base while the PSLF diagram defines its internal gate and
turbine quantities on `MWCap`.

## PSLF DYD support

The DYD reader accepts `GPWSCC`, retains the named `MWCap=<value>` field, and
maps the 31 following positional fields in documented order. The generator
machine base comes from the static SAVE/EPC data and is supplied to the model as
`MVABase`.

The supplied `base080626.dyd` records have every `Gv1/Pgv1` through
`Gv6/Pgv6` value equal to zero. GridDyn interprets that all-zero characteristic
as the identity curve (`P_GV=GV`), a practical compatibility assumption which
should be revisited if a PSLF reference specifies another default curve.

## Explicit implementation assumptions

- `db1=eps=db2=0` bypasses both deadbands exactly.
- The document's block drawing does not specify a discrete hysteresis memory
  convention for `db1`, `eps`, or `db2`. GridDyn uses continuous symmetric
  dead zones; `eps` enlarges the speed deadband release band and `db2` is a
  gate-position dead zone.
- The PID integrator is held when a gate is at a position limit and its
  filtered error would drive the command farther beyond that limit.
- A GridDyn dispatch/setpoint change is applied relative to the initialized
  operating point. `Paux` is initialized as `R` times the electrical-power
  feedback, so a no-event simulation starts at equilibrium.

The dedicated governor tests cover diagram equations, analytic Jacobians,
capacity-base conversion, curve inversion, rate/position limiting, factory
registration, DYD parsing of the named `MWCap` field, and attachment to a
dynamic generator.
