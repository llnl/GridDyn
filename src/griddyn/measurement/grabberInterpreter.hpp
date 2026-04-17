/*
 * Copyright (c) 2014-2026, Lawrence Livermore National Security
 * See the top-level NOTICE for additional details. All rights reserved.
 * SPDX-License-Identifier: BSD-3-Clause
 */
#pragma once

#include "core/objectInterpreter.h"
#include "gmlc/utilities/stringConversion.h"
#include "utilities/functionInterpreter.h"
#include <cmath>
#include <functional>
#include <memory>
#include <string>
#include <type_traits>
#include <utility>
#include <vector>

namespace griddyn {
const char mdiv1String[] = "*/^";
const char mdiv2String[] = "*%^";
const char psubString[] = "+-";

bool isOperatorOutsideBlocks(const std::vector<std::pair<size_t, size_t>>& blocks, size_t loc);
template<class baseX, class opX, class funcX>
class grabberInterpreter {
    static_assert(std::is_base_of<baseX, opX>::value,
                  "Operations class and base class must have a parent child relationship");
    static_assert(std::is_base_of<baseX, funcX>::value,
                  "functions class and base class must have a parent child relationship");

  private:
    std::function<std::unique_ptr<baseX>(const std::string&, coreObject*)> createX;

  public:
    explicit grabberInterpreter(
        std::function<std::unique_ptr<baseX>(const std::string&, coreObject*)> fc): createX(fc)
    {
    }
    std::unique_ptr<baseX> interpretGrabberBlock(const std::string& command, coreObject* obj)
    {
        // this is to resolve an issue with URI specified parameters and the division operator but
        // still allow normal division operator in most cases  for URI specified the % must be used
        // for division or the function form
        const std::string& mdivstr =
            (command.find_last_of('?') == std::string::npos) ? mdiv1String : mdiv2String;
        std::unique_ptr<baseX> ggb = nullptr;

        std::vector<std::pair<size_t, size_t>> outer_chunks;
        // check for functions

        size_t endParenthesisLocation = std::string::npos;
        size_t firstParenthesisLocation = command.find_first_of('(', 0);
        while (endParenthesisLocation != command.length()) {
            if (firstParenthesisLocation != std::string::npos) {
                endParenthesisLocation = pChunckEnd(command, firstParenthesisLocation + 1);
                if (endParenthesisLocation == std::string::npos) {
                    endParenthesisLocation = command.length();
                }
            }
            outer_chunks.emplace_back(firstParenthesisLocation, endParenthesisLocation);
            if (endParenthesisLocation < command.length()) {
                firstParenthesisLocation = command.find_first_of('(', endParenthesisLocation + 1);
                endParenthesisLocation = std::string::npos;
            } else {
                break;
            }
        }

        if ((!outer_chunks.empty()) && (outer_chunks[0].first == 0) &&
            (outer_chunks[0].second == command.length() - 1)) {
            return interpretGrabberBlock(command.substr(1, outer_chunks[0].second - 1), obj);
        }
        size_t operatorLocation;
        operatorLocation = command.find_first_of(psubString, 0);
        while (operatorLocation != std::string::npos) {
            if (isOperatorOutsideBlocks(outer_chunks, operatorLocation)) {
                return addSubGrabberBlocks(command, obj, operatorLocation);
            }
            operatorLocation = command.find_first_of(psubString, operatorLocation + 1);
        }

        operatorLocation = command.find_first_of(mdivstr, 0);
        while (operatorLocation != std::string::npos) {
            if (isOperatorOutsideBlocks(outer_chunks, operatorLocation)) {
                return multDivGrabberBlocks(command, obj, operatorLocation);
            }
            operatorLocation = command.find_first_of(mdivstr, operatorLocation + 1);
        }

        if ((!outer_chunks.empty()) && (outer_chunks[0].first != std::string::npos)) {
            std::string cmdBlock = command.substr(0, outer_chunks[0].first);
            if (isFunctionName(cmdBlock)) {
                std::string fcallstr = gmlc::utilities::stringOps::trim(
                    command.substr(outer_chunks[0].first + 1,
                                   outer_chunks[0].second - outer_chunks[0].first - 1));

                auto gstr = gmlc::utilities::stringOps::splitlineBracket(fcallstr);
                if (gstr.size() == 1) {
                    std::unique_ptr<baseX> ggbA = interpretGrabberBlock(fcallstr, obj);
                    if (ggbA) {
                        ggb = std::make_unique<funcX>(std::move(ggbA), cmdBlock);
                    }
                } else if (gstr.size() == 2) {
                    std::unique_ptr<baseX> ggbA = interpretGrabberBlock(gstr[0], obj);
                    std::unique_ptr<baseX> ggbB = interpretGrabberBlock(gstr[1], obj);
                    if ((ggbA) && (ggbB)) {
                        ggb = std::make_unique<opX>(std::move(ggbA), std::move(ggbB), cmdBlock);
                    }
                }
            } else {
                // not a function call so must be units description
                ggb = singleBlockInterpreter(command, obj);
            }
        } else {
            ggb = singleBlockInterpreter(command, obj);
        }

        return ggb;
    }

