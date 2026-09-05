"""A small PYPOWER case used to exercise GridDyn's PYPOWER reader."""

from numpy import array


def case2():
    ppc = {"version": "2"}
    ppc["baseMVA"] = 100.0
    ppc["bus"] = array(
        [
            [1, 3, 0, 0, 0, 0, 1, 1.0, 0, 230, 1, 1.1, 0.9],
            [2, 1, 50, 20, 0, 0, 1, 1.0, 0, 230, 1, 1.1, 0.9],
        ]
    )
    ppc["gen"] = array([[1, 50, 20, 100, -100, 1.0, 100, 1, 150, 0]])
    ppc["branch"] = array([[1, 2, 0.01, 0.1, 0.02, 100, 100, 100, 0, 0, 1, -360, 360]])
    ppc["gencost"] = array([[2, 0, 0, 3, 0.01, 1, 0]])
    return ppc
