/*
 * Copyright (c) 2014-2026, Lawrence Livermore National Security
 * See the top-level NOTICE for additional details. All rights reserved.
 * SPDX-License-Identifier: BSD-3-Clause
 */
#pragma once

#include "REGCA1.h"
#include <array>
#include <span>
#include <string>
#include <string_view>

namespace griddyn {

/** REGCP1 converter with an optional named PLL angle reference. */
class REGCP1 final: public REGCA1 {
  public:
    explicit REGCP1(const std::string& name = "REGCP1_#");
    CoreObject* clone(CoreObject* obj = nullptr) const override;
    std::span<const RenewablePort> inputPorts() const override;
    std::string_view sourceName(RenewableSignal signal) const override;
    void dynObjectInitializeA(CoreTime time0, std::uint32_t flags) override;
    using REGCA1::set;
    void set(std::string_view param, double val, units::unit unitType = units::defunit) override;
    void set(std::string_view param, std::string_view val) override;

  protected:
    std::array<double, 2> powerInjection(const IOdata& inputs,
                                         double activeCurrent,
                                         double reactiveCurrent) const override;
    std::array<double, 2> initialCurrentFramePower(const IOdata& inputs,
                                                   const IOdata& desiredOutput) const override;
    void powerJacobian(const IOdata& inputs,
                       const double state[],
                       MatrixData<double>& matrixData,
                       const IOlocs& inputLocs,
                       const SolverMode& sMode,
                       index_t alg,
                       index_t diff) const override;

  private:
    std::string pllName;
    double angleDifference(const IOdata& inputs) const;
};

}  // namespace griddyn
