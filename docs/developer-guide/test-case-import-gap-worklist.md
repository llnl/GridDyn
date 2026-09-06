# Test case import gap worklist

This document tracks import/model coverage gaps found by scanning the external
GridDyn test-case corpus at:

```text
C:\data\Documents\codeProjects\griddyn_test_cases
```

Note: the initially requested path
`C:\data\Documents\codeProjects\griddyn\_test\_cases` was not present on the
local machine used for this scan.

The intent is planning: each item lists the work to do, the code area likely to
change, and the corpus files that exercise the gap. Unless otherwise noted,
paths under "Exercised by" are relative to the case corpus root above.

Last audit basis: static parser/source scan plus small `gridDynMain.exe`
smoke runs on 2026-09-05.

Resolved during this audit:

- PSS/E `GENROE` and `IEEEX1` are no longer unsupported DYR records. Native
  models, exact field mappings, initialization/residual/Jacobian checks, and
  reader/component tests are now present. The historical corpus counts below
  remain the pre-integration scan; representative whole-case and independent
  external-trajectory validation are still follow-up work.
- RAW generator step-up transformer import no longer belongs on the active
  worklist. It was isolated from
  `interpss\psse\v30\42bus_3winding_from_PSSE_V30.raw` to the fifth generator
  record, at bus 301, whose nonzero `RT/XT` creates an internal step-up
  transformer bus/link. The reader used `gen->find("bus")` instead of the
  generator's actual parent bus, which could resolve to the static default bus;
  adding the internal step-up bus through that default bus failed with the
  generic `failure to add object`. The move also now keeps a temporary owning
  reference while reparenting the generator. A regression fixture was added as
  `test\test_files\input_tests\raw_generator_step_up.raw`.
- RAW v30 transformer `maxtap` import no longer belongs on the active worklist.
  `interpss\psse\v30\42bus_3winding_from_PSSE_V30.raw` and the `NoDC` variant
  contain `COD=4` transformer records. The reader created a fixed `AcLine` for
  those unsupported control codes, but then still applied adjustable-transformer
  fields such as `maxtap`. The parser now applies those fields only when the
  created object is an `AdjustableTransformer`; unsupported control codes are
  imported as fixed transformers with a warning. A regression fixture was added
  as `test\test_files\input_tests\raw_transformer_cod4_fixed.raw`.
- The 42-bus RAW v30 power-flow failure is resolved. Its independent causes
  were reversed receiving-end DC-link power, VSC `MODE=2` terminals being
  treated as voltage controllers, and stale aggregate Q limits when generators
  behind explicit step-up impedances registered after their controlled bus.
  The normal power-flow path now converges with 103 states, 49 buses, and 63
  links. Portable regressions cover DC transfer direction, VSC terminal modes,
  and late remote-generator Q-limit aggregation.

## Loader / extension dispatch gaps

Relevant source: `src/fileInput/fileInput.cpp`.

### [ ] FILE-001: Implement or explicitly reject UCT input

- Current behavior: `.uct` has a dispatch branch but no implementation, so UCT
  files load as an empty system.
- Format review:
  - The `.uct` files do not match any existing reader format.
  - They use UCTE/UCT-style section markers such as `##C`, `##BaseVoltage`,
    `##N`, `##Z*`, `##L`, `##T`, `##R`, and `##TT`.
  - This does not match:
    - CDF/IEEE: `BUS DATA FOLLOWS`, `BRANCH DATA FOLLOWS`, `-999`
      terminators.
    - RAW/PSS/E: comma-separated record sections with `0 / END ... BEGIN ...`
      markers.
    - EPC: lowercase `bus data [n]`, `branch data [n]`, etc.
    - PSP: numeric section code cards such as `1`, `4`, `5`, `15`.
- Work needed:
  - Either implement `loadUct` and wire it into `loadFile`, or emit a clear
    unsupported-format diagnostic instead of silently producing an empty model.
- Exercised by:
  - `interpss\ucte\AusPower_TestCase_Xfr.uct`
  - `interpss\ucte\IEEE14.uct`
  - `interpss\ucte\MarioTest1_Simple.uct`
  - `interpss\ucte\MarioTest2_Xfr.uct`
  - `interpss\ucte\MarioTest3_XfrReg.uct`
  - `interpss\ucte\MarioTest4_PSXfr1.uct`
