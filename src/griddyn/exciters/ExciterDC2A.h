/*
 * Copyright (c) 2014-2026, Lawrence Livermore National Security
 * See the top-level NOTICE for additional details. All rights reserved.
 * SPDX-License-Identifier: BSD-3-Clause
 */

#pragma once

#include "ExciterDC1A.h"
#include <string>
namespace griddyn::exciters {
/** @brief DC2A exciter
 */
class ExciterDC2A: public ExciterDC1A {
  protected:
  public:
    explicit ExciterDC2A(const std::string& objName = "exciterDC2A_#");
    virtual CoreObject* clone(CoreObject* obj = nullptr) const override;

    virtual void residual(const IOdata& inputs,
                          const stateData& stateDataValue,
                          double resid[],
                          const SolverMode& sMode) override;
    virtual void derivative(const IOdata& inputs,
                            const stateData& stateDataValue,
                            double deriv[],
                            const SolverMode& sMode) override;
    virtual void rootTest(const IOdata& inputs,
                          const stateData& stateDataValue,
                          double roots[],
                          const SolverMode& sMode) override;
    virtual ChangeCode rootCheck(const IOdata& inputs,
                                 const stateData& sD,
                                 const SolverMode& sMode,
                                 CheckLevel level) override;

  protected:
    virtual void limitJacobian(double V,
                               int voltageLoc,
                               int refLoc,
                               double cj,
                               matrixData<double>& matrixDataValue) override;
};

}  // namespace griddyn::exciters
