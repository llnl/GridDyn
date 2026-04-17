/*
 * Copyright (c) 2014-2026, Lawrence Livermore National Security
 * See the top-level NOTICE for additional details. All rights reserved.
 * SPDX-License-Identifier: BSD-3-Clause
 */

#ifndef ___W_GRIDDYN_GRIDDYN_SRC_GRIDDYN_STABILIZER_H_
#define ___W_GRIDDYN_GRIDDYN_SRC_GRIDDYN_STABILIZER_H_

#include "gridSubModel.h"
#include <string>

namespace griddyn {
class Stabilizer: public gridSubModel {
  public:
  protected:
    double mp_Tw;
    double mp_Teps;
    double mp_Kw;
    double mp_Kp;
    double mp_Kv;
    double mp_Smax;
    double mp_Smin;

  public:
    explicit Stabilizer(const std::string& objName = "pss_#");
    virtual coreObject* clone(coreObject* obj = nullptr) const override;
    virtual ~Stabilizer();
    virtual void dynObjectInitializeB(const IOdata& inputs,
                                      const IOdata& desiredOutput,
                                      IOdata& fieldSet) override;

    virtual void set(const std::string& param, const std::string& val) override;
    virtual void
        set(const std::string& param, double val, units::unit unitType = units::defunit) override;

    virtual void residual(const IOdata& inputs,
                          const stateData& sD,
                          double resid[],
                          const solverMode& sMode) override;
    virtual void jacobianElements(const IOdata& inputs,
                                  const stateData& sD,
                                  matrixData<double>& md,
                                  const IOlocs& inputLocs,
                                  const solverMode& sMode) override;

    virtual void derivative(const IOdata& inputs,
                            const stateData& sD,
                            double deriv[],
                            const solverMode& sMode) override;

    virtual index_t findIndex(const std::string& field, const solverMode& sMode) const override;
};

}  // namespace griddyn

#endif  // ___W_GRIDDYN_GRIDDYN_SRC_GRIDDYN_STABILIZER_H_
