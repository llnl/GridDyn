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
#include "gmlc/utilities/stringOps.h"
#include "gmlc/utilities/vectorOps.hpp"
#include <cmath>
#include <cstdint>
#include <cstring>
#include <string>

namespace griddyn::links {
using units::convert;
using units::puMW;
using units::unit;
DcLink::DcLink(const std::string& objName): Link(objName)
{
    opFlags.set(DC_ONLY);
    opFlags.set(NETWORK_CONNECTED);
}

DcLink::DcLink(double resistancePu, double reactancePu, const std::string& objName):
    Link(objName), r(resistancePu), x(reactancePu)
{
    opFlags.set(DC_ONLY);
    opFlags.set(NETWORK_CONNECTED);
}

CoreObject* DcLink::clone(CoreObject* obj) const
{
    auto* nobj = cloneBase<DcLink, Link>(this, obj);
    if (nobj == nullptr) {
        return obj;
    }
    nobj->r = r;
    nobj->x = x;
    nobj->c = c;
    nobj->topology = topology;
    nobj->currentBalance = currentBalance;

    return nobj;
}

bool DcLink::hasCapacitor() const
{
    return topology != Topology::RL;
}

bool DcLink::isOpenInPowerFlow() const
{
    return (topology == Topology::C) || (topology == Topology::RC_SERIES) ||
        (topology == Topology::RLC_SERIES);
}

count_t DcLink::currentStateOffset(const SolverMode& sMode) const
{
    if (hasCapacitor()) {
        return offsets.getAlgOffset(sMode);
    }
    return isDynamic(sMode) ? offsets.getDiffOffset(sMode) : offsets.getAlgOffset(sMode);
}

void DcLink::timestep(CoreTime /*time*/, const IOdata& /*inputs*/, const SolverMode& /*sMode*/)
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

void DcLink::updateBus(GridBus* bus, index_t busnumber)
{
    if (dynamic_cast<DcBus*>(bus) != nullptr) {
        Link::updateBus(bus, busnumber);
    } else {
        throw(UnrecognizedObjectException(this));
    }
}

double DcLink::getMaxTransfer() const
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
void DcLink::set(std::string_view param, std::string_view val)
{
    if ((param == "model") || (param == "topology")) {
        const auto model = gmlc::utilities::convertToLowerCase(val);
        if ((model == "r") || (model == "l") || (model == "rls") || (model == "rl")) {
            topology = Topology::RL;
        } else if (model == "c") {
            topology = Topology::C;
        } else if (model == "rcp") {
            topology = Topology::RC_PARALLEL;
        } else if (model == "rlcp") {
            topology = Topology::RLC_PARALLEL;
        } else if (model == "rcs") {
            topology = Topology::RC_SERIES;
        } else if (model == "rlcs") {
            topology = Topology::RLC_SERIES;
        } else {
            throw(InvalidParameterValue(val));
        }
    } else if ((param == "andes_current_balance") || (param == "current_balance")) {
        const auto mode = gmlc::utilities::convertToLowerCase(val);
        currentBalance =
            (mode == "true") || (mode == "1") || (mode == "current") || (mode == "andes");
    } else {
        Link::set(param, val);
    }
}
void DcLink::set(std::string_view param, double val, unit unitType)
{
    if ((param == "r") || (param == "rdc")) {
        r = val;
    } else if ((param == "l") || (param == "x") || (param == "ldc")) {
        x = val;
        // set line admittance
    } else if ((param == "c") || (param == "cdc")) {
        c = val;
    } else if ((param == "andes_current_balance") || (param == "current_balance")) {
        currentBalance = (val > 0.5);
    } else {
        Link::set(param, val, unitType);
    }
}

void DcLink::pFlowObjectInitializeA(CoreTime time0, std::uint32_t flags)
{
    Link::pFlowObjectInitializeA(time0, flags);
    if (isEnabled()) {
        if (opFlags[FIXED_TARGET_POWER]) {
            fixRealPower(Pset, 1);
        }
    }
}

void DcLink::pFlowObjectInitializeB()
{
    if (isEnabled()) {
        updateLocalCache();
        if (opFlags[FIXED_TARGET_POWER]) {
            fixRealPower(Pset, 1);
        }
    }
}

void DcLink::dynObjectInitializeA(CoreTime /*time0*/, std::uint32_t /*flags*/)
{
    std::uint32_t differentialCount = 0U;
    if (topology == Topology::RLC_PARALLEL || topology == Topology::RLC_SERIES) {
        differentialCount = 2U;
    } else if (hasCapacitor() || (x != 0.0)) {
        differentialCount = 1U;
    }
    m_dstate_dt.assign(differentialCount, 0.0);
    m_state.assign(differentialCount, 0.0);
    updateLocalCache();
    if (!hasCapacitor() && (x != 0)) {
        m_state[0] = Idc;
    } else if (hasCapacitor()) {
        // The capacitor voltage is always the first differential state.
        m_state[(topology == Topology::RLC_PARALLEL || topology == Topology::RLC_SERIES) ? 1 : 0] =
            linkInfo.v1 - linkInfo.v2;
    }
}

StateSizes DcLink::localStateSizes(const SolverMode& sMode) const
{
    StateSizes localSS;
    if (isDynamic(sMode)) {
        if (hasCapacitor()) {
            if (!isAlgebraicOnly(sMode)) {
                localSS.diffSize =
                    (topology == Topology::RLC_PARALLEL || topology == Topology::RLC_SERIES) ? 2 :
                                                                                               1;
            }
            localSS.algSize = 1;
        } else if ((x != 0.0) && !isAlgebraicOnly(sMode)) {
            localSS.diffSize = 1;
        }
    } else if (!isOpenInPowerFlow() && (r <= 0.0)) {  // superconducting
        localSS.algSize = 1;
    }
    return localSS;
}

count_t DcLink::localJacobianCount(const SolverMode& sMode) const
{
    count_t jacCount = 0;
    if (isDynamic(sMode)) {
        if (hasCapacitor()) {
            const auto diff =
                (topology == Topology::RLC_PARALLEL || topology == Topology::RLC_SERIES) ? 2 : 1;
            jacCount = static_cast<count_t>(4 + (2 * diff));
        } else if ((x != 0.0) && !isAlgebraicOnly(sMode)) {
            jacCount = 3;
        }
    } else if (!isOpenInPowerFlow() && (r <= 0.0)) {  // superconducting
        jacCount = 2;
    }
    return jacCount;
}

void DcLink::ioPartialDerivatives(id_type_t busId,
                                  const StateData& stateData,
                                  MatrixData<double>& jacobian,
                                  const IOlocs& inputLocs,
                                  const SolverMode& sMode)
{
    // check if line is enabled
    updateLocalCache(noInputs, stateData, sMode);
    if (!(isEnabled())) {
        return;
    }

    if (currentBalance) {
        if (stateSize(sMode) > 0) {
            return;
        }
        jacobian.assignCheckCol(POUT_LOCATION, inputLocs[VOLTAGE_IN_LOCATION], 1.0 / r);
        return;
    }
    if ((busId == 2) || (busId == B2->getID())) {
        jacobian.assignCheckCol(POUT_LOCATION, inputLocs[VOLTAGE_IN_LOCATION], -Idc);
    } else {
        jacobian.assignCheckCol(POUT_LOCATION, inputLocs[VOLTAGE_IN_LOCATION], Idc);
    }
}

void DcLink::outputPartialDerivatives(id_type_t busId,
                                      const StateData& stateData,
                                      MatrixData<double>& jacobian,
                                      const SolverMode& sMode)
{
    if (!(isEnabled())) {
        return;
    }
    updateLocalCache(noInputs, stateData, sMode);

    double p1v2 = 0.0;
    double p2v1 = 0.0;
    if (!isDynamic(sMode)) {
        if ((r > 0.0) && !isOpenInPowerFlow()) {
            p1v2 = -linkInfo.v1 / r;
            p2v1 = -linkInfo.v1 / r;
        } else {
        }
    } else {  // in some other mode
    }
    // int mode = B1->getMode(sMode) * 4 + B2->getMode(sMode);

    //    md.assign(B1Voffset, B2Voffset, Q1V2);
    //    md.assign(B2Voffset, B1Voffset, Q2V1);
    if (currentBalance) {
        if ((busId == 2) || (busId == B2->getID())) {
            if (stateSize(sMode) > 0) {
                jacobian.assign(POUT_LOCATION, currentStateOffset(sMode), -1.0);
            } else if (r > 0.0) {
                const int bus1VoltageOffset = B1->getOutputLoc(sMode, VOLTAGE_IN_LOCATION);
                jacobian.assignCheckCol(POUT_LOCATION, bus1VoltageOffset, -1.0 / r);
            }
        } else {
            if (stateSize(sMode) > 0) {
                jacobian.assign(POUT_LOCATION, currentStateOffset(sMode), 1.0);
            } else if (r > 0.0) {
                const int bus2VoltageOffset = B2->getOutputLoc(sMode, VOLTAGE_IN_LOCATION);
                jacobian.assignCheckCol(POUT_LOCATION, bus2VoltageOffset, -1.0 / r);
            }
        }
        return;
    }
    if ((busId == 2) || (busId == B2->getID())) {
        if (stateSize(sMode) > 0) {
            jacobian.assign(POUT_LOCATION, currentStateOffset(sMode), -linkInfo.v2);
        } else {
            const int bus1VoltageOffset = B1->getOutputLoc(sMode, VOLTAGE_IN_LOCATION);
            jacobian.assignCheckCol(POUT_LOCATION, bus1VoltageOffset, p2v1);
        }
    } else {
        if (stateSize(sMode) > 0) {
            jacobian.assign(POUT_LOCATION, currentStateOffset(sMode), linkInfo.v1);
        } else {
            const int bus2VoltageOffset = B2->getOutputLoc(sMode, VOLTAGE_IN_LOCATION);
            jacobian.assignCheckCol(POUT_LOCATION, bus2VoltageOffset, p1v2);
        }
    }
}

count_t DcLink::outputDependencyCount(index_t num, const SolverMode& /*sMode*/) const
{
    return (num == POUT_LOCATION) ? 1 : 0;
}

void DcLink::jacobianElements(const IOdata& /*inputs*/,
                              const StateData& stateData,
                              MatrixData<double>& jacobian,
                              const IOlocs& /*inputLocs*/,
                              const SolverMode& sMode)
{
    if (hasCapacitor() && isDynamic(sMode)) {
        const int bus1VoltageOffset = B1->getOutputLoc(sMode, VOLTAGE_IN_LOCATION);
        const int bus2VoltageOffset = B2->getOutputLoc(sMode, VOLTAGE_IN_LOCATION);
        const auto currentOffset = offsets.getAlgOffset(sMode);
        const auto firstDiffOffset = offsets.getDiffOffset(sMode);
        const auto capacitorOffset = firstDiffOffset +
            ((topology == Topology::RLC_PARALLEL || topology == Topology::RLC_SERIES) ? 1 : 0);

        // Capacitor voltage constraint: vC = v1 - v2, with Idc as a branch current.
        jacobian.assign(currentOffset, capacitorOffset, 1.0);
        jacobian.assignCheckCol(currentOffset, bus1VoltageOffset, -1.0);
        jacobian.assignCheckCol(currentOffset, bus2VoltageOffset, 1.0);

        if (topology == Topology::C) {
            jacobian.assign(capacitorOffset, capacitorOffset, -stateData.cj);
            jacobian.assign(capacitorOffset, currentOffset, -1.0 / c);
        } else if (topology == Topology::RC_PARALLEL) {
            jacobian.assign(capacitorOffset, capacitorOffset, (1.0 / (r * c)) - stateData.cj);
            jacobian.assign(capacitorOffset, currentOffset, -1.0 / c);
        } else if (topology == Topology::RLC_PARALLEL) {
            jacobian.assign(firstDiffOffset, capacitorOffset, 1.0 / x);
            jacobian.assign(firstDiffOffset, firstDiffOffset, -stateData.cj);
            jacobian.assign(capacitorOffset, firstDiffOffset, 1.0 / c);
            jacobian.assign(capacitorOffset, capacitorOffset, (1.0 / (r * c)) - stateData.cj);
            jacobian.assign(capacitorOffset, currentOffset, -1.0 / c);
        } else if (topology == Topology::RC_SERIES) {
            jacobian.assign(currentOffset, currentOffset, -r);
            jacobian.assign(capacitorOffset, capacitorOffset, -stateData.cj);
            jacobian.assign(capacitorOffset, currentOffset, -1.0 / c);
        } else {  // RLC series
            jacobian.assign(currentOffset, firstDiffOffset, -1.0);
            jacobian.assign(currentOffset, currentOffset, -1.0);
            jacobian.assign(firstDiffOffset, firstDiffOffset, (-r / x) - stateData.cj);
            jacobian.assign(firstDiffOffset, capacitorOffset, -1.0 / x);
            jacobian.assignCheckCol(firstDiffOffset, bus1VoltageOffset, 1.0 / x);
            jacobian.assignCheckCol(firstDiffOffset, bus2VoltageOffset, -1.0 / x);
            jacobian.assign(capacitorOffset, firstDiffOffset, 1.0 / c);
            jacobian.assign(capacitorOffset, capacitorOffset, -stateData.cj);
        }
    } else if (stateSize(sMode) > 0) {
        const int bus1VoltageOffset = B1->getOutputLoc(sMode, VOLTAGE_IN_LOCATION);
        const int bus2VoltageOffset = B2->getOutputLoc(sMode, VOLTAGE_IN_LOCATION);
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
            if (opFlags[FIXED_TARGET_POWER]) {
                jacobian.assignCheckCol(offset,
                                        bus1VoltageOffset,
                                        -Pset / (linkInfo.v1 * linkInfo.v1));
                jacobian.assign(offset, offset, -1.0);
            }
        }
    }
}

