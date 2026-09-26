# GridDyn renewable model support: architecture and test plan

The [architecture refinement](renewable-generator-architecture-refinement.md)
is the current recommendation for submodel inheritance, signal binding, and
`RenewableGenerator::add`. The [component inventory](renewable-generator-component-mapping.md)
records the earlier, more rigid role-class sketch for comparison.

## Scope and conclusion

This plan targets the physical and control behavior of renewable models shipped
in the adjacent ANDES checkout, `C:\Users\phlpt\Documents\andes`, as inspected
on 2026-09-25. ANDES is a useful catalog and comparison implementation, not an
architecture to reproduce. GridDyn should use its own generator, submodel,
factory, initialization, and solver conventions. Each externally selectable
dynamic model needs its own GridDyn model object and factory identity so
dynamics-file readers can construct, replace, and connect it independently.
Internal blocks and solver helpers may be shared. The first useful milestone
is independently selectable `REGCA1` converter and `REECA1` electrical-control
models, with `REPCA1` as a separately selectable plant controller.

The hypothesis is partly right. GridDyn needs a converter **electrical model and
renewable control interface**, since its existing `GenModelInverter` is a
voltage-behind-impedance, power-angle model, whereas ANDES `REGCA1` injects
current using `Ipcmd` and `Iqcmd`. A `RenewableGenerator : Generator` sibling of
`DynamicGenerator` is the recommended host for GridDyn renewable submodels.
`DynamicGenerator` hard-codes the synchronous-machine `Eft`/`Pmech` contract in
initialization, input caching, stepping, and Jacobian assembly; subclassing it
would require replacing most of those methods. Inherit static power-flow and
bus behavior from `Generator`, and extract genuinely common dynamic traversal
or solver-location code into shared helpers when both classes need it.

A separate top-level **wind generator** class is unnecessary for this model
family. The ANDES Type-3 example uses the same `REGCA1` converter behavior as
its solar example, with additional drivetrain, aerodynamic, pitch, and torque
behavior. GridDyn can provide those as optional submodels of one renewable
generator. The ANDES solar case also serves as a Type-4 electrical equivalent;
the mechanical path is optional when a study requires it.

Keep the host's required electrical slot more general than a converter.
Future Type-1/Type-2 wind models use a directly connected induction generator,
with mechanical/turbine models and, for Type 2, external rotor-resistance
control. `RenewableGenerator` should accept a `TerminalElectricalModel`
whose concrete implementation can be a converter or an induction generator.
Its other components are optional and validated against typed port contracts.
GridDyn's induction `MotorLoad` family may offer reusable
numerical calculations but cannot be attached directly as generator submodels.

**Compatibility target:** implement each supported model's governing dynamics,
parameter meaning, control modes, limits, and initialization at the electrical
bus, and match its externally observable response. Internal state
names, submodel boundaries, initialization order, and file representation may
differ from ANDES when GridDyn's design calls for it. Document every supported
mode and every deliberate behavior difference. Independent model identity is
part of compatibility: replacing `REECA1` must not require replacing `REGCA1`,
and removing `REPCA1` must leave the converter and electrical controller usable.

| ANDES behavior family | Natural GridDyn component |
| --- | --- |
| `REGCA1`, `REGCP1` | Separately selectable converter electrical models; optional PLL input for `REGCP1`. |
| `REECA1` variants, `REECB1` | Separately selectable renewable electrical controllers producing active/reactive current requests. |
| `REPCA1` | Independently selectable optional plant controller using GridDyn bus/line measurements. |
| `WTDS`, `WTDTA1` | Optional single- or dual-mass wind drivetrain. |
| `WTARA1`, `WTARV1`, `WTPTA1`, `WTTQA1` | Separately selectable aerodynamic, pitch, and torque models; share internal blocks where useful. |
| `REGCV1/2`, `REGF1/2/3`, `PVD1` | Alternative converter/distributed-resource behaviors sharing the host where their electrical contract fits. |

The proposed `RenewableGenerator` owns one terminal electrical model and a
role-indexed collection of optional components. `REGCA1`,
`REECA1`, and `REPCA1` are separate `GridSubModel` objects with separate
parameter storage, states, factory names, clone behavior, and solver callbacks.
One `RenewableComponent` contract declares each model's role and typed ports;
`TerminalElectricalModel` adds the common terminal P/Q contract. The host
handles bus injection and routing; it does not embed any
one model's equations. A dynamics reader creates one object per model record,
attaches it by bus and machine ID, and validates compatible combinations before
dynamic initialization. Input record order should not determine whether the
assembly succeeds.

