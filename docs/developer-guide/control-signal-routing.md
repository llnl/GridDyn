# Controller signal routing

## Scope

`ControlSignalRouting` is a reusable connection and linear signal-math layer
for submodels that retain GridDyn's existing `IOdata` and `IOlocs` callbacks.
`RenewableGenerator` and `DynamicGenerator` use this layer for their submodel
input connections. The routing layer does not contain limiter equations; the
generic `ExcitationLimiter` submodel supplies optional OEL and UEL actions.

The host decides which source supplies each model input. It resolves source
identity during assembly and stores a `ControlSignalRoute` for that input.
The route reads the **current** `StateData` on every callback. This separation
allows a source to be found once without freezing its dynamic value.
Resolved pointers are non-owning. GridDyn's dynamic topology and source
configuration must remain fixed during a solve; rebuild bindings during
dynamic initialization after changing either.

```text
host-specific source resolution
    -> ControlSignalRoute {input index, source readers, gain, offset}
    -> model IOdata and solver input locations
    -> model residual/Jacobian callbacks
```

## Value and derivative contract

Each route has a value reader and, where the input participates in a
Jacobian, a reader for sparse derivatives with respect to solver locations.
Both readers receive the current host inputs and `StateData`, so a computed
signal can provide state-dependent derivatives.
For frame-slot routes, a nonempty sparse derivative list takes precedence over
a direct solver location for that slot.
The shared layer applies `gain * source + offset` to the value and multiplies
each source derivative by the same gain. An unavailable source retains
`kNullVal`; it is not silently converted to zero. The host validates required
connections before dynamic initialization.

For a direct connection with unit derivative, the model receives the real
solver location. A scaled or computed signal receives a private pseudo
location; `ControlSignalInputLocations::assign` expands a model's Jacobian
entry by the chain rule. This is necessary for machine signals such as
`XadIfd`, whose derivative can involve more than one state. The binding
layer performs routing and affine conversion; nonlinear control equations,
limiters, and selectors remain in their concrete models.
The router's pseudo locations occupy a different range from the existing
`DynamicGenerator`'s former machine-signal markers, which have been removed.
The Jacobian adapter uses GridDyn's `MatrixDataCustomWriteOnly` convention:
submodels write entries through `assign` or its checked variants. A submodel
that reads back entries from the matrix needs a different adapter.

Input locations are assembled for each solver mode. A provider may use an
algebraic state from the paired solve during a differential-only callback;
that value does not create a column in the differential Jacobian. Routes
must preserve that distinction when supplying values and derivatives.

## RenewableGenerator pilot

`RenewableComponent::inputPorts()` remains the typed model contract.
`RenewableGenerator` validates unique required providers on every rebuild,
compiles routes after the dynamics records have been attached, and samples
those routes in initialization, residuals, explicit stepping, and Jacobian
calls. The host retains its existing role-specific initialization and timestep
order.
Route selection also rejects a second provider instead of silently replacing
the first one if validation and routing policies diverge.
Validation and route selection still traverse renewable providers separately.
When adding a new renewable source type, update both policies. A future
renewable cleanup should combine them into one binding pass so each source
policy has one implementation.

The compiled routes cover terminal and regulation-bus measurements, named
area sensors, named synchronous-machine speed, scheduled active references,
and outputs of other renewable components. Named sensor pointers used during
explicit stepping are collected once. Routes stay compiled for the initialized
generator's lifetime. Changing a source name, connection, port, or ownership
requires a structural reset and full dynamic initialization. Component
replacement and removal are rejected while routes are compiled. Ordinary
callbacks never rebuild bindings.

This replaces repeated provider discovery in the renewable solver callbacks.
It still allocates per-model `IOdata` results and uses callable readers on
the hot path. Large-case runtime and allocation effects should be measured
before applying the mechanism broadly.

## Route lifetime and structural reset

The target generator contract is to compile source identities and input-port
connections once after assembly. State values, derivative coefficients, and
solver locations are evaluated from the current state and solver mode. A
submodel changing state count or Jacobian sparsity does not by itself change
which source feeds an input. It does require GridDyn's existing solver-layout
or sparse-Jacobian reset, and input locations must be recalculated afterward.
If a computed route gains new derivative dependencies at runtime, its model
must report the Jacobian-pattern change or reserve those nonzeros in advance.

