/*
 * Copyright (c) 2014-2026, Lawrence Livermore National Security
 * See the top-level NOTICE for additional details. All rights reserved.
 * SPDX-License-Identifier: BSD-3-Clause
 */

#pragma once

#include <string>

/** enumeration of the known fmu types*/
enum class FmuType {
    UNKNOWN,  //!< unknown fmu type
    MODEL_EXCHANGE,  //!< fmi for model exchange
    COSIMULATION,  //!< fmi for cosimulation
};

enum class FmiVariabilityType {
    continuous = 0,
    constant,
    fixed,
    tunable,
    discrete,
    unknown,
};
/** class wrapper for fmi variability
 */
class FmiVariability {
  public:
    FmiVariabilityType variability;  //!< variability data member

    FmiVariability(const std::string& vstring);
    FmiVariability(FmiVariabilityType type): variability(type) {}
    std::string to_string() const;
    bool operator==(FmiVariabilityType type) const { return (variability == type); }
    bool operator==(const FmiVariability v) const { return (v.variability == variability); }
    FmiVariabilityType value() const { return variability; }
};

enum class FmiCausalityType {
    local,
    parameter,
    calculatedParameter,
    input,
    output,
    independent,
    unknown,
    any,
};

class FmiCausality {
  public:
    FmiCausalityType causality;  //!< causality data member

    FmiCausality(const std::string& vstring);
    FmiCausality(FmiCausalityType type): causality(type) {}
    std::string to_string() const;
    bool operator==(FmiCausalityType type) const { return (causality == type); }
    bool operator==(const FmiCausality v) const { return (v.causality == causality); }
    FmiCausalityType value() const { return causality; }
};

enum class FmiVariableType {
    real = 0,
    integer,
    boolean,
    string,
    enumeration,
    unknown,
    numeric,  //!< not used directly in an fmu but intended to catch all numeric in search
              //!< operations
};

class FmiVariableTypeInfo {
  public:
    FmiVariableType variable = FmiVariableType::real;  //!< variable type data member
    FmiVariableTypeInfo() {}
    FmiVariableTypeInfo(const std::string& vstring);
    FmiVariableTypeInfo(FmiVariableType type): variable(type) {}
    std::string to_string() const;
    bool operator==(FmiVariableType type) const { return (variable == type); }
    bool operator!=(FmiVariableType type) const { return (variable != type); }
    bool operator==(const FmiVariableTypeInfo v) const { return (v.variable == variable); }
    FmiVariableType value() const { return variable; }
};

enum class FmiDependencyType {
    dependent = 0,
    constant,
    fixed,
    tunable,
    discrete,
    independent,
    unknown,
};

class FmiDependencyTypeInfo {
  public:
    FmiDependencyType dependency = FmiDependencyType::dependent;  //!< dependency data member
    FmiDependencyTypeInfo() {}
    FmiDependencyTypeInfo(const std::string& vstring);
    FmiDependencyTypeInfo(FmiDependencyType type): dependency(type) {}
    std::string to_string() const;
    bool operator==(FmiDependencyType type) const { return (dependency == type); }
    bool operator==(const FmiDependencyTypeInfo v) const { return (v.dependency == dependency); }
    FmiDependencyType value() const { return dependency; }
};

enum FmuCapabilityFlags : int {
    MODEL_EXCHANGE_CAPABLE,
    CO_SIMULATION_CAPABLE,
    CAN_GET_AND_SET_FMU_STATE,
    PROVIDES_DIRECTIONAL_DERIVATIVE,
    CAN_SERIALIZE_FMU_STATE,
    NEEDS_EXECUTION_TOOL,
    COMPLETED_INTEGRATOR_STEP_NOT_NEEDED,
    CAN_HANDLE_VARIABLE_COMMUNICATION_STEP_SIZE,
    CAN_INTERPOLATE_INPUTS,
    CAN_RUN_ASYNCHRONOUSLY,
    CAN_BE_INSTANTIATED_ONLY_ONCE_PER_PROCESS,
    CAN_NOT_USE_MEMORY_MANAGEMENT_FUNCTIONS,

};
