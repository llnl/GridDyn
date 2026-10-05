# ACTIVSg25k dynamic-model audit

This audit uses the files in `C:\Users\phlpt\Documents\test_cases\ACTIVSg25k`
as supplied on 2026-09-25. GridDyn's DYR reader is the primary import path for
this case. Counts are records, not unique generators. A reader dispatch proves
that a model name is recognized; it does not prove initialization or matching
disturbance trajectories.

Model/import status below was reviewed against the GridDyn working tree on
2026-10-04. The source-case inventory and parameter counts remain the
2026-09-25 snapshot described above.

## Model inventory

`ACTIVSg25k.dyr` has 18,108 records in 22 model families. The 16 synchronous
families below all have GridDyn DYR reader branches. Their case configurations
still require full-case initialization and trajectory checks.

| Model  | Records | Model  | Records |
| ------ | ------: | ------ | ------: |
| GENROU |   2,857 | GENSAL |   1,244 |
| GGOV1  |   1,742 | HYGOV  |   1,244 |
| IEEEG1 |   1,115 | IEEEST |   4,101 |
| ESST4B |   1,396 | IEEET1 |     942 |
| SCRX   |     446 | EXPIC1 |     287 |
| EXAC2  |     239 | ESDC2A |     202 |
| ESAC6A |     194 | ESAC1A |     142 |
| EXAC1  |     130 | ESDC1A |     123 |

All 1,742 `GGOV1` records use `TENG=0`, all 202 `ESDC2A` records use
`Switch=0`, all 4,101 `IEEEST` records use `MODE=1` and `BUSR=0`, and all
1,115 `IEEEG1` records have no secondary generator. These values avoid known
unsupported options in the corresponding GridDyn models. Other numeric
parameters and initial conditions are not yet certified by this audit.

| Renewable model | DYR records | Current GridDyn status                                                                                                                                                                                                                            |
| --------------- | ----------: | ------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------- |
| REGCA1          |         614 | Reader and converter exist. `Accel=0.8` is accepted as a numerical parameter. Zero reactive recovery limits are treated as disabled. Trajectories need validation.                                                                                |
| REECA1          |         614 | Reader, constant-Q branch, zeroed VDL tables, `Tpord=0`, and the `Thld2=0.5` active-current hold are implemented. The case profile initializes in a reader test; disturbance trajectories still need validation.                                  |
| WT3G1           |         119 | Dedicated Type-3 electrical interface, PSS/E DYR mapping, PLL/equivalent-reactance path, steady-state initialization, and host-Jacobian tests are implemented. Full-plant validation remains open because WT3T1/WT3P1 adapters are provisional.   |
| WT3E1           |         119 | Dedicated Type-3 electrical controller, PSS/E DYR mapping, initialization-command handoff, steady-state initialization, and host-Jacobian tests are implemented. Full-plant validation remains open because WT3T1/WT3P1 adapters are provisional. |
| WT3T1           |         119 | DYR adapter splits the record into `WTARA1` plus `WTDS`/`WTDTA1`; the mapping is not verified against authoritative `WT3T1` equations or trajectories.                                                                                            |
| WT3P1           |         119 | DYR adapter translates the record to `WTPTA1`; the mapping is not verified against authoritative `WT3P1` equations or trajectories.                                                                                                               |

The 614 `REGCA1`/`REECA1` pairs represent one renewable family and the 119
four-record `WT3*` bundles represent a separate Type-3 wind family. The
existing `WTDTA1`/`WTARA1`/`WTPTA1`/`WTTQA1` classes describe a different
WECC wind assembly. They must not be substituted for `WT3*` based on name or
role alone.

`WT3G1` and `WT3E1` use dedicated models with the renewable host and
typed-signal infrastructure. The generator model retains its separate Type-3
electrical interface, effective reactance, and PLL behavior; the controller
retains the reactive-voltage and active-power paths and receives
initialization commands through explicit initialization-only signals. The
DYR reader now accepts `WT3T1` and `WT3P1` by translating them to existing
`WTARA1`/`WTDS`/`WTDTA1` and `WTPTA1` components. Those adapters permit import,
but do not establish equation or parameter equivalence with the legacy PSS/E
models. PowerWorld documents the PSS/E
`WT3E1`/`WT3T1`/`WT3P1` relationship to older `WT3E`/`WT3T`/`WT3P` models,
which may help source the remaining equations. That relationship does not
justify mapping them to the newer WECC `WTDTA1` family.

## Parameter paths actually used

