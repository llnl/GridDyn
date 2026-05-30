/*
 * Copyright (c) 2014-2026, Lawrence Livermore National Security
 * See the top-level NOTICE for additional details. All rights reserved.
 * SPDX-License-Identifier: BSD-3-Clause
 */

#include "GrabberSource.h"

#include "../measurement/GrabberSet.h"
#include "core/CoreObjectTemplates.hpp"
#include "core/ObjectInterpreter.h"
#include <memory>
#include <string>

namespace griddyn::sources {
GrabberSource::GrabberSource(const std::string& objName): RampSource(objName) {}
GrabberSource::~GrabberSource() = default;

CoreObject* GrabberSource::clone(CoreObject* obj) const
{
    auto src = cloneBase<GrabberSource, Source>(this, obj);
    if (src == nullptr) {
        return obj;
    }
    src->updateTarget(target);
    src->updateField(field);
    src->set("gain", multiplier);
    if ((gset) && (!src->gset)) {
        src->pFlowInitializeA(prevTime, 0);
    }
    return src;
}

void GrabberSource::setFlag(std::string_view flag, bool val)
{
    if (flag.empty()) {
    } else {
        Source::setFlag(flag, val);
    }
}
void GrabberSource::set(std::string_view param, std::string_view val)
{
    if (param == "field") {
        if (opFlags[dyn_initialized]) {
            updateField(std::string{val});
        } else {
            field = val;
        }
    } else if (param == "target") {
        if (opFlags[dyn_initialized]) {
            updateTarget(target);
        } else {
            target = val;
        }
    } else {
        Source::set(param, val);
    }
}

void GrabberSource::set(std::string_view param, double val, units::unit unitType)
{
    if ((param == "gain") || (param == "multiplier")) {
        multiplier = val;
        if (gset) {
            gset->setGain(multiplier);
        }
    } else {
        Source::set(param, val, unitType);
    }
}

double GrabberSource::get(std::string_view param, units::unit unitType) const
{
    if (param == "multiplier") {
        return multiplier;
    }
    return Source::get(param, unitType);
}

void GrabberSource::pFlowObjectInitializeA(coreTime /*time0*/, std::uint32_t /*flags*/)
{
    CoreObject* obj = locateObject(target, this);
    gset = std::make_unique<GrabberSet>(field, obj);
    gset->setGain(multiplier);
}

void GrabberSource::dynObjectInitializeB(const IOdata& /*inputs*/,
                                         const IOdata& /*desiredOutput*/,
                                         IOdata& fieldSet)
{
    fieldSet.resize(1);
    fieldSet[0] = gset->grabData();
}
IOdata GrabberSource::getOutputs(const IOdata& /*inputs*/,
                                 const StateData& sD,
                                 const SolverMode& sMode) const
{
    return {gset->grabData(sD, sMode)};
}
double GrabberSource::getOutput(const IOdata& /*inputs*/,
                                const StateData& sD,
                                const SolverMode& sMode,
                                index_t outputNum) const
{
    if (outputNum == 0) {
        return gset->grabData(sD, sMode);
    }
    return kNullVal;
}

double GrabberSource::getOutput(index_t outputNum) const
{
    if (outputNum == 0) {
        return gset->grabData();
    }
    return kNullVal;
}

double GrabberSource::getDoutdt(const IOdata& /*inputs*/,
                                const StateData& /*sD*/,
                                const SolverMode& /*sMode*/,
                                index_t /*outputNum*/) const
{
    return 0.0;
}

void GrabberSource::updateField(const std::string& newField)
{
    if (gset) {
        gset->updateField(newField);
    }
    field = newField;
}

void GrabberSource::updateTarget(const std::string& newTarget)
{
    if (gset) {
        auto obj = locateObject(newTarget, this);
        gset->updateObject(obj);
    }
    target = newTarget;
}

void GrabberSource::updateTarget(CoreObject* obj)
{
    if (gset) {
        gset->updateObject(obj);
    }
    target = obj->getName();
}
}  // namespace griddyn::sources