- Smoke result:
  - `IEEE14.uct` loaded with `0 buses=0 links=0 gens=0 loads=0`.

### [ ] FILE-002: Recognize `.ieee` as IEEE CDF, or require extension override

- Current behavior: CDF is reached through `.cdf` and `.txt`; `.ieee` files are
  not mapped and currently load as empty systems.
- Format review:
  - Most `.ieee` files in `interpss\ieee_format` are directly CDF-shaped and
    match the current fixed-column CDF reader:
    - first card contains the system base MVA in the CDF location,
    - bus section begins with `BUS DATA FOLLOWS`,
    - branch section begins with `BRANCH DATA FOLLOWS`,
    - sections terminate with `-999`.
  - The `UCTE_2000_*.ieee` files are confusingly named but also appear
    CDF-shaped, not UCT-shaped.
  - `ieee14_comma.ieee` has CDF section markers, but the data rows are
    comma-delimited. That does not match the current CDF reader's fixed-column
    substring parsing. A direct extension mapping would help the normal
    fixed-column `.ieee` files but not the comma-delimited variant.
- Work needed:
  - Map fixed-column `.ieee` files to the CDF reader if that is intended.
  - Add CDF validation/detection so comma-delimited CDF variants can be rejected
    cleanly or normalized before parsing.
  - Alternatively add a diagnostic that instructs users to rename/override
    compatible fixed-column `.ieee` files as `.cdf`/`.txt`.
- Exercised by:
  - `interpss\ieee_format\ieee14.ieee`
  - `interpss\ieee_format\ieee14_comma.ieee`
  - `interpss\ieee_format\Ieee14Bus.ieee`
  - `interpss\ieee_format\ieee30.ieee`
  - `interpss\ieee_format\ieee57.ieee`
  - `interpss\ieee_format\ieee118.ieee`
  - `interpss\ieee_format\ieee300.ieee`
  - `interpss\ieee_format\UCTE_2000_Summer.ieee`
  - `interpss\ieee_format\UCTE_2000_WinterOffPeak.ieee`
  - `interpss\ieee_format\UCTE_2000_WinterPeak.ieee`
- Smoke result:
  - `ieee14.ieee` loaded with `0 buses=0 links=0 gens=0 loads=0`.
  - `interpss\ieee_format\ieee300Bus.txt` did load as a CDF-style case.
- Compatible with existing reader:
  - Yes for fixed-column `.ieee` files, if routed to `loadCdf`.
  - No for `ieee14_comma.ieee` without comma normalization or a separate
    delimited-CDF path.

### [ ] FILE-003: Avoid treating arbitrary `.txt` tables/reports as CDF cases

- Current behavior: all `.txt` files route to the CDF reader.
- Work needed:
  - Add CDF header validation before parsing, or improve diagnostics when a
    `.txt` file is not CDF.
- Exercised by:
  - `RTS96\branch_data.txt`
  - `RTS96\bus_data.txt`
  - `RTS96\gen_param.txt`
  - `RTS96\dc_line_data.txt`
  - `interpss\psse\PSSE_5bus_output.TXT`
  - `interpss\ipssdata\A.txt`
  - `interpss\ipssdata\B.txt`
- Smoke result:
  - `RTS96\branch_data.txt` aborts with `unable to convert string`.

### [ ] FILE-004: Add PSLF `.dyd` dynamic-data support or unsupported diagnostic

- Current behavior: `.dyd` has no top-level loader mapping. These files are
  currently unusable as dynamic imports.
- Work needed:
  - Add a PSLF dynamic reader, or emit a direct unsupported-format diagnostic.
  - If implementing, decide whether to normalize model names through existing
    DYR model classes where schemas match.
