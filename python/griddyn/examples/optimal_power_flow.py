"""Run the native DC OPF on a MATPOWER case and print the dispatch."""

import griddyn as gd

from ._data import example_file


def main() -> None:
    sim = gd.load(example_file("case9.m"), type="optimization")
    result = sim.optimization.opf()
    if not result["success"]:
        raise RuntimeError(f"OPF did not solve: {result['status']} ({result['message']})")

    print(f"OPF status: {result['status']}")
    print(f"Objective value: {result['objective_value']:.3f}")
    print("Generator dispatch (GridDyn convention; generation is negative):")
    for generator in sim.Gen.as_dicts():
        print(f"  {generator['name']} at {generator['bus']}: {generator['p']:.4f}")


if __name__ == "__main__":
    main()
