/*
 * Copyright (c) 2014-2026, Lawrence Livermore National Security
 * See the top-level NOTICE for additional details. All rights reserved.
 * SPDX-License-Identifier: BSD-3-Clause
 */

#include "FDepLoad.h"

#include "../GridBus.h"
#include "core/CoreExceptions.h"
#include "core/CoreObjectTemplates.hpp"
#include "gmlc/utilities/stringOps.h"
#include "utilities/MatrixData.hpp"
#include <cmath>
#include <string>
namespace griddyn::loads {
FDepLoad::FDepLoad(const std::string& objName): ExponentialLoad(objName) {}
FDepLoad::FDepLoad(double realPower, double reactivePower, const std::string& objName):
    ExponentialLoad(realPower, reactivePower, objName)
{
}

void FDepLoad::add(CoreObject* obj)
{
    auto* filter = dynamic_cast<GridBlock*>(obj);
    if (filter == nullptr) {
        throw UnrecognizedObjectException(this);
    }
    setFrequencyFilter(filter);
}

void FDepLoad::setFrequencyFilter(GridBlock* filter)
{
    if (filter == frequencyFilter.get()) {
        return;
    }
    if (opFlags[DYN_INITIALIZED]) {
        throw InvalidParameterValue(
            "frequency filter cannot be changed after dynamic initialization");
    }

    if (frequencyFilter) {
        removeSubObject(frequencyFilter.get());
        frequencyFilter = nullptr;
    }

    if (filter == nullptr) {
        if ((betaP == 0.0) && (betaQ == 0.0)) {
            opFlags.reset(USES_BUS_FREQUENCY);
        }
        return;
    }

    frequencyFilter = CoreOwningPtr<GridBlock>(filter);
    addSubObject(frequencyFilter.get());
    frequencyFilter->parentSetFlag(SEPARATE_PROCESSING, true, this);
    opFlags.set(USES_BUS_FREQUENCY);
}

void FDepLoad::dynObjectInitializeA(CoreTime time0, std::uint32_t flags)
{
    if ((betaP != 0.0) || (betaQ != 0.0) || frequencyFilter) {
        opFlags.set(USES_BUS_FREQUENCY);
    }
    ExponentialLoad::dynObjectInitializeA(time0, flags);
}

void FDepLoad::dynObjectInitializeB(const IOdata& inputs,
                                    const IOdata& desiredOutput,
                                    IOdata& fieldSet)
{
    GridComponent::dynObjectInitializeB(inputs, desiredOutput, fieldSet);
    if (frequencyFilter) {
        const double frequency = (inputs.size() > FREQUENCY_IN_LOCATION) ?
            inputs[FREQUENCY_IN_LOCATION] :
            bus->getFreq(emptyStateData, cLocalSolverMode);
        IOdata filterFieldSet;
        frequencyFilter->dynInitializeB({frequency}, {}, filterFieldSet);
    }
}

CoreObject* FDepLoad::clone(CoreObject* obj) const
{
    auto* clonedLoad = cloneBase<FDepLoad, ExponentialLoad>(this, obj);
    if (clonedLoad == nullptr) {
        return obj;
    }

    clonedLoad->betaP = betaP;
    clonedLoad->betaQ = betaQ;
    clonedLoad->powerScaleP = powerScaleP;
    clonedLoad->powerScaleQ = powerScaleQ;
    clonedLoad->voltageReference = voltageReference;
    clonedLoad->frequencyBus = frequencyBus;
    return clonedLoad;
}

void FDepLoad::updateObjectLinkages(CoreObject* newRoot)
{
    GridSecondary::updateObjectLinkages(newRoot);
    // The reader only records local BusFreq links. After cloning or moving a
    // tree, the local source is therefore the newly resolved owning bus.
    if (frequencyBus != nullptr) {
        frequencyBus = bus;
    }
}

double FDepLoad::getBusFrequency(const IOdata& inputs,
                                 const StateData& stateDataValue,
                                 const SolverMode& sMode) const
{
    const auto* sourceBus = (frequencyBus != nullptr) ? frequencyBus : bus;
    return (inputs.size() > FREQUENCY_IN_LOCATION) ? inputs[FREQUENCY_IN_LOCATION] :
                                                     sourceBus->getFreq(stateDataValue, sMode);
}

double FDepLoad::getFrequency(const IOdata& inputs,
                              const StateData& stateDataValue,
                              const SolverMode& sMode) const
{
    const double busFrequency = getBusFrequency(inputs, stateDataValue, sMode);

    if (!frequencyFilter || !frequencyFilter->checkFlag(DYN_INITIALIZED) || !isDynamic(sMode)) {
        return busFrequency;
    }
    if (stateDataValue.empty()) {
        return frequencyFilter->getBlockOutput();
    }
    return frequencyFilter->getBlockOutput(stateDataValue, sMode);
}

double FDepLoad::getLocalFrequency() const
{
    if (frequencyFilter && frequencyFilter->checkFlag(DYN_INITIALIZED)) {
        return frequencyFilter->getBlockOutput();
    }
    const auto* sourceBus = (frequencyBus != nullptr) ? frequencyBus : bus;
    return sourceBus->getFreq();
}

// set properties
void FDepLoad::set(std::string_view param, std::string_view val)
{
    if (param == "loadtype") {
        auto vtype = gmlc::utilities::convertToLowerCase(val);
        if (vtype == "fluorescent") {
            alphaP = 1.2;
            alphaQ = 3.0;
            betaP = -0.1;
            betaQ = 2.8;
        } else if (vtype == "incandescent") {
            alphaP = 1.6;
            alphaQ = 0.0;
            betaP = 0.0;
            betaQ = 0.0;
        } else if (vtype == "heater") {
            alphaP = 2.0;
            alphaQ = 0.0;
            betaP = 0.0;
            betaQ = 0.0;
        } else if (vtype == "motor-full") {
            alphaP = 0.1;
            alphaQ = 0.6;
            betaP = 2.8;
            betaQ = 1.8;
        } else if (vtype == "motor-half") {
            alphaP = 0.2;
            alphaQ = 1.6;
            betaP = 1.5;
            betaQ = -0.3;
        } else if (vtype == "Reduction_furnace") {
            alphaP = 1.9;
            alphaQ = 2.1;
            betaP = -0.5;
            betaQ = 0.0;
        } else if (vtype == "aluminum_plant") {
            alphaP = 1.8;
            alphaQ = 2.2;
            betaP = -0.3;
            betaQ = 0.6;
        }
    } else {
        ExponentialLoad::set(param, val);
    }
}

void FDepLoad::set(std::string_view param, double val, units::unit unitType)
{
    if (param == "betap") {
        betaP = val;
    } else if (param == "betaq") {
        betaQ = val;
    } else if (param == "beta") {
        betaP = betaQ = val;
    } else if ((param == "kp") || (param == "p_scale")) {
        powerScaleP = (param == "kp") ? val / 100.0 : val;
    } else if ((param == "kq") || (param == "q_scale")) {
        powerScaleQ = (param == "kq") ? val / 100.0 : val;
    } else if ((param == "vref") || (param == "v0")) {
        if (val > 0.0) {
            voltageReference = val;
        }
    } else {
        ExponentialLoad::set(param, val, unitType);
    }
    if ((betaP != 0.0) || (betaQ != 0.0)) {
        opFlags.set(USES_BUS_FREQUENCY);
    }
}

double FDepLoad::get(std::string_view param, units::unit unitType) const
{
    if (param == "kp") {
        return powerScaleP * 100.0;
    }
    if (param == "kq") {
        return powerScaleQ * 100.0;
    }
    if ((param == "p_scale") || (param == "q_scale")) {
        return (param == "p_scale") ? powerScaleP : powerScaleQ;
    }
    if ((param == "vref") || (param == "v0")) {
        return voltageReference;
    }
    return ExponentialLoad::get(param, unitType);
}

void FDepLoad::ioPartialDerivatives(const IOdata& inputs,
                                    const StateData& stateData,
                                    MatrixData<double>& matrixData,
                                    const IOlocs& inputLocs,
                                    const SolverMode& sMode)
{
    const double voltage = inputs[VOLTAGE_IN_LOCATION];
    const double frequency = getFrequency(inputs, stateData, sMode);
    const bool useFilter =
        frequencyFilter && frequencyFilter->checkFlag(DYN_INITIALIZED) && isDynamic(sMode);
    // power vs voltage
    if (inputLocs[VOLTAGE_IN_LOCATION] != kNullLocation) {
        matrixData.assign(POUT_LOCATION,
                          inputLocs[VOLTAGE_IN_LOCATION],
                          getP() * powerScaleP * alphaP *
                              pow(voltage / voltageReference, alphaP - 1.0) /
                              voltageReference * pow(frequency, betaP));

        // reactive power vs voltage
        matrixData.assign(QOUT_LOCATION,
                          inputLocs[VOLTAGE_IN_LOCATION],
                          getQ() * powerScaleQ * alphaQ *
                              pow(voltage / voltageReference, alphaQ - 1.0) /
                              voltageReference * pow(frequency, betaQ));
    }
    // When a dynamic filter is present, the load's direct frequency input is no longer the
    // filtered signal. The filter-state dependency is added by outputPartialDerivatives().
    if (!useFilter && (inputLocs[FREQUENCY_IN_LOCATION] != kNullLocation)) {
        matrixData.assign(POUT_LOCATION,
                          inputLocs[FREQUENCY_IN_LOCATION],
                          getP() * powerScaleP * pow(voltage / voltageReference, alphaP) * betaP *
                              pow(frequency, betaP - 1.0));
        matrixData.assign(QOUT_LOCATION,
                          inputLocs[FREQUENCY_IN_LOCATION],
                          getQ() * powerScaleQ * pow(voltage / voltageReference, alphaQ) * betaQ *
                              pow(frequency, betaQ - 1.0));
    }
}

void FDepLoad::outputPartialDerivatives(const IOdata& inputs,
                                        const StateData& stateDataValue,
                                        MatrixData<double>& matrixDataValue,
                                        const SolverMode& sMode)
{
    if (!frequencyFilter || !frequencyFilter->checkFlag(DYN_INITIALIZED) || !isDynamic(sMode) ||
        (frequencyFilter->getOutputLoc(sMode) == kNullLocation)) {
        return;
    }

    const IOdata busInputs =
        inputs.empty() ? bus->getOutputs(noInputs, stateDataValue, sMode) : inputs;
    const double voltage = busInputs[VOLTAGE_IN_LOCATION];
    const double frequency = getFrequency(busInputs, stateDataValue, sMode);
    const index_t filterOutputLocation = frequencyFilter->getOutputLoc(sMode);

    matrixDataValue.assign(POUT_LOCATION,
                           filterOutputLocation,
                           getP() * powerScaleP * pow(voltage / voltageReference, alphaP) * betaP *
                               pow(frequency, betaP - 1.0));
    matrixDataValue.assign(QOUT_LOCATION,
                           filterOutputLocation,
                           getQ() * powerScaleQ * pow(voltage / voltageReference, alphaQ) * betaQ *
                               pow(frequency, betaQ - 1.0));
}

count_t FDepLoad::outputDependencyCount(index_t /*outputNum*/, const SolverMode& sMode) const
{
    return (frequencyFilter && isDynamic(sMode) && (frequencyFilter->stateSize(sMode) > 0)) ? 1 : 0;
}

void FDepLoad::timestep(CoreTime time, const IOdata& inputs, const SolverMode& sMode)
{
    if (frequencyFilter && frequencyFilter->checkFlag(DYN_INITIALIZED) && isDynamic(sMode) &&
        frequencyFilter->currentTime() < time) {
        const double frequency = (inputs.size() > FREQUENCY_IN_LOCATION) ?
            inputs[FREQUENCY_IN_LOCATION] :
            bus->getFreq();
        frequencyFilter->timestep(time, {frequency}, sMode);
    }
    GridComponent::timestep(time, inputs, sMode);
}

void FDepLoad::residual(const IOdata& inputs,
                        const StateData& stateDataValue,
                        double resid[],
                        const SolverMode& sMode)
{
    GridComponent::residual(inputs, stateDataValue, resid, sMode);
    if (frequencyFilter && isDynamic(sMode) && frequencyFilter->stateSize(sMode) > 0) {
        frequencyFilter->blockResidual(
            getBusFrequency(inputs, stateDataValue, sMode), 0.0, stateDataValue, resid, sMode);
    }
}

void FDepLoad::derivative(const IOdata& inputs,
                          const StateData& stateDataValue,
                          double deriv[],
                          const SolverMode& sMode)
{
    GridComponent::derivative(inputs, stateDataValue, deriv, sMode);
    if (frequencyFilter && isDynamic(sMode) && frequencyFilter->diffSize(sMode) > 0) {
        frequencyFilter->blockDerivative(
            getBusFrequency(inputs, stateDataValue, sMode), 0.0, stateDataValue, deriv, sMode);
    }
}

void FDepLoad::algebraicUpdate(const IOdata& inputs,
                               const StateData& stateDataValue,
                               double update[],
                               const SolverMode& sMode,
                               double alpha)
{
    GridComponent::algebraicUpdate(inputs, stateDataValue, update, sMode, alpha);
    if (frequencyFilter && isDynamic(sMode) && frequencyFilter->algSize(sMode) > 0) {
        frequencyFilter->algebraicUpdate(
            {getBusFrequency(inputs, stateDataValue, sMode)}, stateDataValue, update, sMode, alpha);
    }
}

void FDepLoad::jacobianElements(const IOdata& inputs,
                                const StateData& stateDataValue,
                                MatrixData<double>& matrixDataValue,
                                const IOlocs& inputLocs,
                                const SolverMode& sMode)
{
    GridComponent::jacobianElements(inputs, stateDataValue, matrixDataValue, inputLocs, sMode);
    if (frequencyFilter && isDynamic(sMode) && frequencyFilter->stateSize(sMode) > 0) {
        frequencyFilter->blockJacobianElements(getBusFrequency(inputs, stateDataValue, sMode),
                                               0.0,
                                               stateDataValue,
                                               matrixDataValue,
                                               inputLocs[FREQUENCY_IN_LOCATION],
                                               sMode);
    }
}

double FDepLoad::getRealPower() const
{
    return getRealPower(bus->getVoltage(), getLocalFrequency());
}
double FDepLoad::getReactivePower() const
{
    return getReactivePower(bus->getVoltage(), getLocalFrequency());
}
double FDepLoad::getRealPower(const IOdata& inputs,
                              const StateData& stateData,
                              const SolverMode& sMode) const
{
    return getRealPower(inputs[VOLTAGE_IN_LOCATION], getFrequency(inputs, stateData, sMode));
}

double FDepLoad::getReactivePower(const IOdata& inputs,
                                  const StateData& stateData,
                                  const SolverMode& sMode) const
{
    return getReactivePower(inputs[VOLTAGE_IN_LOCATION], getFrequency(inputs, stateData, sMode));
}

double FDepLoad::getRealPower(const double voltage) const
{
    return getRealPower(voltage, getLocalFrequency());
}
double FDepLoad::getReactivePower(double voltage) const
{
    return getReactivePower(voltage, getLocalFrequency());
}
double FDepLoad::getRealPower(double voltage, double frequency) const
{
    if (isConnected()) {
        double val = getP();
        val *= powerScaleP * pow(voltage / voltageReference, alphaP) * pow(frequency, betaP);
        return val;
    }
    return 0.0;
}

double FDepLoad::getReactivePower(double voltage, double frequency) const
{
    if (isConnected()) {
        double val = getQ();
        val *= powerScaleQ * pow(voltage / voltageReference, alphaQ) * pow(frequency, betaQ);
        return val;
    }
    return 0.0;
}
}  // namespace griddyn::loads
