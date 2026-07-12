from pathlib import Path

import pytest

import griddyn as gd


def test_public_exports_are_available():
    assert isinstance(gd.__version__, str)
    assert gd.__version__
    assert issubclass(gd.InvalidObjectError, gd.GridDynError)
    assert issubclass(gd.InvalidParameterError, gd.GridDynError)
    assert issubclass(gd.FileLoadError, gd.GridDynError)
    assert issubclass(gd.SolveError, gd.GridDynError)
    assert issubclass(gd.ExecutionError, gd.GridDynError)


def test_simulation_default_state_and_repr():
    sim = gd.Simulation(name="ci-smoke")

    assert sim.name == "ci-smoke"
    assert sim.time == 0.0
    assert "ci-smoke" in repr(sim)
    assert "time=0.000000" in repr(sim)


def test_simulation_name_is_mutable():
    sim = gd.Simulation(name="initial")

    sim.name = "renamed"

    assert sim.name == "renamed"


def test_invalid_simulation_type_raises_pythonic_error():
    with pytest.raises(gd.InvalidParameterError, match="unsupported simulation type"):
        gd.Simulation(type="not-a-type")


def test_missing_file_raises_file_load_error(tmp_path):
    sim = gd.Simulation(name="loader")
    missing_file = tmp_path / "does-not-exist.grid"

    with pytest.raises(gd.FileLoadError, match="file does not exist"):
        sim.load(missing_file)


def test_load_file_alias_uses_pathlike_protocol(tmp_path):
    sim = gd.Simulation(name="loader")
    missing_file = tmp_path / "does-not-exist.grid"

    with pytest.raises(gd.FileLoadError):
        sim.load_file(Path(missing_file))
