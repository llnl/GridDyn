/*
 * Copyright (c) 2014-2026, Lawrence Livermore National Security
 * See the top-level NOTICE for additional details. All rights reserved.
 * SPDX-License-Identifier: BSD-3-Clause
 */
#pragma once

#include "REGCA1.h"
#include <string>

namespace griddyn {

/** REGCP1 with terminal-voltage aligned current injection (no PLL). */
class REGCP1 final: public REGCA1 {
  public:
    explicit REGCP1(const std::string& name = "REGCP1_#");
    CoreObject* clone(CoreObject* obj = nullptr) const override;
    using REGCA1::set;
    void set(std::string_view param, double val, units::unit unitType = units::defunit) override;
    void set(std::string_view param, std::string_view val) override;
};

}  // namespace griddyn
