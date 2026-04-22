/*
 * Copyright (c) 2014-2026, Lawrence Livermore National Security
 * See the top-level NOTICE for additional details. All rights reserved.
 * SPDX-License-Identifier: BSD-3-Clause
 */

#include "fmiInfo.h"

#include "formatInterpreters/tinyxml2ReaderElement.h"
#include "gmlc/utilities/stringConversion.h"
#include <map>
#include <memory>
#include <string>
#include <vector>

using gmlc::utilities::convertToLowerCase;
using gmlc::utilities::str2vector;
using gmlc::utilities::stringVector;

fmiInfo::fmiInfo() = default;

fmiInfo::fmiInfo(const std::string& xmlFile)
{
    loadFile(xmlFile);
}

int fmiInfo::loadFile(const std::string& xmlFile)
{
    std::shared_ptr<readerElement> rd = std::make_shared<tinyxml2ReaderElement>(xmlFile);
    if (!rd->isValid()) {
        return (-1);
    }
    headerInfo["xmlfile"] = xmlFile;
    headerInfo["xmlfilename"] = xmlFile;

    loadFmiHeader(rd);
    loadUnitInformation(rd);
    loadVariables(rd);
    loadStructure(rd);
    return 0;
}

static const std::map<std::string, int> flagMap{
    {"modelExchangeCapable", modelExchangeCapable},
    {"coSimulationCapable", coSimulationCapable},
    {"canGetAndSetFMUstate", canGetAndSetFMUstate},
    {"providesDirectionalDerivative", providesDirectionalDerivative},
    {"canSerializeFMUstate", canSerializeFMUstate},
    {"canGetAndSetFMUstate", canGetAndSetFMUstate},
    {"needsExecutionTool", needsExecutionTool},
    {"completedIntegratorStepNotNeeded", completedIntegratorStepNotNeeded},
    {"canHandleVariableCommunicationStepSize", canHandleVariableCommunicationStepSize},
    {"canInterpolateInputs", canInterpolateInputs},
    {"canRunAsynchronously", canRunAsynchronously},
    {"canBeInstantiatedOnlyOncePerProcess", canBeInstantiatedOnlyOncePerProcess},
    {"canNotUseMemoryManagementFunctions", canNotUseMemoryManagementFunctions}};

void loadFmuFlag(std::bitset<32>& capabilities, const readerAttribute& att)
{
    auto fnd = flagMap.find(att.getName());
    if (fnd != flagMap.end()) {
        capabilities.set(fnd->second, (att.getText() == "true"));
    }
}

bool fmiInfo::checkFlag(fmuCapabilityFlags flag) const
{
    return capabilities[flag];
}

int fmiInfo::getCounts(const std::string& countType) const
{
    auto cnt = size_t(-1);
    if (countType == "variables") {
        cnt = variables.size();
    } else if ((countType == "states") || (countType == "derivatives") || (countType == "state")) {
        cnt = states.size();
    } else if ((countType == "units") || (countType == "unit")) {
        cnt = units.size();
    } else if ((countType == "parameters") || (countType == "parameter")) {
        cnt = parameters.size();
    } else if ((countType == "local") || (countType == "locals")) {
        cnt = local.size();
    } else if ((countType == "initialunknowns") || (countType == "unknown")) {
        cnt = initUnknown.size();
    } else if ((countType == "outputs") || (countType == "output")) {
        cnt = outputs.size();
    } else if ((countType == "inputs") || (countType == "input")) {
        cnt = inputs.size();
    } else if (countType == "nonzeros") {
        cnt = derivDep.size();
    }
    if (cnt == size_t(-1)) {
        return (-1);
    }
    return static_cast<int>(cnt);
}

static const char emptyString[] = "";

const std::string& fmiInfo::getString(const std::string& field) const
{
    static const std::string emptyStringValue{emptyString};
    auto fnd = headerInfo.find(field);
    if (fnd != headerInfo.end()) {
        return fnd->second;
    }
    return emptyStringValue;
}

