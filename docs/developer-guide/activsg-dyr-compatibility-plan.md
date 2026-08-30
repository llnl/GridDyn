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

The five files contain 67,941 DYR records. 34,143 records use models the
current DYR reader recognizes; 33,798 records use models that still need
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

| PSS/E model | ACTIVSg500 | ACTIVSg2000 | ACTIVSg10k | ACTIVSg25k | ACTIVSg70k |  Total | Current status / required work                                                                                         |
| ----------- | ---------: | ----------: | ---------: | ---------: | ---------: | -----: | ---------------------------------------------------------------------------------------------------------------------- |
| `GENROU`    |         90 |         410 |      1,136 |      2,857 |      6,937 | 11,430 | Reader supported; continue initialization and trajectory validation.                                                   |
| `SEXS`      |         90 |           0 |          0 |          0 |          0 |     90 | Reader supported; validate case configurations.                                                                        |
| `TGOV1`     |         21 |           0 |          0 |          0 |          0 |     21 | Reader supported; validate case configurations.                                                                        |
| `IEEEST`    |          2 |         434 |      1,851 |      4,101 |      9,243 | 15,631 | Reader supported for local-input configurations; validate variants used.                                               |
| `IEEEG1`    |          0 |          43 |        162 |      1,115 |      3,518 |  4,838 | Reader supported; validate case configurations.                                                                        |
| `ESDC1A`    |          0 |          12 |        163 |        123 |        427 |    725 | Reader supported; validate case configurations.                                                                        |
| `EXAC2`     |          0 |          38 |        106 |        239 |        486 |    869 | Reader supported; validate case configurations.                                                                        |
| `GGOV1`     |          0 |         367 |        974 |      1,742 |      3,419 |  6,502 | **Missing.** Add an exact general turbine/governor; do not silently substitute `TGOV1`.                                |
| `ESST4B`    |          0 |         278 |        745 |      1,396 |      3,187 |  5,606 | **Missing.** Add static-exciter equations, DYR schema, initialization, and limits.                                     |
| `GENSAL`    |          0 |          25 |        715 |      1,244 |      2,306 |  4,290 | **Missing.** Add/audit a salient-pole machine before attaching its controllers.                                        |
| `HYGOV`     |         39 |          25 |        715 |      1,244 |      2,306 |  4,329 | **Missing.** Audit `GovernorHydro`; add dedicated HYGOV if not equation-compatible.                                    |
| `SCRX`      |          0 |           5 |        312 |        446 |      1,053 |  1,816 | **Missing.** Add static controlled-rectifier exciter.                                                                  |
| `IEEET1`    |          0 |          23 |        214 |        942 |      1,907 |  3,086 | **Missing.** Add/audit IEEE Type 1 exciter.                                                                            |
| `EXPIC1`    |          0 |          61 |        153 |        287 |        569 |  1,070 | **Missing.** Add the excitation controller/interface behavior.                                                         |
| `ESDC2A`    |          0 |           1 |         87 |        202 |        104 |    394 | **Missing.** Add/audit DC2A exciter.                                                                                   |
| `ESAC6A`    |          0 |           7 |         26 |        194 |        583 |    810 | **Missing.** Add AC6A exciter.                                                                                         |
| `EXAC1`     |          0 |           6 |         22 |        130 |        381 |    539 | Reader supported; validate case configurations.                                                                        |
| `ESAC1A`    |          0 |           4 |         23 |        142 |        546 |    715 | **Missing.** Add AC1A exciter.                                                                                         |
| `GAST`      |         30 |           0 |          0 |          0 |          0 |     30 | **Missing.** Add a dedicated gas-turbine governor; do not substitute `TGOV1`.                                          |
| `REECA1`    |          0 |           0 |          0 |        614 |        571 |  1,185 | **Missing.** Add renewable electrical control; implement and validate it with the associated `REGCA1` converter model. |
| `REGCA1`    |          0 |           0 |          0 |        614 |        571 |  1,185 | **Missing.** Add the renewable converter model; do not import it independently from `REECA1`.                          |
| `WT3E1`     |          0 |           0 |          0 |        119 |        576 |    695 | **Missing.** Add the Type-3 wind electrical-control subsystem.                                                         |
| `WT3G1`     |          0 |           0 |          0 |        119 |        576 |    695 | **Missing.** Add the Type-3 wind generator/drivetrain subsystem.                                                       |
| `WT3P1`     |          0 |           0 |          0 |        119 |        576 |    695 | **Missing.** Add the Type-3 wind pitch-control subsystem.                                                              |
| `WT3T1`     |          0 |           0 |          0 |        119 |        576 |    695 | **Missing.** Add the Type-3 wind turbine subsystem.                                                                    |

