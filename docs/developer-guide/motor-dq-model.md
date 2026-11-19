# Balanced induction motor with stator flux dynamics

`MotorLoadDQ`, registered as load type `motor_dq`, models a balanced,
unsaturated single-cage induction motor. It adds stator electrical dynamics
to the existing load framework. Its five states represent two stator fluxes,
two rotor fluxes, and rotor speed. Existing `motor5`/CIM5/6 models also have
five differential states, but those represent transient/subtransient rotor
circuits with algebraic stator currents.

This model was motivated by GridKit's `PowerElectronics::InductionMotor` at
commit `9c211f32`. That component's indexing and apparent mutual-coupling
errors prevent treating it as an authoritative implementation. The GridDyn
model uses the symmetric single-cage inductance relation, consistent
synchronous-frame signs, and independent steady-state equivalent-circuit and
energy checks. It is not a claim of GridKit trajectory parity. The original
[integration assessment](gridkit-induction-motor-assessment.md) records the
source defects and the architectural comparison.

## Equations and conventions

The frame rotates at the fixed system base angular frequency `omega_b`.
The bus angle is its phasor angle in that frame. Terminal voltage is

```text
v_sd = V * cos(angle)
v_sq = V * sin(angle)
```

Rotor speed `w` is normalized electrical rotor speed, or equivalently
mechanical rotor speed divided by synchronous mechanical speed. Slip is
`s = 1 - w`. Bus frequency estimates do not independently change the frame;
frequency departures act through evolving bus angles. This avoids combining
a moving frame with voltages expressed in a fixed frame.

Circuit resistances and reactances are per unit on the motor MVA/terminal
voltage base. Flux is normalized so that the inductance relation uses the
base-frequency reactances directly:

```text
Ls = xls + xm
Lr = xlr + xm
psi_sd = Ls * i_sd + xm * i_rd
psi_sq = Ls * i_sq + xm * i_rq
psi_rd = xm * i_sd + Lr * i_rd
psi_rq = xm * i_sq + Lr * i_rq
```

Currents follow from the inverse of the symmetric two-by-two matrix on each
axis. Its determinant is `xls * xlr + xm * (xls + xlr)`. The positive leakage
parameters keep it nonsingular. The state equations are

```text
d(psi_sd)/dt = omega_b * (v_sd - rs * i_sd + psi_sq)
d(psi_sq)/dt = omega_b * (v_sq - rs * i_sq - psi_sd)
d(psi_rd)/dt = omega_b * (-rr * i_rd + (1 - w) * psi_rq)
d(psi_rq)/dt = omega_b * (-rr * i_rq - (1 - w) * psi_rd)
Te = psi_sd * i_sq - psi_sq * i_sd
Tm = load_torque * (torque_constant + torque_linear * w + torque_quadratic * w^2)
dw/dt = (Te - Tm - damping * w) / (2 * H)
```

Positive terminal active and reactive power denote consumption:

```text
Pmotor = v_sd * i_sd + v_sq * i_sq
Qmotor = v_sq * i_sd - v_sd * i_sq
Psystem = scale * Pmotor
Qsystem = scale * Qmotor
scale = motor_MVA / system_MVA
```

The terminal model is a `GridLoad`; it provides P/Q and their derivatives
through the normal balanced phasor bus interface. No instantaneous phase
voltages, zero-sequence network, or external shaft ports are introduced.

## Initialization and configuration

By default, `initialization=running` solves the low-slip running branch.
With an automatically determined mechanical load, the initial electrical
operating point matches the allocated active power at the initial bus
voltage; `load_torque` is then calibrated to balance electromagnetic torque
and damping. Motor Q follows the equivalent circuit. Specify compensation
separately when a case must reproduce an assigned aggregate Q.

An explicit `load_torque` selects a torque-balanced running point instead;
its actual P follows from the circuit and rating, and need not equal the
configured P allocation. Failure to find a loaded running branch raises an
invalid-parameter error. The model does not silently substitute a start for
an infeasible running point.

`initialization=starting` holds `initial_speed` while solving the electrical
power-flow state. Dynamic initialization then sets both stator and rotor
fluxes to zero at that speed, representing motor energization. The default
starting mechanical load is `load_torque=1` with a quadratic torque curve.
The power-flow load is the equivalent-circuit demand at the prescribed
speed; it is not the configured P allocation. An explicit rating is useful
for this mode. Starting does not impose equilibrium on the shaft.

