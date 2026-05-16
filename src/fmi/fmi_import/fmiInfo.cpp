/*
 * Copyright (c) 2014-2026, Lawrence Livermore National Security
 * See the top-level NOTICE for additional details. All rights reserved.
 * SPDX-License-Identifier: BSD-3-Clause
 */

#include "fmiInfo.h"

#include "formatInterpreters/XmlReaderElement.h"
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
    std::shared_ptr<readerElement> readerElementPtr = std::make_shared<XmlReaderElement>(xmlFile);
    if (!readerElementPtr->isValid()) {
        return (-1);
    }
    headerInfo["xmlfile"] = xmlFile;
    headerInfo["xmlfilename"] = xmlFile;

    loadFmiHeader(readerElementPtr);
    loadUnitInformation(readerElementPtr);
    loadVariables(readerElementPtr);
    loadStructure(readerElementPtr);
    return 0;
}

static const std::map<std::string, int>& flagMap()
{
    static const auto* capabilityFlagMap = new std::map<std::string, int>{
        {"modelExchangeCapable", MODEL_EXCHANGE_CAPABLE},
        {"coSimulationCapable", CO_SIMULATION_CAPABLE},
        {"canGetAndSetFMUstate", CAN_GET_AND_SET_FMU_STATE},
        {"providesDirectionalDerivative", PROVIDES_DIRECTIONAL_DERIVATIVE},
        {"canSerializeFMUstate", CAN_SERIALIZE_FMU_STATE},
        {"canGetAndSetFMUstate", CAN_GET_AND_SET_FMU_STATE},
        {"needsExecutionTool", NEEDS_EXECUTION_TOOL},
        {"completedIntegratorStepNotNeeded", COMPLETED_INTEGRATOR_STEP_NOT_NEEDED},
        {"canHandleVariableCommunicationStepSize", CAN_HANDLE_VARIABLE_COMMUNICATION_STEP_SIZE},
        {"canInterpolateInputs", CAN_INTERPOLATE_INPUTS},
        {"canRunAsynchronously", CAN_RUN_ASYNCHRONOUSLY},
        {"canBeInstantiatedOnlyOncePerProcess", CAN_BE_INSTANTIATED_ONLY_ONCE_PER_PROCESS},
        {"canNotUseMemoryManagementFunctions", CAN_NOT_USE_MEMORY_MANAGEMENT_FUNCTIONS}};
    return *capabilityFlagMap;
}

static void loadFmuFlag(std::bitset<32>& capabilities, const readerAttribute& att)
{
    const auto& capabilityFlagMap = flagMap();
    auto fnd = capabilityFlagMap.find(att.getName());
    if (fnd != capabilityFlagMap.end()) {
        capabilities.set(fnd->second, (att.getText() == "true"));
    }
}

bool fmiInfo::checkFlag(FmuCapabilityFlags flag) const
{
    return capabilities[flag];
}

int fmiInfo::getCounts(const std::string& countType) const
{
    int cnt = -1;
    if (countType == "variables") {
        cnt = static_cast<int>(variables.size());
    } else if ((countType == "states") || (countType == "derivatives") || (countType == "state")) {
        cnt = static_cast<int>(states.size());
    } else if ((countType == "units") || (countType == "unit")) {
        cnt = static_cast<int>(units.size());
    } else if ((countType == "parameters") || (countType == "parameter")) {
        cnt = static_cast<int>(parameters.size());
    } else if ((countType == "local") || (countType == "locals")) {
        cnt = static_cast<int>(local.size());
    } else if ((countType == "initialunknowns") || (countType == "unknown")) {
        cnt = static_cast<int>(initUnknown.size());
    } else if ((countType == "outputs") || (countType == "output")) {
        cnt = static_cast<int>(outputs.size());
    } else if ((countType == "inputs") || (countType == "input")) {
        cnt = static_cast<int>(inputs.size());
    } else if (countType == "nonzeros") {
        cnt = static_cast<int>(derivDep.size());
    }
    if (cnt == -1) {
        return (-1);
    }
    return cnt;
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
        return defaultExperiment.startTime;
    }
    if ((fld == "stop") || (fld == "stoptime")) {
        return defaultExperiment.stopTime;
    }
    if ((fld == "step") || (fld == "stepsize")) {
        return defaultExperiment.stepSize;
    }
    if (fld == "tolerance") {
        return defaultExperiment.tolerance;
    }
    return (-1.0e-48);
}