All 614 `REGCA1` records share the same 15 parameter values, including
`Lvplsw=1`, `Tg=0.02`, `Rrpwr=10`, `Iqrmax=Iqrmin=0`, and `Accel=0.8`.
`Accel` is a numerical acceleration factor rather than a continuous dynamic
state in the ANDES implementation; GridDyn stores and validates it. PowerWorld
documents each nonpositive/nonnegative reactive recovery limit as disabling
that limit, matching the zero values in this case. REGCA1 trajectories still
need a PowerWorld/PSSE comparison, particularly following faults. PowerWorld's
native `REGC_A` treats part of its high-voltage response as an algebraic
network-boundary calculation, whereas the current GridDyn/ANDES-style
`REGCA1` uses converter current equations. The DYR and DYD parameter sets
therefore need separate validation before claiming cross-format equivalence.

All 614 `REECA1` records use local measurements (`BUSR=0`),
`PFFLAG=VFLAG=QFLAG=PFLAG=PQFLAG=0`, `Thld=Iqfrz=0`, `Thld2=0.5`,
`Tp=0`, `Tpord=0`, `Imax=1.3`, and all-zero Vq/Iq/Vp/Ip tables. `Trv` and
`Tiq` each take `0.02` or `0.033333`. PowerWorld's current-limit pseudocode
bypasses an empty VDL table. GridDyn treats the all-zero points in this case
as empty, but a reference trajectory is still needed to confirm that encoding.
The
`Thld2` uses two DAE roots: one for entry and exit at the low/high voltage
thresholds, and one for release at recovery time plus 0.5 seconds. The active
current limit follows reactive current during the dip, then holds its final
dip value for 0.5 seconds after recovery. The `Tpord=0` power order is held
during the dip. A root check also handles instantaneous voltage jumps at fault
application and clearing. Component tests exercise both voltage thresholds,
repeated dips, and release timing. A checked-in two-bus ANDES fault reference
compares the shared `REGCA1`/`REECA1`, `REECB1`, `REPCA1`, and newer WECC wind
branches at eight times. The REECB1 comparison exposed and corrected its
post-fault voltage-error injection: it decays with filtered voltage after
clearing, unlike the REECA1 injection branch. See
[`test/reference/renewable_fault/README.md`](../../test/reference/renewable_fault/README.md)
for the fixture and its limits. Full-case and PowerWorld or PSS/E trajectory
comparisons remain to be done.

The `WT3G1` first positional parameter has 47 distinct values and its final
parameter takes `1.65`, `2.2`, or `2.75`. `WT3E1` has 37 positional
parameters, `WT3T1` has eight, and `WT3P1` has nine. They have multiple
parameter variants, so one representative record is not enough for tests.

### WT3P1/WT3T1 adapter and model-fidelity status (reviewed 2026-10-04)

The adapters currently use the parameter interpretation described below.
Import support must not be reported as a dedicated implementation or as
validated compatibility.

The local test corpus at
`C:\data\Documents\codeProjects\tests` supplies canonical PSS/E record
shapes, but not the model specifications. Representative records in the
`EI_NPCC_WIND20_WT3G1.dyr` and `WECC_WIND10.dyr` fixtures are:

| Record  | Status values observed | Positional values after status             | Evidence currently established                                                                                                    |
| ------- | ---------------------- | ------------------------------------------ | --------------------------------------------------------------------------------------------------------------------------------- |
| `WT3T1` | `1`, `Z`               | `1.25, 4.95, 0, 0.007, 21.98, 0, 1.8, 1.5` | Eight-field schema and repeatable case values; parameter names, equations, signs, and initialization are still unverified.        |
| `WT3P1` | `1`, `Z`               | `0.3, 150, 25, 3, 30, 0, 27, 10, 1`        | Nine-field schema and repeatable case values; parameter names, equations, limits, flags, and initialization are still unverified. |

An older OpenIPSL history artifact,
`OpenIPSL/Electrical/Wind/PSSE/WT1G/WT12T1.mo` (commit `56e4cd9`), has a
two-mass turbine/shaft implementation whose icon labels it “WT3T1.” It is a
useful equation-level lead for the mechanical states, but it is named
`WT12T1`, exposes a different parameter interface, and does not establish the
PSS/E `WT3T1` positional mapping. Neither the current OpenIPSL model tree nor
GridKit contains an exact `WT3P1` implementation. `WTDTA1`, `WTPTA1`, GE, and
PSAT models remain non-authoritative alternatives and must not be mapped into
these records by position or by role.

Dedicated model fidelity is still blocked on an authoritative PSS/E WT3P1/WT3T1
specification (or a vendor/reference implementation) that names every field
and defines the equations, signal connections, initialization, limiter/flag
behavior, and sign/base conventions. Keep the adapters explicitly provisional
until that comparison is complete; do not claim a validated full Type-3 plant.

## Other formats in this directory