| Parameter                                              | Default                                | Meaning                                                                                            |
| ------------------------------------------------------ | -------------------------------------- | -------------------------------------------------------------------------------------------------- |
| `rs`, `rr`                                             | 0.01, 0.02                             | Positive stator and referred rotor resistance, per unit                                            |
| `xls`, `xlr`, `xm`                                     | 0.10, 0.10, 3.0                        | Positive leakage and magnetizing reactance, per unit                                               |
| `h`                                                    | 0.5                                    | Positive inertia constant in seconds on the motor MVA base                                         |
| `rating`                                               | Automatic                              | Positive motor MVA rating; numeric default units use the repository's MW/MVA base convention       |
| `load_factor`                                          | 0.8                                    | If no rating is supplied, motor MVA = allocated MW / load factor; must be in (0, 1]                |
| `torque_constant`, `torque_linear`, `torque_quadratic` | 0, 0, 1                                | Nonnegative coefficients; at least one must be positive                                            |
| `damping`                                              | 0                                      | Nonnegative viscous shaft-torque coefficient                                                       |
| `load_torque`                                          | Calibrated for running; 1 for starting | Nonnegative nominal mechanical torque on the motor base; can also be changed for a load-step study |
| `initialization`                                       | `running`                              | `running` or `starting`                                                                            |
| `initial_speed`                                        | 0                                      | Starting speed, per unit; must be in [0, 1)                                                        |

Configure circuit, rating, and initialization settings before initialization.
The scale is fixed from the rating or original allocated P, rather than
changing when the network voltage or electrical demand changes. Getters
also expose `speed`, `slip`, and `scale`; state names are `psi_sd`, `psi_sq`,
`psi_rd`, `psi_rq`, and `speed`.

Example native XML:

```xml
<load name="industrial_motor" type="motor_dq">
  <p>0.7</p>
  <rating>100</rating>
  <h>0.5</h>
  <rs>0.01</rs>
  <rr>0.02</rr>
  <xls>0.10</xls>
  <xlr>0.10</xlr>
  <xm>3.0</xm>
</load>
```

Native JSON and YAML use the same `load` object with `type: motor_dq`. In a
GridDyn CSV `load` section, put `motor_dq` in the `type` column and identify
the parent in the `bus` column. The load can be nested in `loadcomposition`
alongside ZIP and other loads. PSS/E and PSLF records retain their existing
motor mappings. See [Composite load component framework](composite-load-framework.md)
for allocation and feeder conventions.

## Numerical behavior and validation

Power flow solves five algebraic equilibrium variables. Dynamic modes use
five differential variables and no motor algebraic-current states. Analytic
state, terminal-voltage, and terminal-angle Jacobians cover the DAE and
partitioned interfaces. Stator electrical modes require smaller steps than
the reduced motor models; use an implicit DAE solver for normal studies and
check time-step sensitivity when using explicit partitioned integration.

The energy consistency check uses

```text
Wmag = (psi_s dot i_s + psi_r dot i_r) / (2 * omega_b)
Wmech = H * w^2
Pmotor = d(Wmag + Wmech)/dt
         + rs * |i_s|^2 + rr * |i_r|^2 + w * Tm + damping * w^2
```

Tests in `test/componentTests/testMotorDQ.cpp` cover this identity at a
perturbed state, analytic residual/P/Q Jacobians, XML, JSON, YAML, and CSV
loading, equivalent-circuit initialization, energization, voltage-sag/recovery,
partitioned evaluation, composite allocation, cloning, local checkpoint
restore of load torque, and invalid or infeasible configuration. Independent
full-order motor trajectory validation remains open.

Local validation on 2026-10-31 used a fresh MSVC Debug CMake build of
`NetworkComponentTests`. All 62 network-component tests passed, including
all eight new motor tests and the existing motor/composite-load checks.

The first implementation is unsaturated and single cage. It has no timed
protection, aggregate fractional reconnection, hard shaft stop, or dedicated
stall/reset state machine. The shaft equation can decelerate through zero
under an imposed constant opposing torque; configure mechanical loads
accordingly. WECCMotor3 and MotorDLoad remain the purpose-specific models
for their existing composite-load/protection studies.
