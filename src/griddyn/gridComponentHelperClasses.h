/*
 * Copyright (c) 2014-2026, Lawrence Livermore National Security
 * See the top-level NOTICE for additional details. All rights reserved.
 * SPDX-License-Identifier: BSD-3-Clause
 */

#pragma once

/** @file
 * @brief classes and object to help in the operation of gridComponents
 */

#include "solvers/solverMode.hpp"
#include <bitset>

namespace griddyn {
constexpr static std::uint64_t flagMask = 0x3FE;  //!< general flag mask for convenience to mask out
                                                  //!< flags that typically cascade to parents

/** @brief  an enumeration of flags in the opFlags bitset for GridComponent
@details flags are intended to be default off hence the various names in certain circumstances
 */
enum operation_flags {
    // indicator flags 0 -15 are general indicators flags which default false

    // typically cascading flags
    has_constraints = 0,  //!< flag indicating if an object uses constraints
    has_roots = 1,  //!< flag indicating if an object uses root finding
    has_alg_roots =
        2,  //!< flag indicated the object has roots dependent on algebraic states and thus must be
    //!< evaluated after a initial condition update
    has_powerflow_adjustments =
        3,  //!< flag indicating if an object has voltage adjustments for power flow
    preEx_requested = 4,  //!< flag indicating if an object requests pre-execution
    uses_bus_frequency = 5,  //!< flag indicating if an object uses bus frequency calculation
    has_pflow_states = 6,  //!< indicator if the object has power flow states if in question
    has_dyn_states = 7,  //!< indicator if the object has dynamic states if in question
    has_differential_states = 8,  //!< indicator if the object has differential states
    not_cloneable = 9,  //!< flag indicating that an object should not be cloned
    extra_cascading_flag = 10,  //!< reserved for future use
    // end of typically cascading flags

    // for handling remote voltage control capabilities
    remote_voltage_control =
        11,  //!< indicator that the object controls a remote bus voltage at a specific level
    local_voltage_control =
        12,  //!< indicator that the object controls the local bus voltage at a specific level
    indirect_voltage_control =
        13,  //!< flag indicating the object must use indirect means to control the voltage
    adjustable_Q =
        14,  //!< flag indicating that an object has controllable reactive power for power flow
    // for handling remote power control capabilities
    remote_power_control =
        15,  //!< indicator that the object controls a remote bus voltage at a specific level
    local_power_control =
        16,  //!< indicator that the object controls the local bus voltage at a specific level
    indirect_power_control =
        17,  //!< flag indicating the object must use indirect means to control the voltage
    adjustable_P = 18,  //!< flag indicating that the object has adjustable power setting and can be
                        //!< used by slack
    //! bus for control of angle

    // some indicator flags for local objects
    pFlow_initialized = 19,  //!< indicator that powerFlow initialization has been completed
    dyn_initialized = 20,  //!<  indicator that dynamic Initialization has been completed
    object_armed_flag =
        21,  //!< basically an extra object flag if the object has a trigger mechanism of some sort
    late_b_initialize =
        22,  //!< flag indicating the object would like to be initialized after most other objects
    //! only acknowledged by areas and then only within the area
    error_flag = 23,  //!< flag indicating the object has an error

    // flags  24- 31 indicating some sort of condition change
    state_change_flag = 24,  //!< flag indicating that the state size or nature has changed
    object_change_flag = 25,  //!< flag indicating that an object has changed activity state
    constraint_change_flag = 26,  //!< flag indicating an change in constraint values
    root_change_flag = 27,  //!< flag indicating a change in the root finding functions
    jacobian_count_change_flag = 28,  //!< flag indicating a change in the Jacobian count
    slack_bus_change = 29,  //!< flag indicating a change in the slack bus
    voltage_control_change = 30,  //!< flag indicating a change in voltage control on a bus
    connectivity_change_flag = 31,  //!< flag indicating a change in bus connectivity possibly
                                    //!< indicating islanding or isolated buses

    /*flags 32-44 are intended for local object usage*/
    object_flag1 = 32,
    object_flag2 = 33,
    object_flag3 = 34,
    object_flag4 = 35,
    object_flag5 = 36,
    object_flag6 = 37,
    object_flag7 = 38,
    object_flag8 = 39,
    object_flag9 = 40,
    object_flag10 = 41,
    object_flag11 = 42,
    object_flag12 = 43,

