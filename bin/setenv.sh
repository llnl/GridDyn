# LLNS Copyright Start
# Copyright (c) 2014, Lawrence Livermore National Security
# This work was performed under the auspices of the U.S. Department
# of Energy by Lawrence Livermore National Laboratory in part under
# Contract W-7405-Eng-48 and in part under Contract DE-AC52-07NA27344.
# Produced at the Lawrence Livermore National Laboratory.
# All rights reserved.
# For details, see the LICENSE file.
# LLNS Copyright End

#
# Environment setup for ParGrid on LLNL machines
# Sets path to current packages/compilers etc that are used.
#

setenv PARGRID_SRC_DIR $PWD

#case $(hostname) in
#     tux*)
#     # CASC Tux cluster
#     export SYS_TYPE=rh6
#
#     GRIDDYN=/usr/casc/EBSim/apps/rh6

     # export SUNDIALS_DIR=$GRIDDYN/sundials/R3896
     # export BOOST_DIR=$GRIDDYN/boost/1.55.0
     # export XERCES_DIR=$GRIDDYN/xerces/2.8.0
     # export CPPUNIT_DIR=$GRIDDYN/cppunit/1.12.1
     # ;;

     # *)

use gcc-4.8.2p
use mvapich2-gnu

     # Default to LLNL LC
setenv GRIDDYN /usr/gapps/griddyn/apps/${SYS_TYPE}

setenv SUNDIALS_DIR $GRIDDYN/sundials/R4319
setenv KLU_DIR $GRIDDYN/KLU/2014-10
setenv BOOST_DIR $GRIDDYN/boost/1.55.0.llnl
setenv XERCES_DIR $GRIDDYN/xerces/2.8.0
setenv CPPUNIT_DIR $GRIDDYN/cppunit/1.12.1

#     ;;
#esac

set LD_LIBRARY_PATH = ($LD_LIBRARY_PATH $XERCES_DIR/lib $BOOST_DIR/lib)

# This is location for install
setenv PARGRID_DIR $PARGRID_SRC_DIR/install

# Gridlab path
setenv GLPATH $PARGRID_DIR/etc/gridlabd

set PATH = ($PATH $PARGRID_DIR/bin)
#setenv PATH ".:$PATH:$PARGRID_DIR/bin"
