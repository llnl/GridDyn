/*
 * Copyright (c) 2014-2026, Lawrence Livermore National Security
 * See the top-level NOTICE for additional details. All rights reserved.
 * SPDX-License-Identifier: BSD-3-Clause
 */

#pragma once

#include "../core/coreObject.h"
#include "gridComponentHelperClasses.h"
#include "offsetTable.h"
#include <bitset>
#include <string>
#include <utility>
#include <vector>

template<class Y>
class matrixData;

/** the primary namespace for the griddyn library*/
namespace griddyn {
// forward declare the template class matrixData

class Violation;

/** @brief base object for gridDynSimulations
 * the basic object for creating a power system encapsulating some common functions and data that is
 *needed by all objects in the simulation and defining some common methods for use by all objects.
 *This object is not really intended to be instantiated directly and is mostly a common interface to
 *inheriting objects @see gridPrimary,
 *@see gridSecondary, and @see GridSubModel as it encapsulated common functionality between those
 *objects
 **/
class GridComponent: public CoreObject {
  protected:
    std::bitset<64> opFlags;  //!< operational flags these flags are designed to be normal false
                              //!< @see ::operation_flags
    offsetTable offsets;  //!< a table of offsets for the different solver modes
    count_t m_inputSize = 0;  //!< the required size of the inputs input
    count_t m_outputSize = 0;  //!< the number of outputs the subModel produces
    model_parameter systemBaseFrequency =
        kWS;  //!<[radians per second] the base frequency of the system default to 60Hz
    model_parameter systemBasePower = 100;  //!<[MVA] base power for all PU computations
    model_parameter localBaseVoltage = kNullVal;  //!< [kV] base voltage for object

    std::vector<double> m_state;  //!< storage location for internal state
    std::vector<double> m_dstate_dt;  //!< storage location for internal state differential
  private:
    objVector<GridComponent*> subObjectList;  //!< a vector of all the subObjects;
    // stringVec stateNames;           //!<a vector with the names of the states
  public:
    /** @brief default constructor*/
    explicit GridComponent(const std::string& objName = "");
    /** @brief virtual destructor*/
    virtual ~GridComponent() override;
    virtual CoreObject* clone(CoreObject* obj = nullptr) const override;
    /** @brief update internal object linkages to use objects from a new tree
    @details after a clone call on a full simulation
    it is possible that there could be internal linkages inside of objects still pointing to the
    original tree This function is intended to allow an object to update any internal object
    pointers so they point appropriately to new object inside the new tree The call should really
    only be initiated by a root object
    @param[in] newRoot the root of the new simulation tree
    */
    virtual void updateObjectLinkages(CoreObject* newRoot);

    /** @brief initialize object for power flow part A
    after part A of the initialization the object should know how many states it has as part of the
    powerflow, before this calling the load sizes powerflow related solverModes will be unspecified.
    This function is a wrapper around the pFlowObjectInitializeA function which does the local
    object init this function handles any global setup and makes sure all the flags are set properly
    @param[in] time0 the time0 at which the power flow will take place
    @param[in] flags  any flags indicating how the initialization or execution will take place
    @throw an error if something went wrong
    */
    virtual void pFlowInitializeA(coreTime time0, std::uint32_t flags);

    /** @brief initialize object for power flow part B
    partB is to actually initialize the object so an initial guessState will be meaningful,  many
    objects just do everything in part A if there is no need to separate the functions This function
    is a wrapper around the pFlowObjectInitializeB function which does the local object init this
    function handles any global setup and makes sure all the flags are set properly
    */
    virtual void pFlowInitializeB();

    /** @brief initialize object for dynamic simulation part A
    after part A of the initialization the object should know how many states it has as part of the
    dynamic simulation, before this calling the load sizes dynamic related solverModes will be
    unspecified. This function is a wrapper around the dynObjectInitializeA function which does the
    local object init this function handles any global setup and makes sure all the flags are set
    properly
    @param[in] time0 the time0 at which the power flow will take place
    @param[in] flags  any flags indicating how the initialization or execution will take place
    */
    virtual void dynInitializeA(coreTime time0, std::uint32_t flags);