    // flags 43 - 45 state control
    no_powerflow_operations =
        44,  //!< flag indicating there is not nor will ever there be power flow states or checks
    no_dynamics = 45,  //!< flag indicating there is not nor will ever there be dynamic states

    disable_flag_updates = 46,  // flag to temporarily disable flag updates from the alert function
    flag_update_required = 47,  //!< flag indicated that a flag update is required
    pflow_init_required =
        48,  //!< flag indicating that an object is using pflow initialization for dynamic elements

    // Various informative flags that can be used in some situations
    disconnected = 49,  //!< flag indicating that the object is disconnected
    differential_output =
        50,  //!< flag that the model has a differential state variable that is the primary output
    no_gridcomponent_set =
        51,  //!< flag indicating skipping of the GridComponent set function for parent setting
    //! without throwing an error
    being_deleted = 52,  //!<  flag indicating the object is in the process of being deleted
                         //!<  NOTE::useful for some
    //! large objects with components allocated in larger fashion so we skip over some steps
    //! in object removal
    separate_processing =
        53,  //!< flag indicating that the object math functions will be handled by the parent
    //! object and should be skipped by the GridComponent Model
    /*flags 54-63 are intended for object capabilities*/

    multipart_calculation_capable =
        54,  //!< flag indicating the object is capable of using pre and post execution functions
    has_subobject_pflow_states =
        55,  //!< flag indicating that the object has a subobject with pflow states
    extra_capability_flag1 = 56,  //!< flag reserved for future use
    dc_only = 57,  //!< flag indicating the object must be attached to a DC bus
    dc_capable = 58,  //!< flag indicating the object can be attached to a DC bus
    dc_terminal2 = 59,  //!< flag indicating the terminal 2 must be a DC bus
    three_phase_only = 61,  //!< flag indicating the object must be attached to a 3 phase bus
    three_phase_capable = 62,  //!< flag indicating the object can be attached to a 3 phase bus
    three_phase_terminal2 =
        63,  //!< flag indicating the terminal 2 must be attached to a 3 phase bus

};
/** alternate names for some of the flags*/
enum operation_flag_overloads {
    sampled_only = no_dynamics,
};

/** @brief enumeration of possible convergence modes
 */
enum class converge_mode {
    single_iteration,  //!< a single iteration loop
    voltage_only,  //!< only iterate on the voltage
    high_error_only,  //!< only iterate on high error states
    local_iteration,  //!< do a simple iteration to tolerance
    block_iteration,  //!< do an iteration loop at higher level
    strong_iteration,  //!< do a stronger iteration to tolerance
    force_strong_iteration,  //!< no jumping to alternate convergence with strong iteration
    force_voltage_only,  //!< no jumping to alternate convergence mechanics
};

// for the controlFlags bitset used for initialization and powerFlowAdjustments

/** @brief control flag locations for initialization functions */
enum init_control_flags {
    constraints_disabled = 0,  //!< disable all constraints
    roots_disabled = 1,  //!< disable all roots
    unused_control_flag1 = 2,  //!< currently unused
    no_exciter_limits = 3,  //!< ignore exciter limits
    no_governor_limits = 4,  //!< ignore governor limits
    no_limits = 5,  //!< ignore all limits
    ignore_bus_limits = 6,  //!< ignore bus limits
    disable_link_adjustments = 7,  //!< disable all link adjustments
    disable_load_adjustments = 8,  //!< disable all load adjustments
    auto_bus_disconnect = 9,  //!< disable automatic bus disconnection in exceptional circumstances
    no_auto_autogen = 10,  //!< disable automatic autogeneration for slk/afix/pv buses
    all_loads_to_constant_impedence = 11,  //!< convert all loads to constant impedance
    force_constant_pflow_initialization = 12,  //!< for some objects that initialize through power
                                               //!< flow calculations force it to be constant
    ignore_saturation = 13,  //!< ignore saturation effects
    low_voltage_checking = 15,  //!< enable low voltage checking on buses
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
enum class reset_levels {
    // Normal reset levels
    minimal = 0,  //!< a minimal reset
    voltage = 1,  //!< reset the voltage levels
    angle = 2,  //!< reset the angles
    voltage_angle = 3,  //!< reset the voltage and the angle
    full = 4,  //!< do a full reset
    // Low voltage reset levels
    low_voltage_pflow = -2,  //!< reset low voltage levels on power flow
    low_voltage_dyn1 = -1,  //!< reset low voltage levels on dynamic simulation
    low_voltage_dyn2 = -10,  //!< reset low voltage levels on dynamic simulation mode 2
    low_voltage_dyn0 = -12,  //!< reset low voltage levels on dynamic simulation mode 0
};

enum class check_level_t {
    reversable_only = 0,
    full_check = 1,
    low_voltage_check = 3,  //!< check for low voltages
    complete_state_check = 4,
    high_angle_trip = 5,  //!< disconnect all lines with phase differential greater than pi/2;
};

/** @brief helper class for containing sizes to group the data*/
class stateSizes {
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
    void JacobianReset() { jacSize = 0; }
    /** add another stateSizes object to this one
    @param[in] arg the stateSizes object to combine*/
    void add(const stateSizes& arg);
    /** add just the actual state counts ignore the root and Jacobian counts
    @param[in] arg the update to add to the calling object
    */
    void addStateSizes(const stateSizes& arg);
    /** add just the root and Jacobian information
    @param[in] arg the update to add to the calling object*/
    void addRootSizes(const stateSizes& arg);
    /** add just the Jacobian information
    @param[in] arg the update to add to the calling object*/
    void addJacobianSizes(const stateSizes& arg);
    /** get the total count*/
    count_t totalSize() const;
};

/**
*@brief Helper class encapsulating the offsets for the solver evaluation functions
 acts as a container for solver offsets and object indices into the state vectors
**/
class solverOffsets {
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
    solverMode sMode = cLocalSolverMode;  //!< the reference solverMode

