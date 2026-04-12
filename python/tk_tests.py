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
master.geometry("640x480")
frame = Frame(master)
# setting up the directory dialog info
flabel = Label(master, text="dir")
flabel.place(height=30, width=50, x=0, y=0)

w = Entry(master)
w.place(height=30, width=370, x=50, y=0)

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


button = Button(master, text="...", command=_getFolder)
button.place(height=30, width=50, x=400, y=0)

# setup file list box
flabel = Label(master, text="files")
flabel.place(height=30, width=200, x=0, y=30)
scrollbar = Scrollbar(frame, orient=VERTICAL)
lb = Listbox(frame, yscrollcommand=scrollbar.set, width=28, selectmode=SINGLE)
scrollbar.config(command=lb.yview)
scrollbar.place(height=120, width=30, x=200, y=60)
lb.place(height=120, width=200, x=0, y=60)
lb.insert(END, "test")
lb.insert(END, "TEST2")
lb.insert(END, "scan1")
lb.insert(END, "SCAN2")
lb.bind("<ButtonRelease-1>", changeFile)

# setup fields list box
flabel2 = Label(master, text="fields")
flabel2.place(height=30, width=200, x=0, y=200)
scrollbar2 = Scrollbar(frame, orient=VERTICAL)
lb2 = Listbox(frame, yscrollcommand=scrollbar.set, width=28, selectmode=MULTIPLE)
scrollbar2.config(command=lb.yview)
scrollbar2.place(height=120, width=30, x=200, y=230)
lb2.place(height=120, width=200, x=0, y=230)
lb2.insert(END, "test")
lb2.insert(END, "TEST2")

import matplotlib
import matplotlib.pyplot as plt


def _makeplot():
    print(runData.fields)
    plt.plot(runData.time, runData.data[:, 0])
    plt.show()


button = Button(master, text="Plot", command=_makeplot)
button.place(height=30, width=80, x=50, y=350)


def _quit():
    master.quit()  # stops mainloop
    master.destroy()  # this is necessary on Windows to prevent
    # Fatal Python Error: PyEval_RestoreThread: NULL tstate


button = Button(master, text="Close", command=_quit)
button.place(height=30, width=80, x=400, y=400)

frame.place(height=640, width=480)
mainloop()
