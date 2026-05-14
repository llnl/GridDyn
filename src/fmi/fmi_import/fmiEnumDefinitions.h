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

enum class fmi_variability_type_t {
    continuous = 0,
    constant,
    fixed,
    tunable,
    discrete,
    unknown,
};
/** class wrapper for fmi variability
 */
class fmi_variability {
  public:
    fmi_variability_type_t variability;  //!< variability data member

    fmi_variability(const std::string& vstring);
    fmi_variability(fmi_variability_type_t type): variability(type) {}
    std::string to_string() const;
    bool operator==(fmi_variability_type_t type) const { return (variability == type); }
    bool operator==(const fmi_variability v) const { return (v.variability == variability); }
    fmi_variability_type_t value() const { return variability; }
};

enum class fmi_causality_type_t {
    local,
    parameter,
    calculatedParameter,
    input,
    output,
    independent,
    unknown,
    any,
};

class fmi_causality {
  public:
    fmi_causality_type_t causality;  //!< causality data member

    fmi_causality(const std::string& vstring);
    fmi_causality(fmi_causality_type_t type): causality(type) {}
    std::string to_string() const;
    bool operator==(fmi_causality_type_t type) const { return (causality == type); }
    bool operator==(const fmi_causality v) const { return (v.causality == causality); }
    fmi_causality_type_t value() const { return causality; }
};

enum class fmi_variable_type_t {
    real = 0,
    integer,
    boolean,
    string,
    enumeration,
    unknown,
    numeric,  //!< not used directly in an fmu but intended to catch all numeric in search
              //!< operations
};

class fmi_variable_type {
  public:
    fmi_variable_type_t variable = fmi_variable_type_t::real;  //!< variable type data member
    fmi_variable_type() {}
    fmi_variable_type(const std::string& vstring);
    fmi_variable_type(fmi_variable_type_t type): variable(type) {}
    std::string to_string() const;
    bool operator==(fmi_variable_type_t type) const { return (variable == type); }
    bool operator!=(fmi_variable_type_t type) const { return (variable != type); }
    bool operator==(const fmi_variable_type v) const { return (v.variable == variable); }
    fmi_variable_type_t value() const { return variable; }
};

enum class fmi_dependency_type_t {
    dependent = 0,
    constant,
    fixed,
    tunable,
    discrete,
    independent,
    unknown,
};

class fmi_dependency_type {
  public:
    fmi_dependency_type_t dependency =
        fmi_dependency_type_t::dependent;  //!< dependency data member
    fmi_dependency_type() {}
    fmi_dependency_type(const std::string& vstring);
    fmi_dependency_type(fmi_dependency_type_t type): dependency(type) {}
    std::string to_string() const;
    bool operator==(fmi_dependency_type_t type) const { return (dependency == type); }
    bool operator==(const fmi_dependency_type v) const { return (v.dependency == dependency); }
    fmi_dependency_type_t value() const { return dependency; }
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
