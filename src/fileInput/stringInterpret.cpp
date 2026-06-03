/*
 * Copyright (c) 2014-2026, Lawrence Livermore National Security
 * See the top-level NOTICE for additional details. All rights reserved.
 * SPDX-License-Identifier: BSD-3-Clause
 */

#include "core/ObjectInterpreter.h"
#include "fileInput.h"
#include "gmlc/utilities/stringConversion.h"
#include "gmlc/utilities/string_viewConversion.h"
#include "utilities/functionInterpreter.h"
#include <cctype>
#include <cmath>
#include <cstdio>
#include <limits>
#include <print>
#include <string>
#include <string_view>

namespace griddyn {
using gmlc::utilities::string_viewOps::splitlineBracket;
using gmlc::utilities::string_viewOps::trim;
using std::string_view;

namespace {

    double interpretStringBlock(string_view command, ReaderInfo& readerInfo);

    double addSubStringBlocks(string_view command, ReaderInfo& readerInfo, size_t rlc);
    double multDivStringBlocks(string_view command, ReaderInfo& readerInfo, size_t rlc);
    size_t pChunckEnd(string_view command, size_t start);

    double interpretFunction(string_view command, ReaderInfo& readerInfo);
    double interpretFunction(string_view command, double val, ReaderInfo& readerInfo);
    double interpretFunction(string_view command, double val1, double val2, ReaderInfo& readerInfo);

    double stringBlocktoDouble(string_view block, ReaderInfo& readerInfo);
    double interpretStringSv(string_view command, ReaderInfo& readerInfo);

    double objectQuery(string_view command, CoreObject* object);

    // The expression parser intentionally uses recursive descent for nested expressions.
    // NOLINTBEGIN(misc-no-recursion)
    double interpretStringSv(string_view command, ReaderInfo& readerInfo)
    {
        // check for functions
        auto rlcps = command.find_first_of('(');
        size_t rlcp{0};
        if (rlcps != std::string::npos) {
            rlcp = pChunckEnd(command, rlcps + 1);
            if (rlcp == std::string::npos) {
                rlcp = command.length();
            }
        }
        const auto addOpBeforeParentheses = command.find_first_of("+-", 1);
        const auto multOpBeforeParentheses = command.find_first_of("*/^%", 1);
        const auto addOpAfterParentheses = command.find_first_of("+-", rlcp + 1);
        const auto multOpAfterParentheses = command.find_first_of("*/^%", rlcp + 1);

        double val{0.0};
        if ((rlcps == 0) && (rlcp == command.length() - 1)) {
            // just remove outer parenthesis and call again
            val = interpretStringSv(command.substr(1, rlcp - 1), readerInfo);
        } else if ((addOpBeforeParentheses != std::string::npos) &&
                   (addOpBeforeParentheses < rlcps)) {
            val = addSubStringBlocks(command, readerInfo, addOpBeforeParentheses);
        } else if ((multOpBeforeParentheses != std::string::npos) &&
                   (multOpBeforeParentheses < rlcps)) {
            val = multDivStringBlocks(command, readerInfo, multOpBeforeParentheses);
        } else if (addOpAfterParentheses != std::string::npos) {
            val = addSubStringBlocks(command, readerInfo, addOpAfterParentheses);
        } else if (multOpAfterParentheses != std::string::npos) {
            val = multDivStringBlocks(command, readerInfo, multOpAfterParentheses);
        } else {
            if (rlcps != std::string::npos) {
                auto cmdBlock = command.substr(0, rlcps);

                auto fcallstr = trim(command.substr(rlcps + 1, rlcp - rlcps - 1));
                if (fcallstr.empty()) {
                    val = interpretFunction(cmdBlock, readerInfo);
                } else {
                    auto cloc = fcallstr.find_first_of(',');
                    if (cloc != std::string::npos) {
                        auto args = splitlineBracket(fcallstr, ",");
                        trim(args);
                        if (args.size() == 2) {
                            const double value1 = stringBlocktoDouble(args[0], readerInfo);
                            const double value2 = stringBlocktoDouble(args[1], readerInfo);
                            val = interpretFunction(cmdBlock, value1, value2, readerInfo);
                        } else if (args.size() == 1) {
                            // if the single argument is a function of multiple arguments
                            if (cmdBlock == "query") {
                                val = objectQuery(args[0], readerInfo.getKeyObject());
                            } else {
                                const double value1 = stringBlocktoDouble(args[0], readerInfo);
                                val = interpretFunction(cmdBlock, value1, readerInfo);
                            }
                        } else {
                            std::println(stderr,
                                         "invalid arguments to function {}",
                                         std::string{cmdBlock});
                        }
                    } else {
                        if (cmdBlock == "query") {
                            val = objectQuery(fcallstr, readerInfo.getKeyObject());
                        } else {
                            val = stringBlocktoDouble(fcallstr, readerInfo);

                            if (!std::isnan(val)) {
                                val = interpretFunction(cmdBlock, val, readerInfo);
                            }
                        }
                    }
                }
            } else {
                val = interpretStringBlock(command, readerInfo);
            }
        }

        return val;
    }

