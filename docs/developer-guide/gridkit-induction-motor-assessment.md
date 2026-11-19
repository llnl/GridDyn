# GridKit induction-motor integration assessment

Assessed on 2026-10-31 against GridKit commit `9c211f32` and GridDyn
commit `794a3729` plus the local working tree. This is a source and architecture
assessment, not an implemented port or a cross-tool trajectory comparison.

The subsequent balanced `MotorLoadDQ` implementation is described in
[Motor DQ model](motor-dq-model.md). The proposals and executable baseline
below record the assessment before that implementation.

The recommended first integration is a **balanced motor with dynamic stator
and rotor fluxes, implemented as a new `GridLoad` in `src/griddyn/loads/`**.
Use a proposed name such as `MotorLoadDQ` / `motor_dq`. The original GridKit
component needs equation repair and a different connection framework before
it can be used directly. Its value is retaining stator electrical dynamics;
GridDyn already models induction motors, including multiple rotor circuits.

## Existing models and the additional capability

| Model                                      | Dynamic electrical/mechanical states                                                                                                      | Terminal interface and intended use                                                                                                                                       |
| ------------------------------------------ | ----------------------------------------------------------------------------------------------------------------------------------------- | ------------------------------------------------------------------------------------------------------------------------------------------------------------------------- |
| `MotorLoad` (`motor`, `motor1`)            | One slip state; electrical power is an algebraic voltage/slip characteristic                                                              | Balanced phasor load for inexpensive electromechanical response and stall/reset behavior                                                                                  |
| `MotorLoad3` (`motor3`)                    | Slip plus two transient internal-voltage states; two algebraic stator-current variables                                                   | Balanced single-cage transient motor model                                                                                                                                |
| `MotorLoad5` (`motor5`)                    | Slip plus four transient/subtransient internal-voltage states; two algebraic stator-current variables                                     | Balanced motor with additional rotor-circuit dynamics; supports Type-A/Type-B data and a regularized single-cage limit                                                    |
| `CIM5`, `CIM6`                             | The `MotorLoad5` electrical DAE, with saturation and model-specific speed/torque laws                                                     | Native PSS/E-style motor models; factory support exists, motor DYR mapping remains absent                                                                                 |
| `WECCMotor3`                               | The CIM6 electrical DAE, direct CMPLDW parameters, and timed protection groups                                                            | Composite Motor A/B/C, including allocation-aware initialization and trip/reclose fractions; despite its name, it inherits five differential electrical/mechanical states |
| `MotorDLoad`                               | Performance curves and stall/restart/protection logic; optional voltage/thermal states                                                    | Single-phase air-conditioner compressor aggregate for composite-load and delayed-voltage-recovery studies                                                                 |
| GridKit `PowerElectronics::InductionMotor` | Five internal currents, apparently stator alpha/beta/zero and rotor alpha/beta; external speed and angle also have differential equations | Instantaneous phase-voltage inputs, phase-current residuals, and shared shaft variables; a single-cage circuit prototype                                                  |

GridKit's five internal variables are **not** equivalent to GridDyn's
five-state `motor5`. The former retains stator currents and one rotor circuit;
the latter retains transient/subtransient rotor modes while treating stator
currents algebraically. Counting states alone obscures this difference.

A corrected circuit model could support stator electromagnetic transients,
motor energization, waveform current offsets, and torque ripple under
appropriate supply and mechanical-load conditions. GridDyn's existing models
already cover much of balanced voltage-sag, deceleration, stall, and
aggregate recovery behavior. For CMPLDW and compressor studies, the existing
WECC and Motor D models also supply protection behavior absent from GridKit's
motor. A new flux model should be justified by the stator-transient capability.

## GridKit source findings

