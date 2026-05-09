# GridDyn Naming Audit

This is a first-pass inventory of prominent naming patterns in GridDyn that do
not match the project style guide adapted from HELICS.

## Highest-Impact Legacy Patterns

1. Many core class names are lower camel case or lower case instead of
   `PascalCase`

   Examples:
   - `coreObject`
   - `gridSimulation`
   - `gridBus`
   - `basicSolver`
   - `commMessage`
   - `gridRandom`

2. Many enum names use snake case or `_t` suffixes instead of `PascalCase`

   Examples:
   - `solver_print_level`
   - `contingency_mode_t`
   - `recovery_return_codes`
   - `dist_type_t`
   - `satType_t`

3. Member naming is mixed

   A large portion of the code already uses `m_` or trailing underscore
   conventions such as `m_refCount`, `m_messageType`, `payload_V`, and
   `directory_`, while the target convention is `mPascalCase`.

4. Some free functions still use snake case

   Examples:
   - `enable_updates`
   - `should_log`
   - `log_to`
   - `make_block`
   - `send_var`
   - `get_devices`

5. Several filenames do not match their primary class names under the target
   convention

   Examples:
   - `gridSimulation.h` containing `gridSimulation`
   - `coreObject.h` containing `coreObject`
   - `commMessage.h` containing `commMessage`

## Recommended Migration Order

1. Start with new and lightly used code only.
2. Prefer internal helper functions and private members before public classes.
3. Rename enum types before renaming large foundational classes.
4. Defer broad public API renames until we decide whether to preserve legacy
   aliases or make a breaking cleanup pass.

## Good First Renaming Candidates

These appear relatively contained compared to foundational types:

- Snake-case helper functions in newer code paths such as networking and
  communication helpers
- Enum types with limited external exposure
- Private member names in leaf classes

## High-Risk Renaming Areas

These are widely referenced and should be handled only in dedicated refactors:

- `coreObject`
- `gridDynSimulation`
- `gridSimulation`
- `gridBus`, `acBus`, `dcBus`
- Core solver interfaces and widely included headers
