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

#include "helicsSupport.h"

#include <regex>
#include <sstream>
#include <string>

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

    std::future<int> runBroker(const std::string& cmd_args)
    {
        std::string broker_exe = std::string(HELICS_BROKER_EXECUTABLE) + " " + cmd_args;
        auto v = std::async(std::launch::async, [=]() { return system(broker_exe.c_str()); });
        return v;
    }

    std::future<int> runPlayer(const std::string& cmd_args)
    {
        std::string player_exe = std::string(HELICS_PLAYER_EXECUTABLE) + " " + cmd_args;
        auto v = std::async(std::launch::async, [=]() { return system(player_exe.c_str()); });
        return v;
    }

    std::future<int> runRecorder(const std::string& cmd_args)
    {
        std::string recorder_exe = std::string(HELICS_RECORDER_EXECUTABLE) + " " + cmd_args;
        auto v = std::async(std::launch::async, [=]() { return system(recorder_exe.c_str()); });
        return v;
    }

}  // namespace griddyn::helicsLib