The source is at
[`GridKit/Model/PowerElectronics/InductionMotor/InductionMotor.cpp`](https://github.com/ORNL/GridKit/blob/9c211f32/GridKit/Model/PowerElectronics/InductionMotor/InductionMotor.cpp).
The constructor sets five internal and five external variables. External
residuals identify three phase currents, shaft acceleration, and
`d(angle)/dt = speed`. The internal equations identify the current ordering
used in the comparison above; the source does not document that ordering.

| Finding                                 | Evidence and consequence                                                                                                                                                                                                                                                                                             |
| --------------------------------------- | -------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------- |
| Invalid internal derivative index       | The zero-sequence row reads `yp_int_[7]` although there are five internal variables. It cannot legitimately address an eighth internal derivative, even if adjacent system storage happens to make the read accessible.                                                                                              |
| Apparent incorrect mutual coupling      | The first stator row couples to derivative 1 and the second to derivative 0. Given the rotor rows and phase-current reconstruction, mutual inductance should couple stator alpha to rotor alpha (index 3), and stator beta to rotor beta (index 4). This must be checked against an authoritative machine reference. |
| Initialization is a stub                | `initialize()` returns zero without establishing a loaded running point or an explicit starting condition. The constructor still has TODOs for a component case and correctness test.                                                                                                                                |
| Manual Jacobian is a stub               | `evaluateJacobian()` returns zero without populating entries, and the component leaves the manual nonzero count at its base default of zero. Dependency-tracking template instantiations exist; they do not establish working numerical derivatives or correct physical equations.                                   |
| No built-in resisting torque or damping | The shaft residual contains inertia and electromagnetic torque. A connected component could contribute load torque through the shared shaft residual, but the motor by itself does not specify a loaded operating point.                                                                                             |
| Shaft state classification needs review | GridKit's `PowerElectronics::SystemModel::tagDifferentiable()` copies internal tags and assumes shared node variables are algebraic. This motor places speed and angle derivatives on external variables, so the assembled shaft ownership/classification needs explicit treatment.                                  |

The Clarke voltage transform and current reconstruction use a beta-axis sign
opposite to a common convention. That alone is not a defect. Rotation and
torque signs must be checked with that convention throughout; copying a
standard dq equation into only one row could introduce a new sign error.

No motor-specific example or correctness test was found under the inspected
GridKit `tests/` and `examples/` directories. The presence of C++ source and
a CMake target establishes a compiled prototype, not a validated motor.

## Parameter and equation relationship

For an unsaturated single cage, the intended physical inductance relation
on each alpha/beta axis is

```text
psi_s = (Lls + Lms) * i_s + Lms * i_r
psi_r = Lms * i_s + (Llr + Lms) * i_r
```

This gives a symmetric two-by-two inductance matrix. A flux-state
implementation can invert that matrix to obtain stator and rotor currents,
then evaluate electrical and shaft derivatives. Positive leakage inductances
and consistent physical bases make the linear matrix nonsingular. Saturation
would require a separately specified nonlinear extension.

| GridKit parameter | Relation to existing GridDyn motor data                                                                                                                |
| ----------------- | ------------------------------------------------------------------------------------------------------------------------------------------------------ |
| `Rs`              | Stator resistance `r`, after conversion to the motor impedance base                                                                                    |
| `Rr`              | Rotor resistance `r1`, on the same referred circuit/base                                                                                               |
| `Lls`             | Stator leakage reactance `x = omega_b * Lls / Z_base` for SI inductance                                                                                |
| `Llr`             | Rotor leakage reactance `x1 = omega_b * Llr / Z_base`                                                                                                  |
| `Lms`             | Magnetizing reactance `xm = omega_b * Lms / Z_base`                                                                                                    |
| `RJ`              | Appears to be shaft inertia from the acceleration residual; interpret and document units before using `H = J * omega_mech_base^2 / (2 * S_motor_base)` |
| `P`               | Appears to be pole count from the `P/2` speed factor and torque coefficient; it is not GridDyn's load active power `P`                                 |

Use `omega_mech_base = omega_b / pole_pairs`, and distinguish physical
inertia, electrical angular speed, mechanical angular speed, RMS voltage,
and the amplitude-invariant Clarke transform. GridKit's constructor does
not provide enough unit documentation to assume these conversions blindly.

With consistent per-unit single-cage data, the existing `MotorLoad3`
parameter reduction is

```text
x0   = x + xm
xp   = x + x1 * xm / (x1 + xm)
T0p  = (x1 + xm) / (omega_b * r1)
```

These relations provide a useful steady-state and stator-quasisteady
comparison target. They do not make the two models dynamically identical:
the full model retains stator derivatives that `motor3` eliminates.

## Recommended integration

Add a **new sibling load derived from `GridLoad`**, with a provisional
`MotorLoadDQ` class and distinct `motor_dq` factory name. Put its implementation
in `src/griddyn/loads/MotorLoadDQ.{h,cpp}`, add the normal CMake source entries,
and register it with the existing load factories. The name is a proposal;
there is no such model or factory in this assessment.

Use four flux states (`psi_sd`, `psi_sq`, `psi_rd`, `psi_rq`) and rotor speed
or slip. In a balanced model, zero sequence is excluded, and rotor angle need
not be an independent state unless an actual shaft interface uses it. This
is five differential states for a different physical reduction from
`MotorLoad5`.

Derive the equations in a documented synchronous reference frame. Receive
the existing bus voltage/angle/frequency interface and return consumed P/Q
on GridDyn's system base. Include the reference-frame speed terms in both
stator and rotor equations, and establish consistent current, torque, and
reactive-power signs. Simply feeding RMS voltage into GridKit's instantaneous
phase-voltage equations would change the physics.

Direct `GridLoad` inheritance avoids inheriting `MotorLoad3`/`MotorLoad5`
assumptions about algebraic stator currents, slip/voltage state ordering,
and reduced electrical equations. Torque characteristics and
`MotorProtectionGroups` are candidates for deliberate reuse after their
input and reconnection semantics are defined; shared behavior need not
dictate an incompatible electrical-state hierarchy.

Implement power-flow initialization by solving a physically consistent
loaded operating point. Solve for slip and electrical states with the
specified motor rating and mechanical load; let motor reactive consumption
follow the circuit, with explicit compensation when a case must match a
particular bus Q. Provide a separate starting mode with specified initial
speed/flux rather than silently replacing failed equilibrium initialization
with a start.

First expose the model through native XML/load configuration. Once standalone
P/Q allocation, cloning, residuals, and Jacobians work, it can be a child of
`CompositeLoad` / `loadcomposition`, whose existing framework accepts
`GridLoad` objects and sums their terminal contributions. Verify allocated
P, machine rating, and actual consumed P/Q, following the WECC motor
initialization lessons in [Composite load component framework](composite-load-framework.md).
An additional child does not require a new composite container. The current
`CMLDBLU1` mapping describes specific WECC motors; adding this model does not
justify changing that mapping without verified equation/parameter parity.

The synchronous-frame variant would support balanced electrical-transient
studies within GridDyn's phasor-network assumptions. Exact phase-waveform,
point-on-wave, negative/zero-sequence, and neutral effects require a different
network representation and are outside that first integration.

## Where the original circuit form belongs

Preserving the three instantaneous phase voltages, phase-current residuals,
zero-sequence state, and external mechanical shaft requires a circuit/EMT
component family with matching electrical nodes and shaft-state ownership.
This is a broader architecture project. A corrected motor could be an early
component in that family, or a separately coupled circuit subsystem.

`ThreePhaseLoad` does not supply that infrastructure. Its seven electrical
inputs are phase **phasor magnitudes/angles** plus frequency, and its outputs
are phase P/Q. Those are different from GridKit's instantaneous voltage and
current ports. Extending it alone would not implement an EMT network.

## Validation before adopting a model

1. Resolve current indexing, inductance coupling, reference-frame signs,
   units, and mechanical loading against an independent machine reference.
2. Check magnetic-energy consistency, copper losses, torque direction,
   locked-rotor impedance, and running steady-state P/Q. Compare compatible
   single-cage data with `motor3` at a stator-quasisteady operating point.
3. Check loaded equilibrium and explicit start initialization, including
   infeasible load torque and nonzero bus angle.
4. Check residuals and full state/input Jacobians, including the frame-speed
   terms, in coupled DAE and supported partitioned modes.
5. Compare starting, voltage-sag/recovery, and mechanical-load-step trajectories
   with an independent full-order motor. Compare with existing GridDyn models
   over the slower electromechanical portion and explain the stator-transient
   differences. Do not use the unrepaired GridKit source as an oracle.
6. Check composite allocation/base conversion, protection behavior if enabled,
   and time-step sensitivity for the additional fast stator dynamics.

This assessment did not compile or run a GridKit motor, and no model code was
changed. Eight matching tests passed in the existing GridDyn Debug executable;
that executable is dated 2026-10-06 and lacks several tests present in the
current source. This is only a legacy executable baseline, not validation of
the current CIM/WECC implementation or the proposed model.