## Evidence from the two codebases

| Area | Current behavior | Design consequence |
| --- | --- | --- |
| GridDyn electrical model | `src/griddyn/genmodels/GenModelInverter.cpp` solves a single internal angle from `Pmech`, field voltage, and impedance. | Preserve it for its present use; implement a separate `REGCA1`-style current-injection model. |
| GridDyn generator composition | `src/griddyn/generators/DynamicGenerator.cpp` fixes submodels to machine, exciter, governor, PSS and a few controls; it routes `Eft` and `Pmech` to the machine throughout initialization and solver evaluation. | Create a `Generator` sibling with the small set of renewable signal ports required by its configured submodels, including solver-location and Jacobian support. |
| Existing renewable class | `VariableGenerator` supplies a source and one control block, but its `SOURCE_LOC=5` and `CONTROL_BLOCK_LOC=6` overlap `DynamicGenerator`'s `PSET_LOC=5` and `VSET_LOC=6`. Its residual and Jacobian methods also call the base traversal and then evaluate the extra blocks again. | Retain it for the resource-to-power use case; do not use it as the grid-facing renewable generator host. Its fixed non-adjustable-power behavior is unsuitable for some converter controllers. |
| ANDES model graph | `andes/models/group.py` defines `RenGen`, `RenExciter`, `RenPlant`, `RenGovernor`, `RenAerodynamics`, `RenPitch`, and `RenTorque`. Models refer to one another by indices and external variables. | Treat these as behavior and dependency information. In GridDyn, own submodels under their generator, connect their explicit input/output contracts, and avoid a general-purpose replica of ANDES' index/reference system. |
| ANDES Type-3 path | `WTDTA1`/`WTDS` read electrical `Pe` and supply generator speed `wg` to `REECA1`; `WTARA1`/`WTARV1` set mechanical `Pm`; `WTPTA1` controls pitch; `WTTQA1` adjusts `Pref` and initialization. | Wind needs bidirectional electrical/mechanical/control coupling, including initialization and cross-model Jacobians. A `Pmech` pointer alone is insufficient. |
| Imports | `gridReadAndes.cpp` loads static ANDES `PV`/`Slack` as `DynamicGenerator` but has no renewable sections. `gridDynReadDYR.cpp` rejects unsupported DYR models; `gridDynReadDYD.cpp` routes supported PSLF records through the DYR model loader. | Make each renewable model constructible from a separate dynamics record. Map DYR first, then other requested dynamics formats, without making runtime model classes depend on file syntax. |

## Comparison with `VariableGenerator`

The user manual describes `VariableGenerator` as a way to feed weather or other
resource data through a source and filter into solar or wind **available power**.
That is a useful upstream role. The ANDES models here primarily describe the
grid-facing converter, electrical controls, and (for Type 3) turbine feedback.
The two concepts can be connected without giving them the same class.

| Question | `VariableGenerator` today | Proposed `RenewableGenerator` |
| --- | --- | --- |
| Parent and control contract | Inherits `DynamicGenerator`, including the `Eft`/`Pmech` machine chain. | Inherits `Generator`; defines current commands, converter P/Q, plant references, and optional speed/mechanical ports. |
| Resource input | Owns one `Source` and one `GridBlock`; the block's scalar output replaces `Pset`. The source-to-block connection is still marked TODO in `VariableGenerator::residual`. | Can accept an optional available-power signal, then apply curtailment and converter limits; resource modeling is a separate submodel. |
| Power flexibility | Constructor clears `ADJUSTABLE_P` and `LOCAL_POWER_CONTROL`; adjustable-capacity methods return zero, and `generationAdjust` does nothing. | Dispatchability and curtailment are capabilities of the configured controls, rather than fixed class-wide assumptions. |
| Wind feedback | Has no ports for `Pe`, `wg`, `wt`, pitch, shaft torque, or return `Pref`. | Optional wind component graph uses those ports and participates in initialization and Jacobian assembly. |
| Submodel indexing | Extra slots 5 and 6 conflict with inherited slots 5 and 6. `DynamicGenerator` creates an input vector of size 6, so slot 6 is outside that vector during its generic traversal. | Allocate stable role-indexed locations and validate each model's input and output contract. |
| Current execution path | Base residual/Jacobian already traverses registered subobjects; overrides subsequently evaluate the source and control block again. Base initialization likewise traverses them before the override initializes them explicitly. | Each submodel is initialized and evaluated once per phase with explicit dependency ordering. |

