# LLNS Copyright Start
# Copyright (c) 2017, Lawrence Livermore National Security
# This work was performed under the auspices of the U.S. Department
# of Energy by Lawrence Livermore National Laboratory in part under
# Contract W-7405-Eng-48 and in part under Contract DE-AC52-07NA27344.
# Produced at the Lawrence Livermore National Laboratory.
# All rights reserved.
# For details, see the LICENSE file.
# LLNS Copyright End

import griddyn
import unittest


class gridDyn(unittest.TestCase):
    """unittest for gridDy python interface"""

    def setUp(self):
        self.ieee_test_directory = "../../test/test_files/IEEE_test_cases/"
        self.matlab_test_directory = "/matlab_test_files/"
        self.other_test_directory = "/other_test_cases/"

    def test_basic(self):
        """
        Just a check that in the simple case we do actually get the time back we requested
        """
        sim = griddyn.gridDynSimulation_create("", "sim1")
        if sim is not None:
            file = self.ieee_test_directory + "ieee14.cdf"
            res = griddyn.gridDynSimulation_loadfile(sim, file, "")
            self.assertEqual(res, griddyn.EXECUTION_SUCCESS)
            res = griddyn.gridDynSimulation_run(sim)
            self.assertEqual(res, griddyn.EXECUTION_SUCCESS)
            time = griddyn.gridDynSimulation_getCurrentTime(sim)
            self.assertAlmostEqual(time, 30.0, delta=0.0001)
            obj = griddyn.getSimulationObject(sim)
            griddyn.gridDynObject_free(obj)
            griddyn.gridDynSimulation_free(sim)

    def test_getResult_test(self):
        sim = griddyn.gridDynSimulation_create("", "sim1")
        if sim is not None:
            file = self.ieee_test_directory + "ieee14.cdf"
            res = griddyn.gridDynSimulation_loadfile(sim, file, "")
            self.assertEqual(res, griddyn.EXECUTION_SUCCESS)

            res = griddyn.gridDynSimulation_powerflow(sim)
            cnt = griddyn.gridDynSimulation_busCount(sim)
            self.assertEqual(cnt, 14)

            voltages = griddyn.doubleArray(cnt)
            act = griddyn.gridDynSimulation_getResults(sim, "voltage", voltages, cnt)

            angles = griddyn.doubleArray(cnt)
            self.assertEqual(cnt, act)
            act = griddyn.gridDynSimulation_getResults(sim, "angles", angles, cnt)

            #            for i in range(cnt):
            #                print i,voltages[i]
            #
            #            for i in range(cnt):
            #                print i,angles[i]

            self.assertEqual(cnt, act)
            self.assertAlmostEqual(voltages[0], 1.06, delta=0.00001)
            self.assertAlmostEqual(angles[0], 0, delta=0.0000001)
            self.assertAlmostEqual(voltages[1], 1.045, delta=0.00001)
            self.assertAlmostEqual(voltages[2], 1.01, delta=0.00001)
            self.assertAlmostEqual(voltages[8], 1.056, delta=0.001)
            self.assertAlmostEqual(angles[5], -14.22 * 3.1415927 / 180.0, delta=0.001)
            del voltages
            del angles

    def test_getResult_test(self):
        sim = griddyn.gridDynSimulation_create("", "sim1")
        if sim is not None:
            file = self.ieee_test_directory + "ieee14.cdf"
            res = griddyn.gridDynSimulation_loadfile(sim, file, "")
            self.assertEqual(res, griddyn.EXECUTION_SUCCESS)

            res = griddyn.gridDynSimulation_powerflow(sim)
            obj = griddyn.getSimulationObject(sim)

            bus2 = griddyn.gridDynObject_getSubObject(obj, "bus", 8)

            griddyn.gridDynObject_free(
                obj
            )  # just making sure the bus object is disconnected from obj
            self.assertNotEqual(bus2, None)

            result = griddyn.new_doublep()
            status = griddyn.gridDynObject_getValue(bus2, "voltage", "", result)

            self.assertEqual(status, griddyn.EXECUTION_SUCCESS)
            self.assertAlmostEqual(griddyn.doublep_value(result), 1.056, delta=0.001)
            name = griddyn.charArray(50)
            count = griddyn.gridDynObject_getString(bus2, "name", name, 50)
            name = name.cast()
            self.assertEqual(count, len(name))
            self.assertEqual(name[0:5], "Bus 9")
            griddyn.gridDynObject_free(bus2)
            griddyn.gridDynSimulation_free(sim)


if __name__ == "__main__":
    unittest.main()
