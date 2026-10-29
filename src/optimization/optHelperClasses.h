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
/** Network-flow approximation used to formulate the optimization problem. */
enum class FlowModel { NONE, TRANSPORT, DC, AC };

/** Highest polynomial degree allowed in the objective and constraints. */
enum class LinearityMode { LINEAR, QUADRATIC, NONLINEAR };

/**
 * Selects the network model and indexing context used during optimization.
 **/
class OptimizationMode {
  public:
    FlowModel flowMode;  //!< network-flow formulation
    LinearityMode linMode;  //!< objective and constraint complexity
    index_t offsetIndex;  //!< index selecting the corresponding offset set
    count_t numPeriods;  //!< number of periods represented by the mode
    double period;  //!< duration of one period
    bool allowInteger = false;  //!< whether integer variables are permitted
    bool allowBinary = false;  //!< whether binary variables are permitted
};

/**
 * determine if the mode is an AC mode
 **/
bool isAC(const OptimizationMode& oMode);

#define CONTINUOUS_OBJECTIVE_VARIABLE (0)
#define INTEGER_OBJECTIVE_VARIABLE (1)
#define BINARY_OBJECTIVE_VARIABLE (2)

/** Flags describing optimization object initialization state. */
enum OptimizationFlags {
    OPT_INITIALIZED = 4,

};

/** Reserved flags for optimization control flow. */
enum OptimizationControlFlags {

};

#define CHECK_OPTIMIATION_FLAG(flag, flagName) (flag & (1 << flagName))

/** Counts optimization variables and constraints contributed by one model. */
class OptimizationSizes {
  public:
    count_t genSize = 0;  //!< number of local generation variables
    count_t vSize = 0;  //!< number of local voltage variables
    count_t aSize = 0;  //!< number of local Angle variables
    count_t qSize = 0;  //!< number of local reactive power variables
    count_t contSize = 0;  //!< number of local continuous variables
    count_t intSize = 0;  //!< number of local integer variables
    count_t constraintsSize = 0;  //!< number of local constraints

    /** Resets all variable and constraint counts to zero. */
    void reset();

    /** Adds each count from another size record to this one.
     * @param[in] arg counts to add
     */
    void add(const OptimizationSizes& arg);
};

/**
 *Helper struct encapsulating the offsets for the optimization evaluation functions
 **/
class OptimizationOffsets {
  public:
    index_t gOffset = kNullLocation;  //!< Location for the genLevelOffsets
    index_t qOffset = kNullLocation;  //!< Location for the reactive power LevelOffsets
    index_t vOffset = kNullLocation;  //!< Location for the voltage Offsets
    index_t aOffset = kNullLocation;  //!< Location for the Angle offset
    index_t contOffset = kNullLocation;  //!< location for continuous parameters offset
    index_t intOffset = kNullLocation;  //!< location for integer parameter offset

    index_t constraintOffset = kNullLocation;  //!< location for the constraint index
    bool loaded = false;  //!< whether local sizes and offsets have been loaded
    OptimizationSizes local;  //!< sizes contributed by this model subtree

    OptimizationSizes total;  //!< accumulated sizes for the optimization tree

  public:
    OptimizationOffsets() = default;

    /** reset the optimOffset
     */
    void reset();
    /** increment the offsets using the contained sizes to generate the expected next offset
     */
    void increment();
    /** increment the offsets using the contained sizes in another optimOffset Object
    @param offsets the optimOffset object to use as the sizes
    */
    void increment(const OptimizationOffsets& offsets);
    /** advance the offsets by the local sizes in another optimization object
    @param offsets the optimization offsets whose local sizes define the increment
    */
    void localIncrement(const OptimizationOffsets& offsets);
    /** merge the sizes of two OptimizationOffsets
    @param offsets the optimOffset object to use as the sizes
    */
    void addSizes(const OptimizationOffsets& offsets);
    /** load the local variables to the sizes
     */
    void localLoad(bool finishedLoading = false);
    /** set the offsets from another OptimizationOffset object
    @param newOffsets the OptimizationOffset object to use as the sizes
    */
    void setOffsets(const OptimizationOffsets& newOffsets);
    /** set the offsets from a single index
    @param newOffset the index of the new offset
    */
    void setOffset(index_t newOffset);
};

/**
 * Helper class encapsulating offsets for the various solution OptimizationMode types
 **/
class OptimizationOffsetTable {
  private:
    std::vector<OptimizationOffsets>
        offsetContainer;  //!< an array of 6 containers for offsets
                          //!< corresponding to the different solver modes
    index_t paramOffset = kNullLocation;  //!< offset for storing parameters in an array

  public:
    /**constructor
     */
    OptimizationOffsetTable() = default;
    /** check whether an offset set has been loaded
     * return a pointer to the set of offsets for a particular solver mode
     *@param[in] oMode the OptimizationMode we are interested in
     *@return a flag (true) if loaded (false) if not
     */
    bool isLoaded(const OptimizationMode& oMode) const
    {
        return offsetContainer[oMode.offsetIndex].loaded;
    }

    /** get the offsets for an OptimizationMode
     * @return the offsets for a particular solver mode
     *@param[in] oMode the OptimizationMode we are interested in
     */
    OptimizationOffsets& getOffsets(const OptimizationMode& oMode);

