#pragma once

/*
 * Copyright (c) 2014-2026, Lawrence Livermore National Security
 * See the top-level NOTICE for additional details. All rights reserved.
 * SPDX-License-Identifier: BSD-3-Clause
 */

#include "gridComponentHelperClasses.h"
#include "solvers/solverMode.hpp"

namespace griddyn {

/**
 * @brief Helper class encapsulating offsets for the various solution solverMode types
 **/
class offsetTable {
  private:
    // std::vector<solverOffsets> offsetContainer;       //!< a vector of containers for offsets
    // corresponding to the different solver modes
    boost::container::small_vector<solverOffsets, DEFAULT_OFFSET_CONTAINER_SIZE>
        offsetContainer;  //!< a vector of containers for offsets corresponding to the different
                          //!< solver modes
  public:
    /** @brief constructor
     */
    offsetTable() noexcept;

    /** @brief check whether an offset set has been fully loaded
     *@param[in] sMode the solverMode we are interested in
     *@return a flag (true) if loaded (false) if not
     */
    bool isLoaded(const solverMode& sMode) const;
    /** @brief check whether the state information is loaded
     *@param[in] sMode the solverMode we are interested in
     *@return a flag (true) if loaded (false) if not
     */
    bool isStateCountLoaded(const solverMode& sMode) const;
    /** @brief check whether the root information is loaded
     *@param[in] sMode the solverMode we are interested in
     *@return a flag (true) if loaded (false) if not
     */
    bool isRootCountLoaded(const solverMode& sMode) const;
    /** @brief check whether the Jacobian information is loaded
     *@param[in] sMode the solverMode we are interested in
     *@return a flag (true) if loaded (false) if not
     */
    bool isJacobianCountLoaded(const solverMode& sMode) const;
    /** @brief set the offsets for a solverMode
     *@param[in] sMode the solverMode we are interested in
     *@param[in] newOffsets the offsets to assign
     */
    void setOffsets(const solverOffsets& newOffsets, const solverMode& sMode);

    /** @brief set the base offset
     *@param[in] newOffset the location to set the offset to
     *@param[in] sMode the solverMode we are interested in
     */
    void setOffset(index_t newOffset, const solverMode& sMode);

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
    /** @brief get the offsets for a solverMode
     *@param[in] sMode the solverMode we are interested in
     *@return a pointer to a solverOffsets object
     */
    solverOffsets& getOffsets(const solverMode& sMode);

    /** @brief get the offsets for a solverMode
     *@param[in] sMode the solverMode we are interested in
     *@return a const pointer to a solverOffsets object
     */
    const solverOffsets& getOffsets(const solverMode& sMode) const;
    /** @brief set the base offset of algebraic variables
     *@param[in] newOffset the location to set the offset to
     *@param[in] sMode the solverMode we are interested in
     */
    void setAlgOffset(index_t newOffset, const solverMode& sMode);
    /** @brief set the root offset
     *@param[in] newOffset the location to set the offset to
     *@param[in] sMode the solverMode we are interested in
     */
    void setRootOffset(index_t newOffset, const solverMode& sMode);
    /** @brief set the differential offset
     *@param[in] newOffset the location to set the offset to
     *@param[in] sMode the solverMode we are interested in
     */
    void setDiffOffset(index_t newOffset, const solverMode& sMode);
    /** @brief set the voltage offset
     *@param[in] newOffset the location to set the offset to
     *@param[in] sMode the solverMode we are interested in
     */
    void setVOffset(index_t newOffset, const solverMode& sMode);
    /** @brief set the angle offset
     *@param[in] newOffset the location to set the offset to
     *@param[in] sMode the solverMode we are interested in
     */
    void setAOffset(index_t newOffset, const solverMode& sMode);
    /** @brief get the base offset
     *@param[in] sMode the solverMode we are interested in
     *@return the base offset
     */
    index_t getAlgOffset(const solverMode& sMode) const
    {
        return offsetContainer[sMode.offsetIndex].algOffset;
    }
    /** @brief get the differential state offset
     *@param[in] sMode the solverMode we are interested in
     *@return the differential offset
     */
    index_t getDiffOffset(const solverMode& sMode) const
    {
        return offsetContainer[sMode.offsetIndex].diffOffset;
    }
    /**@brief get the root offset
     *@param[in] sMode the solverMode we are interested in
     *@return the root offset
     */
    index_t getRootOffset(const solverMode& sMode) const
    {
        // assert (offsetContainer[static_cast<int> (sMode)].rootOffset != kNullLocation);
        return offsetContainer[sMode.offsetIndex].rootOffset;
    }
    /**@brief get the voltage offset
     *@param[in] sMode the solverMode we are interested in
     *@return the voltage offset
     */
    index_t getVOffset(const solverMode& sMode) const
    {
        return offsetContainer[sMode.offsetIndex].vOffset;
    }
    /**@brief get the angle offset
     *@param[in] sMode the solverMode we are interested in
     *@return the angle offset
     */
    index_t getAOffset(const solverMode& sMode) const
    {
        return offsetContainer[sMode.offsetIndex].aOffset;
    }
    /** @brief get the maximum used index
     *@param[in] sMode the solverMode we are interested in
     *@return the the maximum used index
     */
    index_t maxIndex(const solverMode& sMode) const;

