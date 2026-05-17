/*
 * Copyright (c) 2014-2026, Lawrence Livermore National Security
 * See the top-level NOTICE for additional details. All rights reserved.
 * SPDX-License-Identifier: BSD-3-Clause
 */

#include "grabberSource.h"

#include "../measurement/grabberSet.h"
#include "core/coreObjectTemplates.hpp"
#include "core/objectInterpreter.h"
#include <memory>
#include <string>

namespace griddyn::sources {
grabberSource::grabberSource(const std::string& objName): rampSource(objName) {}
grabberSource::~grabberSource() = default;

CoreObject* grabberSource::clone(CoreObject* obj) const
{
    auto src = cloneBase<grabberSource, Source>(this, obj);
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

void grabberSource::setFlag(std::string_view flag, bool val)
{
    if (flag.empty()) {
    } else {
        Source::setFlag(flag, val);
    }
}
void grabberSource::set(std::string_view param, std::string_view val)
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

void grabberSource::set(std::string_view param, double val, units::unit unitType)
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

double grabberSource::get(std::string_view param, units::unit unitType) const
{
    if (param == "multiplier") {
        return multiplier;
    }
    return Source::get(param, unitType);
}

void grabberSource::pFlowObjectInitializeA(coreTime /*time0*/, std::uint32_t /*flags*/)
{
    CoreObject* obj = locateObject(target, this);
    gset = std::make_unique<grabberSet>(field, obj);
    gset->setGain(multiplier);
}

void grabberSource::dynObjectInitializeB(const IOdata& /*inputs*/,
                                         const IOdata& /*desiredOutput*/,
                                         IOdata& fieldSet)
{
    fieldSet.resize(1);
    fieldSet[0] = gset->grabData();
}
IOdata grabberSource::getOutputs(const IOdata& /*inputs*/,
                                 const stateData& sD,
                                 const solverMode& sMode) const
{
    return {gset->grabData(sD, sMode)};
}
double grabberSource::getOutput(const IOdata& /*inputs*/,
                                const stateData& sD,
                                const solverMode& sMode,
                                index_t outputNum) const
{
    if (outputNum == 0) {
        return gset->grabData(sD, sMode);
    }
    return kNullVal;
}

double grabberSource::getOutput(index_t outputNum) const
{
    if (outputNum == 0) {
        return gset->grabData();
    }
    return kNullVal;
}

double grabberSource::getDoutdt(const IOdata& /*inputs*/,
                                const stateData& /*sD*/,
                                const solverMode& /*sMode*/,
                                index_t /*outputNum*/) const
{
    return 0.0;
}

void grabberSource::updateField(const std::string& newField)
{
    if (gset) {
        gset->updateField(newField);
    }
    field = newField;
}

void grabberSource::updateTarget(const std::string& newTarget)
{
    if (gset) {
        auto obj = locateObject(newTarget, this);
        gset->updateObject(obj);
    }
    target = newTarget;
}

void grabberSource::updateTarget(CoreObject* obj)
{
    if (gset) {
        gset->updateObject(obj);
    }
    target = obj->getName();
}
}  // namespace griddyn::sources

