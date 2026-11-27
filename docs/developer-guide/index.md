# Developer guide and engineering notes

This section collects implementation references, compatibility assessments,
validation records, and work plans for GridDyn contributors. The pages range
from maintained interface descriptions to dated investigations; each page
states its scope and source snapshot where that distinction matters.

Start with the [style guide](style.md) for conventions. The
[naming migration plan](naming-migration-plan.md) records the completed
alignment campaign and summarizes its historical inventory snapshots.

## Architecture and reusable APIs

- [Bus models](bus-models.md)
- [Model parameters and declarations](model-parameters.md)
- [Composite load framework](composite-load-framework.md)
- [Control signal routing](control-signal-routing.md)
- [Renewable generator architecture](renewable-generator-architecture-refinement.md)
- [Optimizer framework work plan](optimizer-work-plan.md)
- [Reader elements](reader-elements.md)
- [Checkpointing and restore boundaries](checkpointing.md)
- [HDF5 state, diagnostic and recorder output](binary-state-output.md)
- [HDF5 integration assessment](hdf5-integration-assessment.md)

## Model implementation references

- [CSVGN1 static var source](csvgn1-model.md)
- [EPCGEN reference](epcgen-reference.md)
- [ESAC5A exciter](esac5a-model.md)
- [ESST2A exciter](esst2a-model.md)
- [Exciter model descriptions](exciter-model-description.md)
- [Component model descriptions](component-model-descriptions.md)
- [COMP compensation model](comp-model.md)
- [Governor variants](governor-variants.md)
- [GPWSCC reference](gpwscc-reference.md)
- [IEEEG3, WPIDHY and MNLEX2](ieeeg3-wpidhy-mnlex2-models.md)
- [DQ induction motor](motor-dq-model.md)
- [PSS2B and ESAC2A](pss2b-esac2a-models.md)
- [SCRX and ESAC6A](scrx-esac6a-model-spec.md)
- [STAB2A stabilizer](stab2a-model.md)
- [TIOCR1 DYR compatibility](tiocr1-dyr-compatibility.md)
- [Underexcitation and overexcitation limiters](uel-oel-models.md)

## Input and compatibility assessments

- [ACTIVSg DYR compatibility](activsg-dyr-compatibility-plan.md)
- [ANDES compatibility](andes-compatibility.md)
- [FSKIT retirement](fskit-retirement.md)
- [GridKit induction motor assessment](gridkit-induction-motor-assessment.md)
- [OpenIPSL compatibility](openipsl-compatibility.md)
- [PowerDynamics compatibility](powerdynamics-compatibility.md)
- [PowerWorld AUX reader](powerworld-aux-reader.md)
- [PSS/E RAW DC compatibility](psse-raw-dc-compatibility.md)
- [RAW model coverage worklist](raw-model-coverage-worklist.md)
- [SyntheticUSA compatibility](syntheticusa-compatibility.md)
- [Test case import gaps](test-case-import-gap-worklist.md)
- [WECC model coverage](wecc-model-coverage-audit.md)

## Validation and investigations

- [ACTIVSg10k three-winding transformer investigation](activsg10k-three-winding-investigation.md)
- [ACTIVSg25k dynamics investigation](activsg25k-dynamic-investigation.md)
- [ACTIVSg25k dynamic model audit](activsg25k-dynamics-audit.md)
- [GenModel8 review](genmodel8-review.md)
- [IDA initial-condition diagnostics](ida-initial-condition-diagnostics.md)
- [Partitioned solver performance benchmark](partitioned-solver-performance-benchmark.md)
- [Power-flow validation follow-ups](powerflow-validation-followups.md)

## Development process

- [Command interface](../command-interface.md)
- [Naming migration plan](naming-migration-plan.md)
- [Style guide](style.md)

Case-specific investigations are retained as engineering records. Check their
dates and stated evidence before using them as a description of current
behavior; source code, tests, and current compatibility pages are the authority
for implemented behavior.