    double interpretStringBlock(string_view command, ReaderInfo& readerInfo)
    {
        auto val = gmlc::utilities::numeric_conversionComplete<double>(command, std::nan("0"));
        if (std::isnan(val)) {
            const std::string ncommand = readerInfo.checkDefines(std::string{command});
            // iterate the process until the variable is no longer modified and still fails
            // conversion to numerical
            if (command != ncommand) {
                val = gmlc::utilities::numeric_conversionComplete<double>(ncommand, std::nan("0"));
                if (std::isnan(val)) {
                    val = interpretStringSv(ncommand, readerInfo);
                }
            }
        }
        return val;
    }

    double addSubStringBlocks(string_view command, ReaderInfo& readerInfo, size_t rlc)
    {
        const char operation = command[rlc];

        // check if either Ablock or Bclock is a constant
        auto ablock = trim(command.substr(0, rlc));
        const double valA = (ablock.empty()) ? 0.0 : stringBlocktoDouble(ablock, readerInfo);

        auto bblock = trim(command.substr(rlc + 1, std::string_view::npos));
        const double valB = stringBlocktoDouble(bblock, readerInfo);

        return (operation == '+') ? valA + valB : valA - valB;
    }

    constexpr double nanVal = std::numeric_limits<double>::quiet_NaN();

    double multDivStringBlocks(string_view command, ReaderInfo& readerInfo, size_t rlc)
    {
        const char operation = command[rlc];

        auto ablock = trim(command.substr(0, rlc));
        const double valA = stringBlocktoDouble(ablock, readerInfo);

        // load the second half of the multiplication
        auto bblock = trim(command.substr(rlc + 1, std::string_view::npos));
        const double valB = stringBlocktoDouble(bblock, readerInfo);

        double val;
        switch (operation) {
            case '*':
                val = valA * valB;
                break;
            case '%':
                val = (valB == 0.0) ? nanVal : fmod(valA, valB);
                break;
            case '/':
                val = (valB == 0.0) ? nanVal : valA / valB;
                break;
            case '^':
                val = pow(valA, valB);
                break;
            default:
                // this shouldn't happen
                val = nanVal;
        }
        return val;
    }

    size_t pChunckEnd(string_view command, size_t start)
    {
        int open = 1;
        size_t rlc = start - 1;
        while (open > 0) {
            rlc = command.find_first_of("()", rlc + 1);
            if (rlc == std::string_view::npos) {
                break;
            }
            open += (command[rlc] == '(') ? 1 : -1;
        }
        return rlc;
    }

    double interpretFunction(string_view command, ReaderInfo& readerInfo)
    {
        auto fval = evalFunction(command);

        // if we still didn't find any function check if there is a redefinition
        if (std::isnan(fval)) {
            auto replacement = readerInfo.checkDefines(std::string{command});
            if (replacement != command) {
                fval = evalFunction(replacement);  // don't let it iterate more than once
            }
        }
        return fval;
    }

    double interpretFunction(string_view command, double val, ReaderInfo& readerInfo)
    {
        auto fval = evalFunction(command, val);

        // if we still didn't find any function check if there is a redefinition
        if (std::isnan(fval)) {
            auto replacement = readerInfo.checkDefines(std::string{command});
            if (replacement != command) {
                fval = evalFunction(replacement, val);  // don't let it iterate more than once
            }
        }
        return fval;
    }

    double interpretFunction(string_view command, double val1, double val2, ReaderInfo& readerInfo)
    {
        auto fval = evalFunction(command, val1, val2);

        // if we still didn't find any function check if there is a redefinition
        if (std::isnan(fval)) {
            auto replacement = readerInfo.checkDefines(std::string{command});
            if (replacement != command) {
                fval =
                    evalFunction(replacement, val1, val2);  // don't let it iterate more than once
            }
        }
        return fval;
    }

    double objectQuery(string_view command, CoreObject* object)
    {
        if (object == nullptr) {
            return nanVal;
        }
        const ObjectInfo query(std::string{command}, object);
        if (!query.mField.empty()) {
            const double val = query.mObject->get(query.mField, query.mUnitType);
            return val;
        }
        return nanVal;
    }

    double stringBlocktoDouble(string_view block, ReaderInfo& readerInfo)
    {
        // if the first character is not a digit then go to the string interpreter
        if (gmlc::utilities::nonNumericFirstCharacter(block)) {
            return interpretStringSv(block, readerInfo);
        }
        try {
            size_t mpos;
            double valA = gmlc::utilities::numConvComp<double>(block, mpos);
            if (mpos < block.length()) {
                valA = interpretStringSv(block, readerInfo);
            }
            return valA;
        }
        catch (std::invalid_argument&) {
            return interpretStringSv(block, readerInfo);
        }
    }
    // NOLINTEND(misc-no-recursion)

}  // namespace

// Declared in readerHelper.h and used across file-input translation units.
// NOLINTNEXTLINE(misc-use-internal-linkage)
double interpretString(const std::string& command, ReaderInfo& readerInfo)
{
    return interpretStringSv(command, readerInfo);
}

}  // namespace griddyn