void DcLink::residual(const IOdata& inputs,
                      const StateData& stateData,
                      double resid[],
                      const SolverMode& sMode)
{
    if (hasCapacitor() && isDynamic(sMode)) {
        updateLocalCache(inputs, stateData, sMode);
        const auto currentOffset = offsets.getAlgOffset(sMode);
        const auto firstDiffOffset = offsets.getDiffOffset(sMode);
        const auto capacitorOffset = firstDiffOffset +
            ((topology == Topology::RLC_PARALLEL || topology == Topology::RLC_SERIES) ? 1 : 0);
        const auto capacitorVoltage = stateData.state[capacitorOffset];
        const auto current = stateData.state[currentOffset];
        resid[currentOffset] = capacitorVoltage - (linkInfo.v1 - linkInfo.v2);

        if (topology == Topology::C) {
            resid[capacitorOffset] = (-current / c) - stateData.dstate_dt[capacitorOffset];
        } else if (topology == Topology::RC_PARALLEL) {
            resid[capacitorOffset] =
                (-(current - (capacitorVoltage / r)) / c) - stateData.dstate_dt[capacitorOffset];
        } else if (topology == Topology::RLC_PARALLEL) {
            const auto inductorCurrent = stateData.state[firstDiffOffset];
            resid[firstDiffOffset] = (capacitorVoltage / x) - stateData.dstate_dt[firstDiffOffset];
            resid[capacitorOffset] =
                (-(current - (capacitorVoltage / r) - inductorCurrent) / c) -
                stateData.dstate_dt[capacitorOffset];
        } else if (topology == Topology::RC_SERIES) {
            resid[currentOffset] -= r * current;
            resid[capacitorOffset] = (-current / c) - stateData.dstate_dt[capacitorOffset];
        } else {
            const auto inductorCurrent = stateData.state[firstDiffOffset];
            resid[currentOffset] = -inductorCurrent - current;
            resid[firstDiffOffset] =
                ((linkInfo.v1 - linkInfo.v2 - (r * inductorCurrent) - capacitorVoltage) / x) -
                stateData.dstate_dt[firstDiffOffset];
            resid[capacitorOffset] = (inductorCurrent / c) - stateData.dstate_dt[capacitorOffset];
        }
    } else if (stateSize(sMode) > 0) {
        updateLocalCache(inputs, stateData, sMode);
        if (isDynamic(sMode)) {
            auto offset = offsets.getDiffOffset(sMode);
            resid[offset] = ((linkInfo.v1 - linkInfo.v2 - (r * stateData.state[offset])) / x) -
                stateData.dstate_dt[offset];
        } else {
            auto offset = offsets.getAlgOffset(sMode);
            if (opFlags[FIXED_TARGET_POWER]) {
                resid[offset] =
                    (linkInfo.v1 - linkInfo.v2) + (Pset / linkInfo.v1) - stateData.state[offset];
            } else {
                resid[offset] = linkInfo.v1 - linkInfo.v2;
            }
        }
    }
}

