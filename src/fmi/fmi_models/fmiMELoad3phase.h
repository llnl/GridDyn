/*
 * Copyright (c) 2014-2026, Lawrence Livermore National Security
 * See the top-level NOTICE for additional details. All rights reserved.
 * SPDX-License-Identifier: BSD-3-Clause
 */

#ifndef ___W_GRIDDYN_GRIDDYN_SRC_FMI_FMI_MODELS_FMIMELOAD3PHASE_H_
#define ___W_GRIDDYN_GRIDDYN_SRC_FMI_FMI_MODELS_FMIMELOAD3PHASE_H_

#include "fmiMEWrapper.hpp"
#include "griddyn/loads/ThreePhaseLoad.h"
#include <string>
#include <vector>

namespace griddyn {
namespace fmi {
    class fmiMESubModel;

    class fmiMELoad3phase: public fmiMEWrapper<loads::ThreePhaseLoad> {
      public:
        enum threephasefmi_load_flags {
            ignore_voltage_angle = object_flag8,
            complex_voltage = object_flag9,
            current_output = object_flag10,
            complex_current_output = object_flag11,
        };

      public:
        fmiMELoad3phase(const std::string& objName = "fmi3phase_$");
        virtual coreObject* clone(coreObject* obj = nullptr) const override;

        virtual void setFlag(const std::string& flag, bool val = true) override;
        virtual void set(const std::string& param, const std::string& val) override;
        virtual void set(const std::string& param,
                         double val,
                         units::unit unitType = units::defunit) override;

        virtual void setState(coreTime time,
                              const double state[],
                              const double dstate_dt[],
                              const solverMode& sMode) override;

        virtual void updateLocalCache(const IOdata& inputs,
                                      const stateData& sD,
                                      const solverMode& sMode) override;
        virtual const std::vector<stringVec>& fmiInputNames() const override;

        virtual const std::vector<stringVec>& fmiOutputNames() const override;
    };

}  // namespace fmi
}  // namespace griddyn
#endif  // ___W_GRIDDYN_GRIDDYN_SRC_FMI_FMI_MODELS_FMIMELOAD3PHASE_H_
