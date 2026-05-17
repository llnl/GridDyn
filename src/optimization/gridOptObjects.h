/*
 * Copyright (c) 2014-2026, Lawrence Livermore National Security
 * See the top-level NOTICE for additional details. All rights reserved.
 * SPDX-License-Identifier: BSD-3-Clause
 */

#pragma once

#include "core/coreObject.h"
#include "optHelperClasses.h"
#include <array>
#include <bitset>
#include <map>
#include <string>

template<class Y>
class vectData;

template<class Y>
class matrixData;

namespace griddyn {
class consData;
/** base object for gridDynOptimizations
 * the basic object for creating a power system encapsulating some common functions and data that is
 *needed by all objects in the simulation and defining some common methods for use by all objects.
 *This object is not really intended to be instantiated directly and is mostly a common interface to
 *inheriting objects gridPrimary, gridSecondary, and gridSubModel as it encapsulated common
 *functionality between those objects
 **/
class GridOptObject: public CoreObject {
  public:
    std::bitset<32> optFlags;  //!< operational flags these flags are designed to be normal false
    count_t numParams = 0;  //!< the number of parameters to store in an archive
    OptimizationOffsetTable offsets;  //!< a table of offsets for the different solver modes
  protected:
  public:
    GridOptObject(const std::string& objName = "optObject_#");
    virtual CoreObject* clone(CoreObject* obj = nullptr) const override;
    virtual void set(std::string_view param, std::string_view val) override;

    virtual void
        set(std::string_view param, double val, units::unit unitType = units::defunit) override;
    /** set the offsets of an object for a particular optimization mode.
    @param newOffsets the offset set to use.
    @param oMode the optimization mode to use.
    */
    virtual void setOffsets(const OptimizationOffsets& newOffsets, const OptimizationMode& oMode);
    /** set the offsets of an object for a particular optimization mode using a single offset.
    @param offset the offset index all variables are sequential.
    @param oMode the optimization mode to use.
    */
    virtual void setOffset(index_t offset, index_t constraintOffset, const OptimizationMode& oMode);

    // size getter functions

    /** get the objective size.
    @param oMode the optimization mode to use.
    */
    count_t objSize(const OptimizationMode& oMode);
    /** get the number of continuous objective variables.
    @param oMode the optimization mode to use.
    */
    count_t contObjSize(const OptimizationMode& oMode);
    /** get the number of integer objective variables.
    @param oMode the optimization mode to use.
    */
    count_t intObjSize(const OptimizationMode& oMode);
    /** get the number of real power generation objective variables.
    @param oMode the optimization mode to use.
    */
    count_t genSize(const OptimizationMode& oMode);
    /** get the number of reactive power generation objective variables.
    @param oMode the optimization mode to use.
    */
    count_t qSize(const OptimizationMode& oMode);
    /** get the number of voltage objective variables.
    @param oMode the optimization mode to use.
    */
    count_t vSize(const OptimizationMode& oMode);
    /** get the number of angle objective variables.
    @param oMode the optimization mode to use.
    */
    count_t aSize(const OptimizationMode& oMode);
    /** get the number of constraints.
    @param oMode the optimization mode to use.
    */
    count_t constraintSize(const OptimizationMode& oMode);

    /** get the objective size.
    @param oMode the optimization mode to use.
    */
    count_t objSize(const OptimizationMode& oMode) const;
    /** get the number of continuous objective variables.
    @param oMode the optimization mode to use.
    */
    count_t contObjSize(const OptimizationMode& oMode) const;
    /** get the number of integer objective variables.
    @param oMode the optimization mode to use.
    */
    count_t intObjSize(const OptimizationMode& oMode) const;
    /** get the number of real power generation objective variables.
    @param oMode the optimization mode to use.
    */
    count_t genSize(const OptimizationMode& oMode) const;
    /** get the number of reactive power generation objective variables.
    @param oMode the optimization mode to use.
    */
    count_t qSize(const OptimizationMode& oMode) const;
    /** get the number of voltage objective variables.
    @param oMode the optimization mode to use.
    */
    count_t vSize(const OptimizationMode& oMode) const;
    /** get the number of angle objective variables.
    @param oMode the optimization mode to use.
    */
    count_t aSize(const OptimizationMode& oMode) const;
    /** get the number of constraints.
    @param oMode the optimization mode to use.
    */
    count_t constraintSize(const OptimizationMode& oMode) const;

  protected:
    /** initialize the object.
    @param flags the optimization mode to use.
    */
    virtual void dynObjectInitializeA(std::uint32_t flags);

  public:
    /** initialize the object.
    @param flags the optimization mode to use.
    */
    void dynInitializeA(std::uint32_t flags);

  protected:
    /** initialize the object.
    @param flags the optimization mode to use.
    */
    virtual void dynObjectInitializeB(std::uint32_t flags);

  public:
    /** initialize the object.
    @param flags the optimization mode to use.
    */
    void dynInitializeB(std::uint32_t flags);
    /** compute the sizes and store them in the offsetTables.
    @param oMode the optimization mode to use.
    */
    virtual void loadSizes(const OptimizationMode& oMode);

