# Capabilities and limits

This page summarizes the Python package surface documented for the current
release. GridDyn's C++ application and optional libraries expose additional
features that are selected at build time.

| Workflow                      | Current Python support                                                                                                |
| ----------------------------- | --------------------------------------------------------------------------------------------------------------------- |
| Load a supported network file | Load MATPOWER cases and GridDyn XML inputs through `griddyn.load()`                                                   |
| Power flow                    | Run the steady-state power-flow solver and inspect bus, generator, and other object collections                       |
| Dynamic simulation            | Initialize and advance time-domain simulation; configure and read recorder output files                               |
| Network edits                 | Read and set supported model parameters, including numeric values with unit conversion                                |
| Power-flow export             | Write static network cases in PYPOWER/MATPOWER format and power-flow result summaries                                 |
| Optimal power flow            | Run continuous DC OPF for supported polynomial costs through quadratic order when built with the optimization library |

Current Python API boundaries include no AC OPF, integer unit commitment,
piecewise-linear cost solution, in-memory recorder-history object, or general
network-construction API. The static case exporters omit dynamic models,
controls, relays, and events. Unsupported export data may be reported in the
warnings returned by the export methods.

PYPOWER is not a GridDyn dependency. It is only needed if an application wants
to load or execute the exported Python case file with PYPOWER itself. For
examples and details, see the [Python interface guide](python-interface.md).
