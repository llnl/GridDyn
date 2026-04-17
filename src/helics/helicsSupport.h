#pragma once

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