    /** set the objective variable values to the objects.
    @param of  the output objective variable values
    @param oMode the optimization mode to use.
    */
    virtual void setValues(const OptimizationData& of, const OptimizationMode& oMode);

    /** get a guessState from the object as to the value.
    @param val  the output objective variable values
    @param oMode the optimization mode to use.
    */
    virtual void guessState(double time, double val[], const OptimizationMode& oMode);

    /** set the tolerances.
    @param oMode the optimization mode to use.
    */
    virtual void getTols(double tols[], const OptimizationMode& oMode);

    /** indicate if a variable is continuous or integer.
    @param sdata  the vector of indices for integer value 0 continuous, 1 integer
    @param oMode the optimization mode to use.
    */
    virtual void getVariableType(double sdata[], const OptimizationMode& oMode);

    /**load the upper and lower limit for an objective variable
    @param upLimit  the upper limit
    @param lowerLimit  the lower limit
    @param oMode the optimization mode to use.
    */
    virtual void valueBounds(double time,
                             double upLimit[],
                             double lowerLimit[],
                             const OptimizationMode& oMode);

    /** load the linear objective parameters
    @param of  the current objective variable values
    @param linObj the structure to store the objective parameters
    @param oMode the optimization mode to use.
    */
    virtual void linearObj(const OptimizationData& optimizationDataRef,
                           vectData<double>& linearObjective,
                           const OptimizationMode& optimizationMode);
    /** load the quadratic objective parameters
    @param of  the current object variable values
    @param linObj the structure to store the linear objective parameters
    @param quadObj the structure to store the 2nd order objective parameters
    @param oMode the optimization mode to use.
    */
    virtual void quadraticObj(const OptimizationData& optimizationDataRef,
                              vectData<double>& linearObjective,
                              vectData<double>& quadraticObjective,
                              const OptimizationMode& optimizationMode);

    /** compute the objective value
    @param of  the current object variable values
    @param oMode the optimization mode to use.
    @return the objective value
    */
    virtual double objValue(const OptimizationData& of, const OptimizationMode& oMode);

    /** compute the gradients of the objective function
    @param of  the current object variable values
    @param grad the vector containing all \frac{dC}{dO_i}
    @param oMode the optimization mode to use.
    */
    virtual void gradient(const OptimizationData& of, double grad[], const OptimizationMode& oMode);

    /** compute the Jacobian entries for the objective value
    @param of  the current object variable values
    @param md the structure for storing \frac{dC_i}{dO_j}
    @param oMode the optimization mode to use.
    */
    virtual void jacobianElements(const OptimizationData& of,
                                  matrixData<double>& md,
                                  const OptimizationMode& oMode);

    // constraint functions
    /** get the linear constraint operations
    @details each constraint is a linear sum of coefficients of the objective values
    @param[in] of  the current object variable values
    @param[out] cons the structure for the constraint parameters storing the coefficients, the upper
    and lower limit
    @param[out] upperLimit value for the upper bound on the constraint function
    @param[out] lowerLimit value for the lower bound on the constraint function
    @param[in] oMode the optimization mode to use.
    */
    virtual void getConstraints(const OptimizationData& of,
                                matrixData<double>& cons,
                                double upperLimit[],
                                double lowerLimit[],
                                const OptimizationMode& oMode);

    /** get the (non)linear constraint operations
    @param of  the current object variable values
    @param cVals the computed value of the constraint
    @param oMode the optimization mode to use.
    */
    virtual void
        constraintValue(const OptimizationData& of, double cVals[], const OptimizationMode& oMode);
    /** get the Jacobian array of the constraints \frac{dCV_i}{dO_j}
    @param of  the current object variable values
    @param md the structure for the constraint Jacobian entries
    @param oMode the optimization mode to use.
    */
    virtual void constraintJacobianElements(const OptimizationData& of,
                                            matrixData<double>& md,
                                            const OptimizationMode& oMode);

    /** get the Hessian array for the objective function
    @param of  the current object variable values
    @param md the structure for the constraint Jacobian entries
    @param oMode the optimization mode to use.
    */
    virtual void hessianElements(const OptimizationData& of,
                                 matrixData<double>& md,
                                 const OptimizationMode& oMode);

    /** get the names of the objective variables
    @param objNames  the location to store the names
    @param oMode the optimization mode to use.
    @param prefix (optional) string to place before the objective name
    */
    virtual void getObjectiveNames(stringVec& objectiveNames,
                                   const OptimizationMode& oMode,
                                   const std::string& prefix = "");

    /** get a specific bus
    @param index  the index of the bus to return
    @return the gridOptObject represented by the bus index
    */
    virtual GridOptObject* getBus(index_t index) const;

    /** get a specific area
    @param index  the index of the area to return
    @return the gridOptObject represented by the area index
    */
    virtual GridOptObject* getArea(index_t index) const;

    /** get a specific Link
    @param index  the index of the area to return
    @return the gridOptObject represented by the link  index
    */
    virtual GridOptObject* getLink(index_t index) const;

    /** get a specific relay
    @param index  the index of the area to return
    @return the gridOptObject represented by the link  index
    */
    virtual GridOptObject* getRelay(index_t index) const;
};

void printObjectiveStateNames(GridOptObject* obj, const OptimizationMode& oMode);

}  // namespace griddyn

