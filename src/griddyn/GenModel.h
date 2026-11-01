/*
 * Copyright (c) 2014-2026, Lawrence Livermore National Security
 * See the top-level NOTICE for additional details. All rights reserved.
 * SPDX-License-Identifier: BSD-3-Clause
 */

#pragma once

#include "ControllerSignals.h"
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

        USE_SATURATION_FLAG =
            OBJECT_FLAG2,  //!< flag indicating that the simulation should use a saturation model
        USE_SPEED_FIELD_ADJUSTMENT = OBJECT_FLAG3,  //!< flag indicating that the simulation should
                                                    //!< use a speed field adjustment
        USE_FREQUENCY_IMPEDANCE_CORRECTION =
            OBJECT_FLAG4,  //!< flag indicating that the model should
                           //!< use frequency impedance corrections
        INTERNAL_FREQUENCY_CALCULATION = OBJECT_FLAG5,
        AT_ANGLE_LIMITS = OBJECT_FLAG6,
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
                              const StateData& stateDataValue,
                              const SolverMode& sMode) const override;

    virtual double getOutput(const IOdata& inputs,
                             const StateData& stateDataValue,
                             const SolverMode& sMode,
                             index_t outNum = 0) const override;
    virtual double getOutput(index_t outNum = 0) const override;

    virtual void ioPartialDerivatives(const IOdata& inputs,
                                      const StateData& stateDataValue,
                                      MatrixData<double>& matrixDataValue,
                                      const IOlocs& inputLocs,
                                      const SolverMode& sMode) override;

    virtual count_t outputDependencyCount(index_t num, const SolverMode& sMode) const override;
    // TODO(phlpt): Split these into separate value and offset accessors.
    virtual double getFreq(const StateData& stateDataValue,
                           const SolverMode& sMode,
                           index_t* freqOffset = nullptr) const;
    virtual double getAngle(const StateData& stateDataValue,
                            const SolverMode& sMode,
                            index_t* angleOffset = nullptr) const;

    /** Return controller-facing machine signals in the convention documented
     * by MachineControllerSignal. Unsupported models return kNullVal entries.
     */
    virtual IOdata getMachineControllerSignals(const IOdata& inputs,
                                               const StateData& stateDataValue,
                                               const SolverMode& sMode) const;

    /** Return sparse derivatives of each controller-facing machine signal.
     * Locations refer to the containing simulation's state/input vector.
     */
    virtual MachineSignalDerivativeData
        getMachineControllerSignalDerivatives(const IOdata& inputs,
                                              const StateData& stateDataValue,
                                              const IOlocs& inputLocs,
                                              const SolverMode& sMode) const;

    virtual const std::vector<stringVec>& inputNames() const override;
    virtual const std::vector<stringVec>& outputNames() const override;
};
}  // namespace griddyn