    /** @brief initialize object for dynamic simulation part B
    partB is to actually initialize the object so an initial guessState will be meaningful,  many
    objects just do everything in part A if there is no need to separate the functions This function
    is a wrapper around the dynObjectInitializeB function which does the local object init this
    function handles any global setup and makes sure all the flags are set properly
    @param[in]  desiredOutput the desired output of the gridPrimary
    */
    virtual void
        dynInitializeB(const IOdata& inputs, const IOdata& desiredOutput, IOdata& fieldSet);

  protected:  // don't allow these functions to be called directly in the public interface
    /** @brief initialize local object for power flow part A
    see GridComponent::pFlowInitializeA for more details
    @param[in] time0 the time0 at which the power flow will take place
    @param[in] flags  any flags indicating how the initialization or execution will take place
    */
    virtual void pFlowObjectInitializeA(coreTime time0, std::uint32_t flags);

    /** @brief initialize local object for power flow part B
    see GridComponent::pFlowInitializeB for more details
    */
    virtual void pFlowObjectInitializeB();
    /** @brief initialize local object for dynamics part A
see GridComponent::dynInitializeA for more details
@param[in] time0 the time at which the power flow will take place
@param[in] flags  any flags indicating how the initialization or execution will take place
*/
    virtual void dynObjectInitializeA(coreTime time0, std::uint32_t flags);

    /** @brief initialize local object for dynamics part B
    see GridComponent::dynInitializeB for more details
    @param[in] inputs the initial inputs to an object (can be empty)
    @param[in] desiredOutput the desired initial output of the component
    @param[in] fieldSet either the computed inputs or outputs depending on which was defined
    */
    virtual void
        dynObjectInitializeB(const IOdata& inputs, const IOdata& desiredOutput, IOdata& fieldSet);

  public:
    virtual void getParameterStrings(stringVec& pstr,
                                     paramStringType pstype = paramStringType::all) const override;
    virtual void set(std::string_view param, std::string_view val) override;

    virtual void
        set(std::string_view param, double val, units::unit unitType = units::defunit) override;
    /** check if the parameter being set is for a subobject, determine which sub object and perform
     * the set operation*/
    bool subObjectSet(std::string_view param, double val, units::unit unitType);
    /** check if the parameter being set is for a subobject, determine which sub object and perform
     * the set operation*/
    bool subObjectSet(std::string_view param, std::string_view val);
    /** check if the parameter being set is for a subobject, determine which sub object and perform
     * the setFlag operation*/
    bool subObjectSet(std::string_view flag, bool val);
    virtual void setFlag(std::string_view flag, bool val = true) override;
    /** there are few flags that parents should be able to set in their children, this function
    allows that to take place
    @param[in] flagID  the numerical location of the flag
    @param[in] val the value to set the flag too
    @param[in] checkParent the identifier of the parent, it must match the objects parent
    */
    void parentSetFlag(index_t flagID, bool val, CoreObject* checkParent);
    virtual bool getFlag(std::string_view flag) const override;
    virtual double get(std::string_view param,
                       units::unit unitType = units::defunit) const override;
    /** check if the parameter being get is for a subobject, determine which sub object and perform
    the setFlag operation
    @param[in] param the subobject string to query the value of
    @param[in] unitType the type of units to convert the output to
    @return a value corresponding to the request or a null value
    */
    double subObjectGet(std::string_view param, units::unit unitType) const;
    /** add a grid object to the subObject container
    @param[in] comp the component to add
    */
    void addSubObject(GridComponent* comp);
    /** remove a grid object to the subObject container
    @param[in] obj the component to remove
    */
    void removeSubObject(GridComponent* obj);
    /** replace a grid object to the subObject container
    @param[in] newObj the component to add
    @param[in] oldObj the component to replace
    */
    void replaceSubObject(GridComponent* newObj, GridComponent* oldObj);