Changing a component, source identity, port declaration, or optional limiter
presence is a routing change. It requires an explicit structural reset at a
simulation safe point: stop solver evaluation, validate and recompile bindings,
rebuild offsets and solver storage, restore consistent initial conditions,
then resume. The existing `GridDynSimulation::reInitDyn()` updates offsets and
solver storage but does not call generator `dynInitializeA`, so it is not a
route-rebuild operation. Both generator hosts provide
`resetSignalRoutesForDynamicInitialization()` to clear their compiled routes
and permit structural edits. It does not reset the simulation, offsets, or
solver state. The caller must invoke full `GridDynSimulation::dynInitialize()`
after edits and before any further dynamic callback. Source values, derivative
coefficients, and solver locations continue to be sampled at each callback.
The DynamicGenerator stabilizer route also checks the source's enabled status
when evaluated. A renewable component's enabled status can affect provider
selection, so changing it may require a structural reset.

## DynamicGenerator integration

`DynamicGenerator` compiles a route table for the machine, exciter, governor,
stabilizer, voltage compensator, isochronous controller, and two optional
excitation limiters. The table maps
each model input to a named slot in a generator signal frame. The host fills
that frame in its existing dependency order: terminal measurements and
setpoints, governor inputs, mechanical power, machine signals, compensated
voltage, stabilizer output, limiter actions, and field voltage. Route tables write the model
input buffers for implicit callbacks, explicit stepping, and controller
initialization. Machine-model initialization retains its separate power-flow
target contract.

The frame carries current values, direct solver locations, and sparse
derivatives for computed machine signals. `ControlSignalRouting::addInput`
binds a model port to a frame slot, including optional gain and offset.
`ControlSignalInputLocations` expands computed routes through the chain rule.
This replaces the former machine-specific pseudo-location translator in
`DynamicGenerator`. Solver locations and derivative coefficients are rebuilt
for each Jacobian assembly; route identities remain fixed.

The optional stabilizer is selected at dynamic initialization. A missing
stabilizer supplies zero to exciter `Vss` with no Jacobian location. A present
but disabled stabilizer also supplies zero; enabling it again uses the same
bound source. The host retains its feedback cache and controller stepping
order. In particular, the machine-signal value snapshot precedes the exciter
field refresh, matching the existing generator equations.

### Optional OEL and UEL routes

`Exciter` now reserves `VUEL` and `VOEL` after `Vss`, leaving every previous
input index unchanged. Both are positive action magnitudes. A missing or
disabled limiter supplies exactly zero and has no solver location. A present
limiter has a stable source identity after `dynInitializeA`; its algebraic
output value and solver location are read on every implicit callback. The
same bindings are used during initialization and explicit stepping. Limiter
inputs are routed from the machine's `XadIfd`, `Id`, `Iq`, `Vd`, and `Vq`
signals, including their sparse chain-rule derivatives. An incompatible
machine measurement raises an initialization error when the limiter evaluates.
In partitioned differential callbacks, the host reads each limiter's output
from the paired algebraic state and omits its algebraic location from that
mode's Jacobian map. Machine signal derivative producers also omit state
columns owned by the paired solver mode; otherwise an algebraic current
offset can alias a differential state column (and vice versa).

`ExcitationLimiter` is one `GridSubModel` type configured with `role=over`
or `role=under` before attachment. Its `threshold` is required; `gain` defaults
to 1 and `max_action` to 10. Its output is an algebraic state with an analytic
Jacobian and an inactive value of zero. For the over role,
`VOEL = clamp(gain * (XadIfd - threshold), 0, max_action)`. For the under role,
reactive injection is `Q = Id*Vq - Iq*Vd` on machine base and
`VUEL = clamp(gain * (threshold - Q), 0, max_action)`. These are simple
instantaneous threshold controls, **not** implementations of WECC OEL1–OEL5C,
BASOEL2, or UEL1/UEL2/UEL2C. Model-specific limiters use the same generator
slots without adding new host signals.

