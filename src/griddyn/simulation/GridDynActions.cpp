/*
 * Copyright (c) 2014-2026, Lawrence Livermore National Security
 * See the top-level NOTICE for additional details. All rights reserved.
 * SPDX-License-Identifier: BSD-3-Clause
 */

#include "GridDynActions.h"

#include "core/CoreExceptions.h"
#include "gmlc/utilities/stringConversion.h"
#include <string>
#include <string_view>

namespace griddyn {
using gmlc::utilities::makeLowerCase;

gridDynAction::gridDynAction(GdAction action) noexcept: command(action) {}

gridDynAction::gridDynAction(std::string_view operation)
{
    process(operation);
}

void gridDynAction::reset()
{
    command = GdAction::ignore;
    string1.clear();
    string2.clear();
    val_double = kNullVal;
    val_double2 = kNullVal;
    val_int1 = -1;
    val_int2 = -1;
}

void gridDynAction::process(std::string_view operation)
{
    /* (s) string,  (d) double,  (i) int, (X)* optional, (s|d|i), string or double or int*/
    auto ssep = gmlc::utilities::stringOps::splitline(
        operation, " ", gmlc::utilities::stringOps::delimiter_compression::on);
    size_t tokenCount = ssep.size();
    for (size_t kk = 0; kk < tokenCount; ++kk) {
        if (ssep[kk][0] == '#')  // clear all the comments
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
    const auto parseStopAndStep = [&]() {
        if (tokenCount > 1) {
            val_double = gmlc::utilities::numeric_conversion<double>(ssep[1], kNullVal);
            if (tokenCount > 2) {
                val_double2 = gmlc::utilities::numeric_conversion<double>(ssep[2], kNullVal);
            }
        }
    };

    if (commandToken == "ignore") {
        // ignore XXXXXX
        command = GdAction::ignore;
    } else if (commandToken == "set") {
        // set parameter(s) value(d)
        if (tokenCount >= 3) {
            command = GdAction::set;
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
        command = GdAction::setall;
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
        // setsolver mode(s) solver(s|i)
        command = GdAction::setsolver;
        if (tokenCount >= 3) {
            string1 = ssep[1];
            val_int1 = gmlc::utilities::numeric_conversion<int>(
                ssep[2],
                -435);  //-435 is some random number with no meaning outside this call
            if (val_int1 == -435) {
                string1 = ssep[2];
            }
        } else {
            throw(InvalidParameterValue(commandToken));
        }
    } else if (commandToken == "print") {
        // print parameter(s) setstring(s)
        command = GdAction::print;
        if (tokenCount >= 3) {
            string1 = ssep[1];
            string2 = ssep[2];
        } else {
            throw(InvalidParameterValue(commandToken));
        }
    } else if (commandToken == "powerflow") {
        // powerflow
        command = GdAction::powerflow;
    } else if (commandToken == "step") {
        // step solutionType*
        command = GdAction::step;
        if (tokenCount > 1) {
            string1 = ssep[1];
        }
    } else if (commandToken == "eventmode") {
        // eventmode tstop*  tstep*
        command = GdAction::eventmode;
        if (tokenCount > 1) {
            val_double = gmlc::utilities::numeric_conversion<double>(ssep[1], kNullVal);
            if (tokenCount > 2) {
                val_double2 = gmlc::utilities::numeric_conversion<double>(ssep[2], kNullVal);
            }
        }
    } else if (commandToken == "initialize") {
        // initialize
        command = GdAction::initialize;
    } else if (commandToken == "dynamic") {
        // dynamic "dae"|"part"|"decoupled" stoptime(d)* steptime(d)*
        if (tokenCount == 1) {
            command = GdAction::dynamicDAE;
        } else {
            makeLowerCase(ssep[1]);
            if (ssep[1] == "dae") {
                command = GdAction::dynamicDAE;
                if (tokenCount > 2) {
                    val_double = gmlc::utilities::numeric_conversion<double>(ssep[2], kNullVal);
                }
            } else if ((ssep[1] == "part") || (ssep[1] == "partitioned")) {
                command = GdAction::dynamicPart;
                if (tokenCount > 2) {
                    val_double = gmlc::utilities::numeric_conversion<double>(ssep[2], kNullVal);
                }
                if (tokenCount > 3) {
                    val_double2 = gmlc::utilities::numeric_conversion<double>(ssep[3], kNullVal);
                }
            } else if (ssep[1] == "decoupled") {
                command = GdAction::dynamicDecoupled;
                if (tokenCount > 2) {
                    val_double = gmlc::utilities::numeric_conversion<double>(ssep[2], kNullVal);
                }
                if (tokenCount > 3) {
                    val_double2 = gmlc::utilities::numeric_conversion<double>(ssep[3], kNullVal);
                }
            } else {
                const auto test = gmlc::utilities::numeric_conversion<double>(ssep[2], kNullVal);
                if (test == kNullVal) {
                    throw(InvalidParameterValue(commandToken));
                }
                if (tokenCount > 2) {
                    val_double = test;
                    val_double2 = gmlc::utilities::numeric_conversion<double>(ssep[3], kNullVal);
                }
                command = GdAction::dynamicDAE;
            }
        }
    } else if (commandToken == "dynamicdae") {
        // dynamicdae stoptime(d)*
        command = GdAction::dynamicDAE;

        if (tokenCount > 1) {
            val_double = gmlc::utilities::numeric_conversion<double>(ssep[1], kNullVal);
        }
    } else if (commandToken == "dynamicpart") {
        // dynamicpart stoptime(d)* steptime(d)*
        command = GdAction::dynamicPart;
        parseStopAndStep();
    } else if (commandToken == "dynamicdecoupled") {
        // dynamicdecoupled stop(d)* step(d)*
        command = GdAction::dynamicDecoupled;
        parseStopAndStep();
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
        command = GdAction::reset;
    } else if (commandToken == "iterate") {
        // iterate interval(d)* stoptime(d)*
        command = GdAction::iterate;
        if (tokenCount > 1) {
            val_double = gmlc::utilities::numeric_conversion<double>(ssep[1], kNullVal);
            if (tokenCount > 2) {
                val_double2 = gmlc::utilities::numeric_conversion<double>(ssep[2], kNullVal);
            }
        }
    } else if (commandToken == "check") {
        // check
        command = GdAction::check;
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
        command = GdAction::run;
    } else if (commandToken == "save") {
        // save subject(s) file(s)
        if (tokenCount > 2) {
            string1 = ssep[1];
            string2 = ssep[2];
        } else {
            throw(InvalidParameterValue(commandToken));
        }
        command = GdAction::save;
    } else if (commandToken == "load") {
        // load subject(s) file(s)
        if (tokenCount > 2) {
            string1 = ssep[1];
            string2 = ssep[2];
        } else {
            throw(InvalidParameterValue("load"));
        }
        command = GdAction::load;
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
        command = GdAction::add;
    } else if (commandToken == "rollback") {
        // rollback point(s|d)
        command = GdAction::rollback;
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
        command = GdAction::checkpoint;
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
        command = GdAction::contingency;
        size_t nindex{1};
        if (ssep[1] == "simplified") {
            flag = 1;
            ++nindex;
        }
        string1 = ssep[nindex];
        if (tokenCount > nindex + 1) {
            string2 = ssep[nindex + 1];
        }
        if (tokenCount > nindex + 2) {
            val_int1 = gmlc::utilities::numeric_conversion<int>(ssep[nindex + 2], 0);
        } else {
            val_int1 = 0;
        }
        if (tokenCount > nindex + 3) {
            val_int2 = gmlc::utilities::numeric_conversion<int>(ssep[nindex + 3], 0);
        } else {
            val_int2 = 0;
        }
    } else if (commandToken == "continuation") {
    } else {
        throw(UnrecognizedParameter(commandToken));
    }
}

}  // namespace griddyn