    virtual void remove(CoreObject* obj) override;
    /** @brief method for checking a specific known flag
    @param[in] flagID the index of the flag to check
    @return boolean indicator of the flag
    */
    bool checkFlag(index_t flagID) const;

    /** @brief get the set of cascading flags
    @return an uint64_t with all non-cascading flags masked out
    */
    inline std::uint64_t cascadingFlags() const { return (opFlags.to_ullong() & flagMask); }

    virtual CoreObject* find(std::string_view object) const override;

    virtual CoreObject* getSubObject(std::string_view typeName, index_t objectNum) const override;

    virtual CoreObject* findByUserID(std::string_view typeName, index_t searchID) const override;

    /** @brief set the offsets of an object for a particular solver mode using a single offset.
    @param[in] newOffset the offset index all variables are sequential.
    @param sMode the solver mode to use.
    */
    virtual void setOffset(index_t newOffset, const solverMode& sMode);

    /** @brief set the offsets of an object for a particular solver mode using a single offset.
    @param newOffsets the offset index all variables are sequential.
    @param sMode the solver mode to use.
    */
    virtual void setOffsets(const solverOffsets& newOffsets, const solverMode& sMode);

    /** @brief get a single state value
    @param offset the offset index: all state variables are sequential.
    @return the value of the state in question
    */
    virtual double getState(index_t offset) const;

    /** @brief get a vector reference to the local states
     */
    const std::vector<double>& getStates() const { return m_state; }
    /** @brief get a vector reference to the local states derivatives
     */
    const std::vector<double>& getDerivs() const { return m_dstate_dt; }
    /** @brief set the offsets for root finding
    @param[in] newRootOffset the offset index all variables are sequential.
    @param[in] sMode the solver mode to use.
    */
    virtual void setRootOffset(index_t newRootOffset, const solverMode& sMode);

    /** @brief get the number of states
    @param[in] sMode  the solverMode to get the stateSize for
      @return the number of states
    */
    count_t stateSize(const solverMode& sMode);

    /** @brief get the total number of Algebraic States algSize+vSize+aSize
    @param[in] sMode solverMode to base the count on
    @return the number of algebraic states
    */
    count_t totalAlgSize(const solverMode& sMode);

    /** @brief get the number of Jacobian non-zeros elements (max not necessarily actual)
    @param[in] sMode solverMode to base the count on
    @return the number of non-zeros in the Jacobian
    */
    count_t jacSize(const solverMode& sMode);
    /** @brief get the number of roots
    @param[in] sMode solverMode to base the count on
    @return the number of roots
    */
    count_t rootSize(const solverMode& sMode);
    /** @brief get the number of algebraic states not including voltage and angle states
    @param[in] sMode solverMode to base the count on
    @return the number of algebraic states
    */
    count_t algSize(const solverMode& sMode);
    /** @brief get the number of differential states
     @param[in] sMode solverMode to base the count on
    @return the number of differential states
    */
    count_t diffSize(const solverMode& sMode);
    /** @brief get the number of voltage states
    @param[in] sMode solverMode to base the count on
    @return the number of voltage states
    */
    count_t voltageStateCount(const solverMode& sMode);
    /** @brief get the number of angle states
    @param[in] sMode  solverMode to base the count on
    @return the number of angle states
    */
    count_t angleStateCount(const solverMode& sMode);

    /** @brief get the number of states
    @param[in] sMode  the solverMode to get the stateSize for
    @return the number of states
    */
    count_t stateSize(const solverMode& sMode) const;

    /** @brief get the total number of Algebraic States algSize+vSize+aSize
    @param[in] sMode solverMode to base the count on
    @return the number of algebraic states
    */
    count_t totalAlgSize(const solverMode& sMode) const;

