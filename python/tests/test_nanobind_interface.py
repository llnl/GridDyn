import ast
import copy
import importlib.util
import json
import math
from pathlib import Path

import pytest

import griddyn as gd

REPO_ROOT = Path(__file__).resolve().parents[2]
PFLOW_FILE = REPO_ROOT / "test" / "test_files" / "pFlow_tests" / "two_bus_example.xml"
CPF_FILE = REPO_ROOT / "test" / "test_files" / "pFlow_tests" / "continuation_two_bus.xml"
CPF_Q_LIMIT_FILE = REPO_ROOT / "test" / "test_files" / "pFlow_tests" / "continuation_q_limits.xml"
RAW_INPUTS = REPO_ROOT / "test" / "test_files" / "input_tests"
CASE9_FILE = REPO_ROOT / "test" / "test_files" / "matlab_test_files" / "case9.m"
DYNAMIC_FILE = REPO_ROOT / "test" / "test_files" / "pFlow_tests" / "two_bus_dynamic_example.xml"
AREA_FILE = REPO_ROOT / "test" / "test_files" / "area_tests" / "area_test1.xml"
RELAY_FILE = REPO_ROOT / "test" / "test_files" / "relay_tests" / "test_relay_comms.xml"
RECORDER_FILE = REPO_ROOT / "python" / "griddyn" / "examples" / "data" / "two_bus_dynamic.xml"
HDF5_RECORDER_FILE = REPO_ROOT / "test" / "test_files" / "recorder_tests" / "recorder_hdf5.xml"
HDF5_SIGNATURE = b"\x89HDF\r\n\x1a\n"


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
    assert gd.Generator is not None
    assert gd.GeneratorCollection is not None
    assert gd.Model is not None
    assert gd.Recorder is not None
    assert gd.RecorderCollection is not None
    assert gd.PowerFlowRoutine is not None
    assert gd.TimeDomainRoutine is not None
    assert issubclass(gd.InvalidObjectError, gd.GridDynError)
    assert issubclass(gd.InvalidParameterError, gd.GridDynError)
    assert issubclass(gd.FileLoadError, gd.GridDynError)
    assert issubclass(gd.SolveError, gd.GridDynError)
    assert issubclass(gd.ExecutionError, gd.GridDynError)
    assert callable(gd.load)


def test_direct_actions_report_error_categories():
    sim = gd.load(CPF_FILE)
    with pytest.raises(gd.InvalidParameterError, match="action 'badverb'"):
        sim.execute("badverb")
    with pytest.raises(gd.InvalidObjectError, match="missing:voltage"):
        sim.execute("set missing:voltage 1.02")
    with pytest.raises(gd.InvalidParameterError, match="action 'continuation'.*load scale"):
        sim.execute("continuation")
    with pytest.raises(gd.ExecutionError, match="unsupported command 'print'"):
        sim.execute("print name stdout")
    assert sim.execute("check powerflow .") != 0


def test_script_errors_stop_at_the_failing_command():
    sim = gd.load(DYNAMIC_FILE)
    sim.set("action", "set name before; badverb; set name after")
    with pytest.raises(gd.InvalidParameterError, match="action 'badverb'"):
        sim.time_domain.run()
    assert sim.get_string("name") == "before"

    sim.set("action", "check powerflow .; set name finished")
    with pytest.raises(gd.SolveError, match="action 'check powerflow .' failed: status"):
        sim.time_domain.run_until(0.2)
    assert sim.get_string("name") == "after"
    sim.time_domain.run()
    assert sim.get_string("name") == "finished"


