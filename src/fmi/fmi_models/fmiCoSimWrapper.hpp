/*
 * Copyright (c) 2014-2026, Lawrence Livermore National Security
 * See the top-level NOTICE for additional details. All rights reserved.
 * SPDX-License-Identifier: BSD-3-Clause
 */

#pragma once

#include "../fmi_models/fmiCoSimSubModel.h"
#include "fmiWrapper.hpp"
#include <string>
#include <vector>

namespace griddyn::fmi {
template<class BaseObj>
class FmiCoSimWrapper: public FmiWrapper<FmiCoSimSubModel, BaseObj> {
  public:
    FmiCoSimWrapper(const std::string& objName): FmiWrapper<FmiCoSimSubModel, BaseObj>(objName) {}
    CoreObject* clone(CoreObject* obj) const override
    {
        auto nobj = cloneBase<FmiCoSimWrapper, FmiWrapper<FmiCoSimSubModel, BaseObj>>(this, obj);
        if (nobj == nullptr) {
            return obj;
        }
        return nobj;
    }

    void pFlowObjectInitializeA(coreTime time0, std::uint32_t flags) override
    {
        if (FmiWrapper<FmiCoSimSubModel, BaseObj>::fmisub->isLoaded()) {
            FmiWrapper<FmiCoSimSubModel, BaseObj>::configureFmiIo();
            SET_CONTROLFLAG(flags, force_constant_pflow_initialization);

            BaseObj::pFlowObjectInitializeA(time0, flags);

        } else {
            this->disable();
        }
    }

    void residual(const IOdata& inputs,
                  const stateData& sD,
                  double resid[],
                  const solverMode& sMode) override
    {
        FmiWrapper<FmiCoSimSubModel, BaseObj>::fmisub->residual(inputs, sD, resid, sMode);
    }

    void derivative(const IOdata& inputs,
                    const stateData& sD,
                    double deriv[],
                    const solverMode& sMode) override
    {
        FmiWrapper<FmiCoSimSubModel, BaseObj>::fmisub->derivative(inputs, sD, deriv, sMode);
    }

    void outputPartialDerivatives(const IOdata& inputs,
                                  const stateData& sD,
                                  matrixData<double>& md,
                                  const solverMode& sMode) override
    {
        FmiWrapper<FmiCoSimSubModel, BaseObj>::fmisub->outputPartialDerivatives(inputs,
                                                                                sD,
                                                                                md,
                                                                                sMode);
    }
    void ioPartialDerivatives(const IOdata& inputs,
                              const stateData& sD,
                              matrixData<double>& md,
                              const IOlocs& inputLocs,
                              const solverMode& sMode) override
    {
        FmiWrapper<FmiCoSimSubModel, BaseObj>::fmisub->ioPartialDerivatives(
            inputs, sD, md, inputLocs, sMode);
    }
    void jacobianElements(const IOdata& inputs,
                          const stateData& sD,
                          matrixData<double>& md,
                          const IOlocs& inputLocs,
                          const solverMode& sMode) override
    {
        FmiWrapper<FmiCoSimSubModel, BaseObj>::fmisub->jacobianElements(
            inputs, sD, md, inputLocs, sMode);
    }

    void rootTest(const IOdata& inputs,
                  const stateData& sD,
                  double roots[],
                  const solverMode& sMode) override
    {
        FmiWrapper<FmiCoSimSubModel, BaseObj>::fmisub->rootTest(inputs, sD, roots, sMode);
    }
    void rootTrigger(coreTime time,
                     const IOdata& inputs,
                     const std::vector<int>& rootMask,
                     const solverMode& sMode) override
    {
        FmiWrapper<FmiCoSimSubModel, BaseObj>::fmisub->rootTrigger(time, inputs, rootMask, sMode);
    }

    void setState(coreTime time,
                  const double state[],
                  const double dstate_dt[],
                  const solverMode& sMode) override
    {
        FmiWrapper<FmiCoSimSubModel, BaseObj>::fmisub->setState(time, state, dstate_dt, sMode);
    }

    index_t findIndex(std::string_view field, const solverMode& sMode) const override
    {
        return FmiWrapper<FmiCoSimSubModel, BaseObj>::fmisub->findIndex(field, sMode);
    }

    void timestep(coreTime time, const IOdata& inputs, const solverMode& sMode) override
    {
        BaseObj::prevTime = time;
        FmiWrapper<FmiCoSimSubModel, BaseObj>::fmisub->timestep(time, inputs, sMode);
    }
};
}  // namespace griddyn::fmi