`ExcitationLimiterMNLEX2` is a dynamic underexcitation model based on the
[OpenIPSL MNLEX2 block](https://github.com/OpenIPSL/OpenIPSL/blob/master/OpenIPSL/Electrical/Controls/PSSE/UEL/MNLEX2.mo).
It computes `P=Id*Vd+Iq*Vq`, `Q=Id*Vq-Iq*Vd`, and the circular margin
`(Q0*V²-Q)²+P²-(Radius*V²)²`. A first-order limited output and derivative
feedback produce `VUEL`. Its two differential states and algebraic output
participate in residual, Jacobian, and explicit stepping. Parameters are
`kf2`, `tf2`, `km`, `tm`, `melmax`, `q0`, and `radius`; P and Q must be on
the same machine base as the model parameters. The algebraic `VUEL` output is
clamped to `[0, melmax]`, and the Jacobian uses the active clamp slope; this
prevents solver overshoot in the internal `MEL` state from appearing on the
generator route. The saturation implementation uses conditional integration
at the bounds; OpenIPSL's `SimpleLagLim` uses its own state reinitialization
rule. Their trajectories near a saturation transition have not yet been
compared.

`ExcitationLimiterOEL4C` follows the reactive-power timer and limited PI
structure of [Dynawo OEL4C](https://github.com/dynawo/dynawo/blob/master/dynawo/sources/Models/Modelica/Dynawo/Electrical/Controls/Machines/OverExcitationLimiters/Standard/Oel4c.mo).
It uses `Qref-Q`, a resettable `tdelay`, `kp`, `ki`, and negative `vmin`.
The published block emits a nonpositive correction; GridDyn routes its
positive magnitude because SCRX subtracts `VOEL`. The PI integrator can
retain an action after Q falls below the threshold, as in the source block.
The delay has a one-shot solver root and resets when Q returns below the
reference. A discontinuous parameter event starts the timer at GridDyn's
algebraic-root probe time, which can be one probe step after the
event; this timing still needs comparison with an external implementation.
`qref` is a fixed GridDyn parameter rather than a separately routed input.
It must use the same machine-base reactive-power units as `Id*Vq-Iq*Vd`;
cases using another MVA base need conversion before setting it.
Its saturation uses conditional integration; Dynawo's small hysteresis
margin is not replicated. External trajectory parity and DYR/DYD import
remain open, so this is a native partial OEL4C implementation.

Three additional optional models use the same five limiter inputs and the
existing `VUEL` / `VOEL` exciter routes. They add no host-specific signal ports.
All three are native partial implementations and are available through the
GridDyn XML `<limiter type="...">` factory. None currently has a PSS/E DYR or
PSLF DYD importer mapping.

`ExcitationLimiterUEL1` implements the UEL1 circular characteristic
`VUC = min(|KUC*Vt - j*It|, VUCmax)`,
`VUR = min(KUR*|Vt|, VURmax)`, followed by the PI,
lead-lag, and output limits. It derives the characteristic from the routed
machine `Id`, `Iq`, `Vd`, and `Vq` signals. The model accepts only `KUF=0`
because no AVR stabilizing-feedback signal is in the current limiter port
contract. It also requires the sum-point positive `VUEL` convention, including
a nonnegative output minimum; it is not compatible with a takeover-gate
exciter input. See the [UEL1 model reference](https://www.powerworld.com/WebHelp/Content/TransientModels_HTML/Under%20Excitation%20Limiter%20UEL1.htm).

`ExcitationLimiterUEL2C` currently provides a fixed-profile subset: it
calculates machine-base `P=Id*Vd+Iq*Vq` and `Q=Id*Vq-Iq*Vd`, applies the
selected voltage powers `K1` and `K2` (each 0, 1, or 2), interpolates a
user-supplied piecewise-linear `P/Q` curve, then applies a limited PI output.
Configure at least two strictly increasing pairs with `p0`/`q0` through
`p6`/`q6`; initialization rejects an unset curve. Values beyond its endpoints
hold the endpoint value. This subset does not yet model the standard's voltage
bias logic, input filters (`Tup`, `Tuq`, `Tuv`), reactive-reference filter,
adjustable-gain switch, `KUF`/`KFB` feedback paths, or either output lead-lag
stage. The full parameter set and block diagram are listed in the
[UEL2C model reference](https://www.powerworld.com/WebHelp/Content/TransientModels_HTML/Under%20Excitation%20Limiter%20UEL2C.htm).

`ExcitationLimiterOEL3C` provides a field-current-only summing-point profile.
It filters routed machine `XadIfd` when `TF>0`, evaluates the positive pickup
error `max((KSCALE*Ifd/ITFpu)^K1 - 1, 0)` for `K1=1` or `2`, then forms the
limited proportional/integral action. The internal standard-style correction
is nonpositive; the routed `VOEL` is its positive magnitude to match SCRX's
existing summing point. `OELInput` must be 0 (`Ifd`). The alternate Efd/VFE
inputs and the standard's switch-dependent `IFref` feedback are not modeled;
integral action holds after pickup clears because this subset has no reference
switch or reset logic. The full reference includes these model parameters
and the block diagram: [OEL3C model reference](https://www.powerworld.com/WebHelp/Content/TransientModels_HTML/Over%20Excitation%20Limiter%20OEL3C.htm).

Example XML configuration (curve values must come from the case data):

```xml
<limiter name="uel_circle" type="uel1" role="under"
         kur="1.95" kuc="1.38" kuf="0" kul="40" kui="0.1"/>
<limiter name="uel_curve" type="uel2c" role="under" k1="0" k2="0"
         kul="1" kui="0.1"
         p0="0" q0="-0.3" p1="0.5" q1="-0.25" p2="1" q2="0"/>
<limiter name="field_oel" type="oel3c" role="over" itfpu="1.05"
         kscale="1" tf="0.02" k1="1" koel="1" toel="24" kpoel="1"
         voelmin1="-1" voelmax1="0.66" voelmin2="-1" voelmax2="0"/>
```

These XML records instantiate GridDyn's native equations, not a claim of DYR
or DYD record compatibility. Before using them for validated studies, import
mapping, machine-base confirmation, exciter compatibility checks, and
comparison against an independent implementation remain required.

XML uses `<limiter role="over" threshold="..."/>` and
`<limiter role="under" threshold="..."/>` under a generator. The loader reads
`role` before attaching the object, so both instances occupy their intended
slots. Set the role before calling `DynamicGenerator::add` in C++; a later
role change would alter source identity and is rejected.
Concrete examples use `<limiter type="mnlex2" role="under" .../>` and
`<limiter type="oel4c" role="over" qref="..." .../>`. Their roles are fixed;
the loader rejects an opposite role before attachment.

An exciter declares which action it implements. `SCRX` sums `+VUEL-VOEL` at
its reference error. `ESAC6A` sums `+VUEL` there and does not accept OEL.
Other exciters currently reject an attached limiter during dynamic route
compilation, rather than silently ignoring its output. Neutral zero inputs
preserve their previous behavior when no limiter is attached. Replacing,
adding, or removing a limiter uses the same full dynamic reset contract as
other route changes. A limiter role cannot be changed after its own dynamic
initialization; replace that submodel after resetting routes.

## Verification boundary

- The renewable component suite checks assembly, source selection, named
  measurements, coupled model Jacobians, partitioned solver modes, and
  disturbance trajectories.
- `ControlSignalRouting.ScaledComputedInputPropagatesSparseJacobian` checks
  affine conversion and a signal with two Jacobian dependencies.
- `ControlSignalRouting.HostFrameRoutesValuesAndSparseDerivatives` checks
  frame-slot routing and sparse derivative translation.
- `DynamicGeneratorModelTests.SignalRoutesFreezeUntilFullDynamicInitialization`
  checks the complete model-port table, optional `Vss`/`VUEL`/`VOEL` behavior, and the
  structural reset requirement. The generator and attached stabilizer suites
  exercise residuals, analytic Jacobians, and explicit stepping.
- Limiter tests cover generic and concrete action equations, delay and reset,
  neutral routes, unsupported exciter pairings, SCRX and ESAC6A action points,
  and GENROU/GENSAL–SCRX Jacobians in full and partitioned solver modes with
  both existing limiters attached. MNLEX2 lower and upper bounds and OEL4C
  inactive and saturated branches have direct residual, derivative, and
  Jacobian checks. The added UEL1, UEL2C, and OEL3C unit tests check active
  outputs and output caps under constant explicit inputs, compare selected
  analytic residual slopes with finite differences, and exercise UEL1
  lead-lags, UEL2C voltage scaling, and OEL3C's filtered and unfiltered input
  paths. These are model-level checks; network disturbance trajectories and
  independent reference-model parity for these three additions remain open.
- Benchmark a representative large case before making performance claims;
  routing removes repeated source selection but still uses callable readers.
  `DynamicGenerator` reuses its sparse input maps, computes machine-signal
  derivatives only when an enabled controller uses them, and skips controller
  maps for external output derivatives.

See [RenewableGenerator architecture refinement](renewable-generator-architecture-refinement.md)
for the host's role and port policy and [WECC model coverage audit](wecc-model-coverage-audit.md)
for the separate UEL/OEL model gaps.