def test_continuation_reports_nose_and_exact_target_without_changing_simulation():
    sim = gd.load(CPF_FILE)
    result = sim.power_flow.continuation(load_scale=6.0)
    assert result["success"] is True
    assert result["status"] == "nose_found"
    assert result["message"]
    assert result["control_policy"] == "fixed"
    assert result["target_lambda"] is None
    assert result["nose_lambda"] == pytest.approx(0.8, abs=1e-5)
    assert result["limiting_lambda"] == result["last_lambda"] == result["nose_lambda"]
    assert result["q_limit_events"] == []

    result = sim.power_flow.continuation(load_scale=6.0, stop_at=1.0)
    assert result["success"] is False
    assert result["status"] == "nose_before_target"
    assert result["target_lambda"] == 1.0
    assert result["limiting_lambda"] == result["nose_lambda"] == pytest.approx(0.8, abs=1e-5)

    sim.loads["demand_load"].set("q", 0.1)
    sim.power_flow.run()
    base_voltage = sim.buses["demand"].voltage
    result = sim.power_flow.continuation(
        load_targets={"demand_load": {"p_mw": 120.0}},
        stop_at=0.5,
        monitor_buses=["demand", 10],
    )
    assert result["success"] is True
    assert result["status"] == "target_reached"
    assert result["limiting_lambda"] is None
    assert result["nose_lambda"] is None
    assert result["target_lambda"] == result["last_lambda"] == result["lambda_values"][-1] == 0.5
    assert list(result["bus_voltages"]) == list(result["bus_angles"]) == [20, 10]
    # At lambda=0.5 the demand is P=0.7 and Q=0.1 p.u. on a line with X=0.5 p.u.
    expected_voltage = math.sqrt((0.9 + math.sqrt(0.31)) / 2)
    assert result["bus_voltages"][20][-1] == pytest.approx(expected_voltage)
    assert result["bus_angles"][20][-1] == pytest.approx(-math.asin(0.35 / expected_voltage))
    assert result["bus_voltages"][10][-1] == 1.0
    assert max(result["residual_norms"]) <= 1e-8
    assert sim.buses["demand"].voltage == base_voltage


def test_continuation_reports_generator_q_limit_and_resumes():
    sim = gd.load(CPF_Q_LIMIT_FILE)
    result = sim.power_flow.continuation(
        load_targets={"reactive_load": {"q_mvar": 110.0}},
        stop_at=0.6,
        monitor_buses=[20],
        enforce_q_limits=True,
    )
    assert result["status"] == "target_reached"
    assert result["control_policy"] == "q_limits"
    (event,) = result["q_limit_events"]
    assert event["bus_id"] == 20
    assert event["generator"] == "regulator"
    assert event["bound"] == "upper"
    assert event["lambda"] == pytest.approx(0.15, abs=2e-7)
    assert event["lambda"] in result["lambda_values"]
    assert event["q_mvar"] == pytest.approx(25.0)
    assert result["lambda_values"][-1] == 0.6
    assert result["bus_voltages"][20][-1] == pytest.approx((1 + math.sqrt(0.1)) / 2)


def test_continuation_distinguishes_q_limit_turn_from_unmet_target():
    sim = gd.load(CPF_Q_LIMIT_FILE)
    sim.buses["regulated"].set("voltage", 0.4)
    sim.generators["regulator"].set("qmin", -0.9)

    for stop_at, success in [("nose", True), (1.0, False)]:
        result = sim.power_flow.continuation(
            load_scale=11.0, stop_at=stop_at, enforce_q_limits=True
        )
        assert result["success"] is success
        assert result["status"] == "q_limit_turn"
        assert result["nose_lambda"] is None
        assert result["limiting_lambda"] == result["last_lambda"] == pytest.approx(0.63, abs=2e-7)
        assert result["limiting_lambda"] == result["q_limit_events"][0]["lambda"]


def test_continuation_omits_q_limit_event_when_switched_base_does_not_solve():
    sim = gd.load(CPF_Q_LIMIT_FILE)
    # With Qgen capped at 0.25 p.u., net Q demand is 0.75 p.u. The line can supply at most 0.5.
    sim.loads["reactive_load"].set("q", 1.0)
    result = sim.power_flow.continuation(load_scale=1.5, stop_at=0.0, enforce_q_limits=True)
    assert result["success"] is False
    assert result["status"] == "base_solve_failed"
    assert result["last_lambda"] is None
    assert result["limiting_lambda"] is None
    assert result["lambda_values"] == []
    assert result["q_limit_events"] == []


