/*
 * Copyright (c) 2014-2026, Lawrence Livermore National Security
 * See the top-level NOTICE for additional details. All rights reserved.
 * SPDX-License-Identifier: BSD-3-Clause
 */

#pragma once

#include "GridComponent.h"
#include <string>

namespace griddyn {
class GridBus;
class GridArea;
class Link;
class Relay;

// these next two enumerations are used throughout the code base so I wouldn't recommend changing
// them if you want the code to work properly making it adaptive would require a lot changes to
// const strings and arrays so isn't worth it as I don't see a good reason for it to need to change
/** @brief locations for secondary input parameters (aka bus output locations)*/
enum SecondaryInputLocations {
    voltageInLocation = 0,
    angleInLocation = 1,
    frequencyInLocation = 2,
};
/** @brief locations grid secondary output locations*/
enum SecondaryOutputLocations {
    PoutLocation = 0,
    QoutLocation = 1,
};

/** @brief base class for top level simulation objects including GridBus, Link, gridRelays, and
GridArea GridPrimary class defines the interface for GridPrimary objects which are nominally objects
that can be contained by a root object which is an area usually,  though there is no restriction in
other classes also containing primary objects.

**/
class GridPrimary: public GridComponent {
  public:
    int zone = 1;  //!< publicly accessible loss zone indicator not used internally
    index_t locIndex2 = kNullLocation;  //!< a second lookup index for the object to reference
                                        //!< parent location in storage arrays for
    //!< use by containing objects with no operational dependencies

  public:
    /**@brief default constructor*/
    explicit GridPrimary(const std::string& objName = "");

    virtual CoreObject* clone(CoreObject* obj = nullptr) const override;

    virtual void pFlowInitializeA(CoreTime time0, std::uint32_t flags) override final;

    virtual void pFlowInitializeB() override final;

    virtual void dynInitializeA(CoreTime time0, std::uint32_t flags) override final;

    virtual void dynInitializeB(const IOdata& inputs,
                                const IOdata& desiredOutput,
                                IOdata& fieldSet) override final;

  public:
    virtual void set(std::string_view param, std::string_view val) override;

    virtual void
        set(std::string_view param, double val, units::unit unitType = units::defunit) override;

    virtual double get(std::string_view param,
                       units::unit unitType = units::defunit) const override;

    virtual void setState(CoreTime time,
                          const double state[],
                          const double dstateDt[],
                          const SolverMode& sMode) override;
    /** @brief get the residual computation for object requiring a delay
      basically calls the residual calculation on the delayed objects
    @param[in] stateDataValue the data representing the current state to operate on
    @param[out] resid the array to store the computed derivative values
    @param[in] sMode the SolverMode which is being solved for
    */
    virtual void delayedResidual(const IOdata& inputs,
                                 const StateData& stateDataValue,
                                 double resid[],
                                 const SolverMode& sMode);

    /** @brief get the residual computation for object requiring a delay
      basically calls the derivative calculation on the delayed objects
    @param[in] stateDataValue the data representing the current state to operate on
    @param[out] deriv the array to store the computed derivative values
    @param[in] sMode the SolverMode which is being solved for
    */
    virtual void delayedDerivative(const IOdata& inputs,
                                   const StateData& stateDataValue,
                                   double deriv[],
                                   const SolverMode& sMode);

    /** @brief get the algebraic update for object requesting a delay
      basically calls the residual calculation on the delayed objects
    @param[in] stateDataValue the data representing the current state to operate on
    @param[out] update the array to store the computed derivative values
    @param[in] sMode the SolverMode which is being solved for
    */
    virtual void delayedAlgebraicUpdate(const IOdata& inputs,
                                        const StateData& stateDataValue,
                                        double update[],
                                        const SolverMode& sMode,
                                        double alpha);

    /** @brief get the residual computation for object requiring a delay
      basically calls the Jacobian calculation on the delayed objects
    @param[in] stateDataValue the data representing the current state to operate on
    @param[out] matrixDataValue the MatrixData structure to store the Jacobian values
    @param[in] sMode the SolverMode which is being solved for
    */
    virtual void delayedJacobian(const IOdata& inputs,
                                 const StateData& stateDataValue,
                                 MatrixData<double>& matrixDataValue,
                                 const IOlocs& inputLocs,
                                 const SolverMode& sMode);

    /** @brief  try to shift the states to something more consistent
      called when the current states do not make a consistent condition,  calling converge will
    attempt to move them to a more valid state mode controls how this is done  0- does a single
    iteration loop mode=1 tries to iterate until convergence based on tol mode=2  tries harder
    mode=3 does it with voltage only
    @param[in] time  the time of the corresponding states
    @param[in,out]  state the states of the system at present and shifted to match the updates
    @param[in,out] dstateDt  the derivatives of the state that get updated
    @param[in] sMode the SolverMode matching the states
    @param[in] mode  the mode of the convergence
    @param[in] tol  the convergence tolerance
    */
    virtual void converge(CoreTime time,
                          double state[],
                          double dstateDt[],
                          const SolverMode& sMode,
                          ConvergeMode mode = ConvergeMode::high_error_only,
                          double tol = 0.01);

    /** @brief do a check on the power flow results
     * checks for an violations of recommended power flow levels such as voltage, power limits,
     * transfer capacity, angle limits, etc
     * @param[out] Violation_vector a storage location for any detected violations
     */
    virtual void pFlowCheck(std::vector<Violation>& Violation_vector);

    using GridComponent::updateLocalCache;
    /** @brief do any local computation to get ready for measurements*/
    virtual void updateLocalCache();

    /**
    *@brief get a pointer for a particular bus
    @param[in] num the index of the bus being requested
    @return a pointer to the requested bus or nullptr
    **/
    virtual GridBus* getBus(index_t num) const;

    /**
    *@brief get a pointer for a particular Link
    @param[in] num the index of the link being requested
    @return a pointer to the requested link or nullptr
    **/
    virtual Link* getLink(index_t num) const;

    /**
    *@brief get a pointer for a particular GridArea
    @param[in] num the index of the area being requested
    @return a pointer to the requested area or nullptr
    **/
    virtual GridArea* getArea(index_t num) const;
    GridArea* getGridArea(index_t num) const { return getArea(num); }

    /**
    *@brief get a pointer for a particular relay
    @param[in] num the index of the relay being requested
    @return a pointer to the requested relay or nullptr
    **/
    virtual Relay* getRelay(index_t num) const;
};

}  // namespace griddyn
