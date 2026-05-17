/*
 * Copyright (c) 2014-2026, Lawrence Livermore National Security
 * See the top-level NOTICE for additional details. All rights reserved.
 * SPDX-License-Identifier: BSD-3-Clause
 */

#include "../Stabilizer.h"

#include "../Generator.h"
#include "../gridBus.h"
#include "core/coreObjectTemplates.hpp"
#include "core/objectFactoryTemplates.hpp"
#include <cmath>
#include <string>

namespace griddyn {
static const typeFactory<Stabilizer> gf("pss", std::to_array<std::string_view>({"basic"}));

Stabilizer::Stabilizer(const std::string& objName): GridSubModel(objName) {}
CoreObject* Stabilizer::clone(CoreObject* obj) const
{
    auto pss = cloneBase<Stabilizer, GridSubModel>(this, obj);
    if (pss == nullptr) {
        return obj;
    }

    return pss;
}

// destructor
Stabilizer::~Stabilizer() = default;
// initial conditions
void Stabilizer::dynObjectInitializeB(const IOdata& /*inputs*/,
                                      const IOdata& /*desiredOutput*/,
                                      IOdata& /*fieldSet*/)
{
}

// residual
void Stabilizer::residual(const IOdata& /*inputs*/,
                          const stateData& /*sD*/,
                          double /*resid*/[],
                          const solverMode& /*sMode*/)
{
}

index_t Stabilizer::findIndex(std::string_view /*field*/, const solverMode& /*sMode*/) const
{
    return kInvalidLocation;
}

void Stabilizer::set(std::string_view param, std::string_view val)
{
    return CoreObject::set(param, val);
}
// set parameters
void Stabilizer::set(std::string_view param, double val, units::unit unitType)
{
    {
        CoreObject::set(param, val, unitType);
    }
}

void Stabilizer::jacobianElements(const IOdata& /*inputs*/,
                                  const stateData& /*sD*/,
                                  matrixData<double>& /*md*/,
                                  const IOlocs& /*inputLocs*/,
                                  const solverMode& sMode)
{
    if (isAlgebraicOnly(sMode)) {
        return;
    }
}

void Stabilizer::derivative(const IOdata& /*inputs*/,
                            const stateData& /*sD*/,
                            double /*deriv*/[],
                            const solverMode& /*sMode*/)
{
}

}  // namespace griddyn

