# ACTIVSg PSS/E compatibility plan

## Scope

This is the single compatibility plan for the supplied ACTIVSg500,
ACTIVSg2000, ACTIVSg10k, ACTIVSg25k, and ACTIVSg70k PSS/E RAW/DYR pairs. It combines their
inventories so that implementation work is prioritized by total impact rather
than by case-file size.

The source cases remain outside the repository:

| Case        | RAW source                                             | DYR source                                                      | DYR records |
| ----------- | ------------------------------------------------------ | --------------------------------------------------------------- | ----------: |
| ACTIVSg500  | `C:\Users\phlpt\Downloads\ACTIVSg500\ACTIVSg500.RAW`   | `C:\Users\phlpt\Downloads\ACTIVSg500\ACTIVSg500_dynamics.dyr`   |         272 |
| ACTIVSg2000 | `C:\Users\phlpt\Downloads\ACTIVSg2000\ACTIVSg2000.RAW` | `C:\Users\phlpt\Downloads\ACTIVSg2000\ACTIVSg2000_dynamics.dyr` |       1,739 |
| ACTIVSg10k  | `C:\Users\phlpt\Downloads\ACTIVSg10k\ACTIVSg10k.RAW`   | `C:\Users\phlpt\Downloads\ACTIVSg10k\ACTIVSg10k_dynamics.dyr`   |       7,404 |
| ACTIVSg25k  | `C:\Users\phlpt\Downloads\ACTIVSg25k\ACTIVSg25k.RAW`   | `C:\Users\phlpt\Downloads\ACTIVSg25k\ACTIVSg25k.dyr`            |      18,108 |
| ACTIVSg70k  | `C:\Users\phlpt\Downloads\ACTIVSg70k\ACTIVSg70k.RAW`   | `C:\Users\phlpt\Downloads\ACTIVSg70k\ACTIVSg70k_dynamics.dyr`   |      40,418 |

The five files contain 67,941 DYR records. 54,870 records use models the
current DYR reader recognizes; 13,071 records use models that still need
support. Recognition is not dynamic validation: a model is complete only
after import, initialization, limits, and disturbed trajectories agree with
an external reference.

## Power-flow readiness

| Case        | MATPOWER                                                    | RAW                                                                                                                     | EPC                                                                       | Current conclusion                                                                                                                                            |
| ----------- | ----------------------------------------------------------- | ----------------------------------------------------------------------------------------------------------------------- | ------------------------------------------------------------------------- | ------------------------------------------------------------------------------------------------------------------------------------------------------------- |
| ACTIVSg500  | Solves                                                      | Solves                                                                                                                  | Solves                                                                    | Outputs agree within the established tolerance.                                                                                                               |
| ACTIVSg2000 | `case_ACTIVSg2000.m` solves                                 | Solves but differs from EPC by up to 0.03746 pu V / 0.00589 rad                                                         | Solves                                                                    | Control-device interpretation still needs parity work.                                                                                                        |
| ACTIVSg10k  | `case_ACTIVSg10k.m` solves: 10,000 buses, 12,706 links      | Solves: 10,000 buses / 12,706 links; max source-state V error 0.000743 pu                                               | Solves: 10,000 buses / 12,706 links; max source-state V error 0.002317 pu | Three-winding topology is validated across all 300 units; RAW convergence defect was negative fixed phase-shifter `COD` interpretation.                       |
| ACTIVSg25k  | `case_ACTIVSg25k.m` provides a 25,000-bus solved state      | Solves: 25,000 buses / 32,230 links; stays within 0.000101 pu V and 0.000124 rad of the RAW-supplied terminal-bus state | Not assessed                                                              | RAW and MATPOWER source states differ by up to 0.07851 pu V / 0.02221 rad on 24,250 common terminal buses, so cross-format parity is not yet established.     |
| ACTIVSg70k  | `case_ACTIVSg70k.m` provides 70,000 buses / 88,207 branches | Not run; source RAW expands to the same 70,000 buses / 88,207 links                                                     | Not assessed                                                              | PSS/E v33 with 2,100 three-winding transformers. No nonempty DC/FACTS sections were found; validate large-scale convergence and state parity before dynamics. |

## Dynamic-model inventory

