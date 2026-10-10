# Constrained TIOCR1 DYR compatibility

GridDyn imports the observed `TIOCR1` records from
`interpss/psse/v30/Bus200/200bus-gen-0805.dyr` through
`TimeOverCurrentRelay`. This is a deliberately narrow compatibility path,
not a claim to implement the complete PSS/E or InterPSS `TIOCR1` model.

## Supported record shape

The supported record has 29 payload fields (31 tokens including the bus and
model name):

```text
I 'TIOCR1' J 1 1 1 N BL
    I J 1 I J 1 I J 1
    I1 T1 I2 T2 I3 T3 I4 T4 I5 T5 I6 T6
    0.05 1 /
```

The reader requires all three transformer references to be identical, the
`BL` field, the three fixed `1` fields, the penultimate value `0.05`, and the
final enable value `1`. It resolves `I-J-circuit` as an existing adjustable
transformer and uses `N` to select the measured transformer terminal.

The six current/time pairs are retained as a monotone, piecewise-linear
time-current characteristic. The current values are currently interpreted as
kA on the selected transformer terminal and converted to GridDyn `puA` using
that bus voltage base. This unit interpretation is supported by the observed
Bus200 values but still needs authoritative model documentation or an
independent trajectory for confirmation.

## Intentional limitations

The reader rejects other `TIOCR1` shapes rather than guessing the meaning of
their fields. The semantics of the three repeated references, `BL`, `0.05`,
the final flag, and the exact breaker/trip behavior are not established by the
known cases. The current implementation therefore does not model any separate
phase CTs, multiple trip targets, relay filtering, or a confirmed two-sided
transformer breaker sequence. It trips the selected GridDyn transformer link
through the selected terminal switch.

The importer does not make the complete Bus200 DYR case loadable by itself;
other unsupported model families remain independent import gaps. Broader
`TIOCR1` support requires records with varying fields and an authoritative
PSS/E/InterPSS definition or reference trajectory.

## Merge boundary and future work

The mergeable scope is the native `TimeOverCurrentRelay`, its factory/CMake
registration, the guarded DYR dispatch, the constrained reader test, and the
small XML fixtures used by the relay tests. These pieces should be kept
together when the feature is promoted to the main GridDyn branch. The reader
must remain fail-fast for records outside the documented shape; silently
approximating unknown fields would make a protection study difficult to audit.

The following work is intentionally deferred:

1. Obtain an authoritative PSS/E or InterPSS `TIOCR1` definition and resolve
   the meanings of the repeated transformer references, `BL`, `0.05`, and the
   final enable flag.
2. Confirm whether the current points are kA, primary amps, secondary amps, or
   another base, and compare pickup/trip times against an independent relay
   trajectory.
3. Determine whether the model represents phase-specific CTs, multiple
   measuring elements, directional supervision, filtering, or a single
   positive-sequence current element.
4. Confirm the trip topology: one terminal, both transformer terminals, a
   dedicated breaker object, or a coordinated relay/breaker sequence. Add a
   two-terminal integration test only after that behavior is specified.
5. Broaden the reader only with representative records for each new variant;
   retain explicit rejection and diagnostics for unsupported variants.
6. Add an end-to-end case test once the remaining Bus200 DYR model families
   are supported. A successful `TIOCR1` import alone is not evidence that the
   complete Bus200 dynamic case is supported.

Before opening the merge request, verify that the relay implementation and all
three relay XML fixtures are included in the feature commit, that the DYR
compatibility note is included in the documentation build, and that the
targeted relay tests plus the normal component-test target pass.