static const VariableInformation& emptyVI()
{
    static const auto* emptyVariableInfo = new VariableInformation{};
    return *emptyVariableInfo;
}

static const FmiVariableSet& emptyVset()
{
    static const auto* emptyVariableSet = new FmiVariableSet{};
    return *emptyVariableSet;
}

const VariableInformation& fmiInfo::getVariableInfo(const std::string& variableName) const
{
    auto variablefind = variableLookup.find(variableName);
    if (variablefind != variableLookup.end()) {
        return variables[variablefind->second];
    }
    return emptyVI();
}

const VariableInformation& fmiInfo::getVariableInfo(unsigned int index) const
{
    if (index < variables.size()) {
        return variables[index];
    }
    return emptyVI();
}

FmiVariableSet fmiInfo::getReferenceSet(const std::vector<std::string>& variableList) const
{
    FmiVariableSet vset;
    for (const auto& vname : variableList) {
        auto vref = getVariableInfo(vname);
        if (vref.valueRef > 0) {
            vset.push(vref.valueRef);
        }
    }
    return vset;
}

FmiVariableSet fmiInfo::getVariableSet(const std::string& variable) const
{
    auto vref = getVariableInfo(variable);
    if (vref.valueRef > 0) {
        return {vref.valueRef};
    }
    return emptyVset();
}

FmiVariableSet fmiInfo::getVariableSet(unsigned int index) const
{
    if (index < variables.size()) {
        return {variables[index].valueRef};
    }
    return emptyVset();
}

FmiVariableSet fmiInfo::getOutputReference() const
{
    FmiVariableSet vset;
    vset.reserve(outputs.size());
    for (const auto& outInd : outputs) {
        vset.push(variables[outInd].valueRef);
    }
    return vset;
}

FmiVariableSet fmiInfo::getInputReference() const
{
    FmiVariableSet vset;
    vset.reserve(inputs.size());
    for (const auto& inInd : inputs) {
        vset.push(variables[inInd].valueRef);
    }
    return vset;
}

