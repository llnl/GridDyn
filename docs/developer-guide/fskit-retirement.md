# FSKIT retirement and coupling boundary

FSKIT is retired from GridDyn. This change removes the FSKIT source tree, its
CMake discovery/configuration, the generated `GRIDDYN_ENABLE_FSKIT`
configuration symbol, and the legacy FSKIT/NS-3 test fixtures. There is no
compatibility shim and new code must not add one.

HELICS is the supported coupling direction in the current codebase. The
FSKIT removal does not define the future HELICS API: the HELICS interface is
expected to receive a separate, intentionally scoped update. That update
should preserve the existing model-facing behavior where practical, but it
must not reintroduce FSKIT names, headers, build options, or message types.

## Merge boundary

The retirement is complete when all of the following remain true:

- no tracked source, build, configuration, or active test file references
  `FSKIT`; this note is the intentional historical documentation reference;
- a default CMake configure/build succeeds without an FSKIT package;
- the normal component tests do not depend on the deleted coupling fixtures;
- HELICS remains an explicit optional feature and is validated by its own CI
  or opt-in test path when enabled.

Downstream applications that included FSKIT headers or enabled the old build
option require migration at their boundary. This merge does not attempt to
translate those applications automatically.

## Follow-up work

1. Define the future HELICS interface update separately, including ownership
   of time advancement, message delivery, lifecycle, and error propagation.
2. Add an opt-in HELICS regression that exercises the supported replacement
   path without coupling it to the relay or DYR-reader tests.
3. Audit downstream projects and release notes for the removed FSKIT CMake
   option, configuration macro, headers, and legacy test fixtures.
4. Keep the removal search in the merge checklist so a later HELICS refactor
   cannot accidentally recreate an FSKIT dependency under a new target name.
