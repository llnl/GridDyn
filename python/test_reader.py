# LLNS Copyright Start
# Copyright (c) 2014, Lawrence Livermore National Security
# This work was performed under the auspices of the U.S. Department
# of Energy by Lawrence Livermore National Laboratory in part under
# Contract W-7405-Eng-48 and in part under Contract DE-AC52-07NA27344.
# Produced at the Lawrence Livermore National Laboratory.
# All rights reserved.
# For details, see the LICENSE file.
# LLNS Copyright End

import numpy as np

import fileReaders

fname = "datafile.dat"


bd = fileReaders.timeSeries2(fname)

import matplotlib

import matplotlib.pyplot as plt

plt.plot(bd.time, bd.data[:, 0])
plt.show()