`ACTIVSg25k_dynamics.dyd` has the same 16 synchronous model counts. Its
renewable records are `regc_a` and `reec_a` (614 each), plus `wt3e`, `wt3t`,
and `wt3p` (119 each). `WT3G1` is present in DYR and AUX but absent from DYD.
DYD also has one `wlwscc` load-characteristic record. GridDyn now applies this
all-load WSCC characteristic to non-fixed-shunt loads, reducing exact ZIP and
frequency-dependent cases to those existing models. The five renewable DYD
names remain unconverted, so loading DYD instead of DYR does not close the
coverage gap. The `ACTIVSg25k_dynamics.aux` file contains all 22 DYR model
families with the same per-family record counts, plus transient options and
network data. Its field order
is native PowerWorld order and cannot be passed positionally to the DYR
reader. The 54-field `Exciter_REEC_A` table includes mode flags, hold times,
VDL points, and machine capacity; the `MachineModel_REGC_A` table includes
device status and PowerWorld-specific converter fields. No model records were
found in `ACTIVSg25k_dynamics.aux` for REPCA1 or the newer WTDTA1 wind family.

## Implementation and validation sequence

1. Compare the 25k-specific zero-VDL and zero-reactive-limit paths with
   PowerWorld or PSS/E. Check residual/Jacobian behavior through both DAE
   event transitions and exact release timing. The small ANDES comparison now
   covers the plant controller and newer WECC wind components.
2. Validate the 614 solar pairs at initialization, then compare a small
   disturbance trajectory against a reference implementation. Check
   `Accel` and zero reactive recovery limit conventions explicitly.
3. Complete the connected `WT3G1`/`WT3E1`/`WT3T1`/`WT3P1` assembly. The
   electrical pair is implemented with RenewableGenerator roles, typed
   signals, DYR mappings, initialization, and host-Jacobian tests. Obtain and
   review the exact `WT3T1`/`WT3P1` parameter maps and equations against the
   eight-/nine-field DYR fixtures above, then require and test the full
   four-record bundle before running an ACTIVSg Type-3 plant.
4. Add explicit DYD adapters for `regc_a`/`reec_a` and `wt3*`, with
   cross-format parameter equivalence checks against DYR. Validate the
   implemented `wlwscc` behavior against the source case and a trusted
   simulator before relying on it for a full-fidelity study.
5. Run the full RAW+DYR case through power flow, dynamic initialization,
   residual/Jacobian checks, and at least one bus-voltage disturbance. Compare
   P, Q, voltage, and representative converter/turbine states to PowerWorld
   or another trusted reference. Repeat with EPC+DYD after adapters exist.

The DYR reader dispatches all four `WT3*` names, but `WT3T1`/`WT3P1` use
provisional translations and the full Type-3 case has not been validated.
Treat ACTIVSg25k as import-recognized but not yet certified for full dynamics.
The separate DYD path still does not translate its `wt3*` records. For
supported DYR records, failures identify file line, bus, and machine.

## Model references

- [EPRI generic renewable model guide](https://restservice.epri.com/publicdownload/000000003002014083/0/Product):
  REGC_A numerical `accel`, REEC_A `Thld2` post-fault hold, and VDL guidance.
- [PowerWorld REEC_A equations and parameters](https://www.powerworld.com/WebHelp/Content/TransientModels_HTML/Exciter%20REEC_A.htm):
  empty VDL tables and the held `Ipmax` current limit.
- [PowerWorld REGC_A parameters](https://www.powerworld.com/WebHelp/Content/TransientModels_HTML/Machine%20Model%20REGC_A.htm):
  zero reactive recovery limits and native network-boundary behavior.
- [PowerWorld WSCC load characteristic](https://www.powerworld.com/WebHelp/Content/TransientModels_HTML/Load%20Characteristic%20WSCC.htm):
  interpretation of the separate `wlwscc` record.
- [PowerWorld Type-3 wind overview](https://www.powerworld.com/WebHelp/Content/MainDocumentation_HTML/Transient_Stability_Overview_WindModeling.htm),
  [WT3G1 parameters](https://www.powerworld.com/WebHelp/Content/TransientModels_HTML/Machine%20Model%20WT3G1.htm),
  [WT3E1 parameters](https://www.powerworld.com/WebHelp/Content/TransientModels_HTML/Exciter%20WT3E%20and%20WT3E1.htm),
  and [WT3P1 parameters](https://www.powerworld.com/WebHelp/Content/TransientModels_HTML/Stabilizer%20WT3P%20and%20WT3P1.htm):
  source material for a separate Type-3 assembly.
- Local ANDES source: `C:\Users\phlpt\Documents\andes\andes\models\renewable\regca1.py`
  and `reeca1.py` for cross-checking the renewable signal paths.
