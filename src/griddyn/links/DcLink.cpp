/*
 * Copyright (c) 2014-2026, Lawrence Livermore National Security
 * See the top-level NOTICE for additional details. All rights reserved.
 * SPDX-License-Identifier: BSD-3-Clause
 */

#include "DcLink.h"

#include "../GridArea.h"
#include "../GridBus.h"
#include "../primary/DcBus.h"
#include "core/CoreExceptions.h"
#include "core/CoreObjectTemplates.hpp"
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

dcLink::dcLink(double resistancePu, double reactancePu, const std::string& objName):
    Link(objName), r(resistancePu), x(reactancePu)
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

void dcLink::updateBus(GridBus* bus, index_t busnumber)
{
    if (dynamic_cast<DcBus*>(bus) != nullptr) {
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
    Link::set(param, val);
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
                                  const stateData& stateData,
                                  matrixData<double>& jacobian,
                                  const IOlocs& inputLocs,
                                  const solverMode& sMode)
{
    // check if line is enabled
    updateLocalCache(noInputs, stateData, sMode);
    if (!(isEnabled())) {
        return;
    }

    if ((busId == 2) || (busId == B2->getID())) {
        jacobian.assignCheckCol(PoutLocation, inputLocs[voltageInLocation], -Idc);
    } else {
        jacobian.assignCheckCol(PoutLocation, inputLocs[voltageInLocation], Idc);
    }
}

void dcLink::outputPartialDerivatives(id_type_t busId,
                                      const stateData& stateData,
                                      matrixData<double>& jacobian,
                                      const solverMode& sMode)
{
    if (!(isEnabled())) {
        return;
    }
    updateLocalCache(noInputs, stateData, sMode);

    double p1v2 = 0.0;
    double p2v1 = 0.0;
    if (!isDynamic(sMode)) {
        if (r > 0.0) {
            p1v2 = -linkInfo.v1 / r;
            p2v1 = -linkInfo.v1 / r;
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
            jacobian.assign(PoutLocation, offset, -linkInfo.v2);
        } else {
            const int bus1VoltageOffset = B1->getOutputLoc(sMode, voltageInLocation);
            jacobian.assignCheckCol(PoutLocation, bus1VoltageOffset, p2v1);
        }
    } else {
        if (stateSize(sMode) > 0) {
            auto offset =
                isDynamic(sMode) ? offsets.getDiffOffset(sMode) : offsets.getAlgOffset(sMode);
            jacobian.assign(PoutLocation, offset, linkInfo.v1);
        } else {
            const int bus2VoltageOffset = B2->getOutputLoc(sMode, voltageInLocation);
            jacobian.assignCheckCol(PoutLocation, bus2VoltageOffset, p1v2);
        }
    }
}

count_t dcLink::outputDependencyCount(index_t num, const solverMode& /*sMode*/) const
{
    return (num == PoutLocation) ? 1 : 0;
}

void dcLink::jacobianElements(const IOdata& /*inputs*/,
                              const stateData& stateData,
                              matrixData<double>& jacobian,
                              const IOlocs& /*inputLocs*/,
                              const solverMode& sMode)
{
    if (stateSize(sMode) > 0) {
        const int bus1VoltageOffset = B1->getOutputLoc(sMode, voltageInLocation);
        const int bus2VoltageOffset = B2->getOutputLoc(sMode, voltageInLocation);
        updateLocalCache(noInputs, stateData, sMode);
        if (isDynamic(sMode)) {
            auto offset = offsets.getDiffOffset(sMode);
            jacobian.assignCheckCol(offset, bus1VoltageOffset, 1.0 / x);
            jacobian.assignCheckCol(offset, bus2VoltageOffset, -1.0 / x);
            jacobian.assign(offset, offset, (-(r / x)) - stateData.cj);
        } else {
            auto offset = offsets.getAlgOffset(sMode);
            jacobian.assignCheckCol(offset, bus1VoltageOffset, 1.0);
            jacobian.assignCheckCol(offset, bus2VoltageOffset, -1.0);
            if (opFlags[fixed_target_power]) {
                jacobian.assignCheckCol(offset,
                                        bus1VoltageOffset,
                                        -Pset / (linkInfo.v1 * linkInfo.v1));
                jacobian.assign(offset, offset, -1.0);
            }
        }
    }
}

void dcLink::residual(const IOdata& inputs,
                      const stateData& stateData,
                      double resid[],
                      const solverMode& sMode)
{
    if (stateSize(sMode) > 0) {
        updateLocalCache(inputs, stateData, sMode);
        if (isDynamic(sMode)) {
            auto offset = offsets.getDiffOffset(sMode);
            resid[offset] = ((linkInfo.v1 - linkInfo.v2 - (r * stateData.state[offset])) / x) -
                stateData.dstate_dt[offset];
        } else {
            auto offset = offsets.getAlgOffset(sMode);
            if (opFlags[fixed_target_power]) {
                resid[offset] =
                    (linkInfo.v1 - linkInfo.v2) + (Pset / linkInfo.v1) - stateData.state[offset];
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
        const std::string prefix2 = prefix + getName() + ':';
        auto offset =
            (isDynamic(sMode)) ? offsets.getDiffOffset(sMode) : offsets.getAlgOffset(sMode);
        stNames[offset] = prefix2 + "idc";
    }
}

void dcLink::updateLocalCache(const IOdata& /*inputs*/,
                              const stateData& stateData,
                              const solverMode& sMode)
{
    if (!stateData.updateRequired(linkInfo.seqID)) {
        return;
    }

    if (!isEnabled()) {
        return;
    }
    linkInfo = {};

    linkInfo.v1 = B1->getVoltage(stateData.state, sMode);
    linkInfo.v2 = B2->getVoltage(stateData.state, sMode);
    if (stateSize(sMode) > 0) {
        auto offset =
            (isDynamic(sMode)) ? offsets.getDiffOffset(sMode) : offsets.getAlgOffset(sMode);
        Idc = stateData.state[offset];
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
        if (B2->getType() == GridBus::busType::SLK) {
            linkInfo.v2 = B2->getVoltage();
            Idc = power / linkInfo.v2;
            const double bus1Voltage = linkInfo.v2 - (Idc * r);
            B1->setVoltageAngle(bus1Voltage, 0);
            updateLocalCache();
            return B1->propogatePower(true);
        }
        if (B1->getType() == GridBus::busType::SLK) {
            linkInfo.v1 = B1->getVoltage();
            if (r > 0) {
                const double temp = linkInfo.v1 / r;
                Idc = 0.5 * (-temp + std::sqrt((temp * temp) + ((4 * power) / r)));
            } else {
                Idc = power / linkInfo.v1;
            }
            const double bus2Voltage = power / Idc;
            B2->setVoltageAngle(bus2Voltage, 0);
            updateLocalCache();
            ret = B2->propogatePower(true);
        } else {
            double bus1Voltage = B1->getVoltage();
            double bus2Voltage = B2->getVoltage();
            const double delta = (r > 0) ?
                (((power * r) - (bus2Voltage * bus2Voltage) + (bus2Voltage * bus1Voltage)) /
                 (bus1Voltage + bus2Voltage)) :
                ((bus1Voltage - bus2Voltage) / 2);
            bus1Voltage = bus1Voltage - delta;
            bus2Voltage = bus2Voltage + delta;
            B1->setVoltageAngle(bus1Voltage, 0);
            B2->setVoltageAngle(bus2Voltage, 0);
            Idc = (r > 0) ? ((bus1Voltage - bus2Voltage) / r) : (power / bus1Voltage);
            updateLocalCache();
            B1->propogatePower(false);
            B2->propogatePower(false);
        }
        Idc = -Idc;
    } else {
        if (B1->getType() == GridBus::busType::SLK) {
            linkInfo.v1 = B1->getVoltage();
            Idc = power / linkInfo.v1;
            const double bus2Voltage = linkInfo.v1 - (Idc * r);
            B2->setVoltageAngle(bus2Voltage, 0);
            updateLocalCache();
            return B2->propogatePower(true);
        }
        if (B2->getType() == GridBus::busType::SLK) {
            linkInfo.v2 = B2->getVoltage();
            if (r > 0) {
                const double temp = linkInfo.v2 / r;
                Idc = 0.5 * (-temp + std::sqrt((temp * temp) + ((4 * power) / r)));
            } else {
                Idc = -power / linkInfo.v2;
            }

            const double bus1Voltage = power / Idc;
            B1->setVoltageAngle(bus1Voltage, 0);
            updateLocalCache();
            ret = B1->propogatePower(true);
        } else {
            double bus1Voltage = B1->getVoltage();
            double bus2Voltage = B2->getVoltage();
            const double delta = (r > 0) ?
                (((power * r) - (bus1Voltage * bus1Voltage) + (bus2Voltage * bus1Voltage)) /
                 (bus1Voltage + bus2Voltage)) :
                ((bus2Voltage - bus1Voltage) / 2);
            bus1Voltage = bus1Voltage + delta;
            bus2Voltage = bus2Voltage - delta;
            B1->setVoltageAngle(bus1Voltage, 0);
            B2->setVoltageAngle(bus2Voltage, 0);
            Idc = (r > 0) ? ((bus1Voltage - bus2Voltage) / r) : (power / bus1Voltage);
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
