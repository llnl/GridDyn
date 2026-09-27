/*
 * Copyright (c) 2014-2026, Lawrence Livermore National Security
 * See the top-level NOTICE for additional details. All rights reserved.
 * SPDX-License-Identifier: BSD-3-Clause
 */
#pragma once

#include "REECA1.h"
#include <string>
#include <string_view>

namespace griddyn {

/** REECA1 with bus frequency and ROCOF active-power support. */
class REECA1E final: public REECA1 {
  public:
    explicit REECA1E(const std::string& name = "REECA1E_#");
    CoreObject* clone(CoreObject* obj = nullptr) const override;
    std::span<const RenewablePort> inputPorts() const override;
    std::string_view sourceName(RenewableSignal signal) const override;
    void set(std::string_view param, double val, units::unit unitType = units::defunit) override;
    void set(std::string_view param, std::string_view val) override;
    double get(std::string_view param, units::unit unitType = units::defunit) const override;
    void dynObjectInitializeA(CoreTime time0, std::uint32_t flags) override;

  protected:
    double activeReferenceAdjustment(const IOdata& inputs) const override;
    void activeReferenceJacobian(const IOlocs& inputLocs,
                                 MatrixData<double>& matrixData,
                                 index_t row,
                                 double gain) const override;

  private:
    double Kf = 0.0;
    double Kdf = 0.0;
    std::string measurementName;
};

}  // namespace griddyn
