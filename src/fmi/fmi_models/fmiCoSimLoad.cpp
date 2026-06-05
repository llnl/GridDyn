/*
 * Copyright (c) 2014-2026, Lawrence Livermore National Security
 * See the top-level NOTICE for additional details. All rights reserved.
 * SPDX-License-Identifier: BSD-3-Clause
 */

#include "fmiCoSimLoad.h"

#include "../fmi_import/fmiObjects.h"
#include "core/CoreExceptions.h"
#include "core/CoreObjectTemplates.hpp"
#include "fmiMESubModel.h"
#include "gmlc/utilities/stringOps.h"
#include "griddyn/GridBus.h"
#include <string>

namespace griddyn::fmi {
FmiCoSimLoad::FmiCoSimLoad(const std::string& objName): FmiCoSimWrapper<GridLoad>(objName) {}

CoreObject* FmiCoSimLoad::clone(CoreObject* obj) const
{
    auto* nobj = cloneBase<FmiCoSimLoad, FmiCoSimWrapper<GridLoad>>(this, obj);
    if (nobj == nullptr) {
        return obj;
    }

    return nobj;
}

void FmiCoSimLoad::pFlowObjectInitializeA(CoreTime time0, std::uint32_t flags)
{
    if (fmisub->isLoaded()) {
        configureFmiIo();
        SET_CONTROLFLAG(flags, FORCE_CONSTANT_PFLOW_INITIALIZATION);
        fmisub->dynInitializeA(time0, flags);
        // ZipLoad::pFlowObjectInitializeA(time0, flags);
        auto inputs = bus->getOutputs(noInputs, emptyStateData, cLocalSolverMode);
        IOdata outset;
        fmisub->dynInitializeB(inputs, outset, outset);
        opFlags.set(POWERFLOW_INITIALIZED);
    } else {
        disable();
    }
}
void FmiCoSimLoad::dynObjectInitializeA(CoreTime time0, std::uint32_t flags)
{
    fmisub->dynInitializeA(time0, flags);
    // ZipLoad::dynObjectInitializeA(time0, flags);
}

void FmiCoSimLoad::dynObjectInitializeB(const IOdata& inputs,
                                        const IOdata& desiredOutput,
                                        IOdata& fieldSet)
{
    fmisub->dynInitializeB(inputs, desiredOutput, fieldSet);
}

void FmiCoSimLoad::setState(CoreTime time,
                            const double state[],
                            const double dstateDt[],
                            const SolverMode& sMode)
{
    fmisub->setState(time, state, dstateDt, sMode);
    auto out = fmisub->getOutputs(noInputs, emptyStateData, cLocalSolverMode);
    setP(out[POUT_LOCATION]);
    setQ(out[QOUT_LOCATION]);
}

}  // namespace griddyn::fmi
