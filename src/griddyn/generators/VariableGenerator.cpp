/*
 * Copyright (c) 2014-2026, Lawrence Livermore National Security
 * See the top-level NOTICE for additional details. All rights reserved.
 * SPDX-License-Identifier: BSD-3-Clause
 */

#include "VariableGenerator.h"

#include "../Block.h"
#include "../GridBus.h"
#include "../Source.h"
#include "core/CoreObjectTemplates.hpp"
#include <string>

namespace griddyn {
using units::convert;
using units::puV;
using units::unit;

VariableGenerator::VariableGenerator(const std::string& objName): DynamicGenerator(objName)
{
    opFlags[variableGeneration] = true;
    opFlags.reset(adjustable_P);
    opFlags.reset(local_power_control);
}

VariableGenerator::VariableGenerator(DynModel dynModel, const std::string& objName):
    DynamicGenerator(dynModel, objName)
{
    opFlags[variableGeneration] = true;
    opFlags.reset(adjustable_P);
    opFlags.reset(local_power_control);
}

CoreObject* VariableGenerator::clone(CoreObject* obj) const
{
    auto* gen = cloneBase<VariableGenerator, DynamicGenerator>(this, obj);
    if (gen == nullptr) {
        return obj;
    }

    gen->mp_Vcutout = mp_Vcutout;
    gen->mp_Vmax = mp_Vmax;
    return gen;
}

// initial conditions of dynamic states

// initial conditions of dynamic states
void VariableGenerator::dynObjectInitializeB(const IOdata& inputs,
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

void VariableGenerator::add(CoreObject* obj)
{
    if (dynamic_cast<GridSubModel*>(obj) != nullptr) {
        add(static_cast<GridSubModel*>(obj));
    } else {
        DynamicGenerator::add(obj);
    }
}

void VariableGenerator::add(GridSubModel* obj)
{
    if (dynamic_cast<Source*>(obj) != nullptr) {
        if (m_source != nullptr) {
            if (isSameObject(obj, m_source)) {
                return;
            }
            GridComponent::remove(m_source);
        }
        m_source = static_cast<Source*>(obj);
        m_source->locIndex = source_loc;

        obj->set("basefreq", systemBaseFrequency);
        addSubObject(obj);
    } else if (dynamic_cast<GridBlock*>(obj) != nullptr) {
        if (m_cBlock != nullptr) {
            if (isSameObject(obj, m_cBlock)) {
                return;
            }

            GridComponent::remove(m_cBlock);
        }
        m_cBlock = static_cast<GridBlock*>(obj);
        m_cBlock->locIndex = control_block_loc;
        obj->set("basefreq", systemBaseFrequency);
        addSubObject(obj);
    } else {
        DynamicGenerator::add(obj);
    }
}

// set properties
void VariableGenerator::set(std::string_view param, std::string_view val)
{
    DynamicGenerator::set(param, val);
}

void VariableGenerator::set(std::string_view param, double val, unit unitType)
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
void VariableGenerator::residual(const IOdata& inputs,
                                 const stateData& stateDataValue,
                                 double resid[],
                                 const SolverMode& sMode)
{
    DynamicGenerator::residual(inputs, stateDataValue, resid, sMode);
    if ((m_source != nullptr) && (m_source->isEnabled())) {
        m_source->residual(inputs, stateDataValue, resid, sMode);
    }
    if ((m_cBlock != nullptr) && (m_cBlock->isEnabled())) {
        // TODO(PT):: this needs to be tied to the source
        m_cBlock->blockResidual(Pset, dPdt, stateDataValue, resid, sMode);
    }
}
void VariableGenerator::jacobianElements(const IOdata& inputs,
                                         const stateData& stateDataValue,
                                         matrixData<double>& matrixDataValue,
                                         const IOlocs& inputLocs,
                                         const SolverMode& sMode)
{
    DynamicGenerator::jacobianElements(
        inputs, stateDataValue, matrixDataValue, inputLocs, sMode);
    if ((m_source != nullptr) && (m_source->isEnabled())) {
        m_source->jacobianElements(inputs, stateDataValue, matrixDataValue, inputLocs, sMode);
    }
    if ((m_cBlock != nullptr) && (m_cBlock->isEnabled())) {
        m_cBlock->jacobianElements(inputs, stateDataValue, matrixDataValue, inputLocs, sMode);
    }
}

CoreObject* VariableGenerator::find(std::string_view object) const
{
    if (object == "source") {
        return m_source;
    }
    if (object == "cblock") {
        return m_cBlock;
    }
    return DynamicGenerator::find(object);
}

CoreObject* VariableGenerator::getSubObject(std::string_view typeName, index_t num) const
{
    auto* out = DynamicGenerator::getSubObject(typeName, num);
    if (out == nullptr) {
        out = find(typeName);
    }
    return out;
}

double VariableGenerator::pSetControlUpdate(const IOdata& inputs,
                                            const stateData& stateDataValue,
                                            const SolverMode& sMode)
{
    if ((m_cBlock != nullptr) && (m_cBlock->isEnabled())) {
        return m_cBlock->getOutput();
    }
    return DynamicGenerator::pSetControlUpdate(inputs, stateDataValue, sMode);
}

index_t VariableGenerator::pSetLocation(const SolverMode& sMode)
{
    if ((m_cBlock != nullptr) && (m_cBlock->isEnabled())) {
        return m_cBlock->getOutputLoc(sMode);
    }
    return DynamicGenerator::pSetLocation(sMode);
}

}  // namespace griddyn
