/*
 * Copyright (c) 2014-2026, Lawrence Livermore National Security
 * See the top-level NOTICE for additional details. All rights reserved.
 * SPDX-License-Identifier: BSD-3-Clause
 */

#include "GridDynActions.h"

#include "core/CoreExceptions.h"
#include "gmlc/utilities/stringConversion.h"
#include <array>
#include <string>
#include <string_view>

namespace griddyn {
using gmlc::utilities::makeLowerCase;

GridDynAction::GridDynAction(GdAction action) noexcept: command(action) {}

GridDynAction::GridDynAction(std::string_view operation)
{
    process(operation);
}

std::string_view GridDynAction::commandName() const noexcept
{
    constexpr std::array<std::string_view, 25> names{"ignore",
                                                     "set",
                                                     "setsolver",
                                                     "setall",
                                                     "print",
                                                     "initialize",
                                                     "powerflow",
                                                     "reset",
                                                     "iterate",
                                                     "eventmode",
                                                     "dynamicdae",
                                                     "dynamicpart",
                                                     "dynamicdecoupled",
                                                     "step",
                                                     "run",
                                                     "save",
                                                     "check",
                                                     "load",
                                                     "add",
                                                     "rollback",
                                                     "checkpoint",
                                                     "contingency",
                                                     "continuation",
                                                     "invalid",
                                                     "dynamic"};
    static_assert(names.size() == static_cast<size_t>(GdAction::DYNAMIC_DEFAULT) + 1);
    const auto index = static_cast<size_t>(command);
    return index < names.size() ? names[index] : "invalid";
}

void GridDynAction::reset()
{
    command = GdAction::IGNORE_ACTION;
    string1.clear();
    string2.clear();
    val_double = kNullVal;
    val_double2 = kNullVal;
    val_int1 = -1;
    val_int2 = -1;
    flag = 0;
}

void GridDynAction::process(std::string_view operation)
{
    /* (s) string,  (d) double,  (i) int, (X)* optional, (s|d|i), string or double or int*/
    auto ssep = gmlc::utilities::stringOps::splitline(
        operation, " ", gmlc::utilities::stringOps::delimiter_compression::on);
    size_t tokenCount = ssep.size();
    for (size_t kk = 0; kk < tokenCount; ++kk) {
        if (ssep[kk].starts_with('#'))  // clear all the comments
        {
            tokenCount = kk;
        }
    }
    reset();
    if (tokenCount == 0) {
        // check if there was no command
        return;
    }
    const std::string commandToken = gmlc::utilities::convertToLowerCase(ssep[0]);
    const auto parseStopAndStep = [&](size_t stopIndex) {
        if (tokenCount > stopIndex) {
            val_double = gmlc::utilities::numeric_conversion<double>(ssep[stopIndex], kNullVal);
        }
        if (tokenCount > stopIndex + 1) {
            val_double2 =
                gmlc::utilities::numeric_conversion<double>(ssep[stopIndex + 1], kNullVal);
        }
    };

    if (commandToken == "ignore") {
        // ignore XXXXXX
        command = GdAction::IGNORE_ACTION;
    } else if (commandToken == "set") {
        // set parameter(s) value(d)
        if (tokenCount >= 3) {
            command = GdAction::SET_ACTION;
            string1 = ssep[1];
            val_double = gmlc::utilities::numeric_conversion<double>(ssep[2], kNullVal);
            if (val_double == kNullVal) {
                string2 = ssep[2];
            }
        } else {
            throw(InvalidParameterValue(commandToken));
        }
    } else if (commandToken == "setall") {
        // setall  objecttype(s) parameter(s) value(d)
        command = GdAction::SETALL;
        if (tokenCount >= 4) {
            const auto test = gmlc::utilities::numeric_conversion<double>(ssep[3], kNullVal);
            if (test == kNullVal) {
                throw(InvalidParameterValue(commandToken));
            }
            string1 = ssep[1];
            string2 = ssep[2];
            val_double = test;
        } else {
            throw(InvalidParameterValue(commandToken));
        }
    } else if (commandToken == "setsolver") {
        // Retain the selection operands for the unsupported action.
        command = GdAction::SETSOLVER;
        if (tokenCount >= 3) {
            string1 = ssep[1];
            string2 = ssep[2];
        } else {
            throw(InvalidParameterValue(commandToken));
        }
    } else if (commandToken == "print") {
        // print parameter(s) setstring(s)
        command = GdAction::PRINT;
        if (tokenCount >= 3) {
            string1 = ssep[1];
            string2 = ssep[2];
        } else {
            throw(InvalidParameterValue(commandToken));
        }
    } else if (commandToken == "powerflow") {
        // powerflow
        command = GdAction::POWERFLOW;
    } else if (commandToken == "step") {
        // step solutionType*
        command = GdAction::STEP;
        if (tokenCount > 1) {
            string1 = ssep[1];
        }
    } else if (commandToken == "eventmode") {
        // eventmode tstop*  tstep*
        command = GdAction::EVENTMODE;
        if (tokenCount > 1) {
            val_double = gmlc::utilities::numeric_conversion<double>(ssep[1], kNullVal);
            if (tokenCount > 2) {
                val_double2 = gmlc::utilities::numeric_conversion<double>(ssep[2], kNullVal);
            }
        }
    } else if (commandToken == "initialize") {
        // initialize
        command = GdAction::INITIALIZE;
    } else if (commandToken == "dynamic") {
        // dynamic [dae|part|partitioned|decoupled] [stoptime [steptime]]
        command = GdAction::DYNAMIC_DEFAULT;
        if (tokenCount > 1) {
            makeLowerCase(ssep[1]);
            if (ssep[1] == "dae") {
                command = GdAction::DYNAMIC_DAE;
                if (tokenCount > 2) {
                    val_double = gmlc::utilities::numeric_conversion<double>(ssep[2], kNullVal);
                }
            } else if ((ssep[1] == "part") || (ssep[1] == "partitioned")) {
                command = GdAction::DYNAMIC_PART;
                parseStopAndStep(2);
            } else if (ssep[1] == "decoupled") {
                command = GdAction::DYNAMIC_DECOUPLED;
                parseStopAndStep(2);
            } else {
                val_double = gmlc::utilities::numeric_conversionComplete<double>(ssep[1], kNullVal);
                if (val_double == kNullVal) {
                    throw(InvalidParameterValue(commandToken));
                }
                if (tokenCount > 2) {
                    val_double2 = gmlc::utilities::numeric_conversion<double>(ssep[2], kNullVal);
                }
            }
        }
    } else if (commandToken == "dynamicdae") {
        // dynamicdae stoptime(d)*
        command = GdAction::DYNAMIC_DAE;

        if (tokenCount > 1) {
            val_double = gmlc::utilities::numeric_conversion<double>(ssep[1], kNullVal);
        }
    } else if (commandToken == "dynamicpart") {
        // dynamicpart stoptime(d)* steptime(d)*
        command = GdAction::DYNAMIC_PART;
        parseStopAndStep(1);
    } else if (commandToken == "dynamicdecoupled") {
        // dynamicdecoupled stop(d)* step(d)*
        command = GdAction::DYNAMIC_DECOUPLED;
        parseStopAndStep(1);
    } else if (commandToken == "reset") {
        // reset level(i)
        if (tokenCount > 1) {
            auto test_int = gmlc::utilities::numeric_conversion<int>(ssep[1], -435);
            if (test_int == -435) {
                throw(InvalidParameterValue(commandToken));
            }
            val_int1 = test_int;
        } else {
            val_int1 = 0;
        }
        command = GdAction::RESET;
    } else if (commandToken == "iterate") {
        // iterate interval(d)* stoptime(d)*
        command = GdAction::ITERATE;
        if (tokenCount > 1) {
            val_double = gmlc::utilities::numeric_conversion<double>(ssep[1], kNullVal);
            if (tokenCount > 2) {
                val_double2 = gmlc::utilities::numeric_conversion<double>(ssep[2], kNullVal);
            }
        }
    } else if (commandToken == "check") {
        // check
        command = GdAction::CHECK;
        if (tokenCount > 1) {
            string1 = ssep[1];
            if (tokenCount > 2) {
                string2 = ssep[2];
            }
        }
    } else if (commandToken == "run") {
        // run time(d)*
        if (tokenCount > 1) {
            const auto test = gmlc::utilities::numeric_conversion<double>(ssep[1], kNullVal);
            if (test == kNullVal) {
                throw(InvalidParameterValue("time"));
            }
            val_double = test;
        } else {
            val_double = kNullVal;
        }
        command = GdAction::RUN;
    } else if (commandToken == "save") {
        // save subject(s) file(s)
        if (tokenCount > 2) {
            string1 = ssep[1];
            string2 = ssep[2];
        } else {
            throw(InvalidParameterValue(commandToken));
        }
        command = GdAction::SAVE;
    } else if (commandToken == "load") {
        // load subject(s) file(s)
        if (tokenCount > 2) {
            string1 = ssep[1];
            string2 = ssep[2];
        } else {
            throw(InvalidParameterValue("load"));
        }
        command = GdAction::LOAD;
    } else if (commandToken == "add") {
        // add addstring(s)
        if (tokenCount > 1) {
            string1 = ssep[1];
            for (size_t kk = 2; kk < tokenCount; ++kk) {
                string1 += " " + ssep[kk];
            }
        } else {
            throw(InvalidParameterValue("add"));
        }
        command = GdAction::ADD;
    } else if (commandToken == "rollback") {
        // rollback point(s|d)
        command = GdAction::ROLLBACK;
        if (tokenCount > 1) {
            val_double = gmlc::utilities::numeric_conversion<double>(ssep[1], kNullVal);
            if (val_double == kNullVal) {
                string1 = ssep[1];
            }
        } else {
            string1 = "last";
        }
    } else if (commandToken == "checkpoint") {
        // checkpoint name(s)
        command = GdAction::CHECKPOINT;
        if (tokenCount > 1) {
            val_double = gmlc::utilities::numeric_conversion<double>(ssep[1], kNullVal);
            if (val_double == kNullVal) {
                string1 = ssep[1];
            }
        } else {
            string1 = "";
        }
    } else if (commandToken == "contingency") {
        // contingency mode|fileName output_file|method start count
        if (tokenCount < 2) {
            throw(InvalidParameterValue(commandToken));
        }
        const bool simplified = (ssep[1] == "simplified");
        const size_t modeIndex = simplified ? 2 : 1;
        if (tokenCount <= modeIndex) {
            throw(InvalidParameterValue(commandToken));
        }
        command = GdAction::CONTINGENCY;
        flag = simplified ? 1 : 0;
        string1 = ssep[modeIndex];
        if (tokenCount > modeIndex + 1) {
            string2 = ssep[modeIndex + 1];
        }
        if (tokenCount > modeIndex + 2) {
            val_int1 = gmlc::utilities::numeric_conversion<int>(ssep[modeIndex + 2], 0);
        } else {
            val_int1 = 0;
        }
        if (tokenCount > modeIndex + 3) {
            val_int2 = gmlc::utilities::numeric_conversion<int>(ssep[modeIndex + 3], 0);
        } else {
            val_int2 = 0;
        }
    } else if (commandToken == "continuation") {
        command = GdAction::CONTINUATION;
    } else {
        throw(UnrecognizedParameter(commandToken));
    }
}

}  // namespace griddyn
