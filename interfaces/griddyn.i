#define __attribute__(x)
#define GRIDDYN_Export __attribute__ ((visibility ("default")))

%include "typemaps.i"
#pragma SWIG nowarn=451

%module griddyn

%include cpointer.i
%pointer_functions(double, doublep);
%pointer_functions(char, charp);

%apply (char *STRING, size_t LENGTH) { (const char *value, int N) };
%cstring_output_maxsize(char *value, int N);
%include carrays.i
%array_class(double, doubleArray);
int gridDynSimulation_getResults(gridDynSimReference sim, const char *datatype, double data[0], int maxSize);

%{
#include "griddyn_export.h"
#include "griddyn_export_advanced.h"
%}

%include "griddyn_export.h"
%include "griddyn_export_advanced.h"
