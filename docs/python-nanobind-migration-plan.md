# Python Nanobind Migration Plan

This document tracks the plan for replacing GridDyn's SWIG-generated Python
interface with a first-class nanobind interface built directly on the C++
simulation API. It is intended to stay current as the work progresses.

## Summary

GridDyn currently exposes Python through SWIG wrappers over the C API in
`interfaces/python`. The new direction is to build a Pythonic interface with
nanobind that calls GridDyn C++ directly. The C API and SWIG-generated wrappers
should not be part of the new Python implementation.

The first milestone is deliberately narrow: expose the simulation object and the
minimum surface needed to load, initialize, solve, and run a GridDyn model from
Python. Object editing, events, queries, solver internals, and detailed result
access can follow after the simulation workflow is stable.

## Goals

1. Provide a modern Python package for GridDyn using nanobind.
2. Bind directly to C++ classes and functions, not to the C API.
3. Make `Simulation` the first supported public concept.
4. Replace return codes and output parameters with Python exceptions and return
   values.
5. Use modern Python conventions such as `pathlib.Path`, properties,
   type-friendly APIs, and eventually NumPy arrays.
6. Deprecate SWIG entirely and remove it as soon as replacement workflows are
   available.
7. Treat Matlab support, if needed later, as a separate interface problem rather
   than a SWIG requirement.

## Non-goals For The First Milestone

1. Bind the entire GridDyn C++ class hierarchy.
2. Reproduce the SWIG function-name API.
3. Wrap the C API with nanobind.
4. Support Matlab, Octave, or Java through the new Python work.
5. Provide complete object construction and model-editing support.
6. Expose advanced solver internals, Jacobians, or state-vector APIs.

## Current State

The existing Python interface is generated from `interfaces/griddyn.i` and
`interfaces/python/griddynPython.i`. It exposes the C API from
`src/griddyn_shared/griddyn_export.h` and
`src/griddyn_shared/griddyn_export_advanced.h`.

That API is intentionally C-shaped:

- Opaque `void*` handles represent objects, simulations, queries, events, and
  solver keys.
- Users call functions such as `gridDynSimulationLoadfile`,
  `gridDynSimulationRunTo`, and `gridDynObjectGetValue`.
- Error reporting is based on `GridDynError` output structures and return
  values.
- Arrays are exposed as raw pointer buffers.
- The generated Python files include SWIG pointer helpers and explicit object
  free functions.

This is useful for broad language binding generation, but it is not the surface
we want to preserve for Python.

## Target Package Shape

The preferred package shape is:

```text
python/
  griddyn/
    __init__.py
    py.typed
  griddyn_python.cpp
pyproject.toml
```

The compiled extension should live under the package, for example:

```text
griddyn/_core.pyd
```

Python users should import from `griddyn`, not from an implementation module:

```python
import griddyn as gd

sim = gd.Simulation(name="case14")
sim.load("case14.xml")
sim.initialize()
sim.powerflow()
sim.run_until(10.0)
```

## First Public API

The first public API should focus on loading and operating a simulation:

```python
class Simulation:
    def __init__(self, *, name: str = "", type: str = "default") -> None:
        ...

    def load(self, path: str | Path, *, format: str | None = None) -> None:
        ...

    def initialize(self, args: str | Sequence[str] | None = None) -> None:
        ...

    def powerflow(self) -> None:
        ...

    def run(self) -> float:
        ...

    def run_until(self, time: float) -> float:
        ...

    def step(self, time: float) -> float:
        ...

    def reset(self) -> None:
        ...

    @property
    def time(self) -> float:
        ...

    @property
    def name(self) -> str:
        ...

    @name.setter
    def name(self, value: str) -> None:
        ...
```

Potential aliases can be added where they improve readability:

- `load_file` as an alias for `load`
- `run_to` as an alias for `run_until`
- `current_time` as an alias for `time`

Aliases should be chosen carefully so the API remains small.

## C++ Binding Design

The nanobind module should use small adapter classes owned by the binding layer.
Those adapters should make Python ownership and error behavior explicit while
calling GridDyn C++ directly.

A likely first adapter is:

```cpp
class PySimulation {
  public:
    explicit PySimulation(std::string name = "", std::string type = "default");

    void load(const std::filesystem::path& path,
              std::optional<std::string> format = std::nullopt);
    void initialize(std::optional<nb::object> args = std::nullopt);
    void powerflow();
    double run();
    double runUntil(double time);
    double step(double time);
    void reset();

    double time() const;
    std::string name() const;
    void setName(std::string_view name);

  private:
    std::shared_ptr<griddyn::GriddynRunner> runner_;
};
```

The adapter should call C++ APIs such as:

- `griddyn::GriddynRunner`
- `griddyn::GridDynSimulation`
- `griddyn::loadFile`
- `GridDynSimulation::powerflow`
- `GridDynSimulation::run`
- `GridSimulation::getSimulationTime`

The nanobind module should not include or call:

- `griddyn_export.h`
- `griddyn_export_advanced.h`
- `GridDynSimulationCreate`
- `gridDynObjectCreate`
- any other C API entry points

## Error Handling

The Python layer should translate GridDyn C++ exceptions into Python
exceptions. Python code should not inspect integer return codes for normal error
handling.

Initial exception hierarchy:

```python
GridDynError
InvalidObjectError
InvalidParameterError
FileLoadError
SolveError
ExecutionError
```

