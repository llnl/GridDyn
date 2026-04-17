/*
 * Copyright (c) 2014-2026, Lawrence Livermore National Security
 * See the top-level NOTICE for additional details. All rights reserved.
 * SPDX-License-Identifier: BSD-3-Clause
 */

#pragma once

#include "../griddyn/gridDynDefinitions.hpp"
#include "helics/application_api.hpp"
#include <complex>
#include <future>
#include <string>

namespace griddyn::helicsLib {
    helics::Time gd2helicsTime(coreTime evntTime);

    coreTime helics2gdTime(helics::Time ftime);

    std::future<int> runBroker(const std::string& cmd_args);
    std::future<int> runPlayer(const std::string& cmd_args);
    std::future<int> runRecorder(const std::string& cmd_args);

}  // namespace griddyn::helicsLib
