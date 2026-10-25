/*
 * Copyright (c) 2014-2026, Lawrence Livermore National Security
 * See the top-level NOTICE for additional details. All rights reserved.
 * SPDX-License-Identifier: BSD-3-Clause
 */

#pragma once

#include "MeasurementMath.hpp"
#include "core/ObjectInterpreter.h"
#include "gmlc/utilities/stringConversion.h"
#include "gmlc/utilities/string_viewConversion.h"
#include "utilities/functionInterpreter.h"
#include <algorithm>
#include <functional>
#include <memory>
#include <string>
#include <string_view>
#include <type_traits>
#include <utility>

namespace griddyn {
/** Split only at top-level separators, preserving empty arguments for validation. */
inline std::vector<std::string> splitGrabberList(std::string_view text,
                                                 std::string_view separators = ",;")
{
    std::vector<std::string> result;
    int depth = 0;
    size_t start = 0;
    for (size_t index = 0; index < text.size(); ++index) {
        if (std::string_view{"([{"}.contains(text[index])) {
            ++depth;
        } else if (std::string_view{")]}"}.contains(text[index])) {
            --depth;
        } else if (depth == 0 && separators.contains(text[index])) {
            result.emplace_back(text.substr(start, index - start));
            start = index + 1;
        }
    }
    result.emplace_back(text.substr(start));
    return result;
}

/** Shared expression grammar for object and solver-state grabbers. */
template<class Base, class Operation, class Function>
class GrabberInterpreter {
    static_assert(std::is_base_of_v<Base, Operation> && std::is_base_of_v<Base, Function>);
    using Grabber = std::unique_ptr<Base>;
    std::function<Grabber(std::string_view, CoreObject*)> mCreateGrabber;

  public:
    explicit GrabberInterpreter(std::function<Grabber(std::string_view, CoreObject*)> factory):
        mCreateGrabber(std::move(factory))
    {
    }

    // Recursive descent follows the nesting and precedence of the expression.
    // NOLINTNEXTLINE(misc-no-recursion)
    Grabber interpretGrabberBlock(std::string_view command, CoreObject* obj)
    {
        command = trim(command);
        if (command.empty() || !balanced(command)) {
            return nullptr;
        }
        const double number = gmlc::utilities::numeric_conversionComplete(command, kNullVal);
        if (number != kNullVal) {
            auto result = mCreateGrabber("constant", obj);
            if (result) {
                result->bias = number;
            }
            return result;
        }

        // Split at the last operator for left-associative +, -, *, / and %.
        // URI paths use '/' as a separator and retain '%' as a division alias.
        const bool uri = command.contains('?');
        for (const auto operators :
             {std::string_view{"+-"}, uri ? std::string_view{"*%"} : std::string_view{"*/%"}}) {
            const auto position = findOperator(command, operators, false);
            if (position != std::string_view::npos) {
                const std::string operation(1,
                                            uri && command[position] == '%' ? '/' :
                                                                              command[position]);
                return combine(interpretGrabberBlock(command.substr(0, position), obj),
                               interpretGrabberBlock(command.substr(position + 1), obj),
                               operation);
            }
        }
        // Exponentiation binds more tightly than a leading sign.
        if (command.front() == '+' || command.front() == '-') {
            auto result = interpretGrabberBlock(command.substr(1), obj);
            if (result && command.front() == '-') {
                result->gain = -result->gain;
                result->bias = -result->bias;
            }
            return result;
        }
        const auto power = findOperator(command, "^", true);
        if (power != std::string_view::npos) {
            return combine(interpretGrabberBlock(command.substr(0, power), obj),
                           interpretGrabberBlock(command.substr(power + 1), obj),
                           "^");
        }

        const auto opening = command.find('(');
        if (opening == 0 && matchingClose(command, opening) == command.size() - 1) {
            return interpretGrabberBlock(command.substr(1, command.size() - 2), obj);
        }
        if (opening != std::string_view::npos) {
            const auto name =
                gmlc::utilities::convertToLowerCase(std::string{trim(command.substr(0, opening))});
            const auto closing = matchingClose(command, opening);
            if (isFunctionName(name) && closing == command.size() - 1) {
                if (trim(command.substr(opening + 1, closing - opening - 1)).empty()) {
                    if (auto function = get0ArgFunction(name)) {
                        if (isPureFunction(name)) {
                            auto result = mCreateGrabber("constant", obj);
                            if (result) {
                                result->bias = function();
                            }
                            return result;
                        }
                        return std::make_unique<Operation>(std::vector<Grabber>{}, name);
                    }
                    return nullptr;
                }
                const auto arguments =
                    splitGrabberList(command.substr(opening + 1, closing - opening - 1), ",");
                if (arguments.size() == 1) {
                    auto argument = interpretGrabberBlock(arguments.front(), obj);
                    if (argument && argument->field == "constant" && isPureFunction(name)) {
                        if (auto function = get1ArgFunction(name)) {
                            argument->bias = function(argument->bias);
                            return argument;
                        }
                        if (auto function = getArrayFunction(name)) {
                            argument->bias = function(std::vector<double>{argument->bias});
                            return argument;
                        }
                    }
                    return argument ?
                        std::make_unique<Function>(std::move(argument), std::string{name}) :
                        nullptr;
                }
                if (arguments.size() == 2) {
                    return combine(interpretGrabberBlock(arguments[0], obj),
                                   interpretGrabberBlock(arguments[1], obj),
                                   std::string{name});
                }
                if (arguments.size() == 3 && get3ArgFunction(name) != nullptr) {
                    std::vector<Grabber> operands;
                    for (const auto& argument : arguments) {
                        auto operand = interpretGrabberBlock(argument, obj);
                        if (!operand) {
                            return nullptr;
                        }
                        operands.push_back(std::move(operand));
                    }
                    if (std::all_of(operands.begin(),
                                    operands.end(),
                                    [](const auto& operand) {
                                        return operand->field == "constant";
                                    }) &&
                        isPureFunction(name)) {
                        operands[0]->bias = measurement::ternaryValue(name,
                                                                      get3ArgFunction(name),
                                                                      operands[0]->bias,
                                                                      operands[1]->bias,
                                                                      operands[2]->bias);
                        return std::move(operands[0]);
                    }
                    return std::make_unique<Operation>(std::move(operands), name);
                }
                return nullptr;
            }
        }
        return singleField(command, obj);
    }

