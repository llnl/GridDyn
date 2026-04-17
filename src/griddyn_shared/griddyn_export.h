/*
 * Copyright (c) 2014-2026, Lawrence Livermore National Security
 * See the top-level NOTICE for additional details. All rights reserved.
 * SPDX-License-Identifier: BSD-3-Clause
 */

#pragma once

/** @file
@brief main file defining the C API to GridDyn
@details the function defined in this file make up the C api for gridDyn
*/

/** @defgroup GridDyn_api the group of function defining the C api to gridDyn
 *  @{
 */
#ifndef ___W_GRIDDYN_GRIDDYN_SRC_GRIDDYN_SHARED_GRIDDYN_EXPORT_H_
#define ___W_GRIDDYN_GRIDDYN_SRC_GRIDDYN_SHARED_GRIDDYN_EXPORT_H_

#include "griddyn_shared_export.h"

#ifdef __cplusplus
extern "C" {
#endif

#include <stdlib.h>

/** typedef a GridDynObject to a void * to represent an object in GridDyn */
typedef void* GridDynObject;

/** typedef a GridDynSimulation to a void * to represent a simulation object*/
typedef void* GridDynSimulation;

/** typedef a GridDynSingleQuery to a void * to represent a query for a single data point*/
typedef void* GridDynSingleQuery;

/** typedef a GridDynVectorQuery to a void * to represent a collector for multiple data points*/
typedef void* GridDynVectorQuery;

/** typedef a GridDynEvent to a void * to represent an event in GridDyn*/
typedef void* GridDynEvent;

/** typedef * SolverKey to a void * to represent a solverMode object for use in a number of
 * functions*/
typedef void* SolverKey;

typedef enum {
    griddyn_error_external_type = -203, /*!< an unknown non-griddyn error was produced */
    griddyn_error_other = -202, /*!< the function produced a griddyn error of some other type */

    griddyn_error_function_failure = -101, /*!< general function failure message*/
    griddyn_error_invalid_function_call = -60, /*!< the call was not valid */
    griddyn_error_object_not_initialized =
        -54, /*!< the object was not in the appropriate initialization state*/
    griddyn_error_solve_error =
        -43, /*!< the call made was invalid in the present state of the calling object*/
    griddyn_error_file_load_failure =
        -36, /*!< error issued when an invalid state transition occurred */
    griddyn_error_query_load_failure = -33, /*!< the function issued a warning of some kind */
    griddyn_error_remove_failure =
        -28, /*!< the federate has terminated and the call cannot be completed*/
    griddyn_error_add_failure = -27, /*!< the input was discarded and not used for some reason */
    griddyn_error_unknown_parameter =
        -26, /*!< the parameter passed was invalid and unable to be used*/
    griddyn_error_invalid_parameter_value = -25, /* indicator that the value passed was invalid*/
    griddyn_error_invalid_object =
        -24, /*!< indicator that the object used was not a valid object */
    griddyn_error_insufficient_space =
        -18, /*!< insufficient space is available to store requested data */
    griddyn_ok = 0 /*!< the function executed successfully */

} void_enum;

/**
 * Griddyn error object
 *
 * if error_code==0 there is no error, if error_code!=0 there is an error and message will contain a
 * string, otherwise it will be an empty string
 */
typedef struct GriddynError {
    int32_t error_code; /*!< an error code associated with the error*/
    const char* message; /*!< a message associated with the error*/
} GridDynError;

// definitions for void
#ifndef GRIDDYN_PENDING
#    define GRIDDYN_PENDING (25)
#endif

#define GRIDDYN_COMPLETE (30)

/**
 * Get a version string for GridDyn.
 */
GRIDDYN_EXPORT const char* gridDynGetVersion(void);

/**
 * Get the build flags used to compile HELICS.
 */
GRIDDYN_EXPORT const char* gridDynGetBuildFlags(void);

/**
 * Get the compiler version used to compile GridDyn
 */
GRIDDYN_EXPORT const char* gridDynGetCompilerVersion(void);

/**
 * Return an initialized error object.
 */
GRIDDYN_EXPORT GriddynError gridDynErrorInitialize(void);

/**
 * Clear an error object.
 */
GRIDDYN_EXPORT void gridDynErrorClear(GridDynError* err);

// the object hierarchy interface and object properties interface

/** create an object in GridDyn
    @details the function creates a an owning pointer to the object, as long as this owning pointer
   exists the object exists,  if the object is added to the hierarchy and remains in operation the
   object will continue to exist even if the GridDynObject representing it is freed
    @param[in] componentType  the component type of the object to create (bus, link, load, etc)
    @param[in] objectType  the specific type of the component to create
    @return a GridDynObject that represents the newly created object
    */
GRIDDYN_EXPORT GridDynObject gridDynObjectCreate(const char* componentType,
                                                 const char* objectType,
                                                 GridDynError* err);

/** clone an object from an existing object
    @details a new object is constructed that is a clone of the original object
    @param[in] obj the original object
    @return a GridDynObject that represents the newly created object
    */
GRIDDYN_EXPORT GridDynObject gridDynObjectClone(GridDynObject obj, GridDynError* err);

/** free the object,  which may result in object destruction if it is not being used elsewhere
    @param[in] obj the object to free
    */
GRIDDYN_EXPORT void gridDynObjectFree(GridDynObject obj);

/** add an object to another in the hierarchy
    @param[in] parentObject the object to which another is being added
    @param[in] objectToAdd  the object being added
    @return 0 if successful, non-zero otherwise
    */
GRIDDYN_EXPORT void
    gridDynObjectAdd(GridDynObject parentObject, GridDynObject objectToAdd, GridDynError* err);

/** remove an object from another in the hierarchy
    @param[in] parentObject the object to which another is being added
    @param[in] objectToRemove  the object being removed
    */
GRIDDYN_EXPORT void gridDynObjectRemove(GridDynObject parentObject,
                                        GridDynObject objectToRemove,
                                        GridDynError* err);

/** set a string parameter in an object
    @param[in] obj the object to set the property of
    @param[in] parameter the parameter to set
    @param[in] value the desired value of the parameter
    @return 0 on success, negative otherwise
    */
GRIDDYN_EXPORT void gridDynObjectSetString(GridDynObject obj,
                                           const char* parameter,
                                           const char* value,
                                           GridDynError* err);

/** set a value parameter in an object
    @details the units are either not needed or assumed to be the default units often pu
    @param[in] obj the object to set the property of
    @param[in] parameter the parameter to set
    @param[in] value the desired value of the parameter
    @return 0 on success, negative otherwise
    */
GRIDDYN_EXPORT void gridDynObjectSetValue(GridDynObject obj,
                                          const char* parameter,
                                          double value,
                                          GridDynError* err);

/** set a value parameter in an object
    @param[in] obj the object to set the property of
    @param[in] parameter the parameter to set
    @param[in] value the desired value of the parameter
    @param[in] units a description of the units which correspond to the parameter
    @return 0 on success, negative otherwise
    */
GRIDDYN_EXPORT void gridDynObjectSetValueUnits(GridDynObject obj,
                                               const char* parameter,
                                               double value,
                                               const char* units,
                                               GridDynError* err);

/** set a string parameter in an object
    @param[in] obj the object to set the property of
    @param[in] parameter the parameter to set
    @param[in] value the desired value of the parameter
    @return 0 on success, negative otherwise
    */
GRIDDYN_EXPORT void
    gridDynObjectSetFlag(GridDynObject obj, const char* flag, int val, GridDynError* err);

/** query the value of a string in an object
    @param[in] obj the object to query
    @param[in] parameter the name of the parameter to query
    @param[out] value the storage location for the string
    @param[in] N the max size of the string
    @param[out] the actual Size of the returned string
    @return the size of the string returned in value*/
GRIDDYN_EXPORT void gridDynObjectGetString(GridDynObject obj,
                                           const char* parameter,
                                           char* value,
                                           int N,
                                           int* actualSize,
                                           GridDynError* err);

/** query the value of an object parameter
    @param[in] obj the object to query
    @param[in] parameter the name of the parameter to query
    @return the value of the parameter*/
GRIDDYN_EXPORT double
    gridDynObjectGetValue(GridDynObject obj, const char* parameter, GridDynError* err);

/** query the value of an object parameter
    @param[in] obj the object to query
    @param[in] parameter the name of the parameter to query
    @param[in] units the desired output units
    @param[out] result the location to store the result
    @return 0 if the value is valid or griddyn_unknown_parameter Otherwise*/
GRIDDYN_EXPORT double gridDynObjectGetValueUnits(GridDynObject obj,
                                                 const char* parameter,
                                                 const char* units,
                                                 GridDynError* err);

/** query the value of an object parameter
    @param[in] obj the object to query
    @param[in] parameter the name of the parameter to query
    @param[out] result the value of the flag 0 if false otherwise(usually 1) if true
    @return 0 if the flag is found griddyn_unknown_parameter Otherwise*/
GRIDDYN_EXPORT int gridDynObjectGetFlag(GridDynObject obj, const char* flag, GridDynError* err);

/** find an object within another object
    @param[in] obj the object as the basis of the search
    @param[in] objectToFind a string describing the object
    @return a GridDynObject with the found object,  nullptr if not found*/
GRIDDYN_EXPORT GridDynObject gridDynObjectFind(GridDynObject obj,
                                               const char* objectToFind,
                                               GridDynError* err);

/** get a subobject of a particular object by type and index
    @param[in] obj the object to use as the basis for the search
    @param[in] componentType the type of component to search
    @param[in] N  the index of the object to retrieve
    @return a GridDynObject as indicated by the index or a nullptr
    */
GRIDDYN_EXPORT GridDynObject gridDynObjectGetSubObject(GridDynObject obj,
                                                       const char* componentType,
                                                       int N,
                                                       GridDynError* err);

/** get a subobject of a particular object by type and index
    @param[in] obj the object to use as the basis for the search
    @param[in] componentType the type of component to search
    @param[in] ID  the user identifier for the object
    @return a GridDynObject as indicated by the user ID
    */
GRIDDYN_EXPORT GridDynObject gridDynObjectFindByUserId(GridDynObject obj,
                                                       const char* componentType,
                                                       int ID,
                                                       GridDynError* err);
/** get the parent of an object
    @param[in] obj the object to get the parent of
    @return the parent of the object in question or a nullptr if the object is a root object
    */
GRIDDYN_EXPORT GridDynObject gridDynObjectGetParent(GridDynObject obj, GridDynError* err);
/** get a const char * to a string representing the componentType of the object
    @param[in] obj the object to query
    @return a pointer to a type string.  The memory is managed internally and does not need to be
   freed the pointers are to a fixed set of strings */
GRIDDYN_EXPORT const char* gridDynObjectGetType(GridDynObject obj);

// functions for the gridDyn Simulation
/** create a new simulation object
    @param[in] type the type of simulation to create (can be "" for the standard type
    @param[in] name  the name of the simulation to create
    @return a reference to the simulation object
    */
GRIDDYN_EXPORT GridDynSimulation gridDynSimulationCreate(const char* type,
                                                         const char* name,
                                                         GridDynError* err);

/** free the simulation memory
    @param[in] sim the simulation runner reference object
    */
GRIDDYN_EXPORT void gridDynSimulationFree(GridDynSimulation sim);

/** free the simulation memory
    @param[in] sim the simulation runner reference object
    @param[in] initializationString
    @return error code if not successful
    */
GRIDDYN_EXPORT void gridDynSimulationInitializeFromString(GridDynSimulation sim,
                                                          const char* initializationString,
                                                          GridDynError* err);

/** free the simulation memory
    @param[in] sim the simulation runner reference object
    @param[in] argc  the argument count from command line arguments
    @param[in] argv the argument values from the command line arguments
    @return 0 if successful an error code otherwise
    */
GRIDDYN_EXPORT void gridDynSimulationInitializeFromArgs(GridDynSimulation sim,
                                                        int argc,
                                                        char* argv[],
                                                        int ignoreUnrecognized,
                                                        GridDynError* err);

/** load a simulation file or add a file to the existing simulation
    @param[in] sim the simulation runner reference object
    @param[in] fileName the name of the file to load
    @param[in] fileType the type of the file set to null string to enable automatic detection based
   on the extension
    @return 0 if successful an error code otherwise
    */
GRIDDYN_EXPORT void gridDynSimulationLoadfile(GridDynSimulation sim,
                                              const char* fileName,
                                              const char* fileType,
                                              GridDynError* err);

/** add a command to the GridDyn Command queue
    @details this is one of the main interfaces to get GridDyn to do all sorts of things and change
   how it does it this includes scripting and execution order once a command queue is set up the run
   command will execute the command queue
    @param[in] sim the simulation runner reference object
    @return 0 if successful an error code otherwise
    */
GRIDDYN_EXPORT void
    gridDynSimulationAddCommand(GridDynSimulation sim, const char* command, GridDynError* err);

/** initialize the simulation so it is ready to execute a power flow
    @param[in] sim the simulation runner reference object
    @return 0 if successful an error code otherwise
    */
GRIDDYN_EXPORT void gridDynSimulationPowerflowInitialize(GridDynSimulation sim, GridDynError* err);

/** run a power flow calculation on the current simulation
    @param[in] sim the simulation runner reference object
    @return 0 if successful an error code otherwise
    */
GRIDDYN_EXPORT void gridDynSimulationPowerflow(GridDynSimulation sim, GridDynError* err);

/** initialize the simulation so it is ready to execute a dynamic simulation
    @param[in] sim the simulation runner reference object
    @return 0 if successful an error code otherwise
    */
GRIDDYN_EXPORT void gridDynSimulationDynamicInitialize(GridDynSimulation sim, GridDynError* err);

/** reset the simulation to time 0
    @param[in] sim the simulation runner reference object
    @return 0 if successful an error code otherwise
    */
GRIDDYN_EXPORT void gridDynSimulationReset(GridDynSimulation sim, GridDynError* err);

/** get the current simulation time
    @param sim the simulation runner reference object
    @return the simulation time in seconds
    */
GRIDDYN_EXPORT double gridDynSimulationGetCurrentTime(GridDynSimulation sim, GridDynError* err);
/** run the command queue of the simulation
    @details if the command queue is empty it will try to run a dynamic simulation if the models are
   capable of that otherwise it runs a power flow and stops
    @param[in] sim the simulation runner reference object
    @return 0 if successful an error code otherwise
    */
GRIDDYN_EXPORT void gridDynSimulationRun(GridDynSimulation sim, GridDynError* err);

/** run the simulation to a particular time
    @param[in] sim the simulation runner reference object
    @param[in] runToTime the time to execute the simulator to
    @return the actual time run to
    */
GRIDDYN_EXPORT double
    gridDynSimulationRunTo(GridDynSimulation sim, double runToTime, GridDynError* err);

/** step the simulator one event step
    @param[in] sim the simulation runner reference object
    @return 0 if successful an error code otherwise
    */
GRIDDYN_EXPORT double gridDynSimulationStep(GridDynSimulation sim, GridDynError* err);

/** run the command queue of the simulation asynchronously
    @details if the command queue is empty it will try to run a dynamic simulation if the models are
   capable of that otherwise it runs a power flow and stops this function will return immediately
    @param[in] sim the simulation runner reference object
    @return 0 if successful an error code otherwise
    */
GRIDDYN_EXPORT void gridDynSimulationRunAsync(GridDynSimulation sim, GridDynError* err);

/** run the simulation to a particular time but return and run asynchronously
    @param[in] sim the simulation runner reference object
    @param[in] runToTime the time to execute the simulator to
    @return 0 if successful an error code otherwise
    */
GRIDDYN_EXPORT void
    gridDynSimulationRunToAsync(GridDynSimulation sim, double runToTime, GridDynError* err);

/** step the simulator one event step
    @param[in] sim the simulation runner reference object
    @return 0 if successful an error code otherwise
    */
GRIDDYN_EXPORT void gridDynSimulationStepAsync(GridDynSimulation sim, GridDynError* err);
/** query the status of a simulation
    @param[in] sim the simulation runner reference object
    @return will return GRIDDYN_PENDING if an Async operation is ongoing, otherwise will return the
   execution state
    */
GRIDDYN_EXPORT int gridDynSimulationGetStatus(GridDynSimulation sim, GridDynError* err);
/** get an object reference for the simulation object that can be used in the gridDynDbject
   functions
    @param[in] sim the simulation runner reference object
    @return a GridDynObject that can be used in the gridDynObject_* functions
    */
GRIDDYN_EXPORT GridDynObject getSimulationObject(GridDynSimulation sim, GridDynError* err);

// mathematical function for using the simulation as a function evaluator
/** get a SolverKey value that can be used for the SolverKey input in the other math functions
    @param[in] sim the simulation runner reference object
    @param[in] solverType  the type or name of a solver used in GridDyn
    @return a SolverKey object used in functions that require a SolverKey
    */
GRIDDYN_EXPORT SolverKey gridDynSimulationGetSolverKey(GridDynSimulation sim,
                                                       const char* solverType,
                                                       GridDynError* err);

/** free a solver key
    @param[in] key the solver key to be freed that is no longer needed
    */
GRIDDYN_EXPORT void gridDynSolverKeyFree(SolverKey key);

/** get the simulation state size
    @param[in] sim the simulation runner reference object
    @param[in] key the index of the solver to use (the value should be retrieved by
    ::gridDynSimulation_getSolverKey)
    @return  the state size of the system if <=0 an error was encountered
    */
GRIDDYN_EXPORT int
    gridDynSimulationStateSize(GridDynSimulation sim, SolverKey key, GridDynError* err);

/** get the number of buses in the simulation
    @param[in] sim the simulation runner reference object
    @return  the number of buses in the simulation
    */
GRIDDYN_EXPORT int gridDynSimulationBusCount(GridDynSimulation sim);
/** get the number of lines in the simulation
    @param[in] sim the simulation runner reference object
    @return  the number of lines in the simulation
    */
GRIDDYN_EXPORT int gridDynSimulationLineCount(GridDynSimulation sim);
/** get a set of data from the simulation
    @param[in] sim the simulation runner reference object
    @param datatype a string defining what data to retrieve from the simulation
    @param[out] data memory for storing the requested results
    @param maxSize the size of the memory location specified by voltages
    @return  the number of points written
    */
GRIDDYN_EXPORT void gridDynSimulationGetResults(GridDynSimulation sim,
                                                const char* dataType,
                                                double* data,
                                                int maxSize,
                                                int* actualSize,
                                                GridDynError* err);

/** have the simulation guess at all the state variables
    @param[in] sim the simulation runner reference object
    @param[in] time the simulation time to guess the state for
    @param[out] states memory to store the states must be at least the size returned by
    ::gridDynSimulation_stateSize
    @param[out] dstate_dt memory to store the guess of the derivatives if the solver key points to a
   solver mode with differential variables must be the same size as ::states
    @param[in] key the index of the solver to use (the value should be retrieved by
    ::gridDynSimulation_getSolverKey)
    @return 0 if successful an error code otherwise
    */
GRIDDYN_EXPORT void gridDynSimulationGuessState(GridDynSimulation sim,
                                                double time,
                                                double* states,
                                                double* dstate_dt,
                                                SolverKey key,
                                                GridDynError* err);

/** set the system state,  store state variables into the simulation objects
    @param[in] sim the simulation runner reference object
    @param[in] time the time relates to the states
    @param[in] states the state variables of the system must be of the size returned by
    ::gridDynSimulation_stateSize
    @param[in] dstate_dt the time derivatives of the states can be nullptr if the solverkey
   references does not have differential states
    @param[in] key the index of the solver to use (the value should be retrieved by
    ::gridDynSimulation_getSolverKey)
    @return 0 if successful an error code otherwise
    */
GRIDDYN_EXPORT void gridDynSimulationSetState(GridDynSimulation sim,
                                              double time,
                                              const double* states,
                                              const double* dstate_dt,
                                              SolverKey key,
                                              GridDynError* err);

/** get the types(algebraic or differential) of all the variables in the simulation voltages and
   angle variables are algebraic
    @param[in] sim the simulation runner reference object
    @param[out] types the types of the system 0 for differential variables 1 for algebraic
    @param[in] key the index of the solver to use (the value should be retrieved by
    ::gridDynSimulation_getSolverKey)
    @return 0 if successful an error code otherwise
    */
GRIDDYN_EXPORT void gridDynSimulationGetStateVariableTypes(GridDynSimulation sim,
                                                           double* types,
                                                           SolverKey key,
                                                           GridDynError* err);

/** compute the residual values for all states in the system
    @details the residual function will result in all very small values if the states are self
   consistent f(x,x')=0 if everything is correct
    @param[in] sim the simulation runner reference object
    @param[in] time the simulation to get the residual for
    @param[out] resid the memory location to store the results of the calculation
    @param[in] states the state data to use in computing the residual
    @param[in] dstate_dt the derivative data to use in computing the residual
    @param[in] key the index of the solver to use (the value should be retrieved by
    ::gridDynSimulation_getSolverKey)
    @return 0 if successful an error code otherwise
    */
GRIDDYN_EXPORT void gridDynSimulationResidual(GridDynSimulation sim,
                                              double time,
                                              double* resid,
                                              const double* states,
                                              const double* dstate_dt,
                                              SolverKey key,
                                              GridDynError* err);

/** compute the derivatives for all differential variables in the system
    @param[in] sim the simulation runner reference object
    @param[in] time the time to compute the derivative for
    @param[out] deriv the memory storage to store the derivative
    @param[in] states the state data to use in the computation
    @param[in] key the index of the solver to use (the value should be retrieved by
    ::gridDynSimulation_getSolverKey)
    @return 0 if successful an error code otherwise
    */
GRIDDYN_EXPORT void gridDynSimulationDerivative(GridDynSimulation sim,
                                                double time,
                                                double* deriv,
                                                const double* states,
                                                SolverKey key,
                                                GridDynError* err);

/** compute an algebraic update for all algebraic variables in a system
    @param[in] sim the simulation runner reference object
    @param[out] update the memory location to store all the updates to the algebraic variables
    @param[in] states the state variables to use in computing the update
    @param[in] alpha a update scaling parameter used in some cases where the update must be computed
   iteratively
    @param[in] key the index of the solver to use (the value should be retrieved by
    ::gridDynSimulation_getSolverKey)
    @return 0 if successful an error code otherwise
    */
GRIDDYN_EXPORT void gridDynSimulationAlgebraicUpdate(GridDynSimulation sim,
                                                     double time,
                                                     double* update,
                                                     const double* states,
                                                     double alpha,
                                                     SolverKey key,
                                                     GridDynError* err);

/** generate a system Jacobian matrix
    @param[in] sim the simulation runner reference object
    @param[in] time the time to use in the Jacobian computation
    @param[in] states the state variable values to use in the computation
    @param[in] dstate_dt the derivative information to use in the computation of the system Jacobian
    @param[in] cj the constant of differentiation.  this is the number to use when the Jacobian
   depends on the derivative of state
    @param[in] key the index of the solver to use (the value should be retrieved by
    ::gridDynSimulation_getSolverKey)
    @param[in] insert a function pointer to a callback that takes a row, a column and a value to
   define values on a sparse matrix
    @return 0 if successful an error code otherwise
    */
GRIDDYN_EXPORT void gridDynSimulationJacobian(GridDynSimulation sim,
                                              double time,
                                              const double* states,
                                              const double* dstate_dt,
                                              double cj,
                                              SolverKey key,
                                              void (*insert)(int, int, double),
                                              GridDynError* err);
// functions for querying values
/** create a query object to retrieve a single value repeatedly
    @details if just a single value is needed once use ::gridDynObject_getValue
    @param[in] obj the object to retrieve the value from (can be nullptr in this case)
    @param[in] queryString a string representing a value to query
    @return a singleQueryObject
    */
GRIDDYN_EXPORT GridDynSingleQuery gridDynSingleQueryCreate(GridDynObject obj,
                                                           const char* queryString,
                                                           GridDynError* err);

/** create a query object to retrieve a set of values repeatedly
    @details can be one or more values that can be retrieved to a vector
    @param[in] obj the object to retrieve the value from (can be nullptr in this case)
    @param[in] queryString a string representing a value to query
    @return a vectorQueryObject
    */
GRIDDYN_EXPORT GridDynVectorQuery gridDynVectorQueryCreate(GridDynObject obj,
                                                           const char* queryString,
                                                           GridDynError* err);

/** release the memory from a single query object
    @param[in] query the query object to free
    */
GRIDDYN_EXPORT void gridDynSingleQueryFree(GridDynSingleQuery query);

/** release the memory from a vector query object
    @param[in] query the query object to free
    */
GRIDDYN_EXPORT void gridDynVectorQueryFree(GridDynVectorQuery query);

/** get the result from a single query
    @param[in] query the single query to run
    @return the value retrieved by the query
    */
GRIDDYN_EXPORT double gridDynSingleQueryRun(GridDynSingleQuery query, GridDynError* err);

/** get the results from a Vector query
    @param[in] query the vector query to run
    @param[out] data the memory location to store the data
    @param[in] N the maximum size of the memory location
    @return the value retrieved by the query
    */
GRIDDYN_EXPORT void
    gridDynVectorQueryRun(GridDynVectorQuery query, double* data, int N, GridDynError* err);

/** add additional measurements to a vector query
    @param[in] query the vector query object to append measurements
    @param[in] obj the object to get the measurement from
    @param[in] queryString a string describing the new queries to add
    */
GRIDDYN_EXPORT void gridDynVectorQueryAppend(GridDynVectorQuery query,
                                             GridDynObject obj,
                                             const char* queryString,
                                             GridDynError* err);
/** change the data a single query gets
    @param[in] query the single query object to alter
    @param[in] obj the object to get the measurement from
    @param[in] queryString a string describing the new queries to add
    */
GRIDDYN_EXPORT void gridDynSingleQueryUpdate(GridDynSingleQuery query,
                                             GridDynObject obj,
                                             const char* queryString,
                                             GridDynError* err);

/** change the data a vector query gets
    @param[in] query the vector query object to alter
    @param[in] obj the object to get the measurement from
    @param[in] queryString a string describing the new queries
    */
GRIDDYN_EXPORT void gridDynVectorQueryUpdate(GridDynVectorQuery query,
                                             GridDynObject obj,
                                             const char* queryString,
                                             GridDynError* err);

// functions for handling events

/** create an event for use on GridDyn
    @param[in] eventString the description string for the event
    @param[in] obj the target object of the event can be nullptr
    @return a GridDynEvent object used in the other functions
    */
GRIDDYN_EXPORT GridDynEvent gridDynEventCreate(const char* eventString,
                                               GridDynObject obj,
                                               GridDynError* err);
/** delete the event
    @param[in] evnt  the event object to be freed
    */
GRIDDYN_EXPORT void gridDynEventFree(GridDynEvent evnt);

/** trigger the action described by the event
    @param[in] evnt the event object to perform the action on
    @return 0 for success or an error code representing an issue
    */
GRIDDYN_EXPORT void gridDynEventTrigger(GridDynEvent evnt, GridDynError* err);

/** trigger the action described by the event
    @param[in] evnt the event object to perform the action on
    @param[in] sim the simulation object to schedule the event on
    @return 0 for success or an error code representing an issue
    */
GRIDDYN_EXPORT void
    gridDynEventSchedule(GridDynEvent evnt, GridDynSimulation sim, GridDynError* err);

/** trigger the action described by the event
    @param[in] evnt the event object to perform the action on
    @param[in] parameter the parameter to change
    @param[in] value the new value to set for the parameter
    @return 0 for success or an error code representing an issue
    */
GRIDDYN_EXPORT void
    gridDynEventSetValue(GridDynEvent evnt, const char* parameter, double value, GridDynError* err);

/** trigger the action described by the event
    @param[in] evnt the event object to perform the action on
    @param[in] parameter the parameter to change
    @param[in] value the new value to set for the parameter
    @return 0 for success or an error code representing an issue
    */
GRIDDYN_EXPORT void gridDynEventSetString(GridDynEvent evnt,
                                          const char* parameter,
                                          const char* value,
                                          GridDynError* err);

/** trigger the action described by the event
    @param[in] evnt the event object to perform the action on
    @param[in] flag the name of the flag to change
    @param[in] val the value to set the flag to (0 for false, otherwise for true)
    @return 0 for success or an error code representing an issue
    */
GRIDDYN_EXPORT void
    gridDynEventSetFlag(GridDynEvent evnt, const char* flag, int val, GridDynError* err);

/** trigger the action described by the event
    @param[in] evnt the event object to perform the action on
    @param[in] obj the object for the event to act upon
    @return 0 for success or an error code representing an issue
    */
GRIDDYN_EXPORT void gridDynEventSetTarget(GridDynEvent evnt, GridDynObject obj, GridDynError* err);

/** @} */  // end of the C-api group

#ifdef __cplusplus
} /* end of extern "C" { */
#endif

#endif  // ___W_GRIDDYN_GRIDDYN_SRC_GRIDDYN_SHARED_GRIDDYN_EXPORT_H_