@pytest.mark.parametrize("solver", ["dense", "sparse"])
def test_continuation_matches_power_flow_for_a_mixed_remote_control_group(solver):
    sim = gd.load(RAW_INPUTS / "raw_remote_voltage_local_group.raw")
    sim.set("flags", solver)
    sim.generators["REMOTE_Gen_1"].set("qmax", 0.24)
    sim.generators["GEN2_Gen_1"].set("qmax", 0.65)
    sim.power_flow.run()
    load = sim.loads["REMOTE_load_1"]
    base_reactive_demand = load.get("q")
    result = sim.power_flow.continuation(
        load_targets={load.name: {"q_mvar": 60}},
        enforce_q_limits=True,
        stop_at=1.0,
        monitor_buses=[2, 3, 4],
    )

    assert result["status"] == "target_reached"
    assert result["bus_voltages"][2] == pytest.approx([1.04] * len(result["lambda_values"]))
    events = result["q_limit_events"]
    assert [
        (event["generator"], event["bus_id"], event["regulated_bus_id"]) for event in events
    ] == [
        ("REMOTE_Gen_1", 2, 2),
        ("GEN2_Gen_1", 4, 2),
    ]
    assert [event["q_mvar"] for event in events] == pytest.approx([24, 65])
    assert 0 < events[0]["lambda"] < events[1]["lambda"] < 1
    assert load.get("q") == base_reactive_demand

    load.set("q", 60, "Mvar")
    sim.power_flow.run()
    assert -sim.generators["REMOTE_Gen_1"].get("q") == pytest.approx(0.24)
    assert -sim.generators["GEN2_Gen_1"].get("q") == pytest.approx(0.65)
    for name in ["REMOTE", "GEN1", "GEN2"]:
        bus = sim.buses[name]
        assert result["bus_voltages"][bus.uid][-1] == pytest.approx(bus.voltage, abs=1e-7)


def test_continuation_releases_remote_voltage_after_the_last_generator_binds():
    sim = gd.load(RAW_INPUTS / "raw_remote_voltage_group.raw")
    limits = {"GEN1_Gen_1": 0.46, "GEN1_Gen_2": 0.53, "GEN2_Gen_1": 0.96}
    for name, qmax in limits.items():
        sim.generators[name].set("qmax", qmax)
    result = sim.power_flow.continuation(
        load_targets={"REMOTE_load_1": {"q_mvar": 90}},
        enforce_q_limits=True,
        stop_at=1.0,
        monitor_buses=["REMOTE"],
    )

    assert result["status"] == "target_reached"
    events = result["q_limit_events"]
    assert {event["generator"]: event["q_mvar"] for event in events} == pytest.approx(
        {name: 100 * qmax for name, qmax in limits.items()}
    )
    assert all(event["regulated_bus_id"] == 2 and event["bound"] == "upper" for event in events)
    assert events[0]["lambda"] < events[1]["lambda"] < events[2]["lambda"] < 1
    last_event = result["lambda_values"].index(events[-1]["lambda"])
    voltages = result["bus_voltages"][2]
    assert voltages[: last_event + 1] == pytest.approx([1.04] * (last_event + 1), abs=1e-7)
    assert voltages[-1] < 1.03
    sim.loads["REMOTE_load_1"].set("q", 90, "Mvar")
    sim.power_flow.run()
    assert voltages[-1] == pytest.approx(sim.buses["REMOTE"].voltage, abs=1e-7)


