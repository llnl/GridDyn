"""Run a dynamic case and read recorder output in memory."""

from tempfile import TemporaryDirectory

import griddyn as gd

from ._data import example_file


def main() -> None:
    with TemporaryDirectory(prefix="griddyn-example-") as output_directory:
        sim = gd.load(example_file("two_bus_dynamic.xml"))

        # Keep the example's configured CSV output inside a temporary folder.
        sim.set("recorddirectory", output_directory)
        sim.time_domain.initialize()
        sim.time_domain.run_until(10.0)

        recorder = sim.recorders["signals"]
        samples = recorder.as_dicts()
        if not samples:
            raise RuntimeError("Recorder produced no samples")

        columns = list(samples[0])
        print(
            f"Recorded {len(samples)} time points from "
            f"{samples[0]['time']:g} to {samples[-1]['time']:g} s."
        )
        print("Recorded columns:", ", ".join(columns))
        print("First sample:", samples[0])
        print("Last sample: ", samples[-1])

        # The recorder and simulation flush their configured output on cleanup.
        del recorder
        del sim


if __name__ == "__main__":
    main()
