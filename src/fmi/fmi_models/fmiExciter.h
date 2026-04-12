/*
 * Copyright (c) 2014-2020, Lawrence Livermore National Security
 * See the top-level NOTICE for additional details. All rights reserved.
 * SPDX-License-Identifier: BSD-3-Clause
 */

#ifndef ___W_GRIDDYN_GRIDDYN_SRC_FMI_FMI_MODELS_FMIEXCITER_H_
#define ___W_GRIDDYN_GRIDDYN_SRC_FMI_FMI_MODELS_FMIEXCITER_H_

#include "fmiMEWrapper.hpp"
#include "griddyn/Exciter.h"
#include <string>

namespace griddyn {
namespace fmi {
    class fmiMESubModel;
    /** class defining an exciter wrapper for an FMU*/
    class fmiExciter: public fmiMEWrapper<Exciter> {
      public:
        fmiExciter(const std::string& objName = "fmiExciter_#");
        virtual coreObject* clone(coreObject* obj = nullptr) const override;

        virtual void set(const std::string& param, const std::string& val) override;
        virtual void set(const std::string& param,
                         double val,
                         units::unit unitType = units::defunit) override;
    };

}  // namespace fmi
}  // namespace griddyn
#endif  // ___W_GRIDDYN_GRIDDYN_SRC_FMI_FMI_MODELS_FMIEXCITER_H_
