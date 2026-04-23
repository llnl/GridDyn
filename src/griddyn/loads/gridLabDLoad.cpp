/*
 * Copyright (c) 2014-2026, Lawrence Livermore National Security
 * See the top-level NOTICE for additional details. All rights reserved.
 * SPDX-License-Identifier: BSD-3-Clause
 */

#include "gridLabDLoad.h"

#include "../gridBus.h"
#include "core/coreExceptions.h"
#include "core/coreObjectTemplates.hpp"
#include "core/objectFactoryTemplates.hpp"
#include "coupling/GhostSwingBusManager.h"
#include "gmlc/utilities/stringConversion.h"
#include "gmlc/utilities/stringOps.h"
#include "gmlc/utilities/vectorOps.hpp"
#include <cassert>
#include <cmath>
#include <iostream>
#include <memory>
#include <string>
#include <vector>

// #define SGS_DEBUG
namespace griddyn::loads {
static typeFactory<gridLabDLoad> gfgld("load", stringVec{"gridlabd", "gridlab"});
// constants for rotating a complex number by +120 and -120 degrees
static const std::complex<double> rotp120(-0.5, sqrt(3.0) / 2.0);
static const std::complex<double> rotn120(-0.5, -sqrt(3.0) / 2.0);

#define CONJUGATE 1

gridLabDLoad::gridLabDLoad(const std::string& objName): rampLoad(objName)
{
    enable_updates();
}
gridLabDLoad::~gridLabDLoad() = default;

void gridLabDLoad::gridLabDInitialize()
{
    if (!opFlags[file_sent_flag]) {
        auto gsm = GhostSwingBusManager::instance();
        if (gsm) {
            task_id.resize(gridlabDfile.size());
            if (opFlags[dual_mode_flag]) {
                forward_task_id.resize(gridlabDfile.size());
            }
            for (size_t kk = 0; kk < gridlabDfile.size(); ++kk) {
                task_id[kk] = gsm->createGridlabDInstance("--workdir " + workdir[kk] + " " +
                                                          gridlabDfile[kk]);
                if (opFlags[dual_mode_flag]) {
                    forward_task_id[kk] = gsm->createGridlabDInstance("--workdir " + workdir[kk] +
                                                                      " " + gridlabDfile[kk]);
                }
            }

            opFlags.set(file_sent_flag);
#ifndef GRIDDYN_ENABLE_MPI
            for (size_t kk = 0; kk < gridlabDfile.size(); ++kk) {
                if (!(dummy_load[kk])) {
                    dummy_load[kk] = std::make_unique<zipLoad>(0.3, 0.1, "dummy");
                    dummy_load[kk]->set("yp", 0.15);
                    dummy_load[kk]->set("yq", 0.192);
                    dummy_load[kk]->set("ip", 0.22);
                    dummy_load[kk]->set("iq", -0.087);
                }
                gsm->setDummyLoadFunction(task_id[kk],
                                          [=, this](VoltageMessage* vm, CurrentMessage* cm) {
                                              run_dummy_load(static_cast<index_t>(kk), vm, cm);
                                          });
                if (opFlags[dual_mode_flag]) {
                    dummy_load_forward[kk] =
                        std::unique_ptr<zipLoad>(static_cast<zipLoad*>(dummy_load[kk]->clone()));
                }
            }
#endif
        }
    }
}

coreObject* gridLabDLoad::clone(coreObject* obj) const
{
    auto* ld = cloneBase<gridLabDLoad, rampLoad>(this, obj);
    if (ld == nullptr) {
        return obj;
    }
    ld->gridlabDfile = gridlabDfile;
    ld->workdir = workdir;
    ld->triggerBound = triggerBound;
    ld->spread = spread;
    ld->task_id.resize(task_id.size());
    ld->forward_task_id.resize(task_id.size());

    ld->dummy_load.resize(dummy_load.size());
    ld->dummy_load_forward.resize(dummy_load.size());
    for (size_t kk = 0; kk < dummy_load.size(); ++kk) {
        if (!(ld->dummy_load[kk])) {
            if (dummy_load[kk]) {
                ld->dummy_load[kk].reset(static_cast<zipLoad*>(dummy_load[kk]->clone(nullptr)));
                ld->dummy_load[kk]->setParent(ld);
                if (opFlags[dual_mode_flag]) {
                    ld->dummy_load_forward[kk].reset(
                        static_cast<zipLoad*>(dummy_load[kk]->clone()));
                    ld->dummy_load_forward[kk]->setParent(ld);
                }
            }
        } else if (dummy_load[kk]) {
            dummy_load[kk]->clone(ld->dummy_load[kk].get());
            ld->dummy_load[kk]->setParent(ld);
            if (opFlags[dual_mode_flag]) {
                dummy_load_forward[kk]->clone(ld->dummy_load_forward[kk].get());
                ld->dummy_load_forward[kk]->setParent(ld);
            }
        }
    }
    ld->cDetail = cDetail;
    ld->dynCoupling = dynCoupling;
    ld->pFlowCoupling = pFlowCoupling;
    return ld;
}

void gridLabDLoad::add(coreObject* obj)
{
    if (dynamic_cast<Load*>(obj) != nullptr) {
        for (size_t kk = 0; kk < dummy_load.size(); ++kk) {
            if (dummy_load[kk]) {
                continue;
            }
            dummy_load[kk].reset(static_cast<Load*>(obj));
            obj->setParent(this);
            if (opFlags[dual_mode_flag]) {
                dummy_load_forward[kk].reset(static_cast<Load*>(obj->clone()));
                dummy_load_forward[kk]->setParent(this);
            }
        }
        if (obj->isRoot()) {
            dummy_load.push_back(std::unique_ptr<Load>(static_cast<Load*>(obj)));
            obj->setParent(this);
            if (opFlags[dual_mode_flag]) {
                dummy_load_forward.push_back(
                    std::unique_ptr<Load>(static_cast<Load*>(obj->clone())));
                dummy_load_forward.back()->setParent(this);
            }
        }
    } else {
        throw(unrecognizedObjectException(this));
    }
}

void gridLabDLoad::pFlowObjectInitializeA(coreTime time0, std::uint32_t flags)
{
    if (!opFlags[file_sent_flag]) {
        gridLabDInitialize();
    }
    m_lastCallTime = time0;
    updateA(time0);
    opFlags[preEx_requested] = true;
    rampLoad::pFlowObjectInitializeA(time0, flags);
}

void gridLabDLoad::pFlowObjectInitializeB()
{
    updateB();
}
void gridLabDLoad::dynObjectInitializeA(coreTime time0, std::uint32_t flags)
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

void gridLabDLoad::dynObjectInitializeB(const IOdata& /*inputs*/,
                                        const IOdata& /*desiredOutput*/,
                                        IOdata& /*fieldSet*/)
{
    if (opFlags[dual_mode_flag]) {
    }
}

void gridLabDLoad::timestep(coreTime time, const IOdata& inputs, const solverMode& sMode)
{
    double V = inputs[voltageInLocation];
    double th = inputs[angleInLocation];
#ifndef GRIDDYN_ENABLE_MPI
    // if we have a dummy load progress it in time appropriately
    for (auto& dl : dummy_load) {
        if (dl) {
            if (dl->currentTime() < time) {
                dl->timestep(time, inputs, sMode);
            }
        }
    }

    for (auto& dl : dummy_load_forward) {
        if (dl) {
            if (dl->currentTime() < time) {
                dl->timestep(time, inputs, sMode);
            }
        }
    }
#else
    (void)sMode;
#endif

    if (cDetail == coupling_detail_t::single) {
        runGridLabA(time, inputs);
    } else if (cDetail == coupling_detail_t::VDep) {
        run2GridLabA(time, inputs);
    } else {
        run3GridLabA(time, inputs);
    }
    Vprev = V;
    Thprev = th;
    prevTime = time;
}

void gridLabDLoad::updateA(coreTime time)
{
    double V = bus->getVoltage();
    double th = bus->getAngle();
    IOdata inputs(2);
    inputs[voltageInLocation] = V;
    inputs[angleInLocation] = th;
#ifndef GRIDDYN_ENABLE_MPI
    // if we have a dummy load progress it in time appropriately
    for (auto& dl : dummy_load) {
        if (dl) {
            if (dl->currentTime() < time) {
                dl->timestep(time, inputs, cLocalSolverMode);
            }
        }
    }

    for (auto& dl : dummy_load_forward) {
        if (dl) {
            if (dl->currentTime() < time) {
                dl->timestep(time, inputs, cLocalSolverMode);
            }
        }
    }
#endif

    if (cDetail == coupling_detail_t::single) {
        runGridLabA(time, inputs);
    } else if (cDetail == coupling_detail_t::VDep) {
        run2GridLabA(time, inputs);
    } else {
        run3GridLabA(time, inputs);
    }
    Vprev = V;
    Thprev = th;
    prevTime = time;
}

coreTime gridLabDLoad::updateB()
{
    switch (cDetail) {
        case coupling_detail_t::single: {
            auto res = runGridLabB(false);
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
            auto LV = run2GridLabB(false);
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
            auto LV = run3GridLabB(false);
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
                      << " gridLabDLoad::updateB realPower = " << getRealPower()
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

void gridLabDLoad::preEx(const IOdata& inputs, const stateData& sD, const solverMode& sMode)
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
            runGridLabA(sD.time, inputs);
        } else if (cDetail == coupling_detail_t::VDep) {
            run2GridLabA(sD.time, inputs);
        } else {
            run3GridLabA(sD.time, inputs);
        }
    } else {
        if (cDetail == coupling_detail_t::single) {
            if ((V > Vprev + 0.5 * spread) || (V < Vprev - 0.5 * spread)) {
                runGridLabA(sD.time, inputs);
            }
        } else if (cDetail == coupling_detail_t::VDep) {
            if ((V > Vprev + spread) || (V < Vprev - spread)) {
                run2GridLabA(sD.time, inputs);
            }
        } else {
            if ((V > Vprev + 1.5 * spread) || (V < Vprev - 1.5 * spread)) {
                run3GridLabA(sD.time, inputs);
            }
        }
    }
}

void gridLabDLoad::updateLocalCache(const IOdata& inputs,
                                    const stateData& sD,
                                    const solverMode& sMode)
{
    if (opFlags[waiting_flag]) {
        updateB();
    }
    rampLoad::updateLocalCache(inputs, sD, sMode);
}

void gridLabDLoad::runGridLabA(coreTime time, const IOdata& inputs)
{
    assert(!opFlags[waiting_flag]);  // this should not happen;
    LOG_TRACE("calling gridlab load 1A");
    GhostSwingBusManager::cvec Vg(3);

    auto dt = time - m_lastCallTime;
    unsigned int tInt = 0;
    // get the right timer interval
    if (dt > (timeOneSecond)) {
        tInt = static_cast<int>(dt.seconds());
        m_lastCallTime += static_cast<double>(tInt);
    }

    // define the A values
    Vprev = inputs[voltageInLocation];
    Thprev = inputs[angleInLocation];
    // static double *res=new double[8];
    double V1 = inputs[voltageInLocation] * localBaseVoltage *
        1000.0;  // localBaseVoltage is in KV  we ignore the angle since it shouldn't matter
    Vg[0] = std::complex<double>(V1, 0);
    Vg[1] = Vg[0] * rotn120;
    Vg[2] = Vg[0] * rotp120;

    auto gsm = GhostSwingBusManager::instance();
    if (gsm) {
        for (size_t kk = 0; kk < task_id.size(); ++kk) {
            gsm->sendVoltageStep(task_id[kk], Vg, tInt);
            if (opFlags[dual_mode_flag]) {
                if (opFlags[dyn_initialized]) {
                    (void)kk;  // ignore for loop conversion suggestion
                    // GhostSwingBusManager::instance ()->sendVoltageStep (forward_task_id[kk],
                    // Vg, tInt);
                }
            }
        }
        opFlags.set(waiting_flag);
    }
}

std::vector<double> gridLabDLoad::runGridLabB(bool unbalancedAlert)
{
    LOG_TRACE("calling gridlab-d load 1B");
    assert(opFlags[waiting_flag]);  // this should not happen;
    GhostSwingBusManager::cvec Ig(3);
    GhostSwingBusManager::cvec Ig2(3);
    GhostSwingBusManager::cvec Vg(3);

    Vg[0] = std::complex<double>(Vprev * localBaseVoltage * 1000.0, 0);
    Vg[1] = Vg[0] * rotn120;
    Vg[2] = Vg[0] * rotp120;

    // get the current in a blocking call;
    auto gsm = GhostSwingBusManager::instance();
    if (gsm) {
        int ii = 0;
        for (auto& tid : task_id) {
            gsm->getCurrent(tid, Ig2);
            if (ii == 0) {
                Ig = Ig2;
                ii = 1;
            } else {
                for (size_t pp = 0; pp < Ig2.size(); pp += 3) {
                    Ig[pp] += Ig2[pp];
                    Ig[pp + 1] += Ig2[pp + 1];
                    Ig[pp + 2] += Ig2[pp + 2];
                }
            }
        }

        opFlags.reset(waiting_flag);
    }
#if CONJUGATE
    std::complex<double> S1 = Vg[0] * conj(Ig[0]);
    std::complex<double> S2 = Vg[1] * conj(Ig[1]);
    std::complex<double> S3 = Vg[2] * conj(Ig[2]);
#else
    std::complex<double> S1 = Vg[0] * Ig[0];
    std::complex<double> S2 = Vg[1] * Ig[1];
    std::complex<double> S3 = Vg[2] * Ig[2];
#endif

    double P1 = S1.real();
    double retP = P1 + S2.real() + S3.real();
    double retQ = S1.imag() + S2.imag() + S3.imag();
    double scale = m_mult / (systemBasePower * 1000000.0);
    retP = retP * scale;  // basepower is MW
    retQ = retQ * scale;

#ifdef SGS_DEBUG
    std::cout << "SGS : retP = " << retP << " systemBasePower = " << systemBasePower << '\n';

    std::cout << "SGS : " << prevTime << " : " << name << " gridLabDLoad::runGridLabB P = " << retP
              << " Q = " << retQ << '\n';
#endif

    if (unbalancedAlert) {
        if (((P1 / retP) > (1.1 / 3.0)) || ((P1 / retP) < (0.9 / 3.0))) {
            alert(this, UNBALANCED_LOAD_ALERT);
        }
    }

    return {retP, retQ};
}

void gridLabDLoad::run2GridLabA(coreTime time, const IOdata& inputs)
{
    assert(opFlags[waiting_flag] == false);  // this should not happen;
    LOG_TRACE("calling gridlab load 2A");
    GhostSwingBusManager::cvec Vg(6);

    auto dt = time - m_lastCallTime;
    int tInt = 0;
    // get the right timer interval
    if (dt > timeOneSecond) {
        tInt = static_cast<int>(dt.seconds());
        m_lastCallTime += static_cast<double>(tInt);
    }

    // define the A values
    double V = inputs[voltageInLocation];
    Vprev = inputs[voltageInLocation];
    Thprev = inputs[angleInLocation];

    Vg[3] = std::complex<double>(V * localBaseVoltage * 1000.0, 0);
    Vg[4] = Vg[3] * rotn120;
    Vg[5] = Vg[3] * rotp120;

    double r1 = (V + spread) / V;
    Vg[0] = Vg[3] * r1;
    Vg[1] = Vg[4] * r1;
    Vg[2] = Vg[5] * r1;
    auto gsm = GhostSwingBusManager::instance();
    if (gsm) {
        for (size_t kk = 0; kk < task_id.size(); ++kk) {
            GhostSwingBusManager::instance()->sendVoltageStep(task_id[kk], Vg, tInt);
            if (opFlags[dual_mode_flag]) {
                if (opFlags[dyn_initialized]) {
                    (void)kk;  // ignore for loop conversion suggestion
                    // GhostSwingBusManager::instance ()->sendVoltageStep (forward_task_id[kk],
                    // Vg, tInt);
                }
            }
        }
        opFlags.set(waiting_flag);
    }
}

std::vector<double> gridLabDLoad::run2GridLabB(bool unbalancedAlert)
{
    assert(opFlags[waiting_flag]);  // this should not happen;
    LOG_TRACE("calling gridlab-d load 2B");
    // Model linear dependence on V
    GhostSwingBusManager::cvec Ig(6);
    GhostSwingBusManager::cvec Ig2(6);
    GhostSwingBusManager::cvec Vg(6);

    double V = Vprev;

    Vg[3] = std::complex<double>(V * localBaseVoltage * 1000.0, 0);
    Vg[4] = Vg[3] * rotn120;
    Vg[5] = Vg[3] * rotp120;

    double r1 = (V + spread) / V;
    Vg[0] = Vg[3] * r1;
    Vg[1] = Vg[4] * r1;
    Vg[2] = Vg[5] * r1;

    // get the current in a blocking call;
    auto gsm = GhostSwingBusManager::instance();
    if (gsm) {
        int ii = 0;
        for (auto& tid : task_id) {
            gsm->getCurrent(tid, Ig2);
            if (ii == 0) {
                Ig = Ig2;
                ii = 1;
            } else {
                for (size_t pp = 0; pp < Ig2.size(); pp += 3) {
                    Ig[pp] += Ig2[pp];
                    Ig[pp + 1] += Ig2[pp + 1];
                    Ig[pp + 2] += Ig2[pp + 2];
                }
            }
        }
    }
    opFlags.reset(waiting_flag);
#if CONJUGATE
    std::complex<double> S1 = Vg[0] * conj(Ig[0]) + Vg[1] * conj(Ig[1]) + Vg[2] * conj(Ig[2]);
    std::complex<double> S2 = Vg[3] * conj(Ig[3]) + Vg[4] * conj(Ig[4]) + Vg[5] * conj(Ig[5]);
#else
    std::complex<double> S1 = Vg[0] * Ig[0] + Vg[1] * Ig[1] + Vg[2] * Ig[2];
    std::complex<double> S2 = Vg[3] * Ig[3] + Vg[4] * Ig[4] + Vg[5] * Ig[5];
#endif
    double scale = m_mult / (systemBasePower * 1000000.0);
    double P1 = S1.real() * scale;  // basePower is MW
    double P2 = S2.real() * scale;  // basePower is MW
    double Q1 = S1.imag() * scale;  // basePower is MW
    double Q2 = S2.imag() * scale;  // basePower is MW

    double V2 = V;  // localBaseVoltage is in KV  we ignore the angle since it shouldn't matter
    double V1 = V * (1.0 + spread);

    std::vector<double> retP(4);
    retP[2] = (P2 - P1) / (V2 - V1);
    retP[3] = (Q2 - Q1) / (V2 - V1);
    retP[0] = P1 - V1 * retP[2];
    retP[1] = Q1 - V1 * retP[3];

#ifdef SGS_DEBUG
    std::cout << "SGS : gridLabDLoad::run2GridLabB P = " << retP[0] << " [1] = " << retP[1] << '\n';
#endif

    if (unbalancedAlert) {
#if CONJUGATE
        P1 = (Vg[0] * conj(Ig[0])).real();
#else
        P1 = (Vg[0] * Ig[0]).real();
#endif
        if (((P1 / S1.real()) > (1.1 / 3.0)) || ((P1 / S1.real()) < (0.9 / 3.0))) {
            alert(this, UNBALANCED_LOAD_ALERT);
        }
    }
    return retP;
}

void gridLabDLoad::run3GridLabA(coreTime time, const IOdata& inputs)
{
    assert(!opFlags[waiting_flag]);  // this should not happen;
    LOG_TRACE("calling gridLab-d load 3A");

    GhostSwingBusManager::cvec Vg(9);
    auto dt = time - m_lastCallTime;
    decltype(dt.seconds()) tInt = 0;
    // get the right timer interval
    if (dt > timeOneSecond) {
        tInt = dt.seconds();
        m_lastCallTime += static_cast<double>(tInt);
    }

    double V = inputs[voltageInLocation];
    Vprev = inputs[voltageInLocation];
    Thprev = inputs[angleInLocation];

    // send the current voltage as the last in the series
    Vg[6] = std::complex<double>(
        V * localBaseVoltage * 1000.0,
        0);  // localBaseVoltage is in KV  we ignore the angle since it shouldn't matter
    Vg[7] = Vg[6] * rotn120;
    Vg[8] = Vg[6] * rotp120;

    double r1 = (V + spread) / V;
    Vg[0] = Vg[6] * r1;
    Vg[1] = Vg[7] * r1;
    Vg[2] = Vg[8] * r1;
    r1 = (V - spread) / V;
    Vg[3] = Vg[6] * r1;
    Vg[4] = Vg[7] * r1;
    Vg[5] = Vg[8] * r1;

    auto gsm = GhostSwingBusManager::instance();
    if (gsm) {
        for (size_t kk = 0; kk < task_id.size(); ++kk) {
            gsm->sendVoltageStep(task_id[kk], Vg, static_cast<unsigned int>(tInt));
            if (opFlags[dual_mode_flag]) {
                if (opFlags[dyn_initialized]) {
                    (void)kk;  // ignore for loop conversion suggestion
                    // GhostSwingBusManager::instance ()->sendVoltageStep (forward_task_id[kk],
                    // Vg, tInt);
                }
            }
        }
        opFlags.set(waiting_flag);
    }
}

std::vector<double> gridLabDLoad::run3GridLabB(bool unbalancedAlert)
{
    assert(opFlags[waiting_flag]);  // this should not happen;

    LOG_TRACE("calling gridLab-d load 3B");
    GhostSwingBusManager::cvec Ig(9);
    GhostSwingBusManager::cvec Ig2(9);
    GhostSwingBusManager::cvec Vg(9);
    double P1;
    double V = Vprev;
    Vg[6] = std::complex<double>(
        V * localBaseVoltage * 1000.0,
        0);  // localBaseVoltage is in KV  we ignore the angle since it shouldn't matter
    Vg[7] = Vg[6] * rotn120;
    Vg[8] = Vg[6] * rotp120;

    double r1 = (V + spread) / V;
    Vg[0] = Vg[6] * r1;
    Vg[1] = Vg[7] * r1;
    Vg[2] = Vg[8] * r1;
    r1 = (V - spread) / V;
    Vg[3] = Vg[6] * r1;
    Vg[4] = Vg[7] * r1;
    Vg[5] = Vg[8] * r1;

    // get the current in a blocking call;
    auto gsm = GhostSwingBusManager::instance();
    if (gsm) {
        int ii = 0;
        for (auto& tid : task_id) {
            gsm->getCurrent(tid, Ig2);
            if (ii == 0) {
                Ig = Ig2;
                ii = 1;
            } else {
                for (size_t pp = 0; pp < Ig2.size(); pp += 3) {
                    Ig[pp] += Ig2[pp];
                    Ig[pp + 1] += Ig2[pp + 1];
                    Ig[pp + 2] += Ig2[pp + 2];
                }
            }
        }
    }
    opFlags.reset(waiting_flag);

#if CONJUGATE
    std::complex<double> S1 = Vg[0] * conj(Ig[0]) + Vg[1] * conj(Ig[1]) + Vg[2] * conj(Ig[2]);
    std::complex<double> S2 = Vg[3] * conj(Ig[3]) + Vg[4] * conj(Ig[4]) + Vg[5] * conj(Ig[5]);
    std::complex<double> S3 = Vg[6] * conj(Ig[6]) + Vg[7] * conj(Ig[7]) + Vg[8] * conj(Ig[8]);
#else
    std::complex<double> S1 = Vg[0] * Ig[0] + Vg[1] * Ig[1] + Vg[2] * Ig[2];
    std::complex<double> S2 = Vg[3] * Ig[3] + Vg[4] * Ig[4] + Vg[5] * Ig[5];
    std::complex<double> S3 = Vg[6] * Ig[6] + Vg[7] * Ig[7] + Vg[8] * Ig[8];
#endif

    if (unbalancedAlert) {
#if CONJUGATE
        P1 = (Vg[0] * conj(Ig[0])).real();
#else
        P1 = (Vg[0] * Ig[0]).real();
#endif

        if (((P1 / S1.real()) > (1.05 / 3.0)) || ((P1 / S1.real()) < (0.95 / 3.0))) {
            alert(this, UNBALANCED_LOAD_ALERT);
        }
    }
    double scale = m_mult / (systemBasePower * 1000000.0);
    P1 = S1.real() * scale;  // basePower is MW
    double P2 = S2.real() * scale;  // basePower is MW
    double P3 = S3.real() * scale;  // basePower is MW
    double Q1 = S1.imag() * scale;  // basePower is MW
    double Q2 = S2.imag() * scale;  // basePower is MW
    double Q3 = S3.imag() * scale;  // basePower is MW

    double V3 = V;
    double V1 = V + spread;
    double V2 = V - spread;
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

#ifdef SGS_DEBUG
    std::cout << "SGS : gridLabDLoad::run3GridLabB systemBasePower = " << systemBasePower << '\n';

    std::cout << "SGS : gridlabDLaod::run3GridLabB V1 = " << V1 << " V2 = " << V2 << " V3 = " << V3
              << '\n';

    std::cout << "SGS : gridlabDLaod::run3GridLabB Vg[0] = " << Vg[0] << " Vg[1] = " << Vg[1]
              << " Vg[2] = " << Vg[2] << '\n';
    std::cout << "SGS : gridlabDLaod::run3GridLabB Ig[0] = " << Ig[0] << " Ig[1] = " << Ig[1]
              << " Ig[2] = " << Ig[2] << '\n';

    std::cout << "SGS : gridLabDLoad::run3GridLabB P1 = " << P1 << " Q1 = " << Q1 << " P2 = " << P2
              << " Q2 = " << Q2 << " P3 = " << P3 << " Q3 = " << Q3 << '\n';

    std::cout << "SGS : gridLabDLoad::run3GridLabB retP[0] = " << retP[0] << " [1] = " << retP[1]
              << " [2] = " << retP[2] << " [3] = " << retP[3] << " [4] = " << retP[4]
              << " [5] = " << retP[5] << '\n';
#endif

    return retP;
}

void gridLabDLoad::set(std::string_view param, std::string_view val)
{
    using gmlc::utilities::convertToLowerCase;
    using gmlc::utilities::numeric_conversion;
    std::string numstr;
    int num;

    if (param.compare(0, 4, "file") == 0) {
        num = gmlc::utilities::stringOps::trailingStringInt(param, numstr, -1);
        if (num >= 0) {
            if (num > static_cast<int>(gridlabDfile.size())) {
                gridlabDfile.resize(num);
                workdir.resize(num);
#ifndef GRIDDYN_ENABLE_MPI
                dummy_load.resize(num);
                dummy_load_forward.resize(num);
#endif
            }
            gridlabDfile[num] = val;
        } else {
            gridlabDfile.push_back(std::string{val});
            workdir.resize(gridlabDfile.size());
#ifndef GRIDDYN_ENABLE_MPI
            dummy_load.resize(gridlabDfile.size());
            dummy_load_forward.resize(gridlabDfile.size());
#endif
        }
    } else if (param.compare(0, 7, "workdir") == 0) {
        numstr = param.substr(7);
        num = numeric_conversion<int>(numstr, -1);
        if (num >= 0) {
            if (num > static_cast<int>(gridlabDfile.size())) {
                gridlabDfile.resize(num);
                workdir.resize(num);
#ifndef GRIDDYN_ENABLE_MPI
                dummy_load.resize(num);
                dummy_load_forward.resize(num);
#endif
            }
            workdir[num] = val;
        } else {
            for (auto& wd : workdir) {
                if (wd.empty()) {
                    wd = val;
                }
            }
        }
    } else if (param == "detail") {
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

void gridLabDLoad::set(std::string_view param, double val, units::unit unitType)
{
    // TODO(phlpt): Convert some of these to a setFlags function.
    if ((param == "spread") || (param == "band")) {
        if (std::abs(val) > kMin_Res) {
            spread = val;
        } else {
            throw(invalidParameterValue(std::string{param}));
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

void gridLabDLoad::rootTest(const IOdata& inputs,
                            const stateData& /*sD*/,
                            double roots[],
                            const solverMode& sMode)
{
    int rootOffset = offsets.getRootOffset(sMode);
    double V = inputs[voltageInLocation];
    roots[rootOffset] = spread * triggerBound - std::abs(V - Vprev);

    // printf("time=%f root =%12.10f\n", time,roots[rootOffset]);
}

void gridLabDLoad::rootTrigger(coreTime time,
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

change_code gridLabDLoad::rootCheck(const IOdata& inputs,
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

int gridLabDLoad::mpiCount() const
{
    int cnt = 0;
    if (opFlags[dual_mode_flag]) {
        for (auto& gfl : gridlabDfile) {
            if (!gfl.empty()) {
                cnt += 2;
            }
        }
    } else {
        for (auto& gfl : gridlabDfile) {
            if (!gfl.empty()) {
                cnt += 1;
            }
        }
    }
    return cnt;
}

#ifndef GRIDDYN_ENABLE_MPI
void gridLabDLoad::run_dummy_load(index_t kk, VoltageMessage* vm, CurrentMessage* cm)
{
    for (int ii = 0; ii < vm->numThreePhaseVoltage; ii++) {
        auto vtest = std::hypot(vm->voltage[ii].real[0], vm->voltage[ii].imag[0]);

        auto rP = dummy_load[kk]->getRealPower({vtest / localBaseVoltage * 0.001},
                                               emptyStateData,
                                               cLocalSolverMode);
        auto rQ = dummy_load[kk]->getReactivePower({vtest / localBaseVoltage * 0.001},
                                                   emptyStateData,
                                                   cLocalSolverMode);
        auto vcom = std::complex<double>(vtest, 0);
        auto power = std::complex<double>(rP, rQ) / 3.0;
        auto ctest = power * (dummy_load[kk]->get("basepower")) * 1000000.0 / vcom;
        for (int pp = 0; pp < 3; pp++) {
            auto c2 = ctest * vcom /
                std::complex<double>(vm->voltage[ii].real[pp], vm->voltage[ii].imag[pp]);
            cm->current[ii].real[pp] = c2.real();
#    if CONJUGATE
            cm->current[ii].imag[pp] = -c2.imag();
#    else
            cm->current[ii].imag[pp] = c2.imag();
#    endif
        }
    }
    cm->numThreePhaseCurrent = vm->numThreePhaseVoltage;
}

void gridLabDLoad::run_dummy_load_forward(index_t kk, VoltageMessage* vm, CurrentMessage* cm)
{
    for (int ii = 0; ii < vm->numThreePhaseVoltage; ii++) {
        auto vtest = std::hypot(vm->voltage[ii].real[0], vm->voltage[ii].imag[0]);

        auto rP = dummy_load_forward[kk]->getRealPower({vtest / localBaseVoltage / 1000},
                                                       emptyStateData,
                                                       cLocalSolverMode);
        auto rQ = dummy_load_forward[kk]->getReactivePower({vtest / localBaseVoltage / 1000},
                                                           emptyStateData,
                                                           cLocalSolverMode);
        auto vcom = std::complex<double>(vtest, 0);
        auto power = std::complex<double>(rP, rQ) / 3.0;
        auto ctest = power * (dummy_load_forward[kk]->get("basepower")) * 1000000.0 / vcom;
        for (int pp = 0; pp < 3; pp++) {
            auto c2 = ctest * vcom /
                std::complex<double>(vm->voltage[ii].real[pp], vm->voltage[ii].imag[pp]);
            cm->current[ii].real[pp] = c2.real();
#    if CONJUGATE
            cm->current[ii].imag[pp] = -c2.imag();
#    else
            cm->current[ii].imag[pp] = c2.imag();
#    endif
        }
    }
    cm->numThreePhaseCurrent = vm->numThreePhaseVoltage;
}
#endif  // GRIDDYN_ENABLE_MPI

}  // namespace griddyn::loads
