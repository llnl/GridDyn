/*
 * Copyright (c) 2014-2026, Lawrence Livermore National Security
 * See the top-level NOTICE for additional details. All rights reserved.
 * SPDX-License-Identifier: BSD-3-Clause
 */

#include "fmiCoSimLoad.h"

#include "../fmi_import/fmiObjects.h"
#include "core/coreExceptions.h"
#include "core/coreObjectTemplates.hpp"
#include "fmiMESubModel.h"
#include "gmlc/utilities/stringOps.h"
#include "griddyn/gridBus.h"
#include <string>

namespace griddyn::fmi {
FmiCoSimLoad::FmiCoSimLoad(const std::string& objName): FmiCoSimWrapper<Load>(objName) {}

coreObject* FmiCoSimLoad::clone(coreObject* obj) const
{
    auto* nobj = cloneBase<FmiCoSimLoad, FmiCoSimWrapper<Load>>(this, obj);
    if (nobj == nullptr) {
        return obj;
    }

    return nobj;
}

void FmiCoSimLoad::pFlowObjectInitializeA(coreTime time0, std::uint32_t flags)
{
    if (fmisub->isLoaded()) {
        setupFmiIo();
        SET_CONTROLFLAG(flags, force_constant_pflow_initialization);
        fmisub->dynInitializeA(time0, flags);
        // zipLoad::pFlowObjectInitializeA(time0, flags);
        auto inputs = bus->getOutputs(noInputs, emptyStateData, cLocalSolverMode);
        IOdata outset;
        fmisub->dynInitializeB(inputs, outset, outset);
        opFlags.set(pFlow_initialized);
    } else {
        disable();
    }
}
void FmiCoSimLoad::dynObjectInitializeA(coreTime time0, std::uint32_t flags)
{
    fmisub->dynInitializeA(time0, flags);
    // zipLoad::dynObjectInitializeA(time0, flags);
}

void FmiCoSimLoad::dynObjectInitializeB(const IOdata& inputs,
                                        const IOdata& desiredOutput,
                                        IOdata& fieldSet)
{
    fmisub->dynInitializeB(inputs, desiredOutput, fieldSet);
}

void FmiCoSimLoad::setState(coreTime time,
                            const double state[],
                            const double dstate_dt[],
                            const solverMode& sMode)
{
    fmisub->setState(time, state, dstate_dt, sMode);
    auto out = fmisub->getOutputs(noInputs, emptyStateData, cLocalSolverMode);
    setP(out[PoutLocation]);
    setQ(out[QoutLocation]);
}

}  // namespace griddyn::fmi