    // local objectSizes
    stateSizes total;  //!< container for total state sizes;
    stateSizes local;  //!< container for local state sizes

  public:
    /** @brief  default constructor*/
    solverOffsets() = default;

    /** @brief reset the solverOffset
     */
    void reset();

    /** @brief reset the solverOffset root components
     */
    void rootCountReset();
    /** @brief reset the solverOffset Jacobian component
     */
    void JacobianCountReset();

    /** @brief reset the solverOffset state components
     */
    void stateReset();

    /** @brief increment the offsets using the contained sizes to generate the expected next offset
     */
    void increment();

    /** @brief increment the offsets using the contained sizes in another solverOffset Object
    @param offsets the solverOffset object to use as the sizes
    */
    void increment(const solverOffsets& offsets);

    /** @brief increment the offsets using the contained local sizes in another solverOffset Object
    @param offsets the solverOffset object to use as the sizes
    */
    void localIncrement(const solverOffsets& offsets);

    /** @brief merge the sizes of two solverOffsets
    @param offsets the solverOffset object to use as the sizes
    */
    void addSizes(const solverOffsets& offsets);

    /** @brief merge the sizes of two solverOffsets state Sizes
      @param offsets the solverOffset object to use as the sizes
      */
    void addStateSizes(const solverOffsets& offsets);

    /** @brief add the Root count parameters to the sizes
     */
    void addRootSizes(const solverOffsets& offsets);
    /** @brief add the Jacobian parameters to the sizes
     */
    void addJacobianSizes(const solverOffsets& offsets);

    /** @brief load the local state variables to the sizes
    @param finishedLoading set the stateLoaded flag to the given value
     */
    void localStateLoad(bool finishedLoading = false);
    /** @brief load the local information to the total
    @param finishedLoading set the stateLoaded flag to the given value
    */
    void localLoadAll(bool finishedLoading = false);

    /** @brief set the offsets from another solverOffset object
    @param newOffsets the solverOffset object to use as the sizes
    */
    void setOffsets(const solverOffsets& newOffsets);

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
    coreTime time = timeZero;  //!< time
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
class stateData {
  public:
    coreTime time = 0.0;  //!< time corresponding to the state data
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
    coreTime altTime = 0.0;  //!< the time corresponding to the other part of the state
    index_t pairIndex = kNullLocation;  //!< the index of the mode the paired data comes from

    stateData(coreTime sTime = 0.0,
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

const stateData emptyStateData{};

#define DEFAULT_OFFSET_CONTAINER_SIZE 5
class GridComponent;

}  // namespace griddyn
#define ALGEBRAIC_VARIABLE (0.0)
#define DIFFERENTIAL_VARIABLE (1.0)

#define POSITIVITY_CONSTRAINT (1.0)
#define NEGATIVITY_CONSTRAINT (-1.0)
#define NONNEGATIVE_CONSTRAINT (2.0)
#define NONPOSITIVE_CONSTRAINT (-2.0)

