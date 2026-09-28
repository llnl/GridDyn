/*
 * Copyright (c) 2014-2026, Lawrence Livermore National Security
 * See the top-level NOTICE for additional details. All rights reserved.
 * SPDX-License-Identifier: BSD-3-Clause
 */
#include "BusMeasurementSensor.h"

#include "../GridArea.h"
#include "../GridBus.h"
#include "../Link.h"
#include "../generators/DynamicGenerator.h"
#include "../links/AcLine.h"
#include "core/CoreExceptions.h"
#include "core/CoreObjectTemplates.hpp"
#include "gmlc/utilities/stringOps.h"
#include "utilities/MatrixData.hpp"
#include <array>
#include <cmath>
#include <functional>
#include <numbers>
#include <string>
#include <utility>

namespace griddyn {
namespace {
    constexpr double twoPi = 2.0 * std::numbers::pi;
    void requirePositive(double value, const char* name)
    {
        if (!std::isfinite(value) || value <= 0.0) {
            throw InvalidParameterValue(std::string{name} + " must be positive and finite");
        }
    }
}  // namespace

BusMeasurementSensor::BusMeasurementSensor(const std::string& name): Sensor(name) {}

CoreObject* BusMeasurementSensor::clone(CoreObject* obj) const
{
    // This base is abstract; concrete subclasses provide the object to populate.
    auto* result =
        obj == nullptr ? nullptr : dynamic_cast<BusMeasurementSensor*>(Sensor::clone(obj));
    if (result != nullptr) {
        result->stateNames = stateNames;
    }
    return result == nullptr ? obj : result;
}

GridBus* BusMeasurementSensor::bus() const
{
    auto* result = dynamic_cast<GridBus*>(m_sourceObject);
    if (result == nullptr) {
        throw InvalidParameterValue("bus measurement sensor requires a bus source");
    }
    return result;
}

double BusMeasurementSensor::angle(const StateData& stateData, const SolverMode& sMode) const
{
    return bus()->getAngle(stateData, sMode);
}

double BusMeasurementSensor::voltage(const StateData& stateData, const SolverMode& sMode) const
{
    return bus()->getVoltage(stateData, sMode);
}

void BusMeasurementSensor::defineStates(count_t alg, count_t diff, stringVec names)
{
    auto& local = offsets.local().local;
    local.algSize = alg;
    local.diffSize = diff;
    local.jacSize = (alg + diff) * 6;
    stateNames = std::move(names);
    m_state.assign(alg + diff, 0.0);
    m_dstate_dt.assign(alg + diff, 0.0);
}

void BusMeasurementSensor::dynObjectInitializeA(CoreTime time0, std::uint32_t flags)
{
    bus();
    if (!filterBlocks.empty() || !inputStrings.empty() || !dataSources.empty()) {
        throw InvalidParameterValue(
            "bus measurement sensor does not accept generic filter or input blocks");
    }
    Sensor::dynObjectInitializeA(time0, flags);
}

void BusMeasurementSensor::dynObjectInitializeB(const IOdata&, const IOdata&, IOdata& fieldSet)
{
    fieldSet.resize(m_outputSize);
    for (index_t index = 0; index < m_outputSize; ++index) {
        fieldSet[index] = getOutput(index);
    }
}

double BusMeasurementSensor::getOutput(const IOdata&,
                                       const StateData& stateData,
                                       const SolverMode& sMode,
                                       index_t outNum) const
{
    const auto stateIndex = outputState(outNum);
    if (stateIndex == kNullLocation || stateIndex >= m_state.size()) {
        return kNullVal;
    }
    if (stateData.state == nullptr || isLocal(sMode)) {
        return m_state[stateIndex];
    }
    const auto algebraicCount = offsets.local().local.algSize;
    if (stateIndex < algebraicCount) {
        if (hasAlgebraic(sMode)) {
            return stateData.state[offsets.getAlgOffset(sMode) + stateIndex];
        }
        if (stateData.algState != nullptr && sMode.pairedOffsetIndex != kNullLocation) {
            const auto& paired = offsets.getSolverMode(sMode.pairedOffsetIndex);
            return stateData.algState[offsets.getAlgOffset(paired) + stateIndex];
        }
    } else if (hasDifferential(sMode)) {
        return stateData.state[offsets.getDiffOffset(sMode) + stateIndex - algebraicCount];
    }
    return m_state[stateIndex];
}

double BusMeasurementSensor::getOutput(index_t outNum) const
{
    const auto index = outputState(outNum);
    return index == kNullLocation || index >= m_state.size() ? kNullVal : m_state[index];
}

index_t BusMeasurementSensor::getOutputLoc(const SolverMode& sMode, index_t outNum) const
{
    const auto index = outputState(outNum);
    if (index == kNullLocation) {
        return kNullLocation;
    }
    const auto algebraicCount = offsets.local().local.algSize;
    if (index < algebraicCount) {
        return hasAlgebraic(sMode) ? offsets.getAlgOffset(sMode) + index : kNullLocation;
    }
    return hasDifferential(sMode) ? offsets.getDiffOffset(sMode) + index - algebraicCount :
                                    kNullLocation;
}

void BusMeasurementSensor::outputPartialDerivatives(const IOdata&,
                                                    const StateData&,
                                                    MatrixData<double>& matrixData,
                                                    const SolverMode& sMode)
{
    for (index_t index = 0; index < m_outputSize; ++index) {
        matrixData.assignCheckCol(index, getOutputLoc(sMode, index), 1.0);
    }
}

stringVec BusMeasurementSensor::localStateNames() const
{
    return stateNames;
}

PLLSensor::PLLSensor(const std::string& name, bool voltagePhase):
    BusMeasurementSensor(name), phaseDetector(voltagePhase)
{
    m_outputSize = 2;
    outputStrings = {{"angle", "am"}, {"frequency_deviation", "df"}};
}

CoreObject* PLLSensor::clone(CoreObject* obj) const
{
    auto* result = cloneBase<PLLSensor, BusMeasurementSensor>(this, obj);
    if (result != nullptr) {
        result->phaseDetector = phaseDetector;
        result->Kp = Kp;
        result->Ki = Ki;
        result->Tf = Tf;
        result->Tp = Tp;
        result->fn = fn;
    }
    return result == nullptr ? obj : result;
}

CoreObject* PLL1Sensor::clone(CoreObject* obj) const
{
    return cloneBase<PLL1Sensor, PLLSensor>(this, obj);
}

CoreObject* PLL2Sensor::clone(CoreObject* obj) const
{
    return cloneBase<PLL2Sensor, PLLSensor>(this, obj);
}

void PLLSensor::set(std::string_view param, double value, units::unit unitType)
{
    const auto key = gmlc::utilities::convertToLowerCase(std::string{param});
    if (key == "kp") {
        Kp = value;
    } else if (key == "ki") {
        Ki = value;
    } else if (key == "tf") {
        Tf = value;
    } else if (key == "tp") {
        Tp = value;
    } else if (key == "fn") {
        fn = value;
    } else {
        BusMeasurementSensor::set(param, value, unitType);
    }
}

double PLLSensor::get(std::string_view param, units::unit unitType) const
{
    const auto key = gmlc::utilities::convertToLowerCase(std::string{param});
    if (key == "kp") {
        return Kp;
    }
    if (key == "ki") {
        return Ki;
    }
    if (key == "tf") {
        return Tf;
    }
    if (key == "tp") {
        return Tp;
    }
    if (key == "fn") {
        return fn;
    }
    return BusMeasurementSensor::get(param, unitType);
}

void PLLSensor::dynObjectInitializeA(CoreTime time0, std::uint32_t flags)
{
    requirePositive(fn, "PLL fn");
    if (!std::isfinite(Kp) || !std::isfinite(Ki) || Kp < 0.0 || Ki < 0.0) {
        throw InvalidParameterValue("PLL gains must be finite and nonnegative");
    }
    if (!phaseDetector) {
        requirePositive(Tf, "PLL1 Tf");
        requirePositive(Tp, "PLL1 Tp");
    }
    BusMeasurementSensor::dynObjectInitializeA(time0, flags);
    defineStates(1,
                 phaseDetector ? 2 : 4,
                 phaseDetector ? stringVec{"df", "pi_integral", "am"} :
                                 stringVec{"df", "filtered_angle", "pi_integral", "ae", "am"});
}

void PLLSensor::dynObjectInitializeB(const IOdata& inputs,
                                     const IOdata& desiredOutput,
                                     IOdata& fieldSet)
{
    const double a = bus()->getAngle();
    if (phaseDetector) {
        m_state[2] = a;
    } else {
        m_state[1] = a;
        m_state[3] = a;
        m_state[4] = a;
    }
    BusMeasurementSensor::dynObjectInitializeB(inputs, desiredOutput, fieldSet);
}

index_t PLLSensor::outputState(index_t outNum) const
{
    if (outNum == 0) {
        return phaseDetector ? 2 : 4;
    }
    if (outNum == 1) {
        return 0;
    }
    return kNullLocation;
}

void PLLSensor::derivative(const IOdata&,
                           const StateData& stateData,
                           double deriv[],
                           const SolverMode& sMode)
{
    if (!hasDifferential(sMode)) {
        return;
    }
    const auto loc = offsets.getLocations(stateData, deriv, sMode, this);
    const auto* x = loc.diffStateLoc;
    const double a = angle(stateData, sMode);
    if (phaseDetector) {
        const double e = voltage(stateData, sMode) * std::sin(a - x[1]);
        const double df = Kp * e + x[0];
        loc.destDiffLoc[0] = Ki * e;
        loc.destDiffLoc[1] = twoPi * fn * df;
    } else {
        const double e = x[0] - x[3];
        const double df = Kp * e + x[1];
        loc.destDiffLoc[0] = (a - x[0]) / Tf;
        loc.destDiffLoc[1] = Ki * e;
        loc.destDiffLoc[2] = twoPi * fn * df;
        loc.destDiffLoc[3] = (x[2] - x[3]) / Tp;
    }
}

void PLLSensor::residual(const IOdata& inputs,
                         const StateData& stateData,
                         double resid[],
                         const SolverMode& sMode)
{
    const auto loc = offsets.getLocations(stateData, resid, sMode, this);
    const auto* x = loc.diffStateLoc;
    const double e = phaseDetector ?
        voltage(stateData, sMode) * std::sin(angle(stateData, sMode) - x[1]) :
        x[0] - x[3];
    const double df = Kp * e + x[phaseDetector ? 0 : 1];
    if (hasAlgebraic(sMode)) {
        loc.destLoc[0] = df - loc.algStateLoc[0];
    }
    if (hasDifferential(sMode)) {
        derivative(inputs, stateData, resid, sMode);
        for (index_t index = 0; index < (phaseDetector ? 2 : 4); ++index) {
            loc.destDiffLoc[index] -= loc.dstateLoc[index];
        }
    }
}

void PLLSensor::algebraicUpdate(const IOdata&,
                                const StateData& stateData,
                                double update[],
                                const SolverMode& sMode,
                                double)
{
    if (!hasAlgebraic(sMode)) {
        return;
    }
    const auto loc = offsets.getLocations(stateData, update, sMode, this);
    const auto* x = loc.diffStateLoc;
    const double e = phaseDetector ?
        voltage(stateData, sMode) * std::sin(angle(stateData, sMode) - x[1]) :
        x[0] - x[3];
    loc.destLoc[0] = Kp * e + x[phaseDetector ? 0 : 1];
}

void PLLSensor::jacobianElements(const IOdata&,
                                 const StateData& stateData,
                                 MatrixData<double>& matrixData,
                                 const IOlocs&,
                                 const SolverMode& sMode)
{
    const auto loc = offsets.getLocations(stateData, sMode, this);
    const auto d = loc.diffOffset;
    const auto a = loc.algOffset;
    const auto* x = loc.diffStateLoc;
    const double phase = phaseDetector ? angle(stateData, sMode) - x[1] : 0.0;
    const double dErrAm = phaseDetector ? -voltage(stateData, sMode) * std::cos(phase) : -1.0;
    const double dErrFiltered = phaseDetector ? 0.0 : 1.0;
    const auto busAngleLoc = bus()->getOutputLoc(sMode, ANGLE_IN_LOCATION);
    if (hasAlgebraic(sMode)) {
        matrixData.assign(a, a, -1.0);
        if (!isAlgebraicOnly(sMode)) {
            matrixData.assign(a, d + (phaseDetector ? 0 : 1), 1.0);
            matrixData.assign(a, d + (phaseDetector ? 1 : 3), Kp * dErrAm);
            if (!phaseDetector) {
                matrixData.assign(a, d, Kp);
            }
        }
    }
    if (!hasDifferential(sMode)) {
        return;
    }
    const double cj = stateData.cj;
    if (phaseDetector) {
        const double v = voltage(stateData, sMode);
        matrixData.assign(d, d, -cj);
        matrixData.assign(d, d + 1, Ki * dErrAm);
        matrixData.assign(d + 1, d, twoPi * fn);
        matrixData.assign(d + 1, d + 1, twoPi * fn * Kp * dErrAm - cj);
        matrixData.assignCheckCol(d, busAngleLoc, Ki * v * std::cos(phase));
        matrixData.assignCheckCol(d + 1, busAngleLoc, twoPi * fn * Kp * v * std::cos(phase));
        const auto vloc = bus()->getOutputLoc(sMode, VOLTAGE_IN_LOCATION);
        matrixData.assignCheckCol(d, vloc, Ki * std::sin(phase));
        matrixData.assignCheckCol(d + 1, vloc, twoPi * fn * Kp * std::sin(phase));
    } else {
        matrixData.assign(d, d, -1.0 / Tf - cj);
        matrixData.assignCheckCol(d, busAngleLoc, 1.0 / Tf);
        matrixData.assign(d + 1, d, Ki * dErrFiltered);
        matrixData.assign(d + 1, d + 1, -cj);
        matrixData.assign(d + 1, d + 3, -Ki);
        matrixData.assign(d + 2, d, twoPi * fn * Kp);
        matrixData.assign(d + 2, d + 1, twoPi * fn);
        matrixData.assign(d + 2, d + 2, -cj);
        matrixData.assign(d + 2, d + 3, twoPi * fn * Kp * dErrAm);
        matrixData.assign(d + 3, d + 2, 1.0 / Tp);
        matrixData.assign(d + 3, d + 3, -1.0 / Tp - cj);
    }
}

void PLLSensor::timestep(CoreTime time, const IOdata&, const SolverMode& sMode)
{
    const double dt = time - prevTime;
    if (dt < 0.0) {
        throw InvalidParameterValue("PLL timestep precedes current time");
    }
    // Explicit integration is only used by the sampled path; the DAE path uses derivative().
    const double a = bus()->getAngle();
    const double v = bus()->getVoltage();
    if (phaseDetector) {
        const double e = v * std::sin(a - m_state[2]);
        const double df = Kp * e + m_state[1];
        m_state[1] += dt * Ki * e;
        m_state[2] += dt * twoPi * fn * df;
        m_state[0] = Kp * v * std::sin(a - m_state[2]) + m_state[1];
    } else {
        const double e = m_state[1] - m_state[4];
        const double df = Kp * e + m_state[2];
        const double filtered = (a - m_state[1]) / Tf;
        const double integral = Ki * e;
        const double estimated = twoPi * fn * df;
        const double measured = (m_state[3] - m_state[4]) / Tp;
        m_state[1] += dt * filtered;
        m_state[2] += dt * integral;
        m_state[3] += dt * estimated;
        m_state[4] += dt * measured;
        m_state[0] = Kp * (m_state[1] - m_state[4]) + m_state[2];
    }
    prevTime = time;
    Relay::timestep(time, {}, sMode);
}

BusROCOFSensor::BusROCOFSensor(const std::string& name): BusMeasurementSensor(name)
{
    m_outputSize = 2;
    outputStrings = {{"df", "frequency_deviation"}, {"dfdt", "rocof"}};
}

CoreObject* BusROCOFSensor::clone(CoreObject* obj) const
{
    auto* result = cloneBase<BusROCOFSensor, BusMeasurementSensor>(this, obj);
    if (result != nullptr) {
        result->Tf = Tf;
        result->Tw = Tw;
        result->Tr = Tr;
        result->fn = fn;
        result->initialAngle = initialAngle;
        result->lastAngle = lastAngle;
    }
    return result == nullptr ? obj : result;
}

void BusROCOFSensor::set(std::string_view param, double value, units::unit unitType)
{
    const auto key = gmlc::utilities::convertToLowerCase(std::string{param});
    if (key == "tf") {
        Tf = value;
    } else if (key == "tw") {
        Tw = value;
    } else if (key == "tr") {
        Tr = value;
    } else if (key == "fn") {
        fn = value;
    } else {
        BusMeasurementSensor::set(param, value, unitType);
    }
}

double BusROCOFSensor::get(std::string_view param, units::unit unitType) const
{
    const auto key = gmlc::utilities::convertToLowerCase(std::string{param});
    if (key == "tf") {
        return Tf;
    }
    if (key == "tw") {
        return Tw;
    }
    if (key == "tr") {
        return Tr;
    }
    if (key == "fn") {
        return fn;
    }
    return BusMeasurementSensor::get(param, unitType);
}

void BusROCOFSensor::dynObjectInitializeA(CoreTime time0, std::uint32_t flags)
{
    requirePositive(Tf, "BUSROCOF Tf");
    requirePositive(Tw, "BUSROCOF Tw");
    requirePositive(Tr, "BUSROCOF Tr");
    requirePositive(fn, "BUSROCOF fn");
    BusMeasurementSensor::dynObjectInitializeA(time0, flags);
    defineStates(2, 3, {"df", "dfdt", "angle_lag", "angle_washout", "frequency_washout"});
}

void BusROCOFSensor::dynObjectInitializeB(const IOdata& inputs,
                                          const IOdata& desiredOutput,
                                          IOdata& fieldSet)
{
    initialAngle = lastAngle = bus()->getAngle();
    m_state[4] = 1.0;
    BusMeasurementSensor::dynObjectInitializeB(inputs, desiredOutput, fieldSet);
}

index_t BusROCOFSensor::outputState(index_t outNum) const
{
    return outNum < 2 ? outNum : kNullLocation;
}

double BusROCOFSensor::deviation(const double diff[]) const
{
    return (diff[0] - diff[1]) / (twoPi * fn * Tw);
}

void BusROCOFSensor::derivative(const IOdata&,
                                const StateData& stateData,
                                double deriv[],
                                const SolverMode& sMode)
{
    if (!hasDifferential(sMode)) {
        return;
    }
    const auto loc = offsets.getLocations(stateData, deriv, sMode, this);
    const auto* x = loc.diffStateLoc;
    loc.destDiffLoc[0] = (angle(stateData, sMode) - initialAngle - x[0]) / Tf;
    loc.destDiffLoc[1] = (x[0] - x[1]) / Tw;
    loc.destDiffLoc[2] = (1.0 + deviation(x) - x[2]) / Tr;
}

void BusROCOFSensor::residual(const IOdata& inputs,
                              const StateData& stateData,
                              double resid[],
                              const SolverMode& sMode)
{
    const auto loc = offsets.getLocations(stateData, resid, sMode, this);
    const auto* x = loc.diffStateLoc;
    const double df = deviation(x);
    if (hasAlgebraic(sMode)) {
        loc.destLoc[0] = df - loc.algStateLoc[0];
        loc.destLoc[1] = (1.0 + df - x[2]) / Tr - loc.algStateLoc[1];
    }
    if (hasDifferential(sMode)) {
        derivative(inputs, stateData, resid, sMode);
        for (index_t index = 0; index < 3; ++index) {
            loc.destDiffLoc[index] -= loc.dstateLoc[index];
        }
    }
}

void BusROCOFSensor::algebraicUpdate(const IOdata&,
                                     const StateData& stateData,
                                     double update[],
                                     const SolverMode& sMode,
                                     double)
{
    if (!hasAlgebraic(sMode)) {
        return;
    }
    const auto loc = offsets.getLocations(stateData, update, sMode, this);
    const double df = deviation(loc.diffStateLoc);
    loc.destLoc[0] = df;
    loc.destLoc[1] = (1.0 + df - loc.diffStateLoc[2]) / Tr;
}

void BusROCOFSensor::jacobianElements(const IOdata&,
                                      const StateData& stateData,
                                      MatrixData<double>& matrixData,
                                      const IOlocs&,
                                      const SolverMode& sMode)
{
    const auto loc = offsets.getLocations(stateData, sMode, this);
    const auto a = loc.algOffset;
    const auto d = loc.diffOffset;
    const double gain = 1.0 / (twoPi * fn * Tw);
    if (hasAlgebraic(sMode)) {
        matrixData.assign(a, a, -1.0);
        matrixData.assign(a + 1, a + 1, -1.0);
        if (!isAlgebraicOnly(sMode)) {
            matrixData.assign(a, d, gain);
            matrixData.assign(a, d + 1, -gain);
            matrixData.assign(a + 1, d, gain / Tr);
            matrixData.assign(a + 1, d + 1, -gain / Tr);
            matrixData.assign(a + 1, d + 2, -1.0 / Tr);
        }
    }
    if (!hasDifferential(sMode)) {
        return;
    }
    matrixData.assign(d, d, -1.0 / Tf - stateData.cj);
    matrixData.assignCheckCol(d, bus()->getOutputLoc(sMode, ANGLE_IN_LOCATION), 1.0 / Tf);
    matrixData.assign(d + 1, d, 1.0 / Tw);
    matrixData.assign(d + 1, d + 1, -1.0 / Tw - stateData.cj);
    matrixData.assign(d + 2, d, gain / Tr);
    matrixData.assign(d + 2, d + 1, -gain / Tr);
    matrixData.assign(d + 2, d + 2, -1.0 / Tr - stateData.cj);
}

void BusROCOFSensor::timestep(CoreTime time, const IOdata&, const SolverMode& sMode)
{
    const double dt = time - prevTime;
    if (dt < 0.0) {
        throw InvalidParameterValue("BUSROCOF timestep precedes current time");
    }
    const std::array<double, 3> old{m_state[2], m_state[3], m_state[4]};
    const double now = bus()->getAngle();
    const auto rates = [&](double a, const std::array<double, 3>& x) {
        return std::array<double, 3>{(a - initialAngle - x[0]) / Tf,
                                     (x[0] - x[1]) / Tw,
                                     (1.0 + deviation(x.data()) - x[2]) / Tr};
    };
    const auto stage = [&](const std::array<double, 3>& k, double scale) {
        return std::array<double, 3>{old[0] + scale * dt * k[0],
                                     old[1] + scale * dt * k[1],
                                     old[2] + scale * dt * k[2]};
    };
    const auto k1 = rates(lastAngle, old);
    const auto k2 = rates((lastAngle + now) / 2.0, stage(k1, 0.5));
    const auto k3 = rates((lastAngle + now) / 2.0, stage(k2, 0.5));
    const auto k4 = rates(now, stage(k3, 1.0));
    for (index_t index = 0; index < 3; ++index) {
        m_state[2 + index] =
            old[index] + dt * (k1[index] + 2.0 * k2[index] + 2.0 * k3[index] + k4[index]) / 6.0;
    }
    m_state[0] = deviation(m_state.data() + 2);
    m_state[1] = (1.0 + m_state[0] - m_state[4]) / Tr;
    lastAngle = now;
    prevTime = time;
    Relay::timestep(time, {}, sMode);
}

FreqDivSensor::FreqDivSensor(const std::string& name): BusMeasurementSensor(name)
{
    m_outputSize = 1;
    outputStrings = {{"frequency", "f"}};
}

CoreObject* FreqDivSensor::clone(CoreObject* obj) const
{
    auto* result = cloneBase<FreqDivSensor, BusMeasurementSensor>(this, obj);
    if (result != nullptr) {
        // Network pointers are rebuilt against the cloned area during initialization.
        result->neighbors.clear();
        result->machines.clear();
        result->diagonal = 0.0;
    }
    return result == nullptr ? obj : result;
}

void FreqDivSensor::dynObjectInitializeA(CoreTime time0, std::uint32_t flags)
{
    BusMeasurementSensor::dynObjectInitializeA(time0, flags);
    defineStates(1, 0, {"frequency"});
    diagonal = 0.0;
    neighbors.clear();
    machines.clear();
    auto* owner = dynamic_cast<GridArea*>(getParent());
    if (owner == nullptr) {
        throw InvalidParameterValue("FreqDiv requires an owning area");
    }
    auto* root = owner;
    while (auto* parentArea = dynamic_cast<GridArea*>(root->getParent())) {
        root = parentArea;
    }
    const auto findNeighbor = [&](GridBus* target) {
        FreqDivSensor* match = nullptr;
        std::function<void(GridArea*)> visit = [&](GridArea* area) {
            for (index_t relayIndex = 0; auto* relay = area->getRelay(relayIndex); ++relayIndex) {
                auto* candidate = dynamic_cast<FreqDivSensor*>(relay);
                if (candidate != nullptr && candidate->sourceBus() == target &&
                    candidate->isEnabled()) {
                    if (match != nullptr) {
                        throw InvalidParameterValue("FreqDiv has duplicate measurements on a bus");
                    }
                    match = candidate;
                }
            }
            for (index_t areaIndex = 0; auto* child = area->getArea(areaIndex); ++areaIndex) {
                visit(child);
            }
        };
        visit(root);
        return match;
    };
    auto* source = bus();
    for (index_t index = 0; auto* link = source->getLink(index); ++index) {
        auto* line = dynamic_cast<AcLine*>(link);
        if (line == nullptr) {
            throw InvalidParameterValue(
                "FreqDiv requires AC line admittance for every connected link");
        }
        const double tap = line->get("tap");
        const double shift = line->get("tapangle");
        if (!std::isfinite(tap) || std::abs(tap - 1.0) > 1e-12 || !std::isfinite(shift) ||
            std::abs(shift) > 1e-12) {
            throw InvalidParameterValue(
                "FreqDiv transformer taps and phase shifts are unsupported");
        }
        const double r = line->get("r");
        const double x = line->get("x");
        const double denom = r * r + x * x;
        if (!std::isfinite(denom) || denom <= 0.0) {
            throw InvalidParameterValue("FreqDiv requires finite nonzero line impedance");
        }
        const bool first = line->getBus(1) == source;
        auto* other = line->getBus(first ? 2 : 1);
        if (other == nullptr) {
            throw InvalidParameterValue("FreqDiv line has no remote bus");
        }
        auto* neighbor = findNeighbor(other);
        if (neighbor == nullptr) {
            throw InvalidParameterValue(
                "FreqDiv requires a sensor for every adjacent bus in its area");
        }
        const double b = -x / denom;
        const double self = b + line->get(first ? "b1" : "b2");
        diagonal += self;
        neighbors.push_back({neighbor, link, first});
    }
    for (index_t index = 0; auto* generator = source->getGen(index); ++index) {
        if (!generator->isEnabled()) {
            continue;
        }
        auto* dynamic = dynamic_cast<DynamicGenerator*>(generator);
        if (dynamic == nullptr) {
            continue;
        }
        auto* model = dynamic->find("genmodel");
        if (model == nullptr) {
            throw InvalidParameterValue("FreqDiv generator has no dynamic machine model");
        }
        const double xdpp = model->get("xdpp");
        const double xqpp = model->get("xqpp");
        double xg = (std::isfinite(xdpp) && xdpp > 0.0 && std::isfinite(xqpp) && xqpp > 0.0) ?
            (xdpp + xqpp) / 2.0 :
            model->get("xdp");
        if (!std::isfinite(xg) || xg <= 0.0) {
            xg = model->get("xs");
        }
        if (!std::isfinite(xg) || xg <= 0.0) {
            throw InvalidParameterValue("FreqDiv generator has no supported reactance");
        }
        const double mbase = dynamic->get("mbase", units::MVAR);
        requirePositive(mbase, "FreqDiv generator MBASE");
        const double gain = mbase / (systemBasePower * xg);
        diagonal -= gain;
        machines.push_back({dynamic, gain});
    }
    if (!std::isfinite(diagonal) || std::abs(diagonal) < 1e-12) {
        throw InvalidParameterValue("FreqDiv has a zero frequency equation diagonal");
    }
}

double FreqDivSensor::effectiveDiagonal() const
{
    double value = 0.0;
    for (const auto& neighbor : neighbors) {
        if (neighbor.link->isConnected()) {
            value += lineCoefficients(neighbor).first;
        }
    }
    for (const auto& machine : machines) {
        if (machine.generator->isEnabled()) {
            value -= machine.coefficient;
        }
    }
    return value;
}

std::pair<double, double> FreqDivSensor::lineCoefficients(const Neighbor& neighbor) const
{
    const auto* line = static_cast<AcLine*>(neighbor.link);
    if (std::abs(line->get("tap") - 1.0) > 1e-12 || std::abs(line->get("tapangle")) > 1e-12) {
        throw InvalidParameterValue("FreqDiv transformer taps and phase shifts are unsupported");
    }
    const double r = line->get("r");
    const double x = line->get("x");
    const double denom = r * r + x * x;
    if (!std::isfinite(denom) || denom <= 0.0) {
        throw InvalidParameterValue("FreqDiv requires finite nonzero line impedance");
    }
    const double b = -x / denom;
    return {b + line->get(neighbor.firstTerminal ? "b1" : "b2"), -b};
}

void FreqDivSensor::dynObjectInitializeB(const IOdata& inputs,
                                         const IOdata& desiredOutput,
                                         IOdata& fieldSet)
{
    m_state[0] = 1.0;
    BusMeasurementSensor::dynObjectInitializeB(inputs, desiredOutput, fieldSet);
}

index_t FreqDivSensor::outputState(index_t outNum) const
{
    return outNum == 0 ? 0 : kNullLocation;
}

double FreqDivSensor::frequencyResidual(const StateData& stateData,
                                        const SolverMode& sMode,
                                        double ownFrequency) const
{
    double result = effectiveDiagonal() * (ownFrequency - 1.0);
    for (const auto& neighbor : neighbors) {
        if (neighbor.link->isConnected()) {
            result += lineCoefficients(neighbor).second *
                (neighbor.sensor->getOutput({}, stateData, sMode) - 1.0);
        }
    }
    for (const auto& machine : machines) {
        if (machine.generator->isEnabled()) {
            result += machine.coefficient * (machine.generator->getFreq(stateData, sMode) - 1.0);
        }
    }
    return result;
}

void FreqDivSensor::residual(const IOdata&,
                             const StateData& stateData,
                             double resid[],
                             const SolverMode& sMode)
{
    if (!hasAlgebraic(sMode)) {
        return;
    }
    const auto loc = offsets.getLocations(stateData, resid, sMode, this);
    loc.destLoc[0] = frequencyResidual(stateData, sMode, loc.algStateLoc[0]);
}

void FreqDivSensor::algebraicUpdate(const IOdata&,
                                    const StateData& stateData,
                                    double update[],
                                    const SolverMode& sMode,
                                    double)
{
    if (!hasAlgebraic(sMode)) {
        return;
    }
    const auto loc = offsets.getLocations(stateData, update, sMode, this);
    const double currentDiagonal = effectiveDiagonal();
    if (std::abs(currentDiagonal) < 1e-12) {
        throw InvalidParameterValue("FreqDiv lost its frequency equation reference");
    }
    loc.destLoc[0] = 1.0 - frequencyResidual(stateData, sMode, 1.0) / currentDiagonal;
}

void FreqDivSensor::jacobianElements(const IOdata&,
                                     const StateData& stateData,
                                     MatrixData<double>& matrixData,
                                     const IOlocs&,
                                     const SolverMode& sMode)
{
    if (!hasAlgebraic(sMode)) {
        return;
    }
    const auto row = offsets.getAlgOffset(sMode);
    matrixData.assign(row, row, effectiveDiagonal());
    for (const auto& neighbor : neighbors) {
        if (neighbor.link->isConnected()) {
            matrixData.assignCheckCol(row,
                                      neighbor.sensor->getOutputLoc(sMode, 0),
                                      lineCoefficients(neighbor).second);
        }
    }
    if (hasDifferential(sMode)) {
        for (const auto& machine : machines) {
            if (machine.generator->isEnabled()) {
                index_t column = kNullLocation;
                machine.generator->getFreq(stateData, sMode, &column);
                matrixData.assignCheckCol(row, column, machine.coefficient);
            }
        }
    }
}

}  // namespace griddyn
