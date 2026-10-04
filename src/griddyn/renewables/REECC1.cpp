/*
 * Copyright (c) 2014-2026, Lawrence Livermore National Security
 * See the top-level NOTICE for additional details. All rights reserved.
 * SPDX-License-Identifier: BSD-3-Clause
 */
#include "REECC1.h"

#include "core/CoreExceptions.h"
#include "core/CoreObjectTemplates.hpp"
#include "gmlc/utilities/stringOps.h"
#include <cmath>
#include <string>

namespace griddyn {

REECC1::REECC1(const std::string& name): REECA1(name)
{
    // REEC_C uses a fixed current limit unless its PSS/E V-I curves are supplied.
    for (int index = 1; index <= 4; ++index) {
        const auto suffix = std::to_string(index);
        REECA1::set("vq" + suffix, 0.0);
        REECA1::set("iq" + suffix, 0.0);
        REECA1::set("vp" + suffix, 0.0);
        REECA1::set("ip" + suffix, 0.0);
    }
    REECA1::set("pmin", -999.0);
    updateInputSize();
    m_outputSize = 4;
}

CoreObject* REECC1::clone(CoreObject* obj) const
{
    auto* out = cloneBase<REECC1, REECA1>(this, obj);
    if (out != nullptr) {
        out->T = T;
        out->SOCini = SOCini;
        out->SOCmax = SOCmax;
        out->SOCmin = SOCmin;
        out->minimumPowerConfigured = minimumPowerConfigured;
    }
    return out == nullptr ? obj : out;
}

void REECC1::set(std::string_view param, double val, units::unit unitType)
{
    const auto key = gmlc::utilities::convertToLowerCase(std::string{param});
    if (key == "t") {
        T = val;
    } else if (key == "socini") {
        SOCini = val;
    } else if (key == "socmax") {
        SOCmax = val;
    } else if (key == "socmin") {
        SOCmin = val;
    } else if (key == "pflag") {
        if (val != 0.0) {
            throw InvalidParameterValue("REECC1 does not support speed-modulated active power");
        }
        REECA1::set(param, val, unitType);
    } else if (key == "pmin") {
        minimumPowerConfigured = true;
        REECA1::set(param, val, unitType);
    } else if (key == "pmax") {
        REECA1::set(param, val, unitType);
        if (!minimumPowerConfigured) {
            REECA1::set("pmin", -std::abs(val));
        }
    } else {
        REECA1::set(param, val, unitType);
    }
}

double REECC1::get(std::string_view param, units::unit unitType) const
{
    const auto key = gmlc::utilities::convertToLowerCase(std::string{param});
    if (key == "t") {
        return T;
    }
    if (key == "socini") {
        return SOCini;
    }
    if (key == "socmax") {
        return SOCmax;
    }
    if (key == "socmin") {
        return SOCmin;
    }
    return REECA1::get(param, unitType);
}

void REECC1::dynObjectInitializeA(CoreTime time0, std::uint32_t flags)
{
    if (!std::isfinite(T) || T <= 0.0 || !std::isfinite(SOCini) || !std::isfinite(SOCmax) ||
        !std::isfinite(SOCmin) || SOCmin < 0.0 || SOCmax > 1.0 || SOCmin >= SOCmax ||
        SOCini < SOCmin || SOCini > SOCmax) {
        throw InvalidParameterValue("REECC1 requires T>0 and SOCmin <= SOCini <= SOCmax in [0,1]");
    }
    REECA1::dynObjectInitializeA(time0, flags);
}

double REECC1::storageSocRate(const IOdata& inputs) const
{
    const auto powerIndex = electricalPowerInputIndex();
    if (inputs.size() <= powerIndex || inputs[powerIndex] == kNullVal ||
        !std::isfinite(inputs[powerIndex])) {
        throw InvalidParameterValue("REECC1 requires measured electrical power from REGC_A");
    }
    // Positive Pgen is discharge, so SOC falls; charging power is negative.
    return -inputs[powerIndex] / T;
}

std::pair<double, double> REECC1::activeCurrentBounds(const IOdata& /*inputs*/,
                                                      const double state[],
                                                      double ipCap) const
{
    const double soc = state[storageSocStateIndex()];
    const double lower = (soc >= SOCmax) ? 0.0 : -ipCap;
    const double upper = (soc <= SOCmin) ? 0.0 : ipCap;
    return {lower, upper};
}

}  // namespace griddyn