def test_continuation_keeps_the_angle_reference_at_a_remote_q_limit():
    sim = gd.load(RAW_INPUTS / "raw_remote_voltage_swing_terminal.raw")
    sim.generators["SLACK_REMOTE_Gen_1"].set("qmax", 0.30)
    result = sim.power_flow.continuation(
        load_targets={"REMOTE_load_1": {"q_mvar": 40}},
        enforce_q_limits=True,
        stop_at=1.0,
        monitor_buses=[1, 2],
    )

    assert result["status"] == "q_limit_turn"
    assert result["success"] is False
    (event,) = result["q_limit_events"]
    assert (event["bus_id"], event["regulated_bus_id"]) == (1, 2)
    assert event["q_mvar"] == pytest.approx(30)
    assert result["limiting_lambda"] == event["lambda"]
    assert result["bus_voltages"][2][-1] == pytest.approx(1.04, abs=1e-6)
    assert max(abs(angle) for angle in result["bus_angles"][1]) < 1e-10


def test_power_flow_solution_checkpoint(tmp_path):
    sim = gd.load(PFLOW_FILE)
    sim.power_flow.run()
    bus = sim.buses[1]
    original_voltage = bus.get("voltage")
    original_angle = bus.get("angle")
    checkpoint = tmp_path / "powerflow.json"
    sim.save_power_flow_checkpoint(checkpoint)
    saved = json.loads(checkpoint.read_text())
    assert saved["format"] == "griddyn-powerflow-checkpoint"
    assert saved["scope"] == "solution"

    load = sim.loads[1]
    load.set("p", 0.8)
    sim.power_flow.run()
    assert bus.get("voltage") != pytest.approx(original_voltage)
    sim.load_power_flow_checkpoint(checkpoint)
    assert bus.get("voltage") == pytest.approx(original_voltage)
    assert bus.get("angle") == pytest.approx(original_angle)
    assert load.get("p") == pytest.approx(0.8)


def test_python_api_uses_one_pythonic_name_per_interface():
    sim = gd.Simulation()

    collection_names = ("buses", "generators", "loads", "links", "areas", "sensors", "relays")
    assert all(hasattr(sim, name) for name in collection_names)
    assert not any(
        hasattr(sim, name)
        for name in (
            "Bus",
            "bus",
            "Generator",
            "generator",
            "Gen",
            "gen",
            "gens",
            "Load",
            "Link",
            "link",
            "Area",
            "area",
            "Sensor",
            "sensor",
            "Relay",
            "relay",
            "PFlow",
            "pflow",
            "TDS",
            "tds",
        )
    )
    assert not hasattr(gd, "Gen")
    assert not hasattr(gd, "GenCollection")

    assert callable(sim.load)
    assert not hasattr(sim, "load_file")
    assert callable(sim.write_file)
    assert not any(
        hasattr(sim, name)
        for name in (
            "save_pypower_case",
            "save_matpower_case",
            "save_powerflow_csv",
            "save_powerflow_xml",
        )
    )
    assert not hasattr(sim, "run_until")
    assert not hasattr(sim, "run_to")
    assert not any(hasattr(sim, name) for name in ("initialize", "run", "step"))
    assert callable(sim.execute)
    assert callable(sim.reset)

    time_domain = sim.time_domain
    assert callable(time_domain.initialize)
    assert callable(time_domain.run)
    assert callable(time_domain.run_until)
    assert callable(time_domain.step)
    assert not hasattr(time_domain, "execute")
    assert not hasattr(time_domain, "reset")
    assert not hasattr(time_domain, "time")
    assert not hasattr(time_domain, "init")
    assert not hasattr(time_domain, "run_to")

    for name in collection_names:
        collection = getattr(sim, name)
        assert callable(collection.as_dicts)
        assert callable(collection.as_dataframe)
        assert not hasattr(collection, "to_list")
        assert not hasattr(collection, "to_dataframe")
    assert callable(gd.Optimization.opf)
    assert not hasattr(gd.Optimization, "solve_opf")


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


def test_load_uses_pathlike_protocol(tmp_path):
    sim = gd.Simulation(name="loader")
    missing_file = tmp_path / "does-not-exist.grid"

    with pytest.raises(gd.FileLoadError):
        sim.load(Path(missing_file))