| PSS/E model | ACTIVSg500 | ACTIVSg2000 | ACTIVSg10k | ACTIVSg25k | ACTIVSg70k |  Total | Current status / required work                                                                                                                                          |
| ----------- | ---------: | ----------: | ---------: | ---------: | ---------: | -----: | ----------------------------------------------------------------------------------------------------------------------------------------------------------------------- |
| `GENROU`    |         90 |         410 |      1,136 |      2,857 |      6,937 | 11,430 | Reader supported; continue initialization and trajectory validation.                                                                                                    |
| `SEXS`      |         90 |           0 |          0 |          0 |          0 |     90 | Reader supported; validate case configurations.                                                                                                                         |
| `TGOV1`     |         21 |           0 |          0 |          0 |          0 |     21 | Reader supported; validate case configurations.                                                                                                                         |
| `IEEEST`    |          2 |         434 |      1,851 |      4,101 |      9,243 | 15,631 | Reader supported for local-input configurations; validate variants used.                                                                                                |
| `IEEEG1`    |          0 |          43 |        162 |      1,115 |      3,518 |  4,838 | Reader supported; validate case configurations.                                                                                                                         |
| `ESDC1A`    |          0 |          12 |        163 |        123 |        427 |    725 | Reader supported; validate case configurations.                                                                                                                         |
| `EXAC2`     |          0 |          38 |        106 |        239 |        486 |    869 | Reader supported; validate case configurations.                                                                                                                         |
| `GGOV1`     |          0 |         367 |        974 |      1,742 |      3,419 |  6,502 | **Reader/model implemented.** Equation, initialization, DYR-order, coupling, residual, and analytic-Jacobian tests exist. Nonzero `TENG` is rejected.                   |
| `ESST4B`    |          0 |         278 |        745 |      1,396 |      3,187 |  5,606 | **Reader/model implemented.** Exact DYR mapping and GENSAL-coupled residual/Jacobian tests exist; external UEL/OEL inputs remain unrouted.                              |
| `GENSAL`    |          0 |          25 |        715 |      1,244 |      2,306 |  4,290 | **Reader/model implemented.** Salient-pole equations, quadratic saturation, initialization, controller signals, and Jacobians are covered.                              |
| `HYGOV`     |         39 |          25 |        715 |      1,244 |      2,306 |  4,329 | Dedicated `GovernorHygov` and DYR mapping implemented; validate case initialization, limits, and trajectories.                                                          |
| `SCRX`      |          0 |           5 |        312 |        446 |      1,053 |  1,816 | **Reader/model implemented; external trajectory open.** OpenIPSL/PowerDynamics equations, source selection, crowbar behavior, and DYR mapping are covered.              |
| `IEEET1`    |          0 |          23 |        214 |        942 |      1,907 |  3,086 | **Implemented; external trajectory open.** Native IEEE Type 1 model and exact DYR mapping are available; validate representative case configurations and trajectories.  |
| `EXPIC1`    |          0 |          61 |        153 |        287 |        569 |  1,070 | **Implemented; external trajectory open.** GridKit-specified equations, exact DYR schema, full block tests, and whole-case residual/Jacobian coverage are present.      |
| `ESDC2A`    |          0 |           1 |         87 |        202 |        104 |    394 | **Implemented and merged; external trajectory pending.** Shares DC2A/EXDC2 equations and DYR schema; nonzero unsupported `Switch` is rejected.                          |
| `ESAC6A`    |          0 |           7 |         26 |        194 |        583 |    810 | **Reader/model implemented; external trajectory open.** GridKit AC6A equations, saturation/rectifier behavior, initialization, and DYR mapping are covered.             |
| `EXAC1`     |          0 |           6 |         22 |        130 |        381 |    539 | Reader supported; validate case configurations.                                                                                                                         |
| `ESAC1A`    |          0 |           4 |         23 |        142 |        546 |    715 | **Implemented and merged; external trajectory pending.** Dedicated AC1A DYR mapping and control-element limits reuse the AC-exciter core; UEL/OEL routing remains open. |
| `GAST`      |         30 |           0 |          0 |          0 |          0 |     30 | **Implemented; external trajectory open.** OpenIPSL/ANDES/GridKit equations, DYR mapping, initialization, selector/limit, and Jacobian tests are present.               |
| `REECA1`    |          0 |           0 |          0 |        614 |        571 |  1,185 | **Missing.** Add renewable electrical control; implement and validate it with the associated `REGCA1` converter model.                                                  |
| `REGCA1`    |          0 |           0 |          0 |        614 |        571 |  1,185 | **Missing.** Add the renewable converter model; do not import it independently from `REECA1`.                                                                           |
| `WT3E1`     |          0 |           0 |          0 |        119 |        576 |    695 | **Missing.** Add the Type-3 wind electrical-control subsystem.                                                                                                          |
| `WT3G1`     |          0 |           0 |          0 |        119 |        576 |    695 | **Missing.** Add the Type-3 wind generator/drivetrain subsystem.                                                                                                        |
| `WT3P1`     |          0 |           0 |          0 |        119 |        576 |    695 | **Missing.** Add the Type-3 wind pitch-control subsystem.                                                                                                               |
| `WT3T1`     |          0 |           0 |          0 |        119 |        576 |    695 | **Missing.** Add the Type-3 wind turbine subsystem.                                                                                                                     |

