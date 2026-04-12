/*
 * Copyright (c) 2014-2020, Lawrence Livermore National Security
 * See the top-level NOTICE for additional details. All rights reserved.
 * SPDX-License-Identifier: BSD-3-Clause
 */

#include "gridDynActions.h"

#include "core/coreExceptions.h"
#include "gmlc/utilities/stringConversion.h"
#include <string>

namespace griddyn {
using gmlc::utilities::makeLowerCase;

gridDynAction::gridDynAction(gd_action_t action) noexcept: command(action) {}

gridDynAction::gridDynAction(const std::string& operation)
{
    process(operation);
}

void gridDynAction::reset()
{
    command = gd_action_t::ignore;
    string1.clear();
    string2.clear();
    val_double = kNullVal;
    val_double2 = kNullVal;
    val_int1 = -1;
    val_int2 = -1;
}

void gridDynAction::process(const std::string& operation)
{
    /* (s) string,  (d) double,  (i) int, (X)* optional, (s|d|i), string or double or int*/
    auto ssep = gmlc::utilities::stringOps::splitline(
        operation, " ", gmlc::utilities::stringOps::delimiter_compression::on);
    size_t sz = ssep.size();
    for (size_t kk = 0; kk < sz; ++kk) {
        if (ssep[kk][0] == '#')  // clear all the comments
        {
            sz = kk;
        }
    }
    reset();
    if (sz == 0) {
        // check if there was no command
        return;
    }
    std::string cmd = gmlc::utilities::convertToLowerCase(ssep[0]);

    if (cmd == "ignore") {
        // ignore XXXXXX
        command = gd_action_t::ignore;
    } else if (cmd == "set") {
        // set parameter(s) value(d)
        if (sz >= 3) {
            command = gd_action_t::set;
            string1 = ssep[1];
            val_double = gmlc::utilities::numeric_conversion<double>(ssep[2], kNullVal);
            if (val_double == kNullVal) {
                string2 = ssep[2];
            }
        } else {
            throw(invalidParameterValue(cmd));
        }
    } else if (cmd == "setall") {
        // setall  objecttype(s) parameter(s) value(d)
        command = gd_action_t::setall;
        if (sz >= 4) {
            double test = gmlc::utilities::numeric_conversion<double>(ssep[3], kNullVal);
            if (test == kNullVal) {
                throw(invalidParameterValue(cmd));
            }
            string1 = ssep[1];
            string2 = ssep[2];
            val_double = test;
        } else {
            throw(invalidParameterValue(cmd));
        }
    } else if (cmd == "setsolver") {
        // setsolver mode(s) solver(s|i)
        command = gd_action_t::setsolver;
        if (sz >= 3) {
            string1 = ssep[1];
            val_int1 = gmlc::utilities::numeric_conversion<int>(
                ssep[2],
                -435);  //-435 is some random number with no meaning outside this call
            if (val_int1 == -435) {
                string1 = ssep[2];
            }
        } else {
            throw(invalidParameterValue(cmd));
        }
    } else if (cmd == "print") {
        // print parameter(s) setstring(s)
        command = gd_action_t::print;
        if (sz >= 3) {
            string1 = ssep[1];
            string2 = ssep[2];
        } else {
            throw(invalidParameterValue(cmd));
        }
    } else if (cmd == "powerflow") {
        // powerflow
        command = gd_action_t::powerflow;
    } else if (cmd == "step") {
        // step solutionType*
        command = gd_action_t::step;
        if (sz > 1) {
            string1 = ssep[1];
        }
    } else if (cmd == "eventmode") {
        // eventmode tstop*  tstep*
        command = gd_action_t::eventmode;
        if (sz > 1) {
            val_double = gmlc::utilities::numeric_conversion<double>(ssep[1], kNullVal);
            if (sz > 2) {
                val_double2 = gmlc::utilities::numeric_conversion<double>(ssep[2], kNullVal);
            }
        }
    } else if (cmd == "initialize") {
        // initialize
        command = gd_action_t::initialize;
    } else if (cmd == "dynamic") {
        // dynamic "dae"|"part"|"decoupled" stoptime(d)* steptime(d)*
        if (sz == 1) {
            command = gd_action_t::dynamicDAE;
        } else {
            makeLowerCase(ssep[1]);
            if (ssep[1] == "dae") {
                command = gd_action_t::dynamicDAE;
                if (sz > 2) {
                    val_double = gmlc::utilities::numeric_conversion<double>(ssep[2], kNullVal);
                }
            } else if ((ssep[1] == "part") || (ssep[1] == "partitioned")) {
                command = gd_action_t::dynamicPart;
                if (sz > 2) {
                    val_double = gmlc::utilities::numeric_conversion<double>(ssep[2], kNullVal);
                }
                if (sz > 3) {
                    val_double2 = gmlc::utilities::numeric_conversion<double>(ssep[3], kNullVal);
                }
            } else if (ssep[1] == "decoupled") {
                command = gd_action_t::dynamicDecoupled;
                if (sz > 2) {
                    val_double = gmlc::utilities::numeric_conversion<double>(ssep[2], kNullVal);
                }
                if (sz > 3) {
                    val_double2 = gmlc::utilities::numeric_conversion<double>(ssep[3], kNullVal);
                }
            } else {
                double test = gmlc::utilities::numeric_conversion<double>(ssep[2], kNullVal);
                if (test == kNullVal) {
                    throw(invalidParameterValue(cmd));
                }
                if (sz > 2) {
                    val_double = test;
                    val_double2 = gmlc::utilities::numeric_conversion<double>(ssep[3], kNullVal);
                }
                command = gd_action_t::dynamicDAE;
            }
        }
    } else if (cmd == "dynamicdae") {
        // dynamicdae stoptime(d)*
        command = gd_action_t::dynamicDAE;

        if (sz > 1) {
            val_double = gmlc::utilities::numeric_conversion<double>(ssep[1], kNullVal);
        }
    } else if (cmd == "dynamicpart") {
        // dynamicpart stoptime(d)* steptime(d)*
        command = gd_action_t::dynamicPart;

        if (sz > 1) {
            val_double = gmlc::utilities::numeric_conversion<double>(ssep[1], kNullVal);
            if (sz > 2) {
                val_double2 = gmlc::utilities::numeric_conversion<double>(ssep[2], kNullVal);
            }
        }
    } else if (cmd == "dynamicdecoupled") {
        // dynamicdecoupled stop(d)* step(d)*
        command = gd_action_t::dynamicPart;

        if (sz > 1) {
            val_double = gmlc::utilities::numeric_conversion<double>(ssep[1], kNullVal);
            if (sz > 2) {
                val_double2 = gmlc::utilities::numeric_conversion<double>(ssep[2], kNullVal);
            }
        }
    } else if (cmd == "reset") {
        // reset level(i)
        if (sz > 1) {
            auto test_int = gmlc::utilities::numeric_conversion<int>(ssep[1], -435);
            if (test_int == -435) {
                throw(invalidParameterValue(cmd));
            }
            val_int1 = test_int;
        } else {
            val_int1 = 0;
        }
        command = gd_action_t::reset;
    } else if (cmd == "iterate") {
        // iterate interval(d)* stoptime(d)*
        command = gd_action_t::iterate;
        if (sz > 1) {
            val_double = gmlc::utilities::numeric_conversion<double>(ssep[1], kNullVal);
            if (sz > 2) {
                val_double2 = gmlc::utilities::numeric_conversion<double>(ssep[2], kNullVal);
            }
        }
    } else if (cmd == "check") {
        // check
        command = gd_action_t::check;
        if (sz > 1) {
            string1 = ssep[1];
            if (sz > 2) {
                string2 = ssep[2];
            }
        }
    } else if (cmd == "run") {
        // run time(d)*
        if (sz > 1) {
            double test = gmlc::utilities::numeric_conversion<double>(ssep[1], kNullVal);
            if (test == kNullVal) {
                throw(invalidParameterValue("time"));
            }
            val_double = test;
        } else {
            val_double = kNullVal;
        }
        command = gd_action_t::run;
    } else if (cmd == "save") {
        // save subject(s) file(s)
        if (sz > 2) {
            string1 = ssep[1];
            string2 = ssep[2];
        } else {
            throw(invalidParameterValue(cmd));
        }
        command = gd_action_t::save;
    } else if (cmd == "load") {
        // load subject(s) file(s)
        if (sz > 2) {
            string1 = ssep[1];
            string2 = ssep[2];
        } else {
            throw(invalidParameterValue("load"));
        }
        command = gd_action_t::load;
    } else if (cmd == "add") {
        // add addstring(s)
        if (sz > 1) {
            string1 = ssep[1];
            for (size_t kk = 2; kk < sz; ++kk) {
                string1 += " " + ssep[kk];
            }
        } else {
            throw(invalidParameterValue("add"));
        }
        command = gd_action_t::add;
    } else if (cmd == "rollback") {
        // rollback point(s|d)
        command = gd_action_t::rollback;
        if (sz > 1) {
            val_double = gmlc::utilities::numeric_conversion<double>(ssep[1], kNullVal);
            if (val_double == kNullVal) {
                string1 = ssep[1];
            }
        } else {
            string1 = "last";
        }
    } else if (cmd == "checkpoint") {
        // checkpoint name(s)
        command = gd_action_t::checkpoint;
        if (sz > 1) {
            val_double = gmlc::utilities::numeric_conversion<double>(ssep[1], kNullVal);
            if (val_double == kNullVal) {
                string1 = ssep[1];
            }
        } else {
            string1 = "";
        }
    } else if (cmd == "contingency") {
        // contingency mode|fileName output_file|method start count
        command = gd_action_t::contingency;
        size_t nindex{1};
        if (ssep[1] == "simplified") {
            flag = 1;
            ++nindex;
        }
        string1 = ssep[nindex];
        if (sz > nindex + 1) {
            string2 = ssep[nindex + 1];
        }
        if (sz > nindex + 2) {
            val_int1 = gmlc::utilities::numeric_conversion<int>(ssep[nindex + 2], 0);
        } else {
            val_int1 = 0;
        }
        if (sz > nindex + 3) {
            val_int2 = gmlc::utilities::numeric_conversion<int>(ssep[nindex + 3], 0);
        } else {
            val_int2 = 0;
        }
    } else if (cmd == "continuation") {
    } else {
        throw(unrecognizedParameter(cmd));
    }
}

}  // namespace griddyn
