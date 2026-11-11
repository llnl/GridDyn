# Checkpointing and restore boundaries

This page describes the checkpoint-related structures that exist today and the
work needed to turn them into a complete simulation restart. GridDyn currently
has solver-vector snapshots, per-object field snapshots, and a solution-only
power-flow checkpoint. These are useful pieces with different scopes; none is
currently a complete dynamic simulation checkpoint.

## Current capture structures

| Structure                  | Captures                                                                                                   | Restore boundary                                                                                                                                                                            |
| -------------------------- | ---------------------------------------------------------------------------------------------------------- | ------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------- |
| HDF5 solver snapshot       | State and, for differential blocks, derivatives. Paired partitioned solvers are written in the same frame. | Supplies arrays to an already initialized solver. The restore level determines whether backend time/history and the simulation clock are reset. It does not restore model fields or events. |
| `ModelFieldSnapshot`       | Declared fields on one exact `CoreObject` type: flags and retained local state; optionally parameters.     | Restores members on an existing compatible object. Does not serialize solver vectors, traverse child objects, rebuild derived data, or invoke normal setters.                               |
| Power-flow JSON checkpoint | Named power-flow solver values and bus voltage/angle values.                                               | Restores a solution into an already initialized compatible model, then updates bus state and local caches. Parameters and dynamic history remain current.                                   |

The HDF5 capture implementation is shared by solver snapshots, diagnostics,
recorder output, and sparse matrix records. `saveStateBinary` writes a complete
HDF5 frame for its primary solver and any initialized paired solver. The
partitioned dynamic path captures its initial frame after startup conditions
have populated both vectors. The frame is a synchronized solver capture, not a
complete model restart. See [HDF5 state, diagnostic and recorder output](binary-state-output.md).

## Binary state restore API

`readStateBinary()` materializes a `BinaryStateData` value without changing a
simulation. `GridDynSimulation::checkpointRestore()` applies that value to
already initialized solver interfaces. Its filename overload reads the last
frame and delegates to the `BinaryStateData` overload, so both entry points use
the same validation and restore logic. Its initial levels are deliberately
narrow:

| `CheckpointRestoreLevel` | Effect                                                                                                                                                                                                                                        |
| ------------------------ | --------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------- |
| `SOLVER_STATE_ONLY`      | Copies the saved state and derivative vectors. Leaves the simulation clocks and backend integration history alone. `loadStateBinary()` uses this level to preserve its existing behavior.                                                     |
| `MINIMAL`                | Copies the vectors, asks each backend to restart from those arrays at the captured time, then updates the simulation clocks. This restarts solver history from an accepted point; it does not reproduce the prior integrator history exactly. |

The minimum restore currently supports `BasicOdeSolver`, SUNDIALS CVODE,
IDA, ARKStep, and KINSOL.
Differential backends must implement `SolverInterface::restoreFromCheckpoint()`;
the base implementation rejects differential modes that have no backend reset
hook. A paired capture requires the destination's initialized paired solver to
be present as well.

Before copying, restore checks that the destination solver is initialized and
that the mode flags, approximation, primary vector size, derivative vector
size, and paired-block presence agree with the capture. If no explicit solver
mode is supplied, it searches initialized solvers for a unique match and
reports ambiguity rather than choosing one arbitrarily. These checks do not
prove the component/state ordering is identical: normal HDF5 state frames do
not write the optional `NAMES` records. `ModelDescription` can describe the
destination layout, but the source frame currently lacks names to compare it
against.
The current frame also does not identify the solver backend implementation, so
restore does not validate backend type. A backend reinitialization error can
occur after vectors have been copied; this first API is not transactional.

`MINIMAL` is not a complete simulation restore. Model fields and parameters,
event queues and progress, output cursors, and other simulation runtime state
remain as configured in the destination. The method provides the first
backend-aware solver/time restore step; future levels can add model and runtime
state once those payloads and their validation are defined.

`ModelDescription` supplies inherited declarations and state-layout metadata.
`GridComponent::solverStateNames()` and `getStateName()` use those declarations
to produce names for active solver layouts. `ModelFieldSnapshot::capture()` is
per object and requires the object's exact dynamic type to be described. Its
field levels are:

| Level                            | Fields included                                                                                    |
| -------------------------------- | -------------------------------------------------------------------------------------------------- |
| `FieldSnapshotLevel::STATE`      | Flags and declared `localState` fields. Parameters remain as configured on the destination object. |
| `FieldSnapshotLevel::PARAMETERS` | Parameters, flags, and declared `localState` fields. This is the default.                          |

