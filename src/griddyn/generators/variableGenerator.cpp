/*
 * Copyright (c) 2014-2026, Lawrence Livermore National Security
 * See the top-level NOTICE for additional details. All rights reserved.
 * SPDX-License-Identifier: BSD-3-Clause
 */

#include "variableGenerator.h"

#include "../Block.h"
#include "../Source.h"
#include "../gridBus.h"
#include "core/coreObjectTemplates.hpp"
#include <string>

namespace griddyn {
using units::convert;
using units::puV;
using units::unit;

variableGenerator::variableGenerator(const std::string& objName): DynamicGenerator(objName)
{
    opFlags[variable_generation] = true;
    opFlags.reset(adjustable_P);
    opFlags.reset(local_power_control);
}

variableGenerator::variableGenerator(dynModel_t dynModel, const std::string& objName):
    DynamicGenerator(dynModel, objName)
{
    opFlags[variable_generation] = true;
    opFlags.reset(adjustable_P);
    opFlags.reset(local_power_control);
}

coreObject* variableGenerator::clone(coreObject* obj) const
{
    auto* gen = cloneBase<variableGenerator, DynamicGenerator>(this, obj);
    if (gen == nullptr) {
        return obj;
    }

    gen->mp_Vcutout = mp_Vcutout;
    gen->mp_Vmax = mp_Vmax;
    return gen;
}

// initial conditions of dynamic states

// initial conditions of dynamic states
void variableGenerator::dynObjectInitializeB(const IOdata& inputs,
                                             const IOdata& desiredOutput,
                                             IOdata& fieldSet)
{
    DynamicGenerator::dynObjectInitializeB(inputs, desiredOutput, fieldSet);
    IOdata args2{P};
    IOdata inputSet(4);
    if (m_source != nullptr) {
        m_source->dynInitializeB(inputs, {0.0}, inputSet);
    }
    if (m_cBlock != nullptr) {
        m_cBlock->dynInitializeB(inputs, {0.0}, inputSet);
    }
}

void variableGenerator::add(coreObject* obj)
{
    if (dynamic_cast<gridSubModel*>(obj) != nullptr) {
        add(static_cast<gridSubModel*>(obj));
    } else {
        DynamicGenerator::add(obj);
    }
}

void variableGenerator::add(gridSubModel* obj)
{
    if (dynamic_cast<Source*>(obj) != nullptr) {
        if (m_source != nullptr) {
            if (isSameObject(obj, m_source)) {
                return;
            }
            gridComponent::remove(m_source);
        }
        m_source = static_cast<Source*>(obj);
        m_source->locIndex = source_loc;

        obj->set("basefreq", systemBaseFrequency);
        addSubObject(obj);
    } else if (dynamic_cast<Block*>(obj) != nullptr) {
        if (m_cBlock != nullptr) {
            if (isSameObject(obj, m_cBlock)) {
                return;
            }

            gridComponent::remove(m_cBlock);
        }
        m_cBlock = static_cast<Block*>(obj);
        m_cBlock->locIndex = control_block_loc;
        obj->set("basefreq", systemBaseFrequency);
        addSubObject(obj);
    } else {
        DynamicGenerator::add(obj);
    }
}

// set properties
void variableGenerator::set(std::string_view param, std::string_view val)
{
    DynamicGenerator::set(param, val);
}

void variableGenerator::set(std::string_view param, double val, unit unitType)
{
    if (param == "vcutout") {
        mp_Vcutout = convert(val, unitType, puV, systemBasePower, localBaseVoltage);
    } else if (param == "vmax") {
        mp_Vmax = convert(val, unitType, puV, systemBasePower, localBaseVoltage);
    } else {
        DynamicGenerator::set(param, val, unitType);
    }
}

// compute the residual for the dynamic states
void variableGenerator::residual(const IOdata& inputs,
                                 const stateData& sD,
                                 double resid[],
                                 const solverMode& sMode)
{
    DynamicGenerator::residual(inputs, sD, resid, sMode);
    if ((m_source != nullptr) && (m_source->isEnabled())) {
        m_source->residual(inputs, sD, resid, sMode);
    }
    if ((m_cBlock != nullptr) && (m_cBlock->isEnabled())) {
        // TODO(PT):: this needs to be tied to the source
        m_cBlock->blockResidual(Pset, dPdt, sD, resid, sMode);
    }
}
void variableGenerator::jacobianElements(const IOdata& inputs,
                                         const stateData& sD,
                                         matrixData<double>& md,
                                         const IOlocs& inputLocs,
                                         const solverMode& sMode)
{
    DynamicGenerator::jacobianElements(inputs, sD, md, inputLocs, sMode);
    if ((m_source != nullptr) && (m_source->isEnabled())) {
        m_source->jacobianElements(inputs, sD, md, inputLocs, sMode);
    }
    if ((m_cBlock != nullptr) && (m_cBlock->isEnabled())) {
        m_cBlock->jacobianElements(inputs, sD, md, inputLocs, sMode);
    }
}

coreObject* variableGenerator::find(std::string_view object) const
{
    if (object == "source") {
        return m_source;
    }
    if (object == "cblock") {
        return m_cBlock;
    }
    return DynamicGenerator::find(object);
}

coreObject* variableGenerator::getSubObject(std::string_view typeName, index_t num) const
{
    auto* out = DynamicGenerator::getSubObject(typeName, num);
    if (out == nullptr) {
        out = find(typeName);
    }
    return out;
}

double variableGenerator::pSetControlUpdate(const IOdata& inputs,
                                            const stateData& sD,
                                            const solverMode& sMode)
{
    if ((m_cBlock != nullptr) && (m_cBlock->isEnabled())) {
        return m_cBlock->getOutput();
    }
    return DynamicGenerator::pSetControlUpdate(inputs, sD, sMode);
}

index_t variableGenerator::pSetLocation(const solverMode& sMode)
{
    if ((m_cBlock != nullptr) && (m_cBlock->isEnabled())) {
        return m_cBlock->getOutputLoc(sMode);
    }
    return DynamicGenerator::pSetLocation(sMode);
}

}  // namespace griddyn
