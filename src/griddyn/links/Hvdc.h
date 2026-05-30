/*
 * Copyright (c) 2014-2026, Lawrence Livermore National Security
 * See the top-level NOTICE for additional details. All rights reserved.
 * SPDX-License-Identifier: BSD-3-Clause
 */

#pragma once

#include "Subsystem.h"
#include <string>

namespace griddyn::links {
/** @brief class defining a complete hvdc system including converters dc buses and dc line*/
class Hvdc: public Subsystem {
  public:
    /** hvdc helper flags*/
    enum HvdcFlags {
        reverseFlow = object_flag6,  //!< flag indicating that the flow is reverse standard
    };

  protected:
  public:
    /** @brief constructor*/
    Hvdc(const std::string& objName = "hvdc_$");
    virtual CoreObject* clone(CoreObject* obj = nullptr) const override;
    // parameter set functions
    virtual void set(std::string_view param, std::string_view val) override;
    virtual void
        set(std::string_view param, double val, units::unit unitType = units::defunit) override;

    virtual double get(std::string_view param,
                       units::unit unitType = units::defunit) const override;
    virtual void updateBus(GridBus* bus, index_t busnumber) override;

  protected:
    static constexpr int forward = 0;  //!< constant defining forward
    static constexpr int reverse = 1;  //!< constant defining reverse
    /** @brief set the flow direction
@param[in] direction  the direction of flow desired
*/
    void setFlow(int direction);
};
}  // namespace griddyn::links
