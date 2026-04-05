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

#include "fmi_importGD.h"

#include <filesystem>
#include <boost/program_options.hpp>
// headers

#include <cstdarg>
#include <cstdio>
#include <cstdlib>
#include <iostream>

#define VERSION_STRING "FMIprocess version 0.1 2015-4-30"
// using namespace boost;
namespace po = boost::program_options;

int argumentParser(int argc, char* argv[], po::variables_map& vm_map);

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
    po::variables_map vm;
    int ret = argumentParser(argc, argv, vm);
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

int argumentParser(int argc, char* argv[], po::variables_map& vm_map)
{
    po::options_description cmd_only("command line only");
    po::options_description config("configuration");
    po::options_description hidden("hidden");

    // input boost controls
    cmd_only.add_options()("help", "produce help message")("config_file",
                                                           po::value<std::string>(),
                                                           "specify a config file to use")(
        "config_file_output",
        po::value<std::string>(),
        "file to store current config options")("version", "print version string");

    config.add_options()("powerflow-output",
                         po::value<std::string>(),
                         "file output for the powerflow solution")(
        "param,P",
        po::value<std::vector<std::string>>(),
        "override simulation file parameters -param ParamName=<val>")(
        "dir,D", po::value<std::vector<std::string>>(), "add search directory for input files");

    hidden.add_options()("input", po::value<std::string>(), "input file");

    po::options_description cmd_line("command line options");
    po::options_description config_file("config file options");
    po::options_description visible("allowed options");

    cmd_line.add(cmd_only).add(config).add(hidden);
    config_file.add(config).add(hidden);
    visible.add(cmd_only).add(config);

    po::positional_options_description p;
    p.add("input", -1);

    po::variables_map cmd_vm;
    po::store(po::command_line_parser(argc, argv).options(cmd_line).positional(p).run(), cmd_vm);
    po::notify(cmd_vm);

    // objects/pointers/variables/constants

    // program options control
    if (cmd_vm.count("help")) {
        std::cout << visible << '\n';
        return 1;
    }

    if (cmd_vm.count("version")) {
        std::cout << VERSION_STRING << '\n';
        return 1;
    }

    po::store(po::command_line_parser(argc, argv).options(cmd_line).positional(p).run(), vm_map);

    std::string config_file_name;
    if (cmd_vm.count("config_file")) {
        config_file_name = cmd_vm["config_file"].as<std::string>();
        if (!std::filesystem::exists(config_file_name)) {
            std::cerr << "config file " << config_file_name << " does not exist\n";
            return -1;
        } else {
            std::ifstream fstr;
            fstr.open(config_file_name.c_str());
            po::store(po::parse_config_file(fstr, config_file), vm_map);
            fstr.close();
        }
    }
    if (std::filesystem::exists("fmiProcess.ini")) {
        std::ifstream fstr;
        fstr.open("fmiProcess.ini");
        po::store(po::parse_config_file(fstr, config_file), vm_map);
        fstr.close();
    }
    po::notify(vm_map);

    return 0;
}
