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
 */

// libraries

#include "CLI11/CLI11.hpp"
#include "fmi_importGD.h"
#include <filesystem>
// headers

#include <cstdarg>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <iostream>

#define VERSION_STRING "FMIprocess version 0.1 2015-4-30"

namespace {
struct FmiProcessOptions {
    std::string configFile;
    std::string configFileOutput;
    std::string powerflowOutput;
    std::vector<std::string> params;
    std::vector<std::string> directories;
    std::string input;
};

int argumentParser(int argc, char* argv[], FmiProcessOptions& options)
{
    CLI::App app{"FMI process command line utility"};
    app.set_help_flag("--help,-h,-?", "produce help message");
    app.add_flag_callback("--version", []() {
        std::cout << VERSION_STRING << '\n';
        throw CLI::Success();
    });
    app.set_config("--config_file", "", "specify a config file to use");
    app.add_option("--config_file_output",
                   options.configFileOutput,
                   "file to store current config options");
    app.add_option("--powerflow-output",
                   options.powerflowOutput,
                   "file output for the powerflow solution");
    app.add_option("--param,-P",
                   options.params,
                   "override simulation file parameters -param ParamName=<val>");
    app.add_option("--dir,-D", options.directories, "add search directory for input files");
    app.add_option("input", options.input, "input file");

    std::vector<std::string> args;
    args.reserve(static_cast<size_t>(std::max(argc - 1, 0)) + 2);
    bool hasExplicitConfigFile = false;
    for (int ii = 1; ii < argc; ++ii) {
        std::string_view arg{argv[ii]};
        if ((arg == "--config_file") || (arg == "--config-file") ||
            (arg.rfind("--config_file=", 0) == 0) || (arg.rfind("--config-file=", 0) == 0)) {
            hasExplicitConfigFile = true;
        }
        args.emplace_back(argv[ii]);
    }
    if ((!hasExplicitConfigFile) && std::filesystem::exists("fmiProcess.ini")) {
        args.emplace_back("--config_file");
        args.emplace_back("fmiProcess.ini");
    }

    try {
        app.parse(args);
    }
    catch (const CLI::ParseError& e) {
        return app.exit(e);
    }

    options.configFile = app.get_config_ptr()->as<std::string>();
    if ((!options.configFile.empty()) && (!std::filesystem::exists(options.configFile))) {
        std::cerr << "config file " << options.configFile << " does not exist\n";
        return -1;
    }

    if (!options.configFileOutput.empty()) {
        std::ofstream out(options.configFileOutput.c_str());
        out << app.config_to_str(true, true);
    }

    return 0;
}
}  // namespace

void importlogger(jm_callbacks* c,
                  jm_string module,
                  jm_log_level_enu_t log_level,
                  jm_string message)
{
    printf("module = %s, log level = %d: %s\n", module, log_level, message);
}

// main
int main(int argc, char* argv[])
{
    FmiProcessOptions options;
    int ret = argumentParser(argc, argv, options);
    if (ret) {
        return ret;
    }
    jm_callbacks callbacks;
    fmi_import_context_t* context;
    fmi_version_enu_t version;

    callbacks.malloc = malloc;
    callbacks.calloc = calloc;
    callbacks.realloc = realloc;
    callbacks.free = free;
    callbacks.logger = importlogger;
    callbacks.log_level = jm_log_level_warning;
    callbacks.context = 0;

    context = fmi_import_allocate_context(&callbacks);

    std::string FMUPath =
        "E:/My_Documents/Code_projects/transmission_git/fmi/fmu_objects/extractFMU/ACMotorFMU.fmu";
    std::string extractPath =
        "E:/My_Documents/Code_projects/transmission_git/fmi/fmu_objects/extractFMU/ACMotorFMU";
    version = fmi_import_get_fmi_version(context, FMUPath.c_str(), extractPath.c_str());

    if (version == fmi_version_1_enu) {
        ret = fmi1_test(context, extractPath.c_str());
    } else if (version == fmi_version_2_0_enu) {
        ret = fmi2_test(context, extractPath.c_str());
    } else {
        fmi_import_free_context(context);
        printf("Only versions 1.0 and 2.0 are supported so far\n");
        return (CTEST_RETURN_FAIL);
    }

    fmi_import_free_context(context);

    return 0;
}
