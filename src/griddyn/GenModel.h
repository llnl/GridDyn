/*
 * Copyright (c) 2014-2026, Lawrence Livermore National Security
 * See the top-level NOTICE for additional details. All rights reserved.
 * SPDX-License-Identifier: BSD-3-Clause
 */

#pragma once

#include "gridSubModel.h"
#include <string>
#include <vector>
namespace griddyn {
constexpr index_t genModelEftInLocation = 2;
constexpr index_t genModelPmechInLocation = 3;

class gridBus;

/** @brief model simulation implementing a generator model into GridDyn
 the GenModel implements a very basic generator model with 4 states the real and reactive
currents as algebraic states and the generator rotational speed and angle otherwise known as the
second order model
*/
class GenModel: public gridSubModel {
  public:
    /** @brief set of flags used by genModels for variations in computation
     */
    enum genModel_flags {

        use_saturation_flag =
            object_flag2,  //!< flag indicating that the simulation should use a saturation model
        use_speed_field_adjustment = object_flag3,  //!< flag indicating that the simulation should
                                                    //!< use a speed field adjustment
        use_frequency_impedance_correction =
            object_flag4,  //!< flag indicating that the model should use frequency impedance
                           //!< corrections
        internal_frequency_calculation = object_flag5,
        at_angle_limits = object_flag6,
    };

  protected:
    double machineBasePower = 100;  //!< [pu]  the operating base of the generator
    double Xd = 1.05;  //!< [pu] d-axis reactance
    double Rs = 0.0;  //!< [pu] generator resistance
    gridBus* bus = nullptr;  //!< reference to the connected bus;
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
                              const stateData& sD,
                              const solverMode& sMode) const override;

    virtual double getOutput(const IOdata& inputs,
                             const stateData& sD,
                             const solverMode& sMode,
                             index_t outNum = 0) const override;
    virtual double getOutput(index_t outNum = 0) const override;

    virtual void ioPartialDerivatives(const IOdata& inputs,
                                      const stateData& sD,
                                      matrixData<double>& md,
                                      const IOlocs& inputLocs,
                                      const solverMode& sMode) override;

    virtual count_t outputDependencyCount(index_t num, const solverMode& sMode) const override;
    // TODO(phlpt): Split these into separate value and offset accessors.
    virtual double
        getFreq(const stateData& sD, const solverMode& sMode, index_t* freqOffset = nullptr) const;
    virtual double getAngle(const stateData& sD,
                            const solverMode& sMode,
                            index_t* angleOffset = nullptr) const;

    virtual const std::vector<stringVec>& inputNames() const override;
    virtual const std::vector<stringVec>& outputNames() const override;
};
}  // namespace griddyn

