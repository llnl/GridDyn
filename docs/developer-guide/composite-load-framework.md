# Composite load component framework

`loadcomposition` is the first layer for building composite dynamic loads from existing
GridDyn load models. It accepts nested `GridLoad` objects, sums their terminal P/Q, and lets
their states, residuals, roots, and Jacobian contributions flow through GridDyn's standard
component hierarchy. Nested objects use the existing GridDyn XML/model reader and load factory.
The DYR reader also has an initial PSS/E `CMLDBLU1` path for the currently supported motor
components, with the restrictions listed below.

The parent P/Q values are the constant-power portions to distribute among the components, in
GridDyn's existing `GridLoad` convention. They do not include any separate ZIP current or
admittance contributions. Component fractions are one-based and may be supplied as `fraction1`,
`fraction2`, etc. (the more explicit `componentfractionN` spelling is also accepted). Fractions
must be in `[0, 1]`. If any fractions are omitted, the remaining share is divided equally among
those components. If every fraction is supplied, they must sum to one. Each fraction scales the
component's P/Q and any configured ZIP current/admittance terms. A component may have an
explicit reactive base when its power factor requires Q but the parent load has zero Q.
Those ZIP terms add to the constant-power portions; configure them before power-flow
initialization. The CMLDBLU1 reader normalizes motor and electronic fractions to sum to one
when their specified total exceeds one, leaving no static remainder.

Example XML:

```xml
<bus name="loadbus">
  <load name="mixed_load" type="loadcomposition">
    <P>1.0</P>
    <Q>0.3</Q>
    <fraction1>0.35</fraction1>
    <fraction2>0.65</fraction2>
    <load name="induction_motor" type="motor3">
      <H>0.5</H>
    </load>
    <load name="static_component" type="zip" />
  </load>
</bus>
```

`component1`/`component2` string parameters can also instantiate a registered load type from
the load factory. Nested load objects are preferable when a component needs its own model
parameters.

This layer combines components at the same bus. For a PSS/E `CMLDBLU1` record with nonzero
network parameters, the reader now creates internal AC buses, the series transformer and feeder,
substation and feeder shunts, and moves the composite to the feeder-end bus. It converts the
distribution-base impedances and susceptances to GridDyn's system base and estimates load-end
voltage and feeder real-power losses from the original bus operating point. `Fb` splits the
estimated feeder compensation between the feeder ends. The Bss estimate uses the low-side
bus voltage, where that shunt is connected.

The network initialization is a first pass, not yet the full WECC initialization procedure. WECC
computes feeder compensation iteratively from the load devices and network power flow, while this
reader estimates it from the original bus P/Q and series reactance. The static remainder uses the
estimated load-end voltage and the shared `CompositeLoad` P/Q allocation; motor reactive power can
therefore differ from a complete CMPLDW initialization. PSS/E `LTC=-1` uses GridDyn's stepped
transformer voltage adjustment during power-flow initialization and is inactive during dynamics.
`LTC=1` and nonzero LTC line-drop compensation (`Rcmp`/`Xcmp`) are rejected because GridDyn does
not yet implement their delayed discrete dynamic behavior and compensation calculation.

The new WECC-specific three-phase `WECCMotor3` and single-phase `MotorDLoad` are additive load types.
The current PSS/E `CMLDBLU1` reader maps type-1 Motor A/B/C components to `MotorDLoad`, type-3
Motor A/B/C components to `WECCMotor3`, Motor D to `MotorDLoad`, electronic load to
`ElectronicLoad`, and the static remainder to an `IEELLoad`. Static polynomial coefficients and
frequency sensitivities map to IEEL terms; the third voltage term is the residual coefficient.
The active-power base is normalized at the estimated load-end voltage, and `PFs` sets the
reactive reference. The static reactive base is scaled to the shared `CompositeLoad` allocation
when the original load has Q; an explicit component reactive base handles zero original Q.
`ElectronicLoad` maps `PFel`, `Vd1`, and `Vd2`; the fixed `CMLDBLU1` record has no `Frcel` field,
so the reader uses the reference default of 0.8. The generic load also accepts independent IEEL-style
active/reactive voltage curves and P/Q frequency coefficients; these default to flat curves and zero
frequency sensitivity for CMPLDW. The optional curve and frequency parameters are available through
the native load configuration, not additional `CMLDBLU1` fields.
The fixed A/B/C record blocks omit `Th1t`, `Th2t`, and `Tv`; type-1 mappings use the defaults in
Table A.10 of the NERC reference. The reader still requires one existing load at the system bus;
load IDs are not yet addressable. Motor D extensions beyond the implemented WECC equations are
rejected. It does not support PSLF records.

