# WT3P1/WT3T1 source investigation

Status: deferred pending authoritative PSS/E model documentation or a
reference implementation.

## Findings

The current GridDyn renewable implementation supports `WT3G1` and `WT3E1`, but
the complete Type-3 wind assembly also requires `WT3P1` and `WT3T1`. Neither
OpenIPSL nor GridKit contains an exact implementation of both models.

The local test corpus contains repeatable PSS/E DYR records in:

`C:\data\Documents\codeProjects\tests`

Representative records are:

| Model   | Status values observed | Positional values after status             | Established                   |
| ------- | ---------------------- | ------------------------------------------ | ----------------------------- |
| `WT3T1` | `1`, `Z`               | `1.25, 4.95, 0, 0.007, 21.98, 0, 1.8, 1.5` | Eight-field record shape only |
| `WT3P1` | `1`, `Z`               | `0.3, 150, 25, 3, 30, 0, 27, 10, 1`        | Nine-field record shape only  |

The relevant fixtures include `EI_NPCC_WIND20_WT3G1.dyr` and `WECC_WIND10.dyr`.
They are suitable as future parser and full-bundle regression fixtures, but
their values alone do not identify parameter names or equations.

An older OpenIPSL history artifact,
`OpenIPSL/Electrical/Wind/PSSE/WT1G/WT12T1.mo` at commit `56e4cd9`, contains a
two-mass turbine/shaft implementation whose icon is labeled `WT3T1`. It is a
useful mechanical-equation lead, but its `WT12T1` name and parameter interface
do not establish the PSS/E `WT3T1` positional mapping. `WTDTA1`, `WTPTA1`, GE,
and PSAT models are different model families and must not be used as
positional substitutes.

## Deferred work

Before adding either model to GridDyn, obtain and record an authoritative
source that defines:

- every DYR field and its positional meaning;
- differential and algebraic equations;
- pitch, turbine, shaft, and electrical signal connections;
- initialization and steady-state conventions;
- limits, flags, saturation, windup, and status behavior;
- per-unit bases, signs, and treatment of zero time constants.

After the source is obtained, implement dedicated `RenewableRole::pitchControl`
and `RenewableRole::driveTrain` components. Require the complete
`WT3G1`/`WT3E1`/`WT3P1`/`WT3T1` bundle during DYR assembly and add:

1. field-by-field DYR mapping tests;
2. steady-state initialization tests for representative variants;
3. residual and analytic host-Jacobian tests;
4. pitch/turbine disturbance and trajectory tests;
5. incomplete-bundle and unsupported-variant diagnostics.

Until those criteria are met, `WT3P1` and `WT3T1` must remain unsupported and
must not be approximated from WECC, GE, PSAT, or historical `WT12T1` behavior.
