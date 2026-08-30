# ACTIVSg10k three-winding import investigation

## Purpose and status

This note records the August 2026 investigation of the ACTIVSg10k PSS/E RAW
and PowerWorld EPC readers.  It is intended to make the work restartable
without repeating the source-file analysis.

Both EPC and RAW now load and solve with the expected 10,000 buses and 12,706
links.  All 300 three-winding transformers map exactly between the formats,
and all 900 star legs agree within source-file rounding.  The RAW convergence
failure was ultimately traced to fixed phase shifters with negative `COD`, not
to the three-winding transformer equivalent.

Source files are outside the repository:

- `C:\Users\phlpt\Downloads\ACTIVSg10k\ACTIVSg10k.RAW`
- `C:\Users\phlpt\Downloads\ACTIVSg10k\ACTIVSg10k.EPC`
- `C:\Users\phlpt\Downloads\ACTIVSg10k\case_ACTIVSg10k.m`

## Case inventory

The RAW and EPC files describe 2,380 transformers:

- 2,080 two-winding transformers
- 300 three-winding transformers

Each three-winding transformer is materialized as one star bus and three
`AcLine` transformer legs.  Together with the 9,700 terminal buses this gives
the expected 10,000 buses.  The complete imported topology contains 12,706
links, matching MATPOWER.

## Verified format interpretation

### RAW three-winding records

PSS/E supplies the three pairwise leakage impedances.  GridDyn converts them
to star legs using:

```text
Z1 = (Z12 + Z13 - Z23) / 2
Z2 = (Z12 + Z23 - Z13) / 2
Z3 = (Z13 + Z23 - Z12) / 2
```

The reader imports each winding's fixed tap, phase angle, ratings, status, and
the supported magnetizing admittance.  The same conversion is implemented for
the EPC reader, which retains EPC's explicit intermediate/star bus.

### EPC field interpretation

For the ACTIVSg10k three-winding records, the important EPC field positions
after tokenization are:

| Meaning | Field(s) |
| --- | --- |
| Exterior buses I/J/K | 0, 3, 17 |
| Explicit star bus | 14 |
| Status/type/control bus | 8, 9, 10 |
| Transformer MVA base | 22 |
| Pair impedance PS/PT/TS | 23-28 |
| Winding nominal kV | 29-31 |
| Winding phase angles | 32-34 |
| Rating A | 35, 78, 81 |
| EPC winding taps | 45-47 |
| Terminal base kV | 2, 5, 19 |
| Primary impedance-correction table | 13 |

EPC taps are relative to winding nominal voltage, while GridDyn's `AcLine`
tap is relative to the connected terminal bus bases.  The required conversion
is:

```text
GridDyn tap = EPC tap * winding nominal kV / terminal base kV
```

This is material for the Glasgow transformer, whose EPC tap is 0.4 because a
345 kV nominal winding is connected to a 138 kV terminal.  Its GridDyn tap is
therefore 1.0.  After this normalization, all 300 EPC three-winding taps agree
with RAW to approximately `1.1e-16`.

### EPC two-winding voltage-base conversion

The EPC two-winding conversion found during the same investigation is:

```text
primary scale   = nominal1 / terminalBase1
secondary scale = nominal2 / terminalBase2
GridDyn tap     = EPC tap * primary scale / secondary scale
GridDyn Z       = EPC Z * secondary scale^2 * system base / transformer base
```

Across all 2,080 transformers this agrees with RAW to about `5e-8` in R,
`5e-6` in X, and `1.75e-5` in tap.  The residual differences are consistent
with RAW numeric rounding.

### Impedance-correction tables

Both readers now pre-scan and interpolate transformer impedance-correction
tables.  The ACTIVSg10k Logan three-winding transformer demonstrates why this
is necessary: winding 1 uses table 3 at -20 degrees, whose 1.18 factor exactly
accounts for the difference between its uncorrected star leg and MATPOWER.

The EPC Desert Center phase shifter similarly uses a correction factor of
approximately 1.061568 at -7.696 degrees.

## Validation results

### EPC

The following command converges without solver warnings:

```powershell
build\bin\Debug\gridDynMain.exe `
  C:\Users\phlpt\Downloads\ACTIVSg10k\ACTIVSg10k.EPC `
  --powerflow-only `
  --powerflow-output output\activs10k_epc_corrections.xml `
  --verbose
```

The result has 10,000 buses and 12,706 links.  Compared with the supplied
MATPOWER bus state:

- maximum voltage error: 0.00231742 pu
- mean voltage error: 0.0000218 pu
- mean signed angle offset: -0.00000773 rad
- maximum angle residual after removing that offset: 0.0003957 rad
  (0.02267 degrees)

The largest remaining error is localized near the Desert Center
phase-shifter/generator region.  For context, GridDyn's solve of the MATPOWER
file differs from the supplied state by as much as approximately 0.0232 pu,
so the EPC voltage agreement is already better than that internal comparison.

### RAW/EPC assembled-network comparison

GridDyn pre-solve exports were generated with `--power_flow_input_data`:

- `output\activs10k_epc_input.xml`
- `output\activs10k_raw_input.xml`

The first 9,700 native bus names are in the same order, and their initial
voltage/angle values agree to source-file precision.  Mapping link endpoint
IDs back to source bus numbers shows that all non-three-winding electrical
connections correspond between RAW and EPC.  Their maximum R/X difference is
`5e-6` pu, again consistent with RAW rounding.

The 900 three-winding legs require an explicit mapping because EPC uses its
source star-bus number while RAW creates a synthetic star-bus ID.  Mapping all
300 source transformer keys `(I,J,K,CKT)` gave the following results:

