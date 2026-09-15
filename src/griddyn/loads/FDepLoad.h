/*
 * Copyright (c) 2014-2026, Lawrence Livermore National Security
 * See the top-level NOTICE for additional details. All rights reserved.
 * SPDX-License-Identifier: BSD-3-Clause
 */

#pragma once

#include "../Block.h"
#include "ExponentialLoad.h"
#include "core/CoreOwningPtr.hpp"
#include <string>
namespace griddyn::loads {
/** @brief a load with powers as a exponential function of voltage and frequency*/
class FDepLoad: public ExponentialLoad {
  public:
  protected:
    model_parameter betaP = 0.0;  //!< the frequency exponent parameter for the real power output
    model_parameter betaQ =
        0.0;  //!< the frequency exponent parameter for the reactive power output
    model_parameter powerScaleP = 1.0;  //!< optional real-power scale at the reference voltage
    model_parameter powerScaleQ = 1.0;  //!< optional reactive-power scale at the reference voltage
    model_parameter voltageReference =
        1.0;  //!< voltage at which the configured P and Q values are specified

    CoreOwningPtr<GridBlock> frequencyFilter;  //!< optional load-local frequency filter
    GridBus* frequencyBus = nullptr;  //!< optional resolved local BusFreq source

  public:
    explicit FDepLoad(const std::string& objName = "fdepLoad_$");
    /** constructor taking power arguments
@param[in] realPower the real power of the load
@param[in] reactivePower the reactive power of the load
@param[in] objName the name of the object
*/
    FDepLoad(double realPower, double reactivePower, const std::string& objName = "fdepLoad_$");

    virtual CoreObject* clone(CoreObject* obj = nullptr) const override;
    virtual void updateObjectLinkages(CoreObject* newRoot) override;

    /** Add an optional frequency filter block to this load. */
    virtual void add(CoreObject* obj) override;

    /**
     * @brief Set the optional load-local frequency filter.
     * @details The filter receives the owning bus frequency and its output is used by the
     * frequency-dependent power equations. The block is dynamically initialized as part of the
     * load and contributes its solver states.
     */
    void setFrequencyFilter(GridBlock* filter);

    /** @return the configured load-local frequency filter, or nullptr if none is configured. */
    GridBlock* getFrequencyFilter() { return frequencyFilter.get(); }
    const GridBlock* getFrequencyFilter() const { return frequencyFilter.get(); }

    /** Record a resolved local ANDES BusFreq source.
     * Remote BusFreq links are intentionally not represented by this model;
     * callers should leave this unset to use the owning bus frequency.
     */
    void setLocalFrequencyBus(GridBus* source) { frequencyBus = source; }
    const GridBus* getFrequencyBus() const { return frequencyBus; }

    virtual void dynObjectInitializeA(CoreTime time0, std::uint32_t flags) override;

  protected:
    virtual void dynObjectInitializeB(const IOdata& inputs,
                                      const IOdata& desiredOutput,
                                      IOdata& fieldSet) override;

  public:
    virtual void timestep(CoreTime time, const IOdata& inputs, const SolverMode& sMode) override;

    virtual void residual(const IOdata& inputs,
                          const StateData& stateDataValue,
                          double resid[],
                          const SolverMode& sMode) override;
    virtual void derivative(const IOdata& inputs,
                            const StateData& stateDataValue,
                            double deriv[],
                            const SolverMode& sMode) override;
    virtual void algebraicUpdate(const IOdata& inputs,
                                 const StateData& stateDataValue,
                                 double update[],
                                 const SolverMode& sMode,
                                 double alpha) override;
    virtual void jacobianElements(const IOdata& inputs,
                                  const StateData& stateDataValue,
                                  MatrixData<double>& matrixDataValue,
                                  const IOlocs& inputLocs,
                                  const SolverMode& sMode) override;
    virtual void outputPartialDerivatives(const IOdata& inputs,
                                          const StateData& stateDataValue,
                                          MatrixData<double>& matrixDataValue,
                                          const SolverMode& sMode) override;
    virtual count_t outputDependencyCount(index_t outputNum,
                                          const SolverMode& sMode) const override;

    virtual void set(std::string_view param, std::string_view val) override;
    virtual void
        set(std::string_view param, double val, units::unit unitType = units::defunit) override;

    virtual double get(std::string_view param,
                       units::unit unitType = units::defunit) const override;

    virtual void ioPartialDerivatives(const IOdata& inputs,
                                      const StateData& stateData,
                                      MatrixData<double>& matrixData,
                                      const IOlocs& inputLocs,
                                      const SolverMode& sMode) override;
    virtual double getRealPower(const IOdata& inputs,
                                const StateData& stateData,
                                const SolverMode& sMode) const override;
    virtual double getReactivePower(const IOdata& inputs,
                                    const StateData& stateData,
                                    const SolverMode& sMode) const override;
    virtual double getRealPower(double V) const override;
    virtual double getReactivePower(double V) const override;
    virtual double getRealPower() const override;
    virtual double getReactivePower() const override;
    /** get the real power input as a function of V and f
@param[in] V the voltage input in pu
@param[in] f the frequency input
@return the real load
*/
    virtual double getRealPower(double V, double f) const;
    /** get the reactive power input as a function of V and f
@param[in] V the voltage input in pu
@param[in] f the frequency input
@return the reactive load
*/
    virtual double getReactivePower(double V, double f) const;

  private:
    double getBusFrequency(const IOdata& inputs,
                           const StateData& stateDataValue,
                           const SolverMode& sMode) const;
    double getFrequency(const IOdata& inputs,
                        const StateData& stateDataValue,
                        const SolverMode& sMode) const;
    double getLocalFrequency() const;
};
}  // namespace griddyn::loads
