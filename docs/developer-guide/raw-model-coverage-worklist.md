# RAW and model coverage follow-up worklist

This document tracks PSS/E RAW data that is either discarded by the reader,
imported with reduced semantics, or already representable by GridDyn but not
yet connected to the RAW import path. It is intended for future development,
not as a promise that every RAW feature should be implemented. Items marked
implemented below still need broader validation as representative source
models become available.

The distinction between model capability and reader coverage is important:
GridDyn often has a useful model for a feature even when the corresponding RAW
fields are currently ignored.

## AESO case-review findings

**Review status:** 2026-09-16. The reviewed corpus is under
`C:\\data\\Documents\\codeProjects\\griddyn_test_cases\\AESO_cases` and contains
40 RAW files, 20 SAVE files, and 40 SEQ files.

### Resolved for the current RAW use case

- All 20 AESO v32 RAW cases and all 20 AESO v33 RAW cases load and complete a
  GridDyn power flow. No remaining RAW syntax or topology issue blocks those
  cases.
- The 40 archived AESO RAW files contain 44 active nonzero-`IREG` records.
  They use only remote buses `610` and `1054`, all have `RMPCT=100%`, and no
  active generator pairs share an `IREG` bus. They therefore fit the current
  single-controller power-flow subset; the AESO corpus does not exercise the
  grouped-participation rejection or the step-up-generator relocation path.
- The v33 failure was traced to fixed phase-shifter/control interpretation and
  invalid tap-limit normalization in a `CW=1`/zero-`NOMV1` combination. The
  reader now retains fixed negative phase-shifter controls and avoids creating
  invalid tap limits.
- Transformer `CM=1` and `CM=2` magnetizing data are now converted and applied
  as first-winding terminal shunts. This is sufficient for the present
  positive-sequence power-flow and network-dynamics models; it does not model
  saturation, inrush, or frequency-dependent core behavior.
- AESO switched-shunt records contain 7,812 cards across the 40 files: 1,328
  fixed (`MODSW=0`), 6,028 stepped-voltage (`MODSW=1`), and 456
  continuous-voltage (`MODSW=2`). There are no AESO `MODSW=3..6` records.
  Ordinary RAW `Svd` power-flow control now honors the supplied `BINIT` level,
  steps the available voltage-support blocks, and uses an algebraic state for
  continuous voltage control. Remote `SWREM` buses are resolved and validated.

### Merge checkpoint and next steps

The current change set is the mergeable power-flow increment for the AESO
review. It covers the active AESO switched-shunt population, single-generator
remote voltage control, transformer magnetizing admittance, fixed and active
two-winding transformer import, and the observed v32/v33 correction-table
forms. Negative transformer `CONT` direction is preserved through
power-flow initialization, and `NTP=0`, `NTP=1`, and stepped `NTP>1` now have
explicit continuous, fixed, and stepped semantics.

The focused regression tests cover switched-shunt initialization and
continuous-control Jacobians, remote-control rejection/operation, transformer
magnetization, fixed negative phase-shifters, negative transformer control
direction, and transformer tap-position counts. The 40-case AESO corpus review
remains the compatibility baseline: grouped `RMPCT` controls are rejected
explicitly, while the observed single-controller cases are supported in power
flow only.

Next steps should be separate follow-up changes rather than prerequisites for
this merge:

- Compare transformer tap direction, limits, correction interpolation, remote
  `CONT`, and continuous controls against PSS/E reference solutions.
- Decide whether to implement PSS/E `ADJM` adjustment ordering and
  `MODSW=3..6`, or reject those modes explicitly instead of accepting reduced
  semantics.
- Design coordinated grouped `RMPCT` equations for generators and switched
  shunts; dynamic remote control requires a separate controller/state design.
- Add broader three-winding and v35 transformer coverage, including independent
  winding correction tables, `RATE4`-`RATE12`, and `NOD`.
- Keep `.SAVE` reverse engineering and `.SEQ` sequence-network modeling out of
  this power-flow merge; prefer RAW/EPC/DYR export for current workflows.

### Remaining format and model findings

- `.save` files are PSS/E binary saved-case files. Their headers are partially
  inspectable, but the format is versioned/proprietary rather than an openly
  documented interchange format. Direct GridDyn support would require a
  compatible PSS/E reader or a reverse-engineered importer and is not a good
  near-term dependency. Prefer exporting these cases to RAW/EPC/DYR when
  possible.
- `.seq` files are readable text PSS/E sequence-network records. They contain
  useful positive-, negative-, and zero-sequence parameters, but GridDyn has no
  sequence-network reader or sequence-domain network models. They are therefore
  useful for a future short-circuit/unbalanced-study workflow, not for the
  current positive-sequence power-flow or phasor-dynamics workflow.
