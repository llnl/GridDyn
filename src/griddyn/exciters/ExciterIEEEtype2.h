/*
 * Copyright (c) 2014-2026, Lawrence Livermore National Security
 * See the top-level NOTICE for additional details. All rights reserved.
 * SPDX-License-Identifier: BSD-3-Clause
 */

#ifndef ___W_GRIDDYN_GRIDDYN_SRC_GRIDDYN_EXCITERS_EXCITERIEEETYPE2_H_
#define ___W_GRIDDYN_GRIDDYN_SRC_GRIDDYN_EXCITERS_EXCITERIEEETYPE2_H_

#include "ExciterIEEEtype1.h"
#include <string>
namespace griddyn {

namespace exciters {

    /** @brief IEEE Type 2 exciter
     */
    class ExciterIEEEtype2: public ExciterIEEEtype1 {
      protected:
        double Tf2 = 1.0;  // [s]    stabilizer time constant
      public:
        explicit ExciterIEEEtype2(const std::string& objName = "exciterIEEEtype2_#");
        virtual coreObject* clone(coreObject* obj = nullptr) const override;
        virtual void dynObjectInitializeA(coreTime time0, std::uint32_t flags) override;
        virtual void dynObjectInitializeB(const IOdata& inputs,
                                          const IOdata& desiredOutput,
                                          IOdata& fieldSet) override;

        virtual void set(const std::string& param, const std::string& val) override;
        virtual void set(const std::string& param,
                         double val,
                         units::unit unitType = units::defunit) override;

        virtual stringVec localStateNames() const override;

        virtual void residual(const IOdata& inputs,
                              const stateData& sD,
                              double resid[],
                              const solverMode& sMode) override;
        virtual void derivative(const IOdata& inputs,
                                const stateData& sD,
                                double deriv[],
                                const solverMode& sMode) override;
        virtual void jacobianElements(const IOdata& inputs,
                                      const stateData& sD,
                                      matrixData<double>& md,
                                      const IOlocs& inputLocs,
                                      const solverMode& sMode) override;

        virtual void rootTest(const IOdata& inputs,
                              const stateData& sD,
                              double roots[],
                              const solverMode& sMode) override;
        virtual change_code rootCheck(const IOdata& inputs,
                                      const stateData& sD,
                                      const solverMode& sMode,
                                      check_level_t level) override;
        // virtual void setTime(coreTime time){prevTime=time;};
    };

}  // namespace exciters
}  // namespace griddyn

#endif  // ___W_GRIDDYN_GRIDDYN_SRC_GRIDDYN_EXCITERS_EXCITERIEEETYPE2_H_