    /** @brief get the number of Jacobian non-zeros elements (max not necessarily actual)
    @param[in] sMode solverMode to base the count on
    @return the number of non-zeros in the Jacobian
    */
    count_t jacSize(const solverMode& sMode) const;
    /** @brief get the number of roots
    @param[in] sMode solverMode to base the count on
    @return the number of roots
    */
    count_t rootSize(const solverMode& sMode) const;
    /** @brief get the number of algebraic states not including voltage and angle states
    @param[in] sMode solverMode to base the count on
    @return the number of algebraic states
    */
    count_t algSize(const solverMode& sMode) const;
    /** @brief get the number of differential states
    @param[in] sMode solverMode to base the count on
    @return the number of differential states
    */
    count_t diffSize(const solverMode& sMode) const;
    /** @brief get the number of voltage states
    @param[in] sMode solverMode to base the count on
    @return the number of voltage states
    */
    count_t voltageStateCount(const solverMode& sMode) const;
    /** @brief get the number of angle states
    @param[in] sMode  solverMode to base the count on
    @return the number of angle states
    */
    count_t angleStateCount(const solverMode& sMode) const;

    /** @brief get the number of outputs
    @return the number of outputs
    */
    count_t numOutputs() const { return m_outputSize; }
    /** @brief get the number of inputs
    @return the number of inputs
    */
    count_t numInputs() const { return m_inputSize; }

    /** @brief function to get a constant pointer to the object offsets
     * @param[in] sMode the mode to get the offsets for
     * return a const pointer to the solver Offsets
     */
    const solverOffsets& getOffsets(const solverMode& sMode) const;

    /** @brief checks if the object state sizes are loaded
    @param[in] sMode  the solverMode to get the stateSize for
    @return boolean true if the object is loaded false if not
    */
    bool isStateCountLoaded(const solverMode& sMode) const;

    /** @brief checks if the object state sizes are loaded
    @param[in] sMode  the solverMode to get the stateSize for
    @return boolean true if the object is loaded false if not
    */
    bool isJacobianCountLoaded(const solverMode& sMode) const;

    /** @brief checks if the object state sizes are loaded
    @param[in] sMode  the solverMode to get the stateSize for
    @return boolean true if the object is loaded false if not
    */
    bool isRootCountLoaded(const solverMode& sMode) const;
    /** @brief convenience function for checking armed status
    @return boolean true if the object is armed false if not
    */
    bool isArmed() const;
    virtual bool isCloneable() const override;
    /** @brief convenience function for checking if the object has states
    @param[in] sMode  the solverMode to check
    @return boolean true if the object is armed false if not
    */
    bool hasStates(const solverMode& sMode) const;

    /** @brief function for checking connected status
    @return boolean true if the object is connect false if not
    */
    virtual bool isConnected() const;
    /** @brief trigger a reconnect operation
     */
    virtual void reconnect();
    /** @brief trigger a disconnect
     */
    virtual void disconnect();

    virtual void alert(CoreObject*, int code) override;

    /** @brief common function to load some flags appropriate to pFlow.
     */
    void setupPFlowFlags();
    /** @brief common function to load some flags appropriate to dynamic simulation.
     */
    void setupDynFlags();
    /** @brief compute the local sizes
    @param sMode the solver mode to use.
    @return a stateSizes object containing the various segment sizes
    */
    virtual stateSizes LocalStateSizes(const solverMode& sMode) const;

    /** @brief compute the local Jacobian count
    @param sMode the solver mode to use.
    @return a stateSizes object containing the various segment sizes
    */
    virtual count_t LocalJacobianCount(const solverMode& sMode) const;

    /** @brief compute the local root count
    @param sMode the solver mode to use.
    @return a pair containing the local root counts <algebraic, differential>
    */
    virtual std::pair<count_t, count_t> LocalRootCount(const solverMode& sMode) const;

    /** @brief compute the sizes and store them in the offsetTables.
    @param sMode the solver mode to use.
    */
    virtual void loadStateSizes(const solverMode& sMode);
    /** @brief compute the sizes and store them in the offsetTables.
    @param sMode the solver mode to use.
    */
    virtual void loadJacobianSizes(const solverMode& sMode);

