/*
 * Copyright (c) 2014-2026, Lawrence Livermore National Security
 * See the top-level NOTICE for additional details. All rights reserved.
 * SPDX-License-Identifier: BSD-3-Clause
 */

#include "approximatingLoad.h"

#include "../gridBus.h"
#include "core/coreExceptions.h"
#include "core/coreObjectTemplates.hpp"
#include "core/objectFactoryTemplates.hpp"
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

approximatingLoad::approximatingLoad(const std::string& objName): rampLoad(objName)
{
    enable_updates();
}
approximatingLoad::~approximatingLoad() = default;

CoreObject* approximatingLoad::clone(CoreObject* obj) const
{
    auto ld = cloneBase<approximatingLoad, rampLoad>(this, obj);
    if (ld == nullptr) {
        return obj;
    }
    ld->triggerBound = triggerBound;
    ld->spread = spread;

    ld->cDetail = cDetail;
    ld->dynCoupling = dynCoupling;
    ld->pFlowCoupling = pFlowCoupling;
    return ld;
}

void approximatingLoad::add(CoreObject* obj)
{
    if (dynamic_cast<Load*>(obj) != nullptr) {
        if (subLoad != nullptr) {
            gridSecondary::remove(subLoad);
        }
        subLoad = static_cast<Load*>(obj);
        addSubObject(subLoad);
    } else {
        throw(unrecognizedObjectException(this));
    }
}

void approximatingLoad::pFlowObjectInitializeA(coreTime time0, std::uint32_t flags)
{
    m_lastCallTime = time0;

    opFlags[preEx_requested] = true;
    rampLoad::pFlowObjectInitializeA(time0, flags);
    updateA(time0);
}

void approximatingLoad::pFlowObjectInitializeB()
{
    updateB();
    rampLoad::pFlowObjectInitializeB();
}

void approximatingLoad::dynObjectInitializeA(coreTime time0, std::uint32_t flags)
{
    switch (dynCoupling) {
        case coupling_mode_t::none:
            opFlags.reset(preEx_requested);
            offsets.local().local.algRoots = 0;
            break;
        case coupling_mode_t::interval:
            opFlags.reset(preEx_requested);

            break;
        case coupling_mode_t::trigger:
            opFlags.reset(preEx_requested);
            offsets.local().local.algRoots = 1;
            break;

        case coupling_mode_t::full:
            opFlags.set(preEx_requested);
            break;
    }
    if (opFlags[dual_mode_flag]) {
    }
    rampLoad::dynObjectInitializeA(time0, flags);
}

void approximatingLoad::dynObjectInitializeB(const IOdata& /*inputs*/,
                                             const IOdata& /*desiredOutput*/,
                                             IOdata& /*fieldSet*/)
{
    if (opFlags[dual_mode_flag]) {
    }
}

void approximatingLoad::timestep(coreTime time, const IOdata& inputs, const solverMode& sMode)
{
    double V = inputs[voltageInLocation];
    double th = inputs[angleInLocation];
    if (subLoad != nullptr) {
        subLoad->timestep(time, inputs, sMode);
    }

    if (cDetail == coupling_detail_t::single) {
        run1ApproxA(time, inputs);
    } else if (cDetail == coupling_detail_t::VDep) {
        run2ApproxA(time, inputs);
    } else {
        run3ApproxA(time, inputs);
    }
    Vprev = V;
    Thprev = th;
    prevTime = time;
}

void approximatingLoad::updateA(coreTime time)
{
    double V = bus->getVoltage();
    double th = bus->getAngle();
    IOdata inputs(2);
    inputs[voltageInLocation] = V;
    inputs[angleInLocation] = th;

    if (subLoad != nullptr) {
        if (subLoad->currentTime() < time) {
            subLoad->timestep(time, inputs, cLocalSolverMode);
        }
    }

    if (cDetail == coupling_detail_t::single) {
        run1ApproxA(time, inputs);
    } else if (cDetail == coupling_detail_t::VDep) {
        run2ApproxA(time, inputs);
    } else {
        run3ApproxA(time, inputs);
    }
    Vprev = V;
    Thprev = th;
    prevTime = time;
}

