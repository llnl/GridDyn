# LLNS Copyright Start
# Copyright (c) 2014, Lawrence Livermore National Security
# This work was performed under the auspices of the U.S. Department
# of Energy by Lawrence Livermore National Laboratory in part under
# Contract W-7405-Eng-48 and in part under Contract DE-AC52-07NA27344.
# Produced at the Lawrence Livermore National Laboratory.
# All rights reserved.
# For details, see the LICENSE file.
# LLNS Copyright End


from tkinter import *

master = Tk()
# master.geometry("640x480")
frame = Frame(master)
# setting up the directory dialog info
flabel = Label(frame, text="dir")
flabel.pack(side=LEFT)

w = Entry(frame, width=50)
w.pack(side=LEFT)

runData = []
cnum = 0


import glob
import os
import numpy as np

import fileReaders


def getFiles():
    dm = w.get()
    dlist = glob.glob(dm + "/*.dat")
    lb.delete(0, END)
    for ff in dlist:
        lb.insert(END, os.path.basename(ff))

    lb.activate(0)
    loadDatFile(dlist[0])


def loadDatFile(fname):
    runData = fileReaders.timeSeries2(fname)
    lb2.delete(0, END)
    print(runData.fields)
    print(runData.cols)
    for fn in runData.fields:
        lb2.insert(END, fn)


def changeFile(event):
    listindex = lb.nearest(event.y)
    if cnum != listindex:
        fn = lb.get(listindex)
        loadDatFile(fn[0])


def _getFolder():
    filename = (
        filedialog.askdirectory()
    )  # show an "Open" dialog box and return the path to the selected file
    w.delete(0, END)
    w.insert(0, filename)
    getFiles()


button = Button(frame, text="...", command=_getFolder)
button.pack(side=RIGHT)

# setup file list box
midgroup = Frame(master)
fgroup = Frame(midgroup)
fbframe = Frame(fgroup)
flabel2 = Label(fbframe, text="files")
flabel2.pack(side=TOP)
scrollbar = Scrollbar(fbframe, orient=VERTICAL)
lb = Listbox(fbframe, yscrollcommand=scrollbar.set, width=28, selectmode=SINGLE)
scrollbar.config(command=lb.yview)


lb.pack(side=LEFT)
lb.insert(END, "test")
lb.insert(END, "TEST2")
lb.insert(END, "scan1")
lb.insert(END, "SCAN2")
lb.bind("<ButtonRelease-1>", changeFile)
scrollbar.pack(side=RIGHT, fill=Y)
fbframe.pack(side=TOP)

# setup fields list box
fbframe2 = Frame(fgroup)
flabel2 = Label(fbframe2, text="fields")
flabel2.pack(side=TOP)
scrollbar2 = Scrollbar(fbframe2, orient=VERTICAL)
lb2 = Listbox(fbframe2, yscrollcommand=scrollbar.set, width=28, selectmode=MULTIPLE)
scrollbar2.config(command=lb.yview)
lb2.pack(side=LEFT)
lb2.insert(END, "test")
lb2.insert(END, "TEST2")
scrollbar2.pack(side=RIGHT, fill=Y)
fbframe2.pack(side=TOP)

fgroup.pack(side=LEFT)

import matplotlib
import matplotlib.pyplot as plt

matplotlib.use("TkAgg")

from numpy import arange, sin, pi
from matplotlib.backends.backend_tkagg import FigureCanvasTkAgg, NavigationToolbar2TkAgg

# implement the default mpl key bindings
from matplotlib.backend_bases import key_press_handler


from matplotlib.figure import Figure

f = Figure(figsize=(5, 4), dpi=100)
a = f.add_subplot(111)
t = arange(0.0, 3.0, 0.01)
s = sin(2 * pi * t)

a.plot(t, s)


# a tk.DrawingArea
pgroup = Frame(midgroup)
canvas = FigureCanvasTkAgg(f, master=pgroup)
canvas.show()
canvas.get_tk_widget().pack(side=TOP, fill=BOTH, expand=1)

toolbar = NavigationToolbar2TkAgg(canvas, pgroup)
toolbar.update()
canvas._tkcanvas.pack(side=TOP, fill=BOTH, expand=1)

pgroup.pack(side=LEFT)

# setup the button group
bgroup1 = Frame(master)


def _makeplot():
    s = sin(4 * pi * t)
    a.plot(t, s)
    canvas.show()


button = Button(bgroup1, text="Plot", command=_makeplot)
button.pack(side=LEFT)


def _quit():
    master.quit()  # stops mainloop
    master.destroy()  # this is necessary on Windows to prevent
    # Fatal Python Error: PyEval_RestoreThread: NULL tstate


frame.pack(side=TOP, fill=Y)
midgroup.pack(side=TOP)

button2 = Button(bgroup1, text="Close", command=_quit)
button2.pack(side=RIGHT)
bgroup1.pack(
    side=TOP,
    fill=Y,
    expand=1,
)

mainloop()
