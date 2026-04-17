/*
 * LLNS Copyright Start
 * Copyright (c) 2014-2018, Lawrence Livermore National Security
 * This work was performed under the auspices of the U.S. Department
 * of Energy by Lawrence Livermore National Laboratory in part under
 * Contract W-7405-Eng-48 and in part under Contract DE-AC52-07NA27344.
 * Produced at the Lawrence Livermore National Laboratory.
 * All rights reserved.
 * For details, see the LICENSE file.
 * LLNS Copyright End
 */

// libraries

// headers
#include "helicsRunner.h"

#include "CLI11/CLI11.hpp"
#include "coupling/GhostSwingBusManager.h"
#include "fileInput/fileInput.h"
#include "gridDynLoader/libraryLoader.h"
#include "griddyn/gridDynSimulation.h"
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

namespace griddyn::helicsLib {
    helicsRunner::helicsRunner()
    {
        griddyn::loadHELICSLibrary();
        m_gds = std::make_shared<gridDynSimulation>();

        coord_ = make_owningPtr<helicsCoordinator>();
        // store the coordinator as a support object so everything can find it
        m_gds->add(coord_.get());
    }

    helicsRunner::helicsRunner(std::shared_ptr<gridDynSimulation> sim): GriddynRunner(sim)
    {
        griddyn::loadHELICSLibrary();
        coord_ = make_owningPtr<helicsCoordinator>();
        // store the coordinator as a support object so everything can find it
        m_gds->add(coord_.get());
    }

    helicsRunner::~helicsRunner() = default;

    std::shared_ptr<CLI::App> helicsRunner::generateLocalCommandLineParser(readerInfo& ri)
    {
        loadHelicsReaderInfoDefinitions(ri);

        auto parser =
            std::make_shared<CLI::App>("options related to helics executable", "helics_options");
        // parser->add_flag("--helicstest", test, "run a test of the GridDyn helics interactions");
        parser->allow_extras();
        return parser;
    }

    void helicsRunner::simInitialize()
    {
        // add an initialization function after the first part of the power flow init
        m_gds->addInitOperation([this]() {
            fed_ = coord_->RegisterAsFederate();
            return ((fed_) ? 0 : -45);
        });
        int ret = 0;
        ret = m_gds->pFlowInitialize();
        if (ret != 0) {
            throw(executionFailure(m_gds.get(), "power flow initialize failure"));
        }

        GriddynRunner::simInitialize();  // TODO(phlpt): Unpack this for co-iteration on
                                         // the power flow
        // solution
        if (!fed_) {
            throw(executionFailure(m_gds.get(), "unable to initialize helics federate"));
        }
        fed_->enterExecutingMode();
    }

    coreTime helicsRunner::Run()
    {
        coreTime stop_time = m_gds->getStopTime();
        auto retTime = Step(stop_time);
        fed_->finalize();
        return retTime;
    }

    coreTime helicsRunner::Step(coreTime time)
    {
        helics::Time time_granted = 0.0; /* the time step HELICS has allowed us to process */
        helics::Time time_desired = 0.0; /* the time step we would like to go to next */

        while (m_gds->getSimulationTime() < time) {
            auto evntTime = m_gds->getEventTime();
            auto nextTime = std::min(evntTime, time);
            time_desired = gd2helicsTime(nextTime);
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
                if (helics2gdTime(time_granted) - m_gds->getSimulationTime() < 0.00001) {
                    continue;
                }
            }

            try {
                m_gds->run(helics2gdTime(time_granted));
            }
            catch (const std::runtime_error& re) {
                std::cerr << "execution error in GridDyn" << re.what() << '\n';
                fed_->localError(m_gds->getErrorCode(), re.what());

                throw(re);
            }
        }
        return m_gds->getSimulationTime();
    }

    void helicsRunner::Finalize()
    {
        fed_->finalize();
    }
}  // namespace griddyn::helicsLib
