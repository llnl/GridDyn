/*
 * Copyright (c) 2014-2026, Lawrence Livermore National Security
 * See the top-level NOTICE for additional details. All rights reserved.
 * SPDX-License-Identifier: BSD-3-Clause
 */

#pragma once

#include "GridSubModel.h"
#include <string>
#include <vector>
namespace griddyn {
constexpr index_t genModelEftInLocation = 2;
constexpr index_t genModelPmechInLocation = 3;

class GridBus;

/** @brief model simulation implementing a generator model into GridDyn
 the GenModel implements a very basic generator model with 4 states the real and reactive
currents as algebraic states and the generator rotational speed and angle otherwise known as the
second order model
*/
class GenModel: public GridSubModel {
  public:
    /** @brief set of flags used by genModels for variations in computation
     */
    enum GenModelFlags {

        useSaturationFlag =
            object_flag2,  //!< flag indicating that the simulation should use a saturation model
        useSpeedFieldAdjustment = object_flag3,  //!< flag indicating that the simulation should
                                                 //!< use a speed field adjustment
        useFrequencyImpedanceCorrection = object_flag4,  //!< flag indicating that the model should
                                                         //!< use frequency impedance corrections
        internalFrequencyCalculation = object_flag5,
        atAngleLimits = object_flag6,
    };

  protected:
    double machineBasePower = 100;  //!< [pu]  the operating base of the generator
    double Xd = 1.05;  //!< [pu] d-axis reactance
    double Rs = 0.0;  //!< [pu] generator resistance
    GridBus* bus = nullptr;  //!< reference to the connected bus;
  public:
    //!< @brief default constructor
    explicit GenModel(const std::string& objName = "genModel_#");
    virtual CoreObject* clone(CoreObject* obj = nullptr) const override;

    virtual void dynObjectInitializeB(const IOdata& inputs,
                                      const IOdata& desiredOutput,
                                      IOdata& fieldSet) override;

    virtual void set(std::string_view param, std::string_view val) override;
    virtual void
        set(std::string_view param, double val, units::unit unitType = units::defunit) override;

    virtual IOdata getOutputs(const IOdata& inputs,
                              const stateData& stateDataValue,
                              const SolverMode& sMode) const override;

    virtual double getOutput(const IOdata& inputs,
                             const stateData& stateDataValue,
                             const SolverMode& sMode,
                             index_t outNum = 0) const override;
    virtual double getOutput(index_t outNum = 0) const override;

    virtual void ioPartialDerivatives(const IOdata& inputs,
                                      const stateData& stateDataValue,
                                      matrixData<double>& matrixDataValue,
                                      const IOlocs& inputLocs,
                                      const SolverMode& sMode) override;

    virtual count_t outputDependencyCount(index_t num, const SolverMode& sMode) const override;
    // TODO(phlpt): Split these into separate value and offset accessors.
    virtual double getFreq(const stateData& stateDataValue,
                           const SolverMode& sMode,
                           index_t* freqOffset = nullptr) const;
    virtual double getAngle(const stateData& stateDataValue,
                            const SolverMode& sMode,
                            index_t* angleOffset = nullptr) const;

    virtual const std::vector<stringVec>& inputNames() const override;
    virtual const std::vector<stringVec>& outputNames() const override;
};
}  // namespace griddyn