- Exercised by:
  - `39busCase\IEEE 39 bus.dyd`
  - `3mach-inf_bus\PSLF\ThreeMIB_Benchmark_System.dyd`
  - `Austrailian14bus\LF_Case01_R4_S\AU14GenModel.dyd`
  - `Austrailian14bus\LF_Case02_R4_S\AU14GenModel.dyd`
  - `Austrailian14bus\LF_Case03_R4_S\AU14GenModel.dyd`
  - `Austrailian14bus\LF_Case04_R4_S\AU14GenModel.dyd`
  - `Austrailian14bus\LF_Case05_R4_S\AU14GenModel.dyd`
  - `Austrailian14bus\LF_Case06_R4_S\AU14GenModel.dyd`
  - `illinois200\Illinois200_dynamics.dyd`
  - `pslf\2tdc.dyd`
  - `pslf\motor.dyd`
  - `pslf\mtdc-PDCI.dyd`
  - `pslf\t3ps.dyd`
  - `pslf\t3ps1.dyd`
  - `pslf\test21exc-V161.dyd`
  - `pslf\upfc.dyd`
  - `TwoAreaSystem\1.Benchmark_4ger_ESST1A_TGR_2015\Benchmark_4ger_33_2015.dyd`
  - `TwoAreaSystem\2.Benchmark_4ger_ESST1A_noTGR_PSSmod_2015\Benchmark_4ger_33_2015.dyd`
  - `TwoAreaSystem\3.Benchmark_4ger_ESST1A_noTGR_PSSori_2015\Benchmark_4ger_33_2015.dyd`
- Models observed in `.dyd` files:
  - `ieeest` 102, `genrou` 87, `tgov1` 52, `gencls` 22,
    `vmetr` 18, `esac1a` 18, `gensal` 17, `exst1` 15,
    `wlwscc` 14, `exac1` 7, `hygov` 5, `fmetr` 5,
    `motor1` 4, `blwscc` 4, `exdc1` 4, `gencc` 4,
    `gast` 3, `scrx` 3, `gentpf` 2, `ieeeg1` 2,
    `dcmt` 2, plus one each of `sexs`, `esdc2a`, `epcmod`,
    `exeli2`, `esst7b`, `dc2t`, `esac3a`, `expic1`,
    `esst5b`, `esac7b`, `esst6b`, `esdc1a`, `esac2a`,
    `upfc`, `esdc3a`.

### [ ] FILE-005: Add diagnostics for unsupported binary/container formats

- Current behavior: these files are present in the corpus but are not mapped by
  the loader. Some are expected to be unsupported, but the failure mode should be
  explicit.
- Work needed:
  - Decide policy: unsupported diagnostic, conversion guidance, or native
    support.
- Exercised by:
  - `.save` PSS/E binary saves: 18 files.
  - `.pwb` / `.pwd` PowerWorld files: 22 each.
  - `.aux` PowerWorld auxiliary files: 5 files.
  - `.xlsx`: 4 files.
  - `.zip`: 14 files.
  - `.rar`: 3 files.

## DYR parser and dynamic model gaps

Relevant sources:

- `src/fileInput/gridDynReadDYR.cpp`
- `src/griddyn/exciters/*`
- `src/griddyn/governors/*`
- `src/griddyn/genmodels/*`

The DYR reader dispatches a fixed set of exact quoted model names. Unknown names
are currently printed as `unknown object type ...` and skipped.

### [ ] DYR-003: Add unsupported DYR model coverage

- Current behavior:
  - Unsupported model records are skipped.
  - Some cases continue; others later fail because dependent supported models
    cannot attach to expected generator/controller state.
- Work needed:
  - Add model loaders/classes, or create deliberate compatibility aliases where
    the model is equivalent to a supported implementation.
  - At minimum, accumulate unsupported-model diagnostics by file and model.
- Highest-count unsupported models in the corpus:

| Count | Model    |
| ----: | -------- |
|   229 | `DISTR1` |
|   109 | `IEELBL` |
|    88 | `ESST1A` |
|    75 | `IEEET2` |
|    66 | `COMP`   |
|    59 | `EXAC3`  |
|    54 | `IEEET3` |
|    46 | `EXST3`  |
|    44 | `IEE2ST` |
|    43 | `IEEEG3` |
|    40 | `IEEEG2` |
|    39 | `IEEEVC` |
|    36 | `GENSAE` |
|    35 | `IEEEX4` |
|    35 | `CSVGN1` |
|    34 | `IEEEX2` |
|    31 | `TIOCR1` |
|    31 | `CGEN1`  |

