# Bus models, APIs, and validation

`GridBus`, `AcBus`, and `DcBus` share attachment, unit conversion, cached power,
and network traversal operations. Their numerical and controller capabilities
differ. A physical DC bus is also distinct from the DC power-flow approximation
used for an AC network.

| Class     | Local network variables                                | Balance equations and fixed variables                                                                                                                     |
| --------- | ------------------------------------------------------ | --------------------------------------------------------------------------------------------------------------------------------------------------------- |
| `GridBus` | No local voltage or angle solver variables             | Holds configured voltage/angle; attached components can still have states. Base convergence and generation-adjustment hooks do nothing.                   |
| `AcBus`   | Voltage and angle; optional frequency-estimator states | Real-power balance determines angle, reactive-power balance determines voltage. Fixed variables keep identity constraint equations in their solver slots. |
| `DcBus`   | One voltage slot; attached components may add states   | Real-power balance determines voltage. Fixed-voltage types constrain the slot to the stored voltage. Only DC-compatible links attach.                     |

## Types and state access

For AC power flow, `pq` solves angle and voltage, `pv` fixes voltage, `afix`
fixes angle, and `slk` fixes both. Dynamic `normal`, `fixedangle`,
`fixedvoltage`, and `dynslk` select the corresponding independent dynamic
behavior. `getType()` and `getDynType()` return the typed enums;
`getBusType()` remains a legacy integer view of the power-flow type.

For a physical DC bus, `pq`/`afix` use real-power balance and `pv`/`slk`
constrain voltage. Dynamic `normal`/`fixedangle` use balance;
`fixedvoltage`/`dynslk` constrain voltage. The inherited AC-oriented names do
not introduce angle or reactive-power equations into a DC bus.

Normal DC initialization preserves the configured bus types and leaves voltage
balance to the network solver. The legacy `propogatePower()` heuristic is an
explicit operation; it can fix neighboring voltages and change bus types.

`useAngle()` and `useVoltage()` identify balance equations, not whether a
solver slot is allocated. `getMode()` returns fixed-variable bits: bit 0 for
angle and bit 1 for voltage. Use the mode-specific output-location methods
for state indices rather than deriving them from those bits.

The no-argument voltage, angle, and frequency getters return stored values.
The state-taking getters use assigned offsets, falling back to stored values
for missing data. In partitioned dynamics, `StateData::algState` or
`fullState` can provide the paired algebraic voltage/angle data. The paired
mode must already have assigned offsets. Raw state arrays have no length
information: callers must supply storage matching the solver layout.

Extended modes reserve aggregate P/Q variables for external constraints;
DC approximation modes reserve P only. Their placeholder residual rows are
not a complete standalone system. Additional equations must define those
aggregate variables. Tests check their coupling and storage bounds, not
independent solvability.

## Units, signs, and caches

Bus defaults are voltage in pu, angle in radians, frequency in pu of the
system frequency, base voltage in kV, and powers in pu of system base power.
Prefer explicit units at application boundaries. For example, a 200 kV bus
at 0.95 pu must report 190 kV, regardless of the units used to configure it.
Voltage limits and targets accept explicit voltage units; real/reactive
limits accept power units.

The legacy bus implementation stores its own voltage base in kV, while
`GridComponent` stores the corresponding base in volts. Bus setters keep
both values synchronized, and pass explicit kV units to newly attached or
replacement generators/loads. The inherited `baseVoltage()` accessor is in
volts. Use `get("basevoltage", units::kV)` for a unit-explicit query.
ZIP load models retain their legacy kV base-voltage storage because derived
load models use that value in internal calculations. Their parameter getter
and setter convert explicit units at the API boundary; use explicit units
when comparing a ZIP load base with a bus or another component. The inherited
`baseVoltage()` accessor exposes the ZIP family's legacy kV field, so use its
unit-aware `get("basevoltage", unit)` parameter API for ZIP loads.

Cached power totals use the balance convention: loads and flows leaving a
bus are positive, generator injections are negative. Their sum is the bus
imbalance. `lastError()` is the cached absolute P plus absolute Q imbalance;
it does not refresh the cache itself.

Call `updateLocalCache()` to refresh power totals from stored values. The
state-based overload also refreshes the voltage/angle/frequency output vector
and mode-specific output locations. `getOutputsRef()` and
`getOutputLocsRef()` borrow those caches; their contents can change on later
updates. Prefer `getOutputLocs(mode)` when no cache has been populated for
that mode. Disconnection clears cached powers, voltage/angle outputs, and
output locations.

`checkCapable()` checks necessary local real-power bounds after power-flow
initialization: maximum generation plus import capability must cover demand,
and minimum generation must fit demand plus export capability. Disabled or
disconnected equipment contributes nothing. This screen does not prove that
a network solution exists or that the solver will converge. Bounds within the
configured power-flow solver tolerance are treated as feasible; before a
solver exists the configured simulation tolerance is used, and a standalone
bus uses a `1e-8` pu-power fallback. This screening tolerance does not change
equipment limits.