    /** @brief compute the sizes and store them in the offsetTables.
    @param sMode the solver mode to use.
    */
    virtual void loadRootSizes(const solverMode& sMode);

  protected:
    enum class sizeCategory {
        state_size_update,
        jacobian_size_update,
        root_size_update,
    };
    /** @brief compute the sizes of the subObject and store them in the offsetTables.
    @param sMode the solver mode to use.
    @param category set to the type of size data being loaded
    */
    void loadSizesSub(const solverMode& sMode, sizeCategory category);

  public:
    /** @brief reset the object
     * reset any internal states to a base level depending on the level
     * @param[in] level  the level of the reset
     */
    virtual void reset(reset_levels level = reset_levels::minimal);

    /** @brief transfer a computed state to the objects
    @param time -the time the state corresponds to
    @param state -- a double array pointing to the state information
    @param dstate_dt a double array pointing to the state derivative information (not necessary for
    states with no corresponding time derivative
    @param sMode  -- the solverMode corresponding to the computed state.
    */
    virtual void setState(coreTime time,
                          const double state[],
                          const double dstate_dt[],
                          const solverMode& sMode);
    /** @brief transfer state information from the objects to a vector
    @param time -the time the state corresponds to
    @param[out] state -- a double array pointing to the state information
    @param[out] dstate_dt a double array pointing to the state derivative information (not necessary
    for states with no corresponding time derivative
    @param sMode  -- the solverMode corresponding to the computed state.
    */
    virtual void
        guessState(coreTime time, double state[], double dstate_dt[], const solverMode& sMode);
    /** @brief load tolerance information from the objects
    @param[out] tols -- a double array with the state tolerance information
    @param[in] sMode  -- the solverMode corresponding to the computed state.
    */
    virtual void getTols(double tols[], const solverMode& sMode);
    /** @brief load variable information 1 for algebraic state 0 for differential state
    @param[out] sdata -- a double array with the state tolerance information
    @param[in] sMode  -- the solverMode corresponding to the computed state.
    */
    virtual void getVariableType(double sdata[], const solverMode& sMode);

    /**@brief get a reference to the vector of subObjects*/
    const objVector<GridComponent*>& getSubObjects() const { return subObjectList; }
    /** @brief load constraint information
     0 for no constraints
    1 for > 0
    -1 for <0
    2 for >=0
    -2 for <=0
    @param[out] constraints -- a double array with the constraint
    @param[in] sMode  -- the solverMode corresponding to the computed state.
    */
    virtual void getConstraints(double constraints[], const solverMode& sMode);
    /** @brief update cascading flag information
    @param[in] dynamicsFlags if true only do so for flags corresponding to dynamic solution
    */
    virtual void updateFlags(bool dynamicsFlags = true);

    /** @brief get the names for all the states
    @param[out] stNames -- the output state names
    @param[in] sMode  -- the solverMode corresponding to the computed state.
    @param[in] prefix  a string prefix to put before the state names of the object-- intended for
    cascading calls
    */
    virtual void getStateName(stringVec& stNames,
                              const solverMode& sMode,
                              const std::string& prefix = "") const;

    /**brief update any local cached information about a particular state/input set
    @param[in] inputs the input inputs
    @param[in] stateDataValue  the stage data to cache information from
    @param[in] sMode the solverMode corresponding to the stateData
    */
    virtual void updateLocalCache(const IOdata& inputs,
                                  const stateData& stateDataValue,
                                  const solverMode& sMode);
    /** @brief locate a state index based on field name
    @param[in] field the name of the field to search for
    @param[in] sMode the solverMode to find the location for
    @return the index of the state  some number if valid  kInvalidLocation if not found,
    kNullLocation if not initialized yet(try again later)
    */
    virtual index_t findIndex(std::string_view field, const solverMode& sMode) const;