- all 300 transformer keys map one-to-one;
- all 900 exterior-to-star endpoints correspond;
- maximum imported R/X difference: `2.4e-6` pu;
- maximum tap difference: approximately `1.1e-16`;
- phase angles and statuses match exactly;
- maximum rating-A difference: 0.05 MVA, caused by EPC rounding;
- all RAW rating-B/rating-C values and magnetizing values are zero for this
  case, and all magnetizing codes are `CM=1`.

At the supplied input state, the maximum real-power-flow difference among the
900 legs is approximately `0.00447` pu and the mean difference is
`2.88e-5` pu.  Only five legs exceed `0.001` pu; they are low-impedance legs
where the source-format impedance rounding is amplified.  Native and star-bus
starting states match EPC to less than `6e-7` pu in voltage.

### RAW convergence and root cause

RAW imports 10,000 buses, 12,706 links, 2,485 generator records, and 5,286 load
objects and now converges without solver warnings.  The final validation run
used:

```powershell
build\bin\Debug\gridDynMain.exe `
  C:\Users\phlpt\Downloads\ACTIVSg10k\ACTIVSg10k.RAW `
  --powerflow-only `
  --powerflow-output output\activs10k_raw_final_inline_star.xml `
  --verbose
```

The solved network has 20,000 power-flow states and 135,002 Jacobian nonzeros.
Compared with the source solved state, using source bus IDs plus the 300 mapped
star buses:

- maximum voltage error: `0.000743175` pu;
- mean voltage error: `2.50e-6` pu;
- mean signed angle offset: `2.3946e-5` rad;
- maximum relative angle residual after removing the offset: `5.2222e-5` rad
  (`0.002992` degrees).

Compared directly with GridDyn's converged EPC result, the maximum voltage
difference is `0.00231746` pu and the maximum relative angle difference is
`0.023176` degrees.  The RAW result is closer to the supplied solved state than
the EPC result for this case.

The convergence defect was in the two-winding adjustable-transformer path.
PSS/E uses a negative `COD` to select manual/fixed operation while the absolute
value still selects the control family.  The reader used `abs(COD)` when it
created the link but later compared the signed value when assigning limits and
step count.  A `COD=-3` fixed phase shifter was therefore assigned voltage-tap
limits instead of phase-angle limits, and initialization changed its tap ratio
from 1.0 to 2.0.  This corrupted the COLSTRIP/BELLINGHAM phase-shifter flows
and drove KINSOL into singular recovery.  Both current and legacy RAW
transformer readers now consistently use `abs(COD)` for the family while
retaining `COD < 0` as manual mode.

The case contains 1,452 generator records with nonzero `IREG` values (1,161
active remote-regulating units).  GridDyn's existing per-generator attachment
does not implement PSS/E's coordinated remote reactive participation, so the
current case-compatible policy preserves the supplied bus voltage as a local
target and does not add independent remote voltage constraints.  Full grouped
remote-regulation support remains a separate improvement.

### Regression coverage

`FileReaderTests` now contains two minimized RAW regressions:

- `RawThreeWindingTransformerCreatesStarEquivalent` checks one source record
  becomes three terminal buses, one star bus, and exactly three legs with the
  expected delta-to-star R/X values, taps, phase angles, and common endpoint.
- `RawNegativePhaseControlRetainsFixedTap` checks that a `COD=-3` phase shifter
  retains its 1.0 tap ratio and supplied phase angle both before and after
  power-flow initialization.

The full `FileReaderTests` suite passes 52/52, and the component-level
`LinkTests.ThreeWindingTransformerSolves` test also passes.

## Remaining limitations

1. The synthetic star bus is now created locally with each transformer.  The
   unsuccessful star-bus ordering pre-scan was removed.
2. The duplicate-final-transformer guard remains because the RAW section loop
   can present the final record twice.  The minimized reader regression checks
   that one three-winding record produces one star bus and three legs.
3. Full PSS/E `IREG` behavior requires grouped reactive participation rather
   than one independent voltage equation per generator.
4. EPC currently applies the primary correction-table reference used by this
   case.  Independent winding-2/winding-3 references need a validating fixture.
5. Uncommon `CW=2/3` winding-voltage forms and `CM=2` magnetizing-loss forms
   still require dedicated conversion fixtures.

## Recommended restart sequence

1. Build `gridDynMain` with the normal CMake command.  On the known Windows
   `Path`/`PATH` collision, use:

   ```cmd
   cmd /v:on /c "set PATH=& set Path=& C:\Progra~1\CMake\bin\cmake.exe --build build --config Debug --target gridDynMain --parallel 4"
   ```

2. Retain the complete-case RAW/EPC comparison artifacts and rerun them after
   transformer-reader changes.
3. Create a minimized one-transformer fixture for Glasgow tap normalization
   and another for Logan impedance correction.  Verify branch flows at the
   supplied state, not only stored parameters.
4. Implement coordinated `IREG` participation and compare generator controls,
   switched shunts, and bus control types against PSS/E.
5. Add independent winding correction-table and uncommon `CW`/`CM` fixtures.

## Acceptance criteria

The ACTIVSg10k base-case import is accepted because:

- RAW and EPC both load 10,000 buses and 12,706 links;
- every three-winding leg agrees after source-ID mapping within documented RAW
  rounding tolerance;
- both cases converge without solver error/recovery warnings;
- bus voltages and relative angles match the supplied solution within the
  project tolerance;
- minimized regressions now cover RAW delta-to-star conversion and negative
  fixed phase-shifter `COD` semantics.  Additional fixtures remain desirable
  for every uncommon voltage-base, correction-table, status, magnetizing, and
  zero-limit variant listed above.