double fmiInfo::getReal(const std::string& field) const
{
    auto fld = convertToLowerCase(field);
    if (fld == "version") {
        return fmiVersion;
    }
    if ((fld == "start") || (fld == "starttime")) {
        return defaultExpirement.startTime;
    }
    if ((fld == "stop") || (fld == "stoptime")) {
        return defaultExpirement.stopTime;
    }
    if ((fld == "step") || (fld == "stepsize")) {
        return defaultExpirement.stepSize;
    }
    if (fld == "tolerance") {
        return defaultExpirement.tolerance;
    }
    return (-1.0e-48);
}

static const variableInformation emptyVI{};
static const fmiVariableSet emptyVset;

const variableInformation& fmiInfo::getVariableInfo(const std::string& variableName) const
{
    auto variablefind = variableLookup.find(variableName);
    if (variablefind != variableLookup.end()) {
        return variables[variablefind->second];
    }
    return emptyVI;
}

const variableInformation& fmiInfo::getVariableInfo(unsigned int index) const
{
    if (index < variables.size()) {
        return variables[index];
    }
    return emptyVI;
}

fmiVariableSet fmiInfo::getReferenceSet(const std::vector<std::string>& variableList) const
{
    fmiVariableSet vset;
    for (auto& vname : variableList) {
        auto vref = getVariableInfo(vname);
        if (vref.valueRef > 0) {
            vset.push(vref.valueRef);
        }
    }
    return vset;
}

fmiVariableSet fmiInfo::getVariableSet(const std::string& variable) const
{
    auto vref = getVariableInfo(variable);
    if (vref.valueRef > 0) {
        return fmiVariableSet(vref.valueRef);
    }
    return emptyVset;
}

fmiVariableSet fmiInfo::getVariableSet(unsigned int index) const
{
    if (index < variables.size()) {
        return fmiVariableSet(variables[index].valueRef);
    }
    return emptyVset;
}

fmiVariableSet fmiInfo::getOutputReference() const
{
    fmiVariableSet vset;
    vset.reserve(outputs.size());
    for (auto& outInd : outputs) {
        vset.push(variables[outInd].valueRef);
    }
    return vset;
}

fmiVariableSet fmiInfo::getInputReference() const
{
    fmiVariableSet vset;
    vset.reserve(inputs.size());
    for (auto& inInd : inputs) {
        vset.push(variables[inInd].valueRef);
    }
    return vset;
}

std::vector<std::string> fmiInfo::getVariableNames(const std::string& type) const
{
    std::vector<std::string> vnames;
    if (type == "state") {
        for (auto& varIndex : states) {
            vnames.push_back(variables[varIndex].name);
        }
    } else {
        fmi_causality caus = type;
        for (auto& var : variables) {
            if ((caus == fmi_causality_type_t::any) || (var.causality == caus)) {
                vnames.push_back(var.name);
            }
        }
    }

    return vnames;
}

static const std::vector<int> emptyVec;

const std::vector<int>& fmiInfo::getVariableIndices(const std::string& type) const
{
    if (type == "state") {
        return states;
    }
    if (type == "deriv") {
        return deriv;
    }
    if (type == "parameter") {
        return parameters;
    }
    if ((type == "inputs") || (type == "input")) {
        return inputs;
    }
    if ((type == "outputs") || (type == "output")) {
        return outputs;
    }
    if (type == "local") {
        return local;
    }
    if (type == "unknown") {
        return initUnknown;
    }
    return emptyVec;
}

/** get the variable indices of the derivative dependencies*/
const std::vector<std::pair<index_t, int>>& fmiInfo::getDerivDependencies(int variableIndex) const
{
    return derivDep.getSet(variableIndex);
}
const std::vector<std::pair<index_t, int>>& fmiInfo::getOutputDependencies(int variableIndex) const
{
    return outputDep.getSet(variableIndex);
}
const std::vector<std::pair<index_t, int>>& fmiInfo::getUnknownDependencies(int variableIndex) const
{
    return unknownDep.getSet(variableIndex);
}

