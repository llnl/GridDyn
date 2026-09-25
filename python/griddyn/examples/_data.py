"""Paths to input files shipped with the examples."""

from pathlib import Path


DATA_DIR = Path(__file__).parent / "data"


def example_file(name: str) -> Path:
    """Return the path to an example network file."""
    return DATA_DIR / name
