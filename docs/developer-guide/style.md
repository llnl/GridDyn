# GridDyn Style Guide

The goal of this style guide is to describe the naming conventions we want to
use for developing GridDyn. This guide is adapted from the HELICS developer
style guide so the two projects follow the same conventions where practical.
Formatting conventions are primarily enforced through the repository
`.clang-format` and `.editorconfig` files.

## Naming Conventions

1. All free functions should be `camelCase`

   ```cpp
   std::shared_ptr<gridDynSimulation> makeSimulation(const std::string& fileName);
   ```

   Exception: when the functionality intentionally matches a standard library
   function such as `to_string`.

2. All classes should be `PascalCase`

   ```cpp
   class GridSimulation: public Area
   {
   public:
       GridSimulation();
   };
   ```

3. Class methods should be `camelCase`

   ```cpp
   void setTime(coreTime newTime);
   ```

   Exception: methods that intentionally match standard library naming.

4. Enumeration names should be `PascalCase`. Enumeration values should be
   `CAPITAL_SNAKE_CASE`

   ```cpp
   enum class SolverPrintLevel {
       ERROR_TRAP,
       WARNINGS,
       DEBUG_OUTPUT
   };
   ```

5. Constants and macros should be `CAPITAL_SNAKE_CASE`

6. Variable names:
   local variable names should be `camelCase`
   member variable names should be `mPascalCase`
   static const members should be `CAPITAL_SNAKE_CASE`
   function input names should be `camelCase`
   index variables can be `camelCase` or short forms such as `ii`, `jj`, `kk`
   global variables should be `gPascalCase`

7. All C++ functions and types should be contained in the `griddyn` namespace
   with subnamespaces used as appropriate

   ```cpp
   namespace griddyn
   {
       ...
   }  // namespace griddyn
   ```

8. C interface functions should begin with `griddynXXXX`

9. C interface functions should be of the format `griddyn{Class}{Action}` or
   `griddyn{Action}` if no class is appropriate

10. All CMake commands that come from CMake itself should be lower case

    ```text
    if as opposed to IF
    install vs INSTALL
    ```

11. Public interface functions should be documented with Doxygen-style comments

    ```cpp
    /** get the identifier for an object
        @param obj the object to query
        @return a string with the object identifier
    */
    ```

12. File names should match class names where practical, especially for new
    code and files renamed as part of cleanup work

13. User-facing options should use a single canonical spelling expressed as
    `nocase`, `camelCase`, or `snake_case`. Do not support multiple synonymous
    names for the same option.

## Migration Notes

GridDyn contains a substantial amount of legacy code that predates these
conventions. New code should follow this guide immediately. Existing code should
be migrated incrementally, with changes grouped into small, reviewable refactors
that preserve public interfaces unless the rename is intentional and coordinated.
