/*
 * Copyright (c) 2014-2026, Lawrence Livermore National Security
 * See the top-level NOTICE for additional details. All rights reserved.
 * SPDX-License-Identifier: BSD-3-Clause
 */

#include "dcLink.h"

#include "../Area.h"
#include "../gridBus.h"
#include "../primary/dcBus.h"
#include "core/coreExceptions.h"
#include "core/coreObjectTemplates.hpp"
#include "gmlc/utilities/vectorOps.hpp"
#include <cmath>
#include <cstring>
#include <string>

namespace griddyn::links {
using units::convert;
using units::puMW;
using units::unit;
dcLink::dcLink(const std::string& objName): Link(objName)
{
    opFlags.set(dc_only);
    opFlags.set(network_connected);
}

dcLink::dcLink(double rP, double Lp, const std::string& objName): Link(objName), r(rP), x(Lp)
{
    opFlags.set(dc_only);
    opFlags.set(network_connected);
}

CoreObject* dcLink::clone(CoreObject* obj) const
{
    auto* nobj = cloneBase<dcLink, Link>(this, obj);
    if (nobj == nullptr) {
        return obj;
    }
    nobj->r = r;
    nobj->x = x;

    return nobj;
}

void dcLink::timestep(coreTime /*time*/, const IOdata& /*inputs*/, const solverMode& /*sMode*/)
{
    if (!isEnabled()) {
        return;
    }
    updateLocalCache();

    /*if (scheduled)
{
Psched=sched->timestepP(time);
}*/
}

void dcLink::updateBus(gridBus* bus, index_t busnumber)
{
    if (dynamic_cast<dcBus*>(bus) != nullptr) {
        Link::updateBus(bus, busnumber);
    } else {
        throw(unrecognizedObjectException(this));
    }
}

double dcLink::getMaxTransfer() const
{
    if (!isConnected()) {
        return 0.0;
    }
    if (Erating > 0.0) {
        return Erating;
    }
    if (ratingB > 0.0) {
        return ratingB;
    }
    if (ratingA > 0.0) {
        return ratingA;
    }

    return ((r > 0.0) ? (1.0 / r) : kBigNum);
}
// set properties
void dcLink::set(std::string_view param, std::string_view val)
{
    return Link::set(param, val);
}
void dcLink::set(std::string_view param, double val, unit unitType)
{
    if ((param == "r") || (param == "rdc")) {
        r = val;
    } else if ((param == "l") || (param == "x") || (param == "ldc")) {
        x = val;
        // set line admittance
    } else {
        Link::set(param, val, unitType);
    }
}

void dcLink::pFlowObjectInitializeA(coreTime time0, std::uint32_t flags)
{
    Link::pFlowObjectInitializeA(time0, flags);
    if (isEnabled()) {
        if (opFlags[fixed_target_power]) {
            fixRealPower(Pset, 1);
        }
    }
}

void dcLink::pFlowObjectInitializeB()
{
    if (isEnabled()) {
        updateLocalCache();
        if (opFlags[fixed_target_power]) {
            fixRealPower(Pset, 1);
        }
    }
}

void dcLink::dynObjectInitializeA(coreTime /*time0*/, std::uint32_t /*flags*/)
{
    m_dstate_dt.resize(1);
    m_state.resize(1);
    updateLocalCache();
    if (x != 0) {
        m_state[0] = Idc;
        m_dstate_dt[0] = 0.0;
    } else {
    }
}

stateSizes dcLink::LocalStateSizes(const solverMode& sMode) const
{
    stateSizes localSS;
    if (isDynamic(sMode)) {
        if (x != 0.0) {
            if (!isAlgebraicOnly(sMode)) {
                localSS.diffSize = 1;
            }
        }
    } else if (r <= 0.0) {  // superconducting
        localSS.algSize = 1;
    }
    return localSS;
}

count_t dcLink::LocalJacobianCount(const solverMode& sMode) const
{
    count_t jacCount = 0;
    if (isDynamic(sMode)) {
        if (x != 0.0) {
            if (!isAlgebraicOnly(sMode)) {
                jacCount = 3;
            }
        }
    } else if (r <= 0.0) {  // superconducting
        jacCount = 2;
    }
    return jacCount;
}

void dcLink::ioPartialDerivatives(id_type_t busId,
                                  const stateData& sD,
                                  matrixData<double>& md,
                                  const IOlocs& inputLocs,
                                  const solverMode& sMode)
{
    // check if line is enabled
    updateLocalCache(noInputs, sD, sMode);
    if (!(isEnabled())) {
        return;
    }

    if ((busId == 2) || (busId == B2->getID())) {
        md.assignCheckCol(PoutLocation, inputLocs[voltageInLocation], -Idc);
    } else {
        md.assignCheckCol(PoutLocation, inputLocs[voltageInLocation], Idc);
    }
}

void dcLink::outputPartialDerivatives(id_type_t busId,
                                      const stateData& sD,
                                      matrixData<double>& md,
                                      const solverMode& sMode)
{
    if (!(isEnabled())) {
        return;
    }
    updateLocalCache(noInputs, sD, sMode);

    double P1V2 = 0.0, P2V1 = 0.0;
    if (!isDynamic(sMode)) {
        if (r > 0.0) {
            P1V2 = -linkInfo.v1 / r;
            P2V1 = -linkInfo.v1 / r;
        } else {
        }
    } else {  // in some other mode
    }
    // int mode = B1->getMode(sMode) * 4 + B2->getMode(sMode);

    //    md.assign(B1Voffset, B2Voffset, Q1V2);
    //    md.assign(B2Voffset, B1Voffset, Q2V1);
    if ((busId == 2) || (busId == B2->getID())) {
        if (stateSize(sMode) > 0) {
            auto offset =
                isDynamic(sMode) ? offsets.getDiffOffset(sMode) : offsets.getAlgOffset(sMode);
            md.assign(PoutLocation, offset, -linkInfo.v2);
        } else {
            int B1Voffset = B1->getOutputLoc(sMode, voltageInLocation);
            md.assignCheckCol(PoutLocation, B1Voffset, P2V1);
        }
    } else {
        if (stateSize(sMode) > 0) {
            auto offset =
                isDynamic(sMode) ? offsets.getDiffOffset(sMode) : offsets.getAlgOffset(sMode);
            md.assign(PoutLocation, offset, linkInfo.v1);
        } else {
            int B2Voffset = B2->getOutputLoc(sMode, voltageInLocation);
            md.assignCheckCol(PoutLocation, B2Voffset, P1V2);
        }
    }
}

count_t dcLink::outputDependencyCount(index_t num, const solverMode& /*sMode*/) const
{
    return (num == PoutLocation) ? 1 : 0;
}

void dcLink::jacobianElements(const IOdata& /*inputs*/,
                              const stateData& sD,
                              matrixData<double>& md,
                              const IOlocs& /*inputLocs*/,
                              const solverMode& sMode)
{
    if (stateSize(sMode) > 0) {
        int B1Voffset = B1->getOutputLoc(sMode, voltageInLocation);
        int B2Voffset = B2->getOutputLoc(sMode, voltageInLocation);
        updateLocalCache(noInputs, sD, sMode);
        if (isDynamic(sMode)) {
            auto offset = offsets.getDiffOffset(sMode);
            md.assignCheckCol(offset, B1Voffset, 1.0 / x);
            md.assignCheckCol(offset, B2Voffset, -1.0 / x);
            md.assign(offset, offset, -r / x - sD.cj);
        } else {
            auto offset = offsets.getAlgOffset(sMode);
            md.assignCheckCol(offset, B1Voffset, 1.0);
            md.assignCheckCol(offset, B2Voffset, -1.0);
            if (opFlags[fixed_target_power]) {
                md.assignCheckCol(offset, B1Voffset, -Pset / (linkInfo.v1 * linkInfo.v1));
                md.assign(offset, offset, -1.0);
            }
        }
    }
}

void dcLink::residual(const IOdata& inputs,
                      const stateData& sD,
                      double resid[],
                      const solverMode& sMode)
{
    if (stateSize(sMode) > 0) {
        updateLocalCache(inputs, sD, sMode);
        if (isDynamic(sMode)) {
            auto offset = offsets.getDiffOffset(sMode);
            resid[offset] =
                (linkInfo.v1 - linkInfo.v2 - r * sD.state[offset]) / x - sD.dstate_dt[offset];
        } else {
            auto offset = offsets.getAlgOffset(sMode);
            if (opFlags[fixed_target_power]) {
                resid[offset] = (linkInfo.v1 - linkInfo.v2) + Pset / linkInfo.v1 - sD.state[offset];
            } else {
                resid[offset] = linkInfo.v1 - linkInfo.v2;
            }
        }
    }
}

void dcLink::setState(coreTime time,
                      const double state[],
                      const double dstate_dt[],
                      const solverMode& sMode)
{
    if (stateSize(sMode) > 0) {
        if (isDynamic(sMode)) {
            auto offset = offsets.getDiffOffset(sMode);
            m_state[0] = state[offset];
            m_dstate_dt[0] = dstate_dt[offset];
            Idc = m_state[0];
        } else {
            auto offset = offsets.getAlgOffset(sMode);
            Idc = state[offset];
        }
    }
    prevTime = time;
}

void dcLink::guessState(const coreTime /*time*/,
                        double state[],
                        double dstate_dt[],
                        const solverMode& sMode)
{
    if (stateSize(sMode) > 0) {
        if (isDynamic(sMode)) {
            auto offset = offsets.getDiffOffset(sMode);
            state[offset] = m_state[0];
            dstate_dt[offset] = m_dstate_dt[0];
        } else {
            auto offset = offsets.getAlgOffset(sMode);
            state[offset] = Idc;
        }
    }
}

void dcLink::getStateName(stringVec& stNames,
                          const solverMode& sMode,
                          const std::string& prefix) const
{
    if (stateSize(sMode) > 0) {
        std::string prefix2 = prefix + getName() + ':';
        auto offset =
            (isDynamic(sMode)) ? offsets.getDiffOffset(sMode) : offsets.getAlgOffset(sMode);
        stNames[offset] = prefix2 + "idc";
    }
}

void dcLink::updateLocalCache(const IOdata& /*inputs*/,
                              const stateData& sD,
                              const solverMode& sMode)
{
    if (!sD.updateRequired(linkInfo.seqID)) {
        return;
    }

    if (!isEnabled()) {
        return;
    }
    linkInfo = {};

    linkInfo.v1 = B1->getVoltage(sD.state, sMode);
    linkInfo.v2 = B2->getVoltage(sD.state, sMode);
    if (stateSize(sMode) > 0) {
        auto offset =
            (isDynamic(sMode)) ? offsets.getDiffOffset(sMode) : offsets.getAlgOffset(sMode);
        Idc = sD.state[offset];
    } else {
        if (r > 0) {
            Idc = (linkInfo.v1 - linkInfo.v2) / r;

            //    Q2 = P2*sqrt(k3sq2*k3sq2 - gamma*gamma);
        } else {
            Idc = Pset / linkInfo.v1;
        }
    }
    linkFlows.P1 = linkInfo.v1 * Idc;
    linkFlows.P2 = -linkInfo.v2 * Idc;
}

void dcLink::updateLocalCache()
{
    linkInfo = {};

    if (isEnabled()) {
        linkInfo.v1 = B1->getVoltage();
        linkInfo.v2 = B2->getVoltage();
        linkFlows.P1 = linkInfo.v1 * Idc;
        linkFlows.P2 = -linkInfo.v2 * Idc;
    }
}

int dcLink::fixRealPower(double power,
                         id_type_t measureTerminal,
                         id_type_t fixedTerminal,
                         units::unit unitType)
{
    int ret = 0;
    opFlags.set(fixed_target_power);
    if (fixedTerminal == 0) {
        fixedTerminal = measureTerminal;
    }
    Pset = convert(power, unitType, puMW, systemBasePower);
    if ((fixedTerminal == 2) || (fixedTerminal == B2->getID())) {
        if (B2->getType() == gridBus::busType::SLK) {
            linkInfo.v2 = B2->getVoltage();
            Idc = power / linkInfo.v2;
            double v1 = linkInfo.v2 - Idc * r;
            B1->setVoltageAngle(v1, 0);
            updateLocalCache();
            return B1->propogatePower(true);
        }
        if (B1->getType() == gridBus::busType::SLK) {
            linkInfo.v1 = B1->getVoltage();
            if (r > 0) {
                double temp = linkInfo.v1 / r;
                Idc = 0.5 * (-temp + std::sqrt(temp * temp + 4 * power / r));
            } else {
                Idc = power / linkInfo.v1;
            }
            double v2 = power / Idc;
            B2->setVoltageAngle(v2, 0);
            updateLocalCache();
            ret = B2->propogatePower(true);
        } else {
            double v1 = B1->getVoltage();
            double v2 = B2->getVoltage();
            double delta = (r > 0) ? (power * r - v2 * v2 + v2 * v1) / (v1 + v2) : (v1 - v2) / 2;
            v1 = v1 - delta;
            v2 = v2 + delta;
            B1->setVoltageAngle(v1, 0);
            B2->setVoltageAngle(v2, 0);
            Idc = (r > 0) ? (v1 - v2) / r : power / v1;
            updateLocalCache();
            B1->propogatePower(false);
            B2->propogatePower(false);
        }
        Idc = -Idc;
    } else {
        if (B1->getType() == gridBus::busType::SLK) {
            linkInfo.v1 = B1->getVoltage();
            Idc = power / linkInfo.v1;
            double v2 = linkInfo.v1 - Idc * r;
            B2->setVoltageAngle(v2, 0);
            updateLocalCache();
            return B2->propogatePower(true);
        }
        if (B2->getType() == gridBus::busType::SLK) {
            linkInfo.v2 = B2->getVoltage();
            if (r > 0) {
                double temp = linkInfo.v2 / r;
                Idc = 0.5 * (-temp + std::sqrt(temp * temp + 4 * power / r));
            } else {
                Idc = -power / linkInfo.v2;
            }

            double v1 = power / Idc;
            B1->setVoltageAngle(v1, 0);
            updateLocalCache();
            ret = B1->propogatePower(true);
        } else {
            double v1 = B1->getVoltage();
            double v2 = B2->getVoltage();
            double delta = (r > 0) ? (power * r - v1 * v1 + v2 * v1) / (v1 + v2) : (v2 - v1) / 2;
            v1 = v1 + delta;
            v2 = v2 - delta;
            B1->setVoltageAngle(v1, 0);
            B2->setVoltageAngle(v2, 0);
            Idc = (r > 0) ? (v1 - v2) / r : power / v1;
            updateLocalCache();
            B1->propogatePower(false);
            B2->propogatePower(false);
        }
    }
    return ret;
}

int dcLink::fixPower(double power,
                     double /*qPower*/,
                     id_type_t measureTerminal,
                     id_type_t fixedTerminal,
                     units::unit unitType)
{
    return fixRealPower(power, measureTerminal, fixedTerminal, unitType);
}

}  // namespace griddyn::links