- A dedicated three-winding `CM=2` fixture and comparison against an independent
  sequence/reference calculation are still desirable. The two-winding CM=2
  conversion is covered; the three-winding reader uses the same conversion
  helper and first-leg placement.
- AESO has 1,608 nonzero-`SWREM` shunt records, all with `RMPCT=100%`. Some
  remote buses have multiple active shunts, so coordinated switched-shunt
  `RMPCT` participation is not yet represented; the current independent-device
  behavior is appropriate only for a single active controller per remote bus.
  `ADJM` is preserved as readable metadata, but its PSS/E adjustment-order
  semantics are not modeled. The AESO `ADJM=1` records are fixed shunts, so
  this does not change their present power-flow result.
- AESO transformer cards are all two-winding `CW=1`/`CM=1`; active controls are
  `COD=1` voltage controls with nonzero `NTP`, and the 80 nonzero correction
  references belong to fixed `COD=-3` phase shifters. The reader applies the
  supplied voltage-control limits and tap positions, preserves negative
  `CONT` direction, and uses the existing correction interpolation.
  Independent validation against PSS/E is still needed for tap direction,
  remote `CONT` buses, limit behavior, and continuous controls.
  `CW=2/3`, active MW/Mvar control, independent winding correction tables,
  v35 `RATE4`-`RATE12`/`NOD`, and transformer ownership/name metadata remain
  reduced or discarded.
- Outside transformers, the highest-value unimplemented RAW areas are grouped
  generator voltage participation (`IREG`/`RMPCT` and newer remote controls),
  induction-machine records, physical multi-terminal DC/FACTS/GNE devices,
  emergency bus-voltage limits, and area interchange targets. These are tracked
  in the numbered items below rather than being silently treated as supported.

## Highest-value follow-ups

### RAW-001: Preserve independent branch terminal shunts

**Priority:** High

**RAW fields:** `GI`, `BI`, `GJ`, and `BJ` in branch records.

**Current state:** Implemented in the RAW reader. Series `R/X`, total line
charging `B`, ratings, and status continue to map as before. `GI`, `BI`, `GJ`,
and `BJ` now contribute to the complete endpoint shunts on `AcLine` as `g1`,
`b1`, `g2`, and `b2`.

`AcLine` represents shunt conductance and susceptance directly at its four
terminals (`g1`, `b1`, `g2`, and `b2`). The aggregate `g` and `b` properties
remain as convenience properties: setting either one assigns equal halves to
both terminals, and reading either one returns the sum of the two terminals.
This preserves asymmetric PSS/E data without maintaining a second aggregate
copy of the same shunt values.

**Existing model population:**

- RAW, CDF, EPC, MATPOWER, PSAT, PSP, and ANDES readers populate the aggregate
  `AcLine` `b` value where their input format provides a single line-charging
  value.
- `LongLine` propagates aggregate `g/b` values to its generated segments.
- `ThreeWindingTransformer` can place magnetizing `g/b` on its first winding
  leg.
- The repository's ANDES line cases contain nonzero aggregate line `b` values,
  but not independent terminal values.
- A scan of the 12 external RAW cases under
  `C:\Users\phlpt\Documents\test_cases` found no branch with nonzero
  `GI/BI/GJ/BJ`. There is therefore no current corpus case that exercises this
  gap.

The RAW reader maps the complete terminal shunts as `BI + B/2`, `GI`,
`BJ + B/2`, and `GJ`. This preserves existing callers that use aggregate
`g/b` while retaining RAW endpoint asymmetry. The endpoint values are included
in the full, approximate, derivative, tap, fault, and open-switch calculations,
and are copied and exposed through the normal `AcLine` parameter interface.

**Implementation options considered:**

1. Store the complete from/to terminal shunts directly as `g1/b1` and
   `g2/b2`. Make the existing aggregate `g/b` properties assign or return
   symmetric terminal values, then update the full and approximate power-flow
   equations, derivatives, cloning, parameter access, and serialization. This
   is the most faithful approach without redundant state.
2. Represent each terminal shunt as a fixed `ZipLoad` attached to the
   corresponding bus. This reuses existing components but requires careful
   handling of branch status, parallel branches, naming, base conversion, and
   whether the shunt should remain active when the branch is opened.
3. Fold the four values into one symmetric `AcLine` shunt. This is simple but
   loses the endpoint asymmetry and should not be used for a fidelity-oriented
   RAW import.

**Selected direction:** Option 1. The RAW reader maps `GI -> g1`, `BI + B/2 ->
b1`, `GJ -> g2`, and `BJ + B/2 -> b2` without silently dropping nonzero
values.

**Remaining validation:**

- The small asymmetric RAW fixture is now present in
  `test/test_files/input_tests/raw_branch_terminal_shunts.raw` and is covered
  by `InputTests.PssERawBranchTerminalShunts`, including the symmetric
  aggregate `g`/`b` property behavior.
