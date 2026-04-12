/*
/* -*- Mode:C++; c-file-style:"gnu"; indent-tabs-mode:nil;  c-set-offset 'innamespace 0; -*- */
/*
  * LLNS Copyright Start
 * Copyright (c) 2016, Lawrence Livermore National Security
 * This work was performed under the auspices of the U.S. Department
 * of Energy by Lawrence Livermore National Laboratory in part under
 * Contract W-7405-Eng-48 and in part under Contract DE-AC52-07NA27344.
 * Produced at the Lawrence Livermore National Laboratory.
 * All rights reserved.
 * For details, see the LICENSE file.
 * LLNS Copyright End


Copyright (C) 2012 Modelon AB

This program is free software: you can redistribute it and/or modify
it under the terms of the BSD style license.

This program is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
FMILIB_License.txt file for more details.

You should have received a copy of the FMILIB_License.txt file
along with this program. If not, contact Modelon AB <http://www.modelon.com>.
*/

#include "fmi_importGD.h"
#include <FMI2/fmi2_types.h>
#include <JM/jm_portability.h>
#include <cstdarg>
#include <cstdio>
#include <cstdlib>
#include <fmilib.h>
#include <fstream>
#include <iostream>
#include <string>
#include <vector>

#define BUFFER 1000

/* Logger function used by the FMU internally */
static void fmi2logger(fmi2_component_environment_t env,
                       fmi2_string_t instanceName,
                       fmi2_status_t status,
                       fmi2_string_t category,
                       fmi2_string_t message,
                       ...)
{
    if (strcmp(category, "logFmi2Call") == 0) {
        return;
    }
    int len;
    char msg[BUFFER];
    va_list argp;
    va_start(argp, message);
    len = vsnprintf(msg, BUFFER, message, argp);
    printf("fmiStatus = %s;  %s (%s): %s\n",
           fmi2_status_to_string(status),
           instanceName,
           category,
           msg);
}

static void stepFinished(fmi2_component_environment_t env, fmi2_status_t status)
{
    printf("stepFinished is called with fmiStatus = %s\n", fmi2_status_to_string(status));
}

void fmi2_runModel(fmi2_import_t* fmu);

int fmi2_test(fmi_import_context_t* context, const char* dirPath)
{
    fmi2_callback_functions_t callBackFunctions;
    const char* modelIdentifier;
    const char* modelName;
    const char* GUID;
    jm_status_enu_t status;

    fmi2_import_t* fmu;
    fmi2_fmu_kind_enu_t fmukind;

    callBackFunctions.logger = fmi2logger;
    callBackFunctions.allocateMemory = calloc;
    callBackFunctions.freeMemory = free;
    callBackFunctions.stepFinished = stepFinished;
    callBackFunctions.componentEnvironment = 0;
    fmu = fmi2_import_parse_xml(context, dirPath, 0);

    if (!fmu) {
        printf("Error parsing XML, exiting\n");
        return (CTEST_RETURN_FAIL);
    }
    modelName = fmi2_import_get_model_name(fmu);
    GUID = fmi2_import_get_GUID(fmu);
    printf("Model GUID: %s\n", GUID);
    printf("Model name: %s\n", modelName);
    if (fmi2_import_get_fmu_kind(fmu) != fmi2_fmu_kind_cs) {
        modelIdentifier = fmi2_import_get_model_identifier_ME(fmu);
        printf("Model identifier for ME: %s\n", modelIdentifier);
        fmukind = fmi2_fmu_kind_me;
    } else if (fmi2_import_get_fmu_kind(fmu) != fmi2_fmu_kind_me) {
        modelIdentifier = fmi2_import_get_model_identifier_CS(fmu);
        printf("Model identifier for CS: %s\n", modelIdentifier);
        fmukind = fmi2_fmu_kind_cs;
    } else {
        printf("Unexpected FMU kind, exiting\n");
        return (CTEST_RETURN_FAIL);
    }
    auto mod_desc = fmi2_import_get_description(fmu);
    printf("Model description: %s\n", mod_desc);

    auto cs = fmi2_import_get_number_of_continuous_states(fmu);
    auto ev = fmi2_import_get_number_of_event_indicators(fmu);
    std::cout << "model has " << cs << " states and " << ev << " event indicators\n";

    const std::vector<std::string> enuVar{
        "constant", "fixed", "tunable", "discrete", "continuous", "unknown"};
    const std::vector<std::string> enuCaus{
        "param", "calcParam", "input", "output", "local", "ind", "unknown"};
    const std::vector<std::string> enuInit{"exact", "approx", "calc", "unknown"};

    const std::vector<std::string> enuType{"real", "int", "bool", "str", "enum"};

    auto vl = fmi2_import_get_variable_list(fmu, 0);

    auto vsize = fmi2_import_get_variable_list_size(vl);
    fmi2_import_variable_t* iv;
    std::vector<fmi2_value_reference_t> vr;
    for (size_t kk = 0; kk < vsize; ++kk) {
        iv = fmi2_import_get_variable(vl, kk);
        auto name = fmi2_import_get_variable_name(iv);
        auto desc = fmi2_import_get_variable_description(iv);
        printf("variable %d: %s:: %s ", fmi2_import_get_variable_vr(iv), name, desc);

        auto btype = fmi2_import_get_variable_base_type(iv);
        auto vari = fmi2_import_get_variability(iv);
        auto caus = fmi2_import_get_causality(iv);
        auto init = fmi2_import_get_initial(iv);
        std::cout << "||type=" << enuType[btype] << ", " << enuVar[vari] << ", " << enuCaus[caus]
                  << ", INIT=" << enuInit[init] << '\n';
        if (caus == 0) {
            vr.push_back(fmi2_import_get_variable_vr(iv));
        }
    }
    fmi2_import_free_variable_list(vl);

    status = fmi2_import_create_dllfmu(fmu, fmukind, &callBackFunctions);
    if (status == jm_status_error) {
        printf("Could not create the DLL loading mechanism(C-API).\n");
        return (CTEST_RETURN_FAIL);
    }

    printf("Version returned from FMU:   %s\n", fmi2_import_get_version(fmu));

    fmi2_runModel(fmu);
    fmi2_import_destroy_dllfmu(fmu);

    fmi2_import_free(fmu);

    return (CTEST_RETURN_SUCCESS);
}

