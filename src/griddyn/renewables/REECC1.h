/*
 * Copyright (c) 2014-2026, Lawrence Livermore National Security
 * See the top-level NOTICE for additional details. All rights reserved.
 * SPDX-License-Identifier: BSD-3-Clause
 */
#pragma once
#include "REECA1.h"

namespace griddyn {

/** REEC_C battery electrical control with state-of-charge limits. */
class REECC1 final: public REECA1 {
  public:
    explicit REECC1(const std::string& name = "REECC1_#");
    CoreObject* clone(CoreObject* obj = nullptr) const override;
    void set(std::string_view param, double val, units::unit unitType = units::defunit) override;
    double get(std::string_view param, units::unit unitType = units::defunit) const override;
    void dynObjectInitializeA(CoreTime time0, std::uint32_t flags) override;

  protected:
    bool hasStorageSoc() const override { return true; }
    bool supportsPowerFactorControl() const override { return true; }
    double initialStorageSoc() const override { return SOCini; }
    double storageSocTimeConstant() const override { return T; }
    double storageSocRate(const IOdata& inputs) const override;
    std::pair<double, double> activeCurrentBounds(const IOdata& inputs,
                                                  const double state[],
                                                  double ipCap) const override;

  private:
    double T = 0.0;
    double SOCini = 0.5;
    double SOCmax = 1.0;
    double SOCmin = 0.0;
    bool minimumPowerConfigured = false;
};

}  // namespace griddyn
