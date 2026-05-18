/*
 * Copyright (c) 2014-2026, Lawrence Livermore National Security
 * See the top-level NOTICE for additional details. All rights reserved.
 * SPDX-License-Identifier: BSD-3-Clause
 */

#include "../Generator.h"
#include "../gridBus.h"
#include "core/coreExceptions.h"
#include "core/coreObjectTemplates.hpp"
#include "core/objectFactoryTemplates.hpp"
#include "gmlc/utilities/vectorOps.hpp"
#include "otherGenModels.h"
#include "utilities/matrixData.hpp"
#include <string>
#include <vector>

namespace griddyn {
namespace {
typeFactory<GenModel>& genModelFactory()
{
    static typeFactory<GenModel> genModelFactoryInstance(
        "genmodel", std::to_array<std::string_view>({"trivial"}));
    return genModelFactoryInstance;
}

void registerGenModelTypes()
{
    static childTypeFactory<griddyn::genmodels::GenModelInverter, GenModel> inverterGenModelFactory(
        "genmodel", std::to_array<std::string_view>({"inverter"}));

    static childTypeFactory<griddyn::genmodels::GenModelClassical, GenModel>
        classicalGenModelFactory(
        "genmodel",
        std::to_array<std::string_view>(
            {"basic", "2", "second", "secondorder", "classic", "classical", "II"}));

    static childTypeFactory<griddyn::genmodels::GenModel3, GenModel> thirdOrderGenModelFactory(
        "genmodel", std::to_array<std::string_view>({"3", "third", "thirdorder", "III"}));
    static childTypeFactory<griddyn::genmodels::GenModel4, GenModel> fourthOrderGenModelFactory(
        "genmodel",
        std::to_array<std::string_view>({"4", "fourth", "fourthorder", "IV", "grdc"}),
        "4");

    static childTypeFactory<griddyn::genmodels::GenModel5, GenModel> fifthOrderGenModelFactory(
        "genmodel",
        std::to_array<std::string_view>({"5", "fifth", "fifthorder", "5.1", "Vtype1", "V"}));

    static childTypeFactory<griddyn::genmodels::GenModel5type2, GenModel>
        fifthOrderGenModelType2Factory(
        "genmodel",
        std::to_array<std::string_view>({"5.2", "fifthtype2", "fifthordertype2", "Vtype2"}));

    static childTypeFactory<griddyn::genmodels::GenModel6, GenModel> sixthOrderGenModelFactory(
        "genmodel", std::to_array<std::string_view>({"6", "six", "sixthorder", "VI"}));

    static childTypeFactory<griddyn::genmodels::GenModel6type2, GenModel>
        sixthOrderGenModelType2Factory(
        "genmodel",
        std::to_array<std::string_view>({"6.2", "sixtype2", "sixthordertype2", "VItype2", "VI.2"}));

    static childTypeFactory<griddyn::genmodels::GenModel8, GenModel> eighthOrderGenModelFactory(
        "genmodel", std::to_array<std::string_view>({"8", "eight", "eighthorder", "VIII"}));

    static const bool registered = []() {
        static_cast<void>(genModelFactory());
        static_cast<void>(&inverterGenModelFactory);
        static_cast<void>(&classicalGenModelFactory);
        static_cast<void>(&thirdOrderGenModelFactory);
        static_cast<void>(&fourthOrderGenModelFactory);
        static_cast<void>(&fifthOrderGenModelFactory);
        static_cast<void>(&fifthOrderGenModelType2Factory);
        static_cast<void>(&sixthOrderGenModelFactory);
        static_cast<void>(&sixthOrderGenModelType2Factory);
        static_cast<void>(&eighthOrderGenModelFactory);
        return true;
    }();
    static_cast<void>(registered);
}

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

GenModel::GenModel(const std::string& objName): GridSubModel(objName)
{
    registerGenModelTypes();
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
    if (inputs[voltageInLocation] > 0.85) {
        fieldSet[genModelPmechInLocation] = desiredOutput[PoutLocation];  // Pmt
        fieldSet[genModelEftInLocation] = desiredOutput[QoutLocation] / Xd;
    } else {
        fieldSet[genModelPmechInLocation] =
            desiredOutput[PoutLocation] / inputs[voltageInLocation] * 0.85;  // Pmt
        fieldSet[genModelEftInLocation] =
            desiredOutput[QoutLocation] / Xd / inputs[voltageInLocation] * 0.85;
    }

    bus = static_cast<GridBus*>(find("bus"));
}

// residual

double GenModel::getFreq(const stateData& stateDataValue,
                         const solverMode& sMode,
                         index_t* freqOffset) const
{
    // there is no inertia in this gen model so it can't compute a frequency and
    // must use the bus frequency
    if (freqOffset != nullptr) {
        *freqOffset = bus->getOutputLoc(sMode, frequencyInLocation);
    }
    return bus->getFreq(stateDataValue, sMode);
}

double GenModel::getAngle(const stateData& /*stateDataValue*/,
                          const solverMode& /*sMode*/,
                          index_t* angleOffset) const
{
    // there is no inertia in this gen model so it can't compute a frequency and
    // must use the bus frequency
    if (angleOffset != nullptr) {
        *angleOffset = kNullLocation;
    }
    return kNullVal;
}

count_t GenModel::outputDependencyCount(index_t /*num*/, const solverMode& /*sMode*/) const
{
    return 0;
}
IOdata GenModel::getOutputs(const IOdata& inputs,
                            const stateData& /*stateDataValue*/,
                            const solverMode& /*sMode*/) const
{
    IOdata out(2);
    const double voltage = inputs[voltageInLocation];
    const double exciterField = inputs[genModelEftInLocation];
    if (voltage > 0.85) {
        out[PoutLocation] = -inputs[genModelPmechInLocation];
        out[QoutLocation] = -exciterField * Xd;
    } else {
        out[PoutLocation] = -inputs[genModelPmechInLocation] * voltage / 0.85;
        out[QoutLocation] = -exciterField * Xd * voltage / 0.85;
    }

    return out;
}

double GenModel::getOutput(const IOdata& inputs,
                           const stateData& /*stateDataValue*/,
                           const solverMode& /*sMode*/,
                           index_t outNum) const
{
    const double voltage = inputs[voltageInLocation];
    const double exciterField = inputs[genModelEftInLocation];
    if (voltage > 0.85) {
        if (outNum == PoutLocation) {
            return -inputs[genModelPmechInLocation];
        }
        if (outNum == QoutLocation) {
            return -exciterField * Xd;
        }
    } else {
        if (outNum == PoutLocation) {
            return -inputs[genModelPmechInLocation] * voltage / 0.85;
        }
        if (outNum == QoutLocation) {
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
                                    const stateData& /*stateDataValue*/,
                                    matrixData<double>& matrixDataValue,
                                    const IOlocs& inputLocs,
                                    const solverMode& /*sMode*/)
{
    const double voltage = inputs[voltageInLocation];

    if (voltage > 0.85) {
        matrixDataValue.assignCheckCol(QoutLocation, inputLocs[genModelEftInLocation], -Xd);

        if (inputLocs[voltageInLocation] != kNullLocation) {
            matrixDataValue.assign(PoutLocation, inputLocs[voltageInLocation], 0);
            matrixDataValue.assign(QoutLocation, inputLocs[voltageInLocation], 0);
        }
        matrixDataValue.assignCheckCol(PoutLocation, inputLocs[genModelPmechInLocation], -1.0);
    } else {
        const double factor = voltage / 0.85;
        matrixDataValue.assignCheckCol(QoutLocation, inputLocs[genModelEftInLocation], -Xd * factor);

        if (inputLocs[voltageInLocation] != kNullLocation) {
            const double exciterField = inputs[genModelEftInLocation];
            matrixDataValue.assign(PoutLocation,
                      inputLocs[voltageInLocation],
                      -inputs[genModelPmechInLocation] / 0.85);
            matrixDataValue.assign(
                QoutLocation, inputLocs[voltageInLocation], -exciterField * Xd / 0.85);
        }
        matrixDataValue.assignCheckCol(PoutLocation, inputLocs[genModelPmechInLocation], -factor);
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
                throw(unrecognizedParameter(param));
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
