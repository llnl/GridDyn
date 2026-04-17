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

#include "helicsLoad.h"

#include "core/coreObjectTemplates.hpp"
#include "gmlc/utilities/stringOps.h"
#include "gmlc/utilities/vectorOps.hpp"
#include "griddyn/gridBus.h"
#include "helicsCoordinator.h"
#include "helicsLibrary.h"
#include <map>
#include <string>

namespace griddyn::helicsLib {
    helicsLoad::helicsLoad(const std::string& objName):
        rampLoad(objName), loadType(helics::data_type::helics_complex),
        voltageType(helics::data_type::helics_complex)
    {
    }

    coreObject* helicsLoad::clone(coreObject* obj) const
    {
        auto nobj = cloneBase<helicsLoad, loads::rampLoad>(this, obj);
        if (nobj == nullptr) {
            return obj;
        }
        nobj->inputUnits = inputUnits;
        nobj->scaleFactor = scaleFactor;
        nobj->loadKey = loadKey;
        nobj->voltageKey = voltageKey;

        return nobj;
    }

    void helicsLoad::pFlowObjectInitializeA(coreTime time0, uint32_t flags)
    {
        if (coord == nullptr) {
            auto rt = getRoot();
            coord = static_cast<helicsCoordinator*>(rt->find("helics"));
        }
        setSubscription();
        loads::rampLoad::pFlowObjectInitializeA(time0, flags);

        prevP = getP();
        prevQ = getQ();
    }

    void helicsLoad::pFlowObjectInitializeB()
    {
        updateA(prevTime);
        // clear any ramps initially
        dPdt = 0.0;
        dQdt = 0.0;
    }

    void helicsLoad::updateA(coreTime time)
    {
        double V = bus->getVoltage();
        double A = bus->getAngle();

        if (!voltageKey.empty()) {
            std::complex<double> Vc = std::polar(V, A);
            Vc *= localBaseVoltage;
            coord->publish(voltageIndex, Vc);
        }
        lastUpdateTime = time;
    }

    coreTime helicsLoad::updateB()
    {
        nextUpdateTime += updatePeriod;

        // now get the updates
        if (!coord->isUpdated(loadIndex)) {
            dPdt = 0.0;
            dQdt = 0.0;
            prevP = getP();
            prevQ = getQ();
            return nextUpdateTime;
        }
        auto res = coord->getValueAs<std::complex<double>>(loadIndex);
        if (res.real() == kNullVal) {
            dPdt = 0.0;
            dQdt = 0.0;
            prevP = getP();
            prevQ = getQ();
            return nextUpdateTime;
        }
        res = res * scaleFactor;
        double newP =
            convert(res.real(), inputUnits, units::puMW, systemBasePower, localBaseVoltage);
        double newQ =
            convert(res.imag(), inputUnits, units::puMW, systemBasePower, localBaseVoltage);

        if (opFlags[use_ramp]) {
            if (opFlags[predictive_ramp])  // ramp uses the previous change to guess into the future
            {
                setP(newP);
                setQ(newQ);
                if ((prevTime - lastUpdateTime) > 0.001) {
                    dPdt = (newP - prevP) / (prevTime - lastUpdateTime);
                    dQdt = (newQ - prevQ) / (prevTime - lastUpdateTime);
                } else {
                    dPdt = 0.0;
                    dQdt = 0.0;
                }
                prevP = newP;
                prevQ = newQ;
                prevTime = lastUpdateTime;
            } else {
                // output will ramp up to the specified value in the update period
                dPdt = (newP - getP()) / updatePeriod;
                dQdt = (newQ - getQ()) / updatePeriod;
                prevP = getP();
                prevQ = getQ();
            }
        } else {
            setP(newP);
            setQ(newQ);
            prevP = newP;
            prevQ = newQ;
            dPdt = 0;
            dQdt = 0;
        }
        return nextUpdateTime;
    }

    void helicsLoad::timestep(coreTime ttime, const IOdata& inputs, const solverMode& sMode)
    {
        while (ttime > nextUpdateTime) {
            updateA(nextUpdateTime);
            updateB();
        }

        rampLoad::timestep(ttime, inputs, sMode);
    }

    void helicsLoad::setFlag(const std::string& flag, bool val)
    {
        if (flag == "initial_query") {
            opFlags.set(initial_query, val);
        } else if (flag == "predictive") {
            if (val) {
                opFlags.set(use_ramp, val);
                opFlags.set(predictive_ramp, val);
            } else {
                opFlags.set(predictive_ramp, false);
            }
        } else if (flag == "interpolate") {
            opFlags.set(use_ramp, val);
            opFlags.set(predictive_ramp, !val);
        } else if (flag == "step") {
            opFlags.set(use_ramp, !val);
        } else if (flag == "use_ramp") {
            opFlags.set(use_ramp, val);
        } else {
            rampLoad::setFlag(flag, val);
        }
    }

    void helicsLoad::set(const std::string& param, const std::string& val)
    {
        if (param == "voltagekey") {
            voltageKey = val;
        } else if (param == "voltagetype") {
            auto type = helics::getTypeFromString(val);
            if (type != helics::data_type::helics_unknown) {
                voltageType = type;
            } else {
                throw(invalidParameterValue("unrecognized type"));
            }
        } else if (param == "loadkey") {
            loadKey = val;
        } else if (param == "loadtype") {
            auto type = helics::getTypeFromString(val);
            if (type != helics::data_type::helics_unknown) {
                loadType = type;
            } else {
                throw(invalidParameterValue("unrecognized type"));
            }
        } else if ((param == "units") || (param == "inputunits")) {
            inputUnits = units::unit_cast_from_string(val);
        } else {
            // no reason to set the ramps in helics load so go to zipLoad instead
            zipLoad::set(param, val);  // NOLINT
        }
    }

    void helicsLoad::set(const std::string& param, double val, units::unit unitType)
    {
        if ((param == "scalefactor") || (param == "scaling")) {
            scaleFactor = val;
            setSubscription();
        } else {
            zipLoad::set(param, val, unitType);
        }
    }

    void helicsLoad::setSubscription()
    {
        if (coord != nullptr) {
            if (!loadKey.empty()) {
                auto Punit = convert(getP(), units::puMW, inputUnits, systemBasePower);
                auto Qunit = convert(getQ(), units::puMW, inputUnits, systemBasePower);
                std::string def = std::to_string(Punit / scaleFactor) + "+" +
                    std::to_string(Qunit / scaleFactor) + "j";
                if (loadIndex < 0) {
                    loadIndex = coord->addSubscription(loadKey, inputUnits);
                } else {
                    coord->updateSubscription(loadIndex, loadKey, inputUnits);
                }
                coord->setDefault(loadIndex, std::complex<double>(Punit, Qunit));
            }
            if (!voltageKey.empty()) {
                if (voltageIndex < 0) {
                    voltageIndex = coord->addPublication(voltageKey, voltageType);
                } else {
                    coord->updatePublication(voltageIndex, voltageKey, voltageType);
                }
            }
        }
    }

}  // namespace griddyn::helicsLib