The broader model-by-model mapping is maintained in the
[ANDES compatibility roadmap](andes-compatibility.md); this table is the
authoritative ACTIVSg demand inventory.

### GridKit equation and case overlay

GridKit at `C:\data\Documents\codeProjects\GridKit` commit `de7bea18`
provides a second C++-oriented source of model equations and JSON validation
artifacts. It materially improves the source position for `GAST` and
`EXPIC1`: GridKit implements `GASTPTI` and supplies ACTIVSg500/WECC240 case
paths, while its EXPIC1 documentation contains the complete PI, filter,
feedback, source/rectifier, saturation, and initialization equations.

| GridKit artifact                         | Models/case coverage                                                                                                 | How GridDyn should use it                                                                                                        |
| ---------------------------------------- | -------------------------------------------------------------------------------------------------------------------- | -------------------------------------------------------------------------------------------------------------------------------- |
| Native model sources                     | `GENROU`, `GENSAL`, `GASTPTI`, `HYGOV`, `TGOV1`, `IEEET1`, `ESDC1A`, `SEXS-PTI`, `IEEEST`, `REGCA`, `REECB`, `REPCA` | Compare model equations and, where the interfaces align, capture initialization and trajectory references.                       |
| Documented but not GridKit-native models | `EXPIC1`, `SCRX`, `ESAC6A`, `ESDC2A`, `ESST4B`, `EXAC1`, `EXAC2`, `EXDC1`, `GGOV1`, `IEEEG1`, `PSS1A`                | Use as equation specifications only; do not claim GridKit trajectory validation until a runnable implementation/case is present. |
| Self-contained regression examples       | Two-bus `IEEET1`, `TGOV1`, and `GENSAL`, with `.case.json`, `.solver.json`, and `.ref.csv`                           | Port as small GridDyn trajectory fixtures before relying on a large-case comparison.                                             |
| Large JSON cases                         | ACTIVSg200, ACTIVSg500, ACTIVSg10k, and WECC240                                                                      | ACTIVSg500 has supplied validation traces; WECC240 additionally exercises GAST/HYGOV/TGOV1 plus `REGCA`/`REECB`/`REPCA`.         |

The local GridKit ACTIVSg2000 directory is **not** a runnable dynamic input:
it contains its README and image only, and the README lists EXPIC1 among its
unimplemented models. Therefore the final ACTIVSg2000 integration remains
dependent on the separate RAW/DYR delivery; GridKit cannot substitute for it.

### ACTIVSg25k static DYR assessment

`ACTIVSg25k.dyr` was inspected as text only and was not loaded or run. It has
18,108 records in 22 model families. The current DYR dispatch recognizes 14
of the families used by this file (`GENROU`, `IEEEST`, `IEEEG1`, `ESDC1A`,
`ESDC2A`, `EXAC1`, `EXAC2`, `ESAC1A`, `GGOV1`, `ESST4B`, `GENSAL`, `HYGOV`,
`IEEET1`, `EXPIC1`, `SCRX`, and `ESAC6A`), accounting for
15,776 records. The other 2,332 records cannot yet be represented faithfully.