- Check from- and to-end real/reactive injections against an independently
  calculated pi-equivalent.
- Exercise a transformer-like tap, an opened branch, parallel branches, and
  the solver's full and approximate power-flow paths.

Relevant code: `src/griddyn/links/AcLine.h`,
`src/griddyn/links/AcLine.cpp`, and `rawReadBranch` in
`src/fileInput/gridDynReadRAW.cpp`.

### RAW-002: Import the remaining branch ratings

**Priority:** Medium

`AcLine` can perform rating and flow checks, but the RAW reader currently
imports only the first three rating fields. RAW v35 can provide additional
ratings, and the branch name, length, and owner participation data also remain
partially unused. Decide whether GridDyn needs named rating families or only a
larger numeric rating set before extending the reader.

### RAW-003: Complete generator voltage-control and participation mapping

**Priority:** Medium

`Generator` already has P/Q limits, a voltage target, a remote bus, machine
base, impedance, and participation-related fields. The RAW reader now applies
`VS`, `IREG`, and `RMPCT` for one active generator regulating one remote bus in
power flow. The generator is attached to the remote `AcBus`, and the existing
indirect-control path uses an exact remote-voltage equation when it is the
only voltage controller on that bus. v35's inserted `NREG` field is accounted
for when locating the subsequent generator fields.

Several-generator `IREG` groups are deliberately rejected with an error for
now. GridDyn does not yet have the coordinated PSS/E reactive-participation
equations needed to apply the `RMPCT` shares consistently, and this RAW
support is power-flow-only; dynamic remote-voltage control remains future
work. Negative/invalid remote identifiers, invalid `VS`, invalid `RMPCT`, and
missing remote buses are also rejected rather than silently approximated.

Future work should implement grouped participation and compare limit sharing
against PSS/E, then design a separate dynamic controller/state treatment.

### RAW-004: Complete transformer operating data

**Priority:** Medium

`AdjustableTransformer` and `ThreeWindingTransformer` already support many of
the needed tap, phase-shift, control, rating, and magnetizing behaviors. The
RAW reader still needs a deliberate treatment for additional winding controls,
extra ratings, magnetizing data, correction-table details, and unsupported
control modes. Do not add fields solely to retain metadata that does not affect
the selected simulation mode.

The current three-winding RAW path intentionally targets a fixed steady-state
equivalent: one generated star bus and three `AcLine` legs. It imports the
terminal buses and circuit ID, `CW`/`CZ`, both PSS/E magnetizing encodings
(`CM=1` direct `MAG1`/`MAG2` and `CM=2` no-load-loss/excitation-current
conversion), transformer status, the pairwise `R/X/SBASE` values,
`VMSTAR`/`ANSTAR`, each winding's fixed `WINDV`/`ANG` values, and the first
three winding ratings. The resulting magnetizing admittance is placed on the
first external/I-side endpoint of the first star leg. A shared missing winding
`CONT` bus is retained as an alias for the generated star bus so later RAW
records, such as switched shunts, can resolve it. This alias does not enable
transformer regulation.

The following three-winding fields are currently ignored or reduced in
meaning:

- Nonzero `COD`/`CONT` control modes remain fixed at their supplied starting
  taps; `RMA`/`RMI`, `VMA`/`VMI`, and `NTP` are not applied.
- RAW v35 rates beyond the first three (`RATE4` through `RATE12`) are not
  represented by the current `AcLine` rating interface.
- `CR`/`CX` and the v35 `NOD` field are not interpreted.
- Only the primary winding's impedance-correction table reference is applied.
- `CM` values other than 1 or 2 are ignored with a warning when their
  `MAG1`/`MAG2` values are nonzero. `CM=2` is converted using the open ANDES
  reader's convention: `G = MAG1/(SBASE*1e6)` and
  `B = sqrt(MAG2^2-G^2)`. PowerModels recognizes the CM field but its current
  parser passes CM=2's raw values through without this conversion.
- `NMETR`, the transformer `NAME`, and owner participation fields
  (`O1`/`F1` through `O4`/`F4`) are discarded because there is no corresponding
  GridDyn electrical or ownership model.

These limitations are acceptable for the present fixed-equivalent import and
should remain separate from the auxiliary-bus aliasing change. Future full
integration should decide whether to construct `ThreeWindingTransformer`
directly or extend the generated-leg representation with coordinated control,
then add fixtures for active `COD` modes, winding limits/steps, all v35 rating
fields, a three-winding `CM=2` case, and uncommon status/voltage-base
combinations.

## Areas and metadata

### RAW-010: Complete area semantics

**Priority:** Medium

`GridArea` is a real container and can compute generation, load, and tie-flow
statistics and host wide-area control. The current RAW import creates areas and
places buses and same-area two-terminal links into them. The RAW, EPC, and
MATPOWER readers now apply this placement explicitly while constructing their
links: when both terminal buses belong to the same area, the link is stored in
that area; links whose endpoints belong to different areas remain in the
enclosing network as tie lines.

