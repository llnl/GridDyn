import ast
import copy
import importlib.util
import math
from pathlib import Path

import pytest

import griddyn as gd

REPO_ROOT = Path(__file__).resolve().parents[2]
PFLOW_FILE = REPO_ROOT / "test" / "test_files" / "pFlow_tests" / "two_bus_example.xml"
CASE9_FILE = REPO_ROOT / "test" / "test_files" / "matlab_test_files" / "case9.m"
DYNAMIC_FILE = REPO_ROOT / "test" / "test_files" / "pFlow_tests" / "two_bus_dynamic_example.xml"
AREA_FILE = REPO_ROOT / "test" / "test_files" / "area_tests" / "area_test1.xml"
RELAY_FILE = REPO_ROOT / "test" / "test_files" / "relay_tests" / "test_relay_comms.xml"


def _exported_matrix(text, output_format, matrix_name):
    if output_format == ".py":
        marker = f'ppc["{matrix_name}"] = array(['
        block = text.split(marker, 1)[1].split("\n    ])", 1)[0]
        rows = [line.strip().removesuffix("],").removeprefix("[") for line in block.splitlines()]
        return [[float(value.strip()) for value in row.split(",")] for row in rows if row]

    marker = f"mpc.{matrix_name} = ["
    block = text.split(marker, 1)[1].split("];", 1)[0]
    rows = [line.strip().removesuffix(";") for line in block.splitlines()]
    return [[float(value) for value in row.split()] for row in rows if row]


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
    assert sim.optimization is None
    sim.powerflow()
    assert sim.time == 0.0


def test_optimization_has_its_own_interface():
    sim = gd.load(CASE9_FILE, type="optimization")

    assert isinstance(sim.optimization, gd.Optimization)
    assert not hasattr(sim, "opf")
    assert not hasattr(sim, "set_generator_cost_curve")


@pytest.mark.parametrize(
    ("method_name", "suffix"),
    [("save_pypower_case", ".py"), ("save_matpower_case", ".m")],
)
def test_case_export_round_trips_from_python(tmp_path, method_name, suffix):
    sim = gd.load(CASE9_FILE, type="optimization")
    sim.Bus[3].set("voltage", 1.012345678901234)
    output = tmp_path / ("exported case" + suffix)

    warnings = getattr(sim, method_name)(output)

    assert warnings == []
    if suffix == ".py":
        source = output.read_text(encoding="utf-8")
        ast.parse(source)
    exported = gd.load(output, type="optimization")
    assert len(exported.Bus) == len(sim.Bus)
    assert len(exported.Gen) == len(sim.Gen)
    assert len(exported.Load) == len(sim.Load)
    assert len(exported.Link) == len(sim.Link)
    assert exported.optimization.get_generator_cost_curve(1) == (
        sim.optimization.get_generator_cost_curve(1)
    )
    assert exported.Bus[3].voltage == pytest.approx(1.012345678901234, abs=1e-14)
    exported.powerflow()


@pytest.mark.parametrize(
    ("method_name", "suffix"),
    [("save_pypower_case", ".py"), ("save_matpower_case", ".m")],
)
def test_case_export_preserves_matpower_operating_fields(tmp_path, method_name, suffix):
    source = CASE9_FILE.read_text(encoding="utf-8")
    source = source.replace(
        "1    3    0    0    0    0    1    1    0    345    1    1.1    0.9;",
        "1    3    0    0    0    0    7    1    0    345    3    1.1    0.9;",
        1,
    )
    source = source.replace(
        "1    4    0    0.0576    0    250    250    250    0    0    1    -360    360;",
        "1    4    0    0.0576    0    250    275    300    1    12    1    -20    30;",
        1,
    )
    source = source.replace(
        "1    0    0    300    -300    1    100    1    250    10    0    0    0    0    0    0    0    0    0    0    0;",
        "1    0    0    300    -300    1    100    1    250    10    10    200    -50    50    -40    60    5    10    20    2    0.5;",
        1,
    )
    input_file = tmp_path / "case9.m"
    input_file.write_text(source, encoding="utf-8")
    sim = gd.load(input_file, type="optimization")
    output_file = tmp_path / ("fidelity" + suffix)

    assert getattr(sim, method_name)(output_file) == []
    output = output_file.read_text(encoding="utf-8")
    bus = _exported_matrix(output, suffix, "bus")
    gen = _exported_matrix(output, suffix, "gen")
    branch = _exported_matrix(output, suffix, "branch")

    assert bus[0][6] == 7
    assert bus[0][10] == 3
    assert len(gen[0]) == 21
    assert gen[0][6] == 100
    assert gen[0][10:21] == [10, 200, -50, 50, -40, 60, 5, 10, 20, 2, 0.5]
    assert branch[0][5:10] == [250, 275, 300, 1, 12]
    assert branch[0][11:13] == [-20, 30]
    assert all(row[11:13] == [0, 0] for row in branch[1:])