  private:
    static std::string_view trim(std::string_view text)
    {
        const auto first = text.find_first_not_of(" \t\r\n");
        if (first == std::string_view::npos) {
            return {};
        }
        return text.substr(first, text.find_last_not_of(" \t\r\n") - first + 1);
    }

    static bool balanced(std::string_view text)
    {
        int depth = 0;
        for (const char character : text) {
            if (character == '(') {
                ++depth;
            } else if (character == ')' && --depth < 0) {
                return false;
            }
        }
        return depth == 0;
    }

    static size_t matchingClose(std::string_view text, size_t opening)
    {
        int depth = 0;
        for (size_t index = opening; index < text.size(); ++index) {
            if (text[index] == '(') {
                ++depth;
            } else if (text[index] == ')' && --depth == 0) {
                return index;
            }
        }
        return std::string_view::npos;
    }

    static size_t findOperator(std::string_view text, std::string_view operators, bool first)
    {
        int depth = 0;
        size_t result = std::string_view::npos;
        for (size_t index = 0; index < text.size(); ++index) {
            const char character = text[index];
            if (character == '(') {
                ++depth;
            } else if (character == ')') {
                --depth;
            } else if (depth == 0 && operators.contains(character)) {
                const auto previous = text.substr(0, index).find_last_not_of(" \t");
                if (previous == std::string_view::npos ||
                    std::string_view{"+-*/%^,"}.contains(text[previous])) {
                    continue;  // unary sign
                }
                if ((character == '+' || character == '-') && previous > 0 &&
                    (text[previous] == 'e' || text[previous] == 'E') &&
                    std::string_view{"0123456789."}.contains(text[previous - 1])) {
                    continue;  // exponent sign in a numeric literal
                }
                result = index;
                if (first) {
                    break;
                }
            }
        }
        return result;
    }

    static Grabber combine(Grabber left, Grabber right, const std::string& operation)
    {
        if (!left || !right) {
            return nullptr;
        }
        if (left->field == "constant" && right->field == "constant" && isPureFunction(operation)) {
            if (auto function = get2ArgFunction(operation)) {
                left->bias = function(left->bias, right->bias);
                return left;
            }
        }
        // Keep affine expressions cheap while composing existing gain/bias.
        if (right->field == "constant") {
            if (operation == "+" || operation == "-") {
                left->bias += operation == "+" ? right->bias : -right->bias;
                return left;
            }
            if (operation == "*" || operation == "/") {
                const double scale = operation == "*" ? right->bias : 1.0 / right->bias;
                left->gain *= scale;
                left->bias *= scale;
                return left;
            }
        }
        if (left->field == "constant" &&
            (operation == "+" || operation == "-" || operation == "*")) {
            if (operation == "*") {
                right->gain *= left->bias;
                right->bias *= left->bias;
            } else if (operation == "+") {
                right->bias += left->bias;
            } else {
                right->gain = -right->gain;
                right->bias = left->bias - right->bias;
            }
            return right;
        }
        return std::make_unique<Operation>(std::move(left), std::move(right), operation);
    }

    Grabber singleField(std::string_view command, CoreObject* obj)
    {
        const auto separator = command.find_last_of(":?");
        if (separator != std::string_view::npos) {
            if (obj == nullptr || separator == 0 || separator == command.size() - 1) {
                return nullptr;
            }
            obj = locateObject(command, obj);
            if (obj == nullptr) {
                return nullptr;
            }
            command = command.substr(separator + 1);
        }
        units::unit outputUnits = units::defunit;
        const auto opening = command.find('(');
        if (opening != std::string_view::npos) {
            const auto closing = matchingClose(command, opening);
            if (closing != command.size() - 1) {
                return nullptr;
            }
            outputUnits = units::unit_cast_from_string(
                std::string{command.substr(opening + 1, closing - opening - 1)});
            command = trim(command.substr(0, opening));
        }
        auto result =
            mCreateGrabber(gmlc::utilities::convertToLowerCase(std::string{command}), obj);
        if (result) {
            result->outputUnits = outputUnits;
        }
        return result;
    }
};
}  // namespace griddyn
