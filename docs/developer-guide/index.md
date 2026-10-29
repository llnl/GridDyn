# Developer guide and engineering notes

This section collects implementation references, compatibility assessments,
validation records, and work plans for GridDyn contributors. The pages range
from maintained interface descriptions to dated investigations; each page
states its scope and source snapshot where that distinction matters.

Start with the [style guide](style.md) for conventions. The
[naming migration plan](naming-migration-plan.md) records the completed
alignment campaign and summarizes its historical inventory snapshots.

## Architecture and reusable APIs

```{toctree}
:maxdepth: 1
:caption: Architecture and reusable APIs

bus-models
composite-load-framework
control-signal-routing
renewable-generator-architecture-refinement
optimizer-work-plan
partitioned-solver-performance-benchmark
reader-elements
```

## Model implementation references

```{toctree}
:maxdepth: 1
:caption: Model implementation references

csvgn1-model
epcgen-reference
esac5a-model
esst2a-model
governor-variants
gpwscc-reference
pss2b-esac2a-models
scrx-esac6a-model-spec
tiocr1-dyr-compatibility
```

## Input and compatibility assessments

```{toctree}
:maxdepth: 1
:caption: Input and compatibility assessments

activsg-dyr-compatibility-plan
andes-compatibility
fskit-retirement
openipsl-compatibility
powerdynamics-compatibility
powerworld-aux-reader
psse-raw-dc-compatibility
raw-model-coverage-worklist
syntheticusa-compatibility
test-case-import-gap-worklist
wecc-model-coverage-audit
```

## Validation and investigations

```{toctree}
:maxdepth: 1
:caption: Validation and investigations

activsg10k-three-winding-investigation
activsg25k-dynamic-investigation
activsg25k-dynamics-audit
ida-initial-condition-diagnostics
powerflow-validation-followups
```

## Development process

```{toctree}
:maxdepth: 1
:caption: Development process

naming-migration-plan
style
```

Case-specific investigations are retained as engineering records. Check their
dates and stated evidence before using them as a description of current
behavior; source code, tests, and current compatibility pages are the authority
for implemented behavior.
