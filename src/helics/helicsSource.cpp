/*
 * LLNS Copyright Start
 * Copyright (c) 2014-2018, Lawrence Livermore National Security
 * This work was performed under the auspices of the U.S. Department
 * of Energy by Lawrence Livermore National Laboratory in part under
 * Contract W-7405-Eng-48 and in part under Contract DE-AC52-07NA27344.
 * Produced at the Lawrence Livermore National Laboratory.
 * All rights reserved.
 * For details, see the LICENSE file.
 * LLNS Copyright End
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

namespace griddyn {
namespace helicsLib {
    helicsSource::helicsSource(const std::string& objName):
        rampSource(objName), valueType(helics::data_type::helics_double)
    {
        opFlags.set(pflow_init_required);
    }

    coreObject* helicsSource::clone(coreObject* obj) const
    {
        auto nobj = cloneBase<helicsSource, rampSource>(this, obj);
        if (nobj == nullptr) {
            return obj;
        }
        nobj->inputUnits = inputUnits;
        nobj->outputUnits = outputUnits;
        nobj->scaleFactor = scaleFactor;
        nobj->valKey = valKey;

        return nobj;
    }

    void helicsSource::pFlowObjectInitializeA(coreTime time0, std::uint32_t flags)
    {
        auto obj = getRoot();

        coord_ = dynamic_cast<helicsCoordinator*>(obj->find("helics"));
        rampSource::pFlowObjectInitializeA(time0, flags);

        if (updatePeriod == maxTime) {
            LOG_WARNING("no period specified defaulting to 10s");
            updatePeriod = 10.0;
        }
        nextUpdateTime = time0;
        if (valKey.empty()) {
            valKey = fullObjectName(this) + "value";
        }
        updateSubscription();
    }

    void helicsSource::pFlowObjectInitializeB()
    {
        updateA(prevTime);
        updateB();
    }

    void helicsSource::dynObjectInitializeA(coreTime time0, std::uint32_t flags)
    {
        rampSource::dynObjectInitializeA(time0, flags);

        if (updatePeriod == maxTime) {
            LOG_WARNING("no period specified defaulting to 10s");
            updatePeriod = 10.0;
        }
        nextUpdateTime = time0;
        updateA(time0);
        updateB();
    }

    void helicsSource::updateA(coreTime time)
    {
        if (time < nextUpdateTime) {
            return;
        }
        lastUpdateTime = time;
        if (!coord_->isUpdated(valueIndex)) {
            prevTime = time;
            return;
        }
        double cval;
        if (valueType == helics::data_type::helics_vector) {
            auto vals = coord_->getValueAs<std::vector<double>>(valueIndex);
            cval = vals[elementIndex];
        } else {
            cval = coord_->getValueAs<double>(valueIndex);
            if (cval == kNullVal) {
                mp_dOdt = 0.0;
                prevVal = m_output;
                prevTime = time;
                lastTime = time;
                return;
            }
        }

        double newVal = convert(cval * scaleFactor, inputUnits, outputUnits, systemBasePower);
        if (opFlags[use_ramp]) {
            if (opFlags[predictive_ramp])  // ramp uses the previous change to guess into the future
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

    void helicsSource::timestep(coreTime ttime, const IOdata& inputs, const solverMode& sMode)
    {
        while (ttime >= nextUpdateTime) {
            updateA(nextUpdateTime);
            updateB();
        }

        rampSource::timestep(ttime, inputs, sMode);
    }

    void helicsSource::setFlag(const std::string& param, bool val)
    {
        if (param == "initial_queury") {
            opFlags.set(initial_query, val);
        } else if (param == "predictive") {
            opFlags.set(use_ramp, val);
            opFlags.set(predictive_ramp, val);
        } else if (param == "interpolate") {
            opFlags.set(use_ramp, val);
            opFlags.set(predictive_ramp, !val);
        } else if (param == "step") {
            opFlags.set(use_ramp, !val);
        } else if (param == "use_ramp") {
            opFlags.set(use_ramp, val);
        } else {
            rampSource::setFlag(param, val);
        }
    }

    void helicsSource::set(const std::string& param, const std::string& val)
    {
        if ((param == "valkey") || (param == "key")) {
            valKey = val;
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

    void helicsSource::set(const std::string& param, double val, units::unit unitType)
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

    void helicsSource::updateSubscription()
    {
        if (coord_) {
            if (!valKey.empty()) {
                // coord_->registerSubscription(valKey, helicsRegister::dataType::helicsDouble,
                // def);

                if (valueIndex < 0) {
                    valueIndex = coord_->addSubscription(valKey, inputUnits);
                } else {
                    coord_->updateSubscription(valueIndex, valKey, inputUnits);
                }
                coord_->setDefault(
                    valueIndex,
                    convert(m_output / scaleFactor, outputUnits, inputUnits, systemBasePower));
            }
        }
    }

}  // namespace helicsLib
}  // namespace griddyn
