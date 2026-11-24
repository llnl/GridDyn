/*
 * Copyright (c) 2014-2026, Lawrence Livermore National Security
 * See the top-level NOTICE for additional details. All rights reserved.
 * SPDX-License-Identifier: BSD-3-Clause
 */

#include "GridDynActions.h"

#include "core/CoreExceptions.h"
#include "gmlc/utilities/stringConversion.h"
#include <algorithm>
#include <array>
#include <charconv>
#include <cmath>
#include <span>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

namespace griddyn {

namespace {
    constexpr std::string_view whitespace = " \t\r\n\v\f";

    bool isSpace(char character)
    {
        return whitespace.contains(character);
    }

    bool isQuotedEscape(std::string_view input, std::size_t pos)
    {
        return input[pos] == '\\' && pos + 1 < input.size() &&
            (input[pos + 1] == '"' || input[pos + 1] == '\\');
    }

    struct ActionToken {
        std::string mValue;
        bool mQuoted = false;
    };

    std::vector<ActionToken> tokenize(std::string_view input)
    {
        std::vector<ActionToken> tokens;
        auto pos = input.find_first_not_of(whitespace);
        while (pos != std::string_view::npos && input[pos] != '#') {
            auto& token = tokens.emplace_back();
            token.mQuoted = input[pos] == '"';
            if (token.mQuoted) {
                ++pos;
                while (pos < input.size() && input[pos] != '"') {
                    if (isQuotedEscape(input, pos)) {
                        ++pos;
                    }
                    token.mValue += input[pos++];
                }
                if (pos == input.size()) {
                    throw InvalidParameterValue("quoted action operand");
                }
                ++pos;
                if (pos < input.size() && !isSpace(input[pos])) {
                    throw InvalidParameterValue("quoted action operand");
                }
            } else {
                const auto end = input.find_first_of(whitespace, pos);
                const auto word = input.substr(pos, end - pos);
                if (word.find_first_of("\";") != std::string_view::npos) {
                    throw InvalidParameterValue("action operand");
                }
                token.mValue = word;
                pos = end;
            }
            if (pos != std::string_view::npos) {
                pos = input.find_first_not_of(whitespace, pos);
            }
        }
        return tokens;
    }

    std::string requiredOperand(const ActionToken& token, std::string_view name)
    {
        if (token.mValue.empty()) {
            throw InvalidParameterValue(name);
        }
        return token.mValue;
    }

    double optionalNumber(const ActionToken& token, std::string_view name)
    {
        if (token.mQuoted) {
            return kNullVal;
        }
        std::size_t end = 0;
        double value = 0.0;
        try {
            value = std::stod(token.mValue, &end);
        }
        catch (const std::out_of_range&) {
            throw InvalidParameterValue(name);
        }
        catch (const std::invalid_argument&) {
            return kNullVal;
        }
        if (end != token.mValue.size()) {
            return kNullVal;
        }
        if (!std::isfinite(value) || value == kNullVal) {
            throw InvalidParameterValue(name);
        }
        return value;
    }

    double actionNumber(const ActionToken& token, std::string_view name)
    {
        const auto value = optionalNumber(token, name);
        if (value == kNullVal) {
            throw InvalidParameterValue(name);
        }
        return value;
    }

    double actionTime(const ActionToken& token, std::string_view name, bool interval = false)
    {
        const auto value = actionNumber(token, name);
        if (value <= static_cast<double>(negTime) || value >= static_cast<double>(maxTime) ||
            (interval && CoreTime(value) <= timeZero)) {
            throw InvalidParameterValue(name);
        }
        return value;
    }

    double nonnegativeActionNumber(const ActionToken& token, std::string_view name)
    {
        const auto value = actionNumber(token, name);
        if (value < 0.0) {
            throw InvalidParameterValue(name);
        }
        return value;
    }

    template<class Integer>
    Integer actionInteger(std::string_view token, std::string_view name)
    {
        if (token.starts_with('+') && token.size() > 1 && !token.substr(1).starts_with('-')) {
            token.remove_prefix(1);
        }
        Integer value = 0;
        const auto* begin = token.data();
        const auto* end = begin + token.size();
        const auto result = std::from_chars(begin, end, value);
        if (result.ec != std::errc{} || result.ptr != end) {
            throw InvalidParameterValue(name);
        }
        return value;
    }

    constexpr auto actionNames = std::to_array<std::string_view>({"ignore",
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
                                                                  "get"});
    using Action = GridDynAction::GdAction;
    static_assert(actionNames.size() == static_cast<size_t>(Action::GET_ACTION) + 1);

