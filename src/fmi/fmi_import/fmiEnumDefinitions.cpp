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
        variability = FmiVariabilityType::CONTINUOUS;
    } else if (vstring == "constant") {
        variability = FmiVariabilityType::CONSTANT;
    } else if (vstring == "fixed") {
        variability = FmiVariabilityType::FIXED;
    } else if (vstring == "tunable") {
        variability = FmiVariabilityType::TUNABLE;
    } else if (vstring == "discrete") {
        variability = FmiVariabilityType::DISCRETE;
    } else {
        variability = FmiVariabilityType::UNKNOWN;
    }
}

std::string FmiVariability::to_string() const
{
    switch (variability) {
        case FmiVariabilityType::CONTINUOUS:
            return "continuous";
            break;
        case FmiVariabilityType::FIXED:
            return "fixed";
            break;
        case FmiVariabilityType::CONSTANT:
            return "constant";
            break;
        case FmiVariabilityType::DISCRETE:
            return "discrete";
            break;
        case FmiVariabilityType::TUNABLE:
            return "tunable";
            break;
        default:
            return "unknown";
    }
}

FmiCausality::FmiCausality(const std::string& vstring)
{
    if (vstring == "local") {
        causality = FmiCausalityType::LOCAL;
    } else if ((vstring == "parameter") || (vstring == "param")) {
        causality = FmiCausalityType::PARAMETER;
    } else if ((vstring == "calculatedParameter") || (vstring == "calculated")) {
        causality = FmiCausalityType::CALCULATED_PARAMETER;
    } else if ((vstring == "input") || (vstring == "inputs")) {
        causality = FmiCausalityType::INPUT;
    } else if ((vstring == "output") || (vstring == "outputs")) {
        causality = FmiCausalityType::OUTPUT;
    } else if ((vstring == "independent") || (vstring == "time")) {
        causality = FmiCausalityType::INDEPENDENT;
    } else if (vstring == "any") {
        causality = FmiCausalityType::ANY;
    } else {
        causality = FmiCausalityType::UNKNOWN;
    }
}

std::string FmiCausality::to_string() const
{
    switch (causality) {
        case FmiCausalityType::LOCAL:
            return "local";
            break;
        case FmiCausalityType::PARAMETER:
            return "parameter";
            break;
        case FmiCausalityType::CALCULATED_PARAMETER:
            return "calculatedParameter";
            break;
        case FmiCausalityType::INPUT:
            return "input";
            break;
        case FmiCausalityType::OUTPUT:
            return "output";
            break;
        case FmiCausalityType::INDEPENDENT:
            return "independent";
            break;
        case FmiCausalityType::ANY:
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
        variable = FmiVariableType::REAL;
    } else if (vstring == "integer") {
        variable = FmiVariableType::INTEGER;
    } else if (vstring == "boolean") {
        variable = FmiVariableType::BOOLEAN;
    } else if (vstring == "string") {
        variable = FmiVariableType::STRING;
    } else if (vstring == "enumeration") {
        variable = FmiVariableType::ENUMERATION;
    } else {
        variable = FmiVariableType::UNKNOWN;
    }
}

std::string FmiVariableTypeInfo::to_string() const
{
    switch (variable) {
        case FmiVariableType::REAL:
            return "real";
            break;
        case FmiVariableType::INTEGER:
            return "integer";
            break;
        case FmiVariableType::BOOLEAN:
            return "boolean";
            break;
        case FmiVariableType::STRING:
            return "string";
            break;
        case FmiVariableType::ENUMERATION:
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
        dependency = FmiDependencyType::DEPENDENT;
    } else if (vstring == "constant") {
        dependency = FmiDependencyType::CONSTANT;
    } else if (vstring == "fixed") {
        dependency = FmiDependencyType::FIXED;
    } else if (vstring == "tunable") {
        dependency = FmiDependencyType::TUNABLE;
    } else if (vstring == "discrete") {
        dependency = FmiDependencyType::DISCRETE;
    } else if (vstring == "independent") {
        dependency = FmiDependencyType::INDEPENDENT;
    } else {
        dependency = FmiDependencyType::UNKNOWN;
    }
}

std::string FmiDependencyTypeInfo::to_string() const
{
    switch (dependency) {
        case FmiDependencyType::DEPENDENT:
            return "dependent";
        case FmiDependencyType::CONSTANT:
            return "constant";
        case FmiDependencyType::FIXED:
            return "fixed";
        case FmiDependencyType::TUNABLE:
            return "tunable";
        case FmiDependencyType::DISCRETE:
            return "discrete";
        case FmiDependencyType::INDEPENDENT:
            return "independent";
        default:
            return "unknown";
    }
}