- Other unsupported models observed:
  - `IEEET4`, `IEEET5`, `GAST2A`, `CRCMGV`, `WPIDHY`,
    `STAB1`, `EXST2`, `TGOV3`, `OEX12T`, `PSS2A`,
    `ESAC2A`, `IEET1A`, `IEET5A`, `STAB3`, `MNLEX2`,
    `IEELAL`, `IEEEX3`, `WESGOV`, `ESAC3A`, `TGOV2`,
    `PTIST3`, `EXAC1A`, `IEEX2A`, `PTIST1`, `BBSEX1`,
    `EXST2A`, `ESAC5A`, `USRMDL`, `HYGOV2`, `SYSANG`.
- Reference-source triage:
  - Local open-source references checked:
    - `C:\data\Documents\codeProjects\OpenIPSL`
    - `C:\data\Documents\codeProjects\PowerDynamics.jl`
    - `C:\data\Documents\codeProjects\ANDES`
    - `C:\data\Documents\codeProjects\GridKit`
  - GridKit has useful adjacent conventional-model references, but no exact
    source was found for this specific unsupported batch beyond adjacent models
    such as `GENROU`, `GENSAL`, `GASTPTI`, `HYGOV`, `TGOV1`, `IEEET1`,
    `EXAC1`, `EXAC2`, `ESDC2A`, `SCRX`, `EXPIC1`, `PSS1A`, `REGCA`,
    `REECB`, and `REPCA`.

| Planning group                                 | Models                                                                                                                                                                                                                                                                                                                | Source position                                                                                                                                                                                                        | Suggested action                                                                                                |
| ---------------------------------------------- | --------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------- | ---------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------- | --------------------------------------------------------------------------------------------------------------- |
| First wave: high-count and source-backed       | `ESST1A`, `IEEET2`, `IEEET3`, `IEEEG2`, `IEEEVC`, `GENSAE`, `CSVGN1`                                                                                                                                                                                                                                                  | Exact equations found in OpenIPSL, PowerDynamics, or ANDES. `IEEEX1` and `GENROE` are now implemented and require only external trajectories. | Port/audit exact models; add DYR schema, initialization, limits, and disturbed-trajectory tests.                |
| Second wave: source-backed but more structural | `IEE2ST`, `PSS2A`, `STAB3`, `MNLEX2`, `WPIDHY`, `ESAC2A`, `ESAC5A`                                                                                                                                                                                                                                                    | Exact equations found, but require stabilizer, UEL/OEL, compensator, hydro-governor, or exciter-interface work.                                                                                                        | Implement after the core machine/exciter/governor path is stable.                                               |
| Archaeology / external documentation needed    | `DISTR1`, `IEELBL`, `COMP`, `EXAC3`, `EXST3`, `IEEEX4`, `IEEEX2`, `TIOCR1`, `CGEN1`, `IEEET4`, `IEEET5`, `GAST2A`, `CRCMGV`, `STAB1`, `EXST2`, `TGOV3`, `OEX12T`, `IEET1A`, `IEET5A`, `IEELAL`, `IEEEX3`, `WESGOV`, `ESAC3A`, `TGOV2`, `PTIST3`, `EXAC1A`, `IEEX2A`, `PTIST1`, `BBSEX1`, `EXST2A`, `HYGOV2`, `SYSANG` | No exact local OpenIPSL/GridKit/PowerDynamics/ANDES source found. Several `interpss\psse\v30\Bus200` records look like protection, distribution, or legacy relay/control data rather than ordinary generator controls. | Obtain PSS/E/InterPSS/vendor documentation, or choose explicit unsupported diagnostics/approved approximations. |
| Blocked external dependency                    | `USRMDL`                                                                                                                                                                                                                                                                                                              | User-written model; DYR parameters alone are insufficient.                                                                                                                                                             | Obtain compiled-model equations/source or an approved replacement before implementation.                        |

- Exact source hits:

