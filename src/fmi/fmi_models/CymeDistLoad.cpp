/*
 * Copyright (c) 2014-2026, Lawrence Livermore National Security
 * See the top-level NOTICE for additional details. All rights reserved.
 * SPDX-License-Identifier: BSD-3-Clause
 */

#include "CymeDistLoad.h"

#include "core/coreExceptions.h"
#include "core/coreObjectTemplates.hpp"
#include "gmlc/utilities/stringOps.h"
#include "nlohmann/json.hpp"
#include <fstream>
#include <iostream>
#include <print>
#include <string>

namespace griddyn::fmi {
std::atomic<int> CymeDistLoadME::indexCounter{0};

CymeDistLoadME::CymeDistLoadME(const std::string& objName): fmiMELoad3phase(objName)
{
    opFlags.set(current_output);
    configIndex = indexCounter++;
}

coreObject* CymeDistLoadME::clone(coreObject* /* obj */) const
{
    return nullptr;
}

void CymeDistLoadME::set(std::string_view param, std::string_view val)
{
    if ((param == "config") || (param == "configfile") || (param == "configuration_file")) {
        const std::string sval{val};
        std::println("loading config file {}", sval);
        loadConfigFile(sval);
    } else {
        const std::string sparam{param};
        const std::string sval{val};
        std::println("setting parameter {} to {}", sparam, sval);
        fmiMELoad3phase::set(param, val);
    }
}

void CymeDistLoadME::set(std::string_view param, double val, units::unit unitType)
{
    if ((param == "configindex") || (param == "config")) {
        configIndex = static_cast<int>(val);
    } else {
        fmiMELoad3phase::set(param, val, unitType);
    }
}

void CymeDistLoadME::loadConfigFile(const std::string& configFileName)
{
    std::ifstream file(configFileName);
    if (!file.is_open()) {
        std::cerr << "unable to open the file:" << configFileName << '\n';
        logging::warning(this, "unable to open the configuration file {}", configFileName);
        return;
    }
    nlohmann::ordered_json doc;
    std::string errs;
    bool parseSucceeded = true;
    try {
        doc = nlohmann::ordered_json::parse(file);
    } catch (const nlohmann::ordered_json::parse_error& err) {
        errs = err.what();
        parseSucceeded = false;
    }
    if (!parseSucceeded) {
        std::println(stderr, "unable to parse json file {}", errs);
        return;
    }
    configFile = configFileName;
    auto mval = doc["models"];

    // this is ambiguous when configIndex is int64_t. unsigned skips the < 0
    // check, so pass this as signed
    auto model = mval[static_cast<int>(configIndex)];
    if (model.is_object()) {
        auto fmu_path = model["fmu_path"].get<std::string>();
        std::println(stderr, "setting fmu_path to {}", fmu_path);
        fmiMELoad3phase::set("fmu", fmu_path);
        logging::debug(this, "setting fmu to {}", model["fmu_path"].get<std::string>());

        if (model.contains("fmu_config_path")) {
            auto config_path = model["fmu_config_path"].get<std::string>();
            std::println(stderr, "fmu config_path={}", config_path);
            if (config_path.size() > 5) {
                fmiMELoad3phase::set("_configurationFileName", config_path);
                logging::debug(this, "setting config file to {}", config_path);
            }
        }
    }
}
}  // namespace griddyn::fmi