The broader model-by-model mapping is maintained in the
[ANDES compatibility roadmap](andes-compatibility.md); this table is the
authoritative ACTIVSg demand inventory.

### ACTIVSg25k static DYR assessment

`ACTIVSg25k.dyr` was inspected as text only and was not loaded or run. It has
18,108 records in 22 model families. The current DYR dispatch recognizes six
of the families used by this file (`GENROU`, `IEEEST`, `IEEEG1`, `ESDC1A`,
`EXAC1`, and `EXAC2`), accounting for 8,565 records. The other 9,543 records
cannot yet be represented faithfully.

The largest immediate dependencies are the `GENSAL` machine and its controller
families (`HYGOV`, `ESST4B`, and `IEEET1`), the general governor `GGOV1`, and
the renewable groups. `REECA1` and `REGCA1` must be initialized and validated
together; the four `WT3*` models are likewise one Type-3 wind-turbine system,
not four independent optional models. Existing recognition is only parser
coverage: every supported family still needs case-configuration,
initialization, limiter, and disturbance-trajectory validation.

### ACTIVSg70k static DYR and RAW assessment

`ACTIVSg70k_dynamics.dyr` was inspected as text only and was not loaded or run.
It has 40,418 records in 22 model families. The current DYR dispatch recognizes
20,992 records in six families (`GENROU`, `IEEEST`, `IEEEG1`, `ESDC1A`,
`EXAC1`, and `EXAC2`); 19,426 records remain unsupported.

The primary gaps are `GGOV1` (3,419), `ESST4B` (3,187), `GENSAL` (2,306),
`HYGOV` (2,306), and `IEEET1` (1,907). The coupled renewable demand is
`REGCA1` plus `REECA1` (571 each) and all four Type-3 wind models (576 each).
`SCRX` (1,053), `EXPIC1` (569), `ESAC6A` (583), `ESAC1A` (546), and `ESDC2A`
(104) complete the missing inventory.

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

1. **Power-flow prerequisites:** resolve the remaining ACTIVSg2000
   control-device parity difference, add grouped PSS/E remote-voltage
   regulation, and investigate the ACTIVSg25k RAW-versus-MATPOWER source-state
   mismatch. ACTIVSg10k RAW/EPC three-winding topology is validated.
2. **Machine and governor coverage:** implement `GENSAL`, `GGOV1`, `HYGOV`,
   and `GAST`. These cover 15,151 unsupported records and unblock many of
   their attached excitation/control models.
3. **Largest synchronous-excitation gap:** implement `ESST4B`, `IEEET1`,
   `SCRX`, and `EXPIC1` (11,578 records).
4. **Renewable generation:** implement and validate `REGCA1` plus `REECA1`,
   then the coupled `WT3G1`/`WT3E1`/`WT3P1`/`WT3T1` Type-3 system (5,150
   records).
5. **Remaining excitation families:** `ESDC2A`, `ESAC6A`, and `ESAC1A`
   (1,919 records).
6. **Reader and validation hardening:** table-driven DYR dispatch, strict
   unknown-model diagnostics, exact bus-plus-machine-ID resolution, minimized
   fixtures, and whole-case trajectory regressions.

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