| Model    | Source found                        | Notes                                                                                       |
| -------- | ----------------------------------- | ------------------------------------------------------------------------------------------- |
| `GENROE` | OpenIPSL + PowerDynamics.jl         | **Implemented; external trajectory pending.** Dedicated exponential saturation; do not alias to quadratic-saturation `GENROU`. |
| `GENSAE` | OpenIPSL + PowerDynamics.jl         | PowerDynamics has OpenIPSL validation tests; do not alias to quadratic-saturation `GENSAL`. |
| `ESST1A` | OpenIPSL + PowerDynamics.jl + ANDES | Common static exciter with multiple source paths.                                           |
| `IEEEX1` | OpenIPSL + ANDES                    | **Implemented; external trajectory pending.** Formerly the highest-count source-backed gap. |
| `IEEET2` | OpenIPSL                            | Exact OpenIPSL model exists.                                                                |
| `IEEET3` | ANDES                               | Exact ANDES implementation exists.                                                          |
| `IEEEG2` | OpenIPSL                            | Exact OpenIPSL governor model exists.                                                       |
| `IEEEVC` | OpenIPSL + ANDES                    | Requires terminal-current compensation / voltage-compensator interface work.                |
| `IEE2ST` | OpenIPSL                            | Exact stabilizer model source exists.                                                       |
| `PSS2A`  | OpenIPSL                            | Exact stabilizer model source exists.                                                       |
| `STAB3`  | OpenIPSL                            | Exact stabilizer model source exists.                                                       |
| `MNLEX2` | OpenIPSL                            | Exact under-excitation limiter model source exists.                                         |
| `WPIDHY` | OpenIPSL                            | Exact hydro governor model source exists.                                                   |
| `ESAC2A` | OpenIPSL                            | Exact exciter model source exists.                                                          |
| `ESAC5A` | ANDES                               | Exact ANDES implementation exists.                                                          |
| `CSVGN1` | OpenIPSL                            | Exact static shunt compensator model source exists.                                         |

- Exercised by:

| File                                                                                 | Unsupported count | Main unsupported models                                             |
| ------------------------------------------------------------------------------------ | ----------------: | ------------------------------------------------------------------- |
| `Base.dyr`                                                                           |              1599 | `IEEEX1`, `IEEET2`, `COMP`, `EXAC3`, `IEEET3`, `EXST3`, many others |
| `interpss\psse\v30\Bus200\200bus-gen-0805.dyr`                                       |               450 | `DISTR1`, `IEELBL`, `CGEN1`, `TIOCR1`, `IEE2ST`, `OEX12T`           |
| `Austrailian14bus\LF_Case01_R4_S\AU14GenModel.dyr`                                   |                29 | `GENROE`, `ESST1A`, `CSVGN1`, `IEELAL`                              |
| `Austrailian14bus\LF_Case02_R4_S\AU14GenModel.dyr`                                   |                29 | `GENROE`, `ESST1A`, `CSVGN1`, `IEELAL`                              |
| `Austrailian14bus\LF_Case03_R4_S\AU14GenModel.dyr`                                   |                29 | `GENROE`, `ESST1A`, `CSVGN1`, `IEELAL`                              |
| `Austrailian14bus\LF_Case04_R4_S\AU14GenModel.dyr`                                   |                29 | `GENROE`, `ESST1A`, `CSVGN1`, `IEELAL`                              |
| `Austrailian14bus\LF_Case05_R4_S\AU14GenModel.dyr`                                   |                29 | `GENROE`, `ESST1A`, `CSVGN1`, `IEELAL`                              |
| `Austrailian14bus\LF_Case06_R4_S\AU14GenModel.dyr`                                   |                29 | `GENROE`, `ESST1A`, `CSVGN1`, `IEELAL`                              |
| `TwoAreaSystem\1.Benchmark_4ger_ESST1A_TGR_2015\Benchmark_4ger_33_2015.dyr`          |                 8 | `ESST1A`, `GENROE`                                                  |
| `TwoAreaSystem\2.Benchmark_4ger_ESST1A_noTGR_PSSmod_2015\Benchmark_4ger_33_2015.dyr` |                 8 | `ESST1A`, `GENROE`                                                  |
| `TwoAreaSystem\3.Benchmark_4ger_ESST1A_noTGR_PSSori_2015\Benchmark_4ger_33_2015.dyr` |                 8 | `ESST1A`, `GENROE`                                                  |
| `brazil7Gen\PSLF\Brazilian_7_bus_Equiv_Model.dyr`                                    |                 5 | `GENSAE`                                                            |
| `brazil7Gen\PSSE\Brazilian_7_bus_Equiv_Model.dyr`                                    |                 5 | `GENSAE`                                                            |
| `3mach-inf_bus\PSSE\ThreeMIB_Benchmark_System.dyr`                                   |                 3 | `GENSAE`, `GENROE`                                                  |

### [ ] DYR-004: Decide compatibility behavior for EXAC1 `TB == 0`

