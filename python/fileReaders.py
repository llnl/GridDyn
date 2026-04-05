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
import struct


class timeSeries2(object):
    """an object to hold a hand of cards"""

    def __init__(self, filename=None, rows=1, columns=1):
        self.description = ""
        self.time = np.empty([rows, 1])
        self.data = np.empty([rows, columns])
        self.cols = columns
        self.count = 0
        self.fields = []
        if filename != None:
            self.loadBinaryFile(filename)

    def __str__(self):
        rep = self.description
        return rep

    def addData(self, t, point, column=0):
        if self.count > self.time.size():
            self.setCapacity(2 * self.time.size())

        if self.count > 0:
            if t <= self.time[self.count - 1]:
                self.count = self.count - 1
            else:
                self.time[self.count] = t
        else:
            self.time[self.count] = t

        if len(point) > 1:
            if len(point) == cols:
                for ii in range(0, cols - 1):
                    self.data[self.count, ii] = point[ii]
        else:
            self.data[self.count, column] = point

    def setSize(self, newSize):
        if newSize > self.time.size:
            self.time.resize((newSize, 1))
            self.data.resize((newSize, self.cols))

    def setCapacity(self, newCap):
        if newCap > self.time.size:
            self.time.resize((newCap, 1))
            self.data.resize((newCap, self.cols))

    def setCols(self, newSize):
        if newSize > self.cols:
            self.data.resize((self.time.size, newSize))
            self.cols = newSize

    def loadBinaryFile(self, filename):
        achar = "<"
        with open(filename, "rb") as f:
            align = struct.unpack(achar + "I", f.read(4))
            if align[0] != 1:
                achar = ">"
            dcount = struct.unpack(achar + "I", f.read(4))
            self.description = f.read(dcount[0])
            cnt = struct.unpack(achar + "I", f.read(4))
            cls = struct.unpack(achar + "I", f.read(4))
            self.setCols(cls[0] - 1)
            self.setSize(cnt[0])
            self.fields = []
            for ii in range(0, self.cols):
                flen = struct.unpack(achar + "b", f.read(1))
                if flen[0] > 0:
                    self.fields.append(f.read(int(flen[0])))
                else:
                    self.fields.append("field_" + str(ii))

            buf = struct.unpack(achar + str(cnt[0]) + "d", f.read(8 * cnt[0]))
            self.time = np.array(buf)
            for ii in range(0, self.cols):
                buf = struct.unpack(achar + str(cnt[0]) + "d", f.read(8 * cnt[0]))
                self.data[:, ii] = buf
            f.close()

    def loadTextFile(self, filename):
        with open(filename, "r") as f:
            f.close()

    def writeBinaryFile(self, filename):
        with open(filename, "wb") as f:
            f.close()

    def writeTextFile(self, filename):
        with open(filename, "w") as f:
            f.close()