def test_generator_cost_curves_can_be_edited_and_exported(tmp_path):
    sim = gd.load(CASE9_FILE, type="optimization")
    optimization = sim.optimization
    gen = sim.Gen[0]
    optimization.set_generator_cost_curve(
        gen.uid, 1, [0.0, 10.0, 250.0, 1000.0], startup_cost=40.0)
    optimization.set_generator_cost_curve(
        gen.uid, 2, [0.01, 2.0], reactive=True, shutdown_cost=8.0)
    expected = {
        "model": 1,
        "startup_cost": 40.0,
        "shutdown_cost": 0.0,
        "coefficients": [0.0, 10.0, 250.0, 1000.0],
    }
    expected_reactive = {
        "model": 2,
        "startup_cost": 0.0,
        "shutdown_cost": 8.0,
        "coefficients": [0.01, 2.0],
    }
    assert optimization.get_generator_cost_curve(gen.uid) == expected
    assert optimization.get_generator_cost_curve(gen.uid, reactive=True) == (
        expected_reactive
    )

    output = tmp_path / "costed_case.py"
    warnings = sim.save_pypower_case(output)
    assert len(warnings) == 2
    exported = gd.load(output, type="optimization")
    assert exported.optimization.get_generator_cost_curve(gen.uid) == expected
    assert exported.optimization.get_generator_cost_curve(gen.uid, reactive=True) == (
        expected_reactive
    )


def test_dc_opf_returns_diagnostics_and_applies_dispatch():
    sim = gd.load(CASE9_FILE, type="optimization")

    result = sim.optimization.opf()

    assert result["success"] is True
    assert result["status"] == "optimal"
    assert result["applied"] is True
    assert math.isfinite(result["objective_value"])
    assert result["solver"] == "native"


def test_pypower_export_reports_write_failures(tmp_path):
    sim = gd.load(PFLOW_FILE)

    with pytest.raises(gd.ExecutionError, match="PYPOWER export failed"):
        sim.save_pypower_case(tmp_path / "missing" / "case.py")


def test_powerflow_result_files_from_python(tmp_path):
    sim = gd.load(PFLOW_FILE)
    sim.powerflow()
    csv_path = tmp_path / "powerflow.csv"
    xml_path = tmp_path / "powerflow.xml"

    sim.save_powerflow_csv(csv_path)
    sim.save_powerflow_xml(xml_path)

    csv_output = csv_path.read_text(encoding="utf-8")
    assert '"voltage(pu)"' in csv_output
    assert '"Qgen(MVAr)"' in csv_output
    assert "<PowerFlow>" in xml_path.read_text(encoding="utf-8")
    with pytest.raises(gd.ExecutionError):
        sim.save_powerflow_csv(tmp_path / "missing" / "powerflow.csv")


def test_pypower_export_runs_with_pypower_when_installed(tmp_path):
    api = pytest.importorskip("pypower.api")
    sim = gd.load(CASE9_FILE, type="optimization")
    output = tmp_path / "case_generated.py"
    assert sim.save_pypower_case(output) == []

    spec = importlib.util.spec_from_file_location("case_generated", output)
    assert spec is not None and spec.loader is not None
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    case = module.case_generated()
    results, success = api.runpf(case, api.ppoption(VERBOSE=0, OUT_ALL=0))

    assert success
    assert results["bus"].shape[0] == len(sim.Bus)
    opf_results = api.runopf(copy.deepcopy(case), api.ppoption(VERBOSE=0, OUT_ALL=0))
    assert opf_results["success"]
    assert opf_results["gencost"].shape[0] == len(sim.Gen)


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
    assert {"name", "bus1", "bus2", "p1", "q1", "p2", "q2", "loss"} <= set(sim.Link.as_dicts()[0])


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