**Decision:** keep `VariableGenerator` as a legacy resource-to-power abstraction,
and do not derive the grid-facing renewable host from it. A weather/availability source or
repaired block from it could later feed `RenewableGenerator`'s active-power
availability port. The existing factory maps `generator` types `variable` and
`renewable` to `VariableGenerator`; introduce an unambiguous new type (for
example `renewable_dynamic`) first, document migration, and change old aliases
only with compatibility tests. If `VariableGenerator` is changed, add focused
regression coverage for its slot indexing, one-time evaluation, and resource
signal flow.

The ANDES fixtures make the intended assembly concrete: `ieee14_solar.xlsx`
contains one each of `REGCA1`, `REECA1`, and `REPCA1`; `ieee14_wt3.xlsx` adds
one each of `WTDTA1`, `WTARA1`, `WTPTA1`, and `WTTQA1`; `ieee14_wt3n.xlsx` has
five such chains. `andes/cases/ieee14/README.md` labels the solar case as a
Type-4 equivalent and the wind case as Type 3.

## Implementation sequence

1. **Define a behavior matrix and independent GridDyn contracts.** For each
   externally selectable model, record its equations, parameters, base units,
   supported flags, dependencies, and dynamics-file fields. Give `REGCA1`,
   `REECA1`, and `REPCA1` separate `GridSubModel` implementations and factory
   names. Define `RenewableComponent` for role and typed port declarations
   and `TerminalElectricalModel` for the shared electrical boundary. Keep
   electrical and plant controllers as independent concrete models. Reuse
   GridDyn control blocks, `IOdata`, lifecycle methods, and solver
   location/Jacobian conventions.
   Unsupported modes and incompatible combinations fail clearly.
