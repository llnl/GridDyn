/*
 * Copyright (c) 2014-2026, Lawrence Livermore National Security
 * See the top-level NOTICE for additional details. All rights reserved.
 * SPDX-License-Identifier: BSD-3-Clause
 */

#include "helicsSupport.h"

#include <regex>
#include <sstream>
#include <string>
#include <string_view>

namespace griddyn::helicsLib {
helics::Time gd2helicsTime(coreTime evntTime)
{
    return helics::Time(evntTime.toCount(time_units::ns), time_units::ns);
}

coreTime helics2gdTime(helics::Time ftime)
{
    return coreTime(ftime.toCount(time_units::ns), time_units::ns);
}

const std::regex creg(
    "([+-]?(\\d+(\\.\\d+)?|\\.\\d+)([eE][+-]?\\d+)?)\\s*([+-]\\s*(\\d+(\\.\\d+)?|\\.\\d+)([eE][+-]?\\d+)?)[ji]*");

std::future<int> runBroker(std::string_view cmd_args)
{
    std::string broker_exe = std::string(HELICS_BROKER_EXECUTABLE) + " " + std::string(cmd_args);
    auto v = std::async(std::launch::async, [=]() { return system(broker_exe.c_str()); });
    return v;
}

std::future<int> runPlayer(std::string_view cmd_args)
{
    std::string player_exe = std::string(HELICS_PLAYER_EXECUTABLE) + " " + std::string(cmd_args);
    auto v = std::async(std::launch::async, [=]() { return system(player_exe.c_str()); });
    return v;
}

std::future<int> runRecorder(std::string_view cmd_args)
{
    std::string recorder_exe =
        std::string(HELICS_RECORDER_EXECUTABLE) + " " + std::string(cmd_args);
    auto v = std::async(std::launch::async, [=]() { return system(recorder_exe.c_str()); });
    return v;
}

}  // namespace griddyn::helicsLib
