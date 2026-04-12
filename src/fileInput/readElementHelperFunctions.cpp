/*
 * Copyright (c) 2014-2020, Lawrence Livermore National Security
 * See the top-level NOTICE for additional details. All rights reserved.
 * SPDX-License-Identifier: BSD-3-Clause
 */

#include "formatInterpreters/readerElement.h"
#include "gmlc/utilities/stringOps.h"
#include "readElement.h"
#include <string>

namespace griddyn {
using gmlc::utilities::convertToLowerCase;
using gmlc::utilities::makeUpperCase;
std::string findElementName(std::shared_ptr<readerElement>& element,
                            const std::string& ename,
                            readerConfig::match_type matching)
{
    std::string fieldName = ename;
    if (element->hasElement(fieldName)) {
        return fieldName;
    }
    switch (matching) {
        case readerConfig::match_type::capital_case_match:

            // check lower case
            fieldName = convertToLowerCase(fieldName);
            if (element->hasElement(fieldName)) {
                return fieldName;
            }
            makeUpperCase(fieldName);
            if (element->hasElement(fieldName)) {
                return fieldName;
            }
            break;
        case readerConfig::match_type::any_case_match:
            fieldName = convertToLowerCase(fieldName);

            element->moveToFirstChild();
            while (element->isValid()) {
                std::string tempName = convertToLowerCase(element->getName());
                if (fieldName == tempName) {
                    fieldName = element->getName();
                    element->moveToParent();
                    return fieldName;
                }
                element->moveToNextSibling();
            }
            element->moveToParent();
            break;
        case readerConfig::match_type::strict_case_match:
        default:
            break;
    }
    return emptyString;
}

std::string getElementAttribute(std::shared_ptr<readerElement>& element,
                                const std::string& ename,
                                readerConfig::match_type matching)
{
    if (element->hasAttribute(ename)) {
        return element->getAttributeText(ename);
    }
    switch (matching) {
        case readerConfig::match_type::capital_case_match: {
            auto tempName = convertToLowerCase(ename);
            if (element->hasAttribute(tempName)) {
                return element->getAttributeText(tempName);
            }
            makeUpperCase(tempName);
            if (element->hasAttribute(tempName)) {
                return element->getAttributeText(tempName);
            }
        } break;
        case readerConfig::match_type::any_case_match: {
            auto tempName = convertToLowerCase(ename);
            auto att = element->getFirstAttribute();
            while (att.isValid()) {
                auto fieldName = convertToLowerCase(att.getName());
                if (tempName == fieldName) {
                    return att.getText();
                }
                att = element->getNextAttribute();
            }
        } break;
        case readerConfig::match_type::strict_case_match:
        default:
            break;
    }
    return emptyString;
}

std::string getElementField(std::shared_ptr<readerElement>& element,
                            const std::string& ename,
                            readerConfig::match_type matching)
{
    auto ret = getElementAttribute(element, ename, matching);
    // an element can override an attribute
    auto name = findElementName(element, ename, matching);
    if (name.empty()) {
        return ret;
    }
    element->moveToFirstChild(name);
    ret = element->getText();
    element->moveToParent();
    return ret;
}

std::string getElementFieldOptions(std::shared_ptr<readerElement>& element,
                                   const stringVec& names,
                                   readerConfig::match_type matching)
{
    for (const auto& str : names) {
        auto ret = getElementField(element, str, matching);
        if (!ret.empty()) {
            return ret;
        }
    }
    return emptyString;
}

stringVec getElementFieldMultiple(std::shared_ptr<readerElement>& element,
                                  const std::string& ename,
                                  readerConfig::match_type matching)
{
    stringVec val;
    std::string elname_act;
    // get an attribute if there is one
    auto ret = getElementAttribute(element, ename, matching);

    if (!ret.empty()) {
        val.push_back(ret);
    }
    auto name = findElementName(element, ename, matching);
    if (name.empty()) {
        return val;
    }
    element->moveToFirstChild(name);
    while (element->isValid()) {
        val.push_back(element->getText());

        element->moveToNextSibling(name);  // next object
    }
    element->moveToParent();
    return val;
}

}  // namespace griddyn
