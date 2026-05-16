/*
 * Copyright (c) 2014-2026, Lawrence Livermore National Security
 * See the top-level NOTICE for additional details. All rights reserved.
 * SPDX-License-Identifier: BSD-3-Clause
 */

#include "fmiEnumDefinitions.h"

#include "fmiInfo.h"
#include "gmlc/utilities/stringOps.h"
#include <string>

FmiVariability::FmiVariability(const std::string& vstring)
{
    if (vstring == "continuous") {
        variability = FmiVariabilityType::continuous;
    } else if (vstring == "constant") {
        variability = FmiVariabilityType::constant;
    } else if (vstring == "fixed") {
        variability = FmiVariabilityType::fixed;
    } else if (vstring == "tunable") {
        variability = FmiVariabilityType::tunable;
    } else if (vstring == "discrete") {
        variability = FmiVariabilityType::discrete;
    } else {
        variability = FmiVariabilityType::unknown;
    }
}

std::string FmiVariability::to_string() const
{
    switch (variability) {
        case FmiVariabilityType::continuous:
            return "continuous";
            break;
        case FmiVariabilityType::fixed:
            return "fixed";
            break;
        case FmiVariabilityType::constant:
            return "constant";
            break;
        case FmiVariabilityType::discrete:
            return "discrete";
            break;
        case FmiVariabilityType::tunable:
            return "tunable";
            break;
        default:
            return "unknown";
    }
}

FmiCausality::FmiCausality(const std::string& vstring)
{
    if (vstring == "local") {
        causality = FmiCausalityType::local;
    } else if ((vstring == "parameter") || (vstring == "param")) {
        causality = FmiCausalityType::parameter;
    } else if ((vstring == "calculatedParameter") || (vstring == "calculated")) {
        causality = FmiCausalityType::calculatedParameter;
    } else if ((vstring == "input") || (vstring == "inputs")) {
        causality = FmiCausalityType::input;
    } else if ((vstring == "output") || (vstring == "outputs")) {
        causality = FmiCausalityType::output;
    } else if ((vstring == "independent") || (vstring == "time")) {
        causality = FmiCausalityType::independent;
    } else if (vstring == "any") {
        causality = FmiCausalityType::any;
    } else {
        causality = FmiCausalityType::unknown;
    }
}

std::string FmiCausality::to_string() const
{
    switch (causality) {
        case FmiCausalityType::local:
            return "local";
            break;
        case FmiCausalityType::parameter:
            return "parameter";
            break;
        case FmiCausalityType::calculatedParameter:
            return "calculatedParameter";
            break;
        case FmiCausalityType::input:
            return "input";
            break;
        case FmiCausalityType::output:
            return "output";
            break;
        case FmiCausalityType::independent:
            return "independent";
            break;
        case FmiCausalityType::any:
            return "any";
            break;
        default:
            return "unknown";
            break;
    }
}

FmiVariableTypeInfo::FmiVariableTypeInfo(const std::string& vstring)
{
    if (vstring == "real") {
        variable = FmiVariableType::real;
    } else if (vstring == "integer") {
        variable = FmiVariableType::integer;
    } else if (vstring == "boolean") {
        variable = FmiVariableType::boolean;
    } else if (vstring == "string") {
        variable = FmiVariableType::string;
    } else if (vstring == "enumeration") {
        variable = FmiVariableType::enumeration;
    } else {
        variable = FmiVariableType::unknown;
    }
}

std::string FmiVariableTypeInfo::to_string() const
{
    switch (variable) {
        case FmiVariableType::real:
            return "real";
            break;
        case FmiVariableType::integer:
            return "integer";
            break;
        case FmiVariableType::boolean:
            return "boolean";
            break;
        case FmiVariableType::string:
            return "string";
            break;
        case FmiVariableType::enumeration:
            return "enumeration";
            break;
        default:
            return "unknown";
            break;
    }
}

FmiDependencyTypeInfo::FmiDependencyTypeInfo(const std::string& vstring)
{
    if (vstring == "dependent") {
        dependency = FmiDependencyType::dependent;
    } else if (vstring == "constant") {
        dependency = FmiDependencyType::constant;
    } else if (vstring == "fixed") {
        dependency = FmiDependencyType::fixed;
    } else if (vstring == "tunable") {
        dependency = FmiDependencyType::tunable;
    } else if (vstring == "discrete") {
        dependency = FmiDependencyType::discrete;
    } else if (vstring == "independent") {
        dependency = FmiDependencyType::independent;
    } else {
        dependency = FmiDependencyType::unknown;
    }
}

std::string FmiDependencyTypeInfo::to_string() const
{
    switch (dependency) {
        case FmiDependencyType::dependent:
            return "dependent";
        case FmiDependencyType::constant:
            return "constant";
        case FmiDependencyType::fixed:
            return "fixed";
        case FmiDependencyType::tunable:
            return "tunable";
        case FmiDependencyType::discrete:
            return "discrete";
        case FmiDependencyType::independent:
            return "independent";
        default:
            return "unknown";
    }
}
