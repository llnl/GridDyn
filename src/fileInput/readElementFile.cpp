/*
 * Copyright (c) 2014-2026, Lawrence Livermore National Security
 * See the top-level NOTICE for additional details. All rights reserved.
 * SPDX-License-Identifier: BSD-3-Clause
 */

#include "readElementFile.h"

#include "core/helperObject.h"
#include "elementReaderTemplates.hpp"
#include "gmlc/utilities/stringConversion.h"
#include "griddyn/gridDynSimulation.h"
#include "readerHelper.h"
#include "utilities/gridRandom.h"
#include <filesystem>
#include <memory>
#include <sstream>
#include <string>
#include <utility>

namespace griddyn {
using gmlc::utilities::convertToLowerCase;
using gmlc::utilities::numeric_conversion;

void loadElementInformation(coreObject* obj,
                            std::shared_ptr<readerElement>& element,
                            const std::string& objectName,
                            readerInfo& readerInfoRef,
                            const IgnoreListType& ignoreList)
{
    objSetAttributes(obj, element, objectName, readerInfoRef, ignoreList);
    readImports(element, readerInfoRef, obj, false);
    // check for child objects
    loadSubObjects(element, readerInfoRef, obj);

    // get all element fields
    paramLoopElement(obj, element, objectName, readerInfoRef, ignoreList);
    readImports(element, readerInfoRef, obj, true);
}

static void checkForEndUnits(gridParameter& param, const std::string& parameterString);

static const char importString[] = "import";
void readImports(std::shared_ptr<readerElement>& element,
                 readerInfo& readerInformation,
                 coreObject* parentObject,
                 bool finalFlag)
{
    if (!element->hasElement(importString)) {
        return;
    }

    // run any source files
    const auto baseFlags = readerInformation.getFlags();
    element->bookmark();
    element->moveToFirstChild(importString);
    while (element->isValid()) {
        bool finalMode = false;
        const std::string fstring = getElementField(element, "final", readerConfig::defMatchType);

        if ((fstring == "true") || (fstring == "1")) {
            finalMode = true;
        }

        if (finalFlag != finalMode) {
            element->moveToNextSibling(importString);
            continue;
        }

        const std::string flags = getElementField(element, "flags", readerConfig::defMatchType);
        if (!flags.empty()) {
            addFlags(readerInformation, flags);
        }
        std::string sourceFile = getElementField(element, "file", readerConfig::defMatchType);
        if (sourceFile.empty()) {
            // if we don't find a field named file, just use the text in the source element
            sourceFile = element->getText();
        }

        // check through the files to find the right location
        readerInformation.checkFileParam(sourceFile, true);
        std::string prefix =
            getElementField(element, "prefix", readerConfig::MatchType::CAPITAL_CASE_MATCH);
        // get the prefix if any
        if (prefix.empty()) {
            prefix = readerInformation.prefix;
        } else if (!readerInformation.prefix.empty()) {
            auto temp = readerInformation.prefix;
            temp.push_back('_');
            temp.append(prefix);
            prefix = std::move(temp);
        }

        // check for type override
        const std::string ext =
            convertToLowerCase(getElementField(element, "filetype", readerConfig::defMatchType));

        std::swap(prefix, readerInformation.prefix);
        if (ext.empty()) {
            loadFile(parentObject, sourceFile, &readerInformation);
        } else {
            loadFile(parentObject, sourceFile, &readerInformation, ext);
        }
        std::swap(prefix, readerInformation.prefix);

        readerInformation.setAllFlags(baseFlags);
        element->moveToNextSibling(importString);  // next import file
    }
    element->restore();
}

static const char unitString1[] = "units";
static const char unitString2[] = "unit";

static units::unit readUnits(const std::shared_ptr<readerElement>& element,
                             const std::string& field)
{
    std::string uname = element->getAttributeText(unitString1);
    // actually specifying a "unit" attribute takes precedence
    if (uname.empty()) {
        uname = element->getAttributeText(unitString2);
    }
    if (!uname.empty()) {
        auto retUnits = units::unit_cast_from_string(uname);
        if (!units::is_valid(retUnits)) {
            WARNPRINT(READER_WARN_ALL, "unknown unit " << uname);
        }
        return retUnits;
    }
    if (field.back() == ')') {
        const auto openParenPos = field.find_last_of('(');

        if (openParenPos != std::string::npos) {
            uname = field.substr(openParenPos + 1, field.length() - 2 - openParenPos);
            auto retUnits = units::unit_cast_from_string(uname);
            if (!units::is_valid(retUnits)) {
                WARNPRINT(READER_WARN_ALL, "unknown unit " << uname);
            }
            return retUnits;
        }
    }
    return units::defunit;
}

static const char valueString[] = "value";

gridParameter getElementParam(const std::shared_ptr<readerElement>& element)
{
    gridParameter param;
    getElementParam(element, param);
    return param;
}

void getElementParam(const std::shared_ptr<readerElement>& element, gridParameter& param)
{
    param.paramUnits = units::defunit;
    param.valid = false;
    std::string fieldName = convertToLowerCase(element->getName());

    if (fieldName == "param") {
        std::string pname = element->getAttributeText("name");
        if (pname.empty()) {
            pname = element->getAttributeText("field");
        }
        if (pname.empty()) {
            // no name or field attribute so just read the string and see if we can process it
            param.fromString(element->getText());
            return;
        }
        param.paramUnits = readUnits(element, pname);
        if (pname.back() == ')') {
            const auto openParenPos = pname.find_last_of('(');
            if (openParenPos != std::string::npos) {
                pname.erase(openParenPos);
            }
        }
        param.field = convertToLowerCase(pname);
        if (element->hasAttribute(valueString)) {
            param.value = element->getAttributeValue(valueString);
            if (param.value == readerNullVal) {
                checkForEndUnits(param, element->getAttributeText(valueString));
            } else {
                param.stringType = false;
            }
        } else {
            param.value = element->getValue();
            if (param.value == readerNullVal) {
                checkForEndUnits(param, element->getText());
            } else {
                param.stringType = false;
            }
        }
    } else {
        // all other properties
        param.paramUnits = readUnits(element, fieldName);
        if (fieldName.back() == ')') {
            const auto openParenPos = fieldName.find_last_of('(');
            if (openParenPos != std::string::npos) {
                fieldName.erase(openParenPos);
            }
        }
        param.field = fieldName;
        param.value = element->getValue();
        if (param.value == readerNullVal) {
            checkForEndUnits(param, element->getText());
        } else {
            param.stringType = false;
        }
    }
    param.valid = true;
}

void checkForEndUnits(gridParameter& param, const std::string& parameterString)
{
    const double numericValue = numeric_conversion(parameterString, readerNullVal);
    if (numericValue != readerNullVal) {
        const auto lastNumericPos = parameterString.find_last_of("012345689. )]");
        if (lastNumericPos < parameterString.size() - 1) {
            auto unitValue =
                units::unit_cast_from_string(parameterString.substr(lastNumericPos + 1));
            if (units::is_valid(unitValue)) {
                param.value = numericValue;
                param.paramUnits = unitValue;
                param.stringType = false;
                return;
            }
        }
    }
    param.strVal = parameterString;
    param.stringType = true;
}

static const IgnoreListType& keywords()
{
    static const auto* keywordSet =
        new IgnoreListType{"type",      "ref",       "number",        "index",   "retype",
                           "name",      "define",    "library",       "import",  "area",
                           "bus",       "link",      "load",          "exciter", "if",
                           "source",    "governor",  "block",         "pss",     "simulation",
                           "generator", "array",     "relay",         "parent",  "genmodel",
                           "line",      "solver",    "agc",           "reserve", "reservedispatch",
                           "dispatch",  "econ",      "configuration", "custom",  "purpose",
                           "event",     "collector", "extra"};
    return *keywordSet;
}

static bool isXmlNamespaceAttribute(const std::string& fieldName)
{
    return (fieldName == "xmlns") || ((fieldName.size() > 6) && fieldName.starts_with("xmlns:"));
}

void objSetAttributes(coreObject* obj,
                      std::shared_ptr<readerElement>& element,
                      const std::string& component,
                      readerInfo& readerInfoRef,
                      const IgnoreListType& ignoreList)
{
    auto att = element->getFirstAttribute();
    while (att.isValid()) {
        units::unit unitType = units::defunit;
        std::string fieldName = convertToLowerCase(att.getName());

        if (isXmlNamespaceAttribute(fieldName)) {
            att = element->getNextAttribute();
            continue;
        }

        if (fieldName.back() == ')') {
            const auto openParenPos = fieldName.find_last_of('(');
            if (openParenPos != std::string::npos) {
                const std::string unitString =
                    fieldName.substr(openParenPos + 1, fieldName.length() - 2 - openParenPos);
                unitType = units::unit_cast_from_string(unitString);
                fieldName = fieldName.substr(0, openParenPos - 1);
            }
        }
        const auto& keywordSet = keywords();
        auto ifind = keywordSet.find(fieldName);
        if (ifind != keywordSet.end()) {
            att = element->getNextAttribute();
            continue;
        }
        ifind = ignoreList.find(fieldName);
        if (ifind != ignoreList.end()) {
            att = element->getNextAttribute();
            continue;
        }

        if (fieldName.contains("file") || (fieldName == "fmu")) {
            std::string strVal = att.getText();
            readerInfoRef.checkFileParam(strVal);
            gridParameter paramObject(fieldName, strVal);
            setObjectParameter(component, obj, paramObject);
        } else if (fieldName.contains("workdir") || fieldName.contains("directory")) {
            std::string strVal = att.getText();
            readerInfoRef.checkDirectoryParam(strVal);
            gridParameter paramObject(fieldName, strVal);
            setObjectParameter(component, obj, paramObject);
        } else if ((fieldName == "flag") || (fieldName == "flags")) {
            // read the flags parameter
            try {
                setMultipleFlags(obj, att.getText());
            }
            catch (const unrecognizedParameter&) {
                WARNPRINT(READER_WARN_ALL, "unrecognized flag " << att.getText() << "\n");
            }
        } else {
            const double val = att.getValue();
            if (val != readerNullVal) {
                gridParameter paramObject(fieldName, val);
                paramObject.paramUnits = unitType;
                setObjectParameter(component, obj, paramObject);
            } else {
                gridParameter paramObject(fieldName, att.getText());
                paramStringProcess(paramObject, readerInfoRef);
                setObjectParameter(component, obj, paramObject);
            }
        }
        att = element->getNextAttribute();
    }
}

void paramLoopElement(coreObject* obj,
                      std::shared_ptr<readerElement>& element,
                      const std::string& component,
                      readerInfo& readerInfoRef,
                      const IgnoreListType& ignoreList)
{
    element->moveToFirstChild();
    while (element->isValid()) {
        const std::string fieldName = convertToLowerCase(element->getName());
        const auto& keywordSet = keywords();
        auto ifind = keywordSet.find(fieldName);
        if (ifind != keywordSet.end()) {
            element->moveToNextSibling();
            continue;
        }
        ifind = readerInfoRef.getIgnoreList().find(fieldName);
        if (ifind != readerInfoRef.getIgnoreList().end()) {
            element->moveToNextSibling();
            continue;
        }
        ifind = ignoreList.find(fieldName);
        if (ifind != ignoreList.end()) {
            element->moveToNextSibling();
            continue;
        }
        // get all the parameter fields
        auto param = getElementParam(element);
        if (param.valid) {
            if (param.stringType) {
                if (param.field.contains("file") || (param.field == "fmu")) {
                    readerInfoRef.checkFileParam(param.strVal);
                    setObjectParameter(component, obj, param);
                } else if (param.field.contains("workdir") || param.field.contains("directory")) {
                    readerInfoRef.checkDirectoryParam(param.strVal);
                    setObjectParameter(component, obj, param);
                } else if ((fieldName == "flag") || (fieldName == "flags")) {
                    // read the flags parameter
                    paramStringProcess(param, readerInfoRef);
                    try {
                        setMultipleFlags(obj, param.strVal);
                    }
                    catch (const unrecognizedParameter&) {
                        WARNPRINT(READER_WARN_ALL, "unrecognized flag in " << param.strVal << "\n");
                    }
                } else {
                    paramStringProcess(param, readerInfoRef);
                    setObjectParameter(component, obj, param);
                }
            } else {
                setObjectParameter(component, obj, param);
            }
        }
        element->moveToNextSibling();
    }
    element->moveToParent();
}

void readConfigurationFields(std::shared_ptr<readerElement>& sim, readerInfo& /*readerInfoRef*/)
{
    if (sim->hasElement("configuration")) {
        sim->bookmark();
        sim->moveToFirstChild("configuration");
        auto cfgAtt = sim->getFirstAttribute();

        while (cfgAtt.isValid()) {
            auto cfgname = convertToLowerCase(cfgAtt.getName());
            if ((cfgname == "matching") || (cfgname == "match_type")) {
                readerConfig::setDefaultMatchType(cfgAtt.getText());
            } else if (cfgname == "printlevel") {
                readerConfig::setPrintMode(cfgAtt.getText());
            } else if ((cfgname == "seed")) {
                try {
                    auto seed = std::stoul(cfgAtt.getText());
                    utilities::gridRandom::setSeed(seed);
                }
                catch (const std::invalid_argument&) {
                    WARNPRINT(READER_WARN_IMPORTANT, "invalid seed value, must be an integer");
                }
            }
            cfgAtt = sim->getNextAttribute();
        }

        sim->moveToFirstChild();
        while (sim->isValid()) {
            auto fieldName = convertToLowerCase(sim->getName());
            if ((fieldName == "matching") || (fieldName == "match_type")) {
                readerConfig::setDefaultMatchType(sim->getText());
            } else if (fieldName == "printlevel") {
                readerConfig::setPrintMode(sim->getText());
            } else if ((fieldName == "seed")) {
                try {
                    auto seed = std::stoul(cfgAtt.getText());
                    utilities::gridRandom::setSeed(seed);
                }
                catch (const std::invalid_argument&) {
                    WARNPRINT(READER_WARN_IMPORTANT, "invalid seed value, must be an integer");
                }
            }
            sim->moveToNextSibling();
        }

        sim->restore();
    }
}

void setAttributes(helperObject* obj,
                   std::shared_ptr<readerElement>& element,
                   const std::string& component,
                   readerInfo& readerInfoRef,
                   const IgnoreListType& ignoreList)
{
    auto att = element->getFirstAttribute();

    while (att.isValid()) {
        const std::string fieldName = convertToLowerCase(att.getName());

        if (isXmlNamespaceAttribute(fieldName)) {
            att = element->getNextAttribute();
            continue;
        }

        auto ifind = ignoreList.find(fieldName);
        if (ifind != ignoreList.end()) {
            att = element->getNextAttribute();
            continue;
        }
        try {
            if (fieldName.contains("file") || (fieldName == "fmu")) {
                std::string strVal = att.getText();
                readerInfoRef.checkFileParam(strVal);
                LEVELPRINT(READER_VERBOSE_PRINT,
                           component << ": setting " << fieldName << " to " << strVal);
                obj->set(fieldName, strVal);
            } else {
                const double val = att.getValue();
                if ((val != readerNullVal) && (val != kNullVal)) {
                    LEVELPRINT(READER_VERBOSE_PRINT,
                               component << ": setting " << fieldName << " to " << val);
                    obj->set(fieldName, val);
                } else {
                    gridParameter paramObject(fieldName, att.getText());
                    paramStringProcess(paramObject, readerInfoRef);
                    if (paramObject.stringType) {
                        obj->set(paramObject.field, paramObject.strVal);
                        LEVELPRINT(READER_VERBOSE_PRINT,
                                   component << ": setting " << fieldName << " to "
                                             << paramObject.strVal);
                    } else {
                        obj->set(paramObject.field, paramObject.value);
                        LEVELPRINT(READER_VERBOSE_PRINT,
                                   component << ": setting " << fieldName << " to "
                                             << paramObject.value);
                    }
                }
            }
        }
        catch (const unrecognizedParameter&) {
            WARNPRINT(READER_WARN_ALL, "unknown " << component << " parameter " << fieldName);
        }
        catch (const invalidParameterValue&) {
            WARNPRINT(READER_WARN_ALL,
                      "value for " << component << " parameter " << fieldName << " ("
                                   << att.getText() << ") is invalid");
        }

        att = element->getNextAttribute();
    }
}

void setParams(helperObject* obj,
               std::shared_ptr<readerElement>& element,
               const std::string& component,
               readerInfo& readerInfoRef,
               const IgnoreListType& ignoreList)
{
    element->moveToFirstChild();
    while (element->isValid()) {
        const std::string fieldName = convertToLowerCase(element->getName());
        auto ifind = ignoreList.find(fieldName);
        if (ifind != ignoreList.end()) {
            element->moveToNextSibling();
            continue;
        }

        auto param = getElementParam(element);
        if (param.valid) {
            try {
                if (param.stringType) {
                    if (param.field.contains("file") || (param.field == "fmu")) {
                        readerInfoRef.checkFileParam(param.strVal);
                        LEVELPRINT(READER_VERBOSE_PRINT,
                                   component << ":setting " << obj->getName() << " file to "
                                             << param.strVal);
                        obj->set(param.field, param.strVal);
                    } else {
                        paramStringProcess(param, readerInfoRef);
                        if (param.stringType) {
                            LEVELPRINT(READER_VERBOSE_PRINT,
                                       component << ":setting " << obj->getName() << " "
                                                 << param.field << " to " << param.strVal);
                            obj->set(param.field, param.strVal);
                        } else {
                            LEVELPRINT(READER_VERBOSE_PRINT,
                                       component << ":setting " << obj->getName() << " "
                                                 << param.field << " to " << param.value);
                            obj->set(param.field, param.value);
                        }
                    }
                } else {
                    LEVELPRINT(READER_VERBOSE_PRINT,
                               component << ":setting " << obj->getName() << " " << param.field
                                         << " to " << param.value);
                    obj->set(param.field, param.value);
                }
            }
            catch (const unrecognizedParameter&) {
                WARNPRINT(READER_WARN_ALL, "unknown " << component << " parameter " << param.field);
            }
            catch (const invalidParameterValue&) {
                if (param.stringType) {
                    WARNPRINT(READER_WARN_ALL,
                              "value for " << component << " parameter " << param.field << " ("
                                           << param.strVal << ") is invalid");
                } else {
                    WARNPRINT(READER_WARN_ALL,
                              "value for " << component << " parameter " << param.field << " ("
                                           << param.value << ") is invalid");
                }
            }
        }
        element->moveToNextSibling();
    }
    element->moveToParent();
}

}  // namespace griddyn
