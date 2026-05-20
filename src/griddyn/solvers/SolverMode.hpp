/*
 * Copyright (c) 2014-2026, Lawrence Livermore National Security
 * See the top-level NOTICE for additional details. All rights reserved.
 * SPDX-License-Identifier: BSD-3-Clause
 */

#pragma once

#include "../gridDynDefinitions.hpp"
#include <bitset>

namespace griddyn {
// object operation flags
// explicit specification since these are used in combination with the flags Bitset
/** solver mode operations enum.
 *  local for non-connected operation
 * localb for solutions with just a small subset of objects
 *  dcFlow for dc power flow case
 *  pFlow for regular ac power flow case
 *  algebraic_only for the algebraic part of the DAE solution
 * differential_only for the differential part of the DAE solution
 *  DAE  the full DAE Solution mode
 */
enum approxkey {
    decoupled = 0,
    small_angle = 1,
    small_r = 2,
    linear = 3,
    force_recalc = 29,
    dc = 31,
};

enum class approxKeyMask : unsigned int {
    none = 0,
    decoupled = (1 << approxkey::decoupled),
    sm_angle = (1 << approxkey::small_angle),
    simplified = (1 << approxkey::small_r),
    simplified_decoupled = (1 << approxkey::decoupled) + (1 << approxkey::small_r),
    simplified_sm_angle = (1 << approxkey::small_angle) + (1 << approxkey::small_r),
    sm_angle_decoupled = (1 << approxkey::decoupled) + (1 << approxkey::small_angle),
    fast_decoupled =
        (1 << approxkey::small_r) + (1 << approxkey::small_angle) + (1 << approxkey::decoupled),
    linear = (1 << approxkey::linear),
};

#ifdef _MSC_VER
#    if _MSC_VER < 1900
#        define KEY_QUAL inline const
#    endif
#endif

#ifndef KEY_QUAL
#    define KEY_QUAL constexpr
#endif

KEY_QUAL unsigned int indexVal(approxKeyMask key)
{
    return static_cast<unsigned int>(key);
}

enum defindedSolverModes : index_t {
    local_mode = 0,
    power_flow = 1,
    dae = 2,
    dynamic_algebraic = 3,
    dynamic_differential = 4,
};

/** @brief class defining how a specific solver operates and how to find information*/
class solverMode {
  public:
    bool dynamic = false;  //!< indicate if the solver is for dynamic simulation
    bool differential = false;  //!< indicate if the solver uses differential states
    bool algebraic = false;  //!< indicate if the solver uses algebraic states
    bool local = false;  //!< indicator if the solver uses local states
    bool extended_state = false;  //!< indicate if the solver uses extended states
    bool parameters = false;  //!< indicator if the solver uses parameters
    std::bitset<32> approx;  //!<  a bitset containing the approximation assumptions the solver
                             //!<  wishes to be made
    //!(request not obligation)
    index_t offsetIndex = kNullLocation;  //!< index into an array of solverOffsets
    index_t pairedOffsetIndex =
        kNullLocation;  //!< the index of a paired solverMode --namely one containing state
    //! information not calculated by this mode
    /**@brief solverMode constructor
  @param[in] index the index to put in offsetIndex*/
    constexpr explicit solverMode(index_t index): offsetIndex(index)
    {
        if (index == local_mode) {  // predefined local
            local = true;
            dynamic = true;
            differential = true;
            algebraic = true;
        } else if (index == power_flow) {  // predefined pflow
            algebraic = true;
            differential = false;
            dynamic = false;
        } else if (index == dae) {  // predefined dae
            dynamic = true;
            differential = true;
            algebraic = true;
        } else if (index == dynamic_algebraic) {  // predefined dynAlg
            algebraic = true;
            differential = false;
            dynamic = true;
        } else if (index == dynamic_differential) {  // predefined dynDiff
            algebraic = false;
            differential = true;
            dynamic = true;
        }
    }
    /**@brief solverMode default constructor*/
    constexpr solverMode() = default;
    constexpr bool operator==(const solverMode& b) const
    {
        return ((dynamic == b.dynamic) && (differential == b.differential) &&
                (algebraic == b.algebraic) && (local == b.local) &&
                (extended_state == b.extended_state) && (approx == b.approx));
    }
};

#define LINKAPPROXMASK ((unsigned int)(0x000F))
constexpr int getLinkApprox(const solverMode& sMode)
{
    return static_cast<int>(sMode.approx.to_ulong() & (LINKAPPROXMASK));
}
inline void setLinkApprox(solverMode& sMode, approxKeyMask key)
{
    sMode.approx &= (~LINKAPPROXMASK);
    sMode.approx |= indexVal(key);
}

inline void setLinkApprox(solverMode& sMode, int val)
{
    sMode.approx.set(val);
}
inline void setLinkApprox(solverMode& sMode, int val, bool setval)
{
    sMode.approx.set(val, setval);
}

inline constexpr solverMode cLocalSolverMode(local_mode);
inline constexpr solverMode cPflowSolverMode(power_flow);
inline constexpr solverMode cDaeSolverMode(dae);
inline constexpr solverMode cDynAlgSolverMode(dynamic_algebraic);
inline constexpr solverMode cDynDiffSolverMode(dynamic_differential);

inline constexpr solverMode cEmptySolverMode{};
/**
 *Helper functions for determining mode capabilities
 **/
/**
 * @brief determine if the mode is dc only
 **/
constexpr bool isDC(const solverMode& sMode)
{
    return sMode.approx[dc];
}
/**
 * @brief determine if the mode is AC only
 **/
constexpr bool isAC(const solverMode& sMode)
{
    return !sMode.approx[dc];
}
/**
 * @brief set the approximation mode to be DC
 **/
inline void setDC(solverMode& sMode)
{
    sMode.approx.set(dc);
}
/**
 * @brief determine if the mode requires dynamic initialization
 **/
constexpr bool isDynamic(const solverMode& sMode)
{
    return sMode.dynamic;
}
/**
* @brief determine if the mode is for power flow
@details isPowerFlow()==(!isDynamic())
**/
constexpr bool isPowerFlow(const solverMode& sMode)
{
    return !sMode.dynamic;
}
/**
 * @brief determine if the mode only uses algebraic variables
 **/
constexpr bool isAlgebraicOnly(const solverMode& sMode)
{
    return (sMode.algebraic) && (!sMode.differential);
}
/**
 * @brief determine if the mode only uses differential variables
 **/
constexpr bool isDifferentialOnly(const solverMode& sMode)
{
    return (!sMode.algebraic) && (sMode.differential);
}
/**
 * @brief determine if the mode uses both algebraic and differential variables
 **/
constexpr bool isDAE(const solverMode& sMode)
{
    return (sMode.algebraic) && (sMode.differential);
}
/**
 * @brief determine if the mode is a local mode
 **/
constexpr bool isLocal(const solverMode& sMode)
{
    return sMode.local;
}
/**
 * @brief determine if the mode has differential components to it
 **/
constexpr bool hasDifferential(const solverMode& sMode)
{
    return sMode.differential;
}
/**
 * @brief determine if the mode has algebraic components to it
 **/
constexpr bool hasAlgebraic(const solverMode& sMode)
{
    return sMode.algebraic;
}
/**
 * @brief determine if the bus is using extended state information (namely Pin and Qin)
 **/
constexpr bool isExtended(const solverMode& sMode)
{
    return sMode.extended_state;
}
}  // namespace griddyn