Solver-managed `state(...)` fields describe names and locations; the actual
values remain in solver vectors. `command(...)` entries describe operations,
not stored data. A command declaration therefore does not capture an event's
target, a file path, a variable-length curve, child objects, or other backing
configuration.

The power-flow JSON checkpoint remains a separate, intentionally narrow API. It
uses state names and bus names to validate the destination, and gives explicit
bus voltage/angle values precedence where those values overlap the solver
vector. It does not capture the simulation's parameters or runtime history. The
Python interface documents this scope in [python-interface.md](../python-interface.md#power-flow-solution-checkpoints).

## What a complete dynamic checkpoint still needs

- **Object graph and identity.** The field snapshot API handles one object. A
  coordinator must enumerate supported objects and match them across restore
  using stable identities, type names, and a compatible topology/layout.
- **Complete field coverage.** Only declared fields are captured. The
  model-parameter audit identifies command-backed and variable-length data,
  helper hierarchies, external integrations, and retained runtime history that
  still need explicit coverage. Unsupported object types must be reported;
  silently skipping them would make a checkpoint appear complete when it is
  not.
- **Simulation runtime.** The live simulation clock, next stop time, event
  queue, event progress, active solver configuration, and other runtime
  bookkeeping are not all `ModelDescription` fields. `EventQueue::clone()` is
  an in-process cloning facility, not a persistent event format.
- **Derived data and restore order.** Field restoration writes members directly
  and bypasses setter validation, notifications, and side effects. Restore must
  apply parameters before constructing parameter-dependent layouts, then
  restore retained state and solver values, synchronize component state, and
  rebuild caches without reinitializing away the restored history.
- **Solver restart semantics.** HDF5 stores solver vectors, not SUNDIALS
  integrator history. A portable restart can reinitialize the integrator from
  an accepted `(time, state, derivative)` point. It should not promise
  bit-for-bit continuation of the prior integration history.
- **Validation and publication.** A coordinator must validate every object's
  type and field layout, every solver block and layout, and all required runtime
  data before mutating the destination. Persistent output needs a versioned
  manifest and a clear complete-checkpoint publication boundary.

These limits also apply to the `CHECKPOINT` and `ROLLBACK` actions: the parser
recognizes them, but the execution switch currently has no implementation.
`LOAD state` is also an empty action branch. The direct C++ and Python APIs above
remain the supported narrower operations.

## Implementation options

| Option                    | Scope                                                                                                                                                                        | Tradeoff                                                                                                                                                               |
| ------------------------- | ---------------------------------------------------------------------------------------------------------------------------------------------------------------------------- | ---------------------------------------------------------------------------------------------------------------------------------------------------------------------- |
| In-process rollback       | Capture fields, solver vectors, simulation runtime, and a cloned event queue in memory.                                                                                      | Avoids a disk schema and can support named rollback points in one process. Solver history is still restarted unless backend-specific state is added.                   |
| Accepted-point restart    | Persist per-object field records plus a reference to an HDF5 solver frame and runtime data. Reconstruct the model from its case/assets, then restart from an accepted point. | Supports process restart and keeps bulk numerical data in HDF5. Requires stable object identities, full runtime adapters, and an explicit restart initialization path. |
| Exact solver continuation | Persist backend integrator state in addition to GridDyn state.                                                                                                               | Backend-specific and version-sensitive; substantially larger scope than a portable accepted-point restart.                                                             |

The practical direction is one in-memory snapshot model with multiple
adapters: memory for rollback, and a versioned manifest plus the existing HDF5
writer for persistent restart. `ModelDescription` should be the authority for
declared component fields and solver-state layout names. It should not become a
second serializer embedded in each model class. Keep the power-flow checkpoint
as a solution-only view over shared capture primitives if that improves
consistency, without changing its current public scope.

An implementation should first define the promised semantics and supported
object set. The first persistent milestone should target restart from a
synchronized accepted boundary with a reset integrator history. Exact backend
continuation can remain a separate capability. No checkpoint should be marked
complete until all required model, solver, event, and runtime records have been
captured and validated.

## HelperObject descriptions: reuse and boundaries

`HelperObject` is a separate, small polymorphic base. It owns a process-local
OID and a name, stores descriptions in a side dictionary, and provides virtual
`set`, `get`, and flag operations. It has no description table or common field
discovery API. Direct helper families include events, parameter operators,
collectors, solver interfaces, and optimizer interfaces; their derived classes
have their own setter logic. Treat helper name and description as checkpoint
identity/metadata, not ordinary parameter fields, and never persist the OID as
cross-run identity. Both bases use string `name` and `rename` to rename; string
`id` is not a rename alias. Numeric `CoreObject::set("id", number)` sets its
user ID, while `HelperObject` has no user ID field; its `getID()` is a
process-local OID, not a checkpoint identity. Model the name and CoreObject
user ID as separate fields. A HelperDescription should not invent an `id` field
just to mirror CoreObject.

The current model field machinery is partly generic and partly tied to
`CoreObject`:

| Existing part                                                                                           | Reuse potential                                                                                                                               |
| ------------------------------------------------------------------------------------------------------- | --------------------------------------------------------------------------------------------------------------------------------------------- |
| `ModelFieldValue`, field kinds/types, aliases, descriptions, and snapshot entries                       | Reuse as the common typed value and metadata vocabulary.                                                                                      |
| Member binding, value conversion, validators, staged restore assignments, and inherited-table traversal | Share through templates parameterized by the object base. These contain much of the code that should not be duplicated.                       |
| `ModelField` reader/writer/restorer callbacks and `fieldOwner<Model>`                                   | Currently accept `CoreObject&` and `const CoreObject&`; this is the main binding seam that must be generalized or wrapped for `HelperObject`. |
| `ModelDescription` solver-state bindings and `SolverMode` lookups                                       | Keep CoreObject-specific. Helper descriptions should not inherit solver-layout APIs.                                                          |
| `ModelFieldSnapshot` capture/restore flow                                                               | Reuse through a base-parameterized snapshot implementation while retaining model and helper facades.                                          |

Making `HelperObject` derive from `CoreObject`, or making `ModelDescription`
pretend every object has solver states, would couple unrelated lifecycles and
would not solve helper reconstruction. Copying `ModelField` and snapshot logic
into a separate helper implementation would reduce the initial refactor but
create two field systems to maintain.

The recommended shape is a shared field-description substrate parameterized by
its polymorphic base, with thin family-specific APIs:

```text
ObjectField<Base> / ObjectDescription<Base> / ObjectFieldSnapshot<Base>
                 /                         \\
ModelDescription (CoreObject + solver states)   HelperDescription (HelperObject)
```

The shared substrate can own member bindings, role/type metadata, aliases,
inherited descriptions, and staged typed snapshots. `ModelDescription` keeps
its state-name/index extensions; `HelperDescription` starts with parameters,
flags, retained helper state, and commands. Helper setters can then use the same
table for ordinary access and checkpoint capture, so one declaration drives
discovery, set/get dispatch, and persistence. Custom commands and side effects
remain in their owning classes. The in-memory snapshot payload and eventual
serialization format should be shared across both families.

This does not make arbitrary helpers persistable. A checkpoint coordinator still
needs stable helper type IDs, factories, object identities, and explicit
reference records rather than raw pointers. `HelperObject::getID()` is assigned
from a process-local counter, so it cannot identify a helper across runs.
References such as an event's target or a `ParameterOperator`'s component must
resolve through stable simulation object IDs.

### Helper-specific runtime adapters

- **Events and the queue** affect future simulation behavior and are first in
  scope. `Event` stores a target `CoreObject*`; event adapters hold scheduled
  times and two-part execution flags; and `EventQueue` owns both scheduled and
  pending part-B lists. Persist event type/configuration and target identity,
  then reconstruct through event factories. A `FunctionEventAdapter` holds an
  arbitrary `std::function`, which cannot be serialized generically; such
  callbacks must be recreated by simulation setup or opt into a named callback
  registry. The current queue clone also omits its pending part-B list, so it is
  only suitable for checkpoints taken at a quiescent event boundary unless
  extended.
- **Solver interfaces** need enough declared configuration to recreate the
  selected solver and mode. Their numeric state remains in the HDF5 solver
  blocks. `SolverInterface::cloneTo(fullCopy=true)` allocates and initializes a
  solver, then copies exposed vectors; it does not copy backend integrator
  history and is not an exact continuation mechanism.
- **Collectors and players** need an explicit policy. Their schedules and
  referenced objects can affect future updates; whether to preserve buffered
  output and file cursors depends on whether checkpointing promises continuous
  recorder output. External files must be named as assets or reopened from
  stable paths.
- **Optimizers and unrelated helpers** should only join a checkpoint when the
  requested operation needs them. Optimization warm starts and backend memory
  have different persistence semantics from restarting a dynamic simulation.

So the code-reuse decision is not “one description class for every object” or
“duplicate ModelDescription.” Share the typed binding/snapshot engine, expose a
`HelperDescription` facade, and keep reconstruction of each helper family in
small explicit adapters. This supports a single field definition without
pretending that a field table alone can serialize queues, pointers, lambdas, or
backend workspaces.