  private:
    std::unique_ptr<baseX> singleBlockInterpreter(const std::string& command, coreObject* obj)
    {
        coreObject* mobj = obj;
        std::string field(command);
        units::unit outUnit = units::defunit;
        // get the object which to grab from
        size_t fieldSeperatorLocation = command.find_last_of(":?");
        if (fieldSeperatorLocation != std::string::npos) {
            mobj = locateObject(command, obj);
            if (mobj == nullptr) {
                return nullptr;
            }
            field = command.substr(fieldSeperatorLocation + 1, std::string::npos);
        }

        size_t openParenthesisLocation = field.find_first_of('(');
        if (openParenthesisLocation != std::string::npos) {
            size_t closeParenthesisLocation = field.find_first_of(')', openParenthesisLocation);
            std::string unitName =
                field.substr(openParenthesisLocation + 1,
                             closeParenthesisLocation - openParenthesisLocation - 1);
            outUnit = units::unit_cast_from_string(unitName);
            field = field.substr(0, openParenthesisLocation);
        }
        std::unique_ptr<baseX> ggb = createX(field, mobj);
        if (ggb) {
            ggb->outputUnits = outUnit;
        }

        return ggb;
    }

    std::unique_ptr<baseX>
        addSubGrabberBlocks(const std::string& command, coreObject* obj, size_t rlc)
    {
        std::unique_ptr<baseX> ggb = nullptr;
        std::string Ablock = command.substr(0, rlc);
        gmlc::utilities::stringOps::trimString(Ablock);
        char op = command[rlc];
        std::string Bblock = command.substr(rlc + 1, std::string::npos);
        gmlc::utilities::stringOps::trimString(Bblock);
        // check if either Ablock or Bblock is a constant
        double valA = gmlc::utilities::numeric_conversionComplete(Ablock, kNullVal);
        std::unique_ptr<baseX> ggbA =
            (valA == kNullVal) ? interpretGrabberBlock(Ablock, obj) : nullptr;

        double valB = gmlc::utilities::numeric_conversionComplete(Bblock, kNullVal);
        std::unique_ptr<baseX> ggbB =
            (valB == kNullVal) ? interpretGrabberBlock(Bblock, obj) : nullptr;

        if (ggbA)  // we know Ablock is std::make_shared<grabber>
        {
            if (ggbB)  // both are grabber blocks
            {
                ggb =
                    std::make_unique<opX>(std::move(ggbA), std::move(ggbB), command.substr(rlc, 1));
            } else if (valB != kNullVal) {
                ggb = std::move(ggbA);
                if (op == '+') {
                    ggb->bias = valB;
                } else {
                    ggb->bias = -valB;
                }
            } else {
                return nullptr;  // we can't interpret this
            }
        } else if (valA != kNullVal) {
            if (ggbB) {
                // both are grabber blocks
                ggb = std::move(ggbB);
                ggb->bias = valA;
                if (op == '-') {
                    ggb->gain = -1.0;
                }
            } else if (valB != kNullVal) {
                // both are numeric
                ggb = std::make_unique<baseX>("constant", obj);
                if (op == '+') {
                    ggb->bias = valA + valB;
                } else {
                    ggb->bias = valA - valB;
                }
            } else {
                return nullptr;  // we can't interpret this
            }
        } else {
            return nullptr;
        }
        return ggb;
    }