- Current behavior:
  - `EXAC1` is recognized, but the Australian DYR cases abort with:
    `invalid parameter value for EXAC1 TB must be positive and finite`.
- Work needed:
  - Verify whether PSS/E permits `TB = 0` for this model and, if so, implement
    a bypass/degenerate lead-lag behavior.
  - If not supported, improve diagnostics to include record bus/id/model/file.
- Exercised by:
  - `Austrailian14bus\LF_Case01_R4_S\AU14GenModel.RAW`
    - `Austrailian14bus\LF_Case01_R4_S\AU14GenModel.dyr`
  - Same pattern for `LF_Case02_R4_S` through `LF_Case06_R4_S`.
- Smoke result:
  - All six Australian RAW+DYR pairs abort with the same `EXAC1 TB` error.

### [ ] DYR-005: Improve generator matching diagnostics and compatibility

- Current behavior:
  - Supported model records can fail if no generator matches the DYR bus/id.
  - Bus200 aborts with `ESST3A requires an existing generator matching its bus and ID`.
- Work needed:
  - Audit RAW generator IDs vs DYR machine IDs.
  - Improve diagnostic with file, model, bus, id, and nearby generator candidates.
  - Decide whether additional legacy matching rules are needed.
- Exercised by:
  - `interpss\psse\v30\Bus200\200busV29-peak.raw`
    - `interpss\psse\v30\Bus200\200bus-gen-0805.dyr`
- Smoke result:
  - Aborts with `ESST3A requires an existing generator matching its bus and ID`.

## RAW / PSS/E power-flow gaps

Relevant source: `src/fileInput/gridDynReadRAW.cpp`.

The RAW reader skips sections mapped to `UNKNOWN`. Many files contain empty
section markers; the table below lists nonempty skipped sections found in the
corpus.

### [ ] RAW-001: Implement or explicitly diagnose FACTS device data

- Current behavior:
  - `BEGIN FACTS CONTROL DEVICE DATA` / `BEGIN FACTS DEVICE DATA` is skipped as
    an unknown section.
- Work needed:
  - Add FACTS device parsing/modeling, or emit a clear unsupported electrical
    device diagnostic.
- Exercised by:

| File                                                      | Records |
| --------------------------------------------------------- | ------: |
| `interpss\psse\v30\42bus_3winding_from_PSSE_V30.raw`      |       2 |
| `interpss\psse\v30\42bus_3winding_from_PSSE_V30_NoDC.raw` |       2 |

### [ ] RAW-002: Implement multi-terminal DC data

- Current behavior:
  - Two-terminal DC and VSC DC are handled, but multi-terminal DC is skipped.
- Work needed:
  - Add multi-terminal DC parsing/modeling, or emit a clear unsupported
    electrical device diagnostic.
- Exercised by:

| File                                                 | Records |
| ---------------------------------------------------- | ------: |
| `interpss\psse\v30\42bus_3winding_from_PSSE_V30.raw` |      14 |

### [ ] RAW-003: Implement multi-section line groups

- Current behavior:
  - Multi-section line groups are skipped as unknown.
- Work needed:
  - Parse the group definitions and connect them to branch records, or document
    why they are safe to ignore for current PF use.
- Exercised by:

| File                                                      | Records |
| --------------------------------------------------------- | ------: |
| `interpss\edispatch\CR113Bus.raw`                         |       1 |
| `interpss\edispatch\savnw.raw`                            |       2 |
| `interpss\psse\PSSE_GuideSample.raw`                      |       2 |
| `interpss\psse\v30\42bus_3winding_from_PSSE_V30.raw`      |       1 |
| `interpss\psse\v30\42bus_3winding_from_PSSE_V30_NoDC.raw` |       1 |

### [ ] RAW-004: Decide treatment for area interchange and inter-area transfer

- Current behavior:
  - Area/inter-area records are skipped. These are often metadata/dispatch, but
    may matter for interchange-constrained studies or round-trip fidelity.
- Work needed:
  - Decide whether to model them, preserve them as metadata, or explicitly log
    that they are ignored.
- Exercised by:

