# Power-flow validation follow-ups

## Current status (2026-09-05)

This log collects the unresolved or bounded outcomes from large-case
power-flow validation. A timeout or an incomplete solver report is not a
convergence result.

### Texas 7k synthetic grid

| Input | Status | Follow-up |
| --- | --- | --- |
| `Modified_Versions\\Texas7k_20220923.RAW` (PSS/E v35) | Solves in 0.059 seconds after v35 table and switched-shunt parsing fixes. It imports 6,717 buses, 9,140 links, 731 generators, and 5,729 loads. | Phase-shifter comparison is resolved; retain as a v35 regression candidate. |
| `Texas7k_20210804.EPC` | Import completes, then power flow continues consuming CPU without a solver result. | Diagnose the post-import power-flow hang and compare EPC controls/topology with the MATPOWER model. |
| `Texas7k_20210804.m` | Solves: 6,717 buses, 9,140 links, 731 generators, and 4,648 loads. | Baseline for the comparisons above. |

The v35 RAW support now handles system-wide data, `@!` cards, generator
`NREG`, branch field offsets, and transformer winding offsets. Remaining
differences should be assessed against actual v35 input layouts rather than
treated as known format gaps.

The first pre-Newton nonfinite state was the link-flow contribution at buses
120437 and 120439. Their shared transformer uses `TAB1=14`; the RAW reader
was treating impedance-correction records as pairs and dropping continuation
cards, rather than reading `T, Re(F), Im(F)` triples. Its terminating `0,0,0`
padding triple was also retained as a zero-factor point, producing a 0.91183
correction factor instead of 1.06077. Ignoring that padding yields the
MATPOWER-equivalent reactance (`0.024737` pu). The fixed 2-degree
phase-shifter result at bus 120439 now agrees with MATPOWER within 0.0002
degrees and has a 0.00087 Mvar reactive residual.

The Texas v35 switched-shunt cards also include an export-specific quoted
identifier and one additional control field; guarded layout detection restores
the supplied initial shunt (for example, bus 120437 is now `-85.9223` Mvar,
matching the MATPOWER initialization).

### Florida 42 GW case

| Input | Status | Follow-up |
| --- | --- | --- |
| Florida RAW | Solves with recovery disabled in 33.19 seconds: 5,658 buses, 9,078 links, 474 generators, and 4,272 loads. | Baseline. |
| Florida MATPOWER | Imports and begins power flow, but no solver result or post-solve topology summary was emitted after 97 seconds of CPU time. | Compare the imported MATPOWER network and controls with the successful RAW case, then add bounded diagnostics. |

### SyntheticUSA case

The bounded command-line check of `case_SyntheticUSA.m` was stopped after
157 seconds of continuous CPU use before emitting the post-import topology
summary. This is an unclassified large-case MATPOWER
import/initialization-performance issue, not evidence of a converged
solution. See the broader static-case assessment in
[`syntheticusa-compatibility.md`](syntheticusa-compatibility.md).

## Cross-case safeguards and regressions

- The command-line runner now rejects a completed simulation with nonfinite
  bus voltage magnitude or angle, so a failed power flow cannot report exit
  code zero merely because the solver returned.
- A zero/negative voltage state in voltage-only convergence is normalized
  before the Newton update, avoiding the Debug assertion previously observed
  in the 25k-bus case.
- Empty EPC DC-converter sections are explicitly skipped. The WSCC 9-bus EPC
  regression solves after that change.
- ACTIVSg v30, v31, and v32 179-bus RAW cases, along with negative phase
  control and three-winding transformer regressions, pass.

## Recommended next work

1. Locate the first nonfinite state in the Texas v35 RAW solve and compare it
   directly with the successful MATPOWER initialization.
2. Profile the Texas, Florida, and SyntheticUSA MATPOWER paths to distinguish
   parsing, initialization, and solver-time costs.
3. Add bounded large-case test coverage that records import completion,
   iteration progress, and a definitive solver outcome.