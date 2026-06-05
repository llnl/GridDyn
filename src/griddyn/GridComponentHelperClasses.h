/*
 * Copyright (c) 2014-2026, Lawrence Livermore National Security
 * See the top-level NOTICE for additional details. All rights reserved.
 * SPDX-License-Identifier: BSD-3-Clause
 */

#pragma once

/** @file
 * @brief classes and object to help in the operation of gridComponents
 */

#include "solvers/SolverMode.hpp"
#include <bitset>

namespace griddyn {
constexpr static std::uint64_t flagMask = 0x3FE;  //!< general flag mask for convenience to mask out
                                                  //!< flags that typically cascade to parents

/** @brief  an enumeration of flags in the opFlags bitset for GridComponent
@details flags are intended to be default off hence the various names in certain circumstances
 */
enum OperationFlags {
    // indicator flags 0 -15 are general indicators flags which default false

    // typically cascading flags
    HAS_CONSTRAINTS = 0,  //!< flag indicating if an object uses constraints
    HAS_ROOTS = 1,  //!< flag indicating if an object uses root finding
    HAS_ALG_ROOTS =
        2,  //!< flag indicated the object has roots dependent on algebraic states and thus must be
    //!< evaluated after a initial condition update
    HAS_POWERFLOW_ADJUSTMENTS =
        3,  //!< flag indicating if an object has voltage adjustments for power flow
    PRE_EX_REQUESTED = 4,  //!< flag indicating if an object requests pre-execution
    USES_BUS_FREQUENCY = 5,  //!< flag indicating if an object uses bus frequency calculation
    HAS_PFLOW_STATES = 6,  //!< indicator if the object has power flow states if in question
    HAS_DYN_STATES = 7,  //!< indicator if the object has dynamic states if in question
    HAS_DIFFERENTIAL_STATES = 8,  //!< indicator if the object has differential states
    NOT_CLONEABLE = 9,  //!< flag indicating that an object should not be cloned
    EXTRA_CASCADING_FLAG = 10,  //!< reserved for future use
    // end of typically cascading flags

    // for handling remote voltage control capabilities
    REMOTE_VOLTAGE_CONTROL =
        11,  //!< indicator that the object controls a remote bus voltage at a specific level
    LOCAL_VOLTAGE_CONTROL =
        12,  //!< indicator that the object controls the local bus voltage at a specific level
    INDIRECT_VOLTAGE_CONTROL =
        13,  //!< flag indicating the object must use indirect means to control the voltage
    ADJUSTABLE_Q =
        14,  //!< flag indicating that an object has controllable reactive power for power flow
    // for handling remote power control capabilities
    REMOTE_POWER_CONTROL =
        15,  //!< indicator that the object controls a remote bus voltage at a specific level
    LOCAL_POWER_CONTROL =
        16,  //!< indicator that the object controls the local bus voltage at a specific level
    INDIRECT_POWER_CONTROL =
        17,  //!< flag indicating the object must use indirect means to control the voltage
    ADJUSTABLE_P = 18,  //!< flag indicating that the object has adjustable power setting and can be
                        //!< used by slack
    //! bus for control of angle

    // some indicator flags for local objects
    POWERFLOW_INITIALIZED = 19,  //!< indicator that powerFlow initialization has been completed
    DYN_INITIALIZED = 20,  //!<  indicator that dynamic Initialization has been completed
    OBJECT_ARMED_FLAG =
        21,  //!< basically an extra object flag if the object has a trigger mechanism of some sort
    LATE_B_INITIALIZE =
        22,  //!< flag indicating the object would like to be initialized after most other objects
    //! only acknowledged by areas and then only within the area
    ERROR_FLAG = 23,  //!< flag indicating the object has an error

    // flags  24- 31 indicating some sort of condition change
    STATE_CHANGE_FLAG = 24,  //!< flag indicating that the state size or nature has changed
    OBJECT_CHANGE_FLAG = 25,  //!< flag indicating that an object has changed activity state
    CONSTRAINT_CHANGE_FLAG = 26,  //!< flag indicating an change in constraint values
    ROOT_CHANGE_FLAG = 27,  //!< flag indicating a change in the root finding functions
    JACOBIAN_COUNT_CHANGE_FLAG = 28,  //!< flag indicating a change in the Jacobian count
    SLACK_BUS_CHANGE_FLAG = 29,  //!< flag indicating a change in the slack bus
    VOLTAGE_CONTROL_CHANGE_FLAG = 30,  //!< flag indicating a change in voltage control on a bus
    CONNECTIVITY_CHANGE_FLAG = 31,  //!< flag indicating a change in bus connectivity possibly
                                    //!< indicating islanding or isolated buses

    /*flags 32-44 are intended for local object usage*/
    OBJECT_FLAG1 = 32,
    OBJECT_FLAG2 = 33,
    OBJECT_FLAG3 = 34,
    OBJECT_FLAG4 = 35,
    OBJECT_FLAG5 = 36,
    OBJECT_FLAG6 = 37,
    OBJECT_FLAG7 = 38,
    OBJECT_FLAG8 = 39,
    OBJECT_FLAG9 = 40,
    OBJECT_FLAG10 = 41,
    OBJECT_FLAG11 = 42,
    OBJECT_FLAG12 = 43,

    // flags 43 - 45 state control
    NO_POWERFLOW_OPERATIONS =
        44,  //!< flag indicating there is not nor will ever there be power flow states or checks
    NO_DYNAMICS = 45,  //!< flag indicating there is not nor will ever there be dynamic states

    DISABLE_FLAG_UPDATES = 46,  // flag to temporarily disable flag updates from the alert function
    FLAG_UPDATE_REQUIRED = 47,  //!< flag indicated that a flag update is required
    PFLOW_INIT_REQUIRED =
        48,  //!< flag indicating that an object is using pflow initialization for dynamic elements

    // Various informative flags that can be used in some situations
    DISCONNECTED = 49,  //!< flag indicating that the object is DISCONNECTED
    DIFFERENTIAL_OUTPUT =
        50,  //!< flag that the model has a differential state variable that is the primary output
    NO_GRIDCOMPONENT_SET =
        51,  //!< flag indicating skipping of the GridComponent set function for parent setting
    //! without throwing an error
    BEING_DELETED = 52,  //!<  flag indicating the object is in the process of being deleted
                         //!<  NOTE::useful for some
    //! large objects with components allocated in larger fashion so we skip over some steps
    //! in object removal
    SEPARATE_PROCESSING =
        53,  //!< flag indicating that the object math functions will be handled by the parent
    //! object and should be skipped by the GridComponent Model
    /*flags 54-63 are intended for object capabilities*/

    MULTIPART_CALCULATION_CAPABLE =
        54,  //!< flag indicating the object is capable of using pre and post execution functions
    HAS_SUBOBJECT_PFLOW_STATES =
        55,  //!< flag indicating that the object has a subobject with pflow states
    EXTRA_CAPABILITY_FLAG1 = 56,  //!< flag reserved for future use
    DC_ONLY = 57,  //!< flag indicating the object must be attached to a DC bus
    DC_CAPABLE = 58,  //!< flag indicating the object can be attached to a DC bus
    DC_TERMINAL2 = 59,  //!< flag indicating the terminal 2 must be a DC bus
    THREE_PHASE_ONLY = 61,  //!< flag indicating the object must be attached to a 3 phase bus
    THREE_PHASE_CAPABLE = 62,  //!< flag indicating the object can be attached to a 3 phase bus
    THREE_PHASE_TERMINAL2 =
        63,  //!< flag indicating the terminal 2 must be attached to a 3 phase bus

};
/** alternate names for some of the flags*/
enum OperationFlagOverloads {
    SAMPLED_ONLY = NO_DYNAMICS,
};

/** @brief enumeration of possible convergence modes
 */
enum class ConvergeMode {
    SINGLE_ITERATION,  //!< a single iteration loop
    VOLTAGE_ONLY,  //!< only iterate on the voltage
    HIGH_ERROR_ONLY,  //!< only iterate on high error states
    LOCAL_ITERATION,  //!< do a simple iteration to tolerance
    BLOCK_ITERATION,  //!< do an iteration loop at higher level
    STRONG_ITERATION,  //!< do a stronger iteration to tolerance
    FORCE_STRONG_ITERATION,  //!< no jumping to alternate convergence with strong iteration
    FORCE_VOLTAGE_ONLY,  //!< no jumping to alternate convergence mechanics
};

// for the controlFlags bitset used for initialization and powerFlowAdjustments

/** @brief control flag locations for initialization functions */
enum InitControlFlags {
    CONSTRAINTS_DISABLED = 0,  //!< disable all constraints
    ROOTS_DISABLED = 1,  //!< disable all roots
    UNUSED_CONTROL_FLAG1 = 2,  //!< currently unused
    NO_EXCITER_LIMITS = 3,  //!< ignore exciter limits
    NO_GOVERNOR_LIMITS = 4,  //!< ignore governor limits
    NO_LIMITS = 5,  //!< ignore all limits
    IGNORE_BUS_LIMITS = 6,  //!< ignore bus limits
    DISABLE_LINK_ADJUSTMENTS = 7,  //!< disable all link adjustments
    DISABLE_LOAD_ADJUSTMENTS = 8,  //!< disable all load adjustments
    AUTO_BUS_DISCONNECT = 9,  //!< disable automatic bus disconnection in exceptional circumstances
    NO_AUTO_AUTOGEN = 10,  //!< disable automatic autogeneration for slk/afix/pv buses
    ALL_LOADS_TO_CONSTANT_IMPEDENCE = 11,  //!< convert all loads to constant impedance
    FORCE_CONSTANT_PFLOW_INITIALIZATION = 12,  //!< for some objects that initialize through power
                                               //!< flow calculations force it to be constant
    IGNORE_SATURATION = 13,  //!< ignore saturation effects
    LOW_VOLTAGE_CHECKING = 15,  //!< enable low voltage checking on buses
};

#define CHECK_CONTROLFLAG(flag, flagName) (((flag) & (1U << flagName)) != 0)
#define SET_CONTROLFLAG(flag, flagName) ((flag) |= (1U << flagName))
#define CLEAR_CONTROLFLAG(flag, flagName) ((flag) &= (~(1U << flagName)))

/** @brief get the lower 32 bits of a flag variable*
@param[in] flags the 64 bit flags to get the lower half from
*/
inline std::uint32_t lower_flags(std::uint64_t flags)
{
    return static_cast<std::uint32_t>(flags & static_cast<std::uint64_t>(0xFFFFFFFF));
}

/** @ brief get the lower 32 bits of 64 bit bitset*
@param[in] flags the 64 bit bitset flags to get the lower half from
*/
inline std::uint32_t lower_flags(std::bitset<64> flags)
{
    return static_cast<std::uint32_t>(flags.to_ullong() & static_cast<std::uint64_t>(0xFFFFFFFF));
}

#define RESET_CHANGE_FLAG_MASK (0xFFFFFFFF00FFFFFF)  // macro to change flag masks

inline bool anyChangeFlags(std::bitset<64> flags)
{
    return ((flags.to_ullong() & (0x00FF000000)) > 0);
}

#define MIN_CHANGE_ALERT (500)  //!< the minimum change alert flag
#define MAX_CHANGE_ALERT (900)  //!< the maximum change alert flag

#define INVALID_STATE_ALERT (585)  //!< invalid state alert
#define INTIALIZATION_FAILURE (587)  //!< indication an object failed to initialize
#define FLAG_CHANGE (605)  //!< flag change alert

#define CONNECTIVITY_CHANGE (607)  //!< connectivity change alert

#define ROOT_COUNT_CHANGE (590)  //!< change in the number of roots
#define ROOT_COUNT_INCREASE (591)  //!< increase in the number of roots
#define ROOT_COUNT_DECREASE (592)  //!< decrease in the number of roots

#define STATE_COUNT_CHANGE (600)  //!< change in the number of states
#define STATE_COUNT_INCREASE (601)  //!< state count increase
#define STATE_COUNT_DECREASE (602)  //!< state count decrease
#define STATE_IDENTITY_CHANGE (604)  //!< states change from algebraic to differential or vice versa

#define OBJECT_COUNT_CHANGE (615)  //!< change in the number of active objects
#define OBJECT_COUNT_INCREASE (616)  //!< increase in the number of active objects
#define OBJECT_COUNT_DECREASE (617)  //!< decrease in the number of active objects

#define JAC_COUNT_CHANGE (630)  //!< change in the number non-zero Jacobian entries
#define JAC_COUNT_INCREASE (631)  //!< increase in the number non-zero Jacobian entries
#define JAC_COUNT_DECREASE (632)  //!< decrease in the number non-zero Jacobian entries

#define SLACK_BUS_CHANGE (655)  //!< change in the slack bus

#define CONSTRAINT_COUNT_CHANGE (670)  //!< change in the constraint count
#define CONSTRAINT_COUNT_INCREASE (671)  //!< increase in the number of constraints
#define CONSTRAINT_COUNT_DECREASE (672)  //!< decrease in the number of constraints

#define POTENTIAL_FAULT_CHANGE (690)  //!< change in a fault condition
#define VERY_LOW_VOLTAGE_ALERT (700)  //!< low voltage alert

#define VOLTAGE_CONTROL_CHANGE (710)  //!< change in voltage control (used by buses)

#define VOLTAGE_CONTROL_UPDATE (401)  //!< update to voltage control parameters (used by buses)
#define POWER_CONTROL_UDPATE (403)  //!< update to power control parameters (used by buses)
#define PV_CONTROL_UDPATE (406)  //!< update to PV control parameters (used by buses)

#define SINGLE_STEP_REQUIRED 2001  //!< indicator that an object requires single step updates
#define SINGLE_STEP_NOT_REQUIRED                                                                   \
    2002  //!< indicator that an object no longer requires single step updates

/** @brief define the reset levels
 */
enum class ResetLevels {
    // Normal reset levels
    MINIMAL = 0,  //!< a MINIMAL reset
    VOLTAGE = 1,  //!< reset the VOLTAGE levels
    ANGLE = 2,  //!< reset the angles
    VOLTAGE_ANGLE = 3,  //!< reset the voltage and the angle
    FULL = 4,  //!< do a FULL reset
    // Low voltage reset levels
    LOW_VOLTAGE_PFLOW = -2,  //!< reset low voltage levels on power flow
    LOW_VOLTAGE_DYN1 = -1,  //!< reset low voltage levels on dynamic simulation
    LOW_VOLTAGE_DYN2 = -10,  //!< reset low voltage levels on dynamic simulation mode 2
    LOW_VOLTAGE_DYN0 = -12,  //!< reset low voltage levels on dynamic simulation mode 0
};

enum class CheckLevel {
    REVERSABLE_ONLY = 0,
    FULL_CHECK = 1,
    LOW_VOLTAGE_CHECK = 3,  //!< check for low voltages
    COMPLETE_STATE_CHECK = 4,
    HIGH_ANGLE_TRIP = 5,  //!< disconnect all lines with phase differential greater than pi/2;
};

/** @brief helper class for containing sizes to group the data*/
class StateSizes {
  public:
    // state sizes
    count_t diffSize = 0;  //!< number of differential variables
    count_t algSize = 0;  //!< number of algebraic variables
    count_t vSize = 0;  //!< number of voltage variables
    count_t aSize = 0;  //!< number of angle variables
    // root sizes
    count_t diffRoots = 0;  //!< number of roots on purely differential states
    count_t algRoots = 0;  //!< number of roots based algebraic components

    count_t jacSize = 0;  //!< upper bound on number of Jacobian entries
    // NOTE:  there is 4 bytes of padding here

    /** reset the sizes to all zeros*/
    void reset();
    /** reset just the sizes related to states to 0*/
    void stateReset();
    /** reset the root counter and  sizes to 0*/
    void rootReset() { algRoots = diffRoots = 0; }

    /** reset the Jacobian counter and  sizes to 0*/
    void jacobianReset() { jacSize = 0; }
    /** add another StateSizes object to this one
    @param[in] arg the StateSizes object to combine*/
    void add(const StateSizes& arg);
    /** add just the actual state counts ignore the root and Jacobian counts
    @param[in] arg the update to add to the calling object
    */
    void addStateSizes(const StateSizes& arg);
    /** add just the root and Jacobian information
    @param[in] arg the update to add to the calling object*/
    void addRootSizes(const StateSizes& arg);
    /** add just the Jacobian information
    @param[in] arg the update to add to the calling object*/
    void addJacobianSizes(const StateSizes& arg);
    /** get the total count*/
    count_t totalSize() const;
};

/**
*@brief Helper class encapsulating the offsets for the solver evaluation functions
 acts as a container for solver offsets and object indices into the state vectors
**/
class SolverOffsets {
  public:
    index_t aOffset = kNullLocation;  //!< Location for the voltage offset
    index_t vOffset = kNullLocation;  //!< Location for the Angle offset
    index_t algOffset = kNullLocation;  //!< location for generic offsets
    index_t diffOffset = kNullLocation;  //!< location for the differential offsets
    index_t rootOffset = kNullLocation;  //!< location for the root offsets
                                         // total object sizes

    bool stateLoaded = false;  //!< flag indicating the state sizes have been loaded
    bool jacobianLoaded = false;  //!< flag indicated Jacobian size is loaded
    bool rootsLoaded = false;  //!< flag indicated root size is loaded
    bool offetLoaded = false;  //!< flag indicating that offsets have been loaded
    SolverMode sMode = cLocalSolverMode;  //!< the reference SolverMode

    // local objectSizes
    StateSizes total;  //!< container for total state sizes;
    StateSizes local;  //!< container for local state sizes

  public:
    /** @brief  default constructor*/
    SolverOffsets() = default;

    /** @brief reset the solverOffset
     */
    void reset();

    /** @brief reset the solverOffset root components
     */
    void rootCountReset();
    /** @brief reset the solverOffset Jacobian component
     */
    void jacobianCountReset();

    /** @brief reset the solverOffset state components
     */
    void stateReset();

    /** @brief increment the offsets using the contained sizes to generate the expected next offset
     */
    void increment();

    /** @brief increment the offsets using the contained sizes in another SolverOffsets object
    @param offsets the SolverOffsets object to use as the sizes
    */
    void increment(const SolverOffsets& offsets);

    /** @brief increment the offsets using the contained local sizes in another SolverOffsets object
    @param offsets the SolverOffsets object to use as the sizes
    */
    void localIncrement(const SolverOffsets& offsets);

    /** @brief merge the sizes of two SolverOffsets
    @param offsets the SolverOffsets object to use as the sizes
    */
    void addSizes(const SolverOffsets& offsets);

    /** @brief merge the sizes of two SolverOffsets state Sizes
      @param offsets the SolverOffsets object to use as the sizes
      */
    void addStateSizes(const SolverOffsets& offsets);

    /** @brief add the Root count parameters to the sizes
     */
    void addRootSizes(const SolverOffsets& offsets);
    /** @brief add the Jacobian parameters to the sizes
     */
    void addJacobianSizes(const SolverOffsets& offsets);

    /** @brief load the local state variables to the sizes
    @param finishedLoading set the stateLoaded flag to the given value
     */
    void localStateLoad(bool finishedLoading = false);
    /** @brief load the local information to the total
    @param finishedLoading set the stateLoaded flag to the given value
    */
    void localLoadAll(bool finishedLoading = false);

    /** @brief set the offsets from another SolverOffsets object
    @param newOffsets the SolverOffsets object to use as the sizes
    */
    void setOffsets(const SolverOffsets& newOffsets);

    /** @brief set the offsets from a single index
    @param newOffset the index of the new offset
    */
    void setOffset(index_t newOffset);

    void setLoaded() { stateLoaded = jacobianLoaded = rootsLoaded = true; }
    void setLoaded(bool dynOnly)
    {
        stateLoaded = (!dynOnly) ? stateLoaded : true;
        jacobianLoaded = rootsLoaded = true;
    }
};

/**@brief local state pointers
 */
class Lp {
  public:
    CoreTime time = timeZero;  //!< time
    count_t algOffset = kNullLocation;  //!< data offset for algebraic components
    count_t diffOffset = kNullLocation;  //!< data offset for differential components
    const double* algStateLoc = nullptr;  //!< location of algebraic state variables
    const double* diffStateLoc = nullptr;  //!< location of differential state values
    const double* dstateLoc = nullptr;  //!< location of derivatives of differential components
    double* destLoc = nullptr;  //!< location to place calculations for algebraic component
    double* destDiffLoc = nullptr;  //!< location to place calculations for differential component
    count_t algSize = 0;  //!< size of algebraic component
    count_t diffSize = 0;  //!< size of differential component
};

/**@brief class for containing state data information
 */
class StateData {
  public:
    CoreTime time = 0.0;  //!< time corresponding to the state data
    count_t seqID = 0;  //!< a sequence id to differentiate between subsequent state data objects
    index_t stateSize = 0;  //!< the size of the state vector, if at zero the information is
                            //!< presumed to come from another source
    double cj = 1.0;  //!< a number used in Jacobian calculations if there is a derivative used in
                      //!< the calculations
    const double* state = nullptr;  //!< the current state guessState
    const double* dstate_dt = nullptr;  //!< the state time derivative array
    const double* fullState = nullptr;  //!< the full state data (for cases where state contains
                                        //!< only differential or algebraic components)
    const double* diffState = nullptr;  //!< the differential state data (for cases where state
                                        //!< contains only algebraic components)
    const double* algState = nullptr;  //!< the algebraic state data (for cases where state contains
                                       //!< only differential components)
    double* scratch1 =
        nullptr;  //!< scratch space the objects can use for calculations (if not null it should be
    //! the same size as state
    double* scratch2 =
        nullptr;  //!< scratch space the objects can use for calculations (if not null it should be
    //! the same size as state
    CoreTime altTime = 0.0;  //!< the time corresponding to the other part of the state
    index_t pairIndex = kNullLocation;  //!< the index of the mode the paired data comes from

    StateData(CoreTime sTime = 0.0,
              const double* sstate = nullptr,
              const double* ndstate_dt = nullptr,
              count_t cseq = 0): time(sTime), seqID(cseq), state(sstate), dstate_dt(ndstate_dt)
    {
    }
    bool empty() const { return (state == nullptr); }
    bool updateRequired(count_t checkID) const
    {
        return ((checkID != seqID) || (seqID == 0) || (empty()));
    }
    bool hasScratch() const { return (scratch1 != nullptr); }
};

const StateData emptyStateData{};

#define DEFAULT_OFFSET_CONTAINER_SIZE 5
class GridComponent;

}  // namespace griddyn
#define ALGEBRAIC_VARIABLE (0.0)
#define DIFFERENTIAL_VARIABLE (1.0)

#define POSITIVITY_CONSTRAINT (1.0)
#define NEGATIVITY_CONSTRAINT (-1.0)
#define NONNEGATIVE_CONSTRAINT (2.0)
#define NONPOSITIVE_CONSTRAINT (-2.0)