    /** @brief get the locations for the data from a stateData pointer and output array
    *@param[in] sMode the solverMode we are interested in
    *@param[in] sD the stateData object to fill the Lp from
    *@param[in] dest the destination location for the calculations
    @param[in] comp the object to use if local information is required
    @return Lp the Location pointer object to fill
    */
    Lp getLocations(const stateData& sD,
                    double dest[],
                    const solverMode& sMode,
                    const gridComponent* comp) const;

    /** @brief get the locations for the data from a stateData pointer
    *@param[in] sMode the solverMode we are interested in
    *@param[in] sD the stateData object to fill the Lp from
    @param[in] comp the object to use if local information is required
    @return Lp the Location pointer object to fill
    */
    Lp getLocations(const stateData& sD, const solverMode& sMode, const gridComponent* comp) const;

    /** @brief get the locations offsets for the data
    *@param[in] sMode the solverMode we are interested in
    @param[in] Loc the location pointer to store the data
    */
    void getLocations(const solverMode& sMode, Lp* Loc) const;
    /** @brief unload all the solverOffset objects
     *@param[in] dynamic_only only unload the dynamic solverObjects
     */
    void unload(bool dynamic_only = false);
    /** @brief unload state information for the solverOffsets
     *@param[in] dynamic_only only unload the dynamic solverObjects
     */
    void stateUnload(bool dynamic_only = false);
    /** @brief unload the root information for the solverOffsets
     *@param[in] dynamic_only only unload the dynamic solverObjects
     */
    void rootUnload(bool dynamic_only = false);
    /** @brief unload the Jacobian information for the solverOffsets
     *@param[in] dynamic_only only unload the dynamic solverObjects
     */
    void JacobianUnload(bool dynamic_only = false);
    /** @brief update all solverOffsets with the local information
     *@param[in] dynamic_only only unload the dynamic solverObjects
     */
    void localUpdateAll(bool dynamic_only = false);
    /** @brief get the size of the solverOffsets
     *@return the size
     */
    count_t size() const { return static_cast<count_t>(offsetContainer.size()); }
    /** @brief get the solverMode corresponding to an index
     *@return a solverMode object
     */
    const solverMode& getSolverMode(index_t index) const;
    /** @brief find a solverMode matching another Mode in everything but index
     *@return a solverMode object
     */
    const solverMode& find(const solverMode& tMode) const;

  private:
    bool isValidIndex(index_t index) const
    {
        return ((index >= 0) && (index < static_cast<count_t>(offsetContainer.size())));
    }
};
}  // namespace griddyn
