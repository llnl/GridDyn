/*
 * Copyright (c) 2014-2022, Lawrence Livermore National Security
 * See the top-level NOTICE for additional details. All rights reserved.
 * SPDX-License-Identifier: BSD-3-Clause
 */

#pragma once

#include "acLine.h"
#include "subsystem.h"

namespace griddyn {
namespace links {
    /** @brief class defining a thee winding transformer model
*/
    class ThreeWindingTransformer: public subsystem {
      private:
        int faultLink = -1;  //!< link number of the fault if one is present
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

}  // namespace links
}  // namespace griddyn