std::vector<std::string> fmiInfo::getVariableNames(const std::string& type) const
{
    std::vector<std::string> vnames;
    if (type == "state") {
        for (const auto& varIndex : states) {
            vnames.push_back(variables[varIndex].name);
        }
    } else {
        const FmiCausality caus = type;
        for (const auto& var : variables) {
            if ((caus == FmiCausalityType::any) || (var.causality == caus)) {
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

void fmiInfo::loadFmiHeader(std::shared_ptr<readerElement>& readerElementPtr)
{
    auto att = readerElementPtr->getFirstAttribute();
    while (att.isValid()) {
        headerInfo.emplace(att.getName(), att.getText());
        auto lcname = convertToLowerCase(att.getName());
        if (lcname != att.getName()) {
            headerInfo.emplace(convertToLowerCase(att.getName()), att.getText());
        }
        att = readerElementPtr->getNextAttribute();
    }
    // get the fmi version information
    auto versionFind = headerInfo.find("fmiversion");
    if (versionFind != headerInfo.end()) {
        fmiVersion = std::stod(versionFind->second);
    }
    if (readerElementPtr->hasElement("ModelExchange")) {
        capabilities.set(MODEL_EXCHANGE_CAPABLE, true);
        readerElementPtr->moveToFirstChild("ModelExchange");
        att = readerElementPtr->getFirstAttribute();
        while (att.isValid()) {
            if (att.getName() == "modelIdentifier") {
                headerInfo["MEIdentifier"] = att.getText();
                headerInfo["meidentifier"] = att.getText();
            } else {
                loadFmuFlag(capabilities, att);
            }

            att = readerElementPtr->getNextAttribute();
        }
        readerElementPtr->moveToParent();
    }
    if (readerElementPtr->hasElement("CoSimulation")) {
        readerElementPtr->moveToFirstChild("CoSimulation");
        capabilities.set(CO_SIMULATION_CAPABLE, true);
        att = readerElementPtr->getFirstAttribute();
        while (att.isValid()) {
            if (att.getName() == "modelIdentifier") {
                headerInfo["CoSimIdentifier"] = att.getText();
                headerInfo["cosimidentifier"] = att.getText();
            } else if (att.getName() == "maxOutputDerivativeOrder") {
                maxOrder = std::stoi(att.getText());
            } else {
                loadFmuFlag(capabilities, att);
            }

            att = readerElementPtr->getNextAttribute();
        }
        readerElementPtr->moveToParent();
    }
    if (readerElementPtr->hasElement("DefaultExperiment")) {
        readerElementPtr->moveToFirstChild("DefaultExperiment");
        att = readerElementPtr->getFirstAttribute();
        while (att.isValid()) {
            if (att.getName() == "startTime") {
                defaultExperiment.startTime = att.getValue();
            } else if (att.getName() == "stopTime") {
                defaultExperiment.stopTime = att.getValue();
            } else if (att.getName() == "stepSize") {
                defaultExperiment.stepSize = att.getValue();
            } else if (att.getName() == "tolerance") {
                defaultExperiment.tolerance = att.getValue();
            }

            att = readerElementPtr->getNextAttribute();
        }
        readerElementPtr->moveToParent();
    }
}

static void loadUnitInfo(std::shared_ptr<readerElement>& readerElementPtr, FmiUnit& unitInfo);

void fmiInfo::loadUnitInformation(std::shared_ptr<readerElement>& readerElementPtr)
{
    readerElementPtr->bookmark();
    readerElementPtr->moveToFirstChild("UnitDefinitions");
    readerElementPtr->moveToFirstChild("Unit");
    int vcount = 0;
    while (readerElementPtr->isValid()) {
        readerElementPtr->moveToNextSibling("Unit");
        ++vcount;
    }
    units.resize(vcount);
    readerElementPtr->moveToParent();
    // now load the variables
    readerElementPtr->moveToFirstChild("Unit");
    int unitIndex = 0;
    while (readerElementPtr->isValid()) {
        loadUnitInfo(readerElementPtr, units[unitIndex]);
        readerElementPtr->moveToNextSibling("Unit");
        ++unitIndex;
    }
    readerElementPtr->restore();
}

static void loadUnitInfo(std::shared_ptr<readerElement>& readerElementPtr, FmiUnit& unitInfo)
{
    unitInfo.name = readerElementPtr->getAttributeText("name");
    if (readerElementPtr->hasElement("BaseUnit")) {
        readerElementPtr->moveToFirstChild("BaseUnit");
        auto att = readerElementPtr->getFirstAttribute();
        while (att.isValid()) {
            if (att.getName() == "offset") {
                unitInfo.offset = att.getValue();
            } else if (att.getName() == "factor") {
                unitInfo.factor = att.getValue();
            } else {
                // unitInfo.baseUnits.emplace_back(att.getName(), att.getValue());
            }
            att = readerElementPtr->getNextAttribute();
        }
        readerElementPtr->moveToParent();
    }

    if (readerElementPtr->hasElement("DisplayUnit")) {
        readerElementPtr->moveToFirstChild("DisplayUnit");
        while (readerElementPtr->isValid()) {
            UnitDef displayUnit;
            displayUnit.name = readerElementPtr->getAttributeText("name");
            displayUnit.factor = readerElementPtr->getAttributeValue("factor");
            displayUnit.offset = readerElementPtr->getAttributeValue("offset");
            unitInfo.displayUnits.push_back(displayUnit);
            readerElementPtr->moveToNextSibling("DisplayUnit");
        }
        readerElementPtr->moveToParent();
    }
}

/** load a single variable information from the XML
@param[in] readerElementPtr the readerElement to load from
@param[out] vInfo the variable information to store the data to
*/
static void loadVariableInfo(std::shared_ptr<readerElement>& readerElementPtr,
                             VariableInformation& vInfo);

/*
valueReference="100663424"
description="Constant output value"
variability="tunable"
*/

static const char ScalarVString[] = "ScalarVariable";
void fmiInfo::loadVariables(std::shared_ptr<readerElement>& readerElementPtr)
{
    readerElementPtr->bookmark();
    readerElementPtr->moveToFirstChild("ModelVariables");
    // Loop over the variables to be able to allocate memory efficiently later on
    readerElementPtr->moveToFirstChild(ScalarVString);
    int vcount = 0;

    while (readerElementPtr->isValid()) {
        ++vcount;
        readerElementPtr->moveToNextSibling(ScalarVString);
    }
    variables.resize(vcount);
    readerElementPtr->moveToParent();
    // now load the variables
    readerElementPtr->moveToFirstChild(ScalarVString);
    int variableIndex = 0;
    while (readerElementPtr->isValid()) {
        loadVariableInfo(readerElementPtr, variables[variableIndex]);
        variables[variableIndex].index = variableIndex;
        auto res = variableLookup.emplace(variables[variableIndex].name, variableIndex);
        if (!res.second) {  // if we failed on the emplace operation, then we need to override
            // this should be unusual but it is possible
            variableLookup[variables[variableIndex].name] = variableIndex;
        }
        // this one may fail and that is ok since this is a secondary detection mechanism for purely
        // lower case parameters and may not be needed
        variableLookup.emplace(convertToLowerCase(variables[variableIndex].name), variableIndex);
        switch (variables[variableIndex].causality.value()) {
            case FmiCausalityType::parameter:
                parameters.push_back(variableIndex);
                break;
            case FmiCausalityType::local:
                local.push_back(variableIndex);
                break;
            case FmiCausalityType::input:
                inputs.push_back(variableIndex);
                break;
            default:
                break;
        }
        readerElementPtr->moveToNextSibling(ScalarVString);
        ++variableIndex;
    }
    readerElementPtr->restore();
}

static void loadVariableInfo(std::shared_ptr<readerElement>& readerElementPtr,
                             VariableInformation& vInfo)
{
    auto att = readerElementPtr->getFirstAttribute();
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
        att = readerElementPtr->getNextAttribute();
    }
    if (readerElementPtr->hasElement("Real")) {
        vInfo.type = FmiVariableType::real;
        readerElementPtr->moveToFirstChild("Real");
        att = readerElementPtr->getFirstAttribute();
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
            att = readerElementPtr->getNextAttribute();
        }
        readerElementPtr->moveToParent();
    } else if (readerElementPtr->hasElement("Boolean")) {
        vInfo.type = FmiVariableType::boolean;
        readerElementPtr->moveToFirstChild("Boolean");
        att = readerElementPtr->getFirstAttribute();
        while (att.isValid()) {
            if (att.getName() == "start") {
                vInfo.start = (att.getText() == "true") ? 1.0 : 0.0;
            }
            att = readerElementPtr->getNextAttribute();
        }
        readerElementPtr->moveToParent();
    } else if (readerElementPtr->hasElement("String")) {
        vInfo.type = FmiVariableType::string;
        readerElementPtr->moveToFirstChild("String");
        att = readerElementPtr->getFirstAttribute();
        while (att.isValid()) {
            if (att.getName() == "start") {
                vInfo.initial = att.getText();
            }
            att = readerElementPtr->getNextAttribute();
        }
        readerElementPtr->moveToParent();
    } else if (readerElementPtr->hasElement("Integer")) {
        vInfo.type = FmiVariableType::integer;
        readerElementPtr->moveToFirstChild("Integer");
        att = readerElementPtr->getFirstAttribute();
        while (att.isValid()) {
            if (att.getName() == "start") {
                vInfo.initial = att.getValue();
            } else if (att.getName() == "min") {
                vInfo.min = att.getValue();
            } else if (att.getName() == "max") {
                vInfo.max = att.getValue();
            }
            att = readerElementPtr->getNextAttribute();
        }
        readerElementPtr->moveToParent();
    }
}

static auto depkindNum(const std::string& depknd)
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

static void loadDependencies(std::shared_ptr<readerElement>& readerElementPtr,
                             std::vector<int>& store,
                             matrixData<int>& depData)
{
    using gmlc::utilities::stringOps::delimiter_compression;
    using gmlc::utilities::stringOps::splitline;

    readerElementPtr->moveToFirstChild(unknownString);
    while (readerElementPtr->isValid()) {
        auto att = readerElementPtr->getAttribute("index");
        auto attDep = readerElementPtr->getAttribute(depString);
        auto attDepKind = readerElementPtr->getAttribute(depKindString);
        auto row = static_cast<index_t>(att.getValue());
        auto dep = str2vector<int>(attDep.getText(), 0, " ");
        auto depknd = (attDepKind.isValid()) ?
            splitline(attDepKind.getText(), " ", delimiter_compression::on) :
            stringVector();
        store.push_back(row - 1);
        const auto validdepkind = !depknd.empty();
        for (size_t dependencyIndex = 0; dependencyIndex < dep.size(); ++dependencyIndex) {
            if (dep[dependencyIndex] > 0) {
                depData.assign(row - 1,
                               dep[dependencyIndex] - 1,
                               validdepkind ? depkindNum(depknd[dependencyIndex]) : 1);
            }
        }
        readerElementPtr->moveToNextSibling(unknownString);
    }
    readerElementPtr->moveToParent();
}

void fmiInfo::loadStructure(std::shared_ptr<readerElement>& readerElementPtr)
{
    readerElementPtr->bookmark();
    // get the output dependencies
    outputDep.setRowLimit(static_cast<index_t>(variables.size()));
    readerElementPtr->moveToFirstChild("ModelStructure");

    readerElementPtr->moveToFirstChild("Outputs");
    if (readerElementPtr->isValid()) {
        loadDependencies(readerElementPtr, outputs, outputDep);
    }
    readerElementPtr->moveToParent();
    // get the derivative dependencies
    readerElementPtr->moveToFirstChild("Derivatives");
    derivDep.setRowLimit(static_cast<index_t>(variables.size()));
    if (readerElementPtr->isValid()) {
        loadDependencies(readerElementPtr, deriv, derivDep);
        for (const auto& der : deriv) {
            states.push_back(variables[der].derivativeIndex);
        }
    }

    readerElementPtr->moveToParent();
    // get the initial unknowns dependencies
    unknownDep.setRowLimit(static_cast<index_t>(variables.size()));
    readerElementPtr->moveToFirstChild("InitialUnknowns");
    if (readerElementPtr->isValid()) {
        loadDependencies(readerElementPtr, initUnknown, unknownDep);
    }
    readerElementPtr->restore();
}

bool checkType(const VariableInformation& info, FmiVariableType type, FmiCausalityType caus)
{
    if (!(info.causality == caus)) {
        if ((info.causality != FmiCausalityType::input) || (caus != FmiCausalityType::parameter)) {
            return false;
        }
    }
    if (info.type == type) {
        return true;
    }
    if (type == FmiVariableType::numeric) {
        switch (info.type.value()) {
            case FmiVariableType::boolean:
            case FmiVariableType::integer:
            case FmiVariableType::real:
            case FmiVariableType::enumeration:
                return true;
            default:
                return false;
        }
    }
    return false;
}
