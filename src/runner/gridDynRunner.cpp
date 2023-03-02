/*
 * Copyright (c) 2014-2020, Lawrence Livermore National Security
 * See the top-level NOTICE for additional details. All rights reserved.
 * SPDX-License-Identifier: BSD-3-Clause
 */

#include "gridDynRunner.h"

#include "CLI11/CLI11.hpp"
#include "core/coreExceptions.h"
#include "core/objectInterpreter.h"
#include "coupling/GhostSwingBusManager.h"
#include "fileInput/fileInput.h"
#include "gmlc/utilities/stringOps.h"
#include "griddyn/events/Event.h"
#include "griddyn/gridDynSimulation.h"
#include "griddyn/measurement/Recorder.h"
#include "griddyn/simulation/gridDynSimulationFileOps.h"
#include "utilities/GlobalWorkQueue.hpp"
#include <chrono>
#include <cstdio>
#include <iomanip>
#include <iostream>
#include <memory>

#include <boost/filesystem.hpp>

namespace filesystem = boost::filesystem;

namespace griddyn {
using namespace gmlc::utilities::stringOps;

GriddynRunner::GriddynRunner() = default;

GriddynRunner::GriddynRunner(std::shared_ptr<gridDynSimulation> sim): m_gds(std::move(sim)) {}

GriddynRunner::~GriddynRunner() = default;

int GriddynRunner::InitializeFromString(const std::string& cmdargs)
{
    if (!m_gds) {
        m_gds = std::make_shared<gridDynSimulation>();
        // gridDynSimulation::setInstance(m_gds.get());  // peer to gridDynSimulation::GetInstance
        // ();
    }
    m_gds->log(nullptr, print_level::summary, "GridDyn version:" GRIDDYN_VERSION_STRING);

    argv_vals = nullptr;
    argc_val = 0;
    arg_string = cmdargs;
    readerInfo ri;
    auto ret = loadCommandArgument(ri, false);
    if (ret != FUNCTION_EXECUTION_SUCCESS) {
        return ret;
    }

    int areas = m_gds->getInt("totalareacount");
    int buses = m_gds->getInt("totalbuscount");
    int links = m_gds->getInt("totallinkcount");
    int relays = m_gds->getInt("totalrelaycount");
    int gens = m_gds->getInt("gencount");
    int loads = m_gds->getInt("loadcount");
    std::cout << "areas=" << areas << " buses=" << buses << " links=" << links
              << " relays=" << relays << " gens=" << gens << " loads=" << loads << '\n';

    // set any flags used by the system

    return FUNCTION_EXECUTION_SUCCESS;
}

int GriddynRunner::Initialize(int argc, char* argv[], readerInfo& ri, bool allowUnrecognized)
{
    if (!m_gds) {
        m_gds = std::make_shared<gridDynSimulation>();
        // gridDynSimulation::setInstance(m_gds.get());  // peer to gridDynSimulation::GetInstance
        // ();
    }
    m_gds->log(nullptr, print_level::summary, "GridDyn version:" GRIDDYN_VERSION_STRING);
    // TODO:: do something different with this
    GhostSwingBusManager::initialize(&argc, &argv);

    argv_vals = argv;
    argc_val = argc;

    auto ret = loadCommandArgument(ri, allowUnrecognized);
    if (ret != FUNCTION_EXECUTION_SUCCESS) {
        return ret;
    }

    int areas = m_gds->getInt("totalareacount");
    int buses = m_gds->getInt("totalbuscount");
    int links = m_gds->getInt("totallinkcount");
    int relays = m_gds->getInt("totalrelaycount");
    int gens = m_gds->getInt("gencount");
    int loads = m_gds->getInt("loadcount");
    std::cout << "areas=" << areas << " buses=" << buses << " links=" << links
              << " relays=" << relays << " gens=" << gens << " loads=" << loads << '\n';

    // set any flags used by the system

    return FUNCTION_EXECUTION_SUCCESS;
}

int GriddynRunner::Initialize(int argc, char* argv[], bool allowUnrecognized)
{
    readerInfo ri;
    return Initialize(argc, argv, ri, allowUnrecognized);
}

void GriddynRunner::simInitialize()
{
    m_startTime = std::chrono::high_resolution_clock::now();
    m_gds->dynInitialize();
    if (!(m_gds->hasDynamics())) {
        eventMode = true;
    }
}

int GriddynRunner::Reset()
{
    if (!isReady()) {
        throw(executionFailure(m_gds.get(), "asynchronous operation ongoing"));
    }
    readerInfo ri;
    return Reset(ri);
}

int GriddynRunner::Reset(readerInfo& ri)
{
    if (!isReady()) {
        throw(executionFailure(m_gds.get(), "asynchronous operation ongoing"));
    }
    // make a new simulation object
    m_gds = std::make_shared<gridDynSimulation>();
    // reload it from the existing vm
    auto ret = loadCommandArgument(ri, false);
    if (ret != FUNCTION_EXECUTION_SUCCESS) {
        return (ret);
    }
    m_gds->log(m_gds.get(),
               print_level::normal,
               std::string("\nsystem reset of ") + m_gds->getName());
    return FUNCTION_EXECUTION_SUCCESS;
}

coreTime GriddynRunner::Run()
{
    if (!isReady()) {
        throw(executionFailure(m_gds.get(), "asynchronous operation ongoing"));
    }

    m_startTime = std::chrono::high_resolution_clock::now();
    m_gds->run();
    m_stopTime = std::chrono::high_resolution_clock::now();
    std::chrono::duration<double> elapsed_t = m_stopTime - m_startTime;
    m_gds->log(m_gds.get(),
               print_level::summary,
               m_gds->getName() + " executed in " + std::to_string(elapsed_t.count()) + " seconds");
    return m_gds->getSimulationTime();
}

void GriddynRunner::RunAsync()
{
    if (!isReady()) {
        throw(executionFailure(m_gds.get(), "asynchronous operation ongoing"));
    }
    async_ret = std::async(std::launch::async, [this] { return Run(); });
}

coreTime GriddynRunner::Step(coreTime time)
{
    if (!isReady()) {
        throw(executionFailure(m_gds.get(), "asynchronous operation ongoing"));
    }
    coreTime actual = time;
    if (m_gds) {
        if (eventMode) {
            int retval = m_gds->eventDrivenPowerflow(time);
            actual = time;
            if (retval < FUNCTION_EXECUTION_SUCCESS) {
                std::string error = "GridDyn failed to advance retval = " + std::to_string(retval);
                throw(std::runtime_error(error));
            }
        } else {
            int retval = m_gds->step(time, actual);
            if (retval < FUNCTION_EXECUTION_SUCCESS) {
                std::string error = "GridDyn failed to advance retval = " + std::to_string(retval);
                throw(std::runtime_error(error));
            }
        }
    }

    return actual;
}

void GriddynRunner::StepAsync(coreTime time)
{
    if (!isReady()) {
        throw(executionFailure(m_gds.get(), "asynchronous operation ongoing"));
    }
    async_ret = std::async(std::launch::async, [this, time] { return Step(time); });
}

std::shared_ptr<gridDynSimulation>& GriddynRunner::getSim()
{
    if (!m_gds) {
        m_gds = std::make_shared<gridDynSimulation>();
    }
    return m_gds;
}

bool GriddynRunner::isReady() const
{
    return (async_ret.valid()) ?
        (async_ret.wait_for(std::chrono::seconds(0)) == std::future_status::ready) :
        true;
}

int GriddynRunner::getStatus(coreTime& timeReturn)
{
    timeReturn = m_gds->getSimulationTime();
    return (isReady()) ? static_cast<int>(m_gds->currentProcessState()) : GRIDDYN_PENDING;
}

coreTime GriddynRunner::getNextEvent() const
{
    return m_gds->getEventTime();
}
void GriddynRunner::StopRecording()
{
    m_gds->log(m_gds.get(), print_level::normal, "Saving recorders...");
    m_gds->saveRecorders();
    m_stopTime = std::chrono::high_resolution_clock::now();
    std::chrono::duration<double> elapsed_t = m_stopTime - m_startTime;
    m_gds->log(m_gds.get(),
               print_level::normal,
               std::string("\nSimulation ") + m_gds->getName() + " executed in " +
                   std::to_string(elapsed_t.count()) + " seconds");
}

void GriddynRunner::Finalize()
{
    StopRecording();
    GhostSwingBusManager::instance()->endSimulation();
}

int GriddynRunner::loadCommandArgument(readerInfo& ri, bool allowUnrecognized)
{
    m_startTime = std::chrono::high_resolution_clock::now();
    auto app = generateBaseCommandLineParser(ri);
    app->allow_extras(allowUnrecognized);
    auto sub_app = generateLocalCommandLineParser(ri);
    if (sub_app) {
        app->add_subcommand(std::move(sub_app));
    }
    try {
        if (argc_val > 0) {
            execPath = argv_vals[0];
            app->parse(argc_val, argv_vals);
        } else {
            app->parse(arg_string);
        }
    }
    catch (const CLI::CallForHelp& e) {
        app->exit(e);
        return 1;
    }
    catch (const CLI::CallForAllHelp& e) {
        app->exit(e);
        return 1;
    }
    catch (const CLI::Success&) {
        return 1;
    }
    catch (const CLI::Error& e) {
        return e.get_exit_code();
    }
    m_stopTime = std::chrono::high_resolution_clock::now();

    std::chrono::duration<double> elapsed_t = m_stopTime - m_startTime;
    m_gds->log(m_gds.get(),
               print_level::normal,
               std::string("\nInitialization ") + m_gds->getName() + " executed in " +
                   std::to_string(elapsed_t.count()) + " seconds");
    return FUNCTION_EXECUTION_SUCCESS;
}

std::shared_ptr<CLI::App> GriddynRunner::generateBaseCommandLineParser(readerInfo& ri)
{
    auto defineTransform = [&ri](const std::string& input) { return ri.checkDefines(input); };

    // function for loading parameters from strings
    CLI::callback_t loadParamString = [this](CLI::results_t results) {
        for (auto& str : results) {
            gridParameter p(str);
            if (p.valid) {
                objInfo oi(p.field, m_gds.get());
                try {
                    if (p.stringType) {
                        oi.m_obj->set(oi.m_field, p.strVal);
                    } else {
                        oi.m_obj->set(oi.m_field, p.value, p.paramUnits);
                    }
                }
                catch (const unrecognizedParameter&) {
                    return false;
                }
            }
        }
        return true;
    };

    // function for loading parameters from strings
    CLI::callback_t loadFileCallback = [this, &ri](CLI::results_t results) {
        for (auto& file : results) {
            loadFile(m_gds.get(), file, &ri);
            if (m_gds->getErrorCode() != 0) {
                throw(CLI::Error(m_gds->getName(),
                                 "Error loading File " + file,
                                 m_gds->getErrorCode()));
            }
        }
        return true;
    };

    auto ptr =
        std::make_shared<CLI::App>("Command line application for GridDyn Power System Simulation");
    ptr->set_help_flag("--help,-h,-?", "generate help message");
    ptr->set_config("--config-file", "gridDynConfig.ini", "read a config INI file");
    ptr->add_flag_callback("--mpicount",
                           []() {
                               readerConfig::setPrintMode(READER_NO_PRINT);
                               readerConfig::setWarnMode(READER_WARN_NONE);
                           })
        ->group("");
    ptr->add_flag_callback("--version", []() {
        std::cout << GRIDDYN_VERSION_STRING << '\n';
        throw(CLI::Success());
    });
    ptr->add_flag("--test", "Execute a test program");
    ptr->add_option_function<std::string>(
           "--powerflow-output,-o,--power_flow_output",
           [this](const std::string& input) { m_gds->set("powerflowfile", input); },
           "file output for the powerflow solution")
        ->ignore_case()
        ->ignore_underscore()
        ->transform(defineTransform);

    ptr->add_option_function<std::string>(
        "--power_flow_input_data",
        [this](const std::string& input) { m_gds->set("powerflowinputfile", input); },
        "file output for the power flow data prior to solving")
        ->ignore_case()
        ->ignore_underscore()
        ->transform(defineTransform);

    ptr->add_flag_callback(
           "--powerflow_only,--powerflow-only",
           [this]() { m_gds->setFlag("powerflow_only", true); },
           "set the solver to stop after the power flow solution and use some powerflow specific models")
        ->ignore_case()
        ->ignore_underscore();

    // Setup for saving the state

    auto savestateGroup =
        ptr->add_option_group("ssgroup", "options related to saving the system state");
    auto ssdata = std::make_shared<std::pair<int, std::string>>(0, std::string{});

    auto so =
        savestateGroup
            ->add_option("--state-output,--state_output,--state_output_file,--state-output-file",
                         ssdata->second,
                         "file for final output state")
            ->ignore_case()
            ->transform(defineTransform)
            ->ignore_underscore();
    savestateGroup
        ->add_option("--save-state-period,--save_state_period",
                     ssdata->first,
                     "save state every N ms, -1 for saving only at the end")
        ->ignore_case()
        ->ignore_underscore()
        ->needs(so);
    savestateGroup
        ->add_option_function<std::string>(
            "--jac_state,--jac_state_file",
            [this](const std::string& jac_file) {
                captureJacState(m_gds.get(), jac_file, m_gds->getSolverMode("pflow"));
            },
            "save the jacobian values of the power flow solution to a file")
        ->transform(defineTransform);

    // group callback to setup state file
    savestateGroup->callback([this, ssdata]() {
        m_gds->set("statefile", ssdata->second);
        if (ssdata->first > 0) {
            m_gds->set("state_record_period", ssdata->first);
        }
    });

    ptr->add_option_function<std::string>(
           "--log-file,--log_file,--log",
           [this](const std::string& file) { m_gds->set("logfile", file); },
           "log file output")
        ->transform(defineTransform)
        ->ignore_case()
        ->ignore_underscore();
    //  ptr->add_option ("--jac-output", po::value<std::string> (), "powerflow Jacobian file
    //  output");
    ptr->add_flag_function(
           "--verbose{3},-v{3},--quiet{0},-q{0},--printlevel{2},--summary{1}",
           [this](int64_t val) {
               readerConfig::setPrintMode(static_cast<int>(val));
               m_gds->set("printlevel", static_cast<double>(val));
           },
           "specify print output verbosity")
        ->transform(
            CLI::Transformer({{"verbose", "3"}, {"normal", "2"}, {"summary", "1"}, {"none", "0"}},
                             CLI::ignore_case))
        ->transform(CLI::Bound(0, 3));

    ptr->add_option("--flags,-f", "specify flags to feed to GridDyn")
        ->delimiter(',')
        ->type_size(-1)
        ->each([this](const std::string& val) {
            try {
                setMultipleFlags(m_gds.get(), val);
            }
            catch (const unrecognizedParameter&) {
                throw CLI::ValidationError("flag " + val + " not recognized");
            }
        });

    ptr->add_flag_function(
           "--warn{1},-w{1},--warn-all{2},--no-warn{0},--warn-important{1}",
           [](int64_t val) { readerConfig::setWarnMode(static_cast<int>(val)); },
           "specify warning level output")
        ->disable_flag_override()
        ->multi_option_policy(CLI::MultiOptionPolicy::TakeLast);
    ptr->add_option_function<int>(
           "--threads,-j",
           [](int val) { griddyn::getGlobalWorkQueue(val); },
           "specify the number of worker threads to use if multithreading is enabled")
        ->check(CLI::PositiveNumber);

    // Reader group should run before the main files are processed
    auto readerGroup =
        ptr->add_option_group("reader_flags",
                              "Flags and options impacting the file parser and reader")
            ->immediate_callback();
    readerGroup->add_option("--file-flags", "specify flags to feed to the file reader")
        ->delimiter(',')
        ->type_size(-1)
        ->each([&ri](const std::string& flag) { addflags(ri, flag); });
    readerGroup->add_option("--define,-D", "definition strings for the element file readers")
        ->type_size(-1)
        ->each([&ri](const std::string& defstr) {
            auto N = defstr.find_first_of('=');
            auto def = trim(defstr.substr(0, N));
            auto rep = trim(defstr.substr(N + 1));
            ri.addLockedDefinition(def, rep);
        });
    readerGroup->add_option("--translate,-T", "translation strings for the element file readers")
        ->type_size(-1)
        ->each([&ri](const std::string& transstr) {
            auto N = transstr.find_first_of('=');
            auto tran = trim(transstr.substr(0, N));
            auto rep = trim(transstr.substr(N + 1));
            ri.addTranslate(tran, rep);
        });

    readerGroup
        ->add_option_function<std::string>("--xml",
                                           readerConfig::setDefaultXMLReader,
                                           "the xml reader to use: 1 for tinyxml, 2 for tinyxml2")
        ->transform(CLI::CheckedTransformer({{"tinyxml", "1"}, {"tinyxml2", "2"}},
                                            CLI::ignore_underscore,
                                            CLI::ignore_case));

    readerGroup
        ->add_option_function<std::string>("--match-type",
                                           readerConfig::setDefaultMatchType,
                                           "the default parameter name matching algorithm to use ")
        ->transform(
            CLI::IsMember({"strict", "exact", "capital", "caps", "any", "all"}, CLI::ignore_case));

    readerGroup
        ->add_option_function<std::vector<std::string>>(
            "--dir",
            [&ri](const std::vector<std::string>& dirList) {
                for (const auto& dirname : dirList) {
                    ri.addDirectory(dirname);
                }
            },
            "add search directory for input files")
        ->check(CLI::ExistingPath)
        ->delimiter(',');

    ptr->add_option("input,--input", loadFileCallback, "main input file")
        ->check(CLI::ExistingFile)
        ->required()
        ->type_size(-1);
    ptr->add_option("--import,-i",
                    loadFileCallback,
                    "add import files loaded after the main input file")
        ->check(CLI::ExistingFile)
        ->type_size(-1);

    ptr->add_option("--param,-P",
                    loadParamString,
                    "override simulation file parameters --param ParamName=<val>")
        ->delimiter(',');

    ptr->add_option("--event", "add event after all input files")
        ->type_size(-1)
        ->each([this](const std::string& event) {
            EventInfo gdEI;
            gdEI.loadString(event, m_gds->getRoot());
            std::shared_ptr<Event> gdE = make_event(gdEI, m_gds->getRoot());
            m_gds->add(std::move(gdE));
        });

    // Setup for automatic data capture
    auto acdata = std::make_shared<std::tuple<double, std::string, std::vector<std::string>>>();

    std::get<0>(*acdata) = 0.0;
    std::get<1>(*acdata) = "auto_capture.bin";
    std::get<2>(*acdata).push_back("auto");

    auto acGroup =
        ptr->add_option_group("acgroup", "options related to automatic variable capture");
    acGroup->option_defaults()->ignore_case()->ignore_underscore();
    auto acp = acGroup->add_option("--auto-capture-period,--auto_capture_period",
                                   std::get<0>(*acdata),
                                   "period to capture the automatic recording");
    acGroup
        ->add_option("--auto-capture,--auto_capture,--auto_capture_file,--auto-capture-file",
                     std::get<1>(*acdata),
                     "file for automatic recording",
                     true)
        ->needs(acp);
    acGroup
        ->add_option(
            "--auto-capture-field,--auto-capture-fields,--auto_capture_field,--auto_capture_fields",
            std::get<2>(*acdata),
            "fields to automatically capture",
            true)
        ->delimiter(',')
        ->needs(acp);

    acGroup->callback([this, acdata]() {
        auto autorec = std::make_shared<Recorder>();
        autorec->set("file", std::get<1>(*acdata));
        autorec->set("period", std::get<0>(*acdata));
        for (auto& field : std::get<2>(*acdata))
            autorec->add(field, m_gds.get());
    });
    return ptr;
}

std::shared_ptr<CLI::App> GriddynRunner::generateLocalCommandLineParser(readerInfo&)
{
    return nullptr;
}

}  // namespace griddyn
