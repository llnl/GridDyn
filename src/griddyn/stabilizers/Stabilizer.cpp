/*
 * Copyright (c) 2014-2026, Lawrence Livermore National Security
 * See the top-level NOTICE for additional details. All rights reserved.
 * SPDX-License-Identifier: BSD-3-Clause
 */

#include "../Stabilizer.h"

#include "../Generator.h"
#include "../GridBus.h"
#include "StabilizerST2CUT.h"
#include "core/CoreObjectTemplates.hpp"
#include "core/ObjectFactoryTemplates.hpp"
#include <cmath>
#include <string>

namespace griddyn {
static const TypeFactory<Stabilizer> gStabilizerFactory("pss",
                                                        std::to_array<std::string_view>({"basic"}));
static ChildTypeFactory<stabilizers::StabilizerST2CUT, Stabilizer>
    gSt2cutFactory("pss", std::to_array<std::string_view>({"st2cut"}));

Stabilizer::Stabilizer(const std::string& objName):
    GridSubModel(objName), mp_Tw(0.0), mp_Teps(0.0), mp_Kw(0.0), mp_Kp(0.0), mp_Kv(0.0),
    mp_Smax(0.0), mp_Smin(0.0)
{
    m_inputSize = pssInputCount;
}
CoreObject* Stabilizer::clone(CoreObject* obj) const
{
    auto* pss = cloneBase<Stabilizer, GridSubModel>(this, obj);
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
                          const StateData& /*sD*/,
                          double /*resid*/[],
                          const SolverMode& /*sMode*/)
{
}

index_t Stabilizer::findIndex(std::string_view /*field*/, const SolverMode& /*sMode*/) const
{
    return kInvalidLocation;
}

void Stabilizer::set(std::string_view param, std::string_view val)
{
    CoreObject::set(param, val);
}
// set parameters
void Stabilizer::set(std::string_view param, double val, units::unit unitType)
{
    {
        CoreObject::set(param, val, unitType);
    }
}

void Stabilizer::jacobianElements(const IOdata& /*inputs*/,
                                  const StateData& /*sD*/,
                                  MatrixData<double>& /*md*/,
                                  const IOlocs& /*inputLocs*/,
                                  const SolverMode& sMode)
{
    if (isAlgebraicOnly(sMode)) {
        return;
    }
}

void Stabilizer::derivative(const IOdata& /*inputs*/,
                            const StateData& /*sD*/,
                            double /*deriv*/[],
                            const SolverMode& /*sMode*/)
{
}

const std::vector<stringVec>& Stabilizer::inputNames() const
{
    static const std::vector<stringVec> inputNamesStr{{"omega", "frequency", "w"},
                                                      {"voltage", "v", "volt"},
                                                      {"pmech", "mechanicalpower"},
                                                      {"pe", "electricalpower", "te"}};
    return inputNamesStr;
}

const std::vector<stringVec>& Stabilizer::outputNames() const
{
    static const std::vector<stringVec> outputNamesStr{{"vss", "stabilizersignal"}};
    return outputNamesStr;
}

}  // namespace griddyn
