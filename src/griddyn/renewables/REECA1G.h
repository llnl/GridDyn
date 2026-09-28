/*
 * Copyright (c) 2014-2026, Lawrence Livermore National Security
 * See the top-level NOTICE for additional details. All rights reserved.
 * SPDX-License-Identifier: BSD-3-Clause
 */
#pragma once

#include "REECA1.h"
#include <span>
#include <string>
#include <string_view>

namespace griddyn {

/** REECA1 with active-reference feedback from a named synchronous generator. */
class REECA1G final: public REECA1 {
  public:
    explicit REECA1G(const std::string& name = "REECA1G_#");
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
    std::string generatorName;
};

}  // namespace griddyn
