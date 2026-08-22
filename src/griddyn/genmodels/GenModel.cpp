/*
 * Copyright (c) 2014-2026, Lawrence Livermore National Security
 * See the top-level NOTICE for additional details. All rights reserved.
 * SPDX-License-Identifier: BSD-3-Clause
 */

#include "../Generator.h"
#include "../GridBus.h"
#include "core/CoreExceptions.h"
#include "core/CoreObjectTemplates.hpp"
#include "core/ObjectFactoryTemplates.hpp"
#include "gmlc/utilities/vectorOps.hpp"
#include "otherGenModels.h"
#include "utilities/MatrixData.hpp"
#include <string>
#include <vector>

namespace griddyn {
namespace {
    const std::vector<stringVec>& genModelInputNames()
    {
        static const std::vector<stringVec> inputNames{
            {"voltage", "v", "volt"},
            {"angle", "ang", "a"},
            {"eft", "e", "field", "exciter"},
            {"pmech", "power", "p", "mech"},
        };
        return inputNames;
    }

    const std::vector<stringVec>& genModelOutputNames()
    {
        static const std::vector<stringVec> outputNames{
            {"e", "field", "exciter"},
        };
        return outputNames;
    }
}  // namespace

static TypeFactory<GenModel> gGenModelFactory("genmodel",
                                              std::to_array<std::string_view>({"trivial"}));
static ChildTypeFactory<griddyn::genmodels::GenModelInverter, GenModel>
    gInverterGenModelFactory("genmodel", std::to_array<std::string_view>({"inverter"}));
static ChildTypeFactory<griddyn::genmodels::GenModelClassical, GenModel> gClassicalGenModelFactory(
    "genmodel",
    std::to_array<std::string_view>(
        {"basic", "2", "second", "secondorder", "classic", "classical", "II"}));
static ChildTypeFactory<griddyn::genmodels::GenModel3, GenModel> gThirdOrderGenModelFactory(
    "genmodel",
    std::to_array<std::string_view>({"3", "third", "thirdorder", "III"}));
static ChildTypeFactory<griddyn::genmodels::GenModel4, GenModel> gFourthOrderGenModelFactory(
    "genmodel",
    std::to_array<std::string_view>({"4", "fourth", "fourthorder", "IV", "grdc"}),
    "4");
static ChildTypeFactory<griddyn::genmodels::GenModel5, GenModel> gFifthOrderGenModelFactory(
    "genmodel",
    std::to_array<std::string_view>({"5", "fifth", "fifthorder", "5.1", "Vtype1", "V"}));
static ChildTypeFactory<griddyn::genmodels::GenModel5type2, GenModel>
    gFifthOrderGenModelType2Factory(
        "genmodel",
        std::to_array<std::string_view>({"5.2", "fifthtype2", "fifthordertype2", "Vtype2"}));
static ChildTypeFactory<griddyn::genmodels::GenModel6, GenModel>
    gSixthOrderGenModelFactory("genmodel",
                               std::to_array<std::string_view>({"6", "six", "sixthorder", "VI"}));
static ChildTypeFactory<griddyn::genmodels::GenModel6type2, GenModel>
    gSixthOrderGenModelType2Factory(
        "genmodel",
        std::to_array<std::string_view>({"6.2", "sixtype2", "sixthordertype2", "VItype2", "VI.2"}));
static ChildTypeFactory<griddyn::genmodels::GenModelGENROU, GenModel>
    gGenrouModelFactory("genmodel", std::to_array<std::string_view>({"genrou"}));
static ChildTypeFactory<griddyn::genmodels::GenModel8, GenModel> gEighthOrderGenModelFactory(
    "genmodel",
    std::to_array<std::string_view>({"8", "eight", "eighthorder", "VIII"}));

GenModel::GenModel(const std::string& objName): GridSubModel(objName)
{
    m_inputSize = 4;
    m_outputSize = 2;
}
CoreObject* GenModel::clone(CoreObject* obj) const
{
    auto* genModelClone = cloneBase<GenModel, GridSubModel>(this, obj);
    if (genModelClone == nullptr) {
        return obj;
    }

    genModelClone->Rs = Rs;
    genModelClone->Xd = Xd;
    genModelClone->machineBasePower = machineBasePower;
    return genModelClone;
}

// initial conditions

void GenModel::dynObjectInitializeB(const IOdata& inputs,
                                    const IOdata& desiredOutput,
                                    IOdata& fieldSet)
{
    if (inputs[VOLTAGE_IN_LOCATION] > 0.85) {
        fieldSet[genModelPmechInLocation] = desiredOutput[POUT_LOCATION];  // Pmt
        fieldSet[genModelEftInLocation] = desiredOutput[QOUT_LOCATION] / Xd;
    } else {
        fieldSet[genModelPmechInLocation] =
            desiredOutput[POUT_LOCATION] / inputs[VOLTAGE_IN_LOCATION] * 0.85;  // Pmt
        fieldSet[genModelEftInLocation] =
            desiredOutput[QOUT_LOCATION] / Xd / inputs[VOLTAGE_IN_LOCATION] * 0.85;
    }

    bus = static_cast<GridBus*>(find("bus"));
}

// residual

double GenModel::getFreq(const StateData& stateDataValue,
                         const SolverMode& sMode,
                         index_t* freqOffset) const
{
    // there is no inertia in this gen model so it can't compute a frequency and
    // must use the bus frequency
    if (freqOffset != nullptr) {
        *freqOffset = bus->getOutputLoc(sMode, FREQUENCY_IN_LOCATION);
    }
    return bus->getFreq(stateDataValue, sMode);
}

double GenModel::getAngle(const StateData& /*stateDataValue*/,
                          const SolverMode& /*sMode*/,
                          index_t* angleOffset) const
{
    // there is no inertia in this gen model so it can't compute a frequency and
    // must use the bus frequency
    if (angleOffset != nullptr) {
        *angleOffset = kNullLocation;
    }
    return kNullVal;
}

IOdata GenModel::getMachineControllerSignals(const IOdata& /*inputs*/,
                                             const StateData& /*stateDataValue*/,
                                             const SolverMode& /*sMode*/) const
{
    const IOdata signals(machineControllerSignalCount, kNullVal);
    return signals;
}

MachineSignalDerivativeData
    GenModel::getMachineControllerSignalDerivatives(const IOdata& /*inputs*/,
                                                    const StateData& /*stateDataValue*/,
                                                    const IOlocs& /*inputLocs*/,
                                                    const SolverMode& /*sMode*/) const
{
    return {};
}

count_t GenModel::outputDependencyCount(index_t /*num*/, const SolverMode& /*sMode*/) const
{
    return 0;
}
IOdata GenModel::getOutputs(const IOdata& inputs,
                            const StateData& /*stateDataValue*/,
                            const SolverMode& /*sMode*/) const
{
    IOdata out(2);
    const double voltage = inputs[VOLTAGE_IN_LOCATION];
    const double exciterField = inputs[genModelEftInLocation];
    if (voltage > 0.85) {
        out[POUT_LOCATION] = -inputs[genModelPmechInLocation];
        out[QOUT_LOCATION] = -exciterField * Xd;
    } else {
        out[POUT_LOCATION] = -inputs[genModelPmechInLocation] * voltage / 0.85;
        out[QOUT_LOCATION] = -exciterField * Xd * voltage / 0.85;
    }

    return out;
}

double GenModel::getOutput(const IOdata& inputs,
                           const StateData& /*stateDataValue*/,
                           const SolverMode& /*sMode*/,
                           index_t outNum) const
{
    const double voltage = inputs[VOLTAGE_IN_LOCATION];
    const double exciterField = inputs[genModelEftInLocation];
    if (voltage > 0.85) {
        if (outNum == POUT_LOCATION) {
            return -inputs[genModelPmechInLocation];
        }
        if (outNum == QOUT_LOCATION) {
            return -exciterField * Xd;
        }
    } else {
        if (outNum == POUT_LOCATION) {
            return -inputs[genModelPmechInLocation] * voltage / 0.85;
        }
        if (outNum == QOUT_LOCATION) {
            return -exciterField * Xd * voltage / 0.85;
        }
    }
    return kNullVal;
}

double GenModel::getOutput(index_t /*numOut*/) const
{
    return kNullVal;
}

void GenModel::ioPartialDerivatives(const IOdata& inputs,
                                    const StateData& /*stateDataValue*/,
                                    MatrixData<double>& matrixDataValue,
                                    const IOlocs& inputLocs,
                                    const SolverMode& /*sMode*/)
{
    const double voltage = inputs[VOLTAGE_IN_LOCATION];

    if (voltage > 0.85) {
        matrixDataValue.assignCheckCol(QOUT_LOCATION, inputLocs[genModelEftInLocation], -Xd);

        if (inputLocs[VOLTAGE_IN_LOCATION] != kNullLocation) {
            matrixDataValue.assign(POUT_LOCATION, inputLocs[VOLTAGE_IN_LOCATION], 0);
            matrixDataValue.assign(QOUT_LOCATION, inputLocs[VOLTAGE_IN_LOCATION], 0);
        }
        matrixDataValue.assignCheckCol(POUT_LOCATION, inputLocs[genModelPmechInLocation], -1.0);
    } else {
        const double factor = voltage / 0.85;
        matrixDataValue.assignCheckCol(QOUT_LOCATION,
                                       inputLocs[genModelEftInLocation],
                                       -Xd * factor);

        if (inputLocs[VOLTAGE_IN_LOCATION] != kNullLocation) {
            const double exciterField = inputs[genModelEftInLocation];
            matrixDataValue.assign(POUT_LOCATION,
                                   inputLocs[VOLTAGE_IN_LOCATION],
                                   -inputs[genModelPmechInLocation] / 0.85);
            matrixDataValue.assign(QOUT_LOCATION,
                                   inputLocs[VOLTAGE_IN_LOCATION],
                                   -exciterField * Xd / 0.85);
        }
        matrixDataValue.assignCheckCol(POUT_LOCATION, inputLocs[genModelPmechInLocation], -factor);
    }
}

// set parameters
void GenModel::set(std::string_view param, std::string_view val)
{
    GridSubModel::set(param, val);
}
void GenModel::set(std::string_view param, double val, units::unit unitType)
{
    if (param.length() == 1) {
        switch (param[0]) {
            case 'x':
                Xd = val;
                break;
            case 'r':
                Rs = val;
                break;
            default:
                throw(UnrecognizedParameter(param));
        }
        return;
    }

    if ((param == "xd") || (param == "xs")) {
        Xd = val;
    } else if (param == "rs") {
        Rs = val;
    } else if ((param == "base") || (param == "mbase")) {
        machineBasePower = val;
    } else {
        GridSubModel::set(param, val, unitType);
    }
}

const std::vector<stringVec>& GenModel::inputNames() const
{
    return genModelInputNames();
}

const std::vector<stringVec>& GenModel::outputNames() const
{
    return genModelOutputNames();
}

}  // namespace griddyn
