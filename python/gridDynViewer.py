# LLNS Copyright Start
# Copyright (c) 2014, Lawrence Livermore National Security
# This work was performed under the auspices of the U.S. Department
# of Energy by Lawrence Livermore National Laboratory in part under
# Contract W-7405-Eng-48 and in part under Contract DE-AC52-07NA27344.
# Produced at the Lawrence Livermore National Laboratory.
# All rights reserved.
# For details, see the LICENSE file.
# LLNS Copyright End

#!/usr/bin/python
# -*- coding: iso-8859-1 -*-

import matplotlib

matplotlib.use("TkAgg")

from numpy import arange, sin, pi
from matplotlib.backends.backend_tkagg import FigureCanvasTkAgg, NavigationToolbar2TkAgg

# implement the default mpl key bindings
from matplotlib.backend_bases import key_press_handler


from matplotlib.figure import Figure

import sys
import platform

if sys.version_info[0] < 3:
    import Tkinter as Tk
    import tkFileDialog as tkf
else:
    import tkinter as Tk
    import tkinter.filedialog as tkf

import glob
import os
import fileReaders


class gridDynViewer(Tk.Frame):
    def __init__(self, parent=None, startdir=None):
        if parent is None:
            self.parent = Tk.Tk()
        # self.parent.geometry("640x480")
        else:
            self.parent = parent

        self.dirEntry = None
        self.dirLoc = ""
        self.lb_files = None
        self.lb_fields = None
        self.canvas = None
        self.subplot = None
        self.figure = None
        self.runData = None
        self.cnum = -1
        self.initialize()
        if startdir is not None:
            self.changeDir(startdir)

        if parent is None:
            Tk.mainloop()

    def initialize(self):
        mframe = Tk.Frame(self.parent)
        # setting up the directory dialog info
        flabel = Tk.Label(mframe, text="dir")
        flabel.pack(side=Tk.LEFT)
        w = Tk.Entry(mframe, width=50)
        w.pack(side=Tk.LEFT)
        self.dirEntry = w
        button = Tk.Button(mframe, text="...", command=self._getFolder)
        button.pack(side=Tk.RIGHT)
        mframe.pack(side=Tk.TOP)
        # create the file list boxes
        # setup file list box
        midgroup = Tk.Frame(self.parent)
        fgroup = Tk.Frame(midgroup)
        fbframe = Tk.Frame(fgroup)
        flabel2 = Tk.Label(fbframe, text="files")
        flabel2.pack(side=Tk.TOP)
        scrollbar = Tk.Scrollbar(fbframe, orient=Tk.VERTICAL)
        lb = Tk.Listbox(fbframe, yscrollcommand=scrollbar.set, width=28, selectmode=Tk.SINGLE)
        scrollbar.config(command=lb.yview)
        lb.pack(side=Tk.LEFT)
        lb.bind("<ButtonRelease-1>", self.changeFile)
        scrollbar.pack(side=Tk.RIGHT, fill=Tk.Y)
        fbframe.pack(side=Tk.TOP)
        self.lb_files = lb
        # setup fields list box
        fbframe2 = Tk.Frame(fgroup)
        flabel2 = Tk.Label(fbframe2, text="fields")
        flabel2.pack(side=Tk.TOP)
        scrollbar2 = Tk.Scrollbar(fbframe2, orient=Tk.VERTICAL)
        lb2 = Tk.Listbox(fbframe2, yscrollcommand=scrollbar.set, width=28, selectmode=Tk.MULTIPLE)
        scrollbar2.config(command=lb.yview)
        lb2.pack(side=Tk.LEFT)
        scrollbar2.pack(side=Tk.RIGHT, fill=Tk.Y)
        fbframe2.pack(side=Tk.TOP)

        fgroup.pack(side=Tk.LEFT)
        self.lb_fields = lb2
        # make the plot area
        f = Figure(figsize=(5, 4), dpi=120)
        a = f.add_subplot(111)
        f.subplots_adjust(bottom=0.15, left=0.15)
        t = arange(0.0, 3.0, 0.01)
        s = sin(2 * pi * t)

        a.plot(t, s)

        # a tk.DrawingArea
        pgroup = Tk.Frame(midgroup)
        canvas = FigureCanvasTkAgg(f, master=pgroup)
        canvas.show()
        canvas.get_tk_widget().pack(side=Tk.TOP, fill=Tk.BOTH, expand=1)

        toolbar = NavigationToolbar2TkAgg(canvas, pgroup)
        toolbar.update()
        canvas._tkcanvas.pack(side=Tk.TOP, fill=Tk.BOTH, expand=1)

        pgroup.pack(side=Tk.LEFT)
        self.canvas = canvas
        self.subplot = a
        self.figure = f
        # setup the button group
        bgroup1 = Tk.Frame(self.parent)
        button = Tk.Button(bgroup1, text="Plot", command=self._makeplot)
        button.pack(side=Tk.LEFT)
        mframe.pack(side=Tk.TOP, fill=Tk.Y)
        midgroup.pack(side=Tk.TOP)

        button2 = Tk.Button(bgroup1, text="Close", command=self._quit)
        button2.pack(side=Tk.RIGHT)
        bgroup1.pack(side=Tk.TOP, fill=Tk.Y, expand=1)

    def changeDir(self, newDir):
        if newDir != self.dirLoc:
            self.dirEntry.delete(0, Tk.END)
            self.dirEntry.insert(0, newDir)
            self.dirLoc = newDir
            self.getFiles()

    def getFiles(self):
        self.dirLoc = self.dirEntry.get()
        if platform.system() == "Windows":
            dlist = glob.glob(self.dirLoc + r"\*.dat")
        else:
            dlist = glob.glob(self.dirLoc + "/*.dat")
        self.lb_files.delete(0, Tk.END)
        for ff in dlist:
            self.lb_files.insert(Tk.END, os.path.basename(ff))

        self.lb_files.activate(0)
        if dlist:
            self.loadDatFile(dlist[0])
        self.cnum = 0

    def loadDatFile(self, fname):
        self.runData = fileReaders.timeSeries2(fname)
        self.lb_fields.delete(0, Tk.END)
        for fn in self.runData.fields:
            self.lb_fields.insert(Tk.END, fn)

    def changeFile(self, event):
        listindex = self.lb_files.nearest(event.y)
        if self.cnum != listindex:
            fn = self.lb_files.get(listindex)
            self.loadDatFile(self.dirLoc + "/" + fn)
            self.cnum = listindex

    def _getFolder(self):
        filename = (
            tkf.askdirectory()
        )  # show an "Open" dialog box and return the path to the selected file
        self.changeDir(filename)

    def _makeplot(self):
        st = self.lb_fields.curselection()
        self.subplot.cla()
        title = ""
        for pp in st:
            self.subplot.plot(self.runData.time, self.runData.data[:, pp])

            title = title + " " + str(self.runData.fields[int(pp)])
        # add a title and axis label
        self.subplot.set_title(title)

        self.subplot.set_xlabel("time (s)")

        self.canvas.show()

    def _quit(self):
        self.parent.quit()  # stops mainloop
        self.parent.destroy()  # this is necessary on Windows to prevent
        # Fatal Python Error: PyEval_RestoreThread: NULL tstate


if __name__ == "__main__":
    if len(sys.argv) > 1:
        app = gridDynViewer(startdir=sys.argv[1])
    else:
        app = gridDynViewer(None)
