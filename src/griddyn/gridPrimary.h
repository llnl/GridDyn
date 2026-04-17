#pragma once

/*
 * Copyright (c) 2014-2026, Lawrence Livermore National Security
 * See the top-level NOTICE for additional details. All rights reserved.
 * SPDX-License-Identifier: BSD-3-Clause
 */

#include "gridComponent.h"
#include <string>

namespace griddyn {
class gridBus;
class Area;
class Link;
class Relay;

// these next two enumerations are used throughout the code base so I wouldn't recommend changing
// them if you want the code to work properly making it adaptive would require a lot changes to
// const strings and arrays so isn't worth it as I don't see a good reason for it to need to change
/** @brief locations for secondary input parameters (aka bus output locations)*/
enum secondary_input_locations {
    voltageInLocation = 0,
    angleInLocation = 1,
    frequencyInLocation = 2,
};
/** @brief locations grid secondary output locations*/
enum secondary_output_locations {
    PoutLocation = 0,
    QoutLocation = 1,
};

/** @brief base class for top level simulation objects including gridBus, Link, gridRelays, and Area
  gridPrimary class defines the interface for gridPrimary objects which are nominally objects that
can be contained by a root object which is an area usually,  though there is no restriction in other
classes also containing primary objects.

**/
class gridPrimary: public gridComponent {
  public:
    int zone = 1;  //!< publicly accessible loss zone indicator not used internally
    index_t locIndex2 = kNullLocation;  //!< a second lookup index for the object to reference
                                        //!< parent location in storage arrays for
    //!< use by containing objects with no operational dependencies

  public:
    /**@brief default constructor*/
    explicit gridPrimary(const std::string& objName = "");

    virtual coreObject* clone(coreObject* obj = nullptr) const override;

    virtual void pFlowInitializeA(coreTime time0, std::uint32_t flags) override final;

    virtual void pFlowInitializeB() override final;

    virtual void dynInitializeA(coreTime time0, std::uint32_t flags) override final;

    virtual void dynInitializeB(const IOdata& inputs,
                                const IOdata& desiredOutput,
                                IOdata& fieldSet) override final;

  public:
    virtual void set(const std::string& param, const std::string& val) override;

    virtual void
        set(const std::string& param, double val, units::unit unitType = units::defunit) override;

    virtual double get(const std::string& param,
                       units::unit unitType = units::defunit) const override;

    virtual void setState(coreTime time,
                          const double state[],
                          const double dstate_dt[],
                          const solverMode& sMode) override;
    /** @brief get the residual computation for object requiring a delay
      basically calls the residual calculation on the delayed objects
    @param[in] sD the data representing the current state to operate on
    @param[out] resid the array to store the computed derivative values
    @param[in] sMode the solverMode which is being solved for
    */
    virtual void delayedResidual(const IOdata& inputs,
                                 const stateData& sD,
                                 double resid[],
                                 const solverMode& sMode);

    /** @brief get the residual computation for object requiring a delay
      basically calls the derivative calculation on the delayed objects
    @param[in] sD the data representing the current state to operate on
    @param[out] deriv the array to store the computed derivative values
    @param[in] sMode the solverMode which is being solved for
    */
    virtual void delayedDerivative(const IOdata& inputs,
                                   const stateData& sD,
                                   double deriv[],
                                   const solverMode& sMode);

    /** @brief get the algebraic update for object requesting a delay
      basically calls the residual calculation on the delayed objects
    @param[in] sD the data representing the current state to operate on
    @param[out] update the array to store the computed derivative values
    @param[in] sMode the solverMode which is being solved for
    */
    virtual void delayedAlgebraicUpdate(const IOdata& inputs,
                                        const stateData& sD,
                                        double update[],
                                        const solverMode& sMode,
                                        double alpha);

    /** @brief get the residual computation for object requiring a delay
      basically calls the Jacobian calculation on the delayed objects
    @param[in] sD the data representing the current state to operate on
    @param[out] md the matrixData structure to store the Jacobian values
    @param[in] sMode the solverMode which is being solved for
    */
    virtual void delayedJacobian(const IOdata& inputs,
                                 const stateData& sD,
                                 matrixData<double>& md,
                                 const IOlocs& inputLocs,
                                 const solverMode& sMode);

    /** @brief  try to shift the states to something more consistent
      called when the current states do not make a consistent condition,  calling converge will
    attempt to move them to a more valid state mode controls how this is done  0- does a single
    iteration loop mode=1 tries to iterate until convergence based on tol mode=2  tries harder
    mode=3 does it with voltage only
    @param[in] time  the time of the corresponding states
    @param[in,out]  state the states of the system at present and shifted to match the updates
    @param[in,out] dstate_dt  the derivatives of the state that get updated
    @param[in] sMode the solverMode matching the states
    @param[in] mode  the mode of the convergence
    @param[in] tol  the convergence tolerance
    */
    virtual void converge(coreTime time,
                          double state[],
                          double dstate_dt[],
                          const solverMode& sMode,
                          converge_mode mode = converge_mode::high_error_only,
                          double tol = 0.01);

    /** @brief do a check on the power flow results
     * checks for an violations of recommended power flow levels such as voltage, power limits,
     * transfer capacity, angle limits, etc
     * @param[out] Violation_vector a storage location for any detected violations
     */
    virtual void pFlowCheck(std::vector<Violation>& Violation_vector);

    using gridComponent::updateLocalCache;
    /** @brief do any local computation to get ready for measurements*/
    virtual void updateLocalCache();

    /**
    *@brief get a pointer for a particular bus
    @param[in] num the index of the bus being requested
    @return a pointer to the requested bus or nullptr
    **/
    virtual gridBus* getBus(index_t num) const;

    /**
    *@brief get a pointer for a particular Link
    @param[in] num the index of the link being requested
    @return a pointer to the requested link or nullptr
    **/
    virtual Link* getLink(index_t num) const;

    /**
    *@brief get a pointer for a particular Area
    @param[in] num the index of the area being requested
    @return a pointer to the requested area or nullptr
    **/
    virtual Area* getArea(index_t num) const;

    /**
    *@brief get a pointer for a particular relay
    @param[in] num the index of the relay being requested
    @return a pointer to the requested relay or nullptr
    **/
    virtual Relay* getRelay(index_t num) const;
};

}  // namespace griddyn
