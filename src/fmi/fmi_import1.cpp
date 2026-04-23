/* -*- Mode:C++; c-file-style:"gnu"; indent-tabs-mode:nil;  c-set-offset 'innamespace 0; -*- */
/*
 * Copyright (c) 2014-2026, Lawrence Livermore National Security
 * See the top-level NOTICE for additional details. All rights reserved.
 * SPDX-License-Identifier: BSD-3-Clause
 */

/*
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
#include <JM/jm_portability.h>
#include <cstdarg>
#include <cstdio>
#include <cstdlib>
#include <print>
#include <fmilib.h>

#define BUFFER 1000

/* Logger function used by the FMU internally */
static void fmi1logger(fmi1_component_t c,
                       fmi1_string_t instanceName,
                       fmi1_status_t status,
                       fmi1_string_t category,
                       fmi1_string_t message,
                       ...)
{
    int len;
    char msg[BUFFER];
    va_list argp;
    va_start(argp, message);
    len = vsnprintf(msg, BUFFER, message, argp);
    std::println("fmiStatus = {};  {} ({}): {}", status, instanceName, category, message);
}

int fmi1_test(fmi_import_context_t* context, const char* dirPath)
{
    fmi1_callback_functions_t callBackFunctions;
    const char* modelIdentifier;
    const char* modelName;
    const char* GUID;
    jm_status_enu_t status;

    fmi1_import_t* fmu;

    callBackFunctions.logger = fmi1logger;
    callBackFunctions.allocateMemory = calloc;
    callBackFunctions.freeMemory = free;

    fmu = fmi1_import_parse_xml(context, dirPath);

    if (!fmu) {
        std::println("Error parsing XML, exiting");
        return (CTEST_RETURN_FAIL);
    }
    modelIdentifier = fmi1_import_get_model_identifier(fmu);
    modelName = fmi1_import_get_model_name(fmu);
    GUID = fmi1_import_get_GUID(fmu);

    std::println("Model name: {}", modelName);
    std::println("Model identifier: {}", modelIdentifier);
    std::println("Model GUID: {}", GUID);

    status = fmi1_import_create_dllfmu(fmu, callBackFunctions, 0);
    if (status == jm_status_error) {
        std::println("Could not create the DLL loading mechanism(C-API).");
        return (CTEST_RETURN_FAIL);
    }

    std::println("Version returned from FMU:   {}", fmi1_import_get_version(fmu));

    fmi1_import_destroy_dllfmu(fmu);

    fmi1_import_free(fmu);

    return (CTEST_RETURN_SUCCESS);
}
