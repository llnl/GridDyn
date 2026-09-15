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
        return ((level >= Qlow) && (level <= Qhigh)) ? 1 : -1;
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
        double qlevel = Qlow;
        if (opFlags[REVERSE_CONTROL_FLAG]) {
            auto block = Cblocks.begin();
            int scount = 0;

            while (step > scount + (*block).first) {
                scount += (*block).first;
                qlevel += (*block).second;
                ++block;
                if (block == Cblocks.end()) {
                    break;
                }
            }
            qlevel += (step - scount) * (*block).second;
        } else {
            auto block = Cblocks.rbegin();
            int scount = 0;
            while (step > scount + (*block).first) {
                scount += (*block).first;
                qlevel += (*block).second;
                ++block;
                if (block == Cblocks.rend()) {
                    break;
                }
            }
            qlevel += (step - scount) * (*block).second;
        }
        setYq(qlevel);
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

void Svd::setState(CoreTime /*time*/,
                   const double /*state*/[],
                   const double /*dstate_dt*/[],
                   const SolverMode& /*sMode*/)
{
}

void Svd::guessState(CoreTime /*time*/,
                     double /*state*/[],
                     double /*dstate_dt*/[],
                     const SolverMode& /*sMode*/)
{
}

ChangeCode Svd::powerFlowAdjust(const IOdata& inputs, std::uint32_t /*flags*/, CheckLevel /*level*/)
{
    if (!andesBankMode || opFlags[LOCKED_FLAG] || !isConnected()) {
        return ChangeCode::NO_CHANGE;
    }

    // ANDES ShuntAdjust enables switching once either the minimum iteration
    // count has been reached or the current power-flow error is within the
    // configured tolerance. Calls without the optional context retain the
    // historical GridDyn behavior and are allowed to adjust.
    if ((inputs.size() > PFLOW_ERROR_LOCATION) &&
        (static_cast<int>(inputs[PFLOW_ITERATION_LOCATION]) < minIter) &&
        (inputs[PFLOW_ERROR_LOCATION] > errTol)) {
        return ChangeCode::NO_CHANGE;
    }

    const double voltage = (controlBus != nullptr) ?
        controlBus->getVoltage() :
        ((inputs.size() > VOLTAGE_IN_LOCATION) ? inputs[VOLTAGE_IN_LOCATION] : bus->getVoltage());
    int direction = 0;
    if (voltage < andesVref - andesDv) {
        direction = 1;
    } else if (voltage > andesVref + andesDv) {
        direction = -1;
    }
    return (direction != 0 && adjustAndesStep(direction)) ? ChangeCode::JACOBIAN_CHANGE :
                                                            ChangeCode::NO_CHANGE;
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
    } else {
        ZipLoad::set(param, val);
    }
}
void Svd::set(std::string_view param, double val, unit unitType)
{
    if (param == "qlow") {
        Qlow = convert(val, unitType, puMW, systemBasePower, localBaseVoltage);
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
    const double convertedStep = units::convert(qstep, unitType, units::puMW, systemBasePower);
    Cblocks.emplace_back(steps, convertedStep);
    Qhigh += steps * convertedStep;
    stepCount += steps;
}

void Svd::configureAndesShunt(const std::vector<double>& gs,
                              const std::vector<double>& bs,
                              const std::vector<int>& ns,
                              double vref,
                              double dv,
                              double dt,
                              double initialG,
                              double initialB)
{
    andesBankMode = true;
    andesGs = gs;
    andesBs = bs;
    andesNs = ns;
    andesVref = (vref > 0.0) ? vref : 1.0;
    andesDv = (dv >= 0.0) ? dv : 0.0;
    andesDt = (dt >= 0.0) ? dt : 0.0;
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
    const int newStep = (std::clamp)(andesStep + direction, 0, andesMaxStep());
    if (newStep == andesStep) {
        return false;
    }
    andesStep = newStep;
    updateAndesAdmittance();
    return true;
}

void Svd::residual(const IOdata& /*inputs*/,
                   const StateData& /*sD*/,
                   double /*resid*/[],
                   const SolverMode& /*sMode*/)
{
}

void Svd::derivative(const IOdata& /*inputs*/,
                     const StateData& /*sD*/,
                     double /*deriv*/[],
                     const SolverMode& /*sMode*/)
{
}

void Svd::outputPartialDerivatives(const IOdata& /*inputs*/,
                                   const StateData& /*sD*/,
                                   MatrixData<double>& /*md*/,
                                   const SolverMode& /*sMode*/)
{
}

void Svd::jacobianElements(const IOdata& /*inputs*/,
                           const StateData& /*sD*/,
                           MatrixData<double>& /*md*/,
                           const IOlocs& /*inputLocs*/,
                           const SolverMode& /*sMode*/)
{
}
void Svd::getStateName(stringVec& /*stNames*/,
                       const SolverMode& /*sMode*/,
                       const std::string& /*prefix*/) const
{
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