    /**
     *@brief set all the values of particular type of object to some value
     * @param[in] type the type of unit to set
     * @param[in] param the parameter to set
     * @param[in] val the value to set it to
     * @param[in] unitType the units of the val
     */
    virtual void setAll(std::string_view type,
                        std::string_view param,
                        double val,
                        units::unit unitType = units::defunit);
    /** @brief get the local state names
    used within a couple functions to automate the population of the state names and finding of the
    indices  states should be algebraic states first,  then differential states
    @return a string vector of the local state names
    */
    virtual stringVec localStateNames() const;
    /** @brief get a vector of input Names
    @details the return data is a vector of vectors of string the first element of each vector is
    the typical input Name the others are alternatives
    @return a const vector of stringVec containing the names +alternatives for the inputs*/
    virtual const std::vector<stringVec>& inputNames() const;

    /** @brief get the unit type for a particular input
    @param[in] inputNum the index of the input to get the units for
    @return a unit value corresponding to the particular input*/
    virtual units::unit inputUnits(index_t inputNum) const;

    /** @brief get the unit type for a particular output
   @param[in] outputNum the index of the output to get the units for
    @return a unit value corresponding to the particular output*/
    virtual units::unit outputUnits(index_t outputNum) const;

    /** get the systemBasePower*/
    model_parameter basePower() const { return systemBasePower; }

    /** get the localBaseVoltage*/
    model_parameter baseVoltage() const { return localBaseVoltage; }
    /** @brief get a vector of output Names
    @details the return data is a vector of vectors of string the first element of each vector is
    the typical output Name the others are alternatives
    @return a const vector of stringVec containing the names +alternatives for the outputs*/
    virtual const std::vector<stringVec>& outputNames() const;

    /******************************************
    Functions for mathematical updates
    *******************************************/
    // residual computation
    /** @brief compute the residual for a given state
    @param[in] inputs  the input arguments
    @param[in] stateDataValue the data representing the current state to operate on
    @param[out] resid the array to store the residual values in
    @param[in] sMode the solverMode which is being solved for
    */
    virtual void residual(const IOdata& inputs,
                          const stateData& stateDataValue,
                          double resid[],
                          const solverMode& sMode);

    /** @brief compute an update to all the algebraic variables in the object
    @param[in] inputs  the input arguments
    @param[in] stateDataValue the data representing the current state to operate on
    @param[out] update the array to store the computed state values
    @param[in] sMode the solverMode which is being solved for
    @param[in] alpha the convergence gain
    */

    virtual void algebraicUpdate(const IOdata& inputs,
                                 const stateData& stateDataValue,
                                 double update[],
                                 const solverMode& sMode,
                                 double alpha);

    /** @brief compute the time derivative for a given state
    @param[in] inputs  the input arguments
    @param[in] stateDataValue the data representing the current state to operate on
    @param[out] deriv the array to store the computed derivative values
    @param[in] sMode the solverMode which is being solved for
    */
    virtual void derivative(const IOdata& inputs,
                            const stateData& stateDataValue,
                            double deriv[],
                            const solverMode& sMode);
    /**
    *@brief compute the partial derivatives of the internal states with respect to inputs and other
    internal states
    @param[in] inputs the inputs for the secondary object
     * @param[in] stateDataValue the current state data for the simulation
    * @param[out] matrixDataValue  the array to store the information in
    * @param[in] inputLocs the vector of input argument locations
    * @param[in] sMode the operations mode
    **/
    virtual void jacobianElements(const IOdata& inputs,
                                  const stateData& stateDataValue,
                                  matrixData<double>& matrixDataValue,
                                  const IOlocs& inputLocs,
                                  const solverMode& sMode);

    // for the stepwise dynamic system
    /** @brief move the object forward in time using local calculations
    tells the object to progress time to a certain point the sMode is guidance on how to do it not
    necessarily indicative of a particular solver it is meant as a suggestion not a requirement.
    @param[in] time the time to progress to
    @param[in] inputs  the input arguments
    @param[in] sMode the solverMode to give guidance to objects on how to perform internal
    calculations
    */
    virtual void timestep(coreTime time, const IOdata& inputs, const solverMode& sMode);

