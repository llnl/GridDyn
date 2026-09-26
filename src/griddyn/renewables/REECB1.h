/*
 * Copyright (c) 2014-2026, Lawrence Livermore National Security
 * See the top-level NOTICE for additional details. All rights reserved.
 * SPDX-License-Identifier: BSD-3-Clause
 */
#pragma once
#include "REECA1.h"

namespace griddyn {

/** REEC_B controller with flat current limits and no speed-dependent P flag. */
class REECB1 final: public REECA1 {
  public:
    explicit REECB1(const std::string& name="REECB1_#");
    CoreObject* clone(CoreObject* obj=nullptr) const override;
    void set(std::string_view param,double val,units::unit unitType=units::defunit) override;
  protected:
    // REEC_B injection follows filtered voltage through fault recovery.
    bool useVoltageInjection(double) const override { return true; }
};

} // namespace griddyn