2. **Implement the common generator and converter path.** Add
   `RenewableGenerator : Generator` to own and independently replace its
   configured submodels. Implement a `REGCA1` converter submodel (`P=V*Ip`,
   `Q=V*Iq` with GridDyn's injection convention),
   voltage-dependent current logic, and limits. Retain `Generator`'s power-flow
   behavior and initialize dynamic states from its solved P/Q operating point.
   Implement state sizes, residual, derivative, outputs, roots, and analytic
   Jacobians in the same solver modes used by other GridDyn dynamic components.
   Share generic traversal with `DynamicGenerator` only where it removes real
   duplication. Register a distinct generator factory type while preserving
   legacy `variable`/`renewable` aliases until compatibility is resolved.
3. **Add independent electrical and plant controls.** Implement `REECA1` and
   `REPCA1` as separately owned and replaceable models with their own
   parameters, states, and factory registrations. `REECA1` provides its flags,
   filters, current priorities, voltage dip response, and active/reactive
   references. `REPCA1` optionally uses GridDyn remote measurements and sends
   plant-level reference increments to the controller. Neither needs to inherit
   `Exciter`, whose output contract is field voltage. Add `REECB1`, `REECA1E`,
   and `REECA1G` as selectable alternatives, sharing internal blocks where
   their equations permit it. Verify converter-only, converter plus electrical
   control, and converter plus electrical plus plant combinations.
4. **Add optional wind behavior.** Start with single-mass `WTDS` behavior,
   then dual-mass `WTDTA1`, and add aerodynamic, pitch, and torque behavior
   represented by `WTARA1`/`WTARV1`, `WTPTA1`, and `WTTQA1`. Give each
   separately named dynamics-file model its own replaceable object; share
   internal control blocks without merging their public identities. Route
   converter electrical power to the drivetrain, mechanical
   power into the shaft equations, shaft speed into electrical/torque control,
   and the resulting active-power reference back to the converter controller.
   Initialize the coupled assembly to one consistent equilibrium using
   GridDyn's dynamic initialization phases; duplicate ANDES' initialization
   sequence only if it is needed to reproduce the physical operating point.
   Converter-only solar and Type-4 electrical equivalents remain valid.
5. **Build dynamics-file construction with each model.** Configure each model
   through GridDyn's normal factory/input path and add `REGCA1`, `REECA1`, and
   `REPCA1` DYR record loaders as their implementations land. Use the existing
   bus/machine-ID lookup, converting the associated steady-state generator to
   `RenewableGenerator` while preserving its operating point, status, rating,
   and identity. Permit records in any order by attaching independently and
   validating the final assembly before dynamic initialization. Extend the
   DYD path through its existing DYR dispatch mechanism where its schema is
   compatible; add explicit parameter conversion where it is not. Other
   dynamics formats can create the same model objects through their own
   adapters. Keep ANDES JSON and native `.xlsx` import optional. File syntax
   and ANDES' group hierarchy remain outside the simulation classes.
6. **Extend model families.** Add `REGCP1` behavior with optional PLL;
   `REGCV1`/`REGCV2` and `REGF1`/`REGF2`/`REGF3` as appropriate converter
   alternatives; then distributed `PVD1` and related storage/EV behaviors if
   those are included in the support target. Reuse the generator host and
   GridDyn building blocks while preserving each model's distinctive external
   dynamics.

## Verification and acceptance gates

For each supported model behavior, retain a parameter/feature map and a small
GridDyn fixture. Use independently calculated operating points and model
identities for component tests. Use frozen ANDES traces as one external check
of bus-level behavior, with a pinned ANDES commit, solver settings,
disturbance, and per-unit bases. The test suite should not require a live
ANDES installation, nor assume matching internal state names or addresses.

1. **Component behavior:** Check equilibrium, P/Q injection signs, current
   magnitude and priority limits, voltage-dependent branches, reference
   tracking, wind energy balance, and limit release using direct expected
   values. Cover every flag claimed as supported. Finite-difference GridDyn
   Jacobians on both sides of relevant limits, away from discontinuities.
2. **GridDyn integration:** Verify separate factory construction and parameter
   setting for `REGCA1`, `REECA1`, and `REPCA1`; independent replacement of
   each model; submodel ownership and cloning; output/state locations;
   disabled or missing optional models; invalid combinations; one-time
   evaluation; and power-flow-to-dynamic initialization. Exercise full DAE
   and partitioned differential/algebraic modes. Keep synchronous generator
   and `VariableGenerator` behavior covered when shared code changes.
3. **System response:** Create GridDyn fixtures corresponding to ANDES
   `ieee14_solar`, `ieee14_wt3`, `ieee14_wt3n`, `kundur_wtds`,
   `kundur_wtdta1`, and `ieee14_pvd1` as their behaviors are implemented.
   Compare steady-state P/Q and bus voltage, then observable response to a
   voltage sag/recovery, frequency event, active-power reference change, and
   wind pitch/torque disturbance. Use peak error, RMS error, event timing,
   and final value for bus P/Q and relevant control outputs. Set tolerances
   from solver/reference reproducibility, and explain any accepted difference
   caused by deliberately different internal implementation.
4. **Dynamics-file regression:** Load each of `REGCA1`, `REECA1`, and
   `REPCA1` from its own DYR record, singly where valid and in valid
   combinations, including reversed record order and different compatible
   replacement models. Check field order against the relevant schema, bus
   and machine-ID linkage, base conversion, missing fields, duplicate or
   incompatible records, and unsupported-model diagnostics. Apply the same
   tests to each other dynamics format as support is added.

Build with `cmake --build build --config Debug --parallel 4`, then run the
`GeneratorComponentTests`, `ModelComparisonTests`, `FileReaderTests`, and a
small dynamic-system test subset. The repository's `AGENTS.md` specifies a
`Path`/`PATH` workaround only if MSBuild raises the exact duplicate-key error.

**First implementation acceptance:** A solar fixture loads separate `REGCA1`
and `REECA1` DYR records into one renewable generator in either record order,
initializes from power flow at the specified P/Q operating point, responds
correctly to a voltage step and active/reactive reference changes, and runs
in full and partitioned solve modes. Its bus-level trajectory is compared
with the frozen ANDES solar case within recorded tolerances. A separate
`REPCA1` DYR record can be added, removed, or replaced without rebuilding
the other two models and passes its own plant-control acceptance tests. Wind
models follow the same independent-record rule. Native ANDES workbook import
is not a condition of these gates.
