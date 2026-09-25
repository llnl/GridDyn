# Documentation release plan

**Review date:** 2026-09-25
**Release context:** Python package version 0.11.4 is the current project version.

This plan records a practical documentation baseline and a staged path to
improve the release documentation. It prioritizes accurate user guidance and
then closes the highest-value gaps in the generated C++ reference. It does not
make the entire C++ implementation a release blocker.

## Current baseline

- The PyPI project description is the repository `README.md`. The README now
  links to Read the Docs for user guidance and includes the documentation URL
  in package metadata.
- The Read the Docs build uses Sphinx, MyST, and the Read the Docs theme. Its
  content is Markdown under `docs/`; the pinned Sphinx requirements are in
  `docs/requirements.txt`.
- The Python guide documents the package's Python 3.13 minimum, four packaged
  examples, power-flow export, dynamic recorder workflow, and current DC OPF
  limits. PYPOWER is not a GridDyn installation or CI dependency.
- The Windows Visual Studio 2022 configuration found MSVC 19.44 and Doxygen
  1.18. The CMake `doc` target completed and generated HTML after correcting
  the missing logo path and Doxygen target working directory.
- `config/Doxyfile.in` is now a compact project configuration instead of the
  large generated template. The CMake target was rebuilt with it, and the
  warning count stayed at 3,786 after the focused API pass.
- Doxygen warnings remain enabled. Doxygen writes the full report to
  `build/doxygen_warnings.log`; the HTML output is directed to a build folder.
  The current scan reads `src/` and `docs/`, and excludes test sources and
  presentations. This is broader than a curated public C++ API list.
- The initial local Doxygen baseline reported 3,798 warning entries: about
  3,753 undocumented declarations and 45 other warnings. A focused pass on
  `GridDynSimulation.h` documented the dynamic solver choices, residual
  parallelism policy, partitioned initialization, and parameter sensitivities.
  The current count is 3,786 entries, including 3,741 undocumented
  declarations; the 45 other warnings remain. The largest clusters are in
  PARADAE, optimization, and FMI headers. Many other warnings are Doxygen's
  ambiguous association of inherited overloaded `set()` documentation, where
  the candidate list makes the log especially large. Counts depend on the
  Doxygen version and source revision.
- The live Read the Docs pages still show older content until this branch is
  published and Read the Docs builds it. This source update does not publish
  the site by itself.
- A local Sphinx build is still unverified. Sphinx is not installed in the
  local Python environment, and installing the pinned requirements stalled;
  the Read the Docs build remains the release check for the Sphinx pages.

## Before the next release

1. Build the Sphinx site with warnings treated as errors, and repair missing or
   broken links in the pages linked from the README.
2. Check that each quick-start command and API example matches the packaged
   Python examples and current binding behavior. Keep PYPOWER described only as
   an optional consumer of exported case files.
3. Confirm PyPI metadata points to the user docs, the installation page names
   Python 3.13+, and the README can stand alone as the package landing page.
4. Make the Read the Docs latest build from the release branch or main branch
   available before the PyPI announcement. Confirm the deployed page and its
   navigation after Read the Docs finishes.
5. Keep the CMake Doxygen target buildable and retain its complete warning
   file. Fix clear comment/parser defects as they are encountered; do not
   suppress undocumented warnings to make the count appear smaller.

## Next documentation pass

### User guide

- Add concise guides for the main user workflows: supported input formats,
  network model configuration, steady-state power flow, dynamic simulation,
  recorders and output files, and the optimization/export features.
- Explain units, parameter names, return values, errors, and known boundaries
  where users make decisions. Mark optional features and required build options
  explicitly.
- Keep examples runnable from an installed package. Prefer the packaged
  example cases over historical machine-specific paths and output snapshots.
- Add a clear version/release note and a compatibility/support table once the
  supported platforms and wheel matrix for the release are confirmed.

### C++ API reference

1. Decide which C++ headers represent supported interfaces. Separate those
   from internal solver machinery, generated code, and optional integrations
   before setting a documentation completeness target.
2. Triage the current warnings in this order: malformed or unresolved comment
   commands; genuinely missing documentation on key entry points; ambiguous
   inherited overload associations; then undocumented internal declarations.
3. Document the high-use public concepts first: simulation lifecycle, network
   loading and readers, buses/loads/generators/links, control models, solver
   configuration, and result/recorder access. Cover ownership and lifetime,
   units, side effects, error behavior, and optional-build requirements.
4. Review each page for useful descriptions and examples instead of treating
   “zero undocumented declarations” as the quality measure. Maintain a short
   warning register by category and revisit counts after each focused pass.
5. Decide where generated C++ HTML should be published after its scope and
   navigation are stable. Evaluate a Breathe/Sphinx bridge against the simpler
   separate Doxygen output; do not add another documentation dependency until
   it improves reader navigation.

### Documentation build setup

- Keep the Sphinx guides and the Doxygen C++ reference as separate build
  targets for now. The user guide build is small and does not need to depend on
  a C++ compiler or Graphviz.
- Keep `config/Doxyfile.in` compact. Add a setting only when it changes
  intended output or warning behavior, and preserve the warning log, explicit
  exclusions, and HTML-only output.
- Add the Sphinx warning-as-error build to the documentation review workflow.
  Consider a Doxygen build check separately once its runtime and warning
  baseline are stable enough to make CI output actionable.
- Check screenshots, diagrams, and linked assets for licensing, source, and
  maintenance ownership before restoring old manuals or presentations to the
  primary user navigation.

## Completion checks

For each release documentation update:

- The Read the Docs configuration builds successfully with no Sphinx warnings
  on the documented Python version.
- The README and PyPI metadata link to live, relevant user pages.
- Commands and examples match the supported package and source build paths.
- The Doxygen `doc` target generates HTML, and new warning classes are
  reviewed from its saved warning file.
- Documentation describes current limits as clearly as supported features.