This is intentionally reader-level policy rather than behavior hidden in
`GridArea::add(Link*)`. The `add` operation remains a simple container
insertion, allowing each input reader or other construction path to choose its
own arrangement. The readers perform the area-parent selection after the
endpoint connections are known, including for reader-generated line and
transformer link objects.

The reader still does not map area `ISW`, `PDES`, or `PTOL` into area-control or
interchange-target objects. There is also no direct RAW inter-area-transfer
record model.

Possible follow-up work is to add explicit area identifiers and interchange
targets, then connect them to the existing AGC/tie-flow infrastructure.

### RAW-011: Preserve zones and owners

**Priority:** Low to medium

`GridPrimary` has a `zone` integer, but it is documented as a loss-zone
indicator and is not used internally. GridDyn has no direct owner model and no
zone-definition/name table. Decide whether these are needed as operational
metadata, reporting metadata, or control groupings before adding reader
support.

### RAW-012: Preserve emergency bus-voltage limits

**Priority:** Low to medium

`AcBus` supports normal voltage minimum/maximum values and a voltage target.
RAW emergency limits `EVHI` and `EVLO` would require separate fields and clear
solver/reporting semantics rather than overwriting the normal limits.

## Loads and shunt devices

### RAW-020: Import additional load metadata and behavior

**Priority:** Medium

GridDyn has `ZipLoad` and other load models, so the basic constant P/I/Y load
behavior is available. RAW fields for scaling, interruption, distributed
generation, ownership, and load type do not currently have a direct mapping.
Implement only after deciding whether these values affect power flow,
contingency studies, or dynamic initialization in GridDyn.

### RAW-021: Complete switched-shunt modes

**Priority:** Medium

`Svd` now supports the AESO voltage-control modes (`MODSW=1/2`) in power flow,
including remote measurement buses, `BINIT` initialization, block stepping,
and continuous algebraic control. `ADJM` remains metadata because its exact
PSS/E adjustment-order algorithm is not needed by the AESO active population.
Grouped remote `RMPCT` participation and the unused `MODSW=3..6` families need
source-backed semantics and focused comparison tests before being enabled.

### RAW-022: Import induction-machine records

**Priority:** Medium

GridDyn has `MotorLoad`, `MotorLoad3`, and `MotorLoad5`, with meaningful
induction-motor parameters and dynamic states. The RAW induction-machine
section is not connected to those models. A future importer must decide which
motor class matches each RAW record and how the motor's share of the bus load
is split from the static load record.

## DC, FACTS, and other device sections

### RAW-030: Translate RAW DC records into physical DC models

**Priority:** Medium

GridDyn contains `DcBus`, `DcLink`, `AcDcConverter`, `VSCShunt`, and `Hvdc`, so
the model layer has meaningful physical building blocks. The RAW reader
currently uses `RawDcLine`, a compatibility model for scheduled terminal
transfers rather than a physical HVDC network. Two-terminal and VSC imports
should remain separate from future multi-terminal topology assembly.

### RAW-031: Add multi-terminal DC topology import

**Priority:** Low to medium

The physical DC model classes are available, but there is no importer that
constructs a multi-terminal network from the RAW records. Do not flatten a
multi-terminal system into independent two-terminal transfers.

### RAW-032: Decide on FACTS representation

**Priority:** Low to medium

There is no dedicated generic FACTS/TCSC/STATCOM/UPFC model. Some simpler
devices may be approximated with `AdjustableTransformer` or VSC classes, but a
faithful RAW FACTS import needs an explicit device-model decision first.

### RAW-033: Decide on GNE representation

**Priority:** Low

No direct GridDyn model was found for PSS/E GNE records. The reader should
continue to identify or diagnose unsupported GNE records rather than silently
claiming they were modeled.

### RAW-034: Decide on substation representation

**Priority:** Low

GridArea could provide a generic container, but there is no dedicated
substation object or substation-level electrical semantics. Add one only if
substation identity or topology is needed by a concrete workflow.

### RAW-035: Handle system-wide RAW settings

**Priority:** Low

RAW system-wide, Gauss, Newton, and related settings belong more naturally in
reader or solver configuration than in network components. Decide which values
should configure GridDyn's solver and which should be reported as unsupported.

## General reader policy

The RAW reader currently consumes several unsupported sections without creating
GridDyn objects. For each section retained on this worklist, future changes
should do one of the following:

- map the data into an existing model and add a regression;
- add a dedicated model with a clear power-flow/dynamic meaning; or
- emit a section-level diagnostic that identifies the unsupported data.

The reader should not silently discard nonzero electrical data when GridDyn
already has a plausible representation for it.
