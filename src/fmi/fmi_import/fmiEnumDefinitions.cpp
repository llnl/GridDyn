/*
 * Copyright (c) 2014-2020, Lawrence Livermore National Security
 * See the top-level NOTICE for additional details. All rights reserved.
 * SPDX-License-Identifier: BSD-3-Clause
 */

#include "fmiEnumDefinitions.h"

#include "fmiInfo.h"
#include "gmlc/containers/mapOps.hpp"
#include "gmlc/utilities/stringOps.h"

#include <string>
#include <unordered_map>

fmi_variability::fmi_variability(const std::string& vstring)
{
    if (vstring == "continuous") {
        variability = fmi_variability_type_t::continuous;
    } else if (vstring == "constant") {
        variability = fmi_variability_type_t::constant;
    } else if (vstring == "fixed") {
        variability = fmi_variability_type_t::fixed;
    } else if (vstring == "tunable") {
        variability = fmi_variability_type_t::tunable;
    } else if (vstring == "discrete") {
        variability = fmi_variability_type_t::discrete;
    } else {
        variability = fmi_variability_type_t::unknown;
    }
}

std::string fmi_variability::to_string() const
{
    switch (variability) {
        case fmi_variability_type_t::continuous:
            return "continuous";
            break;
        case fmi_variability_type_t::fixed:
            return "fixed";
            break;
        case fmi_variability_type_t::constant:
            return "constant";
            break;
        case fmi_variability_type_t::discrete:
            return "discrete";
            break;
        case fmi_variability_type_t::tunable:
            return "tunable";
            break;
        default:
            return "unknown";
    }
}

static const std::unordered_map<std::string, fmi_causality_type_t> causality_map{
    {"local", fmi_causality_type_t::local},
    {"parameter", fmi_causality_type_t::parameter},
    {"param", fmi_causality_type_t::parameter},
    {"calculatedParameter", fmi_causality_type_t::calculatedParameter},
    {"calculated", fmi_causality_type_t::calculatedParameter},
    {"input", fmi_causality_type_t::input},
    {"inputs", fmi_causality_type_t::input},
    {"output", fmi_causality_type_t::output},
    {"outputs", fmi_causality_type_t::output},
    {"independent", fmi_causality_type_t::independent},
    {"time", fmi_causality_type_t::independent},
    {"any", fmi_causality_type_t::any},
    {"unknown", fmi_causality_type_t::unknown},
};

fmi_causality::fmi_causality(const std::string& vstring)
{
    causality = mapFind(causality_map, vstring, fmi_causality_type_t::unknown);
}

std::string fmi_causality::to_string() const
{
    switch (causality) {
        case fmi_causality_type_t::local:
            return "local";
            break;
        case fmi_causality_type_t::parameter:
            return "parameter";
            break;
        case fmi_causality_type_t::calculatedParameter:
            return "calculatedParameter";
            break;
        case fmi_causality_type_t::input:
            return "input";
            break;
        case fmi_causality_type_t::output:
            return "output";
            break;
        case fmi_causality_type_t::independent:
            return "independent";
            break;
        case fmi_causality_type_t::any:
            return "any";
            break;
        default:
            return "unknown";
            break;
    }
}

fmi_variable_type::fmi_variable_type(const std::string& vstring)
{
    if (vstring == "real") {
        variable = fmi_variable_type_t::real;
    } else if (vstring == "integer") {
        variable = fmi_variable_type_t::integer;
    } else if (vstring == "boolean") {
        variable = fmi_variable_type_t::boolean;
    } else if (vstring == "string") {
        variable = fmi_variable_type_t::string;
    } else if (vstring == "enumeration") {
        variable = fmi_variable_type_t::enumeration;
    } else {
        variable = fmi_variable_type_t::unknown;
    }
}

std::string fmi_variable_type::to_string() const
{
    switch (variable) {
        case fmi_variable_type_t::real:
            return "real";
            break;
        case fmi_variable_type_t::integer:
            return "integer";
            break;
        case fmi_variable_type_t::boolean:
            return "boolean";
            break;
        case fmi_variable_type_t::string:
            return "string";
            break;
        case fmi_variable_type_t::enumeration:
            return "enumeration";
            break;
        default:
            return "unknown";
            break;
    }
}

fmi_dependency_type::fmi_dependency_type(const std::string& vstring)
{
    if (vstring == "dependent") {
        dependency = fmi_dependency_type_t::dependent;
    } else if (vstring == "constant") {
        dependency = fmi_dependency_type_t::constant;
    } else if (vstring == "fixed") {
        dependency = fmi_dependency_type_t::fixed;
    } else if (vstring == "tunable") {
        dependency = fmi_dependency_type_t::tunable;
    } else if (vstring == "discrete") {
        dependency = fmi_dependency_type_t::discrete;
    } else if (vstring == "independent") {
        dependency = fmi_dependency_type_t::independent;
    } else {
        dependency = fmi_dependency_type_t::unknown;
    }
}

std::string fmi_dependency_type::to_string() const
{
    switch (dependency) {
        case fmi_dependency_type_t::dependent:
            return "dependent";
        case fmi_dependency_type_t::constant:
            return "constant";
        case fmi_dependency_type_t::fixed:
            return "fixed";
        case fmi_dependency_type_t::tunable:
            return "tunable";
        case fmi_dependency_type_t::discrete:
            return "discrete";
        case fmi_dependency_type_t::independent:
            return "independent";
        default:
            return "unknown";
    }
}
