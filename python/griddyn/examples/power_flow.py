"""Load a MATPOWER case, solve a power flow, and inspect bus results."""

import griddyn as gd

from ._data import example_file


def main() -> None:
    sim = gd.load(example_file("case9.m"))
    sim.PFlow.run()

    buses = sim.Bus.as_dicts()
    lowest_voltage = min(buses, key=lambda bus: bus["v"])
    highest_voltage = max(buses, key=lambda bus: bus["v"])

    print(f"Solved {len(buses)} buses.")
    print(
        f"Voltage range: {lowest_voltage['name']}={lowest_voltage['v']:.4f} pu to "
        f"{highest_voltage['name']}={highest_voltage['v']:.4f} pu"
    )
    print("Bus voltages:")
    for bus in buses:
        print(f"  {bus['name']}: {bus['v']:.4f} pu at {bus['a']:.4f} rad")


if __name__ == "__main__":
    main()