void fmiInfo::loadFmiHeader(std::shared_ptr<readerElement>& rd)
{
    auto att = rd->getFirstAttribute();
    while (att.isValid()) {
        headerInfo.emplace(att.getName(), att.getText());
        auto lcname = convertToLowerCase(att.getName());
        if (lcname != att.getName()) {
            headerInfo.emplace(convertToLowerCase(att.getName()), att.getText());
        }
        att = rd->getNextAttribute();
    }
    // get the fmi version information
    auto versionFind = headerInfo.find("fmiversion");
    if (versionFind != headerInfo.end()) {
        fmiVersion = std::stod(versionFind->second);
    }
    if (rd->hasElement("ModelExchange")) {
        capabilities.set(modelExchangeCapable, true);
        rd->moveToFirstChild("ModelExchange");
        att = rd->getFirstAttribute();
        while (att.isValid()) {
            if (att.getName() == "modelIdentifier") {
                headerInfo["MEIdentifier"] = att.getText();
                headerInfo["meidentifier"] = att.getText();
            } else {
                loadFmuFlag(capabilities, att);
            }

            att = rd->getNextAttribute();
        }
        rd->moveToParent();
    }
    if (rd->hasElement("CoSimulation")) {
        rd->moveToFirstChild("CoSimulation");
        capabilities.set(coSimulationCapable, true);
        att = rd->getFirstAttribute();
        while (att.isValid()) {
            if (att.getName() == "modelIdentifier") {
                headerInfo["CoSimIdentifier"] = att.getText();
                headerInfo["cosimidentifier"] = att.getText();
            } else if (att.getName() == "maxOutputDerivativeOrder") {
                maxOrder = std::stoi(att.getText());
            } else {
                loadFmuFlag(capabilities, att);
            }

            att = rd->getNextAttribute();
        }
        rd->moveToParent();
    }
    if (rd->hasElement("DefaultExperiment")) {
        rd->moveToFirstChild("DefaultExperiment");
        att = rd->getFirstAttribute();
        while (att.isValid()) {
            if (att.getName() == "startTime") {
                defaultExpirement.startTime = att.getValue();
            } else if (att.getName() == "stopTime") {
                defaultExpirement.stopTime = att.getValue();
            } else if (att.getName() == "stepSize") {
                defaultExpirement.stepSize = att.getValue();
            } else if (att.getName() == "tolerance") {
                defaultExpirement.tolerance = att.getValue();
            }

            att = rd->getNextAttribute();
        }
        rd->moveToParent();
    }
}

void loadUnitInfo(std::shared_ptr<readerElement>& rd, fmiUnit& unitInfo);

void fmiInfo::loadUnitInformation(std::shared_ptr<readerElement>& rd)
{
    rd->bookmark();
    rd->moveToFirstChild("UnitDefinitions");
    rd->moveToFirstChild("Unit");
    int vcount = 0;
    while (rd->isValid()) {
        rd->moveToNextSibling("Unit");
        ++vcount;
    }
    units.resize(vcount);
    rd->moveToParent();
    // now load the variables
    rd->moveToFirstChild("Unit");
    int kk = 0;
    while (rd->isValid()) {
        loadUnitInfo(rd, units[kk]);
        rd->moveToNextSibling("Unit");
        ++kk;
    }
    rd->restore();
}

void loadUnitInfo(std::shared_ptr<readerElement>& rd, fmiUnit& unitInfo)
{
    unitInfo.name = rd->getAttributeText("name");
    if (rd->hasElement("BaseUnit")) {
        rd->moveToFirstChild("BaseUnit");
        auto att = rd->getFirstAttribute();
        while (att.isValid()) {
            if (att.getName() == "offset") {
                unitInfo.offset = att.getValue();
            } else if (att.getName() == "factor") {
                unitInfo.factor = att.getValue();
            } else {
                // unitInfo.baseUnits.emplace_back(att.getName(), att.getValue());
            }
            att = rd->getNextAttribute();
        }
        rd->moveToParent();
    }

    if (rd->hasElement("DisplayUnit")) {
        rd->moveToFirstChild("DisplayUnit");
        while (rd->isValid()) {
            unitDef Dunit;
            Dunit.name = rd->getAttributeText("name");
            Dunit.factor = rd->getAttributeValue("factor");
            Dunit.offset = rd->getAttributeValue("offset");
            unitInfo.displayUnits.push_back(Dunit);
            rd->moveToNextSibling("DisplayUnit");
        }
        rd->moveToParent();
    }
}