coreTime approximatingLoad::updateB()
{
    switch (cDetail) {
        case coupling_detail_t::single: {
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

        case coupling_detail_t::VDep: {
            auto LV = run2ApproxB();
            setP(LV[0]);
            setQ(LV[1]);
            setIp(LV[2]);
            setIq(LV[3]);
            if (LV.size() == 8) {
                double diff = LV[4] - LV[0];
                dPdt = diff / updatePeriod;
                diff = LV[5] - LV[1];
                dQdt = diff / updatePeriod;
                diff = LV[6] - LV[2];
                dIpdt = diff / updatePeriod;
                diff = LV[7] - LV[3];
                dIqdt = diff / updatePeriod;
            }
        } break;

        case coupling_detail_t::triple: {
            auto LV = run3ApproxB();
            // printf("t=%f deltaP=%e deltaQ=%e deltaIr=%e deltaIq=%e deltaZr=%e deltaZq=%e\n",
            // prevTime, P - LV[0], Q
            // - LV[1], Ir - LV[2], Iq - LV[3], Yp - LV[4], Yq - LV[5]);

            setP(LV[0]);
            setQ(LV[1]);
            setIp(LV[2]);
            setIq(LV[3]);
            setup(LV[4]);
            setYq(LV[5]);

            if (LV.size() == 12) {
                double diff = LV[6] - LV[0];
                dPdt = diff / updatePeriod;
                diff = LV[7] - LV[1];
                dQdt = diff / updatePeriod;
                diff = LV[8] - LV[2];
                dIpdt = diff / updatePeriod;
                diff = LV[9] - LV[3];
                dIqdt = diff / updatePeriod;
                diff = LV[10] - LV[4];
                dYpdt = diff / updatePeriod;
                diff = LV[11] - LV[5];
                dYqdt = diff / updatePeriod;
            }
#ifdef SGS_DEBUG
            std::cout << "SGS : " << prevTime << " : " << name
                      << " approximatingLoad::updateB realPower = " << getRealPower()
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

void approximatingLoad::preEx(const IOdata& inputs, const stateData& sD, const solverMode& sMode)
{
    if ((lastSeqID == sD.seqID) && (sD.seqID != 0)) {
        return;
    }
    lastSeqID = sD.seqID;
    double V = inputs[voltageInLocation];

    coupling_mode_t mode;
    if (!isDynamic(sMode)) {
        mode = pFlowCoupling;
    } else {
        mode = dynCoupling;
    }
    if (mode == coupling_mode_t::full) {
        if (cDetail == coupling_detail_t::single) {
            run1ApproxA(sD.time, inputs);
        } else if (cDetail == coupling_detail_t::VDep) {
            run2ApproxA(sD.time, inputs);
        } else {
            run3ApproxA(sD.time, inputs);
        }
    } else {
        if (cDetail == coupling_detail_t::single) {
            if ((V > Vprev + 0.5 * spread) || (V < Vprev - 0.5 * spread)) {
                run1ApproxA(sD.time, inputs);
            }
        } else if (cDetail == coupling_detail_t::VDep) {
            if ((V > Vprev + spread) || (V < Vprev - spread)) {
                run2ApproxA(sD.time, inputs);
            }
        } else {
            if ((V > Vprev + 1.5 * spread) || (V < Vprev - 1.5 * spread)) {
                run3ApproxA(sD.time, inputs);
            }
        }
    }
}

void approximatingLoad::updateLocalCache(const IOdata& inputs,
                                         const stateData& sD,
                                         const solverMode& sMode)
{
    if (opFlags[waiting_flag]) {
        updateB();
    }
    rampLoad::updateLocalCache(inputs, sD, sMode);
}

std::vector<std::tuple<double, double, double>>
    approximatingLoad::getLoadValues(const std::vector<double>& inputs,
                                     const std::vector<double>& voltages)
{
    std::vector<std::tuple<double, double, double>> res;
    if (subLoad == nullptr) {
        return res;
    }

    IOdata cinputs(inputs.begin(), inputs.end());
    for (auto& V : voltages) {
        cinputs[voltageInLocation] = V;
        subLoad->updateLocalCache(cinputs, emptyStateData, cLocalSolverMode);
        auto Psub = subLoad->getRealPower(cinputs, emptyStateData, cLocalSolverMode);
        auto Qsub = subLoad->getReactivePower(cinputs, emptyStateData, cLocalSolverMode);
        res.emplace_back(V, Psub, Qsub);
    }
    return res;
}

void approximatingLoad::run1ApproxA(coreTime /*time*/, const IOdata& inputs)
{
    using gmlc::containers::make_workBlock;
    assert(!opFlags[waiting_flag]);  // this should not happen;

    // auto dt = time - m_lastCallTime;

    std::vector<double> voltages;
    voltages.push_back(inputs[voltageInLocation]);
    std::vector<double> inputb(inputs.begin(), inputs.end());
    auto wb =
        make_workBlock([inputb, voltages, this]() { return getLoadValues(inputb, voltages); });
    vres = wb->get_future();
    getGlobalWorkQueue()->addWorkBlock(std::move(wb));
    opFlags.set(waiting_flag);
}

std::vector<double> approximatingLoad::run1ApproxB()
{
    auto res = vres.get();
    opFlags.reset(waiting_flag);
    return {std::get<1>(res[0]), std::get<2>(res[0])};
}

void approximatingLoad::run2ApproxA(coreTime /*time*/, const IOdata& inputs)
{
    using gmlc::containers::make_workBlock;
    assert(!opFlags[waiting_flag]);  // this should not happen;

    // auto dt = time - m_lastCallTime;

    std::vector<double> voltages;
    double V = inputs[voltageInLocation];
    voltages.push_back(V);
    double r1 = (V + spread) / V;
    voltages.push_back(V * r1);
    std::vector<double> inputb(inputs.begin(), inputs.end());
    auto wb =
        make_workBlock([inputb, voltages, this]() { return getLoadValues(inputb, voltages); });
    vres = wb->get_future();
    getGlobalWorkQueue()->addWorkBlock(std::move(wb));
    opFlags.set(waiting_flag);
}

std::vector<double> approximatingLoad::run2ApproxB()
{
    assert(opFlags[waiting_flag]);  // this should not happen;
    auto res = vres.get();
    opFlags.reset(waiting_flag);
    double V1 = std::get<0>(res[0]);
    double P1 = std::get<1>(res[0]);
    double Q1 = std::get<2>(res[0]);
    double V2 = std::get<0>(res[1]);
    double P2 = std::get<1>(res[1]);
    double Q2 = std::get<2>(res[1]);
    std::vector<double> retP(4);
    retP[2] = (P2 - P1) / (V2 - V1);
    retP[3] = (Q2 - Q1) / (V2 - V1);
    retP[0] = P1 - V1 * retP[2];
    retP[1] = Q1 - V1 * retP[3];
    return retP;
}

void approximatingLoad::run3ApproxA(coreTime /*time*/, const IOdata& inputs)
{
    using gmlc::containers::make_workBlock;
    assert(!opFlags[waiting_flag]);  // this should not happen;

    // auto dt = time - m_lastCallTime;

    std::vector<double> voltages;
    double V = inputs[voltageInLocation];

    double r1 = (V - spread) / V;
    voltages.push_back(V * r1);
    r1 = (V + spread) / V;
    voltages.push_back(V * r1);
    voltages.push_back(V);
    std::vector<double> inputb(inputs.begin(), inputs.end());
    auto wb =
        make_workBlock([inputb, voltages, this]() { return getLoadValues(inputb, voltages); });
    vres = wb->get_future();
    getGlobalWorkQueue()->addWorkBlock(std::move(wb));
    opFlags.set(waiting_flag);
}

std::vector<double> approximatingLoad::run3ApproxB()
{
    assert(opFlags[waiting_flag]);  // this should not happen;
    auto res = vres.get();
    opFlags.reset(waiting_flag);
    double V1 = std::get<0>(res[0]);
    double P1 = std::get<1>(res[0]);
    double Q1 = std::get<2>(res[0]);
    double V2 = std::get<0>(res[1]);
    double P2 = std::get<1>(res[1]);
    double Q2 = std::get<2>(res[1]);
    double V3 = std::get<0>(res[2]);
    double P3 = std::get<1>(res[2]);
    double Q3 = std::get<2>(res[2]);
#if 0
  a1 = P1 / ((V1 - V2) * (V1 - V3));
  double a2 = P2 / ((V2 - V1) * (V2 - V3));
  double a3 = P3 / ((V3 - V1) * (V3 - V2));
  double A, B, C;
  A = a1 + a2 + a3;
  B = (a1 * (V2 + V3) + a2 * (V1 + V3) + a3 * (V1 + V2));
  C = a1 * V2 * V3 + a2 * V1 * V3 + a3 * V1 * V2;

  std::vector<double> retP (6);

  retP[0] = C;
  retP[2] = B;
  retP[4] = A;

  a1 = Q1 / ((V1 - V2) * (V1 - V3));
  a2 = Q2 / ((V2 - V1) * (V2 - V3));
  a3 = Q3 / ((V3 - V1) * (V3 - V2));

  A = a1 + a2 + a3;
  B = (a1 * (V2 + V3) + a2 * (V1 + V3) + a3 * (V1 + V2));
  C = a1 * V2 * V3 + a2 * V1 * V3 + a3 * V1 * V2;
  retP[1] = C;
  retP[3] = B;
  retP[5] = A;
#else

    std::vector<double> retP(6);
    double X3;

    double b1 = V1 * V1;
    double b2 = V2 * V2;
    double b3 = V3 * V3;
    // do a check for linearity

    /*
P = LV[0];
Q = LV[1];
Ip = LV[2];
Iq = LV[3];
Yp = LV[4];
Yq = LV[5];
*/

    double X1 = (P2 - P1) / (V2 - V1);
    double X2 = (P3 - P1) / (V3 - V1);
    if ((opFlags[linearize_triple]) ||
        (std::abs(X1 - X2) < 0.0001))  // we are pretty well linear here
    {
        retP[4] = 0;
        retP[0] = P1 - V1 * (X1 + X2) / 2;
        retP[2] = (X1 + X2) / 2.0;
    } else {
        X3 = ((V2 - V1) * (P3 - P1) + (V1 - V3) * (P2 - P1)) /
            ((V1 - V3) * (b2 - b1) + (b1 - b3) * (V1 - V2));
        X2 = (P2 - P1 + b1 * X3 - b2 * X3) / (V2 - V1);
        X1 = P1 - V1 * X2 - b1 * X3;

        retP[0] = X1;
        retP[2] = X2;
        retP[4] = X3;
    }

    X1 = (Q2 - Q1) / (V2 - V1);
    X2 = (Q3 - Q1) / (V3 - V1);
    if ((opFlags[linearize_triple]) ||
        (std::abs(X1 - X2) < 0.0001))  // we are pretty well linear here
    {
        retP[1] = Q1 - V1 * (X1 + X2) / 2;
        retP[3] = (X1 + X2) / 2.0;
        retP[5] = 0;
    } else {
        X3 = ((V2 - V1) * (Q3 - Q1) + (V1 - V3) * (Q2 - Q1)) /
            ((V1 - V3) * (b2 - b1) + (b1 - b3) * (V1 - V2));
        X2 = (Q2 - Q1 + b1 * X3 - b2 * X3) / (V2 - V1);
        X1 = Q1 - V1 * X2 - b1 * X3;

        retP[1] = X1;
        retP[3] = X2;
        retP[5] = X3;
    }
#endif

    return retP;
}

void approximatingLoad::set(std::string_view param, std::string_view val)
{
    if (param == "detail") {
        auto v2 = convertToLowerCase(val);
        if ((v2 == "triple") || (v2 == "high") || (v2 == "zip") || (v2 == "3")) {
            cDetail = coupling_detail_t::triple;
        } else if ((v2 == "lineartriple") || (v2 == "linear3")) {
            cDetail = coupling_detail_t::triple;
            opFlags.set(linearize_triple);
        } else if ((v2 == "single") || (v2 == "low") || (v2 == "constant") || (v2 == "1")) {
            cDetail = coupling_detail_t::single;
        } else if ((v2 == "double") || (v2 == "vdep") || (v2 == "linear") || (v2 == "2")) {
            cDetail = coupling_detail_t::VDep;
        }
    } else if ((param == "mode") || (param == "coupling") || (param == "dyncoupling")) {
        auto v2 = convertToLowerCase(val);
        if (v2 == "none") {
            dynCoupling = coupling_mode_t::none;
        } else if ((v2 == "interval") || (v2 == "periodic")) {
            dynCoupling = coupling_mode_t::interval;
        } else if (v2 == "trigger") {
            dynCoupling = coupling_mode_t::trigger;
        } else if (v2 == "full") {
            dynCoupling = coupling_mode_t::full;
        }
    } else if ((param == "pflow") || (param == "pflowcoupling")) {
        auto v2 = convertToLowerCase(val);
        if (v2 == "none") {
            pFlowCoupling = coupling_mode_t::none;
        } else if ((v2 == "interval") || (v2 == "periodic")) {
            pFlowCoupling = coupling_mode_t::interval;
        } else if (v2 == "trigger") {
            pFlowCoupling = coupling_mode_t::trigger;
        } else if (v2 == "full") {
            pFlowCoupling = coupling_mode_t::full;
        }
    } else {
        zipLoad::set(param, val);
    }
}

void approximatingLoad::set(std::string_view param, double val, units::unit unitType)
{
    // TODO(phlpt): Convert some of these to a setFlags function.
    if ((param == "spread") || (param == "band")) {
        if (std::abs(val) > kMin_Res) {
            spread = val;
        } else {
            throw(invalidParameterValue(param));
        }
    } else if ((param == "bounds") || (param == "usebounds")) {
        opFlags.set(uses_bounds_flag, (val > 0));
    } else if ((param == "mult") || (param == "multiplier")) {
        m_mult = val;
    } else if (param == "detail") {
        if (val <= 1.5) {
            cDetail = coupling_detail_t::single;
        } else if (val < 2.25) {
            cDetail = coupling_detail_t::VDep;
        } else if (val < 2.75) {
            cDetail = coupling_detail_t::triple;
            opFlags.set(linearize_triple);
        } else if (val >= 2.75) {
            cDetail = coupling_detail_t::triple;
        }
    } else if ((param == "dual") || (param == "dualmode")) {
        opFlags.set(dual_mode_flag, (val > 0.0));
    } else if (param == "lineartriple") {
        opFlags.set(linearize_triple, (val > 0.0));
    } else {
        zipLoad::set(param, val, unitType);
    }
}

// return D[0]=dP/dV D[1]=dP/dtheta,D[2]=dQ/dV,D[3]=dQ/dtheta

void approximatingLoad::rootTest(const IOdata& inputs,
                                 const stateData& /*sD*/,
                                 double roots[],
                                 const solverMode& sMode)
{
    int rootOffset = offsets.getRootOffset(sMode);
    double V = inputs[voltageInLocation];
    roots[rootOffset] = spread * triggerBound - std::abs(V - Vprev);

    // printf("time=%f root =%12.10f\n", time,roots[rootOffset]);
}

void approximatingLoad::rootTrigger(coreTime time,
                                    const IOdata& /*inputs*/,
                                    const std::vector<int>& rootMask,
                                    const solverMode& sMode)
{
    int rootOffset = offsets.getRootOffset(sMode);
    if (rootMask[rootOffset] != 0) {
        updateA(time);
        updateB();
    }
}

change_code approximatingLoad::rootCheck(const IOdata& inputs,
                                         const stateData& sD,
                                         const solverMode& /*sMode*/,
                                         check_level_t /*level*/)
{
    double V = inputs[voltageInLocation];
    if (std::abs(V - Vprev) > spread * triggerBound) {
        updateA((sD.empty()) ? (sD.time) : prevTime);
        updateB();
        return change_code::parameter_change;
    }
    return change_code::no_change;
}

}  // namespace griddyn::loads

