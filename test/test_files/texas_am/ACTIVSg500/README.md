# ACTIVSg500 — Texas A&M University synthetic case

This directory contains the PSS/E inputs used to exercise the ACTIVSg500 case
in GridDyn:

- `ACTIVSg500.RAW` — power-flow network data
- `ACTIVSg500.EPC` — PowerWorld/EPC power-flow network data
- `ACTIVSg500_dynamics.dyr` — transient-stability dynamic-model data
- `ACTIVSg500_dynamics.dyd` — PSLF dynamic-model data for the EPC case

## Attribution

ACTIVSg500 is the **SouthCarolina 500-Bus System**, a fully synthetic case
provided by Texas A&M University researchers through the Electric Grid Test
Case Repository. It is based on public information and statistical analysis,
and does not represent the actual electric grid in South Carolina or contain
CEII.

Source pages:

- [SouthCarolina 500-Bus System: ACTIVSg500](https://electricgrids.engr.tamu.edu/electric-grid-test-cases/activsg500/)
- [Electric Grid Test Case Repository — References](https://electricgrids.engr.tamu.edu/references/)

Please retain this attribution when creating derived GridDyn test cases.

## Recommended citations

The repository requests citation of the synthetic-network creation work and,
when dynamic models are used, the transient-stability and system-dynamics
references:

1. A. B. Birchfield, T. Xu, K. M. Gegner, K. S. Shetye, and T. J. Overbye,
   “Grid Structural Characteristics as Validation Criteria for Synthetic
   Networks,” _IEEE Transactions on Power Systems_, vol. 32, no. 4,
   pp. 3258–3265, July 2017. [IEEE Xplore](https://ieeexplore.ieee.org/document/7725528/)
2. T. Xu, A. B. Birchfield, K. S. Shetye, and T. J. Overbye, “Creation of
   Synthetic Electric Grid Models for Transient Stability Studies,” _2017
   IREP Symposium Bulk Power System Dynamics and Control_, Espinho, Portugal, 2017. [Paper](https://irep2017.inesctec.pt/conference-papers/conference-papers/paper23i1v5hwmh.pdf)
3. T. Xu, A. B. Birchfield, and T. J. Overbye, “Modeling, Tuning and Validating
   System Dynamics in Synthetic Electric Grids,” _IEEE Transactions on Power
   Systems_, 2018. [IEEE Xplore](https://ieeexplore.ieee.org/document/8334287/)
