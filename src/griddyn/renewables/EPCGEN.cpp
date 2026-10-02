/*
 * Copyright (c) 2014-2026, Lawrence Livermore National Security
 * See the top-level NOTICE for additional details. All rights reserved.
 * SPDX-License-Identifier: BSD-3-Clause
 */

#include "EPCGEN.h"

#include "core/CoreExceptions.h"
#include "core/CoreObjectTemplates.hpp"
#include "gmlc/utilities/stringOps.h"
#include "utilities/MatrixData.hpp"
#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <limits>
#include <string>
#include <vector>

namespace griddyn {
namespace {
    constexpr std::array<RenewablePort, 2> terminalInputPorts{{
        {.signal = RenewableSignal::terminalVoltage, .ioIndex = 0},
        {.signal = RenewableSignal::terminalFrequency, .ioIndex = 1},
    }};
    constexpr std::array<RenewablePort, 2> terminalOutputPorts{{
        {.signal = RenewableSignal::electricalPower, .ioIndex = 0, .base = RenewableBase::machine},
        {.signal = RenewableSignal::reactivePower, .ioIndex = 1, .base = RenewableBase::machine},
    }};

    constexpr double nominalFrequency = 60.0;
    constexpr double minimumVoltage = 1.0e-4;

    bool finitePositive(double value)
    {
        return std::isfinite(value) && value > 0.0;
    }
}  // namespace

EPCGEN::EPCGEN(const std::string& name): TerminalElectricalModel(name)
{
    m_inputSize = 2;
}

CoreObject* EPCGEN::clone(CoreObject* obj) const
{
    auto* out = cloneBase<EPCGEN, TerminalElectricalModel>(this, obj);
    if (out != nullptr) {
        copyParametersTo(out);
    }
    return out == nullptr ? obj : out;
}

void EPCGEN::copyParametersTo(EPCGEN* target) const
{
    target->rsrc = rsrc;
    target->xsrc = xsrc;
    target->tfrq = tfrq;
    target->ofpdb = ofpdb;
    target->ufpdb = ufpdb;
    target->ofpdroop = ofpdroop;
    target->ufpdroop = ufpdroop;
    target->vbreak = vbreak;
    target->imax = imax;
    target->pmax = pmax;
    target->pmin = pmin;
    target->pref = pref;
    target->kp = kp;
    target->ki = ki;
    target->kip = kip;
    target->kiq = kiq;
    target->rq = rq;
    target->tq = tq;
    target->tg = tg;
    target->t1 = t1;
    target->t2 = t2;
    target->td = td;
    target->ted = ted;
    target->teq = teq;
    target->qmax = qmax;
    target->qmin = qmin;
    target->voltageTripDelta = voltageTripDelta;
    target->voltageTripTime = voltageTripTime;
    target->initialVoltageReference = initialVoltageReference;
    target->prevTime = prevTime;
    target->tripped = tripped;
}

std::span<const RenewablePort> EPCGEN::inputPorts() const
{
    return terminalInputPorts;
}

std::span<const RenewablePort> EPCGEN::outputPorts() const
{
    return terminalOutputPorts;
}

void EPCGEN::set(std::string_view param, double val, units::unit unitType)
{
    const auto key = gmlc::utilities::convertToLowerCase(std::string{param});
    if (!std::isfinite(val)) {
        throw InvalidParameterValue("EPCGEN parameter must be finite");
    }
    if (key == "rsrc") {
        rsrc = val;
    } else if (key == "xsrc") {
        xsrc = val;
    } else if (key == "tfrq" || key == "tr") {
        // Preserve "tr" as a legacy alias for the exposed TFRQ parameter.
        tfrq = val;
    } else if (key == "ofpdb") {
        ofpdb = val;
    } else if (key == "ufpdb") {
        ufpdb = val;
    } else if (key == "ofpdroop") {
        ofpdroop = val;
    } else if (key == "ufpdroop") {
        ufpdroop = val;
    } else if (key == "vbreak") {
        vbreak = val;
    } else if (key == "imax") {
        imax = val;
    } else if (key == "pmax") {
        pmax = val;
    } else if (key == "pmin") {
        pmin = val;
    } else if (key == "pref") {
        pref = val;
    } else if (key == "kp") {
        kp = val;
    } else if (key == "ki") {
        ki = val;
    } else if (key == "kip") {
        kip = val;
    } else if (key == "kiq") {
        kiq = val;
    } else if (key == "rq") {
        rq = val;
    } else if (key == "tq") {
        tq = val;
    } else if (key == "tg") {
        tg = val;
    } else if (key == "t1") {
        t1 = val;
    } else if (key == "t2") {
        t2 = val;
    } else if (key == "td") {
        td = val;
    } else if (key == "ted") {
        ted = val;
    } else if (key == "teq") {
        teq = val;
    } else if (key == "qmax") {
        qmax = val;
    } else if (key == "qmin") {
        qmin = val;
    } else if (key == "dv" || key == "dvt") {
        voltageTripDelta = val;
    } else if (key == "dt") {
        voltageTripTime = val;
    } else {
        TerminalElectricalModel::set(param, val, unitType);
    }
}

double EPCGEN::get(std::string_view param, units::unit unitType) const
{
    const auto key = gmlc::utilities::convertToLowerCase(std::string{param});
#define EPCGEN_GET(name, value)                                                                    \
    if (key == (name)) {                                                                           \
        return (value);                                                                            \
    }
    EPCGEN_GET("rsrc", rsrc)
    EPCGEN_GET("xsrc", xsrc)
    EPCGEN_GET("tfrq", tfrq)
    EPCGEN_GET("ofpdb", ofpdb)
    EPCGEN_GET("ufpdb", ufpdb)
    EPCGEN_GET("ofpdroop", ofpdroop)
    EPCGEN_GET("ufpdroop", ufpdroop)
    EPCGEN_GET("vbreak", vbreak)
    EPCGEN_GET("imax", imax)
    EPCGEN_GET("pmax", pmax)
    EPCGEN_GET("pmin", pmin)
    EPCGEN_GET("pref", pref)
    EPCGEN_GET("tr", tfrq)
    EPCGEN_GET("kp", kp)
    EPCGEN_GET("ki", ki)
    EPCGEN_GET("kip", kip)
    EPCGEN_GET("kiq", kiq)
    EPCGEN_GET("rq", rq)
    EPCGEN_GET("tq", tq)
    EPCGEN_GET("tg", tg)
    EPCGEN_GET("t1", t1)
    EPCGEN_GET("t2", t2)
    EPCGEN_GET("td", td)
    EPCGEN_GET("ted", ted)
    EPCGEN_GET("teq", teq)
    EPCGEN_GET("qmax", qmax)
    EPCGEN_GET("qmin", qmin)
    EPCGEN_GET("dv", voltageTripDelta)
    EPCGEN_GET("dt", voltageTripTime)
    EPCGEN_GET("tripped", tripped ? 1.0 : 0.0)
#undef EPCGEN_GET
    return TerminalElectricalModel::get(param, unitType);
}

void EPCGEN::dynObjectInitializeA(CoreTime time0, std::uint32_t /*flags*/)
{
    if (!std::isfinite(rsrc) || !std::isfinite(xsrc) || !finitePositive(tfrq) ||
        !std::isfinite(ofpdb) || !std::isfinite(ufpdb) || !finitePositive(ofpdroop) ||
        !finitePositive(ufpdroop) || !std::isfinite(vbreak) || !finitePositive(imax) ||
        !std::isfinite(pmax) || !std::isfinite(pmin) || pmin > pmax || !std::isfinite(pref) ||
        !finitePositive(kp) || !finitePositive(ki) || !finitePositive(tq) || !finitePositive(tg) ||
        t1 < 0.0 || !finitePositive(t2) || !finitePositive(td) || !finitePositive(ted) ||
        !finitePositive(teq) || qmin > qmax || !finitePositive(voltageTripTime) ||
        voltageTripDelta < 0.0) {
        throw InvalidParameterValue("EPCGEN limits, gains, or time constants");
    }
    auto& local = offsets.local().local;
    local.algSize = 2;
    local.diffSize = stateCount;
    local.jacSize = (2 + stateCount) * (2 + stateCount + 2);
    local.algRoots = 2;
    prevTime = time0;
    tripped = false;
}

double EPCGEN::frequencyHz(const IOdata& inputs)
{
    if (inputs.size() <= 1 || inputs[1] == kNullVal || !std::isfinite(inputs[1])) {
        return nominalFrequency;
    }
    return nominalFrequency * inputs[1];
}

double EPCGEN::reactiveLimit(double voltage, double activePower) const
{
    if (voltage <= 0.0) {
        return 0.0;
    }
    const double currentPowerLimit = std::sqrt(
        std::max(0.0, ((imax * voltage) * (imax * voltage)) - (activePower * activePower)));
    const double voltageFactor = vbreak >= 1.0 ?
        1.0 :
        std::clamp((voltage - vbreak) / std::max(1.0 - vbreak, minimumVoltage), 0.0, 1.0);
    return std::min(qmax, currentPowerLimit) * voltageFactor;
}

EPCGEN::Evaluation EPCGEN::evaluate(const IOdata& inputs, const double state[]) const
{
    Evaluation result;
    const double voltage = std::max(inputs.empty() ? 1.0 : inputs[0], minimumVoltage);
    const double activePower = voltage * state[activeCurrent];
    const double reactivePower = -voltage * state[reactiveCurrent];
    result.power = tripped ? std::array<double, 2>{0.0, 0.0} :
                             std::array<double, 2>{activePower, reactivePower};

    const double frequency = frequencyHz(inputs);
    const double underFrequencyPower = frequency < ufpdb ? (ufpdb - frequency) / ufpdroop : 0.0;
    const double overFrequencyPower = frequency > ofpdb ? (frequency - ofpdb) / ofpdroop : 0.0;
    const double commandedActivePower =
        std::clamp(pref + underFrequencyPower - overFrequencyPower, pmin, pmax);
    const double filteredVoltage = state[voltageFilter];
    const double voltageError = initialVoltageReference - filteredVoltage - (rq * reactivePower);
    const double reactiveCommand = std::clamp((kp * voltageError) + state[qIntegrator],
                                              -reactiveLimit(voltage, commandedActivePower),
                                              reactiveLimit(voltage, commandedActivePower));
    const double activeCurrentCommand = (commandedActivePower / voltage) + state[activeCorrection];
    const double reactiveCurrentCommand = (-reactiveCommand / voltage) + state[reactiveCorrection];

    const double currentMagnitude = std::hypot(activeCurrentCommand, reactiveCurrentCommand);
    const double currentScale = currentMagnitude > imax ? imax / currentMagnitude : 1.0;
    const double limitedActiveCurrent = activeCurrentCommand * currentScale;
    const double limitedReactiveCurrent = reactiveCurrentCommand * currentScale;
    const double pCommand = voltage * limitedActiveCurrent;
    const double qCommand = -voltage * limitedReactiveCurrent;

    result.rates[qIntegrator] = ki * (voltageError + (qCommand - reactiveCommand));
    result.rates[voltageFilter] = (voltage - filteredVoltage) / tfrq;
    result.rates[governor] = (commandedActivePower - state[governor]) / tg;
    result.rates[leadLag] = (state[governor] - state[leadLag] - (state[governor] * t1 / t2)) / t2;
    result.rates[reactiveCurrent] = (limitedReactiveCurrent - state[reactiveCurrent]) / tq;
    result.rates[activeCurrent] = (limitedActiveCurrent - state[activeCurrent]) / td;

    const double internalDVoltage =
        voltage + (state[activeCurrent] * rsrc) - (state[reactiveCurrent] * xsrc);
    const double internalQVoltage = (state[reactiveCurrent] * rsrc) + (state[activeCurrent] * xsrc);
    result.rates[internalD] = (internalDVoltage - state[internalD]) / ted;
    result.rates[internalQ] = (internalQVoltage - state[internalQ]) / teq;
    result.rates[activeCorrection] = kip * (pCommand - activePower);
    result.rates[reactiveCorrection] = kiq * (qCommand - reactivePower);
    if (tripped) {
        result.rates.fill(0.0);
    }
    return result;
}

void EPCGEN::dynObjectInitializeB(const IOdata& inputs,
                                  const IOdata& desiredOutput,
                                  IOdata& fieldSet)
{
    if (inputs.empty() || inputs[0] <= 0.0 || desiredOutput.size() < 2 ||
        !std::isfinite(inputs[0]) || !std::isfinite(desiredOutput[0]) ||
        !std::isfinite(desiredOutput[1])) {
        throw InvalidParameterValue("EPCGEN terminal voltage or initial P/Q");
    }
    const double voltage = inputs[0];
    const double initialP = desiredOutput[0];
    const double initialQ = desiredOutput[1];
    initialVoltageReference = voltage + (rq * initialQ);
    m_state[0] = initialP;
    m_state[1] = initialQ;
    auto* state = m_state.data() + 2;
    state[qIntegrator] = initialQ;
    state[voltageFilter] = voltage;
    state[governor] = initialP;
    state[leadLag] = initialP * (1.0 - (t1 / t2));
    state[reactiveCurrent] = -initialQ / voltage;
    state[activeCurrent] = initialP / voltage;
    state[internalD] = voltage + (state[activeCurrent] * rsrc) - (state[reactiveCurrent] * xsrc);
    state[internalQ] = (state[reactiveCurrent] * rsrc) + (state[activeCurrent] * xsrc);
    state[activeCorrection] = 0.0;
    state[reactiveCorrection] = 0.0;
    tripped = false;
    fieldSet = {state[activeCurrent], -state[reactiveCurrent]};
}

void EPCGEN::derivative(const IOdata& inputs,
                        const StateData& stateData,
                        double deriv[],
                        const SolverMode& sMode)
{
    if (!hasDifferential(sMode)) {
        return;
    }
    const auto loc = offsets.getLocations(stateData, deriv, sMode, this);
    const auto result = evaluate(inputs, loc.diffStateLoc);
    for (index_t index = 0; index < stateCount; ++index) {
        loc.destDiffLoc[index] = result.rates[index];
    }
}

void EPCGEN::residual(const IOdata& inputs,
                      const StateData& stateData,
                      double resid[],
                      const SolverMode& sMode)
{
    const auto loc = offsets.getLocations(stateData, resid, sMode, this);
    const auto result = evaluate(inputs, loc.diffStateLoc);
    if (hasAlgebraic(sMode)) {
        loc.destLoc[0] = result.power[0] - loc.algStateLoc[0];
        loc.destLoc[1] = result.power[1] - loc.algStateLoc[1];
    }
    if (hasDifferential(sMode)) {
        for (index_t index = 0; index < stateCount; ++index) {
            loc.destDiffLoc[index] = result.rates[index] - loc.dstateLoc[index];
        }
    }
}

void EPCGEN::algebraicUpdate(const IOdata& inputs,
                             const StateData& stateData,
                             double update[],
                             const SolverMode& sMode,
                             double /*alpha*/)
{
    if (!hasAlgebraic(sMode)) {
        return;
    }
    const auto loc = offsets.getLocations(stateData, update, sMode, this);
    const auto result = evaluate(inputs, loc.diffStateLoc);
    loc.destLoc[0] = result.power[0];
    loc.destLoc[1] = result.power[1];
}

void EPCGEN::timestep(CoreTime time, const IOdata& inputs, const SolverMode& /*sMode*/)
{
    const double deltaTime = time - prevTime;
    if (deltaTime < 0.0) {
        throw InvalidParameterValue("EPCGEN timestep precedes current time");
    }
    auto* state = m_state.data() + 2;
    const auto result = evaluate(inputs, state);
    for (index_t index = 0; index < stateCount; ++index) {
        state[index] += deltaTime * result.rates[index];
    }
    const auto updated = evaluate(inputs, state);
    m_state[0] = updated.power[0];
    m_state[1] = updated.power[1];
    prevTime = time;
}

void EPCGEN::jacobianElements(const IOdata& inputs,
                              const StateData& stateData,
                              MatrixData<double>& matrixData,
                              const IOlocs& inputLocs,
                              const SolverMode& sMode)
{
    const auto loc = offsets.getLocations(stateData, sMode, this);
    std::array<double, stateCount> states{};
    std::copy_n(loc.diffStateLoc, stateCount, states.begin());
    const auto base = evaluate(inputs, states.data());
    const auto addColumn =
        [&](index_t column, const Evaluation& value, double step, const Evaluation& reference) {
            if (hasAlgebraic(sMode)) {
                matrixData.assignCheckCol(loc.algOffset,
                                          column,
                                          (value.power[0] - reference.power[0]) / step);
                matrixData.assignCheckCol(loc.algOffset + 1,
                                          column,
                                          (value.power[1] - reference.power[1]) / step);
            }
            if (hasDifferential(sMode)) {
                for (index_t row = 0; row < stateCount; ++row) {
                    matrixData.assignCheckCol(loc.diffOffset + row,
                                              column,
                                              (value.rates[row] - reference.rates[row]) / step);
                }
            }
        };
    if (hasAlgebraic(sMode)) {
        matrixData.assign(loc.algOffset, loc.algOffset, -1.0);
        matrixData.assign(loc.algOffset + 1, loc.algOffset + 1, -1.0);
    }
    if (hasDifferential(sMode)) {
        for (index_t index = 0; index < stateCount; ++index) {
            const double step = 1.0e-7 * std::max(1.0, std::abs(states[index]));
            states[index] += step;
            const auto shifted = evaluate(inputs, states.data());
            addColumn(loc.diffOffset + index, shifted, step, base);
            matrixData.assign(loc.diffOffset + index,
                              loc.diffOffset + index,
                              ((shifted.rates[index] - base.rates[index]) / step) - stateData.cj);
            states[index] -= step;
        }
    }
    for (std::size_t index = 0; index < inputs.size(); ++index) {
        if (index >= inputLocs.size() || inputLocs[index] == kNullLocation ||
            inputs[index] == kNullVal) {
            continue;
        }
        IOdata shifted = inputs;
        const double step = 1.0e-7 * std::max(1.0, std::abs(inputs[index]));
        shifted[index] += step;
        addColumn(inputLocs[index], evaluate(shifted, states.data()), step, base);
    }
}

IOdata EPCGEN::getOutputs(const IOdata& /*inputs*/,
                          const StateData& stateData,
                          const SolverMode& sMode) const
{
    const auto loc = offsets.getLocations(stateData, sMode, this);
    return {loc.algStateLoc[0], loc.algStateLoc[1]};
}

void EPCGEN::outputPartialDerivatives(const IOdata& /*inputs*/,
                                      const StateData& /*stateData*/,
                                      MatrixData<double>& matrixData,
                                      const SolverMode& sMode)
{
    const auto alg = offsets.getAlgOffset(sMode);
    matrixData.assign(0, alg, 1.0);
    matrixData.assign(1, alg + 1, 1.0);
}

void EPCGEN::rootTest(const IOdata& inputs,
                      const StateData& stateData,
                      double roots[],
                      const SolverMode& sMode)
{
    const auto loc = offsets.getLocations(stateData, sMode, this);
    const double current =
        std::hypot(loc.diffStateLoc[activeCurrent], loc.diffStateLoc[reactiveCurrent]);
    roots[offsets.getRootOffset(sMode)] = current - imax;
    roots[offsets.getRootOffset(sMode) + 1] =
        (inputs.empty() ? 1.0 : inputs[0]) - initialVoltageReference - voltageTripDelta;
}

void EPCGEN::rootTrigger(CoreTime /*time*/,
                         const IOdata& /*inputs*/,
                         const std::vector<int>& rootMask,
                         const SolverMode& /*sMode*/)
{
    if (!rootMask.empty()) {
        tripped = true;
    }
}

ChangeCode EPCGEN::rootCheck(const IOdata& inputs,
                             const StateData& stateData,
                             const SolverMode& sMode,
                             CheckLevel /*level*/)
{
    const auto loc = offsets.getLocations(stateData, sMode, this);
    const double current =
        std::hypot(loc.diffStateLoc[activeCurrent], loc.diffStateLoc[reactiveCurrent]);
    const bool limitExceeded = current > imax ||
        (!inputs.empty() && inputs[0] > initialVoltageReference + voltageTripDelta);
    if (limitExceeded && !tripped) {
        tripped = true;
        return ChangeCode::NON_STATE_CHANGE;
    }
    return ChangeCode::NO_CHANGE;
}

stringVec EPCGEN::localStateNames() const
{
    return {"QPI", "Vf", "Pgov", "Pll", "Iq", "Ip", "Ed", "Eq", "Pcorr", "Qcorr"};
}

}  // namespace griddyn
