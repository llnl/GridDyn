/*
 * Copyright (c) 2014-2026, Lawrence Livermore National Security
 * See the top-level NOTICE for additional details. All rights reserved.
 * SPDX-License-Identifier: BSD-3-Clause
 */

#pragma once

#include "GridComponentHelperClasses.h"
#include "solvers/SolverMode.hpp"

namespace griddyn {

/**
 * @brief Helper class encapsulating offsets for the various solution SolverMode types
 **/
class OffsetTable {
  private:
    // std::vector<solverOffsets> offsetContainer;       //!< a vector of containers for offsets
    // corresponding to the different solver modes
    boost::container::small_vector<solverOffsets, DEFAULT_OFFSET_CONTAINER_SIZE>
        offsetContainer;  //!< a vector of containers for offsets corresponding to the different
                          //!< solver modes
  public:
    /** @brief constructor
     */
    OffsetTable();

    /** @brief check whether an offset set has been fully loaded
     *@param[in] sMode the SolverMode we are interested in
     *@return a flag (true) if loaded (false) if not
     */
    bool isLoaded(const SolverMode& sMode) const;
    /** @brief check whether the state information is loaded
     *@param[in] sMode the SolverMode we are interested in
     *@return a flag (true) if loaded (false) if not
     */
    bool isStateCountLoaded(const SolverMode& sMode) const;
    /** @brief check whether the root information is loaded
     *@param[in] sMode the SolverMode we are interested in
     *@return a flag (true) if loaded (false) if not
     */
    bool isRootCountLoaded(const SolverMode& sMode) const;
    /** @brief check whether the Jacobian information is loaded
     *@param[in] sMode the SolverMode we are interested in
     *@return a flag (true) if loaded (false) if not
     */
    bool isJacobianCountLoaded(const SolverMode& sMode) const;
    /** @brief set the offsets for a SolverMode
     *@param[in] sMode the SolverMode we are interested in
     *@param[in] newOffsets the offsets to assign
     */
    void setOffsets(const solverOffsets& newOffsets, const SolverMode& sMode);

    /** @brief set the base offset
     *@param[in] newOffset the location to set the offset to
     *@param[in] sMode the SolverMode we are interested in
     */
    void setOffset(index_t newOffset, const SolverMode& sMode);

    /**get a pointer the offsets for the local mode
     */
    solverOffsets& local()  // local is always the first element
    {
        return offsetContainer.front();
    }
    /**get a const pointer to the local mode of operations
     */
    const solverOffsets& local() const  // local is always the first element
    {
        return offsetContainer.front();
    }
    /** @brief get the offsets for a SolverMode
     *@param[in] sMode the SolverMode we are interested in
     *@return a pointer to a solverOffsets object
     */
    solverOffsets& getOffsets(const SolverMode& sMode);