## Follow-on model work

1. Replace the first-pass feeder compensation estimate with the iterative WECC initialization that
   balances the original system-bus P/Q against component power, transformer/feeder losses, and
   shunts. Verify initialization across three-phase motors, Motor D, electronic, and static-only
   combinations.
2. Implement delayed discrete LTC motion and line-drop compensation, then broaden topology tests to
   fixed and initialization-only tap modes.
3. Extend the current CMLDBLU1 mapping to PSS/E's
   supported load-ID scopes. Validate every field against the target PSS/E or PSLF format before
   broadening DYR acceptance.
4. Add load-shedding behavior, then connect component protection and
   reactive-compensation changes to the composite load's distribution network.
5. Add PSLF `CMPLDW` and modular `CMPLDW2` readers after their record formats and field mappings
   are verified.
6. Explore whether CMPLDW should become a first-class subsystem load model that contains its
   internal buses, transformer, feeder, shunts, and component loads. Compare that design with a
   reusable builder that registers ordinary network objects in the owning `GridArea`; account for
   topology indexing, area semantics, object lifecycle, cloning, and serialization before choosing
   an API. The current reader generates ordinary GridDyn network objects and does not define a
   dedicated subsystem container.

`WECCMotor3` uses the five-state CIM5/6-family electrical equations with CMPLDW's direct
parameter set and speed-exponent torque equation. Its timed fractional trip/reconnect groups are
separate from the existing induction-motor defaults. `MotorDLoad` implements the CMPLDW compressor
power curves, definite/inverse-time stall behavior, restart, undervoltage trip, contactor, thermal
relay, and voltage-lag behavior. These component models have targeted equation, Jacobian, and behavior
checks. The three-phase tests now assert a positive allocated P, motor MVA base, and
electrical consumption in addition to residual and Jacobian consistency.
The transformer-feeder topology has a DYR test with a Motor D composite, power-flow
initialization, and a DAE Jacobian check. Full reactive compensation, dynamic LTC, remaining end-use
models, coordinated tripping effects, and complete PSS/E/PSLF record support remain gaps.

## Three-phase motor initialization and remaining merge work

- `WECCMotor3` now transfers the composite's allocated GridLoad P to `Pmot` and sets a
  positive motor MVA base from `LFm`. The inherited five-state motor equations had
  subtransient self-feedback terms wired to the opposite subtransient state; correcting
  those residual, derivative, and Jacobian terms permits a positive-slip, positive-P
  operating point. Initialization solves the electrical circuit and locates the
  low-slip root for allocated P, with motor Q determined by that circuit. The base is
  fixed from allocated P rather than the terminal P after power flow, which may differ
  slightly when the network voltage settles. An explicit motor MVA rating takes
  precedence over `LFm`; both cases are covered by operating-point and DAE checks.
- `Tppo=0` and `Lpp=Lp` both select the single-cage limit. The five-state solver
  represents the removed subtransient circuit with `Lpp=Lp` and a 1e-7 s time constant,
  matching the existing CIM single-cage regularization. DYR cases for both encodings
  pass power flow, DAE residual/Jacobian checks, a short dynamic run, and an operating-point
  equivalence check. This is a stiff numerical approximation of the reduced-order model;
  a dedicated three-state implementation may improve long-run performance.
- Feeder compensation is still a one-pass estimate. Its Bss voltage location is corrected,
  but the estimate does not reconcile the original system-bus P/Q with device P/Q and
  losses, or decrease compensation when component protections trip.