## Ownership and topology edits

Adding a load or generator acquires an intrusive owning reference and assigns
its parent, bus index, and base values. A bus release can destroy the object.
Use `CoreOwningPtr` when an independent owner must retain it after removal.
Pointers returned by `getLoad()`, `getGen()`, and lookup methods are borrowed.
Removal compacts and reindexes the remaining objects.

Replacement preserves the slot and remaining order. The replacement must be
unattached; replacing an object with itself is a no-op. The old object can
be destroyed when its bus reference is released. A component already attached
to another bus must be explicitly detached before moving it.

Bus link lists borrow pointers. Use `Link::updateBus()` to maintain both link
terminals and bus lists, and keep links alive while those lists can be used.
The owning area/simulation normally owns the links.

Perform component replacement and topology construction before initializing
controllers and solvers. Mutation after initialization needs further work:
AC controller groups and the frequency-reference generator retain borrowed
pointers, and generic replacement does not rebuild all such relationships.
Reinitialization alone should not be assumed to deregister old controllers.

`getDirectPath()` is an unbranched controller-routing check. It follows one
active onward link, excludes the incoming link, and rejects branches, dead
ends, and cycles. Its result contains the originating object (or this bus),
the traversed links, and the destination. Intermediate buses are implicit in
link terminals. It does not choose a shortest route through a meshed network.
`directPath()` uses the same implementation.

## AC frequency estimation

`setFlag("compute_frequency", true)` enables the simple derivative estimator;
`use_frequency` is a supported alias. Passing false removes the estimator
before dynamic initialization. `getFlag()` and `getParameterStrings()` expose
the same flag names. A component requiring bus frequency can independently
request estimation during initialization.

`configureFrequencyFilter(Tf, Tw, Fn)` selects the lag/washout estimator,
replacing a simple estimator if one was configured. Both times are in seconds
and the nominal frequency is in Hz. All three values must be positive and
finite. Configure or change estimator structure before dynamic initialization;
late structural changes raise `InvalidParameterValue`.
Until an enabled estimator has completed dynamic initialization, frequency
queries return the bus's stored frequency. Frequency output locations are
reported only for solver modes with a differential state.

Sampled voltage and angle rates use successive accepted algebraic states.
Predictor calls may update the bus's working voltage and angle, but do not
replace the accepted sample used to calculate the next rate.

## Tests and remaining work

`test/componentTests/testBus.cpp`, in `SystemComponentTests`, contains direct
behavioral checks using the public attachment, sizing, offset, and parameter
APIs. The numerical checks compare analytical Jacobians with central finite
differences of residuals, including fixed-voltage modes, disabled loads, and
extended DC layouts. A two-bus resistive DC network is compared with its
analytical high-voltage solution. These small cases avoid long transients.

The review leaves these larger capabilities for follow-up:

| Area                           | Remaining work                                                                                                                                                                                                                     |
| ------------------------------ | ---------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------- |
| Initialized component mutation | Define transactional controller deregistration/re-registration, remote-controller lifetime handling, and frequency-reference invalidation for removal/replacement. Add tests through full simulation initialization.               |
| DC controller integration      | `DcBusControls` has bookkeeping operations, but `DcBus` does not override the common controller-registration hooks. Do not assume AC-style automatic controller aggregation, redispatch, or aggregate limit handling is complete.  |
| DC convergence recovery        | `DcBus::converge()` is a no-op; the global solver performs convergence. A bounded voltage-recovery algorithm needs its own numerical tests.                                                                                        |
| DC bus merging                 | DC control helper methods exist, but the public bus merge/unmerge hooks still inherit base no-ops. Full node/breaker behavior needs implementation and topology tests.                                                             |
| Legacy adjustment API          | `propogatePower()` retains its spelling and inconsistent legacy status values. A future replacement should define a typed result rather than merely rename the method.                                                             |
| Legacy aggregate queries       | Base generation maxima use sentinel bounds; adjustable capacity/regulation defaults are placeholders. DC inherits those defaults. Callers should query actual equipment limits until a complete aggregate contract is implemented. |
| Broader numerical validation   | Add loaded-network recovery, PV/PQ limit cycling, zero-impedance merge/unmerge, remote-controller proxy, and DC transient tests. Small Jacobian checks do not establish those larger behaviors.                                    |

Further API consolidation should preserve existing callers while making the
contracts easier to test:

- Replace the shadowed kV/volt base fields with one canonical voltage base and
  explicit conversion at the bus API boundary.
- Share AC/DC type-string parsing after specifying which aliases have common
  semantics; keep the physical equation selection in each bus class.
- Introduce checked state views for new numerical APIs. Current raw-pointer
  overloads cannot validate array lengths, and `StateData::stateSize` is optional.
- Separate internal derivative/cache assembly hooks from application-facing
  configuration. Public `DcBus::computeDerivatives()` currently relies on a
  prior cache update and is primarily an assembly helper.
- Replace ambiguous legacy adjustment statuses with a typed result and define
  how controller registration, solver invalidation, and topology edits interact.
