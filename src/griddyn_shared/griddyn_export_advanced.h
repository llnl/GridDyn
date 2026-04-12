/*
 * LLNS Copyright Start
 * Copyright (c) 2014-2018, Lawrence Livermore National Security
 * This work was performed under the auspices of the U.S. Department
 * of Energy by Lawrence Livermore National Laboratory in part under
 * Contract W-7405-Eng-48 and in part under Contract DE-AC52-07NA27344.
 * Produced at the Lawrence Livermore National Laboratory.
 * All rights reserved.
 * For details, see the LICENSE file.
 * LLNS Copyright End
 */

#ifndef ___W_GRIDDYN_GRIDDYN_SRC_GRIDDYN_SHARED_GRIDDYN_EXPORT_ADVANCED_H_
#define ___W_GRIDDYN_GRIDDYN_SRC_GRIDDYN_SHARED_GRIDDYN_EXPORT_ADVANCED_H_

#include "griddyn_export.h"

#ifdef __cplusplus
extern "C" {
#endif

// object mathematical functions*/

/** get the statesize for an object
    @details this function uses a SolverKey.  0 can be used on objects with no subobjects on objects
   with subobject using a key of 0 will generate an error code and not produce results
    @param[in] obj the object to query
    @param[in] key a SolverKey obtained from ::gridDynSimulation_getSolverKey
    @return the state size of the object for a particular solverkey
    */
GRIDDYN_EXPORT int gridDynObjectStateSize(GridDynObject obj, SolverKey key, GridDynError* err);

/** get the statesize for an object
    @details this function uses a SolverKey.  0 can be used on objects with no subobjects on objects
   with subobject using a key of 0 will generate an error code and not produce results
    @param[in] obj the object to query
    @param[in] key a SolverKey obtained from ::gridDynSimulation_getSolverKey
    @return the state size of the object for a particular solverkey
    */
GRIDDYN_EXPORT void gridDynObjectGuessState(GridDynObject obj,
                                            double time,
                                            double* states,
                                            double* dstate_dt,
                                            SolverKey key,
                                            GridDynError* err);

/** get the statesize for an object
    @details this function uses a SolverKey.  0 can be used on objects with no subobjects on objects
   with subobject using a key of 0 will generate an error code and not produce results
    @param[in] obj the object to query
    @param[in] key a SolverKey obtained from ::gridDynSimulation_getSolverKey
    @return the state size of the object for a particular solverkey
    */
GRIDDYN_EXPORT void gridDynObjectSetState(GridDynObject obj,
                                          double time,
                                          const double* states,
                                          const double* dstate_dt,
                                          SolverKey key,
                                          GridDynError* err);

/** get the statesize for an object
    @details this function uses a SolverKey.  0 can be used on objects with no subobjects on objects
   with subobject using a key of 0 will generate an error code and not produce results
    @param[in] obj the object to query
    @param[in] key a SolverKey obtained from ::gridDynSimulation_getSolverKey
    @return the state size of the object for a particular solverkey
    */
GRIDDYN_EXPORT void gridDynObjectGetStateVariableTypes(GridDynObject obj,
                                                       double* types,
                                                       SolverKey key,
                                                       GridDynError* err);

/** get the statesize for an object
    @details this function uses a SolverKey.  0 can be used on objects with no subobjects on objects
   with subobject using a key of 0 will generate an error code and not produce results
    @param[in] obj the object to query
    @param[in] key a SolverKey obtained from ::gridDynSimulation_getSolverKey
    @return the state size of the object for a particular solverkey
    */
GRIDDYN_EXPORT void gridDynObjectResidual(GridDynObject obj,
                                          const double* inputs,
                                          int inputSize,
                                          double* resid,
                                          SolverKey key,
                                          GridDynError* err);

/** get the statesize for an object
    @details this function uses a SolverKey.  0 can be used on objects with no subobjects on objects
   with subobject using a key of 0 will generate an error code and not produce results
    @param[in] obj the object to query
    @param[in] key a SolverKey obtained from ::gridDynSimulation_getSolverKey
    @return the state size of the object for a particular solverkey
    */
GRIDDYN_EXPORT void gridDynObjectDerivative(GridDynObject obj,
                                            const double* inputs,
                                            int inputSize,
                                            double* deriv,
                                            SolverKey key,
                                            GridDynError* err);

/** get the statesize for an object
    @details this function uses a SolverKey.  0 can be used on objects with no subobjects on objects
   with subobject using a key of 0 will generate an error code and not produce results
    @param[in] obj the object to query
    @param[in] key a SolverKey obtained from ::gridDynSimulation_getSolverKey
    @return the state size of the object for a particular solverkey
    */
GRIDDYN_EXPORT void gridDynObjectAlgebraicUpdate(GridDynObject obj,
                                                 const double* inputs,
                                                 int inputSize,
                                                 double* update,
                                                 double alpha,
                                                 SolverKey key,
                                                 GridDynError* err);

/** get the statesize for an object
    @details this function uses a SolverKey.  0 can be used on objects with no subobjects on objects
   with subobject using a key of 0 will generate an error code and not produce results
    @param[in] obj the object to query
    @param[in] key a SolverKey obtained from ::gridDynSimulation_getSolverKey
    @return the state size of the object for a particular solverkey
    */
GRIDDYN_EXPORT void gridDynObjectJacobian(GridDynObject obj,
                                          const double* inputs,
                                          int inputSize,
                                          double cj,
                                          void (*insert)(int, int, double),
                                          SolverKey key,
                                          GridDynError* err);

/** get the statesize for an object
    @details this function uses a SolverKey.  0 can be used on objects with no subobjects on objects
   with subobject using a key of 0 will generate an error code and not produce results
    @param[in] obj the object to query
    @param[in] key a SolverKey obtained from ::gridDynSimulation_getSolverKey
    @return the state size of the object for a particular solverkey
    */
GRIDDYN_EXPORT void gridDynObjectIoPartialDerivatives(GridDynObject obj,
                                                      const double* inputs,
                                                      int inputSize,
                                                      void (*insert)(int, int, double),
                                                      SolverKey key,
                                                      GridDynError* err);

/** get the statesize for an object
    @details this function uses a SolverKey.  0 can be used on objects with no subobjects on objects
   with subobject using a key of 0 will generate an error code and not produce results
    @param[in] obj the object to query
    @param[in] key a SolverKey obtained from ::gridDynSimulation_getSolverKey
    @return the state size of the object for a particular solverkey
    */
GRIDDYN_EXPORT void gridDynObjectOutputPartialDerivatives(GridDynObject obj,
                                                          const double* inputs,
                                                          int inputSize,
                                                          void (*insert)(int, int, double),
                                                          SolverKey key,
                                                          GridDynError* err);

#ifdef __cplusplus
} /* end of extern "C" { */
#endif

#endif  // ___W_GRIDDYN_GRIDDYN_SRC_GRIDDYN_SHARED_GRIDDYN_EXPORT_ADVANCED_H_
