/*
 * Copyright (c) 2014-2026, Lawrence Livermore National Security
 * See the top-level NOTICE for additional details. All rights reserved.
 * SPDX-License-Identifier: BSD-3-Clause
 */

#ifndef ___W_GRIDDYN_GRIDDYN_SRC_GRIDDYN_LINKS_HVDC_H_
#define ___W_GRIDDYN_GRIDDYN_SRC_GRIDDYN_LINKS_HVDC_H_

#include "subsystem.h"
#include <string>

namespace griddyn {
namespace links {
    /** @brief class defining a complete hvdc system including converters dc buses and dc line*/
    class hvdc: public subsystem {
      public:
        /** hvdc helper flags*/
        enum hvdc_flags {
            reverse_flow = object_flag6,  //!< flag indicating that the flow is reverse standard
        };

      protected:
      public:
        /** @brief constructor*/
        hvdc(const std::string& objName = "hvdc_$");
        virtual coreObject* clone(coreObject* obj = nullptr) const override;
        // parameter set functions
        virtual void set(const std::string& param, const std::string& val) override;
        virtual void set(const std::string& param,
                         double val,
                         units::unit unitType = units::defunit) override;

        virtual double get(const std::string& param,
                           units::unit unitType = units::defunit) const override;
        virtual void updateBus(gridBus* bus, index_t busnumber) override;

      protected:
        static const int forward = 0;  //!< constant defining forward
        static const int reverse = 1;  //!< constant defining reverse
        /** @brief set the flow direction
  @param[in] direction  the direction of flow desired
  */
        void setFlow(int direction);
    };
}  // namespace links
}  // namespace griddyn
#endif  // ___W_GRIDDYN_GRIDDYN_SRC_GRIDDYN_LINKS_HVDC_H_
