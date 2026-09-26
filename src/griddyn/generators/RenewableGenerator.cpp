/*
 * Copyright (c) 2014-2026, Lawrence Livermore National Security
 * See the top-level NOTICE for additional details. All rights reserved.
 * SPDX-License-Identifier: BSD-3-Clause
 */

#include "RenewableGenerator.h"

#include "../GridBus.h"
#include "core/CoreExceptions.h"
#include "core/CoreObjectTemplates.hpp"
#include "utilities/MatrixData.hpp"
#include "utilities/MatrixDataScale.hpp"
#include <algorithm>
#include <array>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

namespace griddyn {
namespace {
    bool isTerminalSignal(RenewableSignal signal)
    {
        return signal == RenewableSignal::terminalVoltage ||
            signal == RenewableSignal::terminalAngle;
    }
}  // namespace

RenewableGenerator::RenewableGenerator(const std::string& name): Generator(name) {}

CoreObject* RenewableGenerator::clone(CoreObject* obj) const
{
    return cloneBase<RenewableGenerator, Generator>(this, obj);
}

std::size_t RenewableGenerator::roleIndex(RenewableRole role)
{
    const auto index = static_cast<std::size_t>(role);
    if (index >= roleCount) {
        throw InvalidParameterValue("renewable component role");
    }
    return index;
}

void RenewableGenerator::add(GridSubModel* obj)
{
    if (obj == nullptr ||
        (obj->getParent() != nullptr &&
         !isSameObject(static_cast<id_type_t>(0), obj->getParent()) && obj->getParent() != this)) {
        throw UnrecognizedObjectException(this);
    }
    auto* renewable = dynamic_cast<RenewableComponent*>(obj);
    if (renewable == nullptr) {
        Generator::add(obj);
        return;
    }
    const auto index = roleIndex(renewable->role());
    if (renewable->role() == RenewableRole::electrical &&
        dynamic_cast<TerminalElectricalModel*>(renewable) == nullptr) {
        throw UnrecognizedObjectException(this);
    }
    if (components[index] == renewable) {
        return;
    }
    replaceSubObject(renewable, components[index]);
    components[index] = renewable;
    if (index == roleIndex(RenewableRole::electrical)) {
        electricalModel = static_cast<TerminalElectricalModel*>(renewable);
    }
}

void RenewableGenerator::remove(CoreObject* obj)
{
    for (auto& component : components) {
        if (component == obj) {
            component = nullptr;
        }
    }
    if (electricalModel == obj) {
        electricalModel = nullptr;
    }
    GridComponent::remove(obj);
}

CoreObject* RenewableGenerator::find(std::string_view object) const
{
    if (object == "electrical") {
        return electricalModel;
    }
    if (object == "electrical_control") {
        return components[roleIndex(RenewableRole::electricalControl)];
    }
    if (object == "plant_control") {
        return components[roleIndex(RenewableRole::plantControl)];
    }
    return Generator::find(object);
}

CoreObject* RenewableGenerator::getSubObject(std::string_view typeName, index_t num) const
{
    if (typeName == "renewable_component" && num >= 0 && std::cmp_less(num, roleCount)) {
        return components[static_cast<std::size_t>(num)];
    }
    return Generator::getSubObject(typeName, num);
}

void RenewableGenerator::validateAssembly() const
{
    if (electricalModel == nullptr || !electricalModel->isEnabled()) {
        throw InvalidParameterValue("renewable generator requires a terminal electrical model");
    }
    for (const auto* component : components) {
        if (component == nullptr || !component->isEnabled()) {
            continue;
        }
        for (const auto& input : component->inputPorts()) {
            if (isTerminalSignal(input.signal)) {
                continue;
            }
            count_t providers = 0;
            for (const auto* candidate : components) {
                if (candidate == nullptr || candidate == component || !candidate->isEnabled()) {
                    continue;
                }
                for (const auto& output : candidate->outputPorts()) {
                    if (output.signal == input.signal && output.base == input.base) {
                        ++providers;
                    }
                }
            }
            if ((input.required && providers != 1) || providers > 1) {
                throw InvalidParameterValue("renewable input has no unique compatible provider");
            }
        }
        if (component != electricalModel) {
            bool consumed = false;
            for (const auto& output : component->outputPorts()) {
                for (const auto* consumer : components) {
                    if (consumer == nullptr || consumer == component || !consumer->isEnabled()) {
                        continue;
                    }
                    for (const auto& input : consumer->inputPorts()) {
                        consumed |= input.signal == output.signal && input.base == output.base;
                    }
                }
            }
            if (!consumed) {
                throw InvalidParameterValue("renewable component has no connected output");
            }
        }
    }
}

IOdata RenewableGenerator::modelInputs(const RenewableComponent* model,
                                       const IOdata& inputs,
                                       const StateData& stateDataValue,
                                       const SolverMode& sMode) const
{
    IOdata result;
    for (const auto& port : model->inputPorts()) {
        const auto portIndex = static_cast<std::size_t>(port.ioIndex);
        if (result.size() <= portIndex) {
            result.resize(portIndex + 1, kNullVal);
        }
        switch (port.signal) {
            case RenewableSignal::terminalVoltage:
                if (inputs.size() > VOLTAGE_IN_LOCATION) {
                    result[portIndex] = inputs[VOLTAGE_IN_LOCATION];
                }
                break;
            case RenewableSignal::terminalAngle:
                if (inputs.size() > ANGLE_IN_LOCATION) {
                    result[portIndex] = inputs[ANGLE_IN_LOCATION];
                }
                break;
            default:
                for (const auto* candidate : components) {
                    if (candidate == nullptr || candidate == model || !candidate->isEnabled() ||
                        !candidate->checkFlag(DYN_INITIALIZED)) {
                        continue;
                    }
                    for (const auto& output : candidate->outputPorts()) {
                        if (output.signal == port.signal && output.base == port.base) {
                            const SolverMode* outputMode = &sMode;
                            StateData outputState = stateDataValue;
                            if (isDifferentialOnly(sMode) &&
                                sMode.pairedOffsetIndex != kNullLocation &&
                                stateDataValue.algState != nullptr) {
                                const auto& paired = offsets.getSolverMode(sMode.pairedOffsetIndex);
                                if (paired.algebraic &&
                                    candidate->algSize(paired) > output.ioIndex) {
                                    outputMode = &paired;
                                    outputState.state = stateDataValue.algState;
                                }
                            }
                            result[portIndex] =
                                candidate->getOutput({}, outputState, *outputMode, output.ioIndex);
                        }
                    }
                }
        }
    }
    return result;
}

IOlocs RenewableGenerator::modelInputLocs(const RenewableComponent* model,
                                          const IOlocs& inputLocs,
                                          const SolverMode& sMode) const
{
    IOlocs result;
    for (const auto& port : model->inputPorts()) {
        const auto portIndex = static_cast<std::size_t>(port.ioIndex);
        if (result.size() <= portIndex) {
            result.resize(portIndex + 1, kNullLocation);
        }
        if (port.signal == RenewableSignal::terminalVoltage &&
            inputLocs.size() > VOLTAGE_IN_LOCATION) {
            result[portIndex] = inputLocs[VOLTAGE_IN_LOCATION];
        } else if (port.signal == RenewableSignal::terminalAngle &&
                   inputLocs.size() > ANGLE_IN_LOCATION) {
            result[portIndex] = inputLocs[ANGLE_IN_LOCATION];
        } else {
            for (const auto* candidate : components) {
                if (candidate == nullptr || candidate == model || !candidate->isEnabled()) {
                    continue;
                }
                for (const auto& output : candidate->outputPorts()) {
                    if (output.signal == port.signal && output.base == port.base) {
                        result[portIndex] = candidate->getOutputLoc(sMode, output.ioIndex);
                    }
                }
            }
        }
    }
    return result;
}

void RenewableGenerator::dynObjectInitializeA(CoreTime time0, std::uint32_t flags)
{
    validateAssembly();
    Generator::dynObjectInitializeA(time0, flags);
}

void RenewableGenerator::dynObjectInitializeB(const IOdata& inputs,
                                              const IOdata& desiredOutput,
                                              IOdata& fieldSet)
{
    Generator::dynObjectInitializeB(inputs, desiredOutput, fieldSet);
    const double scale = systemBasePower / machineBasePower;
    IOdata const target{P * scale, Q * scale};
    IOdata modelFieldSet;
    electricalModel->dynInitializeB(
        modelInputs(electricalModel, inputs, emptyStateData, cLocalSolverMode),
        target,
        modelFieldSet);
    for (auto* component : components) {
        if (component != nullptr && component != electricalModel && component->isEnabled()) {
            IOdata ignored;
            component->dynInitializeB(
                modelInputs(component, inputs, emptyStateData, cLocalSolverMode), target, ignored);
        }
    }
    // A torque model chooses the wind operating speed from its power-speed curve.
    // Reconcile the shaft, torque integral, and pitch after every provider exists.
    if (components[roleIndex(RenewableRole::driveTrain)] != nullptr &&
        components[roleIndex(RenewableRole::torqueControl)] != nullptr) {
        for (auto role : {RenewableRole::driveTrain,
                          RenewableRole::torqueControl,
                          RenewableRole::aerodynamics,
                          RenewableRole::pitchControl}) {
            auto* component = components[roleIndex(role)];
            if (component != nullptr && component->isEnabled()) {
                IOdata ignored;
                component->dynInitializeB(
                    modelInputs(component, inputs, emptyStateData, cLocalSolverMode),
                    target,
                    ignored);
            }
        }
    }
}

void RenewableGenerator::setState(CoreTime time,
                                  const double state[],
                                  const double dstateDt[],
                                  const SolverMode& sMode)
{
    Generator::setState(time, state, dstateDt, sMode);
    for (auto* component : components) {
        if (component != nullptr && component->isEnabled()) {
            component->setState(time, state, dstateDt, sMode);
        }
    }
}

void RenewableGenerator::guessState(CoreTime time,
                                    double state[],
                                    double dstateDt[],
                                    const SolverMode& sMode)
{
    Generator::guessState(time, state, dstateDt, sMode);
    for (auto* component : components) {
        if (component != nullptr && component->isEnabled()) {
            component->guessState(time, state, dstateDt, sMode);
        }
    }
}

void RenewableGenerator::residual(const IOdata& inputs,
                                  const StateData& stateDataValue,
                                  double resid[],
                                  const SolverMode& sMode)
{
    if (!isDynamic(sMode)) {
        Generator::residual(inputs, stateDataValue, resid, sMode);
        return;
    }
    for (auto* component : components) {
        if (component != nullptr && component->isEnabled()) {
            component->residual(modelInputs(component, inputs, stateDataValue, sMode),
                                stateDataValue,
                                resid,
                                sMode);
        }
    }
}

void RenewableGenerator::derivative(const IOdata& inputs,
                                    const StateData& stateDataValue,
                                    double deriv[],
                                    const SolverMode& sMode)
{
    for (auto* component : components) {
        if (component != nullptr && component->isEnabled()) {
            component->derivative(modelInputs(component, inputs, stateDataValue, sMode),
                                  stateDataValue,
                                  deriv,
                                  sMode);
        }
    }
}

void RenewableGenerator::algebraicUpdate(const IOdata& inputs,
                                         const StateData& stateDataValue,
                                         double update[],
                                         const SolverMode& sMode,
                                         double alpha)
{
    if (!isDynamic(sMode)) {
        Generator::algebraicUpdate(inputs, stateDataValue, update, sMode, alpha);
        return;
    }
    for (auto* component : components) {
        if (component != nullptr && component->isEnabled()) {
            component->algebraicUpdate(modelInputs(component, inputs, stateDataValue, sMode),
                                       stateDataValue,
                                       update,
                                       sMode,
                                       alpha);
        }
    }
}

void RenewableGenerator::timestep(CoreTime time, const IOdata& inputs, const SolverMode& sMode)
{
    const auto stepRole = [&](RenewableRole role) {
        auto* component = components[roleIndex(role)];
        if (component != nullptr && component->isEnabled()) {
            component->timestep(time,
                                modelInputs(component, inputs, emptyStateData, cLocalSolverMode),
                                sMode);
        }
    };
    stepRole(RenewableRole::plantControl);
    stepRole(RenewableRole::driveTrain);
    stepRole(RenewableRole::torqueControl);
    stepRole(RenewableRole::electricalControl);
    stepRole(RenewableRole::pitchControl);
    stepRole(RenewableRole::aerodynamics);
    for (std::size_t index = 0; index < roleCount; ++index) {
        if (index != roleIndex(RenewableRole::plantControl) &&
            index != roleIndex(RenewableRole::driveTrain) &&
            index != roleIndex(RenewableRole::torqueControl) &&
            index != roleIndex(RenewableRole::electricalControl) &&
            index != roleIndex(RenewableRole::pitchControl) &&
            index != roleIndex(RenewableRole::aerodynamics) &&
            index != roleIndex(RenewableRole::electrical)) {
            stepRole(static_cast<RenewableRole>(index));
        }
    }
    stepRole(RenewableRole::electrical);
    const auto output = getOutputs(inputs, emptyStateData, cLocalSolverMode);
    P = -output[POUT_LOCATION];
    Q = -output[QOUT_LOCATION];
    prevTime = time;
}

void RenewableGenerator::jacobianElements(const IOdata& inputs,
                                          const StateData& stateDataValue,
                                          MatrixData<double>& matrixDataValue,
                                          const IOlocs& inputLocs,
                                          const SolverMode& sMode)
{
    if (!isDynamic(sMode)) {
        Generator::jacobianElements(inputs, stateDataValue, matrixDataValue, inputLocs, sMode);
        return;
    }
    for (auto* component : components) {
        if (component != nullptr && component->isEnabled()) {
            component->jacobianElements(modelInputs(component, inputs, stateDataValue, sMode),
                                        stateDataValue,
                                        matrixDataValue,
                                        modelInputLocs(component, inputLocs, sMode),
                                        sMode);
        }
    }
}

IOdata RenewableGenerator::getOutputs(const IOdata& inputs,
                                      const StateData& stateDataValue,
                                      const SolverMode& sMode) const
{
    if (!isDynamic(sMode) || electricalModel == nullptr) {
        return Generator::getOutputs(inputs, stateDataValue, sMode);
    }
    const SolverMode* outputMode = &sMode;
    StateData outputState = stateDataValue;
    if (isDifferentialOnly(sMode) && sMode.pairedOffsetIndex != kNullLocation &&
        stateDataValue.algState != nullptr) {
        const auto& paired = offsets.getSolverMode(sMode.pairedOffsetIndex);
        if (paired.algebraic && electricalModel->algSize(paired) >= 2) {
            outputMode = &paired;
            outputState.state = stateDataValue.algState;
        }
    }
    auto output =
        electricalModel->getOutputs(modelInputs(electricalModel, inputs, stateDataValue, sMode),
                                    outputState,
                                    *outputMode);
    const double scale = -machineBasePower / systemBasePower;
    output[POUT_LOCATION] *= scale;
    output[QOUT_LOCATION] *= scale;
    return output;
}

double RenewableGenerator::getRealPower(const IOdata& inputs,
                                        const StateData& stateDataValue,
                                        const SolverMode& sMode) const
{
    return getOutputs(inputs, stateDataValue, sMode)[POUT_LOCATION];
}

double RenewableGenerator::getReactivePower(const IOdata& inputs,
                                            const StateData& stateDataValue,
                                            const SolverMode& sMode) const
{
    return getOutputs(inputs, stateDataValue, sMode)[QOUT_LOCATION];
}

void RenewableGenerator::outputPartialDerivatives(const IOdata& inputs,
                                                  const StateData& stateDataValue,
                                                  MatrixData<double>& matrixDataValue,
                                                  const SolverMode& sMode)
{
    if (!isDynamic(sMode)) {
        Generator::outputPartialDerivatives(inputs, stateDataValue, matrixDataValue, sMode);
        return;
    }
    MatrixDataScale<double> scaled(matrixDataValue, -machineBasePower / systemBasePower);
    electricalModel->outputPartialDerivatives(
        modelInputs(electricalModel, inputs, stateDataValue, sMode), stateDataValue, scaled, sMode);
}

void RenewableGenerator::ioPartialDerivatives(const IOdata& inputs,
                                              const StateData& stateDataValue,
                                              MatrixData<double>& matrixDataValue,
                                              const IOlocs& inputLocs,
                                              const SolverMode& sMode)
{
    if (!isDynamic(sMode)) {
        Generator::ioPartialDerivatives(inputs, stateDataValue, matrixDataValue, inputLocs, sMode);
        return;
    }
    MatrixDataScale<double> scaled(matrixDataValue, -machineBasePower / systemBasePower);
    electricalModel->ioPartialDerivatives(
        modelInputs(electricalModel, inputs, stateDataValue, sMode),
        stateDataValue,
        scaled,
        modelInputLocs(electricalModel, inputLocs, sMode),
        sMode);
}

void RenewableGenerator::rootTest(const IOdata& inputs,
                                  const StateData& stateDataValue,
                                  double roots[],
                                  const SolverMode& sMode)
{
    for (auto* component : components) {
        if (component != nullptr && component->isEnabled() && component->rootSize(sMode) > 0) {
            component->rootTest(modelInputs(component, inputs, stateDataValue, sMode),
                                stateDataValue,
                                roots,
                                sMode);
        }
    }
}

void RenewableGenerator::rootTrigger(CoreTime time,
                                     const IOdata& inputs,
                                     const std::vector<int>& rootMask,
                                     const SolverMode& sMode)
{
    for (auto* component : components) {
        if (component != nullptr && component->isEnabled() && component->rootSize(sMode) > 0) {
            component->rootTrigger(time,
                                   modelInputs(component, inputs, emptyStateData, cLocalSolverMode),
                                   rootMask,
                                   sMode);
        }
    }
}

ChangeCode RenewableGenerator::rootCheck(const IOdata& inputs,
                                         const StateData& stateDataValue,
                                         const SolverMode& sMode,
                                         CheckLevel level)
{
    auto result = ChangeCode::NO_CHANGE;
    for (auto* component : components) {
        if (component != nullptr && component->isEnabled() && component->rootSize(sMode) > 0) {
            result =
                std::max(result,
                         component->rootCheck(modelInputs(component, inputs, stateDataValue, sMode),
                                              stateDataValue,
                                              sMode,
                                              level));
        }
    }
    return result;
}

count_t RenewableGenerator::outputDependencyCount(index_t num, const SolverMode& sMode) const
{
    if (!isDynamic(sMode)) {
        return Generator::outputDependencyCount(num, sMode);
    }
    return electricalModel == nullptr ? 0 : electricalModel->outputDependencyCount(num, sMode) + 1;
}

void RenewableGenerator::getStateName(stringVec& stNames,
                                      const SolverMode& sMode,
                                      const std::string& prefix) const
{
    if (!isDynamic(sMode) && stateSize(sMode) > 0) {
        Generator::getStateName(stNames, sMode, prefix);
    }
    GridComponent::getStateName(stNames, sMode, prefix);
}

}  // namespace griddyn