void DcLink::setState(CoreTime time,
                      const double state[],
                      const double dstateDt[],
                      const SolverMode& sMode)
{
    if (hasCapacitor() && isDynamic(sMode)) {
        const auto firstDiffOffset = offsets.getDiffOffset(sMode);
        const auto differentialCount =
            (topology == Topology::RLC_PARALLEL || topology == Topology::RLC_SERIES) ? 2U : 1U;
        for (unsigned int ii = 0; ii < differentialCount; ++ii) {
            m_state[ii] = state[firstDiffOffset + ii];
            m_dstate_dt[ii] = dstateDt[firstDiffOffset + ii];
        }
        Idc = state[offsets.getAlgOffset(sMode)];
    } else if (stateSize(sMode) > 0) {
        if (isDynamic(sMode)) {
            auto offset = offsets.getDiffOffset(sMode);
            m_state[0] = state[offset];
            m_dstate_dt[0] = dstateDt[offset];
            Idc = m_state[0];
        } else {
            auto offset = offsets.getAlgOffset(sMode);
            Idc = state[offset];
        }
    }
    prevTime = time;
}

void DcLink::guessState(const CoreTime /*time*/,
                        double state[],
                        double dstateDt[],
                        const SolverMode& sMode)
{
    if (hasCapacitor() && isDynamic(sMode)) {
        const auto firstDiffOffset = offsets.getDiffOffset(sMode);
        for (unsigned int ii = 0; ii < m_state.size(); ++ii) {
            state[firstDiffOffset + ii] = m_state[ii];
            dstateDt[firstDiffOffset + ii] = m_dstate_dt[ii];
        }
        state[offsets.getAlgOffset(sMode)] = Idc;
    } else if (stateSize(sMode) > 0) {
        if (isDynamic(sMode)) {
            auto offset = offsets.getDiffOffset(sMode);
            state[offset] = m_state[0];
            dstateDt[offset] = m_dstate_dt[0];
        } else {
            auto offset = offsets.getAlgOffset(sMode);
            state[offset] = Idc;
        }
    }
}