def test_load_rejects_unknown_or_missing_format(tmp_path):
    unknown_format = tmp_path / "network.unknown"
    no_extension = tmp_path / "network"
    unknown_format.write_text("ignored", encoding="utf-8")
    no_extension.write_text("ignored", encoding="utf-8")

    with pytest.raises(gd.FileLoadError, match="unsupported input format"):
        gd.load(unknown_format)
    with pytest.raises(gd.FileLoadError, match="no extension"):
        gd.load(no_extension)


def test_reset_reloads_command_line_input_and_invalidates_old_handles():
    sim = gd.Simulation()
    sim.load_from_args(["--input", str(PFLOW_FILE)])
    original_bus = sim.buses["bus1"]

    sim.reset()

    assert sim.buses["bus1"].name == "bus1"
    with pytest.raises(gd.InvalidObjectError, match="previous simulation"):
        _ = original_bus.name


def test_command_line_load_rejects_invalid_arguments():
    sim = gd.Simulation()
    with pytest.raises(gd.ExecutionError, match="loading failed"):
        sim.load_from_args(["--not-a-grid-option"])


def test_recorder_collection_returns_numeric_time_samples(monkeypatch, tmp_path):
    monkeypatch.chdir(tmp_path)
    sim = gd.load(RECORDER_FILE)
    sim.set("recorddirectory", str(tmp_path))
    sim.time_domain.initialize()
    sim.time_domain.run_until(0.1)

    assert sim.recorders.names == ["signals"]
    recorder = sim.recorders["signals"]
    samples = recorder.as_dicts()
    assert samples
    assert isinstance(samples[0]["time"], float)


def test_recorder_samples_keep_duplicate_signal_columns(monkeypatch, tmp_path):
    monkeypatch.chdir(tmp_path)
    source = RECORDER_FILE.read_text(encoding="utf-8")
    duplicate_case = tmp_path / "duplicate_signals.xml"
    duplicate_case.write_text(
        source.replace(
            "bus1:voltage, bus1::gen1:p, bus1:linkreal",
            "bus1:voltage, bus1:voltage",
        ),
        encoding="utf-8",
    )
    sim = gd.load(duplicate_case)
    sim.set("recorddirectory", str(tmp_path))
    sim.time_domain.initialize()
    sim.time_domain.run_until(0.1)

    row = sim.recorders["signals"].as_dicts()[0]
    assert row["bus1:voltage"] == pytest.approx(row["bus1:voltage_2"])


@pytest.mark.parametrize("extension", [".csv", ".dat", ".h5", ".hdf5"])
def test_python_can_configure_flush_and_export_recorders(tmp_path, extension):
    sim = gd.load(HDF5_RECORDER_FILE)
    recorder = sim.recorders["bus_signals"]
    output = tmp_path / ("signals" + extension)
    assert recorder.set("directory", tmp_path) is recorder
    assert recorder.set("file", output) is recorder
    recorder.set("autosave", 0)
    recorder.set("compression", 2)
    recorder.set("period", 2.0)
    assert sim.execute("iterate 1 2") == 0
    recorder.flush()
    captured = output.read_bytes()
    assert captured
    if extension in (".h5", ".hdf5"):
        assert captured.startswith(HDF5_SIGNATURE)
    elif extension == ".csv":
        assert b"magnitude" in captured and b"phase" in captured
    else:
        assert not captured.startswith(HDF5_SIGNATURE)
    recorder.save_file()
    assert output.read_bytes() == captured
    snapshot = tmp_path / "snapshot.h5"
    recorder.save_file(snapshot)
    assert snapshot.read_bytes().startswith(HDF5_SIGNATURE)
    assert output.read_bytes() == captured


