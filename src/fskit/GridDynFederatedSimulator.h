/* -*- Mode:C++; c-file-style:"gnu"; indent-tabs-mode:nil;  eval: (c-set-offset 'innamespace 0); -*-
 */
/*
 * Copyright (c) 2014-2026, Lawrence Livermore National Security
 * See the top-level NOTICE for additional details. All rights reserved.
 * SPDX-License-Identifier: BSD-3-Clause
 */

#pragma once

#include "fskit/fskitRunner.h"
#include <fskit/discrete-event-federated-simulator.h>
#include <fskit/granted-time-window-scheduler.h>
#include <fskit/time.h>
#include <fskit/variable-step-size-federated-simulator.h>
#include <iostream>
#include <memory>
#include <random>
#include <string>
#include <tuple>

namespace griddyn {
class fskitRunner;
}  // namespace griddyn

/**
 * Example variable step size simulator implementation.
 */
class GriddynFederatedSimulator:
    public fskit::VariableStepSizeFederatedSimulator,
    public fskit::DiscreteEventFederatedSimulator {
  public:
    GriddynFederatedSimulator(std::string name,
                              int argc,
                              char* argv[],
                              std::shared_ptr<fskit::GrantedTimeWindowScheduler> scheduler);

    bool Initialize(void);

    void StartCommunication(void);

    bool TestCommunication(void);

    fskit::Time CalculateLocalGrantedTime(void);

    bool Finalize(void);

    // Methods used by Variable Step Size simulator
    std::tuple<fskit::Time, bool> TimeAdvancement(const fskit::Time& time);

    // Methods used by Discrete Event simulator
    void StartTimeAdvancement(const fskit::Time& time);

    std::tuple<bool, bool> TestTimeAdvancement(void);

  private:
    std::string mName;

    fskit::Time mCurrentFskitTime;
    fskit::Time mGrantedTime;
    griddyn::coreTime mCurrentGriddynTime;

    std::shared_ptr<griddyn::fskitRunner> mGridDyn;
};