| Section             | File                                                      | Records |
| ------------------- | --------------------------------------------------------- | ------: |
| Area interchange    | `interpss\psse\LFModel_testV26.raw`                       |       4 |
| Area interchange    | `pf.output.raw`                                           |       2 |
| Inter-area transfer | `interpss\edispatch\savnw.raw`                            |       4 |
| Inter-area transfer | `interpss\psse\PSSE_GuideSample.raw`                      |       4 |
| Inter-area transfer | `interpss\psse\v30\42bus_3winding_from_PSSE_V30.raw`      |       5 |
| Inter-area transfer | `interpss\psse\v30\42bus_3winding_from_PSSE_V30_NoDC.raw` |       3 |
| Inter-area transfer | `Texas2000\Texas2000_June2016.RAW`                        |      27 |

### [ ] RAW-005: Increase two-terminal and VSC DC model fidelity

- Current behavior:
  - The compatibility link now applies the scheduled two-terminal transfer with
    opposite terminal signs, and only VSC converter `MODE=1` creates an AC
    voltage-control equation.
  - It is not a physical DC-network model. Two-terminal `RDC` losses are not
    calculated, VSC active transfer starts at zero, and several converter loss
    and reactive-limit fields are retained only as descriptive metadata.
- Work needed:
  - Decide whether RAW import should construct GridDyn's physical DC/converter
    objects or extend the compatibility link with mode-specific active-power,
    loss, and reactive-limit behavior.
- Exercised by:
  - `interpss\psse\v30\42bus_3winding_from_PSSE_V30.raw`

## EPC / PSLF power-flow gaps

Relevant source: `src/fileInput/gridReadEPC.cpp`.

### [ ] EPC-001: Consume and model `dc converter` records

- Current behavior:
  - `dc bus` and `dc line` sections are read.
  - `dc converter` has an empty handler, so converter records remain effectively
    unconsumed/ignored.
  - Smoke run on `pslf\2tdc.epc` printed `converter` and then
    `unrecognized token 3` / `unrecognized token 4`.
- Work needed:
  - Parse converter records and attach them to the DC network, or consume the
    records with a clear unsupported-model diagnostic.
- Exercised by:

| File                    | DC buses | DC lines | DC converters |
| ----------------------- | -------: | -------: | ------------: |
| `pslf\2tdc.epc`         |        2 |        1 |             2 |
| `pslf\mtdc-PDCI-NS.epc` |       10 |        8 |             6 |
| `pslf\mtdc-PDCI-SN.epc` |       10 |        8 |             6 |

### [ ] EPC-002: Decide metadata preservation for ignored EPC sections

- Current behavior:
  - Several top-level metadata sections are intentionally ignored, including
    `area`, `zone`, `interface`, `substation`, `ba`, `owner`, `transaction`,
    `qtable`, and `z`.
  - `injgroup` / `injgrpelem` are ignored, with diagnostics only if nonempty.
- Work needed:
  - Decide whether to preserve area/zone/owner/interface/substation metadata.
  - Improve docs/diagnostics if these remain intentionally ignored.
- Exercised by:
  - `portugal.epc`
  - `PSERC-1.epc`
  - `UCTE_2002_Summer.EPC`
  - `illinois200\Illinois200.EPC`
  - `SouthCarolina500\SouthCarolina500.EPC`
  - `Texas2000\ACTIV_SG_2000.EPC`
  - `Texas2000\Texas2000_June2016.EPC`
  - `pslf\sample.epc`
  - `pslf\upfc.epc`

## MATPOWER / PSAT `.m` gaps

Relevant sources:

- `src/fileInput/readMatlabData.cpp`
- `src/fileInput/gridReadMatPower.cpp`
- `src/fileInput/gridReadPSAT.cpp`

### [ ] MATLAB-001: Decide treatment for MATPOWER optional fields

- Current behavior:
  - MATPOWER reader loads `baseMVA`, `bus`, `gen`, `branch`, and `gencost`.
  - `gencost` is a no-op unless the optimization library is enabled.
  - `bus_name` and `areas` are ignored.
- Work needed:
  - Preserve bus names as metadata/object names if desired.
  - Preserve or explicitly ignore `areas`.
  - Clarify `gencost` behavior when optimization support is not enabled.
