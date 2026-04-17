/*
 * Copyright (c) 2014-2026, Lawrence Livermore National Security
 * See the top-level NOTICE for additional details. All rights reserved.
 * SPDX-License-Identifier: BSD-3-Clause
 */

#include "fDepLoad.h"

#include "../gridBus.h"
#include "core/coreObjectTemplates.hpp"
#include "gmlc/utilities/stringOps.h"
#include "utilities/matrixData.hpp"
#include <cmath>
#include <string>
namespace griddyn {
namespace loads {
    fDepLoad::fDepLoad(const std::string& objName): exponentialLoad(objName) {}
    fDepLoad::fDepLoad(double rP, double qP, const std::string& objName):
        exponentialLoad(rP, qP, objName)
    {
    }
    void fDepLoad::dynObjectInitializeA(coreTime time0, std::uint32_t flags)
    {
        if ((betaP != 0.0) || (betaQ != 0.0)) {
            opFlags.set(uses_bus_frequency);
        }
        return exponentialLoad::dynObjectInitializeA(time0, flags);
    }

    coreObject* fDepLoad::clone(coreObject* obj) const
    {
        auto ld = cloneBase<fDepLoad, exponentialLoad>(this, obj);
        if (ld == nullptr) {
            return obj;
        }

        ld->betaP = betaP;
        ld->betaQ = betaQ;
        return ld;
    }

    // set properties
    void fDepLoad::set(const std::string& param, const std::string& val)
    {
        if (param == "loadtype") {
            auto vtype = gmlc::utilities::convertToLowerCase(val);
            if (vtype == "fluorescent") {
                alphaP = 1.2;
                alphaQ = 3.0;
                betaP = -0.1;
                betaQ = 2.8;
            } else if (vtype == "incandescent") {
                alphaP = 1.6;
                alphaQ = 0.0;
                betaP = 0.0;
                betaQ = 0.0;
            } else if (vtype == "heater") {
                alphaP = 2.0;
                alphaQ = 0.0;
                betaP = 0.0;
                betaQ = 0.0;
            } else if (vtype == "motor-full") {
                alphaP = 0.1;
                alphaQ = 0.6;
                betaP = 2.8;
                betaQ = 1.8;
            } else if (vtype == "motor-half") {
                alphaP = 0.2;
                alphaQ = 1.6;
                betaP = 1.5;
                betaQ = -0.3;
            } else if (vtype == "Reduction_furnace") {
                alphaP = 1.9;
                alphaQ = 2.1;
                betaP = -0.5;
                betaQ = 0.0;
            } else if (vtype == "aluminum_plant") {
                alphaP = 1.8;
                alphaQ = 2.2;
                betaP = -0.3;
                betaQ = 0.6;
            }
        } else {
            exponentialLoad::set(param, val);
        }
    }

    void fDepLoad::set(const std::string& param, double val, units::unit unitType)
    {
        if (param == "betap") {
            betaP = val;
        } else if (param == "betaq") {
            betaQ = val;
        } else if (param == "beta") {
            betaP = betaQ = val;
        } else {
            exponentialLoad::set(param, val, unitType);
        }
        if ((betaP != 0.0) || (betaQ != 0.0)) {
            opFlags.set(uses_bus_frequency);
        }
    }

    void fDepLoad::ioPartialDerivatives(const IOdata& inputs,
                                        const stateData& /*sD*/,
                                        matrixData<double>& md,
                                        const IOlocs& inputLocs,
                                        const solverMode& /*sMode*/)
    {
        const double V = inputs[voltageInLocation];
        double freq = inputs[frequencyInLocation];
        // power vs voltage
        if (inputLocs[voltageInLocation] != kNullLocation) {
            md.assign(PoutLocation,
                      inputLocs[voltageInLocation],
                      getP() * alphaP * pow(V, alphaP - 1.0) * pow(freq, betaP));

            // reactive power vs voltage
            md.assign(QoutLocation,
                      inputLocs[voltageInLocation],
                      getQ() * alphaQ * pow(V, alphaQ - 1.0) * pow(freq, betaQ));
        }
        if (inputLocs[frequencyInLocation] != kNullLocation) {
            md.assign(PoutLocation,
                      inputLocs[frequencyInLocation],
                      getP() * pow(V, alphaP) * betaP * pow(freq, betaP - 1.0));
            md.assign(QoutLocation,
                      inputLocs[frequencyInLocation],
                      getQ() * pow(V, alphaQ) * betaQ * pow(freq, betaQ - 1.0));
        }
    }

    double fDepLoad::getRealPower() const
    {
        return getRealPower(bus->getVoltage(), bus->getFreq());
    }
    double fDepLoad::getReactivePower() const
    {
        return getReactivePower(bus->getVoltage(), bus->getFreq());
    }
    double fDepLoad::getRealPower(const IOdata& inputs,
                                  const stateData& /*sD*/,
                                  const solverMode& /*sMode*/) const
    {
        return getRealPower(inputs[voltageInLocation], inputs[frequencyInLocation]);
    }

    double fDepLoad::getReactivePower(const IOdata& inputs,
                                      const stateData& /*sD*/,
                                      const solverMode& /*sMode*/) const
    {
        return getReactivePower(inputs[voltageInLocation], inputs[frequencyInLocation]);
    }

    double fDepLoad::getRealPower(const double V) const
    {
        return getRealPower(V, bus->getFreq());
    }
    double fDepLoad::getReactivePower(double V) const
    {
        return getReactivePower(V, bus->getFreq());
    }
    double fDepLoad::getRealPower(double V, double f) const
    {
        if (isConnected()) {
            double val = getP();
            val *= pow(V, alphaP) * pow(f, betaP);
            return val;
        }
        return 0.0;
    }

    double fDepLoad::getReactivePower(double V, double f) const
    {
        if (isConnected()) {
            double val = getQ();
            val *= pow(V, alphaQ) * pow(f, betaQ);
            return val;
        }
        return 0.0;
    }
}  // namespace loads
}  // namespace griddyn
