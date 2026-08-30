# PSS/E RAW DC compatibility status

This note records the intended scope and known limitations of GridDyn's PSS/E
RAW DC import.  It is a compatibility layer for the AC power-flow model used
by PowerModels.jl; it is not a replacement for GridDyn's physical DC-network
models (`DcBus`, `DcLink`, `AcDcConverter`, `VSCShunt`, and `Hvdc`).

## Implemented scope

The RAW reader recognizes these section labels:

| RAW section | GridDyn representation | Power-flow behavior |
| --- | --- | --- |
| `BEGIN TWO-TERMINAL DC DATA` / `BEGIN TWO-TERMINAL DC LINE DATA` | `links::RawDcLine` | Scheduled active terminal transfer; PQ-terminal reactive variable and voltage setpoint |
| `BEGIN VOLTAGE SOURCE CONVERTER DATA` / `BEGIN VSC DC LINE DATA` | `links::RawDcLine` | PowerModels-compatible zero initial active transfer; PQ-terminal reactive variable and voltage setpoint |

Two-terminal active transfer follows PowerModels' RAW conversion:

- `MDC == 1`: `abs(SETVL)` MW.
- `MDC == 2`: `abs(SETVL / VSCHD / 1000)` MW.
- `MDC == 0`: out of service.

For VSC data, the active transfer starts at zero, as it does in
PowerModels' PSS/E importer.  The VSC loss slope and rating are retained on
the compatibility link; loss intercept and RAW limit fields are retained in
its description for diagnostics.

`RawDcLine` keeps the active-transfer convention of PowerModels' `dcline`
model.  At each connected PQ AC bus, it adds the reactive terminal variable
and voltage-magnitude equality needed to retain the bus reactive balance.
This is deliberately separate from GridDyn's physical DC components.

## Numerical evidence

The regression input is
`test/test_files/input_tests/psse_dc_components.raw`.  It is solved by both
GridDyn and PowerModels' JuMP/IPOPT AC power flow.

For the shared 100 MVA test case, both give these bus-2 results:

| Quantity | Value |
| --- | ---: |
| voltage magnitude | `1.000000` pu |
| voltage angle | `-0.025284658` rad |

The combined reactive power from the two RAW DC terminals at bus 2 is also
checked at `-0.0781963933423` pu.  The regression is
`InputTests.PssERawDcComponentsImportAsScheduledLinks`.

## Known differences and follow-up work

| Topic | Current behavior | Required follow-up |
| --- | --- | --- |
| Physical HVDC equations | RAW records use the PowerModels-style AC-terminal `dcline` abstraction, not a DC grid or converter commutation model. | Map RAW data to GridDyn's native DC models only after validating the PSS/E control and base-conversion semantics. |
| Reactive limits | Two-terminal angle-derived limits and VSC `MINQ`/`MAXQ` are recorded as diagnostics, but are not solver-enforced. | Add limiter equations/state transitions and test constrained cases against a version-matched PSS/E reference. |
| Multiple DC terminals at one PQ bus | PowerModels creates redundant voltage equalities and has a non-unique split of terminal reactive power. GridDyn selects the first RAW DC terminal as the voltage controller and assigns the combined required reactive power there. Bus voltages, angles, and combined reactive power match. | Define and test a documented allocation rule if per-terminal reactive reporting must reproduce a particular PowerModels solver result. |
| VSC losses and nonzero active control | The PowerModels RAW importer initializes VSC active terminal powers at zero, so `loss0` and `loss1` do not alter the regression solution. GridDyn retains the slope as link loss metadata. | Add cases and equations for nonzero active VSC control and affine losses when the intended RAW/PSS/E semantics are established. |
| Active/reactive ratings | Ratings are imported for reporting/violation checks, not imposed as feasibility constraints. | Add solver-enforced apparent-power and terminal power limits. |
| Multi-terminal DC | `BEGIN MULTI-TERMINAL DC LINE DATA` remains unsupported and is skipped. | Implement a separate multi-terminal topology/model; do not flatten it into independent two-terminal transfers. |
| Dynamics | The compatibility link is for AC power flow only. | Validate physical LCC/VSC controls and trajectories against PSS/E or CIGRE benchmark cases using GridDyn's native DC classes. |
| RAW-version coverage | Field handling is based on PowerModels' importer and representative RAW records, not a complete version-specific PSS/E data-format audit. | Audit against the licensed PSS/E RAW manual for each advertised RAW version, including defaults and all control modes. |

## Validation procedure

Use PowerModels' JuMP/IPOPT path, rather than its native `compute_ac_pf`,
because the latter does not support `dcline` records:

```julia
solve_ac_pf("psse_dc_components.raw", Ipopt.Optimizer)
```

Compare bus voltage magnitude/angle, active generation, active DC terminal
flows, and the total reactive DC terminal injection at each bus.  Individual
reactive flows at a bus with multiple DC terminals should only be compared
after choosing an explicit allocation convention.