    /**
    *@brief compute the partial derivatives of the output states with respect to internal states
    @param[in] inputs the inputs for the secondary object
    * @param[in] stateDataValue the current state data for the simulation
    * @param[out] matrixDataValue  the array to store the information in
    * @param[in] sMode the operations mode
    **/
    virtual void outputPartialDerivatives(const IOdata& inputs,
                                          const stateData& stateDataValue,
                                          matrixData<double>& matrixDataValue,
                                          const solverMode& sMode);
    /**
    @brief return the count of output dependencies on internal states
    @param[in] outputNum  the index of the output to query
    @param[in] sMode the solver mode to consider
    @return the count of the output Dependencies
    */
    virtual count_t outputDependencyCount(index_t outputNum, const solverMode& sMode) const;
    /**
     * @brief compute the partial derivatives of the output states with respect to inputs
     * @param[in] inputs the inputs for the secondary object
     * @param[in] sD the current state data for the simulation
     * @param[out] md  the array to store the information in
     * @param[in] inputLocs the vector of input argument locations
     * @param[in] sMode the operations mode
     **/
    virtual void ioPartialDerivatives(const IOdata& inputs,
                                      const stateData& sD,
                                      matrixData<double>& md,
                                      const IOlocs& inputLocs,
                                      const solverMode& sMode);

    /** @brief call any objects that need 2 part execution to allow for parallelism
    do any pre-work for a residual call later in the calculations
    secondary objects do not allow partial computation like primary objects can so there is no
    delayed computation calls just the regular call and the primary object will handle delaying
    action.
    @param[in] inputs  the input arguments
    @param[in] stateDataValue the data representing the current state to operate on
    @param[in] sMode the solverMode which is being solved for
    */
    virtual void
        preEx(const IOdata& inputs, const stateData& stateDataValue, const solverMode& sMode);

    /******************************************
    Functions related to root finding
    *******************************************/
    /**
    *evaluate the root functions and return the value
    @param[in] inputs the inputs for the secondary object
    * @param[in] stateDataValue the state of the system
    * @param[out] roots the memory to store the root evaluation
    * @param[in] sMode the mode the solver is in
    **/
    virtual void rootTest(const IOdata& inputs,
                          const stateData& stateDataValue,
                          double roots[],
                          const solverMode& sMode);

    /**
    *a root has occurred now take action
    * @param[in] time the simulation time the root evaluation takes place
    @param[in] rootMask a vector of integers representing a rootMask  (only object having a value of
    1 in their root locations should actually trigger
    * @param[in] sMode the mode the solver is in
    **/
    virtual void rootTrigger(coreTime time,
                             const IOdata& inputs,
                             const std::vector<int>& rootMask,
                             const solverMode& sMode);

    /**
    *evaluate the root functions and execute trigger from a static state for operation after an
    initial condition check
    @param[in] inputs the inputs for the secondary object
    * @param[in] stateDataValue the state of the system
    * @param[in] sMode the mode the solver is in
    @param[in] level the level of root to check for
    **/
    virtual change_code rootCheck(const IOdata& inputs,
                                  const stateData& stateDataValue,
                                  const solverMode& sMode,
                                  check_level_t level);
    /******************************************
    output functions
    *******************************************/
    /**
    *@brief get a vector of all outputs
    @param[in] inputs the inputs for the secondary object
    * @param[in] stateDataValue the current state data for the simulation
    * @param[in] sMode the mode the solver is in
    @return a vector containing  all the outputs
    **/
    virtual IOdata getOutputs(const IOdata& inputs,
                              const stateData& stateDataValue,
                              const solverMode& sMode) const;

    /**
    *@brief get the time derivative of a single state
    @param[in] inputs the inputs for the secondary object
    * @param[in] stateDataValue the current state data for the simulation
    * @param[in] sMode the mode the solver is in
    @param[in] outputNum the index of the output derivative being requested
    @return the value of the time derivative of a state being requested
    **/
    virtual double getDoutdt(const IOdata& inputs,
                             const stateData& stateDataValue,
                             const solverMode& sMode,
                             index_t outputNum = 0) const;

