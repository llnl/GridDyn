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
        virtual coreObject* clone(coreObject* obj = nullptr) const override;

        virtual void residual(const IOdata& inputs,
                              const stateData& sD,
                              double resid[],
                              const solverMode& sMode) override;
        virtual void derivative(const IOdata& inputs,
                                const stateData& sD,
                                double deriv[],
                                const solverMode& sMode) override;
        virtual void rootTest(const IOdata& inputs,
                              const stateData& sD,
                              double roots[],
                              const solverMode& sMode) override;
        virtual change_code rootCheck(const IOdata& inputs,
                                      const stateData& sD,
                                      const solverMode& sMode,
                                      check_level_t level) override;

      protected:
        virtual void limitJacobian(double V,
                                   int VLoc,
                                   int refLoc,
                                   double cj,
                                   matrixData<double>& md) override;
    };

}  // namespace griddyn::exciters
