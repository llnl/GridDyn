/*
 * Copyright (c) 2014-2026, Lawrence Livermore National Security
 * See the top-level NOTICE for additional details. All rights reserved.
 * SPDX-License-Identifier: BSD-3-Clause
 */

#include "FunctionSource.h"

#include "core/CoreObjectTemplates.hpp"
#include <string>
#include <utility>

namespace griddyn::sources {
FunctionSource::FunctionSource(const std::string& objName): Source(objName) {}
CoreObject* FunctionSource::clone(CoreObject* obj) const
{
    auto gS = cloneBase<FunctionSource, GridSubModel>(this, obj);
    if (gS == nullptr) {
        return obj;
    }
    gS->sourceFunc = sourceFunc;
    return gS;
}

IOdata FunctionSource::getOutputs(const IOdata& /*inputs*/,
                                  const StateData& sD,
                                  const SolverMode& /*sMode*/) const
{
    return {sourceFunc(sD.time)};
}
double FunctionSource::getOutput(const IOdata& /*inputs*/,
                                 const StateData& sD,
                                 const SolverMode& /*sMode*/,
                                 index_t outputNum) const
{
    return (outputNum == 0) ? sourceFunc(sD.time) : kNullVal;
}

double FunctionSource::getOutput(index_t outputNum) const
{
    return (outputNum == 0) ? sourceFunc(prevTime) : kNullVal;
}
double FunctionSource::getDoutdt(const IOdata& /*inputs*/,
                                 const StateData& sD,
                                 const SolverMode& /*sMode*/,
                                 index_t outputNum) const
{
    return (outputNum == 0) ? ((sourceFunc(sD.time + 1e-7) - sourceFunc(sD.time)) / 1e-7) : 0.0;
}

void FunctionSource::setFunction(std::function<double(double)> calcFunc)
{
    sourceFunc = std::move(calcFunc);
}
}  // namespace griddyn::sources