    void parseContinuation(GridDynAction& action, std::span<const ActionToken> operands)
    {
        action.val_double = nonnegativeActionNumber(operands.front(), "continuation load scale");
        for (std::size_t pos = 1; pos < operands.size(); ++pos) {
            const auto option = gmlc::utilities::convertToLowerCase(operands[pos].mValue);
            const bool hasValue = pos + 1 < operands.size();
            if (option == "qlimits" && action.flag == 0) {
                action.flag = 1;
            } else if (option == "target" && hasValue && action.val_double2 == kNullVal) {
                action.val_double2 =
                    nonnegativeActionNumber(operands[++pos], "continuation target lambda");
            } else if (option == "archive" && hasValue && action.string1.empty()) {
                action.string1 = requiredOperand(operands[++pos], "file name");
            } else if (option == "monitor" && hasValue && action.monitorBuses.empty()) {
                const auto& ids = operands[++pos].mValue;
                if (ids.empty() || ids.starts_with(',') || ids.ends_with(',') ||
                    ids.contains(",,")) {
                    throw InvalidParameterValue("continuation monitor bus ID");
                }
                for (const auto& bus : gmlc::utilities::stringOps::splitline(ids, ',')) {
                    if (bus.starts_with('-')) {
                        throw InvalidParameterValue("continuation monitor bus ID");
                    }
                    action.monitorBuses.push_back(
                        actionInteger<index_t>(bus, "continuation monitor bus ID"));
                }
            } else {
                throw InvalidParameterValue("continuation option " + option);
            }
        }
    }
}  // namespace

std::vector<std::string_view> action_detail::splitLine(std::string_view line)
{
    std::vector<std::string_view> commands;
    bool quoted = false;
    std::size_t start = 0;
    std::size_t pos = 0;
    for (; pos < line.size(); ++pos) {
        if (quoted && isQuotedEscape(line, pos)) {
            ++pos;
        } else if (line[pos] == '"') {
            quoted = !quoted;
        } else if (!quoted) {
            if (line[pos] == '#' && (pos == start || isSpace(line[pos - 1]))) {
                break;
            }
            if (line[pos] == ';') {
                commands.push_back(line.substr(start, pos - start));
                start = pos + 1;
            }
        }
    }
    commands.push_back(line.substr(start, pos - start));
    return commands;
}

GridDynAction::GridDynAction(GdAction action) noexcept: command(action) {}

GridDynAction::GridDynAction(std::string_view operation)
{
    process(operation);
}

std::string_view GridDynAction::commandName() const noexcept
{
    const auto index = static_cast<std::size_t>(command);
    return index < actionNames.size() ? actionNames[index] : "invalid";
}

void GridDynAction::reset()
{
    command = GdAction::IGNORE_ACTION;
    string1.clear();
    string2.clear();
    setting.clear();
    val_double = kNullVal;
    val_double2 = kNullVal;
    val_int1 = -1;
    val_int2 = -1;
    flag = 0;
    monitorBuses.clear();
}

void GridDynAction::process(std::string_view operation)
{
    reset();
    const auto tokens = tokenize(operation);
    if (tokens.empty()) {
        return;
    }
    const auto commandToken = gmlc::utilities::convertToLowerCase(tokens.front().mValue);
    const auto index = static_cast<std::size_t>(std::ranges::find(actionNames, commandToken) -
                                                actionNames.begin());
    if (tokens.front().mQuoted || index == actionNames.size() || commandToken == "invalid") {
        throw UnrecognizedParameter(commandToken);
    }
    command = static_cast<GdAction>(index);
    auto args = std::span<const ActionToken>{tokens}.subspan(1);
    const auto arity = [&](std::size_t minimum, std::size_t maximum) {
        if (args.size() < minimum || args.size() > maximum) {
            throw InvalidParameterValue(commandToken);
        }
    };
    const auto parseStopAndInterval = [&](std::size_t maxArgs) {
        arity(0, maxArgs);
        if (!args.empty()) {
            val_double = actionTime(args[0], "stop time");
        }
        if (args.size() > 1) {
            val_double2 = actionTime(args[1], "interval", true);
        }
    };
    const auto keyword = [&](std::size_t index) {
        return gmlc::utilities::convertToLowerCase(args[index].mValue);
    };

    switch (command) {
        case GdAction::IGNORE_ACTION:
            break;
        case GdAction::POWERFLOW:
            arity(0, 0);
            break;
        case GdAction::SET_ACTION:
            arity(2, 2);
            string1 = requiredOperand(args[0], "field selector");
            val_double = optionalNumber(args[1], "set value");
            if (val_double == kNullVal) {
                string2 = args[1].mValue;
            }
            break;
        case GdAction::GET_ACTION:
            arity(1, 2);
            if (!args[0].mQuoted && keyword(0) == "--help") {
                flag = 1;
                if (args.size() == 2) {
                    string1 = requiredOperand(args[1], "object selector");
                }
            } else {
                string1 = requiredOperand(args[0], "field selector");
                if (args.size() == 2) {
                    string2 = requiredOperand(args[1], "file name");
                }
            }
            break;
        case GdAction::SETALL:
            arity(3, 3);
            string1 = keyword(0);
            string2 = requiredOperand(args[1], "field selector");
            val_double = actionNumber(args[2], "setall value");
            break;
        case GdAction::SETSOLVER:
            arity(2, 3);
            string1 = requiredOperand(args[0], "solver or mode");
            string2 = requiredOperand(args[1], "solver or field");
            if (args.size() == 3) {
                flag = 1;
                val_double = optionalNumber(args[2], "solver setting");
                if (val_double == kNullVal) {
                    setting = args[2].mValue;
                }
            }
            break;
        case GdAction::PRINT:
            arity(2, 2);
            string1 = args[0].mValue;
            string2 = args[1].mValue;
            break;
        case GdAction::INITIALIZE:
        case GdAction::RUN:
        case GdAction::DYNAMIC_DAE:
            parseStopAndInterval(1);
            break;
        case GdAction::DYNAMIC_DEFAULT:
            if (!args.empty()) {
                const auto mode = keyword(0);
                if (mode == "dae") {
                    command = GdAction::DYNAMIC_DAE;
                } else if (mode == "part" || mode == "partitioned") {
                    command = GdAction::DYNAMIC_PART;
                } else if (mode == "decoupled") {
                    command = GdAction::DYNAMIC_DECOUPLED;
                }
                if (command != GdAction::DYNAMIC_DEFAULT) {
                    args = args.subspan(1);
                }
            }
            parseStopAndInterval(command == GdAction::DYNAMIC_DAE ? 1 : 2);
            break;
        case GdAction::DYNAMIC_PART:
        case GdAction::DYNAMIC_DECOUPLED:
        case GdAction::EVENTMODE:
            parseStopAndInterval(2);
            break;
        case GdAction::ITERATE:
            arity(0, 2);
            if (!args.empty()) {
                val_double = actionTime(args[0], "interval", true);
            }
            if (args.size() == 2) {
                val_double2 = actionTime(args[1], "stop time");
            }
            break;
        case GdAction::STEP:
            if (!args.empty() && keyword(0) == "dae") {
                string1 = "dae";
                args = args.subspan(1);
            }
            arity(0, 1);
            if (!args.empty()) {
                val_double = actionTime(args[0], "step interval", true);
            }
            break;
        case GdAction::RESET:
            arity(0, 1);
            val_int1 = args.empty() ? 0 : actionInteger<int>(args[0].mValue, "reset level");
            if (val_int1 < 0 || val_int1 > 4) {
                throw InvalidParameterValue("reset level (0 through 4)");
            }
            break;
        case GdAction::CHECK:
            arity(1, 3);
            string1 = keyword(0);
            args = args.subspan(1);
            if (!args.empty() && !args[0].mQuoted && keyword(0) == "--assert") {
                flag = 1;
                args = args.subspan(1);
            }
            arity(0, 1);
            if (!args.empty()) {
                string2 = requiredOperand(args[0], "file name");
            }
            break;
        case GdAction::SAVE:
        case GdAction::LOAD:
            arity(1, 2);
            string1 = keyword(0);
            if (command == GdAction::LOAD || string1 == "powerflow-checkpoint") {
                arity(2, 2);
            }
            if (args.size() == 2) {
                string2 = requiredOperand(args[1], "file name");
            }
            break;
        case GdAction::ADD:
            arity(1, tokens.size());
            string1 = keyword(0);
            if (string1 == "event") {
                arity(2, tokens.size());
                string2 = requiredOperand(args[1], "event description");
                for (const auto& token : args.subspan(2)) {
                    string2.append(" ").append(token.mValue);
                }
            }
            break;
        case GdAction::CHECKPOINT:
        case GdAction::ROLLBACK:
            arity(1, 2);
            string1 = requiredOperand(args[0], "file name");
            if (args.size() == 2) {
                string2 = keyword(1);
            }
            break;
        case GdAction::CONTINGENCY:
            if (!args.empty() && keyword(0) == "simplified") {
                flag = 1;
                args = args.subspan(1);
            }
            arity(1, 4);
            string1 = requiredOperand(args[0], "contingency mode");
            if (args.size() > 1) {
                string2 = requiredOperand(args[1], "contingency output");
            }
            if (args.size() > 2) {
                val_int1 = actionInteger<int>(args[2].mValue, "contingency start");
                if (val_int1 < 0 || (args.size() == 3 && val_int1 == 0)) {
                    throw InvalidParameterValue(args.size() == 3 ? "contingency count" :
                                                                   "contingency start");
                }
            }
            if (args.size() > 3) {
                val_int2 = actionInteger<int>(args[3].mValue, "contingency count");
                if (val_int2 <= 0) {
                    throw InvalidParameterValue("contingency count");
                }
            }
            break;
        case GdAction::CONTINUATION:
            if (args.empty()) {
                throw InvalidParameterValue("continuation load scale");
            }
            parseContinuation(*this, args);
            break;
        case GdAction::ACTION_FILE:
            arity(1, 1);
            string1 = requiredOperand(args[0], "action script file");
            break;
        case GdAction::OPF:
            if (!args.empty() && (keyword(0) == "native" || keyword(0) == "highs")) {
                string1 = keyword(0);
                args = args.subspan(1);
            }
            if (!args.empty() && keyword(0) == "preview") {
                flag = 1;
                args = args.subspan(1);
            }
            arity(0, 0);
            break;
        case GdAction::INVALID:
            break;
    }
}

}  // namespace griddyn