    std::unique_ptr<baseX>
        multDivGrabberBlocks(const std::string& command, coreObject* obj, size_t rlc)
    {
        std::unique_ptr<baseX> ggb = nullptr;

        std::string Ablock = command.substr(0, rlc);
        gmlc::utilities::stringOps::trimString(Ablock);
        char op = command[rlc];
        std::string Bblock = command.substr(rlc + 1, std::string::npos);
        gmlc::utilities::stringOps::trimString(Bblock);
        // check if either Ablock or Bblock is a constant
        double valA = gmlc::utilities::numeric_conversionComplete(Ablock, kNullVal);
        std::unique_ptr<baseX> ggbA =
            (valA == kNullVal) ? interpretGrabberBlock(Ablock, obj) : nullptr;

        double valB = gmlc::utilities::numeric_conversionComplete(Bblock, kNullVal);
        std::unique_ptr<baseX> ggbB =
            (valB == kNullVal) ? interpretGrabberBlock(Bblock, obj) : nullptr;

        if (ggbA) {
            // we know Ablock is std::make_shared<grabber
            if (ggbB) {
                // both are grabber blocks
                ggb =
                    std::make_unique<opX>(std::move(ggbA), std::move(ggbB), command.substr(rlc, 1));
            } else if (valB != kNullVal) {
                if (op == '*') {
                    ggb = std::move(ggbA);
                    ggb->gain = valB;
                } else if ((op == '/') || (op == '%')) {
                    ggb = std::move(ggbA);
                    ggb->gain = 1.0 / valB;
                } else {
                    // set up power function
                    ggbB = std::make_unique<baseX>("constant", obj);
                    ggbB->bias = valB;
                    ggb = std::make_unique<opX>(std::move(ggbA),
                                                std::move(ggbB),
                                                command.substr(rlc, 1));
                }
            } else {
                return nullptr;  // we can't interpret this
            }
        } else if (valA != kNullVal) {
            if (ggbB) {
                // B is a block
                if (op == '*') {
                    ggb = std::move(ggbB);
                    ggb->gain = valA;
                } else {
                    // set up division by the grabber not a constant
                    ggbA = std::make_unique<baseX>("constant", obj);
                    ggbA->bias = valA;
                    ggb = std::make_unique<opX>(std::move(ggbA),
                                                std::move(ggbB),
                                                command.substr(rlc, 1));
                }
            } else if (valB != kNullVal) {
                // both are numeric
                ggb = std::make_unique<baseX>("constant", obj);
                if (op == '*') {
                    ggb->bias = valA * valB;
                } else if ((op == '/') || (op == '%')) {
                    if (valB != 0) {
                        ggb->bias = valA / valB;
                    } else {
                        ggb->bias = kNullVal;
                    }
                } else {
                    ggb->bias = pow(valA, valB);
                }
            } else {
                return nullptr;  // we can't interpret this
            }
        } else {
            return nullptr;
        }
        return ggb;
    }

    // TODO(phlpt): Merge this helper with the stringOps-library version.
    size_t pChunckEnd(const std::string& cmd, size_t start)
    {
        int open = 1;
        size_t rlc = start;
        while (open > 0) {
            rlc = cmd.find_first_of("()", rlc + 1);
            if (rlc == std::string::npos) {
                break;
            }
            if (cmd[rlc] == '(') {
                ++open;
            } else {
                --open;
            }
        }
        return rlc;
    }
};

}  // namespace griddyn
