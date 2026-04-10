/*
 * Copyright (c) 2014-2020, Lawrence Livermore National Security
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
                            readerInfo& ri,
                            const IgnoreListType& ignoreList)
{
    objSetAttributes(obj, element, objectName, ri, ignoreList);
    readImports(element, ri, obj, false);
    // check for child objects
    loadSubObjects(element, ri, obj);

    // get all element fields
    paramLoopElement(obj, element, objectName, ri, ignoreList);
    readImports(element, ri, obj, true);
}

void checkForEndUnits(gridParameter& param, const std::string& paramStr);

static const char importString[] = "import";
void readImports(std::shared_ptr<readerElement>& element,
                 readerInfo& ri,
                 coreObject* parentObject,
                 bool finalFlag)
{
    if (!element->hasElement(importString)) {
        return;
    }

    // run any source files
    auto bflags = ri.getFlags();
    element->bookmark();
    element->moveToFirstChild(importString);
    while (element->isValid()) {
        bool finalMode = false;
        std::string fstring = getElementField(element, "final", readerConfig::defMatchType);

        if ((fstring == "true") || (fstring == "1")) {
            finalMode = true;
        }

        if (finalFlag != finalMode) {
            element->moveToNextSibling(importString);
            continue;
        }

        std::string flags = getElementField(element, "flags", readerConfig::defMatchType);
        if (!flags.empty()) {
            addflags(ri, flags);
        }
        std::string sourceFile = getElementField(element, "file", readerConfig::defMatchType);
        if (sourceFile.empty()) {
            // if we don't find a field named file, just use the text in the source element
            sourceFile = element->getText();
        }

        // check through the files to find the right location
        ri.checkFileParam(sourceFile, true);

        std::filesystem::path sourcePath(sourceFile);
        std::string prefix = getElementField(
            element, "prefix", readerConfig::match_type::capital_case_match);
        // get the prefix if any
        if (prefix.empty()) {
            prefix = ri.prefix;
        } else if (!(ri.prefix.empty())) {
            auto temp = ri.prefix;
            temp.push_back('_');
            temp.append(prefix);
            prefix = std::move(temp);
        }

        // check for type override
        const std::string ext =
            convertToLowerCase(getElementField(element, "filetype", readerConfig::defMatchType));

        std::swap(prefix, ri.prefix);
        if (ext.empty()) {
            loadFile(parentObject, sourceFile, &ri);
        } else {
            loadFile(parentObject, sourceFile, &ri, ext);
        }
        std::swap(prefix, ri.prefix);

        ri.setAllFlags(bflags);
        element->moveToNextSibling(importString);  // next import file
    }
    element->restore();
}

static const char unitString1[] = "units";
static const char unitString2[] = "unit";

units::unit readUnits(const std::shared_ptr<readerElement>& element, const std::string& field)
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
        auto p = field.find_last_of('(');

        if (p != std::string::npos) {
            uname = field.substr(p + 1, field.length() - 2 - p);
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
    gridParameter P;
    getElementParam(element, P);
    return P;
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
            auto p = pname.find_last_of('(');
            if (p != std::string::npos) {
                pname.erase(p);
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
            auto p = fieldName.find_last_of('(');
            if (p != std::string::npos) {
                fieldName.erase(p);
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

void checkForEndUnits(gridParameter& param, const std::string& paramStr)
{
    double val = numeric_conversion(paramStr, readerNullVal);
    if (val != readerNullVal) {
        auto N = paramStr.find_last_of("012345689. )]");
        if (N < paramStr.size() - 1) {
            auto Unit = units::unit_cast_from_string(paramStr.substr(N + 1));
            if (units::is_valid(Unit)) {
                param.value = val;
                param.paramUnits = Unit;
                param.stringType = false;
                return;
            }
        }
    }
    param.strVal = paramStr;
    param.stringType = true;
}

static const IgnoreListType keywords{
    "type",      "ref",       "number",        "index",   "retype",
    "name",      "define",    "library",       "import",  "area",
    "bus",       "link",      "load",          "exciter", "if",
    "source",    "governor",  "block",         "pss",     "simulation",
    "generator", "array",     "relay",         "parent",  "genmodel",
    "line",      "solver",    "agc",           "reserve", "reservedispatch",
    "dispatch",  "econ",      "configuration", "custom",  "purpose",
    "event",     "collector", "extra"};

void objSetAttributes(coreObject* obj,
                      std::shared_ptr<readerElement>& element,
                      const std::string& component,
                      readerInfo& ri,
                      const IgnoreListType& ignoreList)
{
    auto att = element->getFirstAttribute();
    while (att.isValid()) {
        units::unit unitType = units::defunit;
        std::string fieldName = convertToLowerCase(att.getName());

        if (fieldName.back() == ')') {
            auto p = fieldName.find_last_of('(');
            if (p != std::string::npos) {
                std::string ustring = fieldName.substr(p + 1, fieldName.length() - 2 - p);
                unitType = units::unit_cast_from_string(ustring);
                fieldName = fieldName.substr(0, p - 1);
            }
        }
        auto ifind = keywords.find(fieldName);
        if (ifind != keywords.end()) {
            att = element->getNextAttribute();
            continue;
        }
        ifind = ignoreList.find(fieldName);
        if (ifind != ignoreList.end()) {
            att = element->getNextAttribute();
            continue;
        }

        if ((fieldName.find("file") != std::string::npos) || (fieldName == "fmu")) {
            std::string strVal = att.getText();
            ri.checkFileParam(strVal);
            gridParameter po(fieldName, strVal);
            objectParameterSet(component, obj, po);
        } else if ((fieldName.find("workdir") != std::string::npos) ||
                   (fieldName.find("directory") != std::string::npos)) {
            std::string strVal = att.getText();
            ri.checkDirectoryParam(strVal);
            gridParameter po(fieldName, strVal);
            objectParameterSet(component, obj, po);
        } else if ((fieldName == "flag") || (fieldName == "flags")) {
            // read the flags parameter
            try {
                setMultipleFlags(obj, att.getText());
            }
            catch (const unrecognizedParameter&) {
                WARNPRINT(READER_WARN_ALL, "unrecognized flag " << att.getText() << "\n");
            }
        } else {
            double val = att.getValue();
            if (val != readerNullVal) {
                gridParameter po(fieldName, val);
                po.paramUnits = unitType;
                objectParameterSet(component, obj, po);
            } else {
                gridParameter po(fieldName, att.getText());
                paramStringProcess(po, ri);
                objectParameterSet(component, obj, po);
            }
        }
        att = element->getNextAttribute();
    }
}

void paramLoopElement(coreObject* obj,
                      std::shared_ptr<readerElement>& element,
                      const std::string& component,
                      readerInfo& ri,
                      const IgnoreListType& ignoreList)
{
    element->moveToFirstChild();
    while (element->isValid()) {
        std::string fieldName = convertToLowerCase(element->getName());
        auto ifind = keywords.find(fieldName);
        if (ifind != keywords.end()) {
            element->moveToNextSibling();
            continue;
        }
        ifind = ri.getIgnoreList().find(fieldName);
        if (ifind != ri.getIgnoreList().end()) {
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
                if ((param.field.find("file") != std::string::npos) || (param.field == "fmu")) {
                    ri.checkFileParam(param.strVal);
                    objectParameterSet(component, obj, param);
                } else if ((param.field.find("workdir") != std::string::npos) ||
                           (param.field.find("directory") != std::string::npos)) {
                    ri.checkDirectoryParam(param.strVal);
                    objectParameterSet(component, obj, param);
                } else if ((fieldName == "flag") || (fieldName == "flags")) {
                    // read the flags parameter
                    paramStringProcess(param, ri);
                    try {
                        setMultipleFlags(obj, param.strVal);
                    }
                    catch (const unrecognizedParameter&) {
                        WARNPRINT(READER_WARN_ALL, "unrecognized flag in " << param.strVal << "\n");
                    }
                } else {
                    paramStringProcess(param, ri);
                    objectParameterSet(component, obj, param);
                }
            } else {
                objectParameterSet(component, obj, param);
            }
        }
        element->moveToNextSibling();
    }
    element->moveToParent();
}

void readConfigurationFields(std::shared_ptr<readerElement>& sim, readerInfo& /*ri*/)
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
            } else if ((cfgname == "xmlreader") || (cfgname == "xml")) {
                readerConfig::setDefaultXMLReader(cfgAtt.getText());
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
            } else if ((fieldName == "xmlreader") || (fieldName == "xml")) {
                readerConfig::setDefaultXMLReader(sim->getText());
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
                   readerInfo& ri,
                   const IgnoreListType& ignoreList)
{
    auto att = element->getFirstAttribute();

    while (att.isValid()) {
        std::string fieldName = convertToLowerCase(att.getName());

        auto ifind = ignoreList.find(fieldName);
        if (ifind != ignoreList.end()) {
            att = element->getNextAttribute();
            continue;
        }
        try {
            if ((fieldName.find("file") != std::string::npos) || (fieldName == "fmu")) {
                std::string strVal = att.getText();
                ri.checkFileParam(strVal);
                LEVELPRINT(READER_VERBOSE_PRINT,
                           component << ": setting " << fieldName << " to " << strVal);
                obj->set(fieldName, strVal);
            } else {
                double val = att.getValue();
                if ((val != readerNullVal) && (val != kNullVal)) {
                    LEVELPRINT(READER_VERBOSE_PRINT,
                               component << ": setting " << fieldName << " to " << val);
                    obj->set(fieldName, val);
                } else {
                    gridParameter po(fieldName, att.getText());
                    paramStringProcess(po, ri);
                    if (po.stringType) {
                        obj->set(po.field, po.strVal);
                        LEVELPRINT(READER_VERBOSE_PRINT,
                                   component << ": setting " << fieldName << " to " << po.strVal);
                    } else {
                        obj->set(po.field, po.value);
                        LEVELPRINT(READER_VERBOSE_PRINT,
                                   component << ": setting " << fieldName << " to " << po.value);
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
               readerInfo& ri,
               const IgnoreListType& ignoreList)
{
    element->moveToFirstChild();
    while (element->isValid()) {
        std::string fieldName = convertToLowerCase(element->getName());
        auto ifind = ignoreList.find(fieldName);
        if (ifind != ignoreList.end()) {
            element->moveToNextSibling();
            continue;
        }

        auto param = getElementParam(element);
        if (param.valid) {
            try {
                if (param.stringType) {
                    if ((param.field.find("file") != std::string::npos) || (param.field == "fmu")) {
                        ri.checkFileParam(param.strVal);
                        LEVELPRINT(READER_VERBOSE_PRINT,
                                   component << ":setting " << obj->getName() << " file to "
                                             << param.strVal);
                        obj->set(param.field, param.strVal);
                    } else {
                        paramStringProcess(param, ri);
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
