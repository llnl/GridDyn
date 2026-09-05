# SyntheticUSA PSS/E compatibility assessment

## Scope and source files

This assessment covers the static SyntheticUSA package supplied at
`C:\Users\phlpt\Downloads\SyntheticUSA`. It contains the PSS/E v33
`SyntheticUSA.RAW`, a PowerWorld `SyntheticUSA.EPC` reference, and matching
`.pwb`/`.pwd` files. `case_SyntheticUSA.m` is the available MATPOWER reference.
There is no DYR file or other dynamic-model data, so this case is a
power-flow/topology compatibility target only; it must not be represented as a
dynamic simulation reference.

GridDyn's import/validation path is `SyntheticUSA.RAW`. The EPC, PWB, PWD, and
MATPOWER files are independent reference representations for comparing the
imported topology and solved state.

## Static inventory

| Item                        | Source count | GridDyn requirement                                                                                     |
| --------------------------- | -----------: | ------------------------------------------------------------------------------------------------------- |
| Terminal buses              |       79,600 | Standard PSS/E v33 bus import.                                                                          |
| Loads                       |       44,430 | Standard ZIP/load import and bus attachment.                                                            |
| Fixed shunts                |          916 | Fixed-shunt import.                                                                                     |
| Generators                  |       13,419 | Standard generator, reactive-limit, and voltage-control import.                                         |
| AC branches                 |       83,425 | Standard branch import.                                                                                 |
| Two-winding transformers    |       13,496 | Transformer impedance, rating, tap, phase shift, and control import.                                    |
| Three-winding transformers  |        2,400 | Delta-to-star expansion into three transformer legs.                                                    |
| Areas / zones / owners      |  76 / 28 / 1 | Metadata or transfer-control preservation where applicable; not new AC elements.                        |
| Two-terminal DC links       |            9 | Existing `RawDcLine` compatibility import; validate scheduled transfer and terminal reactive treatment. |
| Impedance-correction tables |            4 | Existing interpolation support, including all referenced transformer table IDs.                         |
| Switched shunts             |        4,065 | Existing v33 switched-shunt parsing, block expansion, control mode, and initial `BINIT` treatment.      |

The RAW therefore contains 79,600 terminal buses and 83,425 ordinary AC
branches. Adding 13,496 two-winding transformer links and three legs for each
of the 2,400 three-winding units gives the 82,000-bus, 104,121-branch topology
in `case_SyntheticUSA.m`.

The VSC DC, multi-terminal DC, multi-section-line, FACTS, and GNE sections are
empty. No unsupported nonempty RAW section was found. The nonempty
two-terminal-DC section spans 27 physical lines because PSS/E encodes each of
the nine links as one DC-line record plus two converter records.

## Execution boundary and validation plan

No dynamic-model implementation is required for this package. A successful
full power-flow run requires the standard AC network plus the existing
two-terminal-DC, impedance-correction, switched-shunt, and three-winding
transformer paths to work together at scale. It is not sufficient to skip any
of those records simply because the base case has no DYR file.

Before using it as a regression reference, run the RAW and compare it with the
EPC and MATPOWER solved states. At a minimum, verify:

1. 79,600 RAW terminal buses expand to 82,000 GridDyn/MATPOWER buses and the
   link count is 104,121.
2. All 2,400 three-winding units preserve their three terminal connections,
   tap ratios, phase shifts, and ratings after star-bus expansion.
3. The nine two-terminal DC links retain their scheduled active transfers and
   the combined terminal reactive injection at shared buses.
4. All 4,065 switched shunts preserve their v33 control mode, limits, block
   data, and initial susceptance before comparing any regulated voltages.
5. Impedance-correction tables affect each transformer control path that
   references one; table-free transformers remain unchanged.
6. Compare voltage magnitude and angle, generation and load P/Q, branch and
   transformer flows, DC terminal transfers, and shunt injections against the
   chosen reference with declared tolerances.

The current command-line import test accepted the RAW and proceeded into the
large-case solve, so it did not fail at an unsupported-section boundary. Two
unmodified Debug runs were stopped after several minutes of CPU time without a
final solver status (each used roughly 0.7--0.9 GB of working memory). This is
not a convergence or numerical-parity result. Profile the large-case
initialization/solve path and complete a controlled rerun that captures the
final solver status and comparison values before adopting the case as a
regression reference.

Current static-run follow-ups are maintained in
[Power-flow validation follow-ups](powerflow-validation-followups.md).