/** load a single variable information from the XML
@param[in] rd the readerElement to load from
@param[out] vInfo the variable information to store the data to
*/
void loadVariableInfo(std::shared_ptr<readerElement>& rd, variableInformation& vInfo);

/*
valueReference="100663424"
description="Constant output value"
variability="tunable"
*/

static const char ScalarVString[] = "ScalarVariable";
void fmiInfo::loadVariables(std::shared_ptr<readerElement>& rd)
{
    rd->bookmark();
    rd->moveToFirstChild("ModelVariables");
    // Loop over the variables to be able to allocate memory efficiently later on
    rd->moveToFirstChild(ScalarVString);
    int vcount = 0;

    while (rd->isValid()) {
        ++vcount;
        rd->moveToNextSibling(ScalarVString);
    }
    variables.resize(vcount);
    rd->moveToParent();
    // now load the variables
    rd->moveToFirstChild(ScalarVString);
    int kk = 0;
    while (rd->isValid()) {
        loadVariableInfo(rd, variables[kk]);
        variables[kk].index = kk;
        auto res = variableLookup.emplace(variables[kk].name, kk);
        if (!res.second) {  // if we failed on the emplace operation, then we need to override
            // this should be unusual but it is possible
            variableLookup[variables[kk].name] = kk;
        }
        // this one may fail and that is ok since this is a secondary detection mechanism for purely
        // lower case parameters and may not be needed
        variableLookup.emplace(convertToLowerCase(variables[kk].name), kk);
        switch (variables[kk].causality.value()) {
            case fmi_causality_type_t::parameter:
                parameters.push_back(kk);
                break;
            case fmi_causality_type_t::local:
                local.push_back(kk);
                break;
            case fmi_causality_type_t::input:
                inputs.push_back(kk);
                break;
            default:
                break;
        }
        rd->moveToNextSibling(ScalarVString);
        ++kk;
    }
    rd->restore();
}

void loadVariableInfo(std::shared_ptr<readerElement>& rd, variableInformation& vInfo)
{
    auto att = rd->getFirstAttribute();
    while (att.isValid()) {
        if (att.getName() == "name") {
            vInfo.name = att.getText();
        } else if (att.getName() == "valueReference") {
            vInfo.valueRef = static_cast<fmi2ValueReference>(att.getInt());
        } else if (att.getName() == "description") {
            vInfo.description = att.getText();
        } else if (att.getName() == "variability") {
            vInfo.variability = att.getText();
        } else if (att.getName() == "causality") {
            vInfo.causality = att.getText();
        } else if (att.getName() == "initial") {
            vInfo.initial = att.getText();
        }
        att = rd->getNextAttribute();
    }
    if (rd->hasElement("Real")) {
        vInfo.type = fmi_variable_type_t::real;
        rd->moveToFirstChild("Real");
        att = rd->getFirstAttribute();
        while (att.isValid()) {
            if (att.getName() == "declaredType") {
                vInfo.declType = att.getText();
            } else if (att.getName() == "unit") {
                vInfo.unit = att.getText();
            } else if (att.getName() == "start") {
                vInfo.start = att.getValue();
            } else if (att.getName() == "derivative") {
                vInfo.derivative = true;
                vInfo.derivativeIndex = static_cast<int>(att.getInt());
            } else if (att.getName() == "min") {
                vInfo.min = att.getValue();
            } else if (att.getName() == "max") {
                vInfo.max = att.getValue();
            }
            att = rd->getNextAttribute();
        }
        rd->moveToParent();
    } else if (rd->hasElement("Boolean")) {
        vInfo.type = fmi_variable_type_t::boolean;
        rd->moveToFirstChild("Boolean");
        att = rd->getFirstAttribute();
        while (att.isValid()) {
            if (att.getName() == "start") {
                vInfo.start = (att.getText() == "true") ? 1.0 : 0.0;
            }
            att = rd->getNextAttribute();
        }
        rd->moveToParent();
    } else if (rd->hasElement("String")) {
        vInfo.type = fmi_variable_type_t::string;
        rd->moveToFirstChild("String");
        att = rd->getFirstAttribute();
        while (att.isValid()) {
            if (att.getName() == "start") {
                vInfo.initial = att.getText();
            }
            att = rd->getNextAttribute();
        }
        rd->moveToParent();
    } else if (rd->hasElement("Integer")) {
        vInfo.type = fmi_variable_type_t::integer;
        rd->moveToFirstChild("Integer");
        att = rd->getFirstAttribute();
        while (att.isValid()) {
            if (att.getName() == "start") {
                vInfo.initial = att.getValue();
            } else if (att.getName() == "min") {
                vInfo.min = att.getValue();
            } else if (att.getName() == "max") {
                vInfo.max = att.getValue();
            }
            att = rd->getNextAttribute();
        }
        rd->moveToParent();
    }
}

