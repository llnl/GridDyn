/*
 * Copyright (c) 2014-2020, Lawrence Livermore National Security
 * See the top-level NOTICE for additional details. All rights reserved.
 * SPDX-License-Identifier: BSD-3-Clause
 */

#include "CymeDistLoad.h"

#include "core/coreExceptions.h"
#include "core/coreObjectTemplates.hpp"
#include "gmlc/utilities/stringOps.h"
#include "json/reader.h"
#include "json/value.h"
#include <cstdio>
#include <fstream>
#include <iostream>
#include <string>

namespace griddyn {
namespace fmi {
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

    void CymeDistLoadME::set(const std::string& param, const std::string& val)
    {
        if ((param == "config") || (param == "configfile") || (param == "configuration_file")) {
            printf("loading config file %s\n", val.c_str());
            loadConfigFile(val);
        } else {
            printf("setting parameter %s to %s\n", param.c_str(), val.c_str());
            fmiMELoad3phase::set(param, val);
        }
    }

    void CymeDistLoadME::set(const std::string& param, double val, units::unit unitType)
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
            std::cerr << "unable to open the file:" << configFileName << std::endl;
            LOG_WARNING(std::string("unable to open the configuration file ") + configFileName);
            return;
        }
        Json::Value doc;

        Json::CharReaderBuilder rbuilder;
        std::string errs;
        bool ok = Json::parseFromStream(rbuilder, file, &doc, &errs);
        if (!ok) {
            fprintf(stderr, "unable to parse json file %s\n", errs.c_str());
            return;
        }
        configFile = configFileName;
        auto mval = doc["models"];

        // this is ambiguous when configIndex is int64_t. unsigned skips the < 0
        // check, so pass this as signed
        auto model = mval[static_cast<int>(configIndex)];
        if (model.isObject()) {
            auto fmu_path = model["fmu_path"].asString();
            fprintf(stderr, "setting fmu_path to %s\n", fmu_path.c_str());
            fmiMELoad3phase::set("fmu", fmu_path);
            LOG_DEBUG(std::string("setting fmu to ") + model["fmu_path"].asString());

            if (model.isMember("fmu_config_path")) {
                auto config_path = model["fmu_config_path"].asString();
                fprintf(stderr, "fmu config_path=%s\n", config_path.c_str());
                if (config_path.size() > 5) {
                    fmiMELoad3phase::set("_configurationFileName", config_path);
                    LOG_DEBUG(std::string("setting config file to ") + config_path);
                }
            }
        }
    }
}  // namespace fmi
}  // namespace griddyn