void DcLink::getStateName(stringVec& stNames,
                          const SolverMode& sMode,
                          const std::string& prefix) const
{
    if (hasCapacitor() && isDynamic(sMode)) {
        const std::string prefix2 = prefix + getName() + ':';
        const auto firstDiffOffset = offsets.getDiffOffset(sMode);
        if (topology == Topology::RLC_PARALLEL || topology == Topology::RLC_SERIES) {
            stNames[firstDiffOffset] = prefix2 + "il";
            stNames[firstDiffOffset + 1] = prefix2 + "vc";
        } else {
            stNames[firstDiffOffset] = prefix2 + "vc";
        }
        stNames[offsets.getAlgOffset(sMode)] = prefix2 + "idc";
    } else if (stateSize(sMode) > 0) {
        const std::string prefix2 = prefix + getName() + ':';
        auto offset =
            (isDynamic(sMode)) ? offsets.getDiffOffset(sMode) : offsets.getAlgOffset(sMode);
        stNames[offset] = prefix2 + "idc";
    }
}

void DcLink::updateLocalCache(const IOdata& /*inputs*/,
                              const StateData& stateData,
                              const SolverMode& sMode)
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
        Idc = stateData.state[currentStateOffset(sMode)];
    } else {
        if (isOpenInPowerFlow()) {
            Idc = 0.0;
        } else if (r > 0) {
            Idc = (linkInfo.v1 - linkInfo.v2) / r;

            //    Q2 = P2*sqrt(k3sq2*k3sq2 - gamma*gamma);
        } else {
            Idc = Pset / linkInfo.v1;
        }
    }
    linkFlows.P1 = currentBalance ? Idc : linkInfo.v1 * Idc;
    linkFlows.P2 = currentBalance ? -Idc : -linkInfo.v2 * Idc;
}