The first implementation can map all known GridDyn exceptions to the closest
Python exception and use `GridDynError` as the fallback. The mapping should live
in the nanobind source so it is shared by every wrapped method.

## GIL Policy

Long-running simulation operations should release the GIL once they no longer
need Python objects:

- `load`
- `initialize`
- `powerflow`
- `run`
- `run_until`
- `step`
- `reset` if it can perform significant work

The wrapper should reacquire the GIL before throwing Python exceptions or
interacting with Python-owned objects.

## Build And Packaging

Use the `rtunits` Python package as the local model:

- `pyproject.toml`
- `scikit-build-core`
- `nanobind`
- CMake-driven extension build
- package-local compiled extension

Likely CMake options:

```cmake
GRIDDYN_BUILD_PYTHON_LIBRARY=ON
GRIDDYN_BUILD_SWIG_INTERFACE=OFF
```

The current `GRIDDYN_BUILD_PYTHON_INTERFACE` name is ambiguous because it points
to the SWIG interface today. As the new work lands, the CMake option names
should make the distinction clear:

- `GRIDDYN_BUILD_PYTHON_LIBRARY` for the nanobind package
- `GRIDDYN_BUILD_SWIG_INTERFACES` or similar for the legacy SWIG bindings

The nanobind target should link to internal C++ targets needed by
`GriddynRunner` and `GridDynSimulation`, not to `griddyn_shared_lib`.

## SWIG Deprecation And Removal

There are currently no known users relying on the SWIG bindings. That means
SWIG can likely be removed sooner rather than carried through a long
compatibility period.

Proposed policy:

1. Do not add new SWIG typemaps, generated files, or SWIG-based language
   interfaces.
2. Add deprecation messaging to the SWIG CMake path once the nanobind
   `Simulation` API can load and run a model.
3. Disable SWIG Python builds by default as soon as the nanobind package covers
   the basic simulation workflow.
4. Remove SWIG Python sources after a short transition period if no users
   appear.
5. Remove Matlab/Octave/Java SWIG paths unless a concrete user need appears.
6. If Matlab support is needed later, prefer a command-line, file-based, or
   purpose-built interface rather than reviving SWIG.

## C API Removal Direction

The nanobind migration should not depend on the C API. Once the Python package
and any other required workflows no longer need it, the C API can be retired.

Removal should be a separate cleanup phase because the C API currently packages
several useful concepts:

- opaque object handles
- centralized exception-to-error-code translation
- shared-library export boundaries
- raw-array math and solver access

Those concepts should either disappear from the public surface or be replaced
by clearer C++/Python mechanisms before the C API is deleted.

## Future API Phases

### Phase 2: Minimal Object Access

Expose enough object access to inspect and lightly edit loaded simulations:

```python
obj = sim.find("bus1")
obj.get("voltage", units="pu")
obj.set("angle", 0.0, units="rad")
obj.flag("enabled")
```

This will require a `GridObject` adapter with explicit non-owning references
that keep the parent `Simulation` alive.

### Phase 3: Results

Expose common simulation results in a Pythonic way:

- scalar results as return values
- table-like results as Python objects
- vector data as NumPy arrays where useful
- file save/load helpers only where they are part of normal workflows

### Phase 4: Events And Queries

Expose event scheduling and query objects after the simulation and object
surfaces are stable.

### Phase 5: Solver And Advanced Math APIs

Expose advanced state, residual, derivative, and Jacobian APIs only after
deciding the Python scientific stack expectations:

- NumPy arrays
- SciPy sparse matrices
- typed result objects
- ownership and mutability rules for state buffers

## Testing Plan

The first tests should be small and workflow-oriented:

1. Import `griddyn`.
2. Construct `Simulation`.
3. Load a tiny existing test model.
4. Initialize.
5. Run `powerflow`.
6. Read `time`.
7. Run to a short target time if the model supports dynamics.
8. Confirm invalid file loads raise `FileLoadError`.

The first tests should not depend on SWIG or the C API.

The CI workflow should build and smoke-test the Python wheel, but it should not
publish to TestPyPI or PyPI until the Python API is more complete.

## Open Decisions

1. Should the package source live in top-level `python/` or under
   `interfaces/python_nb/` during the transition?
2. Should the extension be named `griddyn._core`, `griddyn._griddyn`, or
   something else?
3. What is the smallest existing model file suitable for a fast Python
   smoke test?
4. Should `initialize(args=...)` accept a string, a sequence, or both in the
   first milestone?
5. What minimum Python version should be required?
6. Should stable ABI wheels be attempted early, or deferred until packaging is
   otherwise stable?
7. Should the first nanobind build link static GridDyn components into the
   extension or ship dependent GridDyn shared libraries beside it?

## Working Checklist

- [ ] Choose final package location.
- [x] Add `pyproject.toml` for scikit-build-core.
- [x] Add nanobind CMake discovery and extension target.
- [x] Add `griddyn` Python package directory.
- [x] Implement `PySimulation` adapter.
- [x] Bind `Simulation` in nanobind.
- [x] Add GridDyn exception translation.
- [x] Add basic Python import and construction smoke test.
- [x] Add CI wheel build and smoke test without package-index publishing.
- [ ] Add load and powerflow smoke test.
- [ ] Add documentation for the first Python API.
- [ ] Add SWIG deprecation messaging.
- [ ] Disable SWIG Python by default once the nanobind smoke tests pass.
- [ ] Decide whether to remove Matlab/Octave/Java SWIG build paths.
