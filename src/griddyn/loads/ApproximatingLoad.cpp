/*
 * Copyright (c) 2014-2026, Lawrence Livermore National Security
 * See the top-level NOTICE for additional details. All rights reserved.
 * SPDX-License-Identifier: BSD-3-Clause
 */

#include "ApproximatingLoad.h"

#include "../GridBus.h"
#include "core/CoreExceptions.h"
#include "core/CoreObjectTemplates.hpp"
#include "core/ObjectFactoryTemplates.hpp"
#include "gmlc/utilities/stringOps.h"
#include "gmlc/utilities/vectorOps.hpp"
#include "utilities/GlobalWorkQueue.hpp"
#include <cassert>
#include <cmath>
#include <complex>
#include <iostream>
#include <string>
#include <utility>
#include <vector>

using gmlc::utilities::convertToLowerCase;

// #define SGS_DEBUG
namespace griddyn::loads {
#define CONJUGATE 1

ApproximatingLoad::ApproximatingLoad(const std::string& objName): RampLoad(objName)
{
    enableUpdates();
}
ApproximatingLoad::~ApproximatingLoad() = default;

CoreObject* ApproximatingLoad::clone(CoreObject* obj) const
{
    auto* loadClone = cloneBase<ApproximatingLoad, RampLoad>(this, obj);
    if (loadClone == nullptr) {
        return obj;
    }
    loadClone->triggerBound = triggerBound;
    loadClone->spread = spread;

    loadClone->cDetail = cDetail;
    loadClone->dynCoupling = dynCoupling;
    loadClone->pFlowCoupling = pFlowCoupling;
    return loadClone;
}

void ApproximatingLoad::add(CoreObject* obj)
{
    if (dynamic_cast<GridLoad*>(obj) != nullptr) {
        if (subLoad != nullptr) {
            GridSecondary::remove(subLoad);
        }
        subLoad = static_cast<GridLoad*>(obj);
        addSubObject(subLoad);
    } else {
        throw(UnrecognizedObjectException(this));
    }
}

void ApproximatingLoad::pFlowObjectInitializeA(CoreTime time0, std::uint32_t flags)
{
    m_lastCallTime = time0;

    opFlags[PRE_EX_REQUESTED] = true;
    RampLoad::pFlowObjectInitializeA(time0, flags);
    updateA(time0);
}

void ApproximatingLoad::pFlowObjectInitializeB()
{
    updateB();
    RampLoad::pFlowObjectInitializeB();
}

void ApproximatingLoad::dynObjectInitializeA(CoreTime time0, std::uint32_t flags)
{
    switch (dynCoupling) {
        case CouplingMode::none:
            opFlags.reset(PRE_EX_REQUESTED);
            offsets.local().local.algRoots = 0;
            break;
        case CouplingMode::interval:
            opFlags.reset(PRE_EX_REQUESTED);

            break;
        case CouplingMode::trigger:
            opFlags.reset(PRE_EX_REQUESTED);
            offsets.local().local.algRoots = 1;
            break;

        case CouplingMode::full:
            opFlags.set(PRE_EX_REQUESTED);
            break;
    }
    if (opFlags[DUAL_MODE_FLAG]) {
    }
    RampLoad::dynObjectInitializeA(time0, flags);
}

void ApproximatingLoad::dynObjectInitializeB(const IOdata& /*inputs*/,
                                             const IOdata& /*desiredOutput*/,
                                             IOdata& /*fieldSet*/)
{
    if (opFlags[DUAL_MODE_FLAG]) {
    }
}

void ApproximatingLoad::timestep(CoreTime time, const IOdata& inputs, const SolverMode& sMode)
{
    const double voltage = inputs[VOLTAGE_IN_LOCATION];
    const double angle = inputs[ANGLE_IN_LOCATION];
    if (subLoad != nullptr) {
        subLoad->timestep(time, inputs, sMode);
    }

    if (cDetail == CouplingDetail::single) {
        run1ApproxA(time, inputs);
    } else if (cDetail == CouplingDetail::VDep) {
        run2ApproxA(time, inputs);
    } else {
        run3ApproxA(time, inputs);
    }
    Vprev = voltage;
    Thprev = angle;
    prevTime = time;
}

void ApproximatingLoad::updateA(CoreTime time)
{
    const double voltage = bus->getVoltage();
    const double angle = bus->getAngle();
    IOdata inputs(2);
    inputs[VOLTAGE_IN_LOCATION] = voltage;
    inputs[ANGLE_IN_LOCATION] = angle;

    if (subLoad != nullptr) {
        if (subLoad->currentTime() < time) {
            subLoad->timestep(time, inputs, cLocalSolverMode);
        }
    }

    if (cDetail == CouplingDetail::single) {
        run1ApproxA(time, inputs);
    } else if (cDetail == CouplingDetail::VDep) {
        run2ApproxA(time, inputs);
    } else {
        run3ApproxA(time, inputs);
    }
    Vprev = voltage;
    Thprev = angle;
    prevTime = time;
}

CoreTime ApproximatingLoad::updateB()
{
    switch (cDetail) {
        case CouplingDetail::single: {
            auto res = run1ApproxB();
            setP(res[0]);
            setQ(res[1]);
            if (res.size() == 4) {
                double diff = res[2] - res[0];
                dPdt = diff / updatePeriod;
                diff = res[3] - res[1];
                dQdt = diff / updatePeriod;
            }
        } break;

        case CouplingDetail::VDep: {
            auto loadValues = run2ApproxB();
            setP(loadValues[0]);
            setQ(loadValues[1]);
            setIp(loadValues[2]);
            setIq(loadValues[3]);
            if (loadValues.size() == 8) {
                double diff = loadValues[4] - loadValues[0];
                dPdt = diff / updatePeriod;
                diff = loadValues[5] - loadValues[1];
                dQdt = diff / updatePeriod;
                diff = loadValues[6] - loadValues[2];
                dIpdt = diff / updatePeriod;
                diff = loadValues[7] - loadValues[3];
                dIqdt = diff / updatePeriod;
            }
        } break;

        case CouplingDetail::triple: {
            auto loadValues = run3ApproxB();
            // printf("t=%f deltaP=%e deltaQ=%e deltaIr=%e deltaIq=%e deltaZr=%e deltaZq=%e\n",
            // prevTime, P - LV[0], Q
            // - LV[1], Ir - LV[2], Iq - LV[3], Yp - LV[4], Yq - LV[5]);

            setP(loadValues[0]);
            setQ(loadValues[1]);
            setIp(loadValues[2]);
            setIq(loadValues[3]);
            setup(loadValues[4]);
            setYq(loadValues[5]);

            if (loadValues.size() == 12) {
                double diff = loadValues[6] - loadValues[0];
                dPdt = diff / updatePeriod;
                diff = loadValues[7] - loadValues[1];
                dQdt = diff / updatePeriod;
                diff = loadValues[8] - loadValues[2];
                dIpdt = diff / updatePeriod;
                diff = loadValues[9] - loadValues[3];
                dIqdt = diff / updatePeriod;
                diff = loadValues[10] - loadValues[4];
                dYpdt = diff / updatePeriod;
                diff = loadValues[11] - loadValues[5];
                dYqdt = diff / updatePeriod;
            }
#ifdef SGS_DEBUG
            std::cout << "SGS : " << prevTime << " : " << name
                      << " ApproximatingLoad::updateB realPower = " << getRealPower()
                      << " reactive power = " << getReactivePower() << '\n';
#endif
        } break;

        default:
            assert(false);
    }
    lastTime = prevTime;
    if (prevTime >= nextUpdateTime) {
        nextUpdateTime += updatePeriod;
    }
    return nextUpdateTime;
}

void ApproximatingLoad::preEx(const IOdata& inputs,
                              const StateData& stateDataValue,
                              const SolverMode& sMode)
{
    if ((lastSeqID == stateDataValue.seqID) && (stateDataValue.seqID != 0)) {
        return;
    }
    lastSeqID = stateDataValue.seqID;
    const double voltage = inputs[VOLTAGE_IN_LOCATION];

    CouplingMode mode;
    if (!isDynamic(sMode)) {
        mode = pFlowCoupling;
    } else {
        mode = dynCoupling;
    }
    if (mode == CouplingMode::full) {
        if (cDetail == CouplingDetail::single) {
            run1ApproxA(stateDataValue.time, inputs);
        } else if (cDetail == CouplingDetail::VDep) {
            run2ApproxA(stateDataValue.time, inputs);
        } else {
            run3ApproxA(stateDataValue.time, inputs);
        }
    } else {
        if (cDetail == CouplingDetail::single) {
            if ((voltage > (Vprev + (0.5 * spread))) || (voltage < (Vprev - (0.5 * spread)))) {
                run1ApproxA(stateDataValue.time, inputs);
            }
        } else if (cDetail == CouplingDetail::VDep) {
            if ((voltage > (Vprev + spread)) || (voltage < (Vprev - spread))) {
                run2ApproxA(stateDataValue.time, inputs);
            }
        } else {
            if ((voltage > (Vprev + (1.5 * spread))) || (voltage < (Vprev - (1.5 * spread)))) {
                run3ApproxA(stateDataValue.time, inputs);
            }
        }
    }
}

void ApproximatingLoad::updateLocalCache(const IOdata& inputs,
                                         const StateData& stateDataValue,
                                         const SolverMode& sMode)
{
    if (opFlags[WAITING_FLAG]) {
        updateB();
    }
    RampLoad::updateLocalCache(inputs, stateDataValue, sMode);
}

std::vector<std::tuple<double, double, double>>
    ApproximatingLoad::getLoadValues(const std::vector<double>& inputs,
                                     const std::vector<double>& voltages)
{
    std::vector<std::tuple<double, double, double>> res;
    if (subLoad == nullptr) {
        return res;
    }

    IOdata cinputs(inputs.begin(), inputs.end());
    for (const auto& voltage : voltages) {
        cinputs[VOLTAGE_IN_LOCATION] = voltage;
        subLoad->updateLocalCache(cinputs, emptyStateData, cLocalSolverMode);
        auto realPowerSub = subLoad->getRealPower(cinputs, emptyStateData, cLocalSolverMode);
        auto reactivePowerSub =
            subLoad->getReactivePower(cinputs, emptyStateData, cLocalSolverMode);
        res.emplace_back(voltage, realPowerSub, reactivePowerSub);
    }
    return res;
}

void ApproximatingLoad::run1ApproxA(CoreTime /*time*/, const IOdata& inputs)
{
    using gmlc::containers::make_workBlock;
    assert(!opFlags[WAITING_FLAG]);  // this should not happen;

    // auto dt = time - m_lastCallTime;

    std::vector<double> voltages;
    voltages.push_back(inputs[VOLTAGE_IN_LOCATION]);
    const std::vector<double> inputBuffer(inputs.begin(), inputs.end());
    auto workBlock = make_workBlock(
        [inputBuffer, voltages, this]() { return getLoadValues(inputBuffer, voltages); });
    vres = workBlock->get_future();
    getGlobalWorkQueue()->addWorkBlock(std::move(workBlock));
    opFlags.set(WAITING_FLAG);
}

std::vector<double> ApproximatingLoad::run1ApproxB()
{
    auto res = vres.get();
    opFlags.reset(WAITING_FLAG);
    return {std::get<1>(res[0]), std::get<2>(res[0])};
}

void ApproximatingLoad::run2ApproxA(CoreTime /*time*/, const IOdata& inputs)
{
    using gmlc::containers::make_workBlock;
    assert(!opFlags[WAITING_FLAG]);  // this should not happen;

    // auto dt = time - m_lastCallTime;

    std::vector<double> voltages;
    const double voltage = inputs[VOLTAGE_IN_LOCATION];
    voltages.push_back(voltage);
    const double ratio1 = (voltage + spread) / voltage;
    voltages.push_back(voltage * ratio1);
    const std::vector<double> inputBuffer(inputs.begin(), inputs.end());
    auto workBlock = make_workBlock(
        [inputBuffer, voltages, this]() { return getLoadValues(inputBuffer, voltages); });
    vres = workBlock->get_future();
    getGlobalWorkQueue()->addWorkBlock(std::move(workBlock));
    opFlags.set(WAITING_FLAG);
}

std::vector<double> ApproximatingLoad::run2ApproxB()
{
    assert(opFlags[WAITING_FLAG]);  // this should not happen;
    auto res = vres.get();
    opFlags.reset(WAITING_FLAG);
    const double voltage1 = std::get<0>(res[0]);
    const double realPower1 = std::get<1>(res[0]);
    const double reactivePower1 = std::get<2>(res[0]);
    const double voltage2 = std::get<0>(res[1]);
    const double realPower2 = std::get<1>(res[1]);
    const double reactivePower2 = std::get<2>(res[1]);
    std::vector<double> retP(4);
    retP[2] = (realPower2 - realPower1) / (voltage2 - voltage1);
    retP[3] = (reactivePower2 - reactivePower1) / (voltage2 - voltage1);
    retP[0] = realPower1 - (voltage1 * retP[2]);
    retP[1] = reactivePower1 - (voltage1 * retP[3]);
    return retP;
}

void ApproximatingLoad::run3ApproxA(CoreTime /*time*/, const IOdata& inputs)
{
    using gmlc::containers::make_workBlock;
    assert(!opFlags[WAITING_FLAG]);  // this should not happen;

    // auto dt = time - m_lastCallTime;

    std::vector<double> voltages;
    const double voltage = inputs[VOLTAGE_IN_LOCATION];

    double ratio1 = (voltage - spread) / voltage;
    voltages.push_back(voltage * ratio1);
    ratio1 = (voltage + spread) / voltage;
    voltages.push_back(voltage * ratio1);
    voltages.push_back(voltage);
    const std::vector<double> inputBuffer(inputs.begin(), inputs.end());
    auto workBlock = make_workBlock(
        [inputBuffer, voltages, this]() { return getLoadValues(inputBuffer, voltages); });
    vres = workBlock->get_future();
    getGlobalWorkQueue()->addWorkBlock(std::move(workBlock));
    opFlags.set(WAITING_FLAG);
}

std::vector<double> ApproximatingLoad::run3ApproxB()
{
    assert(opFlags[WAITING_FLAG]);  // this should not happen;
    auto res = vres.get();
    opFlags.reset(WAITING_FLAG);
    const double voltage1 = std::get<0>(res[0]);
    const double realPower1 = std::get<1>(res[0]);
    const double reactivePower1 = std::get<2>(res[0]);
    const double voltage2 = std::get<0>(res[1]);
    const double realPower2 = std::get<1>(res[1]);
    const double reactivePower2 = std::get<2>(res[1]);
    const double voltage3 = std::get<0>(res[2]);
    const double realPower3 = std::get<1>(res[2]);
    const double reactivePower3 = std::get<2>(res[2]);

    std::vector<double> retP(6);
    double quadraticTerm;

    const double voltageSq1 = voltage1 * voltage1;
    const double voltageSq2 = voltage2 * voltage2;
    const double voltageSq3 = voltage3 * voltage3;
    // do a check for linearity

    /*
P = LV[0];
Q = LV[1];
Ip = LV[2];
Iq = LV[3];
Yp = LV[4];
Yq = LV[5];
*/

    double linearTerm1 = (realPower2 - realPower1) / (voltage2 - voltage1);
    double linearTerm2 = (realPower3 - realPower1) / (voltage3 - voltage1);
    if ((opFlags[LINEARIZE_TRIPLE]) ||
        (std::abs(linearTerm1 - linearTerm2) < 0.0001))  // we are pretty well linear here
    {
        retP[4] = 0;
        retP[0] = realPower1 - ((voltage1 * (linearTerm1 + linearTerm2)) / 2.0);
        retP[2] = (linearTerm1 + linearTerm2) / 2.0;
    } else {
        quadraticTerm = (((voltage2 - voltage1) * (realPower3 - realPower1)) +
                         ((voltage1 - voltage3) * (realPower2 - realPower1))) /
            (((voltage1 - voltage3) * (voltageSq2 - voltageSq1)) +
             ((voltageSq1 - voltageSq3) * (voltage1 - voltage2)));
        linearTerm2 = ((realPower2 - realPower1) + (voltageSq1 * quadraticTerm) -
                       (voltageSq2 * quadraticTerm)) /
            (voltage2 - voltage1);
        linearTerm1 = realPower1 - (voltage1 * linearTerm2) - (voltageSq1 * quadraticTerm);

        retP[0] = linearTerm1;
        retP[2] = linearTerm2;
        retP[4] = quadraticTerm;
    }

    linearTerm1 = (reactivePower2 - reactivePower1) / (voltage2 - voltage1);
    linearTerm2 = (reactivePower3 - reactivePower1) / (voltage3 - voltage1);
    if ((opFlags[LINEARIZE_TRIPLE]) ||
        (std::abs(linearTerm1 - linearTerm2) < 0.0001))  // we are pretty well linear here
    {
        retP[1] = reactivePower1 - ((voltage1 * (linearTerm1 + linearTerm2)) / 2.0);
        retP[3] = (linearTerm1 + linearTerm2) / 2.0;
        retP[5] = 0;
    } else {
        quadraticTerm = (((voltage2 - voltage1) * (reactivePower3 - reactivePower1)) +
                         ((voltage1 - voltage3) * (reactivePower2 - reactivePower1))) /
            (((voltage1 - voltage3) * (voltageSq2 - voltageSq1)) +
             ((voltageSq1 - voltageSq3) * (voltage1 - voltage2)));
        linearTerm2 = ((reactivePower2 - reactivePower1) + (voltageSq1 * quadraticTerm) -
                       (voltageSq2 * quadraticTerm)) /
            (voltage2 - voltage1);
        linearTerm1 = reactivePower1 - (voltage1 * linearTerm2) - (voltageSq1 * quadraticTerm);

        retP[1] = linearTerm1;
        retP[3] = linearTerm2;
        retP[5] = quadraticTerm;
    }

    return retP;
}

void ApproximatingLoad::set(std::string_view param, std::string_view val)
{
    if (param == "detail") {
        auto valueLower = convertToLowerCase(val);
        if ((valueLower == "triple") || (valueLower == "high") || (valueLower == "zip") ||
            (valueLower == "3")) {
            cDetail = CouplingDetail::triple;
        } else if ((valueLower == "lineartriple") || (valueLower == "linear3")) {
            cDetail = CouplingDetail::triple;
            opFlags.set(LINEARIZE_TRIPLE);
        } else if ((valueLower == "single") || (valueLower == "low") ||
                   (valueLower == "constant") || (valueLower == "1")) {
            cDetail = CouplingDetail::single;
        } else if ((valueLower == "double") || (valueLower == "vdep") || (valueLower == "linear") ||
                   (valueLower == "2")) {
            cDetail = CouplingDetail::VDep;
        }
    } else if ((param == "mode") || (param == "coupling") || (param == "dyncoupling")) {
        auto valueLower = convertToLowerCase(val);
        if (valueLower == "none") {
            dynCoupling = CouplingMode::none;
        } else if ((valueLower == "interval") || (valueLower == "periodic")) {
            dynCoupling = CouplingMode::interval;
        } else if (valueLower == "trigger") {
            dynCoupling = CouplingMode::trigger;
        } else if (valueLower == "full") {
            dynCoupling = CouplingMode::full;
        }
    } else if ((param == "pflow") || (param == "pflowcoupling")) {
        auto valueLower = convertToLowerCase(val);
        if (valueLower == "none") {
            pFlowCoupling = CouplingMode::none;
        } else if ((valueLower == "interval") || (valueLower == "periodic")) {
            pFlowCoupling = CouplingMode::interval;
        } else if (valueLower == "trigger") {
            pFlowCoupling = CouplingMode::trigger;
        } else if (valueLower == "full") {
            pFlowCoupling = CouplingMode::full;
        }
    } else {
        ZipLoad::set(param, val);
    }
}

void ApproximatingLoad::set(std::string_view param, double val, units::unit unitType)
{
    // TODO(phlpt): Convert some of these to a setFlags function.
    if ((param == "spread") || (param == "band")) {
        if (std::abs(val) > kMin_Res) {
            spread = val;
        } else {
            throw(InvalidParameterValue(param));
        }
    } else if ((param == "bounds") || (param == "usebounds")) {
        opFlags.set(USES_BOUNDS_FLAG, (val > 0));
    } else if ((param == "mult") || (param == "multiplier")) {
        m_mult = val;
    } else if (param == "detail") {
        if (val <= 1.5) {
            cDetail = CouplingDetail::single;
        } else if (val < 2.25) {
            cDetail = CouplingDetail::VDep;
        } else if (val < 2.75) {
            cDetail = CouplingDetail::triple;
            opFlags.set(LINEARIZE_TRIPLE);
        } else if (val >= 2.75) {
            cDetail = CouplingDetail::triple;
        }
    } else if ((param == "dual") || (param == "dualmode")) {
        opFlags.set(DUAL_MODE_FLAG, (val > 0.0));
    } else if (param == "lineartriple") {
        opFlags.set(LINEARIZE_TRIPLE, (val > 0.0));
    } else {
        ZipLoad::set(param, val, unitType);
    }
}

// return D[0]=dP/dV D[1]=dP/dtheta,D[2]=dQ/dV,D[3]=dQ/dtheta

void ApproximatingLoad::rootTest(const IOdata& inputs,
                                 const StateData& /*sD*/,
                                 double roots[],
                                 const SolverMode& sMode)
{
    const int rootOffset = offsets.getRootOffset(sMode);
    const double voltage = inputs[VOLTAGE_IN_LOCATION];
    roots[rootOffset] = (spread * triggerBound) - std::abs(voltage - Vprev);

    // printf("time=%f root =%12.10f\n", time,roots[rootOffset]);
}

void ApproximatingLoad::rootTrigger(CoreTime time,
                                    const IOdata& /*inputs*/,
                                    const std::vector<int>& rootMask,
                                    const SolverMode& sMode)
{
    const int rootOffset = offsets.getRootOffset(sMode);
    if (rootMask[rootOffset] != 0) {
        updateA(time);
        updateB();
    }
}

ChangeCode ApproximatingLoad::rootCheck(const IOdata& inputs,
                                        const StateData& stateDataValue,
                                        const SolverMode& /*sMode*/,
                                        CheckLevel /*level*/)
{
    const double voltage = inputs[VOLTAGE_IN_LOCATION];
    if (std::abs(voltage - Vprev) > (spread * triggerBound)) {
        updateA((stateDataValue.empty()) ? (stateDataValue.time) : prevTime);
        updateB();
        return ChangeCode::PARAMETER_CHANGE;
    }
    return ChangeCode::NO_CHANGE;
}

}  // namespace griddyn::loads
