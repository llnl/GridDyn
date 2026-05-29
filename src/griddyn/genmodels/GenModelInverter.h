/*
 * Copyright (c) 2014-2026, Lawrence Livermore National Security
 * See the top-level NOTICE for additional details. All rights reserved.
 * SPDX-License-Identifier: BSD-3-Clause
 */

#pragma once

#include "../GenModel.h"
#include <string>
#include <vector>

namespace griddyn::genmodels {
/** @brief model simulation implementing a simple inverter model
the GenModel implements a very basic inverter model modeling the generator as a transmission line
with very fast angle adjustments to keep the mechanical input power balanced
*/
class GenModelInverter: public GenModel {
  public:
  protected:
    double maxAngle = 89.0 * kPI / 180.0;  //!< maximum firing angle
    double minAngle = -89.0 * kPI / 180.0;  //!< minimum firing angle
  public:
    //!< @brief default constructor
    explicit GenModelInverter(const std::string& objName = "genModel_#");
    virtual CoreObject* clone(CoreObject* obj = nullptr) const override;

    virtual void dynObjectInitializeA(coreTime time0, std::uint32_t flags) override;
    virtual void dynObjectInitializeB(const IOdata& inputs,
                                      const IOdata& desiredOutput,
                                      IOdata& fieldSet) override;

    virtual void set(std::string_view param, std::string_view val) override;
    virtual void
        set(std::string_view param, double val, units::unit unitType = units::defunit) override;

    virtual stringVec localStateNames() const override;
    // dynamics

    virtual void residual(const IOdata& inputs,
                          const StateData& stateDataValue,
                          double resid[],
                          const SolverMode& sMode) override;

    virtual IOdata getOutputs(const IOdata& inputs,
                              const StateData& stateDataValue,
                              const SolverMode& sMode) const override;

    using GenModel::getOutput;
    virtual double getOutput(const IOdata& inputs,
                             const StateData& stateDataValue,
                             const SolverMode& sMode,
                             index_t outNum = 0) const override;

    virtual void jacobianElements(const IOdata& inputs,
                                  const StateData& stateDataValue,
                                  matrixData<double>& matrixDataValue,
                                  const IOlocs& inputLocs,
                                  const SolverMode& sMode) override;
    virtual void outputPartialDerivatives(const IOdata& inputs,
                                          const StateData& stateDataValue,
                                          matrixData<double>& matrixDataValue,
                                          const SolverMode& sMode) override;
    virtual count_t outputDependencyCount(index_t num, const SolverMode& sMode) const override;
    virtual void ioPartialDerivatives(const IOdata& inputs,
                                      const StateData& stateDataValue,
                                      matrixData<double>& matrixDataValue,
                                      const IOlocs& inputLocs,
                                      const SolverMode& sMode) override;

    virtual void algebraicUpdate(const IOdata& inputs,
                                 const StateData& stateDataValue,
                                 double update[],
                                 const SolverMode& sMode,
                                 double alpha) override;
    /** helper function to get omega and its state location
     */
    virtual double getFreq(const StateData& stateDataValue,
                           const SolverMode& sMode,
                           index_t* freqOffset = nullptr) const override;
    virtual double getAngle(const StateData& stateDataValue,
                            const SolverMode& sMode,
                            index_t* angleOffset = nullptr) const override;
    virtual void rootTest(const IOdata& inputs,
                          const StateData& stateDataValue,
                          double roots[],
                          const SolverMode& sMode) override;
    virtual void rootTrigger(coreTime time,
                             const IOdata& inputs,
                             const std::vector<int>& rootMask,
                             const SolverMode& sMode) override;
    virtual ChangeCode rootCheck(const IOdata& inputs,
                                 const StateData& stateDataValue,
                                 const SolverMode& sMode,
                                 CheckLevel level) override;

  private:
    void reCalcImpedences();
    /** @brief compute the real power output
@param voltage voltage
@param exciterField Exciter field
@param cosA  the cosine of the power angle
@param sinA  the sine of the power angle
@return the real power output;
*/
    double realPowerCompute(double voltage, double exciterField, double cosA, double sinA) const;
    /** @brief compute the reactive power output
@param voltage voltage
@param exciterField Exciter field
@param cosA  the cosine of the power angle
@param sinA  the sine of the power angle
@return the real power output;
*/
    double
        reactivePowerCompute(double voltage, double exciterField, double cosA, double sinA) const;
    double g = 0;
    double b = (1.0 / 1.05);
};

}  // namespace griddyn::genmodels
