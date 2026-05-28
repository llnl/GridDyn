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
enum ApproxKey {
    DECOUPLED = 0,
    SMALL_ANGLE = 1,
    SMALL_R = 2,
    LINEAR = 3,
    FORCE_RECALC = 29,
    DC = 31,
};

enum class ApproxKeyMask : unsigned int {
    NONE = 0,
    DECOUPLED = (1 << ApproxKey::DECOUPLED),
    SM_ANGLE = (1 << ApproxKey::SMALL_ANGLE),
    SIMPLIFIED = (1 << ApproxKey::SMALL_R),
    SIMPLIFIED_DECOUPLED = (1 << ApproxKey::DECOUPLED) + (1 << ApproxKey::SMALL_R),
    SIMPLIFIED_SM_ANGLE = (1 << ApproxKey::SMALL_ANGLE) + (1 << ApproxKey::SMALL_R),
    SM_ANGLE_DECOUPLED = (1 << ApproxKey::DECOUPLED) + (1 << ApproxKey::SMALL_ANGLE),
    FAST_DECOUPLED =
        (1 << ApproxKey::SMALL_R) + (1 << ApproxKey::SMALL_ANGLE) + (1 << ApproxKey::DECOUPLED),
    LINEAR = (1 << ApproxKey::LINEAR),
};

#ifdef _MSC_VER
#    if _MSC_VER < 1900
#        define KEY_QUAL inline const
#    endif
#endif

#ifndef KEY_QUAL
#    define KEY_QUAL constexpr
#endif

KEY_QUAL unsigned int indexVal(ApproxKeyMask key)
{
    return static_cast<unsigned int>(key);
}

enum DefinedSolverModes : index_t {
    LOCAL_MODE = 0,
    POWER_FLOW = 1,
    DAE = 2,
    DYNAMIC_ALGEBRAIC = 3,
    DYNAMIC_DIFFERENTIAL = 4,
};

/** @brief class defining how a specific solver operates and how to find information*/
class SolverMode {
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
        kNullLocation;  //!< the index of a paired SolverMode --namely one containing state
    //! information not calculated by this mode
    /**@brief SolverMode constructor
  @param[in] index the index to put in offsetIndex*/
    constexpr explicit SolverMode(index_t index): offsetIndex(index)
    {
        if (index == LOCAL_MODE) {  // predefined local
            local = true;
            dynamic = true;
            differential = true;
            algebraic = true;
        } else if (index == POWER_FLOW) {  // predefined pflow
            algebraic = true;
            differential = false;
            dynamic = false;
        } else if (index == DAE) {  // predefined dae
            dynamic = true;
            differential = true;
            algebraic = true;
        } else if (index == DYNAMIC_ALGEBRAIC) {  // predefined dynAlg
            algebraic = true;
            differential = false;
            dynamic = true;
        } else if (index == DYNAMIC_DIFFERENTIAL) {  // predefined dynDiff
            algebraic = false;
            differential = true;
            dynamic = true;
        }
    }
    /**@brief SolverMode default constructor*/
    constexpr SolverMode() = default;
    constexpr bool operator==(const SolverMode& b) const
    {
        return ((dynamic == b.dynamic) && (differential == b.differential) &&
                (algebraic == b.algebraic) && (local == b.local) &&
                (extended_state == b.extended_state) && (approx == b.approx));
    }
};

#define LINKAPPROXMASK ((unsigned int)(0x000F))
constexpr int getLinkApprox(const SolverMode& sMode)
{
    return static_cast<int>(sMode.approx.to_ulong() & (LINKAPPROXMASK));
}
inline void setLinkApprox(SolverMode& sMode, ApproxKeyMask key)
{
    sMode.approx &= (~LINKAPPROXMASK);
    sMode.approx |= indexVal(key);
}

inline void setLinkApprox(SolverMode& sMode, int val)
{
    sMode.approx.set(val);
}
inline void setLinkApprox(SolverMode& sMode, int val, bool setval)
{
    sMode.approx.set(val, setval);
}

inline constexpr SolverMode cLocalSolverMode(LOCAL_MODE);
inline constexpr SolverMode cPflowSolverMode(POWER_FLOW);
inline constexpr SolverMode cDaeSolverMode(DAE);
inline constexpr SolverMode cDynAlgSolverMode(DYNAMIC_ALGEBRAIC);
inline constexpr SolverMode cDynDiffSolverMode(DYNAMIC_DIFFERENTIAL);

inline constexpr SolverMode cEmptySolverMode{};
/**
 *Helper functions for determining mode capabilities
 **/
/**
 * @brief determine if the mode is dc only
 **/
constexpr bool isDC(const SolverMode& sMode)
{
    return sMode.approx[DC];
}
/**
 * @brief determine if the mode is AC only
 **/
constexpr bool isAC(const SolverMode& sMode)
{
    return !sMode.approx[DC];
}
/**
 * @brief set the approximation mode to be DC
 **/
inline void setDC(SolverMode& sMode)
{
    sMode.approx.set(DC);
}
/**
 * @brief determine if the mode requires dynamic initialization
 **/
constexpr bool isDynamic(const SolverMode& sMode)
{
    return sMode.dynamic;
}
/**
* @brief determine if the mode is for power flow
@details isPowerFlow()==(!isDynamic())
**/
constexpr bool isPowerFlow(const SolverMode& sMode)
{
    return !sMode.dynamic;
}
/**
 * @brief determine if the mode only uses algebraic variables
 **/
constexpr bool isAlgebraicOnly(const SolverMode& sMode)
{
    return (sMode.algebraic) && (!sMode.differential);
}
/**
 * @brief determine if the mode only uses differential variables
 **/
constexpr bool isDifferentialOnly(const SolverMode& sMode)
{
    return (!sMode.algebraic) && (sMode.differential);
}
/**
 * @brief determine if the mode uses both algebraic and differential variables
 **/
constexpr bool isDAE(const SolverMode& sMode)
{
    return (sMode.algebraic) && (sMode.differential);
}
/**
 * @brief determine if the mode is a local mode
 **/
constexpr bool isLocal(const SolverMode& sMode)
{
    return sMode.local;
}
/**
 * @brief determine if the mode has differential components to it
 **/
constexpr bool hasDifferential(const SolverMode& sMode)
{
    return sMode.differential;
}
/**
 * @brief determine if the mode has algebraic components to it
 **/
constexpr bool hasAlgebraic(const SolverMode& sMode)
{
    return sMode.algebraic;
}
/**
 * @brief determine if the bus is using extended state information (namely Pin and Qin)
 **/
constexpr bool isExtended(const SolverMode& sMode)
{
    return sMode.extended_state;
}
}  // namespace griddyn
