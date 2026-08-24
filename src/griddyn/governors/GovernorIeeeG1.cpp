/*
 * Copyright (c) 2014-2026, Lawrence Livermore National Security
 * See the top-level NOTICE for additional details. All rights reserved.
 * SPDX-License-Identifier: BSD-3-Clause
 */

#include "GovernorIeeeG1.h"

#include "core/CoreExceptions.h"
#include "core/CoreObjectTemplates.hpp"
#include "utilities/MatrixData.hpp"
#include <algorithm>
#include <cmath>
#include <numeric>
#include <string>
#include <vector>

namespace griddyn::governors {
// The equations below mirror the published block diagram. Extra precedence
// parentheses obscure the signal flow without changing the calculation.
// NOLINTBEGIN(readability-math-missing-parentheses)
namespace {
    constexpr double initializationTolerance = 1e-7;
}

GovernorIeeeG1::GovernorIeeeG1(const std::string& objName): Governor(objName)
{
    K = 20.0;
    T1 = 1.0;
    T2 = 1.0;
    T3 = 0.1;
    Pmax = 5.0;
    Pmin = 0.0;
    m_outputSize = 2;
    opFlags.set(IGNORE_DEADBAND);
    opFlags.set(IGNORE_FILTER);
    opFlags.set(IGNORE_THROTTLE);
    opFlags.set(USES_POWER_LIMITS);
    opFlags.set(USES_RAMP_LIMITS);
}

CoreObject* GovernorIeeeG1::clone(CoreObject* obj) const
{
    auto* governorClone = cloneBase<GovernorIeeeG1, Governor>(this, obj);
    if (governorClone == nullptr) {
        return obj;
    }
    governorClone->Uo = Uo;
    governorClone->Uc = Uc;
    governorClone->turbineTime = turbineTime;
    governorClone->powerFraction = powerFraction;
    governorClone->initializationTarget = initializationTarget;
    governorClone->initializationTargetSet = initializationTargetSet;
    return governorClone;
}

void GovernorIeeeG1::dynObjectInitializeA(CoreTime time0, std::uint32_t /*flags*/)
{
    const double fractionSum = std::accumulate(powerFraction.begin(), powerFraction.end(), 0.0);
    if ((Pmax < Pmin) || !std::isfinite(fractionSum) || (fractionSum <= 0.0)) {
        throw InvalidParameterValue("IEEEG1 limits or power fractions");
    }

    index_t nextState = 0;
    leadLagState = (T1 > 0.0) ? nextState++ : kInvalidLocation;
    valveState = nextState++;
    for (index_t stage = 0; std::cmp_less(stage, turbineTime.size()); ++stage) {
        turbineState[stage] = (turbineTime[stage] > 0.0) ? nextState++ : kInvalidLocation;
    }

    auto& local = offsets.local().local;
    local.algSize = 2;
    local.diffSize = nextState;
    local.algRoots = 2;
    local.diffRoots = 0;
    local.jacSize = 48;
    prevTime = time0;
    initializationActive = false;
    opFlags.reset(RATE_LIMITED);
    opFlags.reset(RATE_LIMIT_HIGH);
    opFlags.reset(POWER_LIMITED);
    opFlags.reset(POWER_LIMIT_HIGH);
}

void GovernorIeeeG1::dynObjectInitializeB(const IOdata& /*inputs*/,
                                          const IOdata& desiredOutput,
                                          IOdata& fieldSet)
{
    if (desiredOutput.empty() || !std::isfinite(desiredOutput[hpOutput])) {
        throw InvalidParameterValue("IEEEG1 initial high-pressure output");
    }
    initializationTarget[hpOutput] = desiredOutput[hpOutput];
    initializationTargetSet[hpOutput] = true;
    initializationActive = true;
    initializeStatesFromTargets();

    fieldSet.resize(2);
    fieldSet[govpSetInLocation] = Pset;
}

bool GovernorIeeeG1::setOutputInitializationTarget(index_t outputIndex, double target)
{
    if ((outputIndex < 0) || (outputIndex >= 2) || !std::isfinite(target)) {
        throw InvalidParameterValue("IEEEG1 output initialization target");
    }
    initializationTarget[outputIndex] = target;
    initializationTargetSet[outputIndex] = true;
    if (initializationActive) {
        initializeStatesFromTargets();
    }
    return true;
}

std::array<double, 8> GovernorIeeeG1::normalizedFractions() const
{
    const double sum = std::accumulate(powerFraction.begin(), powerFraction.end(), 0.0);
    std::array<double, 8> normalized{};
    std::ranges::transform(powerFraction, normalized.begin(), [sum](double value) {
        return value / sum;
    });
    return normalized;
}

void GovernorIeeeG1::initializeStatesFromTargets()
{
    const auto fraction = normalizedFractions();
    const double hpFraction = fraction[0] + fraction[2] + fraction[4] + fraction[6];
    const double lpFraction = fraction[1] + fraction[3] + fraction[5] + fraction[7];

    bool totalSet = false;
    double totalPower = 0.0;
    const auto addTarget = [&](index_t outputIndex, double branchFraction) {
        if (!initializationTargetSet[outputIndex]) {
            return;
        }
        if (branchFraction <= initializationTolerance) {
            if (std::abs(initializationTarget[outputIndex]) > initializationTolerance) {
                throw InvalidParameterValue("IEEEG1 initial output has zero power fraction");
            }
            return;
        }
        const double candidate = initializationTarget[outputIndex] / branchFraction;
        if (totalSet &&
            (std::abs(candidate - totalPower) >
             initializationTolerance * (1.0 + std::abs(totalPower)))) {
            throw InvalidParameterValue("IEEEG1 initial HP/LP powers are not proportional");
        }
        totalPower = candidate;
        totalSet = true;
    };
    addTarget(hpOutput, hpFraction);
    addTarget(lpOutput, lpFraction);
    if (!totalSet || (totalPower < Pmin - initializationTolerance) ||
        (totalPower > Pmax + initializationTolerance)) {
        throw InvalidParameterValue("IEEEG1 initial valve position outside limits");
    }

    Pset = totalPower;
    double* diffState = m_state.data() + offsets.getDiffOffset(cLocalSolverMode);
    if (leadLagState != kInvalidLocation) {
        diffState[leadLagState] = 0.0;
    }
    diffState[valveState] = totalPower;
    for (const auto stageState : turbineState) {
        if (stageState != kInvalidLocation) {
            diffState[stageState] = totalPower;
        }
    }

    const auto output = powerOutputs(diffState);
    double* algState = m_state.data() + offsets.getAlgOffset(cLocalSolverMode);
    algState[hpOutput] = output[hpOutput];
    algState[lpOutput] = output[lpOutput];
}

double GovernorIeeeG1::leadLagOutput(const IOdata& inputs, const double diffState[]) const
{
    const double speedDeviation = 1.0 - inputs[govOmegaInLocation];
    if (leadLagState == kInvalidLocation) {
        return K * speedDeviation;
    }
    const double state = diffState[leadLagState];
    return K * (state + (T2 / T1) * (speedDeviation - state));
}

double GovernorIeeeG1::unlimitedValveRate(const IOdata& inputs, const double diffState[]) const
{
    return (leadLagOutput(inputs, diffState) + Pset - diffState[valveState]) / T3;
}

double GovernorIeeeG1::limitedValveRate(const IOdata& inputs, const double diffState[]) const
{
    return std::clamp(unlimitedValveRate(inputs, diffState),
                      static_cast<double>(Uc),
                      static_cast<double>(Uo));
}

std::array<double, 4> GovernorIeeeG1::turbineOutputs(const double diffState[]) const
{
    std::array<double, 4> output{};
    double input = diffState[valveState];
    for (index_t stage = 0; std::cmp_less(stage, output.size()); ++stage) {
        output[stage] =
            (turbineState[stage] != kInvalidLocation) ? diffState[turbineState[stage]] : input;
        input = output[stage];
    }
    return output;
}

std::array<double, 2> GovernorIeeeG1::powerOutputs(const double diffState[]) const
{
    const auto fraction = normalizedFractions();
    const auto stage = turbineOutputs(diffState);
    return {{fraction[0] * stage[0] + fraction[2] * stage[1] + fraction[4] * stage[2] +
                 fraction[6] * stage[3],
             fraction[1] * stage[0] + fraction[3] * stage[1] + fraction[5] * stage[2] +
                 fraction[7] * stage[3]}};
}

int GovernorIeeeG1::rateLimitStatus(const IOdata& inputs, const double diffState[]) const
{
    const double rate = unlimitedValveRate(inputs, diffState);
    if (rate >= Uo) {
        return 1;
    }
    return (rate <= Uc) ? -1 : 0;
}

int GovernorIeeeG1::valveLimitStatus(const IOdata& inputs, const double diffState[]) const
{
    const double rate = limitedValveRate(inputs, diffState);
    if ((diffState[valveState] >= Pmax) && (rate >= 0.0)) {
        return 1;
    }
    if ((diffState[valveState] <= Pmin) && (rate <= 0.0)) {
        return -1;
    }
    return 0;
}

void GovernorIeeeG1::residual(const IOdata& inputs,
                              const StateData& stateData,
                              double resid[],
                              const SolverMode& sMode)
{
    const auto locations = offsets.getLocations(stateData, resid, sMode, this);
    if (hasAlgebraic(sMode)) {
        const auto output = powerOutputs(locations.diffStateLoc);
        locations.destLoc[hpOutput] = output[hpOutput] - locations.algStateLoc[hpOutput];
        locations.destLoc[lpOutput] = output[lpOutput] - locations.algStateLoc[lpOutput];
    }
    if (!hasDifferential(sMode)) {
        return;
    }
    derivative(inputs, stateData, resid, sMode);
    for (index_t state = 0; state < locations.diffSize; ++state) {
        locations.destDiffLoc[state] -= locations.dstateLoc[state];
    }
}

void GovernorIeeeG1::derivative(const IOdata& inputs,
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

    if (leadLagState != kInvalidLocation) {
        stateDerivative[leadLagState] =
            (1.0 - inputs[govOmegaInLocation] - state[leadLagState]) / T1;
    }
    stateDerivative[valveState] =
        (valveLimitStatus(inputs, state) == 0) ? limitedValveRate(inputs, state) : 0.0;

    double input = state[valveState];
    for (index_t stage = 0; std::cmp_less(stage, turbineState.size()); ++stage) {
        if (turbineState[stage] != kInvalidLocation) {
            stateDerivative[turbineState[stage]] =
                (input - state[turbineState[stage]]) / turbineTime[stage];
            input = state[turbineState[stage]];
        }
    }
}

void GovernorIeeeG1::algebraicUpdate(const IOdata& /*inputs*/,
                                     const StateData& stateData,
                                     double update[],
                                     const SolverMode& sMode,
                                     double /*alpha*/)
{
    if (!hasAlgebraic(sMode)) {
        return;
    }
    const auto locations = offsets.getLocations(stateData, update, sMode, this);
    const auto output = powerOutputs(locations.diffStateLoc);
    locations.destLoc[hpOutput] = output[hpOutput];
    locations.destLoc[lpOutput] = output[lpOutput];
}

void GovernorIeeeG1::jacobianElements(const IOdata& inputs,
                                      const StateData& stateData,
                                      MatrixData<double>& matrixData,
                                      const IOlocs& inputLocs,
                                      const SolverMode& sMode)
{
    const auto locations = offsets.getLocations(stateData, sMode, this);
    const index_t algOffset = locations.algOffset;
    const index_t diffOffset = locations.diffOffset;
    const double* state = locations.diffStateLoc;

    std::array<index_t, 4> stageSource{};
    index_t source = valveState;
    for (index_t stage = 0; std::cmp_less(stage, stageSource.size()); ++stage) {
        if (turbineState[stage] != kInvalidLocation) {
            source = turbineState[stage];
        }
        stageSource[stage] = source;
    }

    if (hasAlgebraic(sMode)) {
        const auto fraction = normalizedFractions();
        matrixData.assign(algOffset + hpOutput, algOffset + hpOutput, -1.0);
        matrixData.assign(algOffset + lpOutput, algOffset + lpOutput, -1.0);
        for (std::size_t stage = 0; stage < stageSource.size(); ++stage) {
            matrixData.assign(algOffset + hpOutput,
                              diffOffset + stageSource[stage],
                              fraction[2 * stage]);
            matrixData.assign(algOffset + lpOutput,
                              diffOffset + stageSource[stage],
                              fraction[2 * stage + 1]);
        }
    }
    if (!hasDifferential(sMode)) {
        return;
    }

    if (leadLagState != kInvalidLocation) {
        matrixData.assign(diffOffset + leadLagState,
                          diffOffset + leadLagState,
                          -1.0 / T1 - stateData.cj);
        matrixData.assignCheckCol(diffOffset + leadLagState,
                                  inputLocs[govOmegaInLocation],
                                  -1.0 / T1);
    }

    const index_t valveRow = diffOffset + valveState;
    const bool valveLimited = valveLimitStatus(inputs, state) != 0;
    const bool rateLimited = rateLimitStatus(inputs, state) != 0;
    matrixData.assign(valveRow, valveRow, -stateData.cj);
    if (!valveLimited && !rateLimited) {
        matrixData.assign(valveRow, valveRow, -1.0 / T3);
        if (leadLagState == kInvalidLocation) {
            matrixData.assignCheckCol(valveRow,
                                      inputLocs[govOmegaInLocation],
                                      -static_cast<double>(K) / T3);
        } else {
            const double leadRatio = T2 / T1;
            matrixData.assign(valveRow, diffOffset + leadLagState, K * (1.0 - leadRatio) / T3);
            matrixData.assignCheckCol(valveRow, inputLocs[govOmegaInLocation], -K * leadRatio / T3);
        }
    }

    source = valveState;
    for (index_t stage = 0; stage < 4; ++stage) {
        if (turbineState[stage] == kInvalidLocation) {
            continue;
        }
        const index_t row = diffOffset + turbineState[stage];
        matrixData.assign(row, diffOffset + source, 1.0 / turbineTime[stage]);
        matrixData.assign(row, row, -1.0 / turbineTime[stage] - stateData.cj);
        source = turbineState[stage];
    }
}

void GovernorIeeeG1::timestep(CoreTime time, const IOdata& inputs, const SolverMode& /*sMode*/)
{
    derivative(inputs, emptyStateData, m_dstate_dt.data(), cLocalSolverMode);
    const double timeStep = time - prevTime;
    const index_t diffOffset = offsets.getDiffOffset(cLocalSolverMode);
    for (index_t state = 0; state < offsets.local().local.diffSize; ++state) {
        m_state[diffOffset + state] += timeStep * m_dstate_dt[diffOffset + state];
    }
    double* diffState = m_state.data() + diffOffset;
    diffState[valveState] =
        std::clamp(diffState[valveState], static_cast<double>(Pmin), static_cast<double>(Pmax));
    const auto output = powerOutputs(diffState);
    const index_t algOffset = offsets.getAlgOffset(cLocalSolverMode);
    m_state[algOffset + hpOutput] = output[hpOutput];
    m_state[algOffset + lpOutput] = output[lpOutput];
    updateLimitFlags(inputs, diffState);
    prevTime = time;
}

bool GovernorIeeeG1::updateLimitFlags(const IOdata& inputs, const double diffState[])
{
    const int rateStatus = rateLimitStatus(inputs, diffState);
    const int valveStatus = valveLimitStatus(inputs, diffState);
    const bool changed = (opFlags[RATE_LIMITED] != (rateStatus != 0)) ||
        (opFlags[RATE_LIMIT_HIGH] != (rateStatus > 0)) ||
        (opFlags[POWER_LIMITED] != (valveStatus != 0)) ||
        (opFlags[POWER_LIMIT_HIGH] != (valveStatus > 0));
    opFlags.set(RATE_LIMITED, rateStatus != 0);
    opFlags.set(RATE_LIMIT_HIGH, rateStatus > 0);
    opFlags.set(POWER_LIMITED, valveStatus != 0);
    opFlags.set(POWER_LIMIT_HIGH, valveStatus > 0);
    return changed;
}

void GovernorIeeeG1::rootTest(const IOdata& inputs,
                              const StateData& stateData,
                              double roots[],
                              const SolverMode& sMode)
{
    const auto locations = offsets.getLocations(stateData, sMode, this);
    const double* state = locations.diffStateLoc;
    const index_t rootOffset = offsets.getRootOffset(sMode);
    const double unlimitedRate = unlimitedValveRate(inputs, state);
    if (opFlags[RATE_LIMITED]) {
        roots[rootOffset] = opFlags[RATE_LIMIT_HIGH] ? Uo - unlimitedRate : unlimitedRate - Uc;
    } else {
        roots[rootOffset] = std::min(Uo - unlimitedRate, unlimitedRate - Uc);
    }

    const double limitedRate = limitedValveRate(inputs, state);
    if (opFlags[POWER_LIMITED]) {
        roots[rootOffset + 1] = opFlags[POWER_LIMIT_HIGH] ? limitedRate : -limitedRate;
    } else {
        roots[rootOffset + 1] = std::min(Pmax - state[valveState], state[valveState] - Pmin);
    }
}

void GovernorIeeeG1::rootTrigger(CoreTime /*time*/,
                                 const IOdata& inputs,
                                 const std::vector<int>& rootMask,
                                 const SolverMode& sMode)
{
    const index_t rootOffset = offsets.getRootOffset(sMode);
    if ((rootMask[rootOffset] == 0) && (rootMask[rootOffset + 1] == 0)) {
        return;
    }
    double* state = m_state.data() + offsets.getDiffOffset(cLocalSolverMode);
    if (state[valveState] >= Pmax) {
        state[valveState] = Pmax;
    } else if (state[valveState] <= Pmin) {
        state[valveState] = Pmin;
    }
    if (updateLimitFlags(inputs, state)) {
        alert(this, JAC_COUNT_CHANGE);
    }
}

ChangeCode GovernorIeeeG1::rootCheck(const IOdata& inputs,
                                     const StateData& /*stateData*/,
                                     const SolverMode& /*sMode*/,
                                     CheckLevel /*level*/)
{
    double* state = m_state.data() + offsets.getDiffOffset(cLocalSolverMode);
    if (state[valveState] > Pmax) {
        state[valveState] = Pmax;
    } else if (state[valveState] < Pmin) {
        state[valveState] = Pmin;
    }
    if (updateLimitFlags(inputs, state)) {
        alert(this, JAC_COUNT_CHANGE);
        return ChangeCode::JACOBIAN_CHANGE;
    }
    return ChangeCode::NO_CHANGE;
}

void GovernorIeeeG1::set(std::string_view param, std::string_view val)
{
    Governor::set(param, val);
}

void GovernorIeeeG1::set(std::string_view param, double val, units::unit unitType)
{
    if ((param == "k") || (param == "droop")) {
        if (!std::isfinite(val) || (val <= 0.0)) {
            throw InvalidParameterValue("IEEEG1 gain must be positive and finite");
        }
        Governor::set(param, val, unitType);
    } else if (param == "r") {
        if (!std::isfinite(val) || (val <= 0.0)) {
            throw InvalidParameterValue("IEEEG1 R must be positive and finite");
        }
        Governor::set(param, val, unitType);
    } else if ((param == "t1") || (param == "t2")) {
        if (!std::isfinite(val) || (val < 0.0)) {
            throw InvalidParameterValue("IEEEG1 T1/T2 must be nonnegative and finite");
        }
        Governor::set(param, val, unitType);
    } else if (param == "t3") {
        if (!std::isfinite(val) || (val <= 0.0)) {
            throw InvalidParameterValue("IEEEG1 T3 must be positive and finite");
        }
        Governor::set(param, val, unitType);
    } else if (param == "uo") {
        if (!std::isfinite(val) || (val < 0.0)) {
            throw InvalidParameterValue("IEEEG1 UO must be nonnegative and finite");
        }
        Uo = val;
    } else if (param == "uc") {
        if (!std::isfinite(val) || (val > 0.0)) {
            throw InvalidParameterValue("IEEEG1 UC must be nonpositive and finite");
        }
        Uc = val;
    } else if ((param == "pmax") || (param == "pmin")) {
        if (!std::isfinite(val)) {
            throw InvalidParameterValue("IEEEG1 valve limit must be finite");
        }
        Governor::set(param, val, unitType);
    } else if ((param.size() == 2) && (param[0] == 't') && (param[1] >= '4') && (param[1] <= '7')) {
        if (!std::isfinite(val) || (val < 0.0)) {
            throw InvalidParameterValue("IEEEG1 turbine time constant must be nonnegative");
        }
        turbineTime[static_cast<std::size_t>(param[1] - '4')] = val;
    } else if ((param.size() == 2) && (param[0] == 'k') && (param[1] >= '1') && (param[1] <= '8')) {
        if (!std::isfinite(val) || (val < 0.0)) {
            throw InvalidParameterValue("IEEEG1 power fraction must be nonnegative");
        }
        powerFraction[static_cast<std::size_t>(param[1] - '1')] = val;
    } else {
        Governor::set(param, val, unitType);
    }
}

double GovernorIeeeG1::get(std::string_view param, units::unit unitType) const
{
    if (param == "uo") {
        return Uo;
    }
    if (param == "uc") {
        return Uc;
    }
    if ((param.size() == 2) && (param[0] == 't') && (param[1] >= '4') && (param[1] <= '7')) {
        return turbineTime[static_cast<std::size_t>(param[1] - '4')];
    }
    if ((param.size() == 2) && (param[0] == 'k') && (param[1] >= '1') && (param[1] <= '8')) {
        return powerFraction[static_cast<std::size_t>(param[1] - '1')];
    }
    if ((param.size() == 3) && (param[0] == 'k') && (param[1] >= '1') && (param[1] <= '8') &&
        (param[2] == 'n')) {
        return normalizedFractions()[static_cast<std::size_t>(param[1] - '1')];
    }
    return Governor::get(param, unitType);
}

stringVec GovernorIeeeG1::localStateNames() const
{
    stringVec names{"php", "plp"};
    if (leadLagState != kInvalidLocation) {
        names.emplace_back("leadlag");
    }
    names.emplace_back("valve");
    for (index_t stage = 0; stage < 4; ++stage) {
        if (turbineState[stage] != kInvalidLocation) {
            names.emplace_back("p" + std::to_string(stage + 4));
        }
    }
    return names;
}

index_t GovernorIeeeG1::findIndex(std::string_view field, const SolverMode& sMode) const
{
    if ((field == "php") || (field == "hp") || (field == "pm") || (field == "pmech")) {
        return offsets.getAlgOffset(sMode) + hpOutput;
    }
    if ((field == "plp") || (field == "lp")) {
        return offsets.getAlgOffset(sMode) + lpOutput;
    }
    if ((field == "leadlag") || (field == "ll")) {
        return (leadLagState != kInvalidLocation) ? offsets.getDiffOffset(sMode) + leadLagState :
                                                    kInvalidLocation;
    }
    if ((field == "valve") || (field == "pv")) {
        return offsets.getDiffOffset(sMode) + valveState;
    }
    if ((field.size() == 2) && (field[0] == 'p') && (field[1] >= '4') && (field[1] <= '7')) {
        const index_t stage = field[1] - '4';
        return (turbineState[stage] != kInvalidLocation) ?
            offsets.getDiffOffset(sMode) + turbineState[stage] :
            kInvalidLocation;
    }
    return kInvalidLocation;
}

const std::vector<stringVec>& GovernorIeeeG1::outputNames() const
{
    static const std::vector<stringVec> names{{"php", "hp", "pmech", "power", "output"},
                                              {"plp", "lp"}};
    return names;
}
// NOLINTEND(readability-math-missing-parentheses)
}  // namespace griddyn::governors
