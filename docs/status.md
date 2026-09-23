# Documentation Status

This Read the Docs configuration is a bootstrap scaffold.

Current state:

- Read the Docs now has an in-repo configuration file.
- Sphinx and MyST are configured for Markdown-based pages.
- The landing pages in this section are intentionally minimal.

## Project status snapshot (2026-09-22)

The main branch now contains the merged ACTIVSg10k dynamic-model compatibility
work. The short-run evidence is:

- EXAC1 `TA=0` is represented as an algebraic regulator path, including the
  associated bypassed states.
- EXAC1/EXAC2 limiter root transitions have regression coverage for stale root
  directions, state projection, and rearming after time advances.
- GENSAL accepts the valid degenerate case `Xdp == Xdpp`.
- The Release generator component suite passes all 182 tests.
- The ACTIVSg10k RAW/DYR case can initialize and advance through the first
  10 MW load-step root sequence. A run to `t=1.03 s` completes, but takes
  approximately 76 seconds and is not yet evidence of an accepted 10-second
  trajectory.

The remaining compatibility boundary is unchanged: Texas7k and ACTIVSg25k
full dynamic execution still require the missing renewable plant families and
additional case-level validation. The ACTIVSg10k load-step runtime and the
post-disturbance limiter cascade remain open performance/model-validation work.

The basic EPC+DYD path is now exercised for the ACTIVSg500 case. DYD records
that map to existing DYR model loaders are attached, while unsupported model
families produce a summarized import error. Generalized-load records are
currently accepted and ignored so that the EPC loads retain DYR-equivalent
static-load behavior; implementing their voltage/frequency dependence remains
future work.

Planned improvements:

- migrate the existing installation and quick-start material into Sphinx-friendly pages
- add API documentation integration
- organize user, developer, and reference documentation into separate sections
- restore richer assets such as diagrams and manuals in a maintainable way