def test_recorder_write_errors_are_typed_and_keep_buffered_samples(tmp_path):
    sim = gd.load(HDF5_RECORDER_FILE)
    recorder = sim.recorders[0]
    recorder.set("autosave", 0)
    assert sim.execute("iterate 1 2") == 0
    samples = recorder.as_dicts()
    blocked = tmp_path / "blocked.h5"
    blocked.mkdir()
    recorder.set("file", blocked)
    with pytest.raises(gd.FileLoadError):
        recorder.flush()
    assert recorder.as_dicts() == samples
    blocked.rmdir()
    recorder.flush()
    assert blocked.read_bytes().startswith(HDF5_SIGNATURE)
    with pytest.raises(gd.InvalidParameterError):
        recorder.set("compression", 10)
    with pytest.raises(gd.InvalidParameterError):
        recorder.set("unknown_setting", "value")
    with pytest.raises(TypeError):
        recorder.set("period", [])


def test_recorder_reset_can_start_an_independent_hdf5_recording(tmp_path):
    sim = gd.load(HDF5_RECORDER_FILE)
    sim.set("recorddirectory", str(tmp_path))
    recorder = sim.recorders["bus_signals"]
    assert sim.execute("iterate 1 2") == 0
    recorder.flush()
    original = (tmp_path / "samples.h5").read_bytes()

    recorder.reset()
    recorder.set("file", tmp_path / "branch.h5")
    assert len(recorder) == 0
    assert sim.execute("iterate 1 4") == 0
    recorder.flush()

    assert [row["time"] for row in recorder.as_dicts()] == [4.0]
    assert (tmp_path / "branch.h5").read_bytes().startswith(HDF5_SIGNATURE)
    assert (tmp_path / "samples.h5").read_bytes() == original


def test_recorder_operations_reject_handles_from_before_simulation_reset(tmp_path):
    sim = gd.Simulation()
    sim.load_from_args(["--input", str(HDF5_RECORDER_FILE)])
    sim.set("recorddirectory", str(tmp_path))
    recorder = sim.recorders[0]
    sim.reset()
    for operation in (
        lambda: recorder.set("compression", 1),
        lambda: recorder.set("file", tmp_path / "stale.h5"),
        recorder.flush,
        lambda: recorder.save_file(tmp_path / "stale.h5"),
        recorder.reset,
    ):
        with pytest.raises(gd.InvalidObjectError, match="previous simulation"):
            operation()
    assert not (tmp_path / "stale.h5").exists()


def test_load_returns_self_for_chaining():
    sim = gd.Simulation(name="two-bus")

    loaded = sim.load(PFLOW_FILE)

    assert isinstance(loaded, gd.Simulation)
    assert loaded.name == "2bus_test"
    loaded.power_flow.run()


def test_simulation_from_file_loads_powerflow_case():
    sim = gd.Simulation.from_file(PFLOW_FILE)

    assert sim.name == "2bus_test"
    assert sim.optimization is None
    sim.power_flow.run()
    assert sim.time == 0.0


def test_optimization_has_its_own_interface():
    sim = gd.load(CASE9_FILE, type="optimization")

    assert isinstance(sim.optimization, gd.Optimization)
    assert not hasattr(sim, "opf")
    assert not hasattr(sim, "set_generator_cost_curve")


@pytest.mark.parametrize(
    "suffix",
    [".py", ".m"],
)
def test_case_export_round_trips_from_python(tmp_path, suffix):
    sim = gd.load(CASE9_FILE, type="optimization")
    sim.buses[3].set("voltage", 1.012345678901234)
    output = tmp_path / ("exported case" + suffix)

    warnings = sim.write_file(output)

    assert warnings == []
    if suffix == ".py":
        source = output.read_text(encoding="utf-8")
        ast.parse(source)
    exported = gd.load(output, type="optimization")
    assert len(exported.buses) == len(sim.buses)
    assert len(exported.generators) == len(sim.generators)
    assert len(exported.loads) == len(sim.loads)
    assert len(exported.links) == len(sim.links)
    assert exported.optimization.get_generator_cost_curve(1) == (
        sim.optimization.get_generator_cost_curve(1)
    )
    assert exported.buses[3].voltage == pytest.approx(1.012345678901234, abs=1e-14)
    exported.power_flow.run()


