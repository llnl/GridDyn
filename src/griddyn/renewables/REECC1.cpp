/*
 * Copyright (c) 2014-2026, Lawrence Livermore National Security
 * See the top-level NOTICE for additional details. All rights reserved.
 * SPDX-License-Identifier: BSD-3-Clause
 */
#include "REECC1.h"

#include "core/CoreExceptions.h"
#include "core/CoreObjectTemplates.hpp"
#include "core/ModelParameter.hpp"
#include "gmlc/utilities/stringOps.h"
#include <cmath>
#include <string>
#include <utility>

namespace griddyn {

const ModelDescription& REECC1::getModelDescription() const
{
    static const auto description = ModelDescription::define<REECC1>(
        "REECC1",
        &REECA1::getModelDescription(),
        {parameter({"t"}, &REECC1::T, units::second, "T"),
         parameter({"socini"}, &REECC1::SOCini, units::one, "Socini"),
         parameter({"socmax"}, &REECC1::SOCmax, units::one, "Socmax"),
         parameter({"socmin"}, &REECC1::SOCmin, units::one, "Socmin"),
         localState("minimumPowerConfigured",
                    &REECC1::minimumPowerConfigured,
                    "Minimum power configured"),
         command({"pflag"}, units::defunit, "Set pflag"),
         command({"pmin"}, units::defunit, "Set pmin"),
         command({"pmax"}, units::defunit, "Set pmax")});
    return description;
}

void REECC1::getParameterStrings(stringVec& pstr, ParamStringType pstype) const
{
    const auto& description = REECC1::getModelDescription();
    getParamString<REECC1, REECA1>(this,
                                   pstr,
                                   description.numericNames(),
                                   description.stringNames(),
                                   description.flagNames(),
                                   pstype);
}

REECC1::REECC1(const std::string& objName): REECA1(objName)
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
    if (REECC1::getModelDescription().trySetLocal(*this, param, val, unitType)) {
        return;
    }
    const auto key = gmlc::utilities::convertToLowerCase(std::string{param});
    if (key != param && REECC1::getModelDescription().trySetLocal(*this, key, val, unitType)) {
        return;
    }
    if (key == "pflag") {
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
    if (const auto value = REECC1::getModelDescription().tryGetLocal(*this, key, unitType)) {
        return *value;
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
    if (std::cmp_less_equal(inputs.size(), powerIndex) || inputs[powerIndex] == kNullVal ||
        !std::isfinite(inputs[powerIndex])) {
        throw InvalidParameterValue("REECC1 requires measured electrical power from REGC_A");
    }
    // Positive Pgen is discharge, so SOC falls; charging power is negative.
    return -inputs[powerIndex] / T;
}

std::pair<double, double>
    REECC1::activeCurrentBounds(const IOdata& /*inputs*/, const double state[], double ipCap) const
{
    const double soc = state[storageSocStateIndex()];
    const double lower = (soc >= SOCmax) ? 0.0 : -ipCap;
    const double upper = (soc <= SOCmin) ? 0.0 : ipCap;
    return {lower, upper};
}

}  // namespace griddyn
