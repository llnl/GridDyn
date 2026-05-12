/*
 * Copyright (c) 2014-2026, Lawrence Livermore National Security
 * See the top-level NOTICE for additional details. All rights reserved.
 * SPDX-License-Identifier: BSD-3-Clause
 */

#include "elementReaderTemplates.hpp"
#include "gmlc/utilities/string_viewOps.h"
#include "readElement.h"
#include "readerHelper.h"
#include <cmath>
#include <string>
#include <string_view>

namespace griddyn {
using std::string_view;

static const IgnoreListType ignoreConditionVariables{"condition"};
bool checkCondition(string_view cond, readerInfo& ri, coreObject* parentObject);
// "aP" is the XML element passed from the reader
void loadConditionElement(std::shared_ptr<readerElement>& element,
                          readerInfo& ri,
                          coreObject* parentObject)
{
    auto riScope = ri.newScope();

    loadDefines(element, ri);
    loadDirectories(element, ri);

    bool eval = false;
    std::string condString = getElementField(element, "condition", readerConfig::defMatchType);

    if (!condString.empty()) {
        // deal with &gt for > and &lt for < if necessary
        condString = gmlc::utilities::stringOps::xmlCharacterCodeReplace(condString);
        eval = checkCondition(condString, ri, parentObject);
    } else {
        WARNPRINT(READER_WARN_IMPORTANT, "no condition specified");
        eval = false;
    }

    if (eval) {
        readImports(element, ri, parentObject, false);
        // load the subobjects of gen
        loadSubObjects(element, ri, parentObject);
        // get all element fields
        paramLoopElement(parentObject, element, "local", ri, ignoreConditionVariables);
        readImports(element, ri, parentObject, true);
    }

    ri.closeScope(riScope);
}

template<class X>
bool compare(const X& val1, const X& val2, char op1, char op2)
{
    switch (op1) {
        case '>':
            return (op2 == '=') ? (val1 >= val2) : (val1 > val2);
        case '<':
            return (op2 == '=') ? (val1 <= val2) : (val1 < val2);
        case '!':
            return (val1 != val2);
        case '=':
            return (val1 == val2);
        default:
            throw(std::invalid_argument("invalid comparison operator"));
    }
}

bool checkCondition(string_view cond, readerInfo& ri, coreObject* parentObject)
{
    gmlc::utilities::string_viewOps::trim(cond);
    bool reverseResult = false;
    if ((cond[0] == '!') || (cond[0] == '~')) {
        reverseResult = true;
        cond = cond.substr(1, std::string_view::npos);
    }
    size_t operatorPos = cond.find_first_of("><=!");
    bool eval = false;
    char primaryOperator;
    char secondaryOperator;
    string_view leftOperand;
    string_view rightOperand;

    if (operatorPos == std::string_view::npos) {
        primaryOperator = '!';
        secondaryOperator = '=';
        leftOperand = gmlc::utilities::string_viewOps::trim(cond);
        rightOperand = "0";
        if (leftOperand.compare(0, 6, "exists") == 0) {
            auto openParenPos = leftOperand.find_first_of('(', 6);
            auto closeParenPos = leftOperand.find_last_of(')');
            auto check = leftOperand.substr(openParenPos + 1, closeParenPos - openParenPos - 1);
            coreObject* obj = locateObject(std::string{check}, parentObject, false);
            return (reverseResult) ? (obj == nullptr) : (obj != nullptr);
        }
    } else {
        primaryOperator = cond[operatorPos];
        secondaryOperator = cond[operatorPos + 1];
        leftOperand = gmlc::utilities::string_viewOps::trim(cond.substr(0, operatorPos));
        rightOperand = (secondaryOperator == '=') ?
                           gmlc::utilities::string_viewOps::trim(cond.substr(operatorPos + 2)) :
                           gmlc::utilities::string_viewOps::trim(cond.substr(operatorPos + 1));
    }

    ri.setKeyObject(parentObject);
    double leftValue = interpretString(std::string{leftOperand}, ri);
    double rightValue = interpretString(std::string{rightOperand}, ri);

    if (!std::isnan(leftValue) && !std::isnan(rightValue)) {
        try {
            eval = compare(leftValue, rightValue, primaryOperator, secondaryOperator);
        }
        catch (const std::invalid_argument&) {
            WARNPRINT(READER_WARN_IMPORTANT, "invalid comparison operator");
        }
    } else if (std::isnan(leftValue) && (std::isnan(rightValue))) {  // do a string comparison
        std::string leftString = ri.checkDefines(std::string{leftOperand});
        std::string rightString = ri.checkDefines(std::string{rightOperand});

        try {
            eval = compare(leftString, rightString, primaryOperator, secondaryOperator);
        }
        catch (const std::invalid_argument&) {
            WARNPRINT(READER_WARN_IMPORTANT, "invalid comparison terms");
        }
    } else {  // mixed string and number comparison
        WARNPRINT(READER_WARN_IMPORTANT, "invalid comparison terms");
    }

    ri.setKeyObject(nullptr);
    return (reverseResult) ? !eval : eval;
}

}  // namespace griddyn
