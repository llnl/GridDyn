/*
 * Copyright (c) 2014-2026, Lawrence Livermore National Security
 * See the top-level NOTICE for additional details. All rights reserved.
 * SPDX-License-Identifier: BSD-3-Clause
 */

#pragma once

#include "griddyn/gridDynDefinitions.hpp"
#include <cstdio>
#include <vector>

namespace griddyn {
enum class FlowModel { NONE, TRANSPORT, DC, AC };

enum class LinearityMode { LINEAR, QUADRATIC, NONLINEAR };

/**
 *Helper class for containing the reference lookup
 **/
class optimMode {
  public:
    FlowModel flowMode;
    LinearityMode linMode;
    index_t offsetIndex;
    count_t numPeriods;
    double period;
    bool allowInteger = false;
    bool allowBinary = false;
};

/**
 * determine if the mode is an AC mode
 **/
bool isAC(const optimMode& oMode);

#define CONTINUOUS_OBJECTIVE_VARIABLE (0)
#define INTEGER_OBJECTIVE_VARIABLE (1)
#define BINARY_OBJECTIVE_VARIABLE (2)

enum OptimizationFlags {
    OPT_INITIALIZED = 4,

};

// for the controlFlags bitset
enum OptimizationControlFlags {

};

#define CHECK_OPTIMIATION_FLAG(flag, flagName) (flag & (1 << flagName))

/** @brief helper struct for containing sizes to group the data*/
class optimSizes {
  public:
    count_t genSize = 0;  //!< number of local generation variables
    count_t vSize = 0;  //!< number of local voltage variables
    count_t aSize = 0;  //!< number of local Angle variables
    count_t qSize = 0;  //!< number of local reactive power variables
    count_t contSize = 0;  //!< number of local continuous variables
    count_t intSize = 0;  //!< number of local integer variables
    count_t constraintsSize = 0;  //!< number of local constraints

    void reset();

    void add(const optimSizes& arg);
};

/**
 *Helper struct encapsulating the offsets for the optimization evaluation functions
 **/
class optimOffsets {
  public:
    index_t gOffset = kNullLocation;  //!< Location for the genLevelOffsets
    index_t qOffset = kNullLocation;  //!< Location for the reactive power LevelOffsets
    index_t vOffset = kNullLocation;  //!< Location for the voltage Offsets
    index_t aOffset = kNullLocation;  //!< Location for the Angle offset
    index_t contOffset = kNullLocation;  //!< location for continuous parameters offset
    index_t intOffset = kNullLocation;  //!< location for integer parameter offset

    index_t constraintOffset = kNullLocation;  //!< location for the constraint index
    bool loaded = false;  // flag indicating if the sizes have been loaded
    optimSizes local;

    optimSizes total;

  public:
    optimOffsets() = default;

    /** reset the optimOffset
     */
    void reset();
    /** increment the offsets using the contained sizes to generate the expected next offset
     */
    void increment();
    /** increment the offsets using the contained sizes in another optimOffset Object
    @param offsets the optimOffset object to use as the sizes
    */
    void increment(const optimOffsets& offsets);
    /** merge the sizes of two optimOffsets
    @param offsets the optimOffset object to use as the sizes
    */
    void addSizes(const optimOffsets& offsets);
    /** load the local variables to the sizes
     */
    void localLoad(bool finishedLoading = false);
    /** set the offsets from another optimOffset object
    @param newOffsets the optimOffset object to use as the sizes
    */
    void setOffsets(const optimOffsets& newOffsets);
    /** set the offsets from a single index
    @param newOffset the index of the new offset
    */
    void setOffset(index_t newOffset);
};

/**
 * Helper class encapsulating offsets for the various solution optimMode types
 **/
class optOffsetTable {
  private:
    std::vector<optimOffsets> offsetContainer;  //!< an array of 6 containers for offsets
                                                //!< corresponding to the different solver modes
    index_t paramOffset = kNullLocation;  //!< offset for storing parameters in an array

