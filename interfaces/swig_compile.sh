#!/bin/bash

INC=$(python -c "import distutils.sysconfig; print(distutils.sysconfig.PREFIX)")/include
#FILELIST=`ls -1 ../src/griddyn_shared/*.cpp`
CONFIGH=$(find ../build -name "config.h" -exec dirname {} \;)
INCPYTHON=$(python -c "from distutils import sysconfig; print(sysconfig.get_python_inc())")
echo compiling griddyn
#swig  -I/software/griddyn/src/coupling -I/software/griddyn/src/extraModels -I/software/griddyn/src/fmi -I/software/griddyn/src/fmi_export -I/software/griddyn/src/fncs -I/software/griddyn/src/formatInterpreters -I/software/griddyn/src/fskit -I/software/griddyn/src/gridDynMain -I/software/griddyn/src/gridDynServer -I/software/griddyn/src/griddyn_shared -I/software/griddyn/src/helics -I/software/griddyn/src/optimization -I/software/griddyn/src/plugins -I/software/griddyn/src/utilities -I/software/griddyn/src/zmqlib -I/software/griddyn/src/gridDynCombined -I/software/griddyn/src/gridDynCombined -I/software/griddyn/src/fileInput/ -I/software/griddyn/src/griddyn/ -python  griddyn.i

swig -I/software/griddyn/src/griddyn_shared -python griddyn.i

echo ${INC}
echo ${CONFIGH}
echo ${INCPYTHON}
#g++ -fPIC -DBUILD_DLL ${FILELIST} griddyn_wrap.c  -I${INCPYTHON} -I. -I../src -I../src/griddyn -I../src/griddyn_shared -I${CONFIGH} -I${INC}/boost -I${INC}/boost/containers  -I${INC} -shared -o _griddyn.so -L/software/anaconda2/envs/gridDyn/lib/ -lgriddyn -lcoreObjects -lutilities -lminizip -lz  -lboost_filesystem -lboost_program_options -lsundials_ida -lsundials_cvode -lsundials_arkode -lsundials_kinsol -lcoupling_static_lib -lsundials_nvecserial -lgridDynCombined -lextraModelLibrary -lformatInterpreter -ltinyxml2 -lticpp  -lfileInput
g++ -fPIC -DBUILD_DLL griddyn_wrap.c -I../src/griddyn_shared -I${INCPYTHON} -I${CONFIGH} -I${INC} -shared -o _griddyn.so -L/software/anaconda2/envs/gridDyn/lib/ -lgriddyn_shared_lib
cp griddyn.py /software/anaconda2/envs/gridDyn/lib/python2.7/site-packages/griddyn/
cp _griddyn.so /software/anaconda2/envs/gridDyn/lib/python2.7/site-packages/griddyn/

swig -I/software/griddyn/src/griddyn_shared -matlab griddyn.i
g++ -shared -fPIC -DBUILD_DLL griddyn_wrap.cxx -I /export/software/matlab/R2017b/extern/include/ -I../src/griddyn_shared -I${INCPYTHON} -I${CONFIGH} -I${INC} -shared -o griddynMEX.mexa64 -L/software/anaconda2/envs/gridDyn/lib/ -lgriddyn_shared_lib -L/export/software/matlab/R2017b/bin/glnxa64 -lmx -lmex
# export LD_LIBRARY_PATH=/software/anaconda2/envs/gridDyn/lib:/export/software/matlab/R2017b/bin/glnxa64