- Exercised by:
  - `GBnetwork.m`: `mpc.areas`, `mpc.gencost`
  - `uiuc_150bus[1].m`: `mpc.gencost`, `mpc.bus_name`
  - `UIUC150\uiuc_150bus.m`: `mpc.gencost`, `mpc.bus_name`
  - `illinois200\Illinois200.m`: `mpc.gencost`, `mpc.bus_name`
  - `SouthCarolina500\SouthCarolina500.m`: `mpc.gencost`, `mpc.bus_name`
  - `Texas2000\case_ACTIV_SG_2000.m`: `mpc.gencost`, `mpc.bus_name`
  - `Texas2000\Texas2000_June2016.m`: `mpc.gencost`, `mpc.bus_name`
  - `case39_RCost.m`: `mpc.gencost`
- Not observed in this corpus:
  - `mpc.dcline`
  - `mpc.storage`
  - `mpc.busdc`
  - `mpc.branchdc`
  - `mpc.convdc`

### [ ] MATLAB-002: Fix or diagnose PSAT `d_iceland.m` import

- Current behavior:
  - `d_iceland.m` smoke run aborts with `failure to add object`.
  - It also contains dynamic arrays that are not in the PSAT loader identifier
    table.
- Work needed:
  - First isolate the add-object failure.
  - Then add or intentionally reject `Tg.con` and `Pss.con`.
- Exercised by:
  - `d_iceland.m`
- Dynamic arrays present:
  - `Bus.con`
  - `SW.con`
  - `PV.con`
  - `PQ.con`
  - `Shunt.con`
  - `Line.con`
  - `Syn.con`
  - `Exc.con`
  - `Tg.con`
  - `Pss.con`

### [ ] MATLAB-003: Implement PSAT turbine-governor and stabilizer arrays

- Current behavior:
  - `Tg.con` is present in source as a commented-out loader entry.
  - `Pss.con` is not present in the PSAT array identifier list.
- Work needed:
  - Add PSAT `Tg.con` loader support.
  - Add PSAT `Pss.con` loader support or diagnostics.
- Exercised by:
  - `d_iceland.m`

## Smoke-run notes

These were quick import checks, not full validation tests.

| Input                                                                            | Result                                                                   |
| -------------------------------------------------------------------------------- | ------------------------------------------------------------------------ |
| `39busCase\IEEE 39 bus.RAW` + `39busCase\IEEE 39 bus.dyr`                        | exit 0, no unknown model output                                          |
| `pit_test_cases\IEEE39.raw` + `pit_test_cases\IEEE39.dyr`                        | exit 0, no unknown model output                                          |
| `illinois200\Illinois200.RAW` + `illinois200\Illinois200_dynamics.dyr`           | exit 0, no unknown model output                                          |
| `sim3-griddyn-config\powerflowWECC.raw` + `sim3-griddyn-config\dynamicsWECC.dyr` | exit 0, no unknown model output                                          |
| `brazil7Gen\PSSE\Brazilian_7_bus_Equiv_Model.RAW` + `.dyr`                       | exit 0, skips 5 `GENSAE` records                                         |
| `3mach-inf_bus\PSSE\ThreeMIB_Benchmark_System.RAW` + `.dyr`                      | Historical run skipped `GENSAE` / `GENROE`; rerun to validate the new GENROE loader          |
| `TwoAreaSystem\*\Benchmark_4ger_33_2015.RAW` + `.dyr`                            | Historical run skipped `GENROE` / `ESST1A`; rerun to validate the new GENROE loader          |
| `Austrailian14bus\LF_Case01-06_R4_S\AU14GenModel.RAW` + `.dyr`                   | exit -5, `EXAC1 TB must be positive and finite`                          |
| `interpss\psse\v30\Bus200\200busV29-peak.raw` + `200bus-gen-0805.dyr`            | exit -5, `ESST3A requires an existing generator matching its bus and ID` |
| `interpss\psse\v30\42bus_3winding_from_PSSE_V30.raw`                             | exit 0, 49 buses and 63 links; default power flow converged              |
| `interpss\ucte\IEEE14.uct`                                                       | exit 0, empty model                                                      |
| `interpss\ieee_format\ieee14.ieee`                                               | exit 0, empty model                                                      |
| `interpss\ieee_format\ieee300Bus.txt`                                            | exit 0, 300 buses loaded                                                 |
| `RTS96\branch_data.txt`                                                          | exit -5, `unable to convert string`                                      |
| `TDC_test1\case5_mod.m`                                                          | exit 0, 5 buses loaded                                                   |
| `d_iceland.m`                                                                    | exit -5, `failure to add object`                                         |
