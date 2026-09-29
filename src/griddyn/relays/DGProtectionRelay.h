/*
 * Copyright (c) 2014-2026, Lawrence Livermore National Security
 * See the top-level NOTICE for additional details. All rights reserved.
 * SPDX-License-Identifier: BSD-3-Clause
 */

#pragma once
#include "../Relay.h"
#include <array>
#include <string>
#include <string_view>

namespace griddyn {
class DistributedConverter;
class GridBus;

/** Timed voltage/frequency protection for a distributed converter. */
class DGProtectionRelay: public Relay {
  public:
    DGProtectionRelay(bool externalVoltage, const std::string& name);
    void set(std::string_view param, double val, units::unit unitType = units::defunit) override;
    double get(std::string_view param, units::unit unitType = units::defunit) const override;
    void pFlowObjectInitializeA(CoreTime time0, std::uint32_t flags) override;
    void updateA(CoreTime time) override;

  protected:
    void copyParametersTo(DGProtectionRelay* target) const;
    void actionTaken(index_t actionNum,
                     index_t conditionNum,
                     ChangeCode actionReturn,
                     CoreTime actionTime) override;
    void conditionCleared(index_t conditionNum, CoreTime timeCleared) override;

  private:
    bool externalVoltage = false;
    bool externalConfigured = false;
    bool lock = false;
    bool frequencyEnabled = true;
    bool voltageEnabled = false;
    double fn = 60.0;
    double externalV = 1.0;
    double resetTime = 0.05;
    CoreTime clearDeadline = maxTime;
    std::array<double, 6> frequency{{50.0, 57.5, 59.2, 60.5, 61.5, 70.0}};
    std::array<double, 7> voltage{{0.1, 0.45, 0.6, 0.88, 1.1, 1.2, 2.0}};
    std::array<double, 4> frequencyTime{{300.0, 10.0, 300.0, 10.0}};
    std::array<double, 5> voltageTime{{2.0, 1.0, 0.16, 1.0, 0.16}};
    DistributedConverter* target = nullptr;
    GridBus* measuredBus = nullptr;
    count_t configuredConditions = 0;
};

class DGPRCT1 final: public DGProtectionRelay {
  public:
    explicit DGPRCT1(const std::string& name = "DGPRCT1_#");
    CoreObject* clone(CoreObject* obj = nullptr) const override;
};
class DGPRCTExt final: public DGProtectionRelay {
  public:
    explicit DGPRCTExt(const std::string& name = "DGPRCTExt_#");
    CoreObject* clone(CoreObject* obj = nullptr) const override;
};
}  // namespace griddyn
