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

GridDynFederatedSimulator::GridDynFederatedSimulator(
    std::string name,
    int argc,
    char* argv[],
    std::shared_ptr<fskit::GrantedTimeWindowScheduler> scheduler):
    VariableStepSizeFederatedSimulator(fskit::FederatedSimulatorId(name)),
    DiscreteEventFederatedSimulator(fskit::FederatedSimulatorId(name)),
    FederatedSimulator(fskit::FederatedSimulatorId(name)), mName(name), mCurrentFskitTime(0),
    mCurrentGriddynTime(0)
{
    mGridDyn = std::make_shared<griddyn::FskitRunner>();

    mGridDyn->Initialize(argc, argv, scheduler);
}

bool GridDynFederatedSimulator::Initialize(void)
{
    mGridDyn->simInitialize();
    return true;
}

void GridDynFederatedSimulator::StartCommunication(void) {}

bool GridDynFederatedSimulator::TestCommunication(void)
{
    return true;
}

fskit::Time GridDynFederatedSimulator::CalculateLocalGrantedTime(void)
{
    const double kBigNum(1e49);

    griddyn::coreTime griddynNextEventTime = mGridDyn->getNextEvent();

    // assert(griddynNextEventTime > mCurrentGriddynTime);

    // Magic number that indicates there are no events on the event queue.
    if (griddynNextEventTime.getBaseTimeCode() == kBigNum) {
        return fskit::Time::getMax();
    }

    // This could be event time + lookahead.
    return fskit::Time(griddynNextEventTime.getBaseTimeCode());
}

bool GridDynFederatedSimulator::Finalize(void)
{
    mGridDyn->Finalize();

    return true;
}

// Method used by Variable Step Size simulator
std::tuple<fskit::Time, bool> GridDynFederatedSimulator::TimeAdvancement(const fskit::Time& time)
{
    griddyn::coreTime gdTime;

    // Convert fskit time to coreTime used by Griddyn
    gdTime.setBaseTimeCode(time.GetRaw());
    bool stopSimulation = false;

    // SGS this is not correct!! How to correctly handled lower resolution of Griddyn time?
    // Advance Griddyn if needed.
    if (gdTime >= mCurrentGriddynTime) {
        try {
            auto gdRetTime = mGridDyn->Step(gdTime);
            // assert(gdRetTime <= gdTime);
            mCurrentGriddynTime = gdRetTime;
            mCurrentFskitTime = fskit::Time(gdRetTime.getBaseTimeCode());

            // Time should not advance beyond granted time.
            assert(mCurrentFskitTime <= time);

            {
                // Next event time should now be larger than granted time
                griddyn::coreTime griddynNextEventTime = mGridDyn->getNextEvent();
                // assert(griddynNextEventTime > mCurrentGriddynTime);
            }
        }
        catch (...) {
            // std::cout << "Griddyn stopping due to runtime_error" << std::endl;
            stopSimulation = true;
        }
    }

    return std::make_tuple(mCurrentFskitTime, stopSimulation);
}

// Method used by Discrete Event simulator
void GridDynFederatedSimulator::StartTimeAdvancement(const fskit::Time& time)
{
    mGrantedTime = time;
}

// Method used by Discrete Event simulator
std::tuple<bool, bool> GridDynFederatedSimulator::TestTimeAdvancement(void)
{
    // SGS fixme, should this be a while loop to ensure granted time is reached?
    std::tuple<fskit::Time, bool> result = TimeAdvancement(mGrantedTime);
    return std::make_tuple(true, std::get<1>(result));
}
