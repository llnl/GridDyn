from pathlib import Path

import pytest

import griddyn as gd

REPO_ROOT = Path(__file__).resolve().parents[2]
PFLOW_FILE = REPO_ROOT / "test" / "test_files" / "pFlow_tests" / "two_bus_example.xml"
DYNAMIC_FILE = REPO_ROOT / "test" / "test_files" / "pFlow_tests" / "two_bus_dynamic_example.xml"


def test_public_exports_are_available():
    assert isinstance(gd.__version__, str)
    assert gd.__version__
    assert issubclass(gd.InvalidObjectError, gd.GridDynError)
    assert issubclass(gd.InvalidParameterError, gd.GridDynError)
    assert issubclass(gd.FileLoadError, gd.GridDynError)
    assert issubclass(gd.SolveError, gd.GridDynError)
    assert issubclass(gd.ExecutionError, gd.GridDynError)
    assert callable(gd.load)


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


def test_load_returns_self_for_chaining():
    sim = gd.Simulation(name="two-bus")

    loaded = sim.load(PFLOW_FILE)

    assert isinstance(loaded, gd.Simulation)
    assert loaded.name == "2bus_test"
    loaded.powerflow()


def test_simulation_from_file_loads_powerflow_case():
    sim = gd.Simulation.from_file(PFLOW_FILE)

    assert sim.name == "2bus_test"
    sim.powerflow()
    assert sim.time == 0.0


def test_andes_style_load_and_powerflow():
    sim = gd.load(PFLOW_FILE)

    sim.PFlow.run()

    assert sim.name == "2bus_test"
    assert sim.pflow is not None


def test_simulation_can_run_dynamic_file(monkeypatch, tmp_path):
    monkeypatch.chdir(tmp_path)
    sim = gd.Simulation.from_file(DYNAMIC_FILE)

    sim.powerflow()
    sim.initialize()
    final_time = sim.run_until(0.1)

    assert final_time >= 0.1
    assert sim.time >= 0.1


def test_andes_style_time_domain_run(monkeypatch, tmp_path):
    monkeypatch.chdir(tmp_path)
    sim = gd.load(DYNAMIC_FILE)

    sim.PFlow.run()
    sim.TDS.init()
    final_time = sim.TDS.run_until(0.1)

    assert final_time >= 0.1
    assert sim.TDS.time >= 0.1
