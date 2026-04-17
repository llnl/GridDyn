/*
 * Copyright (c) 2014-2026, Lawrence Livermore National Security
 * See the top-level NOTICE for additional details. All rights reserved.
 * SPDX-License-Identifier: BSD-3-Clause
 */

#pragma once

#include "acLine.h"
#include "subsystem.h"
#include <string>

namespace griddyn::links {
    /** @brief class defining a thee winding transformer model
     */
    class ThreeWindingTransformer: public subsystem {
      private:
        int faultLink = -1;  //!< link number of the fault if one is present
        double r = 0.0;
        double x = 0.0;
        double mp_B = 0.0;
        double mp_G = 0.0;
        double segmentationLength = 0.0;
        double length = 0.0;
        double fault = -1.0;

      public:
        /** @brief default constructor*/
        ThreeWindingTransformer(const std::string& objName = "ThreeWinding_$");
        virtual coreObject* clone(coreObject* obj = nullptr) const override;
        // add components
        virtual void add(coreObject* obj) override final;  // there shouldn't be any additional adds
        // remove components
        virtual void remove(coreObject* obj)
            override final;  // there shouldn't be any removes all models are controlled internally

        virtual void set(const std::string& param, const std::string& val) override;
        virtual void set(const std::string& param,
                         double val,
                         units::unit unitType = units::defunit) override;

        virtual double get(const std::string& param,
                           units::unit unitType = units::defunit) const override;
    };

}  // namespace griddyn::links
