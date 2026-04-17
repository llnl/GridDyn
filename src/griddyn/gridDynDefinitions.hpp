#pragma once

/*
 * Copyright (c) 2014-2026, Lawrence Livermore National Security
 * See the top-level NOTICE for additional details. All rights reserved.
 * SPDX-License-Identifier: BSD-3-Clause
 */

#include "../core/coreDefinitions.hpp"
#include <string>
#include <vector>

namespace griddyn {
constexpr double kPI(3.1415926535897931);  // defining a Pi constant
constexpr double kWS(60.0 * 2.0 * kPI);  // radians per second at 60 Hz
constexpr double kWS50(50.0 * 2.0 * kPI);  // radians per second at 50 Hz

// A few ERROR codes
#define NO_SLACK_BUS_FOUND (-2)
// alert codes

#define TRANSLINE_ANGLE_TRIP (21)
#define TRANSLINE_LIMIT_TRIP (22)
// generator trip conditions
#define GENERATOR_UNDERFREQUENCY_TRIP (31)
#define GENERATOR_FAULT (32)
#define GENERATOR_OVERSPEED_TRIP (33)
// bus fault conditions
#define BUS_UNDER_POWER (40)
#define BUS_UNDER_VOLTAGE (41)
#define BUS_UNDER_FREQUENCY (42)
// load trip conditions
#define LOAD_TRIP (50)
#define UNDERVOLT_LOAD_TRIP (51)
#define UNDERFREQUENCY_LOAD_TRIP (52)
#define UNBALANCED_LOAD_ALERT (54)
// switch change messages
#define SWITCH_OPEN (60)
#define SWITCH_CLOSE (61)
#define SWITCH1_CLOSE (62)
#define SWITCH1_OPEN (63)
#define SWITCH2_CLOSE (64)
#define SWITCH2_OPEN (65)
// fuse change messages
#define FUSE_BLOWN (70)
#define FUSE_BLOWN_CURRENT (71)
#define FUSE_BLOWN_ANGLE (72)
#define FUSE1_BLOWN_ANGLE (73)
#define FUSE1_BLOWN_CURRENT (74)
#define FUSE2_BLOWN_ANGLE (75)
#define FUSE2_BLOWN_CURRENT (76)
// breaker change messages
#define BREAKER_TRIP (80)
#define BREAKER_TRIP_CURRENT (81)
#define BREAKER_TRIP_ANGLE (82)
#define BREAKER1_TRIP_CURRENT (83)
#define BREAKER1_TRIP_ANGLE (84)
#define BREAKER2_TRIP_CURRENT (85)
#define BREAKER2_TRIP_ANGLE (86)
#define BREAKER_RECLOSE (87)
#define BREAKER1_RECLOSE (88)
#define BREAKER2_RECLOSE (89)

constexpr double kMin_Res(1e-7);
/** @brief enumeration of object changes that can occur throughout the simulation */
enum class change_code {
    not_triggered = -2,  //!< no potential change was triggered
    execution_failure = -1,  //!< the execution has failed
    no_change = 0,  //!< there was no change
    non_state_change = 1,  //!< a change occurred that cannot affect the states
    parameter_change = 2,  //!< a parameter change occurred
    jacobian_change = 3,  //!< a change to the number of non-zeros occurred
    object_change = 4,  //!< a change in the number of number of objects occurred
    state_count_change = 5,  //!< a change in the number of states occurred
};

/* May at some point in the future convert all the set/get functions to use this in the function
prototypes to facilitate transfer to a different type*/
using parameterName = const std::string&;

/* may at some point use this type alias to convert all object parameters to this type and then move
them to a new class to enable additional functionality*/
using model_parameter = double;

/* define the basic vector types*/
}  // namespace griddyn

#ifdef __GNUC__
#    pragma GCC diagnostic push
#    pragma GCC diagnostic ignored "-Wshadow"
#    include <boost/container/small_vector.hpp>
#    pragma GCC diagnostic pop
#else
#    include <boost/container/small_vector.hpp>
#endif

namespace griddyn {
using IOdata = boost::container::small_vector<double, 4>;
using IOlocs = boost::container::small_vector<index_t, 4>;

template<typename X>
using objVector = std::vector<X>;
// template<typename X>
// using objVector = boost::container::small_vector<X,4>;

const IOdata noInputs{};
const IOlocs noInputLocs{};

}  // namespace griddyn
