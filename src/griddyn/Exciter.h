/*
 * Copyright (c) 2014-2026, Lawrence Livermore National Security
 * See the top-level NOTICE for additional details. All rights reserved.
 * SPDX-License-Identifier: BSD-3-Clause
 */

#pragma once

#include "gridSubModel.h"
#include "units/units_decl.hpp"
#include <string>
#include <vector>
namespace griddyn {
class Generator;
/** @brief the exciter class defines the interface for power grid exciters and implements a very
 * simple version of such a device
 */

inline constexpr int exciterVoltageInLocation = 0;
inline constexpr int exciterVsetInLocation = 1;
inline constexpr int exciterPmechInLocation = 2;
inline constexpr int exciterOmegaInLocation = 3;

/** class defining the interface for an exciter as well a trivial implementation of such
 */
class Exciter: public GridSubModel {
  public:
    enum exciter_flags {
        outside_vlim = object_flag3,
        etrigger_high = object_flag4,
    };

  protected:
    // model_parameter Vrmin = -5.1;  //!< [pu] lower voltage limit
    model_parameter Vrmin{-5.1};  //!< [pu] lower voltage limit
    model_parameter Vrmax = 6;  //!< [pu] upper voltage limit
    model_parameter Vref = 1.0;  //!< [pu] reference voltage for voltage regulator
    model_parameter Ka = 10;  //!< [pu] amplifier gain
    model_parameter Ta = 0.004;  //!< [s]    amplifier time constant
    model_parameter vBias = 0.0;  //!< bias field level for adjusting the field output so the ref
                                  //!< can remain at some nominal level
    int limitState = 0;  //!< indicator of which state has the limits applied
  public:
    /** @brief constructor*/
    explicit Exciter(const std::string& objName = "exciter_#");
    virtual CoreObject* clone(CoreObject* obj = nullptr) const override;

    virtual void dynObjectInitializeA(coreTime time0, std::uint32_t flags) override;
    virtual void dynObjectInitializeB(const IOdata& inputs,
                                      const IOdata& desiredOutput,
                                      IOdata& fieldSet) override;
    virtual void set(std::string_view param, std::string_view val) override;
    virtual void
        set(std::string_view param, double val, units::unit unitType = units::defunit) override;

    virtual stringVec localStateNames() const override;

    virtual void residual(const IOdata& inputs,
                          const stateData& stateData,
                          double resid[],
                          const solverMode& solverMode) override;
    virtual void derivative(const IOdata& inputs,
                            const stateData& stateData,
                            double deriv[],
                            const solverMode& solverMode) override;
    virtual void jacobianElements(const IOdata& inputs,
                                  const stateData& stateData,
                                  matrixData<double>& matrix,
                                  const IOlocs& inputLocs,
                                  const solverMode& solverMode) override;
    // handle the rootfinding functions
    virtual void rootTest(const IOdata& inputs,
                          const stateData& stateData,
                          double root[],
                          const solverMode& solverMode) override;
    virtual void rootTrigger(coreTime time,
                             const IOdata& inputs,
                             const std::vector<int>& rootMask,
                             const solverMode& solverMode) override;
    virtual change_code rootCheck(const IOdata& inputs,
                                  const stateData& stateData,
                                  const solverMode& solverMode,
                                  check_level_t level) override;

    // virtual void setTime(coreTime time){prevTime=time;};
    virtual const std::vector<stringVec>& inputNames() const override;
    virtual const std::vector<stringVec>& outputNames() const override;

  protected:
    void checkForLimits();
};

}  // namespace griddyn

