/*
 * Copyright (c) 2014-2026, Lawrence Livermore National Security
 * See the top-level NOTICE for additional details. All rights reserved.
 * SPDX-License-Identifier: BSD-3-Clause
 */

#include "ExciterESST3A.h"

#include "StaticExciterRectifier.h"
#include "core/CoreExceptions.h"
#include "core/CoreObjectTemplates.hpp"
#include "utilities/MatrixData.hpp"
#include <algorithm>
#include <array>
#include <cmath>
#include <string>
#include <utility>
#include <vector>

namespace griddyn::exciters {
// The implementation follows the published block equations, whose conventional
// grouping is clearer than precedence-only parentheses around every product.
// NOLINTBEGIN(readability-math-missing-parentheses)
namespace {
    constexpr index_t voltageMeasurementState = 0;
    constexpr index_t leadLagState = 1;
    constexpr index_t dynamicVoltageRegulatorState = 2;
    constexpr index_t dynamicFieldRegulatorState = 3;
    constexpr index_t staticFieldRegulatorState = 2;
    constexpr double limitTolerance = 1e-7;
}  // namespace

ExciterESST3A::ExciterESST3A(const std::string& objName): Exciter(objName)
{
    m_inputSize = exciterInputCount;
    Ka = 50.0;
    Ta = 0.1;
    Vrmax = 8.0;
    Vrmin = 0.0;
}

CoreObject* ExciterESST3A::clone(CoreObject* obj) const
{
    auto* exciterClone = cloneBase<ExciterESST3A, Exciter>(this, obj);
    if (exciterClone == nullptr) {
        return obj;
    }
    exciterClone->Tr = Tr;
    exciterClone->Vimax = Vimax;
    exciterClone->Vimin = Vimin;
    exciterClone->Km = Km;
    exciterClone->Tc = Tc;
    exciterClone->Tb = Tb;
    exciterClone->Kg = Kg;
    exciterClone->Kp = Kp;
    exciterClone->Ki = Ki;
    exciterClone->Vbmax = Vbmax;
    exciterClone->Kc = Kc;
    exciterClone->Xl = Xl;
    exciterClone->Vgmax = Vgmax;
    exciterClone->ThetaP = ThetaP;
    exciterClone->Tm = Tm;
    exciterClone->Vmmax = Vmmax;
    exciterClone->Vmmin = Vmmin;
    return exciterClone;
}

void ExciterESST3A::dynObjectInitializeA(CoreTime /*time0*/, std::uint32_t /*flags*/)
{
    if (!std::isfinite(Tr) || !std::isfinite(Vimax) || !std::isfinite(Vimin) ||
        !std::isfinite(Km) || !std::isfinite(Tc) || !std::isfinite(Tb) || !std::isfinite(Ka) ||
        !std::isfinite(Ta) || !std::isfinite(Vrmax) || !std::isfinite(Vrmin) ||
        !std::isfinite(Kg) || !std::isfinite(Kp) || !std::isfinite(Ki) || !std::isfinite(Vbmax) ||
        !std::isfinite(Kc) || !std::isfinite(Xl) || !std::isfinite(Vgmax) ||
        !std::isfinite(ThetaP) || !std::isfinite(Tm) || !std::isfinite(Vmmax) ||
        !std::isfinite(Vmmin) || (Tr <= 0.0) || (Tb <= 0.0) || (Ta < 0.0) || (Tm <= 0.0) ||
        (Ka <= 0.0) || (Km <= 0.0) || (Kp <= 0.0) || (Vimax < Vimin) || (Vrmax < Vrmin) ||
        (Vmmax < Vmmin) || (Vbmax <= 0.0)) {
        throw InvalidParameterValue("ESST3A gains, time constants, or limits");
    }
    offsets.local().local.algSize = hasDynamicVoltageRegulator() ? 1 : 2;
    offsets.local().local.diffSize = hasDynamicVoltageRegulator() ? 4 : 3;
    offsets.local().local.algRoots = hasDynamicVoltageRegulator() ? 2 : 1;
    offsets.local().local.jacSize = 30;
}

void ExciterESST3A::dynObjectInitializeB(const IOdata& inputs,
                                         const IOdata& desiredOutput,
                                         IOdata& fieldSet)
{
    for (const index_t inputIndex : {exciterIdInLocation,
                                     exciterIqInLocation,
                                     exciterVdInLocation,
                                     exciterVqInLocation,
                                     exciterXadIfdInLocation}) {
        if (!std::isfinite(inputs[inputIndex]) || (std::abs(inputs[inputIndex]) > 1e20)) {
            throw InvalidParameterValue("ESST3A requires compatible synchronous-machine signals");
        }
    }

    const double fieldVoltage = desiredOutput.empty() ? 0.0 : desiredOutput[0];
    const double rectifierOutput = rectifierVoltage(inputs);
    if (rectifierOutput <= 1e-12) {
        throw InvalidParameterValue("ESST3A initial rectifier voltage");
    }
    const double fieldRegulator = fieldVoltage / rectifierOutput;
    if ((fieldRegulator < Vmmin - limitTolerance) || (fieldRegulator > Vmmax + limitTolerance)) {
        throw InvalidParameterValue("ESST3A initial VM outside limits");
    }

    const double feedbackVoltage = std::min(Kg * fieldVoltage, Vgmax);
    const double vrs = fieldRegulator / Km;
    const double regulatorVoltage = vrs + feedbackVoltage;
    if ((regulatorVoltage < Vrmin - limitTolerance) ||
        (regulatorVoltage > Vrmax + limitTolerance)) {
        throw InvalidParameterValue("ESST3A initial VR outside limits");
    }

    const bool dynamicVoltageRegulator = hasDynamicVoltageRegulator();
    m_state[0] = fieldVoltage;
    if (!dynamicVoltageRegulator) {
        m_state[1] =
            std::clamp(regulatorVoltage, static_cast<double>(Vrmin), static_cast<double>(Vrmax));
    }
    double* state = m_state.data() + (dynamicVoltageRegulator ? 1 : 2);
    state[voltageMeasurementState] = inputs[exciterVoltageInLocation];
    if (dynamicVoltageRegulator) {
        state[dynamicVoltageRegulatorState] =
            std::clamp(regulatorVoltage, static_cast<double>(Vrmin), static_cast<double>(Vrmax));
    }
    const index_t fieldRegulatorState =
        dynamicVoltageRegulator ? dynamicFieldRegulatorState : staticFieldRegulatorState;
    state[fieldRegulatorState] =
        std::clamp(fieldRegulator, static_cast<double>(Vmmin), static_cast<double>(Vmmax));
    const double initialRegulatorVoltage =
        dynamicVoltageRegulator ? state[dynamicVoltageRegulatorState] : m_state[1];
    const double inputVoltage = initialRegulatorVoltage / Ka;
    state[leadLagState] = inputVoltage;
    vBias = state[voltageMeasurementState] + inputVoltage - Vref;
    fieldSet[exciterVsetInLocation] = Vref;
    std::fill(m_dstate_dt.begin(), m_dstate_dt.end(), 0.0);
}

double ExciterESST3A::rectifierVoltage(const IOdata& inputs) const
{
    return detail::computeRectifierData(inputs, Kp, Ki, Kc, Xl, ThetaP, Vbmax).voltage;
}

double ExciterESST3A::leadLagOutput(double voltageMeasurement, double leadLag) const
{
    const double inputVoltage = std::clamp(Vref + vBias - voltageMeasurement,
                                           static_cast<double>(Vimin),
                                           static_cast<double>(Vimax));
    const double leadRatio = Tc / Tb;
    return leadLag + leadRatio * (inputVoltage - leadLag);
}

double
    ExciterESST3A::vrDrive(double voltageMeasurement, double leadLag, double regulatorVoltage) const
{
    return Ka * leadLagOutput(voltageMeasurement, leadLag) - regulatorVoltage;
}

double ExciterESST3A::vmDrive(double fieldVoltage,
                              double regulatorVoltage,
                              double fieldRegulator) const
{
    const double feedbackVoltage = std::min(Kg * fieldVoltage, Vgmax);
    return Km * (regulatorVoltage - feedbackVoltage) - fieldRegulator;
}

bool ExciterESST3A::hasDynamicVoltageRegulator() const
{
    return Ta > 0.0;
}

void ExciterESST3A::residual(const IOdata& inputs,
                             const StateData& stateData,
                             double resid[],
                             const SolverMode& sMode)
{
    const auto locations = offsets.getLocations(stateData, resid, sMode, this);
    const bool dynamicVoltageRegulator = hasDynamicVoltageRegulator();
    const index_t fieldRegulatorState =
        dynamicVoltageRegulator ? dynamicFieldRegulatorState : staticFieldRegulatorState;
    if (hasAlgebraic(sMode)) {
        locations.destLoc[0] =
            rectifierVoltage(inputs) * locations.diffStateLoc[fieldRegulatorState] -
            locations.algStateLoc[0];
        if (!dynamicVoltageRegulator) {
            const double regulatorTarget =
                std::clamp(Ka *
                               leadLagOutput(locations.diffStateLoc[voltageMeasurementState],
                                             locations.diffStateLoc[leadLagState]),
                           static_cast<double>(Vrmin),
                           static_cast<double>(Vrmax));
            locations.destLoc[1] = regulatorTarget - locations.algStateLoc[1];
        }
    }
    if (!hasDifferential(sMode)) {
        return;
    }
    derivative(inputs, stateData, resid, sMode);
    for (index_t index = 0; index < locations.diffSize; ++index) {
        locations.destDiffLoc[index] -= locations.dstateLoc[index];
    }
}

void ExciterESST3A::derivative(const IOdata& inputs,
                               const StateData& stateData,
                               double deriv[],
                               const SolverMode& sMode)
{
    if (!hasDifferential(sMode)) {
        return;
    }
    const auto locations = offsets.getLocations(stateData, deriv, sMode, this);
    const double* state = locations.diffStateLoc;
    double* stateDerivative = locations.destDiffLoc;
    const bool dynamicVoltageRegulator = hasDynamicVoltageRegulator();
    const index_t fieldRegulatorState =
        dynamicVoltageRegulator ? dynamicFieldRegulatorState : staticFieldRegulatorState;

    stateDerivative[voltageMeasurementState] =
        (inputs[exciterVoltageInLocation] - state[voltageMeasurementState]) / Tr;
    const double inputVoltage = std::clamp(Vref + vBias - state[voltageMeasurementState],
                                           static_cast<double>(Vimin),
                                           static_cast<double>(Vimax));
    stateDerivative[leadLagState] = (inputVoltage - state[leadLagState]) / Tb;
    const double regulatorVoltage =
        dynamicVoltageRegulator ? state[dynamicVoltageRegulatorState] : locations.algStateLoc[1];
    if (dynamicVoltageRegulator) {
        stateDerivative[dynamicVoltageRegulatorState] = opFlags[VR_LIMITED] ?
            0.0 :
            vrDrive(state[voltageMeasurementState], state[leadLagState], regulatorVoltage) / Ta;
    }
    stateDerivative[fieldRegulatorState] = opFlags[VM_LIMITED] ?
        0.0 :
        vmDrive(locations.algStateLoc[0], regulatorVoltage, state[fieldRegulatorState]) / Tm;
}

void ExciterESST3A::jacobianElements(const IOdata& inputs,
                                     const StateData& stateData,
                                     MatrixData<double>& matrixData,
                                     const IOlocs& inputLocs,
                                     const SolverMode& sMode)
{
    const auto locations = offsets.getLocations(stateData, sMode, this);
    const index_t refAlg = locations.algOffset;
    const index_t refDiff = locations.diffOffset;
    const double* state = locations.diffStateLoc;
    const bool dynamicVoltageRegulator = hasDynamicVoltageRegulator();
    const index_t fieldRegulatorState =
        dynamicVoltageRegulator ? dynamicFieldRegulatorState : staticFieldRegulatorState;
    const index_t regulatorLocation =
        dynamicVoltageRegulator ? refDiff + dynamicVoltageRegulatorState : refAlg + 1;

    if (hasAlgebraic(sMode)) {
        const auto rectifier = detail::computeRectifierData(inputs, Kp, Ki, Kc, Xl, ThetaP, Vbmax);
        matrixData.assign(refAlg, refAlg, -1.0);
        matrixData.assign(refAlg, refDiff + fieldRegulatorState, rectifier.voltage);
        const std::array<index_t, 5> rectifierInputLocations{exciterIdInLocation,
                                                             exciterIqInLocation,
                                                             exciterVdInLocation,
                                                             exciterVqInLocation,
                                                             exciterXadIfdInLocation};
        for (index_t index = 0; std::cmp_less(index, rectifierInputLocations.size()); ++index) {
            matrixData.assignCheckCol(refAlg,
                                      inputLocs[rectifierInputLocations[index]],
                                      state[fieldRegulatorState] * rectifier.derivatives[index]);
        }

        if (!dynamicVoltageRegulator) {
            matrixData.assign(refAlg + 1, refAlg + 1, -1.0);
            const double unlimitedInput = Vref + vBias - state[voltageMeasurementState];
            const bool inputLimitActive = (unlimitedInput <= Vimin) || (unlimitedInput >= Vimax);
            const double unlimitedRegulator =
                Ka * leadLagOutput(state[voltageMeasurementState], state[leadLagState]);
            const bool regulatorLimitActive =
                (unlimitedRegulator <= Vrmin) || (unlimitedRegulator >= Vrmax);
            if (!regulatorLimitActive) {
                const double leadRatio = Tc / Tb;
                matrixData.assign(refAlg + 1, refDiff + leadLagState, Ka * (1.0 - leadRatio));
                if (!inputLimitActive) {
                    matrixData.assign(refAlg + 1,
                                      refDiff + voltageMeasurementState,
                                      -Ka * leadRatio);
                }
            }
        }
    }
    if (!hasDifferential(sMode)) {
        return;
    }

    matrixData.assign(refDiff + voltageMeasurementState,
                      refDiff + voltageMeasurementState,
                      -1.0 / Tr - stateData.cj);
    matrixData.assignCheckCol(refDiff + voltageMeasurementState,
                              inputLocs[exciterVoltageInLocation],
                              1.0 / Tr);

    const double unlimitedInput = Vref + vBias - state[voltageMeasurementState];
    const bool inputLimitActive = (unlimitedInput <= Vimin) || (unlimitedInput >= Vimax);
    matrixData.assign(refDiff + leadLagState, refDiff + leadLagState, -1.0 / Tb - stateData.cj);
    if (!inputLimitActive) {
        matrixData.assign(refDiff + leadLagState, refDiff + voltageMeasurementState, -1.0 / Tb);
    }

    if (dynamicVoltageRegulator) {
        if (opFlags[VR_LIMITED]) {
            matrixData.assign(refDiff + dynamicVoltageRegulatorState,
                              refDiff + dynamicVoltageRegulatorState,
                              -stateData.cj);
        } else {
            const double leadRatio = Tc / Tb;
            matrixData.assign(refDiff + dynamicVoltageRegulatorState,
                              refDiff + dynamicVoltageRegulatorState,
                              -1.0 / Ta - stateData.cj);
            matrixData.assign(refDiff + dynamicVoltageRegulatorState,
                              refDiff + leadLagState,
                              Ka * (1.0 - leadRatio) / Ta);
            if (!inputLimitActive) {
                matrixData.assign(refDiff + dynamicVoltageRegulatorState,
                                  refDiff + voltageMeasurementState,
                                  -Ka * leadRatio / Ta);
            }
        }
    }

    if (opFlags[VM_LIMITED]) {
        matrixData.assign(refDiff + fieldRegulatorState,
                          refDiff + fieldRegulatorState,
                          -stateData.cj);
    } else {
        const bool feedbackLimitActive = Kg * locations.algStateLoc[0] >= Vgmax;
        matrixData.assign(refDiff + fieldRegulatorState,
                          refDiff + fieldRegulatorState,
                          -1.0 / Tm - stateData.cj);
        matrixData.assign(refDiff + fieldRegulatorState, regulatorLocation, Km / Tm);
        if (!feedbackLimitActive) {
            matrixData.assign(refDiff + fieldRegulatorState, refAlg, -Km * Kg / Tm);
        }
    }
}

void ExciterESST3A::timestep(CoreTime time, const IOdata& inputs, const SolverMode& /*sMode*/)
{
    derivative(inputs, emptyStateData, m_dstate_dt.data(), cLocalSolverMode);
    const double timeStep = time - prevTime;
    const bool dynamicVoltageRegulator = hasDynamicVoltageRegulator();
    const index_t algebraicStateCount = dynamicVoltageRegulator ? 1 : 2;
    const index_t differentialStateCount = dynamicVoltageRegulator ? 4 : 3;
    const index_t fieldRegulatorState =
        dynamicVoltageRegulator ? dynamicFieldRegulatorState : staticFieldRegulatorState;
    double* state = m_state.data() + algebraicStateCount;
    const double* stateDerivative = m_dstate_dt.data() + algebraicStateCount;
    for (index_t index = 0; index < differentialStateCount; ++index) {
        state[index] += timeStep * stateDerivative[index];
    }
    if (dynamicVoltageRegulator) {
        state[dynamicVoltageRegulatorState] = std::clamp(state[dynamicVoltageRegulatorState],
                                                         static_cast<double>(Vrmin),
                                                         static_cast<double>(Vrmax));
    } else {
        m_state[1] =
            std::clamp(Ka * leadLagOutput(state[voltageMeasurementState], state[leadLagState]),
                       static_cast<double>(Vrmin),
                       static_cast<double>(Vrmax));
    }
    state[fieldRegulatorState] = std::clamp(state[fieldRegulatorState],
                                            static_cast<double>(Vmmin),
                                            static_cast<double>(Vmmax));
    m_state[0] = rectifierVoltage(inputs) * state[fieldRegulatorState];
    prevTime = time;
}

void ExciterESST3A::rootTest(const IOdata& /*inputs*/,
                             const StateData& stateData,
                             double roots[],
                             const SolverMode& sMode)
{
    const auto locations = offsets.getLocations(stateData, sMode, this);
    const double* state = locations.diffStateLoc;
    const index_t rootOffset = offsets.getRootOffset(sMode);
    const bool dynamicVoltageRegulator = hasDynamicVoltageRegulator();
    index_t fieldRootOffset = rootOffset;
    double regulatorVoltage =
        dynamicVoltageRegulator ? state[dynamicVoltageRegulatorState] : locations.algStateLoc[1];
    index_t fieldRegulatorState = staticFieldRegulatorState;
    if (dynamicVoltageRegulator) {
        regulatorVoltage = state[dynamicVoltageRegulatorState];
        fieldRegulatorState = dynamicFieldRegulatorState;
        if (opFlags[VR_LIMITED]) {
            const double drive =
                vrDrive(state[voltageMeasurementState], state[leadLagState], regulatorVoltage);
            roots[rootOffset] = opFlags[VR_LIMIT_HIGH] ? -drive : drive;
        } else {
            roots[rootOffset] = std::min(Vrmax - regulatorVoltage, regulatorVoltage - Vrmin);
            opFlags.set(VR_LIMIT_HIGH, regulatorVoltage >= Vrmax);
        }
        fieldRootOffset = rootOffset + 1;
    }

    if (opFlags[VM_LIMITED]) {
        const double drive =
            vmDrive(locations.algStateLoc[0], regulatorVoltage, state[fieldRegulatorState]);
        roots[fieldRootOffset] = opFlags[VM_LIMIT_HIGH] ? -drive : drive;
    } else {
        roots[fieldRootOffset] =
            std::min(Vmmax - state[fieldRegulatorState], state[fieldRegulatorState] - Vmmin);
        opFlags.set(VM_LIMIT_HIGH, state[fieldRegulatorState] >= Vmmax);
    }
}

void ExciterESST3A::rootTrigger(CoreTime /*time*/,
                                const IOdata& /*inputs*/,
                                const std::vector<int>& rootMask,
                                const SolverMode& sMode)
{
    const index_t rootOffset = offsets.getRootOffset(sMode);
    const auto toggleLimit = [this](bool voltageRegulator) {
        const int limitedFlag = voltageRegulator ? VR_LIMITED : VM_LIMITED;
        if (opFlags[limitedFlag]) {
            opFlags.reset(limitedFlag);
            alert(this, JAC_COUNT_INCREASE);
        } else {
            opFlags.set(limitedFlag);
            alert(this, JAC_COUNT_DECREASE);
        }
    };
    const bool dynamicVoltageRegulator = hasDynamicVoltageRegulator();
    if (dynamicVoltageRegulator && (rootMask[rootOffset] != 0)) {
        toggleLimit(true);
    }
    const index_t fieldRootOffset = rootOffset + (dynamicVoltageRegulator ? 1 : 0);
    if (rootMask[fieldRootOffset] != 0) {
        toggleLimit(false);
    }
}

ChangeCode ExciterESST3A::rootCheck(const IOdata& /*inputs*/,
                                    const StateData& /*stateData*/,
                                    const SolverMode& /*sMode*/,
                                    CheckLevel /*level*/)
{
    const bool dynamicVoltageRegulator = hasDynamicVoltageRegulator();
    const double* state = m_state.data() + (dynamicVoltageRegulator ? 1 : 2);
    ChangeCode result = ChangeCode::NO_CHANGE;
    double regulatorVoltage = m_state[1];
    index_t fieldRegulatorState = staticFieldRegulatorState;
    if (dynamicVoltageRegulator) {
        regulatorVoltage = state[dynamicVoltageRegulatorState];
        fieldRegulatorState = dynamicFieldRegulatorState;
        const double voltageDrive =
            vrDrive(state[voltageMeasurementState], state[leadLagState], regulatorVoltage);
        if (opFlags[VR_LIMITED]) {
            const bool release =
                opFlags[VR_LIMIT_HIGH] ? (voltageDrive < 0.0) : (voltageDrive > 0.0);
            if (release) {
                opFlags.reset(VR_LIMITED);
                alert(this, JAC_COUNT_INCREASE);
                result = ChangeCode::JACOBIAN_CHANGE;
            }
        } else if ((regulatorVoltage > Vrmax + limitTolerance) ||
                   (regulatorVoltage < Vrmin - limitTolerance)) {
            opFlags.set(VR_LIMIT_HIGH, regulatorVoltage > Vrmax);
            opFlags.set(VR_LIMITED);
            alert(this, JAC_COUNT_DECREASE);
            result = ChangeCode::JACOBIAN_CHANGE;
        }
    }

    const double fieldDrive = vmDrive(m_state[0], regulatorVoltage, state[fieldRegulatorState]);
    if (opFlags[VM_LIMITED]) {
        const bool release = opFlags[VM_LIMIT_HIGH] ? (fieldDrive < 0.0) : (fieldDrive > 0.0);
        if (release) {
            opFlags.reset(VM_LIMITED);
            alert(this, JAC_COUNT_INCREASE);
            result = ChangeCode::JACOBIAN_CHANGE;
        }
    } else if ((state[fieldRegulatorState] > Vmmax + limitTolerance) ||
               (state[fieldRegulatorState] < Vmmin - limitTolerance)) {
        opFlags.set(VM_LIMIT_HIGH, state[fieldRegulatorState] > Vmmax);
        opFlags.set(VM_LIMITED);
        alert(this, JAC_COUNT_DECREASE);
        result = ChangeCode::JACOBIAN_CHANGE;
    }
    return result;
}

void ExciterESST3A::set(std::string_view param, std::string_view val)
{
    Exciter::set(param, val);
}

void ExciterESST3A::set(std::string_view param, double val, units::unit unitType)
{
    if (param == "tr") {
        if (!std::isfinite(val) || (val <= 0.0)) {
            throw InvalidParameterValue("ESST3A TR must be positive and finite");
        }
        Tr = val;
    } else if (param == "vimax") {
        if (!std::isfinite(val)) {
            throw InvalidParameterValue("ESST3A VIMAX must be finite");
        }
        Vimax = val;
    } else if (param == "vimin") {
        if (!std::isfinite(val)) {
            throw InvalidParameterValue("ESST3A VIMIN must be finite");
        }
        Vimin = val;
    } else if (param == "km") {
        if (!std::isfinite(val) || (val <= 0.0)) {
            throw InvalidParameterValue("ESST3A KM must be positive and finite");
        }
        Km = val;
    } else if (param == "tc") {
        if (!std::isfinite(val)) {
            throw InvalidParameterValue("ESST3A TC must be finite");
        }
        Tc = val;
    } else if (param == "tb") {
        if (!std::isfinite(val) || (val <= 0.0)) {
            throw InvalidParameterValue("ESST3A TB must be positive and finite");
        }
        Tb = val;
    } else if (param == "ka") {
        if (!std::isfinite(val) || (val <= 0.0)) {
            throw InvalidParameterValue("ESST3A KA must be positive and finite");
        }
        Ka = val;
    } else if (param == "ta") {
        if (!std::isfinite(val) || (val < 0.0)) {
            throw InvalidParameterValue("ESST3A TA must be nonnegative and finite");
        }
        Ta = val;
    } else if ((param == "vrmax") || (param == "urmax")) {
        if (!std::isfinite(val)) {
            throw InvalidParameterValue("ESST3A VRMAX must be finite");
        }
        Vrmax = val;
    } else if ((param == "vrmin") || (param == "urmin")) {
        if (!std::isfinite(val)) {
            throw InvalidParameterValue("ESST3A VRMIN must be finite");
        }
        Vrmin = val;
    } else if (param == "kg") {
        if (!std::isfinite(val)) {
            throw InvalidParameterValue("ESST3A KG must be finite");
        }
        Kg = val;
    } else if (param == "kp") {
        if (!std::isfinite(val) || (val <= 0.0)) {
            throw InvalidParameterValue("ESST3A KP must be positive and finite");
        }
        Kp = val;
    } else if (param == "ki") {
        if (!std::isfinite(val)) {
            throw InvalidParameterValue("ESST3A KI must be finite");
        }
        Ki = val;
    } else if (param == "vbmax") {
        if (!std::isfinite(val) || (val <= 0.0)) {
            throw InvalidParameterValue("ESST3A VBMAX must be positive and finite");
        }
        Vbmax = val;
    } else if (param == "kc") {
        if (!std::isfinite(val)) {
            throw InvalidParameterValue("ESST3A KC must be finite");
        }
        Kc = val;
    } else if (param == "xl") {
        if (!std::isfinite(val)) {
            throw InvalidParameterValue("ESST3A XL must be finite");
        }
        Xl = val;
    } else if (param == "vgmax") {
        if (!std::isfinite(val)) {
            throw InvalidParameterValue("ESST3A VGMAX must be finite");
        }
        Vgmax = val;
    } else if ((param == "thetap") || (param == "theta_p")) {
        if (!std::isfinite(val)) {
            throw InvalidParameterValue("ESST3A THETAP must be finite");
        }
        ThetaP = val;
    } else if (param == "tm") {
        if (!std::isfinite(val) || (val <= 0.0)) {
            throw InvalidParameterValue("ESST3A TM must be positive and finite");
        }
        Tm = val;
    } else if (param == "vmmax") {
        if (!std::isfinite(val)) {
            throw InvalidParameterValue("ESST3A VMMAX must be finite");
        }
        Vmmax = val;
    } else if (param == "vmmin") {
        if (!std::isfinite(val)) {
            throw InvalidParameterValue("ESST3A VMMIN must be finite");
        }
        Vmmin = val;
    } else {
        Exciter::set(param, val, unitType);
    }
}

double ExciterESST3A::get(std::string_view param, units::unit unitType) const
{
    if (param == "ka") {
        return Ka;
    }
    if (param == "ta") {
        return Ta;
    }
    if (param == "vrmax") {
        return Vrmax;
    }
    if (param == "vrmin") {
        return Vrmin;
    }
    if (param == "tr") {
        return Tr;
    }
    if (param == "vimax") {
        return Vimax;
    }
    if (param == "vimin") {
        return Vimin;
    }
    if (param == "km") {
        return Km;
    }
    if (param == "tc") {
        return Tc;
    }
    if (param == "tb") {
        return Tb;
    }
    if (param == "kg") {
        return Kg;
    }
    if (param == "kp") {
        return Kp;
    }
    if (param == "ki") {
        return Ki;
    }
    if (param == "vbmax") {
        return Vbmax;
    }
    if (param == "kc") {
        return Kc;
    }
    if (param == "xl") {
        return Xl;
    }
    if (param == "vgmax") {
        return Vgmax;
    }
    if ((param == "thetap") || (param == "theta_p")) {
        return ThetaP;
    }
    if (param == "tm") {
        return Tm;
    }
    if (param == "vmmax") {
        return Vmmax;
    }
    if (param == "vmmin") {
        return Vmmin;
    }
    return Exciter::get(param, unitType);
}

stringVec ExciterESST3A::localStateNames() const
{
    return hasDynamicVoltageRegulator() ? stringVec{"efd", "vmeas", "ll", "vr", "vm"} :
                                          stringVec{"efd", "vr", "vmeas", "ll", "vm"};
}

index_t ExciterESST3A::findIndex(std::string_view field, const SolverMode& sMode) const
{
    if ((field == "efd") || (field == "field")) {
        return offsets.getAlgOffset(sMode);
    }
    const index_t diffOffset = offsets.getDiffOffset(sMode);
    if (field == "vmeas") {
        return diffOffset + voltageMeasurementState;
    }
    if (field == "ll") {
        return diffOffset + leadLagState;
    }
    if (field == "vr") {
        return hasDynamicVoltageRegulator() ? diffOffset + dynamicVoltageRegulatorState :
                                              offsets.getAlgOffset(sMode) + 1;
    }
    if (field == "vm") {
        return diffOffset +
            (hasDynamicVoltageRegulator() ? dynamicFieldRegulatorState : staticFieldRegulatorState);
    }
    return kInvalidLocation;
}
// NOLINTEND(readability-math-missing-parentheses)
}  // namespace griddyn::exciters
