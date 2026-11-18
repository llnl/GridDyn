# GenModel8 equation review

Reviewed on 2026-10-31 against GridDyn commit `794a3729` and the working
tree. The review combines source comparison, deductions from the implemented
equations, and numerical evaluation of the current C++ model. It identifies
an initialization sign error and incomplete stator-flux coupling. The
analytic Jacobians are consistent with the implemented equations; that
consistency does not establish a complete physical eighth-order machine.

This review adds an explicit diagnostic executable and documentation. It
does not change generator equations or claim an external trajectory match.

## References and their limits

| Reference | Checkout | Use in this review |
| --- | --- | --- |
| [OpenIPSL PSAT Order6](https://github.com/OpenIPSL/OpenIPSL/blob/9a595ebfe42696650108dbaa47b112cf7afb24e1/OpenIPSL/Electrical/Machines/PSAT/Order6.mo) and its [base machine](https://github.com/OpenIPSL/OpenIPSL/blob/9a595ebfe42696650108dbaa47b112cf7afb24e1/OpenIPSL/Electrical/Machines/PSAT/BaseClasses/baseMachine.mo) | `9a595ebfe42696650108dbaa47b112cf7afb24e1` | Four rotor-state equations, the additional `Taa` term, steady-state initialization, and generator dq conventions |
| [PowerDynamics SauerPaiMachine](https://github.com/JuliaEnergy/PowerDynamics.jl/blob/93792632ae7862f9c0fdaf681763c9c89b4cece4/src/Library/Machines/SauerPaiMachine.jl) | `93792632ae7862f9c0fdaf681763c9c89b4cece4` | An independent source implementation with optional stator dynamics, rotor-frame speed terms, electrical torque, and explicit flux/current constraints |
| [GridDyn GenModel8](../../src/griddyn/genmodels/GenModel8.cpp) | Current working tree | The actual equations and initialization evaluated by the diagnostic |

The two external reference files were clean in their local checkouts.
OpenIPSL Order6 treats stator currents algebraically and does not validate
eighth-order stator dynamics. SauerPaiMachine uses different rotor-state
definitions and conventions; it supplies a structural reference, not an
established one-to-one parameter or trajectory mapping. The unfinished
GridKit circuit synchronous machine is not an equation authority for this
review.

## State and reference conventions

`GenModel8` has two algebraic states `[Id, Iq]` and eight differential states
`[delta, w, Edp, Eqp, Edpp, Eqpp, psi_d, psi_q]`. Its inherited terminal
transformation is

```text
Vd = -V * sin(delta - theta)
Vq =  V * cos(delta - theta)
```

The positive generated terminal power is `Pgen = Vd*Id + Vq*Iq`. The model
returns its negative through GridDyn's consumption-sign output interface.
All numerical current, voltage, and power values below use the standalone
model's per-unit base. `omega_b` denotes system base angular frequency.

The implemented stator and mechanical equations are

```text
d(psi_d)/dt = omega_b * (Vd + Rs*Id + w*psi_q)
d(psi_q)/dt = omega_b * (Vq + Rs*Iq - w*psi_d)
Te = psi_d*Iq - psi_q*Id
d(delta)/dt = omega_b * (w - 1)
2H * dw/dt = Pm - Te - D*(w - 1)
```

The variable called `electricalPower` in the implementation is the flux
cross-current electromagnetic torque `Te`. This distinction matters away
from nominal speed.

## Confirmed findings

### 1. Stator flux does not determine terminal currents

The algebraic residual and `algebraicUpdate()` solve

```text
0 = Vd + Rs*Id + (Xqpp - Xl)*Iq - Edpp
0 = Vq + Rs*Iq - (Xdpp - Xl)*Id - Eqpp
```

Neither equation uses `psi_d` or `psi_q`. Consequently, the extra stator
fluxes affect the shaft torque, but have no direct electrical feedback to
the stator currents or rotor electrical-state equations. They can affect
currents indirectly through subsequent speed and angle changes.

The compiled diagnostic confirms this at a perturbed operating point:

| Frozen-state experiment | Change in the magnitude of the dq current vector |
| --- | --- |
| Increase `psi_d` by 0.1 and decrease `psi_q` by 0.1, then update currents | Exactly 0 |
| Reduce terminal voltage magnitude by 10%, keeping all differential states fixed | 0.864843553567 pu |

For a full flux-state model with finite, nonsingular winding inductances,
fixed stator and rotor flux states determine winding currents. A finite
voltage step changes flux derivatives; it cannot produce this immediate
current jump. SauerPaiMachine explicitly includes flux/current constraints
in addition to its stator voltage differential equations.

The existing initialization suggests the following possible algebraic
closure:

```text
0 = psi_d - Eqpp - (Xdpp - Xl)*Id
0 = psi_q + Edpp - (Xqpp - Xl)*Iq
```

At `w=1`, combining those constraints with zero stator-flux derivatives
recovers the existing two voltage equations. This is a useful consistency
deduction, not a validated repair. The internal-voltage definitions,
inductance/passivity relationships, and rotor differential equations must
be established together before adopting that closure.

### 2. The nonzero-Taa initialization term has the wrong sign

Define

```text
r  = Taa / Tdop
dr = Tdopp*(Xdpp - Xl) / (Tdop*(Xdp - Xl))
A  = Xd - Xdp - dr*(Xd - Xdp)
B  = Xdp - Xdpp + dr*(Xd - Xdp)
```

The implemented two direct-axis rotor equations are

```text
Tdop  * d(Eqp)/dt  = -Eqp + A*Id + (1-r)*Ef
Tdopp * d(Eqpp)/dt = -Eqpp + Eqp + B*Id + r*Ef
```

Their equilibrium requires

```text
Ef  = Eqpp - (Xd - Xdpp)*Id
Eqp = Eqpp - B*Id - r*Ef
```

Initialization instead assigns `Eqp = Eqpp - B*Id + r*Ef`. Both the
implemented differential equations and OpenIPSL Order6 support the minus
sign. In OpenIPSL's convention, the direct-axis current has the opposite
sign; the field-voltage term retains the minus sign.

The diagnostic uses `Tdop=5`, `Tdopp=0.05`, `Taa=0.02`, and initial
`Ef=1.82137475289`. The incorrect term introduces an `Eqp` state error of
`2*r*Ef`, producing

```text
d(Eqp)/dt  = -2*r*Ef / Tdop  = -0.00291419960463
d(Eqpp)/dt =  2*r*Ef / Tdopp =  0.291419960463
```

| Initialization experiment | Maximum absolute residual |
| --- | --- |
| Default `Taa=0` | 5.55e-16 |
| `Taa=0.02`, current implementation | 0.291419960463 |
| Same point with only the deduced `Eqp` correction applied to the diagnostic state | 2.78e-16 |

The production model was not modified for this experiment. The default
`Taa=0` hides this defect.

### 3. Singular parameter combinations are accepted

The ratios divide by `Xdp-Xl`, `Xqp-Xl`, `Tdop`, and `Tqop`; the derivative
also divides by `Tdopp`, `Tqopp`, and `H`. The inherited parameter setters
do not provide model-specific guards for all of these requirements.

With `Xdp=Xl=0.12`, the diagnostic's initialization completes without
rejecting the configuration and creates one nonfinite state. Parameter
validation should reject singular reactance/time-constant combinations
before initialization. A deliberate zero-inertia/fixed-speed mode would
need explicit handling rather than division by zero.

## Checks that passed, and what they establish

The audit used the actual C++ residual, derivative, current update, and
Jacobian methods. At a perturbed point with nonzero angle, speed `0.91`,
unequal subtransient reactances, and solver coefficient `cj=2.3`:

| Check | Maximum absolute error |
| --- | --- |
| Central differences of all residual state/input derivatives, including the DAE derivative term | 2.81e-8 |
| Central differences of terminal P/Q state/input derivatives | 6.61e-11 |
| Stator power identity below | 5.55e-17 |

The identity follows directly from the stator differential equations:

```text
Pgen + Rs*(Id^2 + Iq^2)
    = w*Te + (Id*d(psi_d)/dt + Iq*d(psi_q)/dt)/omega_b
```

It checks that the stator rotation terms and torque expression are mutually
consistent. It does not establish a positive magnetic-energy function or
total stator/rotor/shaft energy conservation. Those require a validated
winding-flux/current relationship, which is the missing part of this model.

For `Xl=0`, the four rotor equations have the PSAT Order6 form after mapping
`Id_grid=-Id_OpenIPSL`, `Edp_grid=-e1d`, `Edpp_grid=-e2d`, and keeping the
q-axis voltage states and field voltage unchanged. For nonzero `Xl`, a
formal algebraic mapping shifts every reference reactance by `-Xl`.
Whether that shift represents the intended machine parameter convention
must be resolved before claiming parameter parity. This source comparison
does not establish equivalence to the Sauer-Pai rotor-state formulation.

All 32 existing `GenModelTests` passed in a fresh MSVC Debug CMake build of
`GeneratorComponentTests`. The dedicated existing `GenModel8` test checks
that its algebraic update satisfies its algebraic residual; it does not
test full stator-flux coupling or nonzero-`Taa` equilibrium.

## Reproduce and proceed

The standalone [GenModel8Audit source](../../test/diagnostics/genModel8Audit.cpp)
is excluded from the default build and is not registered as a passing CTest
test. Build and run it explicitly:

```text
cmake --build build --config Debug --target GenModel8Audit --parallel 4
build/bin/Debug/GenModel8Audit.exe
build/bin/Debug/GeneratorComponentTests.exe --gtest_filter=GenModelTests.* --gtest_brief=1
```

Its exit status indicates successful execution; the printed measurements
are the audit results. Follow the repository's local build guidance if
MSBuild needs its environment workaround. On the reviewed machine, loading
the Visual Studio tools and using serial MSBuild completed the builds after
the ordinary parallel invocation exited before compilation with no errors
reported. No `Path`/`PATH` collision was reported or workaround applied.

The supported first repair is the `Taa` initialization sign, together with
a nonzero-`Taa` equilibrium regression check. Parameter guards are also
straightforward once supported degenerate modes are specified. Completing
stator dynamics is a larger change: establish a winding or independently
referenced rotor-flux formulation, update the current constraints and their
Jacobians, verify total energy consistency and the stator-quasisteady
limit, then compare disturbed trajectories with an independent full-order
machine.

The mechanical input convention also needs an explicit decision. The
current swing equation treats the input named `Pm` as torque; its mechanical
power is `w*Pm`. For a true power input, torque is `Pm/w`, with separately
defined behavior near zero speed. Retaining the established near-nominal
torque approximation can be reasonable, but it should be documented rather
than silently carried into wide-speed or starting studies.