  public:
    /**constructor
     */
    optOffsetTable() = default;
    /** check whether an offset set has been loaded
     * return a pointer to the set of offsets for a particular solver mode
     *@param[in] oMode the optimMode we are interested in
     *@return a flag (true) if loaded (false) if not
     */
    bool isLoaded(const optimMode& oMode) const
    {
        return offsetContainer[oMode.offsetIndex].loaded;
    }

    /** get the offsets for a optimMode
     * return a pointer to the set of offsets for a particular solver mode
     *@param[in] oMode the optimMode we are interested in
     *@return a pointer to the
     */
    optimOffsets& getOffsets(const optimMode& oMode);

    /** get the offsets for a optimMode for const object
    *  return a pointer to the set of offsets for a particular solver mode
    returns a point to a nullOffset object if the index is out of range
    *@param[in] oMode the optimMode we are interested in
    *@return a pointer to the
    */
    const optimOffsets& getOffsets(const optimMode& oMode) const;

    /** set the offsets for a optimMode
     * return a pointer to the set of offsets for a particular solver mode
     *@param[in] oMode the optimMode we are interested in
     *@return a pointer to the
     */
    void setOffsets(const optimOffsets& newOffsets, const optimMode& oMode);

    /** set the base offset
     *@param[in] offset the location to set the offset to
     *@param[in] oMode the optimMode we are interested in
     */
    void setOffset(index_t newOffset, const optimMode& oMode);

    /** set the continuous offset
     *@param[in] offset the location to set the offset to
     *@param[in] oMode the optimMode we are interested in
     */
    void setContOffset(index_t newOffset, const optimMode& oMode);

    /** set the voltage offset
     *@param[in] offset the location to set the offset to
     *@param[in] oMode the optimMode we are interested in
     */
    void setIntOffset(index_t newOffset, const optimMode& oMode);

    /** set the constraints offset
     *@param[in] offset the location to set the offset to
     *@param[in] oMode the optimMode we are interested in
     */
    void setConstraintOffset(index_t newOffset, const optimMode& oMode);

    /** get the angle offset
     *@param[in] oMode the optimMode we are interested in
     *@return the angle offset
     */
    index_t getaOffset(const optimMode& oMode) const;
    /** get the voltage offset
     *@param[in] oMode the optimMode we are interested in
     *@return the voltage offset
     */
    index_t getvOffset(const optimMode& oMode) const;
    /** get the continuous offset
     *@param[in] oMode the optimMode we are interested in
     *@return the diffferential offset
     */
    index_t getContOffset(const optimMode& oMode) const;
    /** get the integer offset
     *@param[in] oMode the optimMode we are interested in
     *@return the root offset
     */
    index_t getIntOffset(const optimMode& oMode) const;

    /** get the real generation offset
     *@param[in] oMode the optimMode we are interested in
     *@return the voltage offset
     */
    index_t getgOffset(const optimMode& oMode) const;

    /** get the reactive generation offset
     *@param[in] oMode the optimMode we are interested in
     *@return the voltage offset
     */
    index_t getqOffset(const optimMode& oMode) const;
    /** get the locations for the data
     *@param[in] oMode the optimMode we are interested in
     */
    // void getLocations (const stateData &sD, double d[], const optimMode &oMode, Lp *Loc,
    // gridComponent *comp);
    /** get the locations for the data from a stateData pointer
     *@param[in] oMode the optimMode we are interested in
     *@return the angle offset
     */
    // void getLocations (stateData *sD, double d[], const optimMode &oMode, Lp *Loc, gridComponent
    // *comp);
    /** get the locations offsets for the data
     *@param[in] oMode the optimMode we are interested in
     *@return the angle offset
     */
    // void getLocations (const optimMode &oMode, Lp *Loc);

    index_t getParamOffset() { return paramOffset; }
    void setParamOffset(index_t newParamOffset) { paramOffset = newParamOffset; }
};

/**@brief class for containing state data information
 */
class optimData {
  public:
    double time = 0.0;  //!< time corresponding to the state data
    const double* val = nullptr;  //!< the current values
    count_t seqID = 0;  //!< a sequence id to differentiate between subsequent optimData object
};

}  // namespace griddyn
