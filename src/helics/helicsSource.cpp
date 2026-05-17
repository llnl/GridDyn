/*
 * Copyright (c) 2014-2026, Lawrence Livermore National Security
 * See the top-level NOTICE for additional details. All rights reserved.
 * SPDX-License-Identifier: BSD-3-Clause
 */

#include "helicsSource.h"

#include "core/coreObjectTemplates.hpp"
#include "gmlc/utilities/stringOps.h"
#include "gmlc/utilities/vectorOps.hpp"
#include "griddyn/gridBus.h"
#include "helics/helicsCoordinator.h"
#include "helicsLibrary.h"
#include "helicsSupport.h"
#include <string>

namespace griddyn::helicsLib {
HelicsSource::HelicsSource(const std::string& objName):
    rampSource(objName), valueType(helics::data_type::helics_double)
{
    opFlags.set(pflow_init_required);
}

CoreObject* HelicsSource::clone(CoreObject* obj) const
{
    auto nobj = cloneBase<HelicsSource, rampSource>(this, obj);
    if (nobj == nullptr) {
        return obj;
    }
    nobj->inputUnits = inputUnits;
    nobj->outputUnits = outputUnits;
    nobj->scaleFactor = scaleFactor;
    nobj->valueKey = valueKey;

    return nobj;
}

void HelicsSource::pFlowObjectInitializeA(coreTime time0, std::uint32_t flags)
{
    auto obj = getRoot();

    coordinator_ = dynamic_cast<HelicsCoordinator*>(obj->find("helics"));
    rampSource::pFlowObjectInitializeA(time0, flags);

    if (updatePeriod == maxTime) {
        logging::warning(this, "no period specified defaulting to 10s");
        updatePeriod = 10.0;
    }
    nextUpdateTime = time0;
    if (valueKey.empty()) {
        valueKey = fullObjectName(this) + "value";
    }
    updateSubscription();
}

void HelicsSource::pFlowObjectInitializeB()
{
    updateA(prevTime);
    updateB();
}

void HelicsSource::dynObjectInitializeA(coreTime time0, std::uint32_t flags)
{
    rampSource::dynObjectInitializeA(time0, flags);

    if (updatePeriod == maxTime) {
        logging::warning(this, "no period specified defaulting to 10s");
        updatePeriod = 10.0;
    }
    nextUpdateTime = time0;
    updateA(time0);
    updateB();
}

void HelicsSource::updateA(coreTime time)
{
    if (time < nextUpdateTime) {
        return;
    }
    lastUpdateTime = time;
    if (!coordinator_->isUpdated(valueIndex)) {
        prevTime = time;
        return;
    }
    double cval;
    if (valueType == helics::data_type::helics_vector) {
        auto vals = coordinator_->getValueAs<std::vector<double>>(valueIndex);
        cval = vals[elementIndex];
    } else {
        cval = coordinator_->getValueAs<double>(valueIndex);
        if (cval == kNullVal) {
            mp_dOdt = 0.0;
            prevVal = m_output;
            prevTime = time;
            lastTime = time;
            return;
        }
    }

    double newVal = convert(cval * scaleFactor, inputUnits, outputUnits, systemBasePower);
    if (opFlags[USE_RAMP]) {
        if (opFlags[PREDICTIVE_RAMP])  // ramp uses the previous change to guess into the future
        {
            m_output = newVal;
            if ((time - lastTime) > 0.001) {
                mp_dOdt = (newVal - prevVal) / (time - lastTime);
            } else {
                mp_dOdt = 0;
            }
            prevVal = newVal;
        } else {  // output will ramp up to the specified value in the update period
            mp_dOdt = (newVal - m_output) / updatePeriod;
            prevVal = m_output;
        }
    } else {
        m_output = newVal;
        m_tempOut = newVal;
        prevVal = newVal;
        mp_dOdt = 0;
    }
    lastTime = time;
    prevTime = time;
}

void HelicsSource::timestep(coreTime ttime, const IOdata& inputs, const solverMode& sMode)
{
    while (ttime >= nextUpdateTime) {
        updateA(nextUpdateTime);
        updateB();
    }

    rampSource::timestep(ttime, inputs, sMode);
}

void HelicsSource::setFlag(const std::string& param, bool val)
{
    if (param == "initial_queury") {
        opFlags.set(INITIAL_QUERY, val);
    } else if (param == "predictive") {
        opFlags.set(USE_RAMP, val);
        opFlags.set(PREDICTIVE_RAMP, val);
    } else if (param == "interpolate") {
        opFlags.set(USE_RAMP, val);
        opFlags.set(PREDICTIVE_RAMP, !val);
    } else if (param == "step") {
        opFlags.set(USE_RAMP, !val);
    } else if (param == "use_ramp") {
        opFlags.set(USE_RAMP, val);
    } else {
        rampSource::setFlag(param, val);
    }
}

void HelicsSource::set(const std::string& param, const std::string& val)
{
    if ((param == "valkey") || (param == "key")) {
        valueKey = val;
        updateSubscription();
    } else if (param == "valuetype") {
        auto vType = helics::getTypeFromString(val);
        if (vType == helics::data_type::helics_unknown) {
            throw(invalidParameterValue("unrecognized value type " + val));
        }
        valueType = vType;
    } else if ((param == "input_units") || (param == "inunits") || (param == "inputunits")) {
        inputUnits = units::unit_cast_from_string(val);
        updateSubscription();
    } else if ((param == "output_units") || (param == "outunits") || (param == "outputunits")) {
        outputUnits = units::unit_cast_from_string(val);
        updateSubscription();
    } else if (param == "units") {
        auto uval = units::unit_cast_from_string(val);
        if (!units::is_valid(uval)) {
            if (val != "default") {
                throw(invalidParameterValue("unknown unit type " + val));
            }
        }
        inputUnits = uval;
        outputUnits = uval;
        updateSubscription();
    } else {
        // no reason to set the ramps in helics source so go to Source instead
        Source::set(param, val);
    }
}

void HelicsSource::set(const std::string& param, double val, units::unit unitType)
{
    if ((param == "scalefactor") || (param == "scaling")) {
        scaleFactor = val;
        updateSubscription();
    } else if (param == "element") {
        elementIndex = static_cast<int>(val);
    } else {
        Source::set(param, val, unitType);
    }
}

void HelicsSource::updateSubscription()
{
    if (coordinator_) {
        if (!valueKey.empty()) {
            // coordinator_->registerSubscription(valueKey, helicsRegister::dataType::helicsDouble,
            // def);

            if (valueIndex < 0) {
                valueIndex = coordinator_->addSubscription(valueKey, inputUnits);
            } else {
                coordinator_->updateSubscription(valueIndex, valueKey, inputUnits);
            }
            coordinator_->setDefault(
                valueIndex,
                convert(m_output / scaleFactor, outputUnits, inputUnits, systemBasePower));
        }
    }
}

}  // namespace griddyn::helicsLib
