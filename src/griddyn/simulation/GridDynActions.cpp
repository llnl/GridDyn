/*
 * Copyright (c) 2014-2026, Lawrence Livermore National Security
 * See the top-level NOTICE for additional details. All rights reserved.
 * SPDX-License-Identifier: BSD-3-Clause
 */

#include "GridDynActions.h"

#include "core/CoreExceptions.h"
#include "gmlc/utilities/stringConversion.h"
#include <array>
#include <charconv>
#include <cmath>
#include <stdexcept>
#include <string>
#include <string_view>

namespace griddyn {
using gmlc::utilities::makeLowerCase;

namespace {
    std::string quotedActionOperand(std::string_view token, std::string_view operand)
    {
        if (token.size() >= 2 && token.starts_with('"') && token.ends_with('"')) {
            token = token.substr(1, token.size() - 2);
        }
        if (token.empty() || token.contains('"')) {
            throw InvalidParameterValue(operand);
        }
        return std::string{token};
    }

    std::string actionFileName(std::string_view token)
    {
        return quotedActionOperand(token, "file name");
    }

    double nonnegativeActionNumber(const std::string& token, std::string_view field)
    {
        try {
            const auto value = gmlc::utilities::numeric_conversionComplete<double>(token, kNullVal);
            if (value != kNullVal && std::isfinite(value) && value >= 0.0) {
                return value;
            }
        }
        catch (const std::out_of_range&) {
            throw InvalidParameterValue(field);
        }
        throw InvalidParameterValue(field);
    }

    index_t monitorBusId(std::string_view token)
    {
        const auto* begin = token.data();
        const auto* end = begin + token.size();
        index_t id = 0;
        const auto result = std::from_chars(begin, end, id);
        if (result.ec != std::errc{} || result.ptr != end || token.starts_with('-')) {
            throw InvalidParameterValue("continuation monitor bus ID");
        }
        return id;
    }
}  // namespace

GridDynAction::GridDynAction(GdAction action) noexcept: command(action) {}

GridDynAction::GridDynAction(std::string_view operation)
{
    process(operation);
}

std::string_view GridDynAction::commandName() const noexcept
{
    constexpr std::array<std::string_view, 28> names{"ignore",
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
                                                     "dynamic",
                                                     "opf",
                                                     "action",
                                                     "get"};
    static_assert(names.size() == static_cast<size_t>(GdAction::GET_ACTION) + 1);
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
    monitorBuses.clear();
}

void GridDynAction::process(std::string_view operation)
{
    /* (s) string,  (d) double,  (i) int, (X)* optional, (s|d|i), string or double or int*/
    using gmlc::utilities::stringOps::delimiter_compression;
    constexpr std::string_view whitespace = " \t\r\n";
    const auto countTokens = [](const auto& tokens) {
        size_t count = 0;
        while (count < tokens.size() && !tokens[count].starts_with('#')) {
            ++count;
        }
        return count;
    };
    auto ssep =
        gmlc::utilities::stringOps::splitline(operation, whitespace, delimiter_compression::on);
    auto tokenCount = countTokens(ssep);
    reset();
    if (tokenCount == 0) {
        // check if there was no command
        return;
    }
    const std::string commandToken = gmlc::utilities::convertToLowerCase(ssep[0]);
    if (commandToken == "save" || commandToken == "load" || commandToken == "checkpoint" ||
        commandToken == "rollback" || commandToken == "continuation" || commandToken == "action" ||
        commandToken == "get") {
        ssep = gmlc::utilities::stringOps::splitlineQuotes(operation,
                                                           whitespace,
                                                           "\"",
                                                           delimiter_compression::on);
        tokenCount = countTokens(ssep);
    }
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
    } else if (commandToken == "get") {
        if (tokenCount < 2 || tokenCount > 3) {
            throw InvalidParameterValue("get expects a selector and an optional output file");
        }
        command = GdAction::GET_ACTION;
        flag = ssep[1] == "--help" ? 1 : 0;
        if (flag == 1) {
            if (tokenCount == 3) {
                string1 = quotedActionOperand(ssep[2], "object selector");
            }
        } else {
            string1 = quotedActionOperand(ssep[1], "field selector");
            if (tokenCount == 3) {
                string2 = actionFileName(ssep[2]);
            }
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
            auto testInt = gmlc::utilities::numeric_conversion<int>(ssep[1], -435);
            if (testInt == -435) {
                throw(InvalidParameterValue(commandToken));
            }
            val_int1 = testInt;
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
    } else if (commandToken == "save" || commandToken == "load") {
        if (tokenCount != 3) {
            throw(InvalidParameterValue(commandToken));
        }
        command = (commandToken == "save") ? GdAction::SAVE : GdAction::LOAD;
        string1 = ssep[1];
        string2 = actionFileName(ssep[2]);
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
    } else if (commandToken == "checkpoint" || commandToken == "rollback") {
        if (tokenCount < 2 || tokenCount > 3) {
            throw(InvalidParameterValue(commandToken));
        }
        command = (commandToken == "checkpoint") ? GdAction::CHECKPOINT : GdAction::ROLLBACK;
        string1 = actionFileName(ssep[1]);
        if (tokenCount == 3) {
            string2 = ssep[2];
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
        if (tokenCount < 2) {
            throw InvalidParameterValue("continuation load scale");
        }
        command = GdAction::CONTINUATION;
        val_double = nonnegativeActionNumber(ssep[1], "continuation load scale");
        for (size_t pos = 2; pos < tokenCount; ++pos) {
            const auto& option = ssep[pos];
            if (option == "qlimits" && flag == 0) {
                flag = 1;
            } else if (option == "target" && pos + 1 < tokenCount && val_double2 == kNullVal) {
                val_double2 = nonnegativeActionNumber(ssep[++pos], "continuation target lambda");
            } else if (option == "archive" && pos + 1 < tokenCount && string1.empty()) {
                string1 = actionFileName(ssep[++pos]);
            } else if (option == "monitor" && pos + 1 < tokenCount && monitorBuses.empty()) {
                for (const auto& bus : gmlc::utilities::stringOps::splitline(ssep[++pos], ',')) {
                    monitorBuses.push_back(monitorBusId(bus));
                }
            } else {
                throw InvalidParameterValue("continuation option " + option);
            }
        }
    } else if (commandToken == "action") {
        if (tokenCount != 2) {
            throw InvalidParameterValue("action script file");
        }
        command = GdAction::ACTION_FILE;
        string1 = actionFileName(ssep[1]);
    } else if (commandToken == "opf") {
        command = GdAction::OPF;
        std::size_t pos = 1;
        if (pos < tokenCount && (ssep[pos] == "native" || ssep[pos] == "highs")) {
            string1 = ssep[pos++];
        }
        if (pos < tokenCount && ssep[pos] == "preview") {
            flag = 1;
            ++pos;
        }
        if (pos != tokenCount) {
            throw InvalidParameterValue("opf expects native or highs followed by optional preview");
        }
    } else {
        throw(UnrecognizedParameter(commandToken));
    }
}

}  // namespace griddyn
