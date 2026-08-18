/*
 * Copyright (c) 2014-2026, Lawrence Livermore National Security
 * See the top-level NOTICE for additional details. All rights reserved.
 * SPDX-License-Identifier: BSD-3-Clause
 */

#include "VSCShunt.h"

#include "../GridBus.h"
#include "../primary/DcBus.h"
#include "core/CoreExceptions.h"
#include "core/CoreObjectTemplates.hpp"
#include "gmlc/utilities/stringOps.h"
#include <cmath>
#include <queue>
#include <string>

namespace griddyn::links {
using units::unit;

namespace {
    constexpr index_t ashIndex = 0;
    constexpr index_t vshIndex = 1;
    constexpr index_t pshIndex = 2;
    constexpr index_t qshIndex = 3;
    constexpr index_t pdcIndex = 4;
}  // namespace

VSCShunt::VSCShunt(const std::string& objName): AcDcConverter(objName) {}

CoreObject* VSCShunt::clone(CoreObject* obj) const
{
    auto* nobj = cloneBase<VSCShunt, AcDcConverter>(this, obj);
    if (nobj == nullptr) {
        return obj;
    }
    nobj->control = control;
    nobj->v0 = v0;
    nobj->p0 = p0;
    nobj->q0 = q0;
    nobj->vdc0 = vdc0;
    nobj->k0 = k0;
    nobj->k1 = k1;
    nobj->k2 = k2;
    nobj->droop = droop;
    nobj->droopK = droopK;
    nobj->vhigh = vhigh;
    nobj->vlow = vlow;
    nobj->vshmax = vshmax;
    nobj->vshmin = vshmin;
    nobj->ishmax = ishmax;
    nobj->currentBalance = currentBalance;
    nobj->ash = ash;
    nobj->vsh = vsh;
    nobj->psh = psh;
    nobj->qsh = qsh;
    nobj->pdc = pdc;
    return nobj;
}

bool VSCShunt::isConnected() const
{
    return AcDcConverter::isConnected() && (dcReference != nullptr);
}

GridBus* VSCShunt::getBus(index_t busInd) const
{
    if (busInd == 3) {
        return dcReference;
    }
    return AcDcConverter::getBus(busInd);
}

void VSCShunt::followNetwork(int network, std::queue<GridBus*>& stk)
{
    // AcDcConverter intentionally clears NETWORK_CONNECTED so conventional
    // two-terminal converters do not merge AC and DC islands.  VSCShunt has
    // an explicit DC reference terminal and algebraic equations spanning all
    // three terminals, so its complete electrical island must be traversed.
    if (!isConnected()) {
        return;
    }
    for (auto* bus : {B1, B2, static_cast<GridBus*>(dcReference)}) {
        if ((bus != nullptr) && (bus->Network != network)) {
            stk.push(bus);
        }
    }
}

void VSCShunt::updateBus(GridBus* bus, index_t busnumber)
{
    if (busnumber == 3) {
        auto* dcBus = dynamic_cast<DcBus*>(bus);
        if (dcBus == nullptr) {
            throw(UnrecognizedObjectException(this));
        }
        if (dcReference != nullptr) {
            dcReference->remove(this);
        }
        dcReference = dcBus;
        dcReference->add(this);
        return;
    }
    AcDcConverter::updateBus(bus, busnumber);
}

void VSCShunt::set(std::string_view param, std::string_view val)
{
    const auto lower = gmlc::utilities::convertToLowerCase(val);
    if (param == "control") {
        if (lower == "pq") {
            control = Control::PQ;
        } else if (lower == "pv") {
            control = Control::PV;
        } else if ((lower == "vq") || (lower == "v_q")) {
            control = Control::VQ;
        } else if ((lower == "vv") || (lower == "v_v")) {
            control = Control::VV;
        } else {
            throw(InvalidParameterValue(val));
        }
        return;
    }
    AcDcConverter::set(param, val);
}

void VSCShunt::set(std::string_view param, double val, unit unitType)
{
    if (param == "control") {
        const auto controlValue = static_cast<int>(val);
        if ((controlValue < 0) || (controlValue > 3)) {
            throw(InvalidParameterValue("control"));
        }
        control = static_cast<Control>(controlValue);
    } else if (param == "v0") {
        v0 = val;
    } else if (param == "p0") {
        p0 = val;
    } else if (param == "q0") {
        q0 = val;
    } else if (param == "vdc0") {
        vdc0 = val;
    } else if (param == "k0") {
        k0 = val;
    } else if (param == "k1") {
        k1 = val;
    } else if (param == "k2") {
        k2 = val;
    } else if (param == "droop") {
        droop = val;
    } else if (param == "k") {
        droopK = val;
    } else if (param == "vhigh") {
        vhigh = val;
    } else if (param == "vlow") {
        vlow = val;
    } else if (param == "vshmax") {
        vshmax = val;
    } else if (param == "vshmin") {
        vshmin = val;
    } else if ((param == "ishmax") || (param == "imax")) {
        ishmax = val;
    } else if ((param == "andes_current_balance") || (param == "current_balance")) {
        currentBalance = (val > 0.5);
    } else {
        AcDcConverter::set(param, val, unitType);
    }
}

void VSCShunt::pFlowObjectInitializeA(CoreTime time0, std::uint32_t flags)
{
    Link::pFlowObjectInitializeA(time0, flags);
    ash = B1->getAngle();
    vsh = v0;
    psh = p0;
    qsh = q0;
    pdc = 0.0;
    offsets.local().local.algSize = 5;
    offsets.local().local.jacSize = localJacobianCount(cPflowSolverMode);
    updateLocalCache();
}

void VSCShunt::dynObjectInitializeA(CoreTime time0, std::uint32_t flags)
{
    // ANDES' VSCShunt is a static power-flow component.  Retain its algebraic
    // equations in DAE mode, without inheriting AcDcConverter's unrelated
    // firing-angle controller states.
    Link::dynObjectInitializeA(time0, flags);
    offsets.local().local.algSize = 5;
    offsets.local().local.jacSize = localJacobianCount(cDaeSolverMode);
    updateLocalCache();
}

StateSizes VSCShunt::localStateSizes(const SolverMode& sMode) const
{
    StateSizes sizes;
    if (hasAlgebraic(sMode)) {
        sizes.algSize = 5;
    }
    return sizes;
}

count_t VSCShunt::localJacobianCount(const SolverMode& sMode) const
{
    return hasAlgebraic(sMode) ? 19 : 0;
}

double VSCShunt::dcVoltageDifference() const
{
    return B2->getVoltage() - static_cast<GridBus*>(dcReference)->getVoltage();
}

void VSCShunt::updateFlows(double dcVoltage, double dcReferenceVoltage)
{
    linkFlows.P1 = -psh;
    linkFlows.Q1 = -qsh;
    const double difference = dcVoltage - dcReferenceVoltage;
    if (std::abs(difference) < 1e-12) {
        linkFlows.P2 = 0.0;
        linkFlows.Q2 = 0.0;
        return;
    }
    linkFlows.P2 = currentBalance ? -pdc / difference : -pdc * dcVoltage / difference;
    linkFlows.Q2 = 0.0;
}

void VSCShunt::updateLocalCache()
{
    if (!isEnabled() || !isConnected()) {
        return;
    }
    linkInfo = {};
    linkInfo.v1 = B1->getVoltage();
    linkInfo.v2 = B2->getVoltage();
    linkInfo.theta1 = B1->getAngle();
    updateFlows(linkInfo.v2, static_cast<GridBus*>(dcReference)->getVoltage());
}

void VSCShunt::updateLocalCache(const IOdata& /*inputs*/,
                                const StateData& stateDataValue,
                                const SolverMode& sMode)
{
    if (!isEnabled() || !isConnected() || !stateDataValue.updateRequired(linkInfo.seqID)) {
        return;
    }
    linkInfo = {};
    linkInfo.seqID = stateDataValue.seqID;
    linkInfo.v1 = B1->getVoltage(stateDataValue, sMode);
    linkInfo.v2 = B2->getVoltage(stateDataValue, sMode);
    linkInfo.theta1 = B1->getAngle(stateDataValue, sMode);
    const auto offset = offsets.getAlgOffset(sMode);
    ash = stateDataValue.state[offset + ashIndex];
    vsh = stateDataValue.state[offset + vshIndex];
    psh = stateDataValue.state[offset + pshIndex];
    qsh = stateDataValue.state[offset + qshIndex];
    pdc = stateDataValue.state[offset + pdcIndex];
    updateFlows(linkInfo.v2, dcReference->getVoltage(stateDataValue, sMode));
}

void VSCShunt::ioPartialDerivatives(id_type_t busId,
                                    const StateData& stateDataValue,
                                    MatrixData<double>& matrixDataValue,
                                    const IOlocs& inputLocs,
                                    const SolverMode& sMode)
{
    if (!isEnabled() || (inputLocs[VOLTAGE_IN_LOCATION] == kNullLocation)) {
        return;
    }
    updateLocalCache(noInputs, stateDataValue, sMode);
    const auto difference =
        B2->getVoltage(stateDataValue, sMode) - dcReference->getVoltage(stateDataValue, sMode);
    if (std::abs(difference) < 1e-12) {
        return;
    }
    if (busId == B2->getID()) {
        matrixDataValue.assign(POUT_LOCATION,
                               inputLocs[VOLTAGE_IN_LOCATION],
                               currentBalance ?
                                   pdc / (difference * difference) :
                                   pdc * dcReference->getVoltage(stateDataValue, sMode) /
                                       (difference * difference));
    } else if (busId == dcReference->getID()) {
        matrixDataValue.assign(POUT_LOCATION,
                               inputLocs[VOLTAGE_IN_LOCATION],
                               currentBalance ? pdc / (difference * difference) :
                                                pdc * B2->getVoltage(stateDataValue, sMode) /
                                       (difference * difference));
    }
}

void VSCShunt::outputPartialDerivatives(id_type_t busId,
                                        const StateData& stateDataValue,
                                        MatrixData<double>& matrixDataValue,
                                        const SolverMode& sMode)
{
    if (!isEnabled()) {
        return;
    }
    updateLocalCache(noInputs, stateDataValue, sMode);
    const auto offset = offsets.getAlgOffset(sMode);
    if (busId == B1->getID()) {
        matrixDataValue.assign(POUT_LOCATION, offset + pshIndex, -1.0);
        matrixDataValue.assign(QOUT_LOCATION, offset + qshIndex, -1.0);
        return;
    }
    const auto difference =
        B2->getVoltage(stateDataValue, sMode) - dcReference->getVoltage(stateDataValue, sMode);
    if (std::abs(difference) < 1e-12) {
        return;
    }
    if (busId == B2->getID()) {
        matrixDataValue.assign(POUT_LOCATION,
                               offset + pdcIndex,
                               currentBalance ?
                                   -1.0 / difference :
                                   -B2->getVoltage(stateDataValue, sMode) / difference);
    } else if (busId == dcReference->getID()) {
        matrixDataValue.assign(POUT_LOCATION,
                               offset + pdcIndex,
                               currentBalance ?
                                   1.0 / difference :
                                   dcReference->getVoltage(stateDataValue, sMode) / difference);
    }
}

count_t VSCShunt::outputDependencyCount(index_t /*num*/, const SolverMode& sMode) const
{
    return hasAlgebraic(sMode) ? 2 : 0;
}

void VSCShunt::jacobianElements(const IOdata& /*inputs*/,
                                const StateData& stateDataValue,
                                MatrixData<double>& matrixDataValue,
                                const IOlocs& /*inputLocs*/,
                                const SolverMode& sMode)
{
    updateLocalCache(noInputs, stateDataValue, sMode);
    const auto offset = offsets.getAlgOffset(sMode);
    const auto busVoltage = B1->getOutputLoc(sMode, VOLTAGE_IN_LOCATION);
    const auto busAngle = B1->getOutputLoc(sMode, ANGLE_IN_LOCATION);
    const auto dcVoltage = B2->getOutputLoc(sMode, VOLTAGE_IN_LOCATION);
    const auto dcReferenceVoltage = dcReference->getOutputLoc(sMode, VOLTAGE_IN_LOCATION);
    const auto voltage = linkInfo.v1;
    const auto angleDifference = linkInfo.theta1 - ash;
    const auto cosine = std::cos(angleDifference);
    const auto sine = std::sin(angleDifference);
    const auto denominator = (r * r) + (x * x);
    const auto conductance = r / denominator;
    const auto susceptance = -x / denominator;

    matrixDataValue.assignCheckCol(offset + ashIndex,
                                   busVoltage,
                                   (2.0 * conductance * voltage) - (conductance * vsh * cosine) -
                                       (susceptance * vsh * sine));
    const auto dpdAngle =
        (conductance * voltage * vsh * sine) - (susceptance * voltage * vsh * cosine);
    matrixDataValue.assignCheckCol(offset + ashIndex, busAngle, dpdAngle);
    matrixDataValue.assign(offset + ashIndex, offset + ashIndex, -dpdAngle);
    matrixDataValue.assign(offset + ashIndex,
                           offset + vshIndex,
                           (-conductance * voltage * cosine) - (susceptance * voltage * sine));
    matrixDataValue.assign(offset + ashIndex, offset + pshIndex, -1.0);

    matrixDataValue.assignCheckCol(offset + vshIndex,
                                   busVoltage,
                                   (-2.0 * susceptance * voltage) - (conductance * vsh * sine) +
                                       (susceptance * vsh * cosine));
    const auto dqdAngle =
        (-conductance * voltage * vsh * cosine) - (susceptance * voltage * vsh * sine);
    matrixDataValue.assignCheckCol(offset + vshIndex, busAngle, dqdAngle);
    matrixDataValue.assign(offset + vshIndex, offset + ashIndex, -dqdAngle);
    matrixDataValue.assign(offset + vshIndex,
                           offset + vshIndex,
                           (-conductance * voltage * sine) + (susceptance * voltage * cosine));
    matrixDataValue.assign(offset + vshIndex, offset + qshIndex, -1.0);

    if ((control == Control::PQ) || (control == Control::PV)) {
        matrixDataValue.assign(offset + pshIndex, offset + pshIndex, -1.0);
    } else {
        matrixDataValue.assignCheckCol(offset + pshIndex, dcVoltage, 1.0);
        matrixDataValue.assignCheckCol(offset + pshIndex, dcReferenceVoltage, -1.0);
    }
    if ((control == Control::PQ) || (control == Control::VQ)) {
        matrixDataValue.assign(offset + qshIndex, offset + qshIndex, -1.0);
    } else {
        matrixDataValue.assignCheckCol(offset + qshIndex, busVoltage, -1.0);
    }

    matrixDataValue.assignCheckCol(offset + pdcIndex,
                                   busVoltage,
                                   (-conductance * vsh * cosine) + (susceptance * vsh * sine));
    const auto dpdcAngle =
        (conductance * voltage * vsh * sine) + (susceptance * voltage * vsh * cosine);
    matrixDataValue.assignCheckCol(offset + pdcIndex, busAngle, dpdcAngle);
    matrixDataValue.assign(offset + pdcIndex, offset + ashIndex, -dpdcAngle);
    matrixDataValue.assign(offset + pdcIndex,
                           offset + vshIndex,
                           (2.0 * conductance * vsh) - (conductance * voltage * cosine) +
                               (susceptance * voltage * sine));
    matrixDataValue.assign(offset + pdcIndex, offset + pdcIndex, 1.0);
}

void VSCShunt::residual(const IOdata& inputs,
                        const StateData& stateDataValue,
                        double resid[],
                        const SolverMode& sMode)
{
    updateLocalCache(inputs, stateDataValue, sMode);
    const auto offset = offsets.getAlgOffset(sMode);
    const auto voltage = linkInfo.v1;
    const auto angleDifference = linkInfo.theta1 - ash;
    const auto cosine = std::cos(angleDifference);
    const auto sine = std::sin(angleDifference);
    const auto denominator = (r * r) + (x * x);
    const auto conductance = r / denominator;
    const auto susceptance = -x / denominator;
    const auto dcDifference =
        B2->getVoltage(stateDataValue, sMode) - dcReference->getVoltage(stateDataValue, sMode);

    resid[offset + ashIndex] = (conductance * voltage * voltage) -
        (conductance * voltage * vsh * cosine) - (susceptance * voltage * vsh * sine) - psh;
    resid[offset + vshIndex] = (-susceptance * voltage * voltage) -
        (conductance * voltage * vsh * sine) + (susceptance * voltage * vsh * cosine) - qsh;
    resid[offset + pshIndex] =
        ((control == Control::PQ) || (control == Control::PV)) ? (p0 - psh) : (dcDifference - vdc0);
    resid[offset + qshIndex] =
        ((control == Control::PQ) || (control == Control::VQ)) ? (q0 - qsh) : (v0 - voltage);
    resid[offset + pdcIndex] = (conductance * vsh * vsh) - (conductance * voltage * vsh * cosine) +
        (susceptance * voltage * vsh * sine) + pdc;
}

void VSCShunt::setState(CoreTime time,
                        const double state[],
                        const double /*dstateDt*/[],
                        const SolverMode& sMode)
{
    const auto offset = offsets.getAlgOffset(sMode);
    ash = state[offset + ashIndex];
    vsh = state[offset + vshIndex];
    psh = state[offset + pshIndex];
    qsh = state[offset + qshIndex];
    pdc = state[offset + pdcIndex];
    prevTime = time;
    updateLocalCache();
}

void VSCShunt::guessState(CoreTime /*time*/,
                          double state[],
                          double /*dstateDt*/[],
                          const SolverMode& sMode)
{
    const auto offset = offsets.getAlgOffset(sMode);
    state[offset + ashIndex] = ash;
    state[offset + vshIndex] = vsh;
    state[offset + pshIndex] = psh;
    state[offset + qshIndex] = qsh;
    state[offset + pdcIndex] = pdc;
}

void VSCShunt::getStateName(stringVec& stNames,
                            const SolverMode& sMode,
                            const std::string& prefix) const
{
    const auto offset = offsets.getAlgOffset(sMode);
    const std::string statePrefix = prefix + getName() + ':';
    stNames[offset + ashIndex] = statePrefix + "ash";
    stNames[offset + vshIndex] = statePrefix + "vsh";
    stNames[offset + pshIndex] = statePrefix + "psh";
    stNames[offset + qshIndex] = statePrefix + "qsh";
    stNames[offset + pdcIndex] = statePrefix + "pdc";
}

double VSCShunt::getRealPower(id_type_t busId) const
{
    if ((busId == 3) || ((dcReference != nullptr) && (busId == dcReference->getID()))) {
        const auto difference = dcVoltageDifference();
        if (std::abs(difference) < 1e-12) {
            return 0.0;
        }
        if (currentBalance) {
            return pdc / difference;
        }
        return pdc * static_cast<GridBus*>(dcReference)->getVoltage() / difference;
    }
    return AcDcConverter::getRealPower(busId);
}

double VSCShunt::getReactivePower(id_type_t busId) const
{
    if ((busId == 3) || ((dcReference != nullptr) && (busId == dcReference->getID()))) {
        return 0.0;
    }
    return AcDcConverter::getReactivePower(busId);
}

}  // namespace griddyn::links