@pytest.mark.parametrize(
    "suffix",
    [".py", ".m"],
)
def test_case_export_preserves_matpower_operating_fields(tmp_path, suffix):
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

    assert sim.write_file(output_file) == []
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
    gen = sim.generators[0]
    optimization.set_generator_cost_curve(gen.uid, 1, [0.0, 10.0, 250.0, 1000.0], startup_cost=40.0)
    optimization.set_generator_cost_curve(gen.uid, 2, [0.01, 2.0], reactive=True, shutdown_cost=8.0)
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
    assert optimization.get_generator_cost_curve(gen.uid, reactive=True) == (expected_reactive)

    output = tmp_path / "costed_case.py"
    warnings = sim.write_file(output, type="pypower")
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
        sim.write_file(tmp_path / "missing" / "case.py")


def test_powerflow_result_files_from_python(tmp_path):
    sim = gd.load(PFLOW_FILE)
    sim.power_flow.run()
    csv_path = tmp_path / "powerflow.csv"
    xml_path = tmp_path / "powerflow.xml"

    sim.write_file(csv_path)
    sim.write_file(xml_path)

    csv_output = csv_path.read_text(encoding="utf-8")
    assert '"voltage(pu)"' in csv_output
    assert '"Qgen(MVAr)"' in csv_output
    assert "<PowerFlow>" in xml_path.read_text(encoding="utf-8")
    with pytest.raises(gd.ExecutionError):
        sim.write_file(tmp_path / "missing" / "powerflow.csv")


def test_pypower_export_runs_with_pypower_when_installed(tmp_path):
    api = pytest.importorskip("pypower.api")
    sim = gd.load(CASE9_FILE, type="optimization")
    output = tmp_path / "case_generated.py"
    assert sim.write_file(output) == []

    spec = importlib.util.spec_from_file_location("case_generated", output)
    assert spec is not None and spec.loader is not None
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    case = module.case_generated()
    results, success = api.runpf(case, api.ppoption(VERBOSE=0, OUT_ALL=0))

    assert success
    assert results["bus"].shape[0] == len(sim.buses)
    opf_results = api.runopf(copy.deepcopy(case), api.ppoption(VERBOSE=0, OUT_ALL=0))
    assert opf_results["success"]
    assert opf_results["gencost"].shape[0] == len(sim.generators)


def test_andes_style_load_and_powerflow():
    sim = gd.load(PFLOW_FILE)

    sim.power_flow.run()

    assert sim.name == "2bus_test"
    assert sim.power_flow is not None


def test_powerflow_collections_expose_results():
    sim = gd.load(PFLOW_FILE)

    sim.power_flow.run()

    assert len(sim.buses) == 2
    assert sim.buses.names == ["bus1", "bus2"]
    assert sim.buses[0].name == "bus1"
    assert sim.buses["bus1"].voltage == pytest.approx(1.05)
    assert math.isfinite(sim.buses["bus2"].angle)
    assert {"name", "voltage", "angle", "frequency", "p_gen", "p_load", "p_link"} <= set(
        sim.buses.as_dicts()[0]
    )

    assert len(sim.generators) == 1
    assert sim.generators.names == ["gen1"]
    assert isinstance(sim.generators[0], gd.Generator)
    assert sim.generators["gen1"].bus == "bus1"
    assert math.isfinite(sim.generators[0].p)
    assert {"name", "bus", "p", "q", "pset"} <= set(sim.generators.as_dicts()[0])

    assert len(sim.loads) == 2
    assert sim.loads.names == ["load1", "load2"]
    assert sim.loads["load2"].bus == "bus2"
    assert sim.loads["load1"].p == pytest.approx(1.15)
    assert {"name", "bus", "p", "q"} <= set(sim.loads.as_dicts()[0])

    assert len(sim.links) == 1
    assert sim.links.names == ["bus1_to_bus2"]
    assert sim.links["bus1_to_bus2"].bus1 == "bus1"
    assert sim.links[0].bus2 == "bus2"
    assert math.isfinite(sim.links[0].p1)
    assert {"name", "bus1", "bus2", "p1", "q1", "p2", "q2", "loss"} <= set(sim.links.as_dicts()[0])


