"""Regenerate two-bus ANDES fault trajectories for grid-forming converters."""

import csv
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[4] / "andes"))
import andes  # noqa: E402
import numpy as np  # noqa: E402


def build_case(model_name, load_profile):
    system = andes.System(no_output=True, default_config=True)
    system.add("Bus", idx=1, Vn=100, v0=1.0)
    system.add("Bus", idx=2, Vn=100, v0=1.0)
    system.add("Slack", idx=1, bus=1, Sn=100, Vn=100, v0=1.0, p0=0.0)
    system.add("PV", idx=2, bus=2, Sn=100, Vn=100, v0=1.0, p0=0.6)
    system.add("PQ", idx="load", bus=2, Vn=100, p0=0.4, q0=0.1)
    # Match the GridDyn demand model during dynamics. ANDES initializes
    # both profiles from the same constant-P/Q power-flow load.
    constant_power = load_profile == "power"
    system.PQ.config.p2p = float(constant_power)
    system.PQ.config.p2z = float(not constant_power)
    system.PQ.config.q2q = float(constant_power)
    system.PQ.config.q2z = float(not constant_power)
    system.add("Line", idx="line", bus1=1, bus2=2, Sn=100, Vn1=100, Vn2=100, r=0.01, x=0.1, b=0.0)
    system.add(model_name, idx=1, bus=2, gen=2, Sn=100)
    if model_name == "REGF2":
        system.add("PLL2", idx="pll2a", bus=2)
    system.add("Fault", idx=1, bus=2, tf=0.1, tc=0.2, xf=0.2)
    system.setup()
    return system


def main():
    for load_profile in ("power", "impedance"):
        for model_name in ("REGCV1", "REGCV2", "REGF1", "REGF2", "REGF3"):
            system = build_case(model_name, load_profile)
            if not system.PFlow.run():
                raise RuntimeError(f"{model_name} power flow failed")
            system.TDS.config.tf = 0.9
            # The REGF current loops are fast enough that 5 ms materially changes
            # their fault response; use the same fine reference step for all five.
            system.TDS.config.tstep = 0.0002
            system.TDS.config.fixt = 1
            if not system.TDS.run() or system.exit_code != 0:
                raise RuntimeError(f"{model_name} time-domain simulation failed")
            model = getattr(system, model_name)
            times = system.dae.ts.t
            columns = {
                "voltage": system.TDS.get_timeseries(system.Bus.v).iloc[:, 1].to_numpy(),
                "converter_p": system.TDS.get_timeseries(model.Pe).iloc[:, 0].to_numpy(),
                "converter_q": system.TDS.get_timeseries(model.Qe).iloc[:, 0].to_numpy(),
                "delta": system.TDS.get_timeseries(model.delta).iloc[:, 0].to_numpy(),
            }
            suffix = "" if load_profile == "power" else "_impedance"
            target = Path(__file__).with_name(f"andes_{model_name.lower()}{suffix}_reference.csv")
            with target.open("w", newline="", encoding="utf-8") as stream:
                writer = csv.writer(stream, lineterminator="\n")
                writer.writerow(["time", *columns])
                for time in (0.05, 0.15, 0.21, 0.23, 0.25, 0.4, 0.75, 0.9):
                    writer.writerow(
                        [
                            time,
                            *(float(np.interp(time, times, values)) for values in columns.values()),
                        ]
                    )
            print(f"Wrote {target}")


if __name__ == "__main__":
    main()