void DcLink::updateLocalCache()
{
    linkInfo = {};

    if (isEnabled()) {
        linkInfo.v1 = B1->getVoltage();
        linkInfo.v2 = B2->getVoltage();
        linkFlows.P1 = currentBalance ? Idc : linkInfo.v1 * Idc;
        linkFlows.P2 = currentBalance ? -Idc : -linkInfo.v2 * Idc;
    }
}

int DcLink::fixRealPower(double power,
                         id_type_t measureTerminal,
                         id_type_t fixedTerminal,
                         units::unit unitType)
{
    int ret = 0;
    opFlags.set(FIXED_TARGET_POWER);
    if (fixedTerminal == 0) {
        fixedTerminal = measureTerminal;
    }
    Pset = convert(power, unitType, puMW, systemBasePower);
    if ((fixedTerminal == 2) || (fixedTerminal == B2->getID())) {
        if (B2->getType() == GridBus::BusType::SLK) {
            linkInfo.v2 = B2->getVoltage();
            Idc = power / linkInfo.v2;
            const double bus1Voltage = linkInfo.v2 - (Idc * r);
            B1->setVoltageAngle(bus1Voltage, 0);
            updateLocalCache();
            return B1->propogatePower(true);
        }
        if (B1->getType() == GridBus::BusType::SLK) {
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
        if (B1->getType() == GridBus::BusType::SLK) {
            linkInfo.v1 = B1->getVoltage();
            Idc = power / linkInfo.v1;
            const double bus2Voltage = linkInfo.v1 - (Idc * r);
            B2->setVoltageAngle(bus2Voltage, 0);
            updateLocalCache();
            return B2->propogatePower(true);
        }
        if (B2->getType() == GridBus::BusType::SLK) {
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

int DcLink::fixPower(double power,
                     double /*qPower*/,
                     id_type_t measureTerminal,
                     id_type_t fixedTerminal,
                     units::unit unitType)
{
    return fixRealPower(power, measureTerminal, fixedTerminal, unitType);
}

}  // namespace griddyn::links
