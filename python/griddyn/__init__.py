"""Python interface for GridDyn."""

from ._core import (
    Area,
    AreaCollection,
    Bus,
    BusCollection,
    ExecutionError,
    FileLoadError,
    Gen,
    GenCollection,
    Generator,
    GeneratorCollection,
    GridDynError,
    InvalidObjectError,
    InvalidParameterError,
    Link,
    LinkCollection,
    Load,
    LoadCollection,
    Model,
    Relay,
    RelayCollection,
    Sensor,
    SensorCollection,
    Simulation,
    SolveError,
    __doc__,
    __version__,
)


def load(path, *, format="", name=""):
    """Load a GridDyn simulation file."""
    return Simulation.from_file(path, format=format, name=name)


__all__ = [
    "Area",
    "AreaCollection",
    "Bus",
    "BusCollection",
    "ExecutionError",
    "FileLoadError",
    "Gen",
    "GenCollection",
    "Generator",
    "GeneratorCollection",
    "GridDynError",
    "InvalidObjectError",
    "InvalidParameterError",
    "Link",
    "LinkCollection",
    "Load",
    "LoadCollection",
    "Model",
    "Relay",
    "RelayCollection",
    "Sensor",
    "SensorCollection",
    "Simulation",
    "SolveError",
    "load",
]
