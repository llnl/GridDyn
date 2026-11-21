/*
 * Copyright (c) 2014-2026, Lawrence Livermore National Security
 * See the top-level NOTICE for additional details. All rights reserved.
 * SPDX-License-Identifier: BSD-3-Clause
 */

// libraries

// headers
#include "helicsRunner.h"

#include "CLI11/CLI11.hpp"
#include "coupling/GhostSwingBusManager.h"
#include "fileInput/fileInput.h"
#include "gridDynLoader/libraryLoader.h"
#include "griddyn/GridDynSimulation.h"
#include "helics/helics.hpp"
#include "helicsCoordinator.h"
#include "helicsLibrary.h"
#include "helicsSupport.h"
#include "test/helicsTest.h"
#include <algorithm>
#include <chrono>
#include <cstdio>
#include <exception>
#include <filesystem>
#include <iostream>
#include <memory>
#include <stdexcept>

namespace griddyn::helicsLib {
HelicsRunner::HelicsRunner()
{
    griddyn::loadHelicsLibrary();
    m_gds = std::make_shared<GridDynSimulation>();

    coord_ = makeOwningPtr<HelicsCoordinator>();
    // store the coordinator as a support object so everything can find it
    m_gds->add(coord_.get());
}

HelicsRunner::HelicsRunner(std::shared_ptr<GridDynSimulation> sim): GriddynRunner(sim)
{
    griddyn::loadHelicsLibrary();
    coord_ = makeOwningPtr<HelicsCoordinator>();
    // store the coordinator as a support object so everything can find it
    m_gds->add(coord_.get());
}

HelicsRunner::~HelicsRunner() = default;

std::shared_ptr<CLI::App>
    HelicsRunner::generateLocalCommandLineParser(ReaderInfo& ReaderInformation)
{
    loadHelicsReaderInfoDefinitions(ReaderInformation);

    auto parser =
        std::make_shared<CLI::App>("options related to helics executable", "helics_options");
    // parser->add_flag("--helicstest", test, "run a test of the GridDyn helics interactions");
    parser->allow_extras();
    return parser;
}

void HelicsRunner::simInitialize()
{
    // add an initialization function after the first part of the power flow init
    m_gds->addInitOperation([this]() {
        fed_ = coord_->registerAsFederate();
        return ((fed_) ? 0 : -45);
    });
    int ret = 0;
    ret = m_gds->pFlowInitialize();
    if (ret != 0) {
        throw(ExecutionFailure(m_gds.get(), "power flow initialize failure"));
    }

    GriddynRunner::simInitialize();  // TODO(phlpt): Unpack this for co-iteration on
                                     // the power flow
    // solution
    if (!fed_) {
        throw(ExecutionFailure(m_gds.get(), "unable to initialize helics federate"));
    }
    fed_->enterExecutingMode();
}

CoreTime HelicsRunner::Run()
{
    CoreTime stop_time = m_gds->getStopTime();
    auto retTime = Step(stop_time);
    fed_->finalize();
    return retTime;
}

CoreTime HelicsRunner::Step(CoreTime time)
{
    helics::Time time_granted = 0.0; /* the time step HELICS has allowed us to process */
    helics::Time time_desired = 0.0; /* the time step we would like to go to next */

    while (m_gds->getSimulationTime() < time) {
        auto evntTime = m_gds->getEventTime();
        auto nextTime = std::min(evntTime, time);
        time_desired = gdToHelicsTime(nextTime);
        // printf("nextTime=%f\n", static_cast<double>(nextTime));

        try {
            time_granted = fed_->requestTime(time_desired);
        }
        catch (...) {
            break;
        }
        // printf("grantTime=%llu\n", static_cast<unsigned long long>(time_granted));
        // check if the granted time is too small to do anything about
        if (time_granted < time_desired) {
            if (helicsToGdTime(time_granted) - m_gds->getSimulationTime() < 0.00001) {
                continue;
            }
        }

        try {
            const int status = m_gds->run(helicsToGdTime(time_granted));
            if (status != FUNCTION_EXECUTION_SUCCESS) {
                const auto& error = m_gds->getLastActionError();
                throw std::runtime_error(error.empty() ? "GridDyn failed to run with status " +
                                                 std::to_string(status) :
                                                         error);
            }
        }
        catch (const std::runtime_error& re) {
            std::cerr << "execution error in GridDyn" << re.what() << '\n';
            const int errorCode = m_gds->getErrorCode();
            fed_->localError(errorCode == 0 ? FUNCTION_EXECUTION_FAILURE : errorCode, re.what());

            throw;
        }
    }
    return m_gds->getSimulationTime();
}

void HelicsRunner::Finalize()
{
    fed_->finalize();
}
}  // namespace griddyn::helicsLib
