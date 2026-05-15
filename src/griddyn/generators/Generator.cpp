/*
 * Copyright (c) 2014-2026, Lawrence Livermore National Security
 * See the top-level NOTICE for additional details. All rights reserved.
 * SPDX-License-Identifier: BSD-3-Clause
 */

#include "../controllers/scheduler.h"
#include "../gridBus.h"
#include "../measurement/objectGrabbers.h"
#include "core/coreExceptions.h"
#include "core/coreObjectTemplates.hpp"
#include "core/objectFactoryTemplates.hpp"
#include "core/objectInterpreter.h"
#include "gmlc/utilities/stringOps.h"
#include "gmlc/utilities/vectorOps.hpp"
#include "utilities/OperatingBoundary.h"
#include "utilities/matrixData.hpp"
#include "variableGenerator.h"
#include <memory>
#include <string>
#include <vector>

// #include <set>
/*
For the dynamics states order matters for entries used across
multiple components and other parts of the program.

genModel
[theta, voltage, Id, Iq, delta, w]

exciter
[Ef]

governor --- Pm(t0) = Pset is stored externally as well
[Pm]
*/

namespace griddyn {
static typeFactory<Generator> gf("generator",
                                 std::to_array<std::string_view>({"basic", "simple", "pflow"}));
static childTypeFactory<DynamicGenerator, Generator>
    dgf("generator", std::to_array<std::string_view>({"dynamic", "spinning"}), "dynamic");
static childTypeFactory<variableGenerator, Generator>
    vgf("generator", std::to_array<std::string_view>({"variable", "renewable"}));
using units::convert;
using units::MVAR;
using units::MW;
using units::puHz;
using units::puMW;
using units::puV;
using units::rad;
using units::second;
using units::unit;

std::atomic<count_t> Generator::genCount(0);
// default bus object

Generator::Generator(const std::string& objName): gridSecondary(objName)
{
    setUserID(++genCount);
    updateName();
    opFlags.set(adjustable_P);
    opFlags.set(adjustable_Q);
    opFlags.set(local_voltage_control);
    opFlags.set(local_power_control);
}

Generator::~Generator() = default;

coreObject* Generator::clone(coreObject* obj) const
{
    auto* gen = cloneBaseFactory<Generator, gridSecondary>(this, obj, &gf);
    if (gen == nullptr) {
        return obj;
    }

    gen->P = P;
    gen->Q = Q;
    gen->Pset = Pset;
    gen->Qmax = Qmax;
    gen->Qmin = Qmin;
    gen->Pmax = Pmax;
    gen->Pmin = Pmin;
    gen->dPdt = dPdt;
    gen->dQdt = dQdt;
    gen->machineBasePower = machineBasePower;
    gen->participation = participation;
    gen->m_Rs = m_Rs;
    gen->m_Xs = m_Xs;
    gen->m_Vtarget = m_Vtarget;
    gen->vRegFraction = vRegFraction;
    return gen;
}

void Generator::pFlowObjectInitializeA(coreTime time0, std::uint32_t flags)
{
    if (isConnected() && isEnabled()) {
        if (opFlags[local_voltage_control]) {
            if (bus->getType() != gridBus::busType::PQ) {
                bus->registerVoltageControl(this);
                opFlags.reset(indirect_voltage_control);
            } else if (opFlags[indirect_voltage_control]) {
                remoteBus = bus;
                if (m_Vtarget < 0.6) {
                    m_Vtarget = remoteBus->get("vtarget");
                }
                remoteBus->registerVoltageControl(this);
            }
        } else if (opFlags[remote_voltage_control]) {
            if (m_Vtarget < 0.6) {
                m_Vtarget = remoteBus->get("vtarget");
            }
            remoteBus->registerVoltageControl(this);
            if (remoteBus->getType() == gridBus::busType::PQ) {
                opFlags.set(indirect_voltage_control);
            }
        }
        // load up power control
        if (opFlags[local_power_control]) {
            if (bus->getType() != gridBus::busType::PQ) {
                bus->registerPowerControl(this);
                opFlags.reset(indirect_voltage_control);
            }
        } else if (opFlags[remote_power_control]) {
            // remote bus already configured
            remoteBus->registerPowerControl(this);
        }
        if (Pset < -kHalfBigNum) {
            Pset = P;
        }
    } else {
        P = 0.0;
        Q = 0.0;
    }

    gridSecondary::pFlowObjectInitializeA(time0, flags);
}

void Generator::dynObjectInitializeA(coreTime time0, std::uint32_t flags)
{
    if (machineBasePower < 0.0) {
        machineBasePower = systemBasePower;
    }

    gridSecondary::dynObjectInitializeA(time0, flags);
}

stateSizes Generator::LocalStateSizes(const solverMode& sMode) const
{
    stateSizes localStates;
    if (!isEnabled()) {
        return localStates;
    }
    if (isPowerFlow(sMode)) {
        if ((isAC(sMode)) && (opFlags[indirect_voltage_control])) {
            localStates.algSize = 1;
        }
    }
    return localStates;
}

count_t Generator::LocalJacobianCount(const solverMode& sMode) const
{
    if (!isEnabled()) {
        return 0;
    }
    if (isPowerFlow(sMode)) {
        if ((isAC(sMode)) && (opFlags[indirect_voltage_control])) {
            return 2;
        }
    }
    return 0;
}

// initial conditions of dynamic states
void Generator::dynObjectInitializeB(const IOdata& /*inputs*/,
                                     const IOdata& desiredOutput,
                                     IOdata& fieldSet)
{
    if (desiredOutput.empty()) {
    } else {
        if (desiredOutput.size() > PoutLocation) {
            if (desiredOutput[PoutLocation] > -100000) {
                P = desiredOutput[PoutLocation];
            }
        }

        if (desiredOutput.size() > QoutLocation) {
            if (desiredOutput[QoutLocation] > -100000) {
                Q = desiredOutput[QoutLocation];
            }
        }
    }
    if (std::abs(P) > 1.2 * machineBasePower) {
        logging::warning(
            this,
            "Requested Power output significantly greater than internal base power, may cause dynamic "
            "model instability, suggest updating base power");
    }
    Pset = P;
    fieldSet.resize(2);
    fieldSet[PoutLocation] = -P;
    fieldSet[QoutLocation] = -Q;
}

// save an external state to the internal one
void Generator::setState(coreTime time,
                         const double state[],
                         const double /*dstate_dt*/[],
                         const solverMode& sMode)
{
    if (isDynamic(sMode)) {
        Pset += dPdt * (time - prevTime);
        Pset = gmlc::utilities::valLimit(Pset, Pmin, Pmax);
    } else if (stateSize(sMode) > 0) {
        auto offset = offsets.getAlgOffset(sMode);
        Q = -state[offset];
    }
    prevTime = time;
}

// copy the current state to a vector
void Generator::guessState(coreTime /*time*/,
                           double state[],
                           double /*dstate_dt*/[],
                           const solverMode& sMode)
{
    if ((!isDynamic(sMode)) && (stateSize(sMode) > 0)) {
        auto offset = offsets.getAlgOffset(sMode);
        state[offset] = -Q;
    }
}

void Generator::add(coreObject* obj)
{
    if (dynamic_cast<gridSubModel*>(obj) != nullptr) {
        return add(static_cast<gridSubModel*>(obj));
    }
    if (dynamic_cast<gridBus*>(obj) != nullptr) {
        setRemoteBus(obj);
    } else {
        throw(unrecognizedObjectException(this));
    }
}

void Generator::add(gridSubModel* obj)
{
    if (dynamic_cast<scheduler*>(obj) != nullptr) {
        sched = static_cast<scheduler*>(obj);
    } else {
        throw(unrecognizedObjectException(this));
    }
}

void Generator::setRemoteBus(coreObject* newRemoteBus)
{
    auto* newRbus = dynamic_cast<gridBus*>(newRemoteBus);
    if (newRbus == nullptr) {
        return;
    }
    if (isSameObject(newRbus, remoteBus)) {
        return;
    }
    auto* prevRbus = remoteBus;
    remoteBus = newRbus;
    // update the flags as appropriate
    if (isSameObject(remoteBus, getParent())) {
        opFlags.reset(remote_voltage_control);
        opFlags.set(local_voltage_control);
        opFlags.reset(has_powerflow_adjustments);
    } else {
        opFlags.set(remote_voltage_control);
        opFlags.reset(local_voltage_control);
        opFlags.set(has_powerflow_adjustments);
    }

    if (opFlags[pFlow_initialized]) {
        if (opFlags[adjustable_Q]) {
            remoteBus->registerVoltageControl(this);
            if (prevRbus != nullptr) {
                prevRbus->removeVoltageControl(this);
            }
        }
        if (opFlags[adjustable_P]) {
            remoteBus->registerPowerControl(this);
            if (prevRbus != nullptr) {
                prevRbus->removePowerControl(this);
            }
        }
    }
}
// set properties
void Generator::set(std::string_view param, std::string_view val)
{
    if (param == "remote") {
        setRemoteBus(locateObject(std::string{val}, getRoot(), false));
    } else if (param == "remote_power_control") {
        opFlags.set(remote_power_control);
        opFlags.reset(local_power_control);
    } else if (param == "p") {
        if (val == "max") {
            P = Pmax;
        } else if (val == "min") {
            P = Pmin;
        } else {
            throw(invalidParameterValue(val));
        }
    } else if (param == "q") {
        if (val == "max") {
            Q = Qmax;
        } else if (val == "min") {
            Q = Qmin;
        } else {
            throw(invalidParameterValue(val));
        }
    } else {
        gridSecondary::set(param, val);
    }
}

double Generator::get(std::string_view param, unit unitType) const
{
    double ret = kNullVal;
    if (param == "vcontrolfrac") {
        ret = vRegFraction;
    } else if (param == "vtarget") {
        ret = m_Vtarget;
    } else if (param == "participation") {
        ret = participation;
    } else if (param == "pset") {
        ret = convert(getPset(), puMW, unitType, systemBasePower, localBaseVoltage);
    } else if (param == "pmax") {
        ret = convert(getPmax(), puMW, unitType, systemBasePower, localBaseVoltage);
    } else if (param == "pmin") {
        ret = convert(getPmin(), puMW, unitType, systemBasePower, localBaseVoltage);
    } else if (param == "qmax") {
        ret = convert(getQmax(), puMW, unitType, systemBasePower, localBaseVoltage);
    } else if (param == "qmin") {
        ret = convert(getQmin(), puMW, unitType, systemBasePower, localBaseVoltage);
    } else if (auto fptr = getObjectFunction(this, std::string{param}).first) {
        auto unit = getObjectFunction(this, std::string{param}).second;
        coreObject* tobj = const_cast<Generator*>(this);
        ret = convert(fptr(tobj), unit, unitType, systemBasePower, localBaseVoltage);
    } else {
        ret = gridSecondary::get(param, unitType);
    }
    return ret;
}

void Generator::timestep(coreTime time, const IOdata& inputs, const solverMode& /*sMode*/)
{
    if (Pset < -kHalfBigNum) {
        Pset = P;
    }
    auto dt = time - prevTime;
    Pset = Pset + dPdt * dt;
    Pset = (Pset > getPmax(dt)) ? getPmax(dt) : ((Pset < getPmin(dt)) ? getPmin(dt) : Pset);

    P = Pset;
    Q = Q + dQdt * dt;
    Q = (Q > getQmax(dt, Pset)) ? getQmax(dt, Pset) :
                                  ((Q < getQmin(dt, Pset)) ? getQmin(dt, Pset) : Q);
    if (inputs[voltageInLocation] < 0.8) {
        if (!opFlags[no_voltage_derate]) {
            P = P * (inputs[voltageInLocation] * 1.25);
            Q = Q * (inputs[voltageInLocation] * 1.25);
        }
    }

    // use this as the temporary state storage
    prevTime = time;
}

change_code Generator::powerFlowAdjust(const IOdata& /*inputs*/,
                                       std::uint32_t /*flags*/,
                                       check_level_t /*level*/)
{
    if (opFlags[at_limit]) {
        double voltage = remoteBus->getVoltage();
        if (Q >= getQmax()) {
            if (voltage < m_Vtarget) {
                opFlags.reset(at_limit);
                return change_code::parameter_change;
            }
        } else if (voltage > m_Vtarget) {
            opFlags.reset(at_limit);
            return change_code::parameter_change;
        }
    } else {
        if (Q > getQmax()) {
            opFlags.set(at_limit);
            Q = getQmax();
            return change_code::parameter_change;
        }
        if (Q < getQmin()) {
            opFlags.set(at_limit);
            Q = getQmin();
            return change_code::parameter_change;
        }
    }
    return change_code::no_change;
}

void Generator::generationAdjust(double adjustment)
{
    P = P + adjustment;
    Pset = Pset + adjustment;
    if (P > getPmax()) {
        P = getPmax();
        Pset = P;
    } else if (P < getPmin()) {
        P = getPmin();
        Pset = P;
    }
}

void Generator::setFlag(std::string_view flag, bool val)
{
    if (flag == "capabilitycurve") {
        opFlags.set(use_capability_curve, val);
        if ((val) && (!bounds)) {
            bounds = std::make_unique<utilities::OperatingBoundary>(Pmin, Pmax, Qmin, Qmax);
        }
    } else if ((flag == "variable") || (flag == "variablegen")) {
        opFlags.set(variable_generation, val);
        opFlags.set(local_power_control, false);
        opFlags.set(adjustable_P, false);
    } else if (flag == "no_control") {
        opFlags.set(local_power_control, false);
        opFlags.set(adjustable_P, false);
        opFlags.set(adjustable_Q, false);
        opFlags.set(remote_power_control, false);
        opFlags.set(local_voltage_control, false);
        opFlags.set(remote_voltage_control, false);
    } else if ((flag == "reserve") || (flag == "reservecapable")) {
        opFlags.set(reserve_capable, val);
    } else if ((flag == "agc") || (flag == "agccapable") || (flag == "agc_capable")) {
        opFlags.set(agc_capable, val);
    } else if (flag == "indirect_voltage_control") {
        opFlags.set(indirect_voltage_control, val);
    } else if ((flag == "isoc") || (flag == "isochronous")) {
        opFlags.set(isochronous_operation, val);
    } else {
        gridSecondary::setFlag(flag, val);
    }
}

void Generator::set(std::string_view param, double val, unit unitType)
{
    if (param.length() == 1) {
        switch (param[0]) {
            case 'p':
                P = convert(val, unitType, puMW, systemBasePower, localBaseVoltage);
                break;
            case 'q':
                Q = convert(val, unitType, puMW, systemBasePower, localBaseVoltage);
                break;
            case 'r':
                m_Rs = val;

                break;
            case 'x':
                m_Xs = val;
                break;
            default:
                throw(unrecognizedParameter(param));
        }
        return;
    }

    if (param == "pset") {
        Pset = convert(val, unitType, puMW, systemBasePower, localBaseVoltage);
    } else if ((param == "p+") || (param == "adjustment")) {
        generationAdjust(convert(val, unitType, puMW, systemBasePower, localBaseVoltage));
    } else if (param == "qmax") {
        Qmax = convert(val, unitType, puMW, systemBasePower, localBaseVoltage);
    } else if (param == "qmin") {
        Qmin = convert(val, unitType, puMW, systemBasePower, localBaseVoltage);
    } else if (param == "qbias") {
        Qbias = convert(val, unitType, puMW, systemBasePower, localBaseVoltage);
    } else if (param == "xs") {
        m_Xs = val;
    } else if (param == "rs") {
        m_Rs = val;
    } else if ((param == "vref") || (param == "vtarget")) {
        m_Vtarget = convert(val, unitType, puV, systemBasePower, localBaseVoltage);
    } else if ((param == "rating") || (param == "base") || (param == "mbase")) {
        machineBasePower = convert(val, unitType, MVAR, systemBasePower, localBaseVoltage);
        opFlags.set(independent_machine_base);
    } else if (param == "dpdt") {
        dPdt = convert(val, unitType, puMW, systemBasePower, localBaseVoltage);
    } else if (param == "dqdt") {
        dQdt = convert(val, unitType, puMW, systemBasePower, localBaseVoltage);
    } else if (param == "participation") {
        participation = val;
    } else if (param == "vcontrolfrac" || param == "vregfraction" || param == "vcfrac") {
        vRegFraction = val;
    } else if (param == "pmax") {
        Pmax = convert(val, unitType, puMW, systemBasePower, localBaseVoltage);
        if (machineBasePower < 0) {
            machineBasePower = convert(Pmax, puMW, MW, systemBasePower);
        }
        if (bounds) {
            bounds->setValidRange(Pmin, Pmax);
        }
    } else if (param == "pmin") {
        Pmin = convert(val, unitType, puMW, systemBasePower, localBaseVoltage);
        if (bounds) {
            bounds->setValidRange(Pmin, Pmax);
        }
    } else if (param == "remote") {
        coreObject* root = getRoot();
        setRemoteBus(root->findByUserID("bus", static_cast<index_t>(val)));
    } else {
        gridSecondary::set(param, val, unitType);
    }
}

void Generator::setCapabilityCurve(const std::vector<double>& Ppts,
                                   const std::vector<double>& Qminpts,
                                   const std::vector<double>& Qmaxpts)
{
    if ((Ppts.size() == Qminpts.size()) && (Ppts.size() == Qmaxpts.size())) {
        if (!bounds) {
            bounds = std::make_unique<utilities::OperatingBoundary>(Pmin, Pmax, Qmin, Qmax);
        }
        bounds->addPoints(Ppts, Qminpts, Qmaxpts);
        opFlags.set(use_capability_curve);
    }
}

void Generator::outputPartialDerivatives(const IOdata& /*inputs*/,
                                         const stateData& /*sD*/,
                                         matrixData<double>& md,
                                         const solverMode& sMode)
{
    if (!isDynamic(sMode)) {  // the bus is managing a remote bus voltage
        if (stateSize(sMode) > 0) {
            auto offset = offsets.getAlgOffset(sMode);
            md.assign(QoutLocation, offset, 1.0);
        }
        return;
    }
}

count_t Generator::outputDependencyCount(index_t num, const solverMode& sMode) const
{
    if (!isDynamic(sMode)) {  // the bus is managing a remote bus voltage
        if (stateSize(sMode) > 0) {
            return (num == QoutLocation) ? 1 : 0;
        }
    }
    return 0;
}

void Generator::ioPartialDerivatives(const IOdata& inputs,
                                     const stateData& /*sD*/,
                                     matrixData<double>& md,
                                     const IOlocs& inputLocs,
                                     const solverMode& sMode)
{
    if (!isDynamic(sMode)) {
        if (inputs[voltageInLocation] < 0.8) {
            if (!opFlags[no_voltage_derate]) {
                md.assignCheckCol(PoutLocation, inputLocs[voltageInLocation], -P * 1.25);
                md.assignCheckCol(QoutLocation, inputLocs[voltageInLocation], -Q * 1.25);
            }
        }
    }
}

IOdata
    Generator::getOutputs(const IOdata& inputs, const stateData& sD, const solverMode& sMode) const
{
    IOdata output = {-P, -Q};
    if (!isDynamic(sMode))  // use as a proxy for dynamic state
    {
        if (opFlags[indirect_voltage_control]) {
            auto offset = offsets.getAlgOffset(sMode);
            output[QoutLocation] = -sD.state[offset];
            if (inputs[voltageInLocation] < 0.8) {
                if (!opFlags[no_voltage_derate]) {
                    output[PoutLocation] *= inputs[voltageInLocation] * 1.25;
                }
            }
        } else if (inputs[voltageInLocation] < 0.8) {
            if (!opFlags[no_voltage_derate]) {
                output[PoutLocation] *= inputs[voltageInLocation] * 1.25;
                output[QoutLocation] *= inputs[voltageInLocation] * 1.25;
            }
        }
    }
    // printf("t=%f (%s ) voltage=%f T=%f, P=%f\n", time, parent->name.c_str(),
    // inputs[voltageInLocation], inputs[angleInLocation], output[PoutLocation]);
    return output;
}

double Generator::getRealPower(const IOdata& inputs,
                               const stateData& /*sD*/,
                               const solverMode& sMode) const
{
    double output = -P;
    if (!isDynamic(sMode))  // use as a proxy for dynamic state
    {
        if (opFlags[indirect_voltage_control]) {
            if (inputs[voltageInLocation] < 0.8) {
                if (!opFlags[no_voltage_derate]) {
                    output *= inputs[voltageInLocation] * 1.25;
                }
            }
        } else if (inputs[voltageInLocation] < 0.8) {
            if (!opFlags[no_voltage_derate]) {
                output *= inputs[voltageInLocation] * 1.25;
            }
        }
    }

    // printf("t=%f (%s ) voltage=%f T=%f, P=%f\n", time, parent->name.c_str(),
    // inputs[voltageInLocation], inputs[angleInLocation], output[PoutLocation]);
    return output;
}
double Generator::getReactivePower(const IOdata& inputs,
                                   const stateData& sD,
                                   const solverMode& sMode) const
{
    double output = -Q;
    if (!isDynamic(sMode))  // use as a proxy for dynamic state
    {
        if (opFlags[indirect_voltage_control]) {
            auto offset = offsets.getAlgOffset(sMode);
            output = sD.state[offset];
        } else if (inputs[voltageInLocation] < 0.8) {
            if (!opFlags[no_voltage_derate]) {
                output *= inputs[voltageInLocation] * 1.25;
            }
        }
    }
    // printf("t=%f (%s ) voltage=%f T=%f, P=%f\n", time, parent->name.c_str(),
    // inputs[voltageInLocation], inputs[angleInLocation], output[PoutLocation]);
    return output;
}

double Generator::getRealPower() const
{
    return -P;
}
double Generator::getReactivePower() const
{
    return -Q;
}

void Generator::algebraicUpdate(const IOdata& /*inputs*/,
                                const stateData& sD,
                                double update[],
                                const solverMode& sMode,
                                double /*alpha*/)
{
    if ((!isDynamic(sMode)) &&
        (opFlags[indirect_voltage_control])) {  // the bus is managing a remote bus voltage
        double voltage = remoteBus->getVoltage(sD, sMode);
        auto offset = offsets.getAlgOffset(sMode);
        // printf("Q=%f\n",sD.state[offset]);
        if (!opFlags[at_limit]) {
            update[offset] = -Qbias + (voltage - m_Vtarget) * vRegFraction * 10000.0;
        } else {
            update[offset] = -Q;
        }
    }
}
// compute the residual for the dynamic states
void Generator::residual(const IOdata& /*inputs*/,
                         const stateData& sD,
                         double resid[],
                         const solverMode& sMode)
{
    if ((!isDynamic(sMode)) &&
        (opFlags[indirect_voltage_control])) {  // the bus is managing a remote bus voltage
        double voltage = remoteBus->getVoltage(sD, sMode);
        auto offset = offsets.getAlgOffset(sMode);
        // printf("Q=%f\n",sD.state[offset]);
        if (!opFlags[at_limit]) {
            resid[offset] =
                sD.state[offset] + Qbias - (voltage - m_Vtarget) * vRegFraction * 10000.0;
        } else {
            resid[offset] = sD.state[offset] + Q;
        }
    }
}

void Generator::jacobianElements(const IOdata& /*inputs*/,
                                 const stateData& /*sD*/,
                                 matrixData<double>& md,
                                 const IOlocs& /*inputLocs*/,
                                 const solverMode& sMode)
{
    if ((!isDynamic(sMode)) &&
        (opFlags[indirect_voltage_control])) {  // the bus is managing a remote bus voltage
        auto Voff = remoteBus->getOutputLoc(sMode, voltageInLocation);
        auto offset = offsets.getAlgOffset(sMode);
        if (!opFlags[at_limit]) {
            // resid[offset] = sD.state[offset] - (voltage - m_Vtarget)*remoteVRegFraction * 10000;
            md.assignCheck(offset, offset, 1);
            md.assignCheck(offset, Voff, -vRegFraction * 10000);
        } else {
            md.assignCheck(offset, offset, 1.0);
        }
    }
}

void Generator::getStateName(stringVec& stNames,
                             const solverMode& sMode,
                             const std::string& prefix) const
{
    std::string prefix2 = prefix + getName();
    if ((!isDynamic(sMode)) && (stateSize(sMode) > 0)) {
        auto offset = offsets.getAlgOffset(sMode);
        stNames[offset] = prefix2 + ":Q";
    }
}

coreObject* Generator::find(std::string_view object) const
{
    if (object == "bus") {
        return bus;
    }

    if (object == "sched") {
        return sched;
    }
    if ((object == "generator") || (object == getName())) {
        return const_cast<Generator*>(this);
    }
    return gridComponent::find(object);
}

double Generator::getAdjustableCapacityUp(coreTime time) const
{
    if (sched != nullptr) {
        return (sched->getMax(time) - Pset);
    }
    return Pmax - Pset;
}

double Generator::getAdjustableCapacityDown(coreTime time) const
{
    if (sched != nullptr) {
        return (Pset - sched->getMin(time));
    }
    return (Pset - Pmin);
}

IOdata Generator::predictOutputs(coreTime predictionTime,
                                 const IOdata& /*inputs*/,
                                 const stateData& /*sD*/,
                                 const solverMode& /*sMode*/) const
{
    IOdata out(2);
    out[PoutLocation] = Pset;
    out[QoutLocation] = Q;

    if (predictionTime > prevTime + timeOneSecond) {
        if (sched != nullptr) {
            const double Ppred = sched->predict(predictionTime);
            out[PoutLocation] = Ppred;
        }
    }
    return out;
}

double Generator::getPmax(const coreTime time) const
{
    if (sched != nullptr) {
        return sched->getMax(time);
    }
    return Pmax;
}

double Generator::getQmax(const coreTime /*time*/, double Ptest) const
{
    if (opFlags[use_capability_curve]) {
        return bounds->getMax((Ptest == kNullVal) ? P : Ptest);
    }
    return Qmax;
}

double Generator::getPmin(const coreTime time) const
{
    if (sched != nullptr) {
        return sched->getMin(time);
    }
    return Pmin;
}
double Generator::getQmin(const coreTime /*time*/, double Ptest) const
{
    if (opFlags[use_capability_curve]) {
        return bounds->getMin((Ptest == kNullVal) ? P : Ptest);
    }
    return Qmin;
}

double Generator::getFreq(const stateData& sD, const solverMode& sMode, index_t* freqOffset) const
{
    *freqOffset = kNullLocation;
    return bus->getFreq(sD, sMode);
}

double Generator::getAngle(const stateData& sD, const solverMode& sMode, index_t* angleOffset) const
{
    *angleOffset = kNullLocation;
    return bus->getAngle(sD, sMode);
}

}  // namespace griddyn