def test_model_collections_are_available():
    sim = gd.load(PFLOW_FILE)

    assert isinstance(sim.buses[0], gd.Bus)
    assert isinstance(sim.loads[0], gd.Load)
    assert isinstance(sim.links[0], gd.Link)
    assert isinstance(sim.areas, gd.AreaCollection)
    assert isinstance(sim.relays, gd.RelayCollection)
    assert isinstance(sim.sensors, gd.SensorCollection)

    assert len(sim.areas) == 0
    assert len(sim.relays) == 0
    assert len(sim.sensors) == 0
    assert sim.areas.names == []
    assert sim.relays.as_dicts() == []
    assert sim.sensors.as_dicts() == []


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

    bus = sim.buses["bus1"]
    assert bus.set("period", 0.3) is bus
    assert bus.get("period") == pytest.approx(0.3)
    assert bus.set("voltage", 1.04).get("voltage") == pytest.approx(1.04)
    assert bus.set("description", "python bus").get_string("description") == "python bus"

    gen = sim.generators["gen1"]
    assert gen.set("period", 0.35) is gen
    assert gen.get("period") == pytest.approx(0.35)
    assert gen.set("pset", 0.8).get("pset") == pytest.approx(0.8)
    assert gen.set("description", "python gen").get_string("description") == "python gen"

    load = sim.loads["load1"]
    assert load.set("period", 0.4) is load
    assert load.get("period") == pytest.approx(0.4)
    assert load.set("p", 1.2).get("p") == pytest.approx(1.2)
    assert load.set("description", "python load").get_string("description") == "python load"

    link = sim.links["bus1_to_bus2"]
    assert link.set("period", 0.45) is link
    assert link.get("period") == pytest.approx(0.45)
    assert link.set("rating", 2.0).get("rating") == pytest.approx(2.0)
    assert link.set("description", "python link").get_string("description") == "python link"


def test_area_relay_and_sensor_support_griddyn_get_and_set():
    area_sim = gd.load(AREA_FILE)
    area = area_sim.areas["testArea"]

    assert area.set("period", 0.5) is area
    assert area.get("period") == pytest.approx(0.5)
    assert area.set("description", "python area").get_string("description") == "python area"

    relay_sim = gd.load(RELAY_FILE)
    relay = relay_sim.relays["load4control"]
    sensor = relay_sim.sensors["sensor1"]

    assert relay.set("period", 0.55) is relay
    assert relay.get("period") == pytest.approx(0.55)
    assert relay.set("description", "python relay").get_string("description") == "python relay"

    assert sensor.set("period", 0.6) is sensor
    assert sensor.get("period") == pytest.approx(0.6)
    assert sensor.set("description", "python sensor").get_string("description") == "python sensor"


def test_simulation_can_run_dynamic_file(monkeypatch, tmp_path):
    monkeypatch.chdir(tmp_path)
    sim = gd.Simulation.from_file(DYNAMIC_FILE)

    sim.power_flow.run()
    sim.time_domain.initialize()
    final_time = sim.time_domain.run_until(0.1)

    assert final_time >= 0.1
    assert sim.time >= 0.1


def test_andes_style_time_domain_run(monkeypatch, tmp_path):
    monkeypatch.chdir(tmp_path)
    sim = gd.load(DYNAMIC_FILE)

    sim.power_flow.run()
    sim.time_domain.initialize()
    final_time = sim.time_domain.run_until(0.1)

    assert final_time >= 0.1
    assert sim.time >= 0.1
