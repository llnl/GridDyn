# Worked dynamic model: SEXS exciter

[ExciterSEXS](../../src/griddyn/exciters/ExciterSEXS.cpp) is a compact dynamic example
with a lead-lag regulator, an upper/lower regulator limiter and a field lag.
Its numerical equations and limiter transitions remain in the existing class.

## What the declarations mean here

| Owner | Declaration | Values |
| --- | --- | --- |
| Exciter | `parameter` | `ka`, `ta`, `vref`, `vbias`, `vrmin`/`urmin`, `vrmax`/`urmax` |
| ExciterSEXS | `parameter` | `tb`, `te` |
| ExciterSEXS | `state` | `ef`, `x` |
| Exciter | `localState` | `operating_bias`, `limiter_active`, `limiter_high` |
| CoreObject | `flag` | Inherited enable and update controls |

For SEXS, `ta` is the lead-lag numerator time constant, `tb` its denominator,
and `te` the field time constant. The shared Exciter table uses the neutral
name "Regulator time constant Ta" because other exciter models give Ta a
different role.

The concrete model declares its own solver variables:

```cpp
parameter({"tb"}, &ExciterSEXS::Tb, units::second,
          "Lead-lag denominator time constant")
parameter({"te"}, &ExciterSEXS::Te, units::second,
          "Field time constant")
state("ef", units::one, "Per-unit field voltage")
state("x", units::one, "Lead-lag memory in the solver vector")
```

The parent holds common parameters and retained limiter history. A history bit
uses the same packed storage adapter as a Boolean flag, with the local-state
role determining that no ordinary setter is generated:

```cpp
localState("limiter_active", &Exciter::opFlags, OUTSIDE_VOLTAGE_LIMITS,
           "Regulator is in its limited branch")
localState("limiter_high", &Exciter::opFlags, TRIGGER_HIGH,
           "Retained upper/lower limiter branch")
```

These are internal modes, not user options. They do not appear in `flagNames()`
or numeric parameter lists, and `trySetFlagLocal` does not assign them. Snapshot
restoration writes only those named bits, leaving unrelated operational and
layout flags alone. No access/capture policy arguments are needed.

## Why these local values must be retained

The configured input and retained operating bias use separate members:

```cpp
parameter({"vbias"}, &Exciter::initialBias, units::one,
          "Initial bias when no desired field output is supplied")
localState("operating_bias", &Exciter::vBias,
           "Retained operating bias", units::one)
```

When no desired field output is supplied (an empty output vector or `kNullVal`),
the common Exciter initializer copies `initialBias` into `vBias`. Otherwise it
calculates `vBias` from the operating point. SEXS can then apply extra overdrive
when initialization adjusts a regulator limit. Initialization never overwrites
the configured `initialBias` parameter.

The equations use `vBias`, which is not generally recoverable from the current
field and lead-lag states, particularly while the output is limited. It is
therefore retained as `operating_bias`. Changing `vbias` after initialization
configures the next initialization; it no longer changes the working bias.
Runtime reference adjustments use `vref`. The retained operating bias has no
ordinary setter, and can be inspected through its description's `value()`.

Cloning copies both members. A full parameter snapshot captures both values;
a state-only snapshot restores `operating_bias` while keeping the caller's
current `vbias` configuration. Restoring either snapshot does not initialize.

The limiter flags distinguish a free regulator from a clamped upper/lower
branch. At an exact bound, the same inputs, parameters and solver states can
legitimately accompany either branch depending on the preceding event. The
branches can produce the same instantaneous output while having different
Jacobians and root behavior. Recomputing the branch from the current values
would discard that history.

The boundary regression test constructs both branches at identical numerical
states, verifies that their Jacobians differ, then restores each branch without
changing the solver state or running initialization.

## Values deliberately omitted

The regulator output is evaluated from the current inputs, state, parameters,
bias and limiter mode whenever needed. It has no checkpoint entry.
`m_state` and `m_dstate_dt` are local copies of solver-owned vectors; their values
belong to the existing solver payload. Output caches, structural sizes and the
whole `opFlags` bitset are not duplicated in the model-field snapshot.

The inherited `limitState` index is established by the concrete class/layout.
The strict initial-limit policy is selected from case initialization flags and
is only used during initialization. Neither is additional continuation history
for this model.

`Vrmin` and `Vrmax` remain parameters, even when initialization changes the upper
bound to accommodate the starting point. A full parameter snapshot records the
actual adjusted value; a state-only snapshot intentionally keeps the caller's
current parameter values while restoring bias and limiter history.

## Existing access and inheritance

The description chain is CoreObject -> GridComponent -> Exciter -> ExciterSEXS.
GridSubModel retains its existing initialization and output interfaces without
adding a table for its reconstructible storage.

The model still uses `set(name, value, units)` and `get(name, units)`. SEXS offers
its local parameters to the default helper, then delegates to Exciter, which
handles common parameters before delegating to GridSubModel. Each call qualifies
its own description accessor to preserve normal parent dispatch.

Time constants support ordinary time conversion:

```cpp
exciter.set("te", 500.0, units::ms);
exciter.get("te");                 // approximately 0.5 seconds
exciter.get("te", units::ms);      // 500 milliseconds
```

Per-unit signal values and gains use `units::one`. Explicit dimensionless scaling
such as percent is supported. Physical field/stator voltage base conversion is
not guessed. Native values, including the model's existing zero-time-constant
cases, retain their established meaning. The initialization-only `vbias`
parameter uses the same conversion and setter helpers as the other parameters.

SEXS derives `localStateNames()` from its own `state` declarations. This avoids
a second list of `ef`/`x` names. `findIndex` resolves them through the existing
active offsets; tests use nonzero, changing offsets in both DAE and differential
layouts. The shared Exciter table does not declare solver variables on behalf
of children, since concrete models can replace its equations and state layout.

## Restoring a model-level point

The two parts remain separate:

```cpp
auto fields = ModelFieldSnapshot::capture(exciter);
// Save solver vectors through the existing solver/HDF5 path.

// Later, with a compatible initialized model and restored solver buffers:
fields.restore(exciter);
exciter.setState(savedTime, restoredState, restoredDerivative, mode);
```

Restoring the field snapshot does not call initialization, which would select a
new limiter branch and recompute the bias. The existing `setState` path reloads
solver-backed copies and time. SEXS then evaluates its output, regulator drive,
derivatives, residuals, roots and Jacobian from the restored inputs and values.

The tests perform this round trip on both upper and lower limiter branches in
DAE and differential layouts. They change parameters, bias, time, solver values
and the active branch, then reproduce the saved output, derivatives, residuals,
roots and fresh Jacobian. They also check that normal root checking can release
the restored limiter.

This demonstrates the model-description and model-level restore boundary. It
does not serialize an integrator's history or resume a whole dynamic simulation.
A future checkpoint coordinator still needs to restore case inputs and solver
resources and refresh solver root/Jacobian caches at its normal restart boundary.
Other concrete exciters still need their own description migration; the exact
class guard prevents their inherited common table from silently serving as a
complete object-field snapshot.
