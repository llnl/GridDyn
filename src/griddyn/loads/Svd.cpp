/*
 * Copyright (c) 2014-2026, Lawrence Livermore National Security
 * See the top-level NOTICE for additional details. All rights reserved.
 * SPDX-License-Identifier: BSD-3-Clause
 */

#include "Svd.h"

#include "../GridBus.h"
#include "core/CoreObjectTemplates.hpp"
#include "core/ObjectFactoryTemplates.hpp"
#include "gmlc/utilities/stringConversion.h"
#include "utilities/MatrixData.hpp"
#include <algorithm>
#include <cmath>
#include <string>
#include <utility>
#include <vector>

namespace griddyn::loads {
static TypeFactory<Svd>
    gSvdld("load",
           std::to_array<std::string_view>({"Svd", "switched shunt", "switchedshunt", "ssd"}));

using gmlc::utilities::convertToLowerCase;
using gmlc::utilities::numeric_conversion;
using units::convert;
using units::puMW;
using units::puV;
using units::unit;

Svd::Svd(const std::string& objName): RampLoad(objName) {}
Svd::Svd(double realPower, double reactivePower, const std::string& objName):
    RampLoad(realPower, reactivePower, objName)
{
    opFlags.set(ADJUSTABLE_Q);
}

Svd::~Svd() = default;
CoreObject* Svd::clone(CoreObject* obj) const
{
    auto* load = cloneBase<Svd, RampLoad>(this, obj);
    if (load == nullptr) {
        return obj;
    }

    load->Qmin = Qmin;
    load->Qmax = Qmax;
    load->Vmin = Vmin;
    load->Vmax = Vmax;

    load->Qlow = Qlow;
    load->Qhigh = Qhigh;
    load->currentStep = currentStep;
    load->stepCount = stepCount;
    load->Cblocks = Cblocks;
    load->adjustmentMethod = adjustmentMethod;
    load->participation = participation;
    load->andesBankMode = andesBankMode;
    load->andesGs = andesGs;
    load->andesBs = andesBs;
    load->andesNs = andesNs;
    load->andesVref = andesVref;
    load->andesDv = andesDv;
    load->andesDt = andesDt;
    load->andesBaseG = andesBaseG;
    load->andesBaseB = andesBaseB;
    load->andesStep = andesStep;
    load->andesLastSwitchTime = andesLastSwitchTime;
    load->minIter = minIter;
    load->errTol = errTol;
    return load;
}

void Svd::setControlBus(GridBus* cBus)
{
    if (cBus != nullptr) {
        controlBus = cBus;
    }
}

void Svd::setLoad(double level, unit unitType)
{
    const double dlevel = convert(level, unitType, puMW, systemBasePower);
    const int setLevel = checkSetting(dlevel);
    if (setLevel >= 0) {
        setYq(dlevel);
    }
}

void Svd::setLoad(double plevel, double qlevel, unit unitType)
{
    setup(convert(plevel, unitType, puMW, systemBasePower));
    const double dlevel = convert(qlevel, unitType, puMW, systemBasePower);
    const int setLevel = checkSetting(dlevel);
    if (setLevel >= 0) {
        setYq(dlevel);
    }
}

int Svd::checkSetting(double level)
{
    if (level == 0.0) {
        return 0;
    }
    if (opFlags[CONTINUOUS_FLAG]) {
        const auto qMin = (std::min)(Qlow, Qhigh);
        const auto qMax = (std::max)(Qlow, Qhigh);
        return ((level >= qMin) && (level <= qMax)) ? 1 : -1;
    }

    if (Cblocks.empty()) {
        return -1;
    }

    while (true) {
        int setting = 0;
        double totalQ = Qlow;
        if (!opFlags[REVERSE_CONTROL_FLAG]) {
            auto block = Cblocks.begin();
            while (std::abs(totalQ) < std::abs(level)) {
                for (int kk = 0; kk < (*block).first; ++kk) {
                    totalQ += (*block).second;
                    ++setting;
                    if (std::abs(totalQ - level) < 0.00001) {
                        return setting;
                    }
                }
                ++block;
                if (block == Cblocks.end()) {
                    break;
                }
            }
        } else {
            auto block = Cblocks.rbegin();
            while (std::abs(totalQ) < std::abs(level)) {
                for (int kk = 0; kk < (*block).first; ++kk) {
                    totalQ += (*block).second;
                    ++setting;
                    if (std::abs(totalQ - level) < 0.00001) {
                        return setting;
                    }
                }
                ++block;
                if (block == Cblocks.rend()) {
                    break;
                }
            }
        }
        if (std::abs(totalQ) > std::abs(level)) {
            if (opFlags[REVERSE_TOGGLED_FLAG]) {
                opFlags.flip(REVERSE_CONTROL_FLAG);
                opFlags.reset(REVERSE_TOGGLED_FLAG);
                logging::warning(this, "unable to match requested level");
            } else {
                opFlags.flip(REVERSE_CONTROL_FLAG);
                opFlags.set(REVERSE_TOGGLED_FLAG);
                continue;
            }
        }
        return setting;
    }
}

void Svd::updateSetting(int step)
{
    if (step <= 0) {
        currentStep = checkSetting(step);
        setYq(Qlow);
    } else if (step >= stepCount) {
        currentStep = stepCount;
        setYq(Qhigh);
    } else {
        setYq(levelForStep(step));
        currentStep = step;
    }
}

void Svd::pFlowObjectInitializeA(CoreTime time0, std::uint32_t flags)
{
    if (opFlags[CONTINUOUS_FLAG]) {
        if (!opFlags[LOCKED_FLAG]) {
            opFlags.set(HAS_PFLOW_STATES);
            opFlags.set(HAS_POWERFLOW_ADJUSTMENTS);
        }
    } else {
        if (!opFlags[LOCKED_FLAG]) {
            opFlags.set(HAS_POWERFLOW_ADJUSTMENTS);
        }
    }
    ZipLoad::pFlowObjectInitializeA(time0, flags);
}

StateSizes Svd::localStateSizes(const SolverMode& sMode) const
{
    StateSizes sizes;
    if ((!isDynamic(sMode)) && opFlags[CONTINUOUS_FLAG] && !opFlags[LOCKED_FLAG]) {
        sizes.algSize = 1;
    }
    return sizes;
}

count_t Svd::localJacobianCount(const SolverMode& sMode) const
{
    if ((!isDynamic(sMode)) && opFlags[CONTINUOUS_FLAG] && !opFlags[LOCKED_FLAG]) {
        // The first entry is the state equation's own derivative.  The second
        // is the controlled-bus voltage column (which may be remote).
        return 2;
    }
    return 0;
}

void Svd::dynObjectInitializeA(CoreTime time0, std::uint32_t flags)
{
    if (andesBankMode) {
        andesLastSwitchTime = negTime;
    }
    ZipLoad::dynObjectInitializeA(time0, flags);
}

void Svd::dynObjectInitializeB(const IOdata& /*inputs*/,
                               const IOdata& /*desiredOutput*/,
                               IOdata& /*fieldSet*/)
{
}

void Svd::setState(CoreTime time,
                   const double state[],
                   const double dstateDt[],
                   const SolverMode& sMode)
{
    if ((!isDynamic(sMode)) && opFlags[CONTINUOUS_FLAG] && !opFlags[LOCKED_FLAG] &&
        state != nullptr) {
        const auto offset = offsets.getAlgOffset(sMode);
        if (offset != kNullLocation) {
            setYq(state[offset]);
        }
    }
    ZipLoad::setState(time, state, dstateDt, sMode);
}

void Svd::guessState(CoreTime /*time*/,
                     double state[],
                     double /*dstate_dt*/[],
                     const SolverMode& sMode)
{
    if ((!isDynamic(sMode)) && opFlags[CONTINUOUS_FLAG] && !opFlags[LOCKED_FLAG]) {
        const auto offset = offsets.getAlgOffset(sMode);
        if (offset != kNullLocation) {
            state[offset] = getYq();
        }
    }
}

void Svd::updateLocalCache(const IOdata& inputs,
                           const StateData& stateData,
                           const SolverMode& sMode)
{
    if ((!isDynamic(sMode)) && opFlags[CONTINUOUS_FLAG] && !opFlags[LOCKED_FLAG] &&
        stateData.state != nullptr) {
        const auto offset = offsets.getAlgOffset(sMode);
        if (offset != kNullLocation) {
            setYq(stateData.state[offset]);
        }
    }
    ZipLoad::updateLocalCache(inputs, stateData, sMode);
}

void Svd::outputPartialDerivatives(const IOdata& inputs,
                                   const StateData& stateData,
                                   MatrixData<double>& matrixData,
                                   const SolverMode& sMode)
{
    if ((!isDynamic(sMode)) && opFlags[CONTINUOUS_FLAG] && !opFlags[LOCKED_FLAG]) {
        const auto offset = offsets.getAlgOffset(sMode);
        if (offset != kNullLocation) {
            const auto voltage = inputs.empty() ? bus->getVoltage(stateData, sMode) :
                                                  inputs[VOLTAGE_IN_LOCATION];
            matrixData.assign(QOUT_LOCATION, offset, voltage * voltage);
        }
    }
    ZipLoad::outputPartialDerivatives(inputs, stateData, matrixData, sMode);
}

void Svd::jacobianElements(const IOdata& /*inputs*/,
                           const StateData& /*stateData*/,
                           MatrixData<double>& matrixData,
                           const IOlocs& /*inputLocs*/,
                           const SolverMode& sMode)
{
    if ((!isDynamic(sMode)) && opFlags[CONTINUOUS_FLAG] && !opFlags[LOCKED_FLAG]) {
        const auto offset = offsets.getAlgOffset(sMode);
        if (offset == kNullLocation) {
            return;
        }
        if (!opFlags[AT_LIMIT_FLAG]) {
            auto* voltageBus = (controlBus != nullptr) ? controlBus : bus;
            if (voltageBus != nullptr) {
                matrixData.assignCheckCol(
                    offset, voltageBus->getOutputLoc(sMode, VOLTAGE_IN_LOCATION), 1.0);
            }
        } else {
            // At a reactive limit the voltage equation is replaced by the
            // algebraic bound on the shunt susceptance.  Away from a limit,
            // the row is only voltage-target, so it has no self derivative.
            matrixData.assign(offset, offset, 1.0);
        }
    }
}

bool Svd::powerFlowAdjustmentAllowed(const IOdata& inputs) const
{
    // The power-flow driver supplies iteration/error context.  Keep direct
    // callers compatible with the historical behavior when that context is
    // absent.
    return !((inputs.size() > PFLOW_ERROR_LOCATION) &&
             (static_cast<int>(inputs[PFLOW_ITERATION_LOCATION]) < minIter) &&
             (inputs[PFLOW_ERROR_LOCATION] > errTol));
}

ChangeCode Svd::powerFlowAdjust(const IOdata& inputs,
                                std::uint32_t /*flags*/,
                                CheckLevel /*level*/)
{
    if (opFlags[LOCKED_FLAG] || !isConnected() || !powerFlowAdjustmentAllowed(inputs)) {
        return ChangeCode::NO_CHANGE;
    }

    double voltage = bus->getVoltage();
    if (inputs.size() > VOLTAGE_IN_LOCATION) {
        voltage = inputs[VOLTAGE_IN_LOCATION];
    }
    if (controlBus != nullptr) {
        voltage = controlBus->getVoltage();
    }

    if (andesBankMode) {
        if (opFlags[LOCKED_FLAG]) {
            return ChangeCode::NO_CHANGE;
        }
        int direction = 0;
        if (voltage < andesVref - andesDv) {
            direction = 1;
        } else if (voltage > andesVref + andesDv) {
            direction = -1;
        }
        return (direction != 0 && adjustAndesStep(direction)) ?
            ChangeCode::JACOBIAN_CHANGE :
            ChangeCode::NO_CHANGE;
    }

    if (Cblocks.empty()) {
        return ChangeCode::NO_CHANGE;
    }

    if (opFlags[CONTINUOUS_FLAG]) {
        const auto qMin = (std::min)(Qlow, Qhigh);
        const auto qMax = (std::max)(Qlow, Qhigh);
        const auto qValue = getYq();
        if (opFlags[AT_LIMIT_FLAG]) {
            if (((qValue <= qMin) && (voltage > Vmin)) ||
                ((qValue >= qMax) && (voltage < Vmax))) {
                opFlags.reset(AT_LIMIT_FLAG);
                return ChangeCode::JACOBIAN_CHANGE;
            }
            return ChangeCode::NO_CHANGE;
        }
        if (qValue < qMin) {
            setYq(qMin);
            opFlags.set(AT_LIMIT_FLAG);
            alert(this, JAC_COUNT_DECREASE);
            return ChangeCode::JACOBIAN_CHANGE;
        }
        if (qValue > qMax) {
            setYq(qMax);
            opFlags.set(AT_LIMIT_FLAG);
            alert(this, JAC_COUNT_DECREASE);
            return ChangeCode::JACOBIAN_CHANGE;
        }
        return ChangeCode::NO_CHANGE;
    }

    // Reactive-power-controlled MODSW variants need a coordinated bus-flow
    // measurement and are intentionally not guessed here.  The AESO corpus
    // contains no such active records.
    if (opFlags[REACTIVE_CONTROL_FLAG]) {
        return ChangeCode::NO_CHANGE;
    }

    const auto nextStep = voltageControlStep(voltage);
    if (nextStep == currentStep) {
        return ChangeCode::NO_CHANGE;
    }
    updateSetting(nextStep);
    return ChangeCode::PARAMETER_CHANGE;
}

void Svd::reset(ResetLevels /*level*/)
{
    if (andesBankMode) {
        andesStep = andesInitialStep();
        andesLastSwitchTime = negTime;
        updateAndesAdmittance();
    }
}
// for identifying which variables are algebraic vs differential
void Svd::getVariableType(double /*sdata*/[], const SolverMode& /*sMode*/) {}
void Svd::set(std::string_view param, std::string_view val)
{
    if ((param == "blocks") || (param == "block")) {
        auto bin = gmlc::utilities::stringOps::splitline(val);
        for (size_t kk = 0; kk < bin.size() - 1; ++kk) {
            auto cnt = numeric_conversion<int>(bin[kk], 0);
            const double bsize = numeric_conversion(bin[kk + 1], 0.0);
            if (cnt > 0) {
                addBlock(cnt, bsize);
            }
        }
    } else if (param == "mode") {
        const auto lowerValue = convertToLowerCase(val);
        if ((lowerValue == "manual") || (lowerValue == "locked")) {
            opFlags.set(LOCKED_FLAG);
        }
        if ((lowerValue == "cont") || (lowerValue == "continuous")) {
            opFlags.set(CONTINUOUS_FLAG, true);
            opFlags.reset(LOCKED_FLAG);
        } else if ((lowerValue == "stepped") || (lowerValue == "discrete")) {
            opFlags.reset(CONTINUOUS_FLAG);
            opFlags.reset(LOCKED_FLAG);
        }
    } else if (param == "control") {
        const auto lowerValue = convertToLowerCase(val);
        if (lowerValue == "reactive") {
            opFlags.set(REACTIVE_CONTROL_FLAG, true);
        }
    } else if ((param == "adjm") || (param == "adjustmentmethod")) {
        adjustmentMethod = numeric_conversion<int>(std::string{val}, 0);
    } else {
        ZipLoad::set(param, val);
    }
}
void Svd::set(std::string_view param, double val, unit unitType)
{
    if (param == "qlow") {
        const auto newQlow = convert(val, unitType, puMW, systemBasePower, localBaseVoltage);
        if (!Cblocks.empty()) {
            Qhigh = newQlow;
            for (const auto& block : Cblocks) {
                Qhigh += block.first * block.second;
            }
        }
        Qlow = newQlow;
    } else if (param == "qhigh") {
        Qhigh = convert(val, unitType, puMW, systemBasePower, localBaseVoltage);
    } else if (param == "qmin") {
        Qmin = convert(val, unitType, puMW, systemBasePower, localBaseVoltage);
    } else if (param == "qmax") {
        Qmax = convert(val, unitType, puMW, systemBasePower, localBaseVoltage);
    } else if (param == "vmax") {
        Vmax = convert(val, unitType, puV, systemBasePower, localBaseVoltage);
    } else if (param == "vmin") {
        Vmin = convert(val, unitType, puV, systemBasePower, localBaseVoltage);
    } else if (param == "yq") {
        const double convertedValue =
            convert(val, unitType, puMW, systemBasePower, localBaseVoltage);
        setLoad(convertedValue);
    } else if (param == "step") {
        updateSetting(static_cast<int>(val));
    } else if (param == "participation") {
        participation = val;
    } else if (param == "vref") {
        andesVref = val;
    } else if (param == "dv") {
        andesDv = val;
    } else if (param == "dt") {
        andesDt = val;
    } else if (param == "min_iter") {
        minIter = (std::max)(0, static_cast<int>(val));
    } else if (param == "err_tol") {
        errTol = (std::max)(0.0, val);
    } else if ((param == "adjm") || (param == "adjustmentmethod")) {
        adjustmentMethod = static_cast<int>(val);
    } else if (param == "block") {
        if (Cblocks.size() == 1) {
            if (Cblocks[0].second == 0) {
                Cblocks[0].second = convert(val, unitType, puMW, systemBasePower, localBaseVoltage);
                Qhigh = Qlow + (Cblocks[0].first * Cblocks[0].second);
                stepCount = Cblocks[0].first;
            } else {
                addBlock(1, val, unitType);
            }
        } else {
            addBlock(1, val, unitType);
        }
    } else if (param == "count") {
        if (Cblocks.size() < 2) {
            if (Cblocks.empty()) {
                addBlock(static_cast<int>(val), 0.0);
            } else {
                Cblocks[0].first = static_cast<int>(val);
                Qhigh = Qlow + (Cblocks[0].first * Cblocks[0].second);
                stepCount = Cblocks[0].first;
            }
        }
    } else if (param.starts_with("block") || param.starts_with("count")) {
    } else {
        ZipLoad::set(param, val, unitType);
    }
}

double Svd::get(std::string_view param, unit unitType) const
{
    if ((param == "adjm") || (param == "adjustmentmethod")) {
        return static_cast<double>(adjustmentMethod);
    }
    if (param == "step") {
        return static_cast<double>(currentStep);
    }
    if (param == "vref") {
        return andesVref;
    }
    if (param == "dv") {
        return andesDv;
    }
    if (param == "dt") {
        return andesDt;
    }
    if (param == "min_iter") {
        return static_cast<double>(minIter);
    }
    if (param == "err_tol") {
        return errTol;
    }
    if (param == "andesstep") {
        return static_cast<double>(andesStep);
    }
    if (param == "effectiveg") {
        return andesEffectiveValue(andesGs, andesBaseG, andesStep);
    }
    if (param == "effectiveb") {
        return andesEffectiveValue(andesBs, andesBaseB, andesStep);
    }
    return ZipLoad::get(param, unitType);
}

void Svd::addBlock(int steps, double qstep, units::unit unitType)
{
    if (steps <= 0) {
        return;
    }
    if (Cblocks.empty()) {
        Qhigh = Qlow;
    }
    const double convertedStep = units::convert(qstep, unitType, units::puMW, systemBasePower);
    Cblocks.emplace_back(steps, convertedStep);
    Qhigh += steps * convertedStep;
    stepCount += steps;
}

double Svd::levelForStep(int step) const
{
    if (step <= 0 || Cblocks.empty()) {
        return Qlow;
    }
    int remaining = (std::min)(step, stepCount);
    double qlevel = Qlow;
    if (opFlags[REVERSE_CONTROL_FLAG]) {
        for (auto block = Cblocks.rbegin(); block != Cblocks.rend() && remaining > 0; ++block) {
            const int selected = (std::min)(remaining, block->first);
            qlevel += selected * block->second;
            remaining -= selected;
        }
    } else {
        for (const auto& block : Cblocks) {
            if (remaining <= 0) {
                break;
            }
            const int selected = (std::min)(remaining, block.first);
            qlevel += selected * block.second;
            remaining -= selected;
        }
    }
    return qlevel;
}

int Svd::nearestStep(double level) const
{
    if (stepCount <= 0) {
        return 0;
    }
    int bestStep = 0;
    double bestError = std::abs(level - levelForStep(0));
    for (int step = 1; step <= stepCount; ++step) {
        const double error = std::abs(level - levelForStep(step));
        if (error < bestError) {
            bestError = error;
            bestStep = step;
        }
    }
    return bestStep;
}

void Svd::setInitialReactivePower(double level, units::unit unitType)
{
    const double convertedLevel = units::convert(level, unitType, units::puMW, systemBasePower);
    currentStep = nearestStep(convertedLevel);
}

int Svd::voltageControlStep(double voltage) const
{
    if (stepCount <= 0) {
        return currentStep;
    }
    const double currentLevel = levelForStep(currentStep);
    const bool needMoreInjection = voltage < Vmin;
    const bool needLessInjection = voltage > Vmax;
    if (!needMoreInjection && !needLessInjection) {
        return currentStep;
    }

    const auto isBetter = [needMoreInjection](double candidate, double current) {
        // GridDyn stores a shunt's reactive injection as negative load Q.
        // Lower Q therefore means more voltage support.
        return needMoreInjection ? (candidate < current) : (candidate > current);
    };
    const int forward = currentStep + 1;
    const int reverse = currentStep - 1;
    if ((forward <= stepCount) && isBetter(levelForStep(forward), currentLevel)) {
        return forward;
    }
    if ((reverse >= 0) && isBetter(levelForStep(reverse), currentLevel)) {
        return reverse;
    }
    return currentStep;
}

void Svd::configureAndesShunt(const std::vector<double>& conductanceSteps,
                              const std::vector<double>& susceptanceSteps,
                              const std::vector<int>& stepCounts,
                              double vref,
                              double voltageDelta,
                              double timeDelay,
                              double initialG,
                              double initialB)
{
    andesBankMode = true;
    andesGs = conductanceSteps;
    andesBs = susceptanceSteps;
    andesNs = stepCounts;
    andesVref = (vref > 0.0) ? vref : 1.0;
    andesDv = (voltageDelta >= 0.0) ? voltageDelta : 0.0;
    andesDt = (timeDelay >= 0.0) ? timeDelay : 0.0;
    andesBaseG = initialG;
    andesBaseB = initialB;
    andesStep = andesInitialStep();
    andesLastSwitchTime = negTime;
    updateAndesAdmittance();
}

int Svd::andesMaxStep() const
{
    int maxStep = 0;
    for (const auto count : andesNs) {
        maxStep += (std::max)(count, 0);
    }
    return maxStep;
}

double Svd::andesEffectiveValue(const std::vector<double>& blocks, double baseValue, int step) const
{
    if (blocks.empty() || andesNs.empty()) {
        return baseValue;
    }

    int remaining = (std::max)(step, 0);
    double value = 0.0;
    const auto blockCount = (std::min)(blocks.size(), andesNs.size());
    for (size_t kk = 0; kk < blockCount && remaining > 0; ++kk) {
        const int count = (std::max)(andesNs[kk], 0);
        const int selected = (std::min)(remaining, count);
        value += selected * blocks[kk];
        remaining -= selected;
    }
    return value;
}

int Svd::andesInitialStep() const
{
    if (andesMaxStep() == 0 || andesBs.empty()) {
        return 0;
    }

    for (int step = 0; step <= andesMaxStep(); ++step) {
        if (andesEffectiveValue(andesBs, andesBaseB, step) >= andesBaseB - 1.0e-12) {
            return step;
        }
    }
    return andesMaxStep();
}

void Svd::updateAndesAdmittance()
{
    const double effectiveG = andesEffectiveValue(andesGs, andesBaseG, andesStep);
    const double effectiveB = andesEffectiveValue(andesBs, andesBaseB, andesStep);
    ZipLoad::set("yp", effectiveG, puMW);
    ZipLoad::set("yq", -effectiveB, puMW);
}

bool Svd::adjustAndesStep(int direction)
{
    const int newStep = std::clamp(andesStep + direction, 0, andesMaxStep());
    if (newStep == andesStep) {
        return false;
    }
    andesStep = newStep;
    updateAndesAdmittance();
    return true;
}

void Svd::residual(const IOdata& /*inputs*/,
                   const StateData& stateData,
                   double resid[],
                   const SolverMode& sMode)
{
    if ((!isDynamic(sMode)) && opFlags[CONTINUOUS_FLAG] && !opFlags[LOCKED_FLAG]) {
        const auto offset = offsets.getAlgOffset(sMode);
        if (offset == kNullLocation) {
            return;
        }
        if (opFlags[AT_LIMIT_FLAG]) {
            const auto qMin = (std::min)(Qlow, Qhigh);
            const auto qMax = (std::max)(Qlow, Qhigh);
            const auto qValue = stateData.state[offset];
            resid[offset] = qValue - (((qValue <= qMin) ? qMin : qMax));
        } else {
            const auto* voltageBus = (controlBus != nullptr) ? controlBus : bus;
            const auto voltage = (voltageBus != nullptr) ?
                voltageBus->getVoltage(stateData, sMode) :
                1.0;
            const auto target = (Vmax >= Vmin) ? ((Vmin + Vmax) / 2.0) : 1.0;
            resid[offset] = voltage - target;
        }
    }
}

void Svd::derivative(const IOdata& /*inputs*/,
                     const StateData& /*sD*/,
                     double /*deriv*/[],
                     const SolverMode& /*sMode*/)
{
}

void Svd::getStateName(stringVec& stNames,
                       const SolverMode& sMode,
                       const std::string& prefix) const
{
    if ((!isDynamic(sMode)) && opFlags[CONTINUOUS_FLAG] && !opFlags[LOCKED_FLAG]) {
        const auto offset = offsets.getAlgOffset(sMode);
        if (offset != kNullLocation) {
            stNames[offset] = prefix + getName() + ":susceptance";
        }
    }
}

void Svd::timestep(CoreTime time, const IOdata& /*inputs*/, const SolverMode& /*sMode*/)
{
    if (!andesBankMode || opFlags[LOCKED_FLAG] || !isConnected() || time <= 0.0) {
        return;
    }
    if ((andesLastSwitchTime != negTime) && (time - andesLastSwitchTime < andesDt)) {
        return;
    }

    const double voltage = (controlBus != nullptr) ? controlBus->getVoltage() : bus->getVoltage();
    int direction = 0;
    if (voltage < andesVref - andesDv) {
        direction = 1;
    } else if (voltage > andesVref + andesDv) {
        direction = -1;
    }
    if (direction != 0 && adjustAndesStep(direction)) {
        andesLastSwitchTime = time;
        alert(this, UPDATE_REQUIRED);
    }
}
void Svd::rootTest(const IOdata& /*inputs*/,
                   const StateData& /*sD*/,
                   double /*roots*/[],
                   const SolverMode& /*sMode*/)
{
}

void Svd::rootTrigger(CoreTime /*time*/,
                      const IOdata& /*inputs*/,
                      const std::vector<int>& /*rootMask*/,
                      const SolverMode& /*sMode*/)
{
}

ChangeCode Svd::rootCheck(const IOdata& /*inputs*/,
                          const StateData& /*sD*/,
                          const SolverMode& /*sMode*/,
                          CheckLevel /*level*/)
{
    return ChangeCode::NO_CHANGE;
}
}  // namespace griddyn::loads
