"""Regenerate the checked-in two-bus renewable fault reference with ANDES."""

import argparse
import csv
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[4] / "andes"))
import andes  # noqa: E402
import numpy as np  # noqa: E402


def build_case(include_plant=False, include_wind=False, use_reecb=False):
    system = andes.System(no_output=True, default_config=True)
    system.add("Bus", idx=1, Vn=100, v0=1.0)
    system.add("Bus", idx=2, Vn=100, v0=1.0)
    system.add("Slack", idx=1, bus=1, Sn=100, Vn=100, v0=1.0, p0=0.0)
    system.add("PV", idx=2, bus=2, Sn=100, Vn=100, v0=1.0, p0=0.6)
    system.add("PQ", idx="load", bus=2, Vn=100, p0=0.4, q0=0.1)
    system.add("Line", idx="line", bus1=1, bus2=2, Sn=100, Vn1=100, Vn2=100, r=0.01, x=0.1, b=0.0)
    system.add(
        "REGCA1",
        idx=1,
        bus=2,
        gen=2,
        Sn=100,
        Tg=0.02,
        Tfltr=0.02,
        Iqrmax=999.0,
        Iqrmin=-999.0,
        Lvplsw=0.0,
    )
    if use_reecb:
        system.add(
            "REECB1",
            idx=1,
            reg=1,
            PFFLAG=0,
            VFLAG=0,
            QFLAG=0,
            PQFLAG=0,
            Vref0=0.0,
            Tpord=0.02,
            Imax=1.3,
            QMax=1.0,
            QMin=-1.0,
        )
    else:
        system.add(
            "REECA1",
            idx=1,
            reg=1,
            PFFLAG=0,
            VFLAG=0,
            QFLAG=0,
            PFLAG=0,
            PQFLAG=0,
            Vref0=0.0,
            Iqfrz=0.0,
            Thld=0.0,
            Thld2=0.5,
            Tpord=0.0,
            Imax=1.3,
            QMax=1.0,
            QMin=-1.0,
            Iq1=1.3,
            Iq2=1.3,
            Iq3=1.3,
            Iq4=1.3,
            Ip1=1.3,
            Ip2=1.3,
            Ip3=1.3,
            Ip4=1.3,
        )
    if include_plant:
        system.add(
            "REPCA1", idx=1, ree=1, line="line", VCFlag=0, RefFlag=1, Fflag=0, PLflag=0, Kc=0.0
        )
    if include_wind:
        system.add(
            "WTDTA1", idx=1, ree=1, H=6.0, DAMP=0.0, Htfrac=0.5, Freq1=1.0, Dshaft=1.0, w0=1.0
        )
        system.add("WTARA1", idx=1, rego=1, Ka=1.0, theta0=0.0)
        system.add(
            "WTPTA1",
            idx=1,
            rea=1,
            Kiw=0.1,
            Kpw=0.0,
            Kic=0.1,
            Kpc=0.0,
            Kcc=0.0,
            Tp=0.3,
            thmax=30.0,
            thmin=0.0,
            dthmax=5.0,
            dthmin=-5.0,
        )
        system.add(
            "WTTQA1",
            idx=1,
            rep=1,
            Kip=0.1,
            Kpp=0.0,
            Tp=0.05,
            Twref=30.0,
            Temax=1.2,
            Temin=0.0,
            Tflag=0,
            p1=0.2,
            sp1=0.58,
            p2=0.4,
            sp2=0.72,
            p3=0.6,
            sp3=0.86,
            p4=0.8,
            sp4=1.0,
        )
    system.add("Fault", idx=1, bus=2, tf=0.1, tc=0.2, xf=0.2)
    system.setup()
    return system


def main():
    parser = argparse.ArgumentParser()
    profile = parser.add_mutually_exclusive_group()
    profile.add_argument("--plant", action="store_true", help="include REPCA1 in the model chain")
    profile.add_argument(
        "--wind", action="store_true", help="include the four Type-3 wind components"
    )
    profile.add_argument("--reecb", action="store_true", help="use REECB1 instead of REECA1")
    args = parser.parse_args()
    ss = build_case(args.plant, args.wind, args.reecb)
    if not ss.PFlow.run():
        raise RuntimeError("ANDES power flow failed")
    ss.TDS.config.tf = 0.9
    ss.TDS.config.tstep = 0.005
    ss.TDS.config.fixt = 1
    if not ss.TDS.run() or ss.exit_code != 0:
        raise RuntimeError("ANDES time-domain simulation failed")

    times = ss.dae.ts.t
    columns = {
        "voltage": ss.TDS.get_timeseries(ss.Bus.v).iloc[:, 1].to_numpy(),
        "converter_p": ss.TDS.get_timeseries(ss.REGCA1.Pe).iloc[:, 0].to_numpy(),
        "converter_q": ss.TDS.get_timeseries(ss.REGCA1.Qe).iloc[:, 0].to_numpy(),
        "electrical_ip": ss.TDS.get_timeseries(ss.REECB1.Ipcmd if args.reecb else ss.REECA1.Ipcmd)
        .iloc[:, 0]
        .to_numpy(),
        "electrical_iq": ss.TDS.get_timeseries(ss.REECB1.Iqcmd if args.reecb else ss.REECA1.Iqcmd)
        .iloc[:, 0]
        .to_numpy(),
    }
    if args.wind:
        columns.update(
            {
                "generator_speed": ss.TDS.get_timeseries(ss.WTDTA1.wg).iloc[:, 0].to_numpy(),
                "turbine_speed": ss.TDS.get_timeseries(ss.WTDTA1.wt).iloc[:, 0].to_numpy(),
                "pitch_angle": ss.TDS.get_timeseries(ss.WTARA1.theta).iloc[:, 0].to_numpy(),
                "mechanical_power": ss.TDS.get_timeseries(ss.WTDTA1.Pm).iloc[:, 0].to_numpy(),
            }
        )
    target = Path(__file__).with_name(
        "andes_reecb_reference.csv"
        if args.reecb
        else (
            "andes_wind_reference.csv"
            if args.wind
            else "andes_plant_reference.csv" if args.plant else "andes_reference.csv"
        )
    )
    with target.open("w", newline="", encoding="utf-8") as stream:
        writer = csv.writer(stream, lineterminator="\n")
        writer.writerow(["time", *columns])
        for t in (0.05, 0.15, 0.21, 0.23, 0.25, 0.4, 0.75, 0.9):
            writer.writerow(
                [t, *(float(np.interp(t, times, values)) for values in columns.values())]
            )
    print(f"Wrote {target}")


if __name__ == "__main__":
    main()
