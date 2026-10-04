# WECC model coverage gaps from the Keentel model summary

**Audit date:** 2026-10-04
**Source list:** [Why Dynamic Models Matter for Grid Reliability](https://keentelengineering.com/why-dynamic-models-matter-grid-reliability)

This page records a source-code coverage comparison for the renewable and
nonrenewable model names called out in the linked article. It compares GridDyn
model implementations and factory registrations with the DYR and DYD import
dispatch. “Missing” means no model-specific implementation was found. When a
model exists in GridDyn but has no exact DYR/DYD import mapping, that import gap
is called out separately. A related or similarly named model is not treated as
an equivalent without an equation and parameter-mapping review.

The linked page is an engineering blog, not the WECC model library itself. Its
approval and retirement statements are treated here as candidate names to
check, not as an independent determination of current WECC approval status.

## Renewable models

| Model named by the article | GridDyn coverage                      | Notes                                                                                                                         |
| -------------------------- | ------------------------------------- | ----------------------------------------------------------------------------------------------------------------------------- |
| REGC_A                     | Supported as `REGCA1`                 | DYR and DYD paths exist.                                                                                                      |
| REGC_B                     | **Gap**                               | No REGC_B implementation or import mapping found. `REGCP1` is a distinct GridDyn converter variant, not assumed to be REGC_B. |
| REEC_A                     | Supported as `REECA1`                 | DYR and DYD paths exist.                                                                                                      |
| REEC_C                     | Supported as `REECC1`                 | DYR accepts `REECC1` and `REECCU1`; this audit does not find a PSLF DYD mapping for REEC_C.                                   |
| REEC_D                     | **Gap**                               | No REEC_D implementation or import mapping found.                                                                             |
| REPC_A                     | Present as `REPCA1`, with limitations | The implementation covers local plant voltage/reactive control. Remote-bus and monitored-line operation are not supported.    |
| REPC_B, REPC_C, REPC_D     | **Gap**                               | No implementations or import mappings found.                                                                                  |
| REGFM_A1                   | **Gap**                               | No grid-forming REGFM_A1 implementation or import mapping found.                                                              |

The model-name adapters for REGC_A, REEC_A, and REPC_A are in
`src/fileInput/gridDynReadDYD.cpp`; PSS/E DYR dispatch is in
`src/fileInput/gridDynReadDYR.cpp`. REEC_C is registered in
`src/griddyn/generators/Generator.cpp` and has a DYR record mapping. The
REPCA1 limitations are enforced by its implementation and DYR loader.

## Nonrenewable models

### Excitation systems

| Model named by the article | GridDyn coverage                 | Notes                                                                                                                                             |
| -------------------------- | -------------------------------- | ------------------------------------------------------------------------------------------------------------------------------------------------- |
| AC1A                       | Related implementation: `ESAC1A` | The class documents an IEEE Type AC1A implementation, but the DYR record name accepted by GridDyn is `ESAC1A`; there is no bare `AC1A` DYR alias. |
| AC2A, AC3A                 | **Gap**                          | No matching named implementation or DYR route found.                                                                                              |
| ESST7B, ST6C               | **Gap**                          | No matching named implementation or DYR route found. Existing ESST1A–ESST4B and ESAC6A models are not treated as equivalents.                     |

The article lists EXAC3 and MEXS as retired/not approved. They are not counted
as gaps against its current approved-model target.

### Synchronous machine models

| Model named by the article | GridDyn coverage                                         | Notes                                                                                                                                                      |
| -------------------------- | -------------------------------------------------------- | ---------------------------------------------------------------------------------------------------------------------------------------------------------- |
| GENQEC                     | **Gap**                                                  | No GENQEC implementation or import mapping found.                                                                                                          |
| GENROU                     | Supported                                                | Native model and DYR path exist. The article's approval status statements for GENROU are internally inconsistent, so this audit makes no compliance claim. |
| GENTPJ                     | Supported, though described as phased out by the article | Native model and DYR path exist; its presence in GridDyn does not establish that it is appropriate for a current compliance study.                         |

### Power system stabilizers

| Model named by the article | GridDyn coverage | Notes                                                                                                                                 |
| -------------------------- | ---------------- | ------------------------------------------------------------------------------------------------------------------------------------- |
| PSS2A, PSS2C, PSS3B        | **Gap**          | No matching named model or DYR route found. GridDyn's IEEEST model is not counted as equivalent without a parameter/equation mapping. |
| PSS4B, PSS4C               | **Gap**          | No matching named model or DYR route found.                                                                                           |

PSSSH is identified by the article as proprietary and not approved, so it is
not counted as a gap against the article's current target. GridDyn currently
registers IEEEST and ST2CUT stabilizers.

### Loads and motors

| Model named by the article | GridDyn coverage                           | Notes                                                                                                                                              |
| -------------------------- | ------------------------------------------ | -------------------------------------------------------------------------------------------------------------------------------------------------- |
| CMPLDW, CMPLDWG            | **Gap**                                    | No composite-load model implementation or DYR mapping found. Existing aggregate or individual load classes do not provide CMPLDW behavior by name. |
| MOTOR1                     | Implemented internally; **DYR import gap** | Registered as `motor1`, but the DYR dispatcher has no `MOTOR1` record route.                                                                       |
| MOTORW                     | **Gap**                                    | No MOTORW-specific implementation or import mapping found. Other induction-motor load models are not assumed equivalent.                           |

### Turbine governors

| Model named by the article      | GridDyn coverage | Notes                                                                                                                                                                                                               |
| ------------------------------- | ---------------- | ------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------- |
| GGOV1                           | Supported        | Native implementation and DYR path exist.                                                                                                                                                                           |
| GGOV1D, IEEEG1D, HYGOVD, TGOV1D | **Gap**          | No exact named implementations or import mappings found. GridDyn has GGOV1, IEEEG1, HYGOV, and TGOV1, plus some DB variants; those are not assumed equivalent to the article's D models without a detailed mapping. |

PIDGOV and G2WSCC are listed as retired by the article and are not counted as
gaps against its current-model target.

### Protection and relay models

The following article-listed model names have no matching model-specific
implementation or DYR route found in GridDyn:

- LHFRT, LHVRT, LOCTI, OOSLEN, and TIOC
- SCL1C, SCL2C, PF1, PF2, VAR1, and VAR2

GridDyn has general relay, breaker, fuse, and distributed-generation
protection components. Those components do not establish support for the
named WECC relay equations or parameter records.

### Additional models mentioned in the article's FAQ

| Model named by the article | GridDyn coverage | Notes                                                                                                                             |
| -------------------------- | ---------------- | --------------------------------------------------------------------------------------------------------------------------------- |
| DER_A                      | **Gap**          | No DER_A-specific model or import mapping found.                                                                                  |
| CHVDC2, VHVDC1             | **Gap**          | No named dynamic controller implementations or import mappings found. Generic DC network elements are not treated as equivalents. |

## Source files checked

- Model factories: `src/griddyn/exciters/Exciter.cpp`,
  `src/griddyn/genmodels/GenModel.cpp`,
  `src/griddyn/stabilizers/Stabilizer.cpp`,
  `src/griddyn/loads/MotorLoad.cpp`,
  `src/griddyn/governors/Governor.cpp`, and `src/griddyn/relays/Relay.cpp`.
- Import dispatch: `src/fileInput/gridDynReadDYR.cpp` and
  `src/fileInput/gridDynReadDYD.cpp`.
- Renewable factories: `src/griddyn/generators/Generator.cpp`.

This is a source inventory, not a numerical validation study. “Supported”
means a native implementation and applicable named import path exist. Entries
marked partial or internal-only spell out where that condition is not met.
This does not certify every parameter combination, control option,
initialization case, or cross-tool trajectory against WECC/PSSE/PSLF reference
results.
