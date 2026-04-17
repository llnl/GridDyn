/*
 * Copyright (c) 2014-2026, Lawrence Livermore National Security
 * See the top-level NOTICE for additional details. All rights reserved.
 * SPDX-License-Identifier: BSD-3-Clause
 */

#include "GridDynFederatedSimulator.h"

#include <limits>
#include <memory>
#include <string>
#include <tuple>

GriddynFederatedSimulator::GriddynFederatedSimulator(
    std::string name,
    int argc,
    char* argv[],
    std::shared_ptr<fskit::GrantedTimeWindowScheduler> scheduler):
    VariableStepSizeFederatedSimulator(fskit::FederatedSimulatorId(name)),
    DiscreteEventFederatedSimulator(fskit::FederatedSimulatorId(name)),
    FederatedSimulator(fskit::FederatedSimulatorId(name)), m_name(name), m_currentFskitTime(0),
    m_currentGriddynTime(0)
{
    m_griddyn = std::make_shared<griddyn::fskitRunner>();

    m_griddyn->Initialize(argc, argv, scheduler);
}

bool GriddynFederatedSimulator::Initialize(void)
{
    m_griddyn->simInitialize();
    return true;
}

void GriddynFederatedSimulator::StartCommunication(void) {}

bool GriddynFederatedSimulator::TestCommunication(void)
{
    return true;
}

fskit::Time GriddynFederatedSimulator::CalculateLocalGrantedTime(void)
{
    const double kBigNum(1e49);

    griddyn::coreTime griddynNextEventTime = m_griddyn->getNextEvent();

    // assert(griddynNextEventTime > m_currentGriddynTime);

    // Magic number that indicates there are no events on the event queue.
    if (griddynNextEventTime.getBaseTimeCode() == kBigNum) {
        return fskit::Time::getMax();
    }

    // This could be event time + lookahead.
    return fskit::Time(griddynNextEventTime.getBaseTimeCode());
}

bool GriddynFederatedSimulator::Finalize(void)
{
    m_griddyn->Finalize();

    return true;
}

// Method used by Variable Step Size simulator
std::tuple<fskit::Time, bool> GriddynFederatedSimulator::TimeAdvancement(const fskit::Time& time)
{
    griddyn::coreTime gdTime;

    // Convert fskit time to coreTime used by Griddyn
    gdTime.setBaseTimeCode(time.GetRaw());
    bool stopSimulation = false;

    // SGS this is not correct!! How to correctly handled lower resolution of Griddyn time?
    // Advance Griddyn if needed.
    if (gdTime >= m_currentGriddynTime) {
        try {
            auto gdRetTime = m_griddyn->Step(gdTime);
            // assert(gdRetTime <= gdTime);
            m_currentGriddynTime = gdRetTime;
            m_currentFskitTime = fskit::Time(gdRetTime.getBaseTimeCode());

            // Time should not advance beyond granted time.
            assert(m_currentFskitTime <= time);

            {
                // Next event time should now be larger than granted time
                griddyn::coreTime griddynNextEventTime = m_griddyn->getNextEvent();
                // assert(griddynNextEventTime > m_currentGriddynTime);
            }
        }
        catch (...) {
            // std::cout << "Griddyn stopping due to runtime_error" << std::endl;
            stopSimulation = true;
        }
    }

    return std::make_tuple(m_currentFskitTime, stopSimulation);
}

// Method used by Discrete Event simulator
void GriddynFederatedSimulator::StartTimeAdvancement(const fskit::Time& time)
{
    m_grantedTime = time;
}

// Method used by Discrete Event simulator
std::tuple<bool, bool> GriddynFederatedSimulator::TestTimeAdvancement(void)
{
    // SGS fixme, should this be a while loop to ensure granted time is reached?
    std::tuple<fskit::Time, bool> result = TimeAdvancement(m_grantedTime);
    return std::make_tuple(true, std::get<1>(result));
}
