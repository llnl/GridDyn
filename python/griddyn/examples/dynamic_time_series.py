"""Run a dynamic case and read its recorder output as a Python time series."""

import csv
from pathlib import Path
from tempfile import TemporaryDirectory

import griddyn as gd

from ._data import example_file


def read_time_series(path: Path) -> list[dict[str, float]]:
    """Read a GridDyn recorder CSV into plain numeric Python samples."""
    with path.open(newline="", encoding="utf-8") as csv_file:
        rows = csv.DictReader(
            (line for line in csv_file if not line.startswith("#")),
            skipinitialspace=True,
        )
        return [
            {
                name: float(value.strip().removesuffix("s"))
                for name, value in row.items()
                if name is not None and value is not None
            }
            for row in rows
        ]


def main() -> None:
    sim = gd.load(example_file("two_bus_dynamic.xml"))

    # The XML case configures a recorder with a 0.05 second period and a CSV
    # output file. Redirect it to a temporary folder for this run.
    with TemporaryDirectory(prefix="griddyn-example-") as output_directory:
        sim.set("recorddirectory", output_directory)
        sim.TDS.init()
        sim.TDS.run_until(10.0)

        csv_path = Path(output_directory) / "twobusdynout.csv"
        samples = read_time_series(csv_path)
        if not samples:
            raise RuntimeError(f"Recorder produced no rows in {csv_path}")

        # GridDyn writes recorder times with their unit suffix (for example,
        # "0.05s"); read_time_series converts the file into numeric samples.
        columns = list(samples[0])
        print(
            f"Recorded {len(samples)} time points from "
            f"{samples[0]['time']:g} to {samples[-1]['time']:g} s."
        )
        print("Recorded columns:", ", ".join(columns))
        print("First sample:", samples[0])
        print("Last sample: ", samples[-1])


if __name__ == "__main__":
    main()