The largest remaining gap is now the renewable groups. `REECA1` and `REGCA1` must be initialized and
validated together; the four `WT3*` models are likewise one Type-3 wind-turbine
system, not four independent optional models. Existing recognition is not by
itself case validation: every supported family still needs applicable
case-configuration, initialization, limiter, and disturbance-trajectory
coverage.

### ACTIVSg70k static DYR and RAW assessment

`ACTIVSg70k_dynamics.dyr` was inspected as text only and was not loaded or run.
It has 40,418 records in 22 model families. The current DYR dispatch recognizes
35,336 records in 14 families (`GENROU`, `IEEEST`, `IEEEG1`, `ESDC1A`,
`ESDC2A`, `EXAC1`, `EXAC2`, `ESAC1A`, `GGOV1`, `ESST4B`, `GENSAL`, `HYGOV`,
`IEEET1`, `EXPIC1`, `SCRX`, and `ESAC6A`); 3,446 records
remain unsupported.

`GENSAL`, `HYGOV`, `GGOV1`, `ESST4B`, `SCRX`, and `ESAC6A` are recognized and
have focused model and reader tests, but still need large-case initialization
and trajectory validation. The coupled renewable demand is `REGCA1` plus
`REECA1` (571 each) and all four Type-3 wind models (576 each).

#### Full-dynamics execution boundary

The package contains both PowerWorld (`.PWB`, `.aux`, `.pwd`, and `.EPC`) and
PSS/E (`.RAW` and `.dyr`) representations. GridDyn's reference import path is
the paired `ACTIVSg70k.RAW` and `ACTIVSg70k_dynamics.dyr` files; the PowerWorld
artifacts are useful cross-tool references, but are not substitutes for the
RAW/DYR import. The DYR is a complete plant assembly, so a run must attach
every record for a machine ID before initializing the case.

| Dynamic assembly                | DYR families required together                     |                                  Records | Execution consequence                                                                                                                                              |
| ------------------------------- | -------------------------------------------------- | ---------------------------------------: | ------------------------------------------------------------------------------------------------------------------------------------------------------------------ |
| Round-rotor conventional plants | `GENROU`, exciter, governor, and optional `IEEEST` |                   6,937 `GENROU` records | The machine and the six currently recognized controller families need full initialization and trajectory validation; missing controller records cannot be dropped. |
| Salient-pole hydro plants       | `GENSAL` + `HYGOV` + exciter + optional `IEEEST`   | 2,306 `GENSAL` and 2,306 `HYGOV` records | Native machine and governor models are implemented; validate whole-plant initialization, bases, limits, controller interfaces, and disturbed trajectories.         |
| General-governor plants         | `GENROU` + `GGOV1` + exciter + optional `IEEEST`   |                    3,419 `GGOV1` records | Native selectable governor/turbine modes are implemented; validate case parameter combinations and reject nonzero `TENG` until transport delay is added.           |
| Renewable converter plants      | `REGCA1` + `REECA1`                                |                              571 of each | Blocked until the converter and electrical controller initialize and enforce their current-limit ordering as a pair.                                               |
| Type-3 wind plants              | `WT3G1` + `WT3E1` + `WT3P1` + `WT3T1`              |                              576 of each | Blocked until the complete generator, electrical, pitch, and turbine assembly is available; no individual `WT3*` record may be omitted.                            |

Accordingly, GridDyn may use the RAW alone for static power-flow work once
large-scale topology validation is complete, but it must reject a requested
full 70k DYR run until the remaining unsupported families are implemented:
`WT3P1`, `WT3T1`, `WT3E1`, `WT3G1`, `REGCA1`, and `REECA1`.
A strict diagnostic is preferable
to a partial dynamic simulation.

The supplied RAW is PSS/E v33. It has 67,900 terminal buses, 71,352 branches,
10,555 two-winding transformers, and 2,100 three-winding transformers. Its
delta-to-star expansion yields the 70,000 buses and 88,207 branches in the
MATPOWER file. All transformer records use `CW=1`, `CZ=1`, and `CM=1`; 2,083
winding records use fixed/manual `COD=-1` control. There are no nonempty DC,
FACTS, multisection-line, induction-machine, or system-switching-device
sections. Therefore no new RAW record family is required for this case, but
large-scale validation of the three-winding and fixed-control transformer paths
is required before accepting the power flow or initializing the DYR.

## Implementation order