    /** get the offsets for an OptimizationMode for const object
     *  @return the offsets for a particular solver mode, or a null offset object
     *  if the mode index is out of range
     *@param[in] oMode the OptimizationMode we are interested in
     */
    const OptimizationOffsets& getOffsets(const OptimizationMode& oMode) const;

    /** set the offsets for an OptimizationMode
     *@param[in] oMode the OptimizationMode we are interested in
     *@param[in] newOffsets the offsets to store for the solver mode
     */
    void setOffsets(const OptimizationOffsets& newOffsets, const OptimizationMode& oMode);

    /** set the base offset
     *@param[in] newOffset the location to set the offset to
     *@param[in] oMode the OptimizationMode we are interested in
     */
    void setOffset(index_t newOffset, const OptimizationMode& oMode);

    /** set the continuous offset
     *@param[in] newOffset the location to set the offset to
     *@param[in] oMode the OptimizationMode we are interested in
     */
    void setContOffset(index_t newOffset, const OptimizationMode& oMode);

    /** set the voltage offset
     *@param[in] newOffset the location to set the offset to
     *@param[in] oMode the OptimizationMode we are interested in
     */
    void setIntOffset(index_t newOffset, const OptimizationMode& oMode);

    /** set the constraints offset
     *@param[in] newOffset the location to set the offset to
     *@param[in] oMode the OptimizationMode we are interested in
     */
    void setConstraintOffset(index_t newOffset, const OptimizationMode& oMode);

    /** get the angle offset
     *@param[in] oMode the OptimizationMode we are interested in
     *@return the angle offset
     */
    index_t getAngleOffset(const OptimizationMode& oMode) const;
    /** get the voltage offset
     *@param[in] oMode the OptimizationMode we are interested in
     *@return the voltage offset
     */
    index_t getVoltageOffset(const OptimizationMode& oMode) const;
    /** get the continuous offset
     *@param[in] oMode the OptimizationMode we are interested in
     *@return the diffferential offset
     */
    index_t getContOffset(const OptimizationMode& oMode) const;
    /** get the integer offset
     *@param[in] oMode the OptimizationMode we are interested in
     *@return the root offset
     */
    index_t getIntOffset(const OptimizationMode& oMode) const;

    /** get the real generation offset
     *@param[in] oMode the OptimizationMode we are interested in
     *@return the voltage offset
     */
    index_t getGenerationOffset(const OptimizationMode& oMode) const;

    /** get the reactive generation offset
     *@param[in] oMode the OptimizationMode we are interested in
     *@return the voltage offset
     */
    index_t getReactiveGenerationOffset(const OptimizationMode& oMode) const;
    /** Gets the offset reserved for parameter storage. */
    index_t getParamOffset() { return paramOffset; }

    /** Sets the offset reserved for parameter storage.
     * @param[in] newParamOffset new parameter offset
     */
    void setParamOffset(index_t newParamOffset) { paramOffset = newParamOffset; }
};

/**@brief class for containing optimization evaluation data information.
 *
 * OptimizationData mirrors StateData for optimization callbacks.  The
 * OptimizerInterface owns the actual arrays; this object is the lightweight
 * view passed through the distributed GridOptObject tree for a single
 * objective, constraint, gradient, or Jacobian evaluation.
 */
class OptimizationData {
  public:
    double time = 0.0;  //!< time corresponding to the optimization data
    count_t seqID = 0;  //!< sequence id differentiating subsequent evaluations
    index_t valueSize = 0;  //!< size of the decision-variable vector; if zero use source context
    index_t constraintSize = 0;  //!< size of the constraint vector; if zero use source context
    const double* val = nullptr;  //!< current decision-variable vector
    const double* multiplier = nullptr;  //!< optional constraint/objective multiplier vector
    double* scratch1 = nullptr;  //!< optional scratch space, owned by the optimizer interface
    double* scratch2 = nullptr;  //!< optional scratch space, owned by the optimizer interface

    /** Creates callback data referencing a candidate solution.
     * @param[in] evalTime time associated with this evaluation
     * @param[in] values decision-variable vector, or null when unavailable
     * @param[in] sequence identifier used to avoid repeated model updates
     * @param[in] variableCount number of values visible through this view
     * @param[in] constraintCount number of constraint slots visible to callbacks
     */
    OptimizationData(double evalTime = 0.0,
                     const double* values = nullptr,
                     count_t sequence = 0,
                     index_t variableCount = 0,
                     index_t constraintCount = 0):
        time(evalTime), seqID(sequence), valueSize(variableCount), constraintSize(constraintCount),
        val(values)
    {
    }

    /** Checks whether this view has a decision-variable vector. */
    bool empty() const { return (val == nullptr); }

    /** Checks whether callbacks should refresh cached model data.
     * @param[in] checkID sequence identifier previously processed by a model
     * @return true if the identifier changed, is zero, or this view is empty
     */
    bool updateRequired(count_t checkID) const
    {
        return ((checkID != seqID) || (seqID == 0) || empty());
    }
    /** Checks whether optional callback scratch storage is available. */
    bool hasScratch() const { return (scratch1 != nullptr); }
};

}  // namespace griddyn
