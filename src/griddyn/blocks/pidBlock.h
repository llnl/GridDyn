/*
 * Copyright (c) 2014-2026, Lawrence Livermore National Security
 * See the top-level NOTICE for additional details. All rights reserved.
 * SPDX-License-Identifier: BSD-3-Clause
 */

#pragma once

#include "../Block.h"
#include <string>

namespace griddyn::blocks {
/** @brief class implementing a PID controller
the derivative operator has a prefilter operation on it with a time constant T1 and the output has a
delay of Td*/
class pidBlock: public Block {
  public:
  protected:
    double m_P = 1;  //!< proportional control constant
    double m_I = 0;  //!< integral control constant
    double m_D = 0;  //!< differential control constant
    double m_T1 = 0.01;  //!< filtering delay on the input for the differential calculation
    double m_Td = 0.01;  //!< filtering delay on the output
    double iv = 0;  //!< intermediate value for calculations
    bool& no_D;  //!< ignore the derivative part of the calculations
  public:
    /** @brief default constructor*/
    explicit pidBlock(const std::string& objName = "pidBlock_#");
    /** @brief alternate constructor
@param[in] P the proportional gain
@param[in] I the integral gain
@param[in] D the derivative gain
*/
    pidBlock(double P, double I, double D, const std::string& objName = "pidBlock_#");
    virtual CoreObject* clone(CoreObject* obj = nullptr) const override;
    virtual void dynObjectInitializeA(coreTime time0, std::uint32_t flags) override;
    virtual void dynObjectInitializeB(const IOdata& inputs,
                                      const IOdata& desiredOutput,
                                      IOdata& fieldSet) override;

    virtual void set(std::string_view param, std::string_view val) override;
    virtual void
        set(std::string_view param, double val, units::unit unitType = units::defunit) override;
    virtual index_t findIndex(std::string_view field, const solverMode& sMode) const override;

    virtual void blockDerivative(double input,
                                 double didt,
                                 const stateData& sD,
                                 double deriv[],
                                 const solverMode& sMode) override;
    // only called if the genModel is not present
    virtual void blockJacobianElements(double input,
                                       double didt,
                                       const stateData& sD,
                                       matrixData<double>& md,
                                       index_t argLoc,
                                       const solverMode& sMode) override;
    virtual double step(coreTime time, double inputA) override;
    virtual stringVec localStateNames() const override;
};

}  // namespace griddyn::blocks