The cross-case source availability and P1/P2 ranking is maintained in the
[OpenIPSL dynamic-model assessment](openipsl-compatibility.md#synthetic-case-demand-overlay).
That overlay distinguishes models that can be ported from an inspected
OpenIPSL/ANDES/PowerDynamics implementation from source gaps that require
equations before implementation.

1. **Power-flow prerequisites:** resolve the remaining ACTIVSg2000
   control-device parity difference, add grouped PSS/E remote-voltage
   regulation, and investigate the ACTIVSg25k RAW-versus-MATPOWER source-state
   mismatch. ACTIVSg10k RAW/EPC three-winding topology is validated.
2. **P1 conventional machine/governor validation:** `GENSAL`, `GGOV1`,
   `HYGOV`, and the merged `IEESGO` realization are implemented; add captured
   external trajectories and whole-case initialization coverage. GGOV1
   deliberately rejects nonzero `TENG` transport delay.
3. **P1 synchronous excitation:** `ESST4B`, `IEEET1`, `SCRX`, `EXPIC1`, and
   `ESAC6A` are implemented with exact DYR mappings and focused equation
   tests. Capture external trajectories; ANDES's `SEXS` conversion remains
   only an approximation for SCRX/ESAC6A.
4. **P1 renewable generation:** implement and validate `REGCA1` plus
   `REECA1` (and `REPCA1` for Texas7k), then the coupled
   `WT3G1`/`WT3E1`/`WT3P1`/`WT3T1` Type-3 system. The latter two models have
   no exact external source and must be derived or obtained as part of the
   complete system, not silently omitted.
5. **P2 remaining excitation/source gaps:** `ESDC2A`, `ESAC1A`, `EXPIC1`, and
   `ESAC6A` are merged; capture independent trajectories and add UEL/OEL
   routing where the exciter interface needs it.
6. **Reader and validation hardening:** table-driven DYR dispatch, strict
   unknown-model diagnostics, exact bus-plus-machine-ID resolution, minimized
   fixtures, and whole-case trajectory regressions. Add dedicated
   representative-parameter stability cases for `EXPIC1` and `GAST`, including
   EXPIC1 limiter/rectifier behavior and GAST temperature-selector and valve
   anti-windup transitions.

## Guardrails and acceptance criteria

### Three-winding transformer scope and simplifications

GridDyn represents a three-winding transformer as a star bus and three AC
transformer legs. RAW pairwise leakage impedances are converted from their
delta form to those three star-leg impedances. The initial implementation
supports the ordinary fixed-transformer cases (`CW=1`, `CZ=1/2/3`, and
`CM=1`), per-winding ratings, tap ratios, and phase shifts.

For simple planning and test cases, the following deliberately conservative
simplifications are acceptable when documented in the test result:

- Treat `CW=2` or `CW=3` winding-voltage entries as already normalized fixed
  taps, with a reader warning. Use this only when all three nominal voltage
  bases are consistent; otherwise the case needs the dedicated base conversion.
- Treat nonzero `COD` control records as their supplied fixed starting tap.
  This preserves the base power-flow topology but does not reproduce regulated
  tap movement or reactive/active-flow control.
- Omit or warn on `CM=2` magnetizing-loss conversion rather than inventing an
  admittance. `CM=1` conductance/susceptance is applied to the first star leg.
- A zero rating is retained as GridDyn's existing “no thermal limit” value; it
  is not replaced with an arbitrary finite rating.

These shortcuts are only suitable for uncomplicated steady-state cases. A
reader is complete only after all PSS/E control codes, voltage-base forms, and
magnetizing representations used by the target cases are implemented and
validated.

- Never silently replace an unsupported PSS/E record with a similarly named
  GridDyn model.
- Resolve DYR controllers by **RAW bus number and quoted machine ID**, never
  generator-array position.
- Validate record length, numeric domains, controller limits, and unit bases
  before attachment.
- Before a full DYR comparison, RAW, EPC, and MATPOWER must retain equivalent
  bus and transformer topology and agree within declared voltage/angle
  tolerances.
- For each new model, require parser/attachment, initialization/limiter, and
  disturbed-trajectory tests against a documented PSS/E or trusted reference.
- Completion requires strict loading with no unknown records plus whole-case
  equilibrium and fault/frequency trajectory agreement.
