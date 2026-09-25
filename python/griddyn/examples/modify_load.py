"""Increase a load, resolve the power flow, and compare the bus voltages."""

import griddyn as gd

from ._data import example_file


def main() -> None:
    sim = gd.load(example_file("case9.m"))
    sim.PFlow.run()

    load = sim.Load[0]
    load_bus = load.bus
    initial_load = load.p
    initial_voltage = sim.Bus[load_bus].voltage
    initial_generation = -sum(bus["p_gen"] for bus in sim.Bus.as_dicts())

    load.set("p", initial_load * 1.10)
    sim.PFlow.run()

    final_voltage = sim.Bus[load_bus].voltage
    final_generation = -sum(bus["p_gen"] for bus in sim.Bus.as_dicts())
    print(f"Changed {load.name} at {load_bus} from {initial_load:.4f} to {load.p:.4f}.")
    print(f"Voltage at {load_bus}: {initial_voltage:.5f} -> {final_voltage:.5f} pu")
    print(f"Total generation: {initial_generation:.5f} -> {final_generation:.5f}")


if __name__ == "__main__":
    main()