auto depkindNum(const std::string& depknd)
{
    if (depknd == "dependent") {
        return 1;
    }
    if (depknd == "fixed") {
        return 2;
    }
    if (depknd == "constant") {
        return 3;
    }
    if (depknd == "tunable") {
        return 4;
    }
    if (depknd == "discrete") {
        return 5;
    }
    return 6;
}

static const char unknownString[] = "Unknown";
static const char depString[] = "dependencies";
static const char depKindString[] = "dependenciesKind";

void loadDependencies(std::shared_ptr<readerElement>& rd,
                      std::vector<int>& store,
                      matrixData<int>& depData)
{
    using gmlc::utilities::stringOps::delimiter_compression;
    using gmlc::utilities::stringOps::splitline;

    rd->moveToFirstChild(unknownString);
    while (rd->isValid()) {
        auto att = rd->getAttribute("index");
        auto attDep = rd->getAttribute(depString);
        auto attDepKind = rd->getAttribute(depKindString);
        auto row = static_cast<index_t>(att.getValue());
        auto dep = str2vector<int>(attDep.getText(), 0, " ");
        auto depknd = (attDepKind.isValid()) ?
            splitline(attDepKind.getText(), " ", delimiter_compression::on) :
            stringVector();
        store.push_back(row - 1);
        auto validdepkind = !depknd.empty();
        for (size_t kk = 0; kk < dep.size(); ++kk) {
            if (dep[kk] > 0) {
                depData.assign(row - 1, dep[kk] - 1, (validdepkind) ? (depkindNum(depknd[kk])) : 1);
            }
        }
        rd->moveToNextSibling(unknownString);
    }
    rd->moveToParent();
}

void fmiInfo::loadStructure(std::shared_ptr<readerElement>& rd)
{
    rd->bookmark();
    // get the output dependencies
    outputDep.setRowLimit(static_cast<index_t>(variables.size()));
    rd->moveToFirstChild("ModelStructure");

    rd->moveToFirstChild("Outputs");
    if (rd->isValid()) {
        loadDependencies(rd, outputs, outputDep);
    }
    rd->moveToParent();
    // get the derivative dependencies
    rd->moveToFirstChild("Derivatives");
    derivDep.setRowLimit(static_cast<index_t>(variables.size()));
    if (rd->isValid()) {
        loadDependencies(rd, deriv, derivDep);
        for (auto& der : deriv) {
            states.push_back(variables[der].derivativeIndex);
        }
    }

    rd->moveToParent();
    // get the initial unknowns dependencies
    unknownDep.setRowLimit(static_cast<index_t>(variables.size()));
    rd->moveToFirstChild("InitialUnknowns");
    if (rd->isValid()) {
        loadDependencies(rd, initUnknown, unknownDep);
    }
    rd->restore();
}

bool checkType(const variableInformation& info, fmi_variable_type_t type, fmi_causality_type_t caus)
{
    if (!(info.causality == caus)) {
        if (!((info.causality == fmi_causality_type_t::input) &&
              (caus == fmi_causality_type_t::parameter))) {
            return false;
        }
    }
    if (info.type == type) {
        return true;
    }
    if (type == fmi_variable_type_t::numeric) {
        switch (info.type.value()) {
            case fmi_variable_type_t::boolean:
            case fmi_variable_type_t::integer:
            case fmi_variable_type_t::real:
            case fmi_variable_type_t::enumeration:
                return true;
            default:
                return false;
        }
    }
    return false;
}