    /**
    *@brief get a single output
    @param[in] inputs the inputs for object
    * @param[in] stateDataValue the current state data for the simulation
    * @param[in] sMode the mode the solver is in
    @param[in] outputNum the index of the output being requested
    @return the value of the state being requested
    **/
    virtual double getOutput(const IOdata& inputs,
                             const stateData& stateDataValue,
                             const solverMode& sMode,
                             index_t outputNum = 0) const;

    /**
    @brief find the index of an output given its name
    @param[in] outputName the name of the output
    @return the index number of the output
    */
    index_t lookupOutputIndex(std::string_view outputName) const;
    /**
    *@brief get a single output based on local data
    @param[in] outputNum the index of the output being requested
    @return the value of the state being requested
    **/
    virtual double getOutput(index_t outputNum = 0) const;
    /**
    *@brief get a single output and location
    @ details used in cases where the state of one object is used int the computation of another for
    computation of the Jacobian
    * @param[in] sMode the mode the solver is in
    @param[in] outputNum the number of the state being requested
    @return the value of the state requested
    **/
    virtual index_t getOutputLoc(const solverMode& sMode, index_t outputNum = 0) const;
    /**
    *@brief get a vector state indices for the output
    @ details used in cases where the state of one object is used int the computation of another for
    computation of the Jacobian
    * @param[in] sMode the mode the solver is in
    @return a vector containing  all the outputs locations,  kNullLocation if there is no state
    representing the output
    **/
    virtual IOlocs getOutputLocs(const solverMode& sMode) const;

    /******************************************
    functions for setting and querying defined parameters
    *******************************************/
    /** set a defined parameter for an object
    @param[in] param the index of the parameter to set
    @param[in] value the value to set the parameter
    */
    virtual void setParameter(index_t param, double value);
    /** set a defined parameter for an object
    @param[in] param the index of the parameter to set
    @return the current value of the parameter
    */
    virtual double getParameter(index_t param) const;

    /**
    *@brief compute the partial derivatives of the residuals with respect to a parameter
    @details an object may define some number of key parameters to perform particular calculations
    on for sensitivity analysis or other calculations
    @param[in] param the index code of the parameter to set in an object
    @param[in] val the updated value of the parameter to use as the basis for the calculations
    @param[in] inputs the inputs for the secondary object
    * @param[in] sD the current state data for the simulation
    * @param[out] md  the array to store the information in
    * @param[in] sMode the operations mode
    **/
    virtual void parameterPartialDerivatives(index_t param,
                                             double val,
                                             const IOdata& inputs,
                                             const stateData& sD,
                                             matrixData<double>& md,
                                             const solverMode& sMode);
    /**
    *@brief compute the partial derivatives of an outputs with respect to a parameter
     @param[in] param the index code of the parameter to set in an object
    @param[in] val the updated value of the parameter to use as the basis for the calculations
    @param[in] outputNum the index of the output to perform the calculations for
    @param[in] inputs the inputs for the secondary object
    @param[in] sD the current state data for the simulation
    * @param[in] sMode the operations mode
    **/
    virtual double parameterOutputPartialDerivatives(index_t param,
                                                     double val,
                                                     index_t outputNum,
                                                     const IOdata& inputs,
                                                     const stateData& sD,
                                                     const solverMode& sMode);
    /****
    other items
    */

    /** @brief adjust the power flow solution if needed
    @param[in] inputs the inputs for the secondary object
    @param[in] flags for suggesting how the object handle the adjustments
    * @param[in] level  the level of the adjustments to perform
    */
    virtual change_code
        powerFlowAdjust(const IOdata& inputs, std::uint32_t flags, check_level_t level);
    friend class offsetTable;
};

/** @brief display all the state names to the screen
@param comp the object to display the states for
@param sMode the mode which to display the states for
*/
void printStateNames(const GridComponent* comp, const solverMode& sMode);

}  // namespace griddyn