    /** @brief get the offsets for a SolverMode
     *@param[in] sMode the SolverMode we are interested in
     *@return a const pointer to a solverOffsets object
     */
    const solverOffsets& getOffsets(const SolverMode& sMode) const;
    /** @brief set the base offset of algebraic variables
     *@param[in] newOffset the location to set the offset to
     *@param[in] sMode the SolverMode we are interested in
     */
    void setAlgOffset(index_t newOffset, const SolverMode& sMode);
    /** @brief set the root offset
     *@param[in] newOffset the location to set the offset to
     *@param[in] sMode the SolverMode we are interested in
     */
    void setRootOffset(index_t newOffset, const SolverMode& sMode);
    /** @brief set the differential offset
     *@param[in] newOffset the location to set the offset to
     *@param[in] sMode the SolverMode we are interested in
     */
    void setDiffOffset(index_t newOffset, const SolverMode& sMode);
    /** @brief set the voltage offset
     *@param[in] newOffset the location to set the offset to
     *@param[in] sMode the SolverMode we are interested in
     */
    void setVOffset(index_t newOffset, const SolverMode& sMode);
    /** @brief set the angle offset
     *@param[in] newOffset the location to set the offset to
     *@param[in] sMode the SolverMode we are interested in
     */
    void setAOffset(index_t newOffset, const SolverMode& sMode);
    /** @brief get the base offset
     *@param[in] sMode the SolverMode we are interested in
     *@return the base offset
     */
    index_t getAlgOffset(const SolverMode& sMode) const
    {
        return offsetContainer[sMode.offsetIndex].algOffset;
    }
    /** @brief get the differential state offset
     *@param[in] sMode the SolverMode we are interested in
     *@return the differential offset
     */
    index_t getDiffOffset(const SolverMode& sMode) const
    {
        return offsetContainer[sMode.offsetIndex].diffOffset;
    }
    /**@brief get the root offset
     *@param[in] sMode the SolverMode we are interested in
     *@return the root offset
     */
    index_t getRootOffset(const SolverMode& sMode) const
    {
        // assert (offsetContainer[static_cast<int> (sMode)].rootOffset != kNullLocation);
        return offsetContainer[sMode.offsetIndex].rootOffset;
    }
    /**@brief get the voltage offset
     *@param[in] sMode the SolverMode we are interested in
     *@return the voltage offset
     */
    index_t getVOffset(const SolverMode& sMode) const
    {
        return offsetContainer[sMode.offsetIndex].vOffset;
    }
    /**@brief get the angle offset
     *@param[in] sMode the SolverMode we are interested in
     *@return the angle offset
     */
    index_t getAOffset(const SolverMode& sMode) const
    {
        return offsetContainer[sMode.offsetIndex].aOffset;
    }
    /** @brief get the maximum used index
     *@param[in] sMode the SolverMode we are interested in
     *@return the the maximum used index
     */
    index_t maxIndex(const SolverMode& sMode) const;

    /** @brief get the locations for the data from a stateData pointer and output array
    *@param[in] sMode the SolverMode we are interested in
    *@param[in] sD the stateData object to fill the Lp from
    *@param[in] dest the destination location for the calculations
    @param[in] comp the object to use if local information is required
    @return Lp the Location pointer object to fill
    */
    Lp getLocations(const stateData& stateDataValue,
                    double dest[],
                    const SolverMode& sMode,
                    const GridComponent* comp) const;

    /** @brief get the locations for the data from a stateData pointer
    *@param[in] sMode the SolverMode we are interested in
    *@param[in] sD the stateData object to fill the Lp from
    @param[in] comp the object to use if local information is required
    @return Lp the Location pointer object to fill
    */
    Lp getLocations(const stateData& stateDataValue,
                    const SolverMode& sMode,
                    const GridComponent* comp) const;

    /** @brief get the locations offsets for the data
    *@param[in] sMode the SolverMode we are interested in
    @param[in] Loc the location pointer to store the data
    */
    void getLocations(const SolverMode& sMode, Lp* Loc) const;
    /** @brief unload all the solverOffset objects
     *@param[in] dynamic_only only unload the dynamic solverObjects
     */
    void unload(bool dynamicOnly = false);
    /** @brief unload state information for the solverOffsets
     *@param[in] dynamic_only only unload the dynamic solverObjects
     */
    void stateUnload(bool dynamicOnly = false);
    /** @brief unload the root information for the solverOffsets
     *@param[in] dynamic_only only unload the dynamic solverObjects
     */
    void rootUnload(bool dynamicOnly = false);
    /** @brief unload the Jacobian information for the solverOffsets
     *@param[in] dynamic_only only unload the dynamic solverObjects
     */
    void jacobianUnload(bool dynamicOnly = false);
    /** @brief update all solverOffsets with the local information
     *@param[in] dynamic_only only unload the dynamic solverObjects
     */
    void localUpdateAll(bool dynamicOnly = false);
    /** @brief get the size of the solverOffsets
     *@return the size
     */
    count_t size() const { return static_cast<count_t>(offsetContainer.size()); }
    /** @brief get the SolverMode corresponding to an index
     *@return a SolverMode object
     */
    const SolverMode& getSolverMode(index_t index) const;
    /** @brief find a SolverMode matching another Mode in everything but index
     *@return a SolverMode object
     */
    const SolverMode& find(const SolverMode& tMode) const;

  private:
    bool isValidIndex(index_t index) const
    {
        return ((index >= 0) && (index < static_cast<count_t>(offsetContainer.size())));
    }
};
}  // namespace griddyn
