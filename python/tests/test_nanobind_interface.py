import math
from pathlib import Path

import pytest

import griddyn as gd

REPO_ROOT = Path(__file__).resolve().parents[2]
PFLOW_FILE = REPO_ROOT / "test" / "test_files" / "pFlow_tests" / "two_bus_example.xml"
DYNAMIC_FILE = REPO_ROOT / "test" / "test_files" / "pFlow_tests" / "two_bus_dynamic_example.xml"
AREA_FILE = REPO_ROOT / "test" / "test_files" / "area_tests" / "area_test1.xml"
RELAY_FILE = REPO_ROOT / "test" / "test_files" / "relay_tests" / "test_relay_comms.xml"


def test_public_exports_are_available():
    assert isinstance(gd.__version__, str)
    assert gd.__version__
    assert gd.Gen is gd.Generator
    assert gd.GenCollection is gd.GeneratorCollection
    assert gd.Model is not None
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


def test_powerflow_collections_expose_results():
    sim = gd.load(PFLOW_FILE)

    sim.PFlow.run()

    assert len(sim.Bus) == 2
    assert sim.Bus.names == ["bus1", "bus2"]
    assert sim.buses[0].name == "bus1"
    assert sim.Bus["bus1"].voltage == pytest.approx(1.05)
    assert math.isfinite(sim.Bus["bus2"].angle)
    assert {"name", "v", "a", "p_gen", "p_load", "p_link"} <= set(sim.Bus.as_dicts()[0])

    assert len(sim.Gen) == 1
    assert len(sim.Generator) == 1
    assert sim.Gen.names == ["gen1"]
    assert isinstance(sim.Generator[0], gd.Generator)
    assert sim.gens["gen1"].bus == "bus1"
    assert sim.generators["gen1"].bus == "bus1"
    assert math.isfinite(sim.Gen[0].p)
    assert {"name", "bus", "p", "q", "pset"} <= set(sim.Gen.as_dicts()[0])

    assert len(sim.Load) == 2
    assert sim.Load.names == ["load1", "load2"]
    assert sim.loads["load2"].bus == "bus2"
    assert sim.Load["load1"].p == pytest.approx(1.15)
    assert {"name", "bus", "p", "q"} <= set(sim.Load.as_dicts()[0])

    assert len(sim.Link) == 1
    assert sim.Link.names == ["bus1_to_bus2"]
    assert sim.links["bus1_to_bus2"].bus1 == "bus1"
    assert sim.links[0].bus2 == "bus2"
    assert math.isfinite(sim.Link[0].p1)
    assert {"name", "bus1", "bus2", "p1", "q1", "p2", "q2", "loss"} <= set(
        sim.Link.as_dicts()[0]
    )


def test_model_collections_are_available():
    sim = gd.load(PFLOW_FILE)

    assert isinstance(sim.Bus[0], gd.Bus)
    assert isinstance(sim.Load[0], gd.Load)
    assert isinstance(sim.Link[0], gd.Link)
    assert isinstance(sim.Area, gd.AreaCollection)
    assert isinstance(sim.Relay, gd.RelayCollection)
    assert isinstance(sim.Sensor, gd.SensorCollection)

    assert len(sim.Area) == 0
    assert len(sim.Relay) == 0
    assert len(sim.Sensor) == 0
    assert sim.areas.names == []
    assert sim.relays.as_dicts() == []
    assert sim.sensors.to_list() == []


def test_find_returns_typed_models():
    sim = gd.load(PFLOW_FILE)
    bus = sim.find("bus1")

    assert isinstance(bus, gd.Bus)
    assert bus.name == "bus1"
    assert isinstance(sim.find("bus1_to_bus2"), gd.Link)
    assert isinstance(bus.find("load1"), gd.Load)
    assert isinstance(bus.find("gen1"), gd.Generator)
    assert isinstance(bus.find("link!bus2"), gd.Link)
    assert sim.find("not-a-real-object") is None
    assert bus.find("not-a-real-object") is None


def test_find_works_for_area_relay_and_sensor_models():
    area_sim = gd.load(AREA_FILE)
    area = area_sim.find("testArea")

    assert isinstance(area, gd.Area)
    assert isinstance(area.find("bus5"), gd.Bus)

    relay_sim = gd.load(RELAY_FILE)

    assert isinstance(relay_sim.find("load4control"), gd.Relay)
    assert isinstance(relay_sim.find("sensor1"), gd.Sensor)


def test_models_support_griddyn_get_and_set():
    sim = gd.load(PFLOW_FILE)

    assert sim.set("period", 0.25) is sim
    assert sim.get("period") == pytest.approx(0.25)
    assert sim.set("description", "python simulation").get_string("description") == (
        "python simulation"
    )

    bus = sim.Bus["bus1"]
    assert bus.set("period", 0.3) is bus
    assert bus.get("period") == pytest.approx(0.3)
    assert bus.set("voltage", 1.04).get("voltage") == pytest.approx(1.04)
    assert bus.set("description", "python bus").get_string("description") == "python bus"

    gen = sim.Generator["gen1"]
    assert gen.set("period", 0.35) is gen
    assert gen.get("period") == pytest.approx(0.35)
    assert gen.set("pset", 0.8).get("pset") == pytest.approx(0.8)
    assert gen.set("description", "python gen").get_string("description") == "python gen"

    load = sim.Load["load1"]
    assert load.set("period", 0.4) is load
    assert load.get("period") == pytest.approx(0.4)
    assert load.set("p", 1.2).get("p") == pytest.approx(1.2)
    assert load.set("description", "python load").get_string("description") == "python load"

    link = sim.Link["bus1_to_bus2"]
    assert link.set("period", 0.45) is link
    assert link.get("period") == pytest.approx(0.45)
    assert link.set("rating", 2.0).get("rating") == pytest.approx(2.0)
    assert link.set("description", "python link").get_string("description") == "python link"


def test_area_relay_and_sensor_support_griddyn_get_and_set():
    area_sim = gd.load(AREA_FILE)
    area = area_sim.Area["testArea"]

    assert area.set("period", 0.5) is area
    assert area.get("period") == pytest.approx(0.5)
    assert area.set("description", "python area").get_string("description") == "python area"

    relay_sim = gd.load(RELAY_FILE)
    relay = relay_sim.Relay["load4control"]
    sensor = relay_sim.Sensor["sensor1"]

    assert relay.set("period", 0.55) is relay
    assert relay.get("period") == pytest.approx(0.55)
    assert relay.set("description", "python relay").get_string("description") == "python relay"

    assert sensor.set("period", 0.6) is sensor
    assert sensor.get("period") == pytest.approx(0.6)
    assert sensor.set("description", "python sensor").get_string("description") == "python sensor"


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
