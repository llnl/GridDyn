"""Python interface for GridDyn."""

from ._core import (
    ExecutionError,
    FileLoadError,
    GridDynError,
    InvalidObjectError,
    InvalidParameterError,
    Simulation,
    SolveError,
    __doc__,
    __version__,
)


def load(path, *, format="", name=""):
    """Load a GridDyn simulation file."""
    return Simulation.from_file(path, format=format, name=name)


__all__ = [
    "ExecutionError",
    "FileLoadError",
    "GridDynError",
    "InvalidObjectError",
    "InvalidParameterError",
    "Simulation",
    "SolveError",
    "load",
]