void fmi2_runModel(fmi2_import_t* fmu)
{
    std::ofstream file;
    file.open("resV.txt");
    std::vector<fmi2_value_reference_t> vr{2, 3};
    std::vector<double> ret(2);
    double state;
    double der_x;
    double time = -6;
    auto status = fmi2_import_instantiate(fmu, "acmotorInstance", fmi2_model_exchange, NULL, 0);

    fmi2_import_enter_initialization_mode(fmu);

    fmi2_import_exit_initialization_mode(fmu);
    fmi2_import_enter_continuous_time_mode(fmu);

    fmi2_import_get_continuous_states(fmu, &state, 1);

    fmi2_import_set_time(fmu, time);
    fmi2_import_get_real(fmu, vr.data(), vr.size(), ret.data());

    double Tend = 9;
    double h = 0.01;
    int sv = 0;
    double aval = 0.95;
    unsigned int aloc = 7;

    fmi2_boolean_t eventMode;
    fmi2_boolean_t terminateSim;
    file << "t, P, Q\n";
    while (time < Tend) {
        // compute derivatives
        fmi2_import_get_derivatives(fmu, &der_x, 1);
        // advance time

        time = time + h;
        fmi2_import_set_time(fmu, time);

        // set inputs at t = time
        if ((time >= 1 - 1e-5) && (sv == 0)) {
            fmi2_import_enter_event_mode(fmu);
            fmi2_import_set_real(fmu, &aloc, 1, &aval);
            fmi2_import_enter_continuous_time_mode(fmu);
            sv = 1;
        } else if ((time >= 6 - 1e-5) && (sv == 1)) {
            aval = 1.0;
            fmi2_import_enter_event_mode(fmu);
            fmi2_import_set_real(fmu, &aloc, 1, &aval);
            fmi2_import_enter_continuous_time_mode(fmu);
            sv = 2;
        }
        // set states at t = time and perform one step
        state = state + h * der_x;  // forward Euler method
        fmi2_import_set_continuous_states(fmu, &state, 1);
        // get event indicators at t = time

        fmi2_import_completed_integrator_step(fmu, false, &eventMode, &terminateSim);
        fmi2_import_get_real(fmu, vr.data(), vr.size(), ret.data());
        file << time << ", " << ret[1] << ", " << ret[0] << '\n';
    }
    // terminate simulation and
    file.close();
    fmi2_import_free_instance(fmu);
}
