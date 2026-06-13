/*
 * Copyright (c) 2014-2026, Lawrence Livermore National Security
 * See the top-level NOTICE for additional details. All rights reserved.
 * SPDX-License-Identifier: BSD-3-Clause
 */

#include "../GridBus.h"
#include "../controllers/Scheduler.h"
#include "../measurement/ObjectGrabbers.h"
#include "VariableGenerator.h"
#include "core/CoreExceptions.h"
#include "core/CoreObjectTemplates.hpp"
#include "core/ObjectFactoryTemplates.hpp"
#include "core/ObjectInterpreter.h"
#include "gmlc/utilities/stringOps.h"
#include "gmlc/utilities/vectorOps.hpp"
#include "utilities/MatrixData.hpp"
#include "utilities/OperatingBoundary.h"
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
static TypeFactory<Generator>
    gGeneratorFactory("generator", std::to_array<std::string_view>({"basic", "simple", "pflow"}));
static ChildTypeFactory<DynamicGenerator, Generator>
    gDynamicGeneratorFactory("generator",
                             std::to_array<std::string_view>({"dynamic", "spinning"}),
                             "dynamic");
static ChildTypeFactory<VariableGenerator, Generator>
    gVariableGeneratorFactory("generator",
                              std::to_array<std::string_view>({"variable", "renewable"}));

using units::convert;
using units::MVAR;
using units::MW;
using units::puMW;
using units::puV;
using units::unit;

std::atomic<count_t> Generator::genCount(0);
// default bus object

Generator::Generator(const std::string& objName): GridSecondary(objName)
{
    setUserID(++genCount);
    updateName();
    opFlags.set(ADJUSTABLE_P);
    opFlags.set(ADJUSTABLE_Q);
    opFlags.set(LOCAL_VOLTAGE_CONTROL);
    opFlags.set(LOCAL_POWER_CONTROL);
}

Generator::~Generator() = default;

CoreObject* Generator::clone(CoreObject* obj) const
{
    auto* gen = cloneBaseFactory<Generator, GridSecondary>(this, obj, &gGeneratorFactory);
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

void Generator::pFlowObjectInitializeA(CoreTime time0, std::uint32_t flags)
{
    if (isConnected() && isEnabled()) {
        if (opFlags[LOCAL_VOLTAGE_CONTROL]) {
            if (bus->getType() != GridBus::BusType::PQ) {
                bus->registerVoltageControl(this);
                opFlags.reset(INDIRECT_VOLTAGE_CONTROL);
            } else if (opFlags[INDIRECT_VOLTAGE_CONTROL]) {
                remoteBus = bus;
                if (m_Vtarget < 0.6) {
                    m_Vtarget = remoteBus->get("vtarget");
                }
                remoteBus->registerVoltageControl(this);
            }
        } else if (opFlags[REMOTE_VOLTAGE_CONTROL]) {
            if (m_Vtarget < 0.6) {
                m_Vtarget = remoteBus->get("vtarget");
            }
            remoteBus->registerVoltageControl(this);
            if (remoteBus->getType() == GridBus::BusType::PQ) {
                opFlags.set(INDIRECT_VOLTAGE_CONTROL);
            }
        }
        // load up power control
        if (opFlags[LOCAL_POWER_CONTROL]) {
            if (bus->getType() != GridBus::BusType::PQ) {
                bus->registerPowerControl(this);
                opFlags.reset(INDIRECT_VOLTAGE_CONTROL);
            }
        } else if (opFlags[REMOTE_POWER_CONTROL]) {
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

    GridSecondary::pFlowObjectInitializeA(time0, flags);
}

void Generator::dynObjectInitializeA(CoreTime time0, std::uint32_t flags)
{
    if (machineBasePower < 0.0) {
        machineBasePower = systemBasePower;
    }

    GridSecondary::dynObjectInitializeA(time0, flags);
}

StateSizes Generator::localStateSizes(const SolverMode& sMode) const
{
    StateSizes localStates;
    if (!isEnabled()) {
        return localStates;
    }
    if (isPowerFlow(sMode)) {
        if ((isAC(sMode)) && (opFlags[INDIRECT_VOLTAGE_CONTROL])) {
            localStates.algSize = 1;
        }
    }
    return localStates;
}

count_t Generator::localJacobianCount(const SolverMode& sMode) const
{
    if (!isEnabled()) {
        return 0;
    }
    if (isPowerFlow(sMode)) {
        if ((isAC(sMode)) && (opFlags[INDIRECT_VOLTAGE_CONTROL])) {
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
        if (desiredOutput.size() > POUT_LOCATION) {
            if (desiredOutput[POUT_LOCATION] > -100000) {
                P = desiredOutput[POUT_LOCATION];
            }
        }

        if (desiredOutput.size() > QOUT_LOCATION) {
            if (desiredOutput[QOUT_LOCATION] > -100000) {
                Q = desiredOutput[QOUT_LOCATION];
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
    fieldSet[POUT_LOCATION] = -P;
    fieldSet[QOUT_LOCATION] = -Q;
}

// save an external state to the internal one
void Generator::setState(CoreTime time,
                         const double state[],
                         const double /*dstate_dt*/[],
                         const SolverMode& sMode)
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
void Generator::guessState(CoreTime /*time*/,
                           double state[],
                           double /*dstate_dt*/[],
                           const SolverMode& sMode)
{
    if ((!isDynamic(sMode)) && (stateSize(sMode) > 0)) {
        auto offset = offsets.getAlgOffset(sMode);
        state[offset] = -Q;
    }
}

void Generator::add(CoreObject* obj)
{
    if (dynamic_cast<GridSubModel*>(obj) != nullptr) {
        add(static_cast<GridSubModel*>(obj));
        return;
    }
    if (dynamic_cast<GridBus*>(obj) != nullptr) {
        setRemoteBus(obj);
    } else {
        throw(UnrecognizedObjectException(this));
    }
}

void Generator::add(GridSubModel* obj)
{
    if (dynamic_cast<Scheduler*>(obj) != nullptr) {
        sched = static_cast<Scheduler*>(obj);
    } else {
        throw(UnrecognizedObjectException(this));
    }
}

void Generator::setRemoteBus(CoreObject* newRemoteBus)
{
    auto* newRbus = dynamic_cast<GridBus*>(newRemoteBus);
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
        opFlags.reset(REMOTE_VOLTAGE_CONTROL);
        opFlags.set(LOCAL_VOLTAGE_CONTROL);
        opFlags.reset(HAS_POWERFLOW_ADJUSTMENTS);
    } else {
        opFlags.set(REMOTE_VOLTAGE_CONTROL);
        opFlags.reset(LOCAL_VOLTAGE_CONTROL);
        opFlags.set(HAS_POWERFLOW_ADJUSTMENTS);
    }

    if (opFlags[POWERFLOW_INITIALIZED]) {
        if (opFlags[ADJUSTABLE_Q]) {
            remoteBus->registerVoltageControl(this);
            if (prevRbus != nullptr) {
                prevRbus->removeVoltageControl(this);
            }
        }
        if (opFlags[ADJUSTABLE_P]) {
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
        opFlags.set(REMOTE_POWER_CONTROL);
        opFlags.reset(LOCAL_POWER_CONTROL);
    } else if (param == "p") {
        if (val == "max") {
            P = Pmax;
        } else if (val == "min") {
            P = Pmin;
        } else {
            throw(InvalidParameterValue(val));
        }
    } else if (param == "q") {
        if (val == "max") {
            Q = Qmax;
        } else if (val == "min") {
            Q = Qmin;
        } else {
            throw(InvalidParameterValue(val));
        }
    } else {
        GridSecondary::set(param, val);
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
    } else if (auto fptr = getObjectFunction(this, param).first) {
        auto unit = getObjectFunction(this, param).second;
        CoreObject* tobj = const_cast<Generator*>(this);
        ret = convert(fptr(tobj), unit, unitType, systemBasePower, localBaseVoltage);
    } else {
        ret = GridSecondary::get(param, unitType);
    }
    return ret;
}

void Generator::timestep(CoreTime time, const IOdata& inputs, const SolverMode& /*sMode*/)
{
    if (Pset < -kHalfBigNum) {
        Pset = P;
    }
    const auto timeDelta = time - prevTime;
    Pset = Pset + dPdt * timeDelta;
    Pset = gmlc::utilities::valLimit(Pset, getPmin(timeDelta), getPmax(timeDelta));

    P = Pset;
    Q = Q + dQdt * timeDelta;
    Q = gmlc::utilities::valLimit(Q, getQmin(timeDelta, Pset), getQmax(timeDelta, Pset));
    if (inputs[VOLTAGE_IN_LOCATION] < 0.8) {
        if (!opFlags[NO_VOLTAGE_DERATE]) {
            P = P * (inputs[VOLTAGE_IN_LOCATION] * 1.25);
            Q = Q * (inputs[VOLTAGE_IN_LOCATION] * 1.25);
        }
    }

    // use this as the temporary state storage
    prevTime = time;
}

ChangeCode Generator::powerFlowAdjust(const IOdata& /*inputs*/,
                                      std::uint32_t /*flags*/,
                                      CheckLevel /*level*/)
{
    if (opFlags[AT_LIMIT]) {
        const double voltage = remoteBus->getVoltage();
        if (Q >= getQmax()) {
            if (voltage < m_Vtarget) {
                opFlags.reset(AT_LIMIT);
                return ChangeCode::PARAMETER_CHANGE;
            }
        } else if (voltage > m_Vtarget) {
            opFlags.reset(AT_LIMIT);
            return ChangeCode::PARAMETER_CHANGE;
        }
    } else {
        if (Q > getQmax()) {
            opFlags.set(AT_LIMIT);
            Q = getQmax();
            return ChangeCode::PARAMETER_CHANGE;
        }
        if (Q < getQmin()) {
            opFlags.set(AT_LIMIT);
            Q = getQmin();
            return ChangeCode::PARAMETER_CHANGE;
        }
    }
    return ChangeCode::NO_CHANGE;
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
        opFlags.set(USE_CAPABILITY_CURVE, val);
        if (val && (!bounds)) {
            bounds = std::make_unique<utilities::OperatingBoundary>(Pmin, Pmax, Qmin, Qmax);
        }
    } else if ((flag == "variable") || (flag == "variablegen")) {
        opFlags.set(VARIABLE_GENERATION, val);
        opFlags.set(LOCAL_POWER_CONTROL, false);
        opFlags.set(ADJUSTABLE_P, false);
    } else if (flag == "no_control") {
        opFlags.set(LOCAL_POWER_CONTROL, false);
        opFlags.set(ADJUSTABLE_P, false);
        opFlags.set(ADJUSTABLE_Q, false);
        opFlags.set(REMOTE_POWER_CONTROL, false);
        opFlags.set(LOCAL_VOLTAGE_CONTROL, false);
        opFlags.set(REMOTE_VOLTAGE_CONTROL, false);
    } else if ((flag == "reserve") || (flag == "reservecapable")) {
        opFlags.set(RESERVE_CAPABLE, val);
    } else if ((flag == "agc") || (flag == "agccapable") || (flag == "agc_capable")) {
        opFlags.set(AGC_CAPABLE, val);
    } else if (flag == "indirect_voltage_control") {
        opFlags.set(INDIRECT_VOLTAGE_CONTROL, val);
    } else if ((flag == "isoc") || (flag == "isochronous")) {
        opFlags.set(ISOCHRONOUS_OPERATION, val);
    } else {
        GridSecondary::setFlag(flag, val);
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
                throw(UnrecognizedParameter(param));
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
        opFlags.set(INDEPENDENT_MACHINE_BASE);
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
        const CoreObject* root = getRoot();
        setRemoteBus(root->findByUserID("bus", static_cast<index_t>(val)));
    } else {
        GridSecondary::set(param, val, unitType);
    }
}

void Generator::setCapabilityCurve(const std::vector<double>& ppts,
                                   const std::vector<double>& qminpts,
                                   const std::vector<double>& qmaxpts)
{
    if ((ppts.size() == qminpts.size()) && (ppts.size() == qmaxpts.size())) {
        if (!bounds) {
            bounds = std::make_unique<utilities::OperatingBoundary>(Pmin, Pmax, Qmin, Qmax);
        }
        bounds->addPoints(ppts, qminpts, qmaxpts);
        opFlags.set(USE_CAPABILITY_CURVE);
    }
}

void Generator::outputPartialDerivatives(const IOdata& /*inputs*/,
                                         const StateData& /*stateDataValue*/,
                                         MatrixData<double>& matrixDataValue,
                                         const SolverMode& sMode)
{
    if (!isDynamic(sMode)) {  // the bus is managing a remote bus voltage
        if (stateSize(sMode) > 0) {
            auto offset = offsets.getAlgOffset(sMode);
            matrixDataValue.assign(QOUT_LOCATION, offset, 1.0);
        }
        return;
    }
}

count_t Generator::outputDependencyCount(index_t num, const SolverMode& sMode) const
{
    if (!isDynamic(sMode)) {  // the bus is managing a remote bus voltage
        if (stateSize(sMode) > 0) {
            return (num == QOUT_LOCATION) ? 1 : 0;
        }
    }
    return 0;
}

void Generator::ioPartialDerivatives(const IOdata& inputs,
                                     const StateData& /*stateDataValue*/,
                                     MatrixData<double>& matrixDataValue,
                                     const IOlocs& inputLocs,
                                     const SolverMode& sMode)
{
    if (!isDynamic(sMode)) {
        if (inputs[VOLTAGE_IN_LOCATION] < 0.8) {
            if (!opFlags[NO_VOLTAGE_DERATE]) {
                matrixDataValue.assignCheckCol(POUT_LOCATION,
                                               inputLocs[VOLTAGE_IN_LOCATION],
                                               -P * 1.25);
                matrixDataValue.assignCheckCol(QOUT_LOCATION,
                                               inputLocs[VOLTAGE_IN_LOCATION],
                                               -Q * 1.25);
            }
        }
    }
}

IOdata Generator::getOutputs(const IOdata& inputs,
                             const StateData& stateDataValue,
                             const SolverMode& sMode) const
{
    IOdata output = {-P, -Q};
    if (!isDynamic(sMode))  // use as a proxy for dynamic state
    {
        if (opFlags[INDIRECT_VOLTAGE_CONTROL]) {
            auto offset = offsets.getAlgOffset(sMode);
            output[QOUT_LOCATION] = -stateDataValue.state[offset];
            if (inputs[VOLTAGE_IN_LOCATION] < 0.8) {
                if (!opFlags[NO_VOLTAGE_DERATE]) {
                    output[POUT_LOCATION] *= inputs[VOLTAGE_IN_LOCATION] * 1.25;
                }
            }
        } else if (inputs[VOLTAGE_IN_LOCATION] < 0.8) {
            if (!opFlags[NO_VOLTAGE_DERATE]) {
                output[POUT_LOCATION] *= inputs[VOLTAGE_IN_LOCATION] * 1.25;
                output[QOUT_LOCATION] *= inputs[VOLTAGE_IN_LOCATION] * 1.25;
            }
        }
    }
    // printf("t=%f (%s ) voltage=%f T=%f, P=%f\n", time, parent->name.c_str(),
    // inputs[VOLTAGE_IN_LOCATION], inputs[ANGLE_IN_LOCATION], output[POUT_LOCATION]);
    return output;
}

double Generator::getRealPower(const IOdata& inputs,
                               const StateData& /*sD*/,
                               const SolverMode& sMode) const
{
    double output = -P;
    if (!isDynamic(sMode))  // use as a proxy for dynamic state
    {
        if (opFlags[INDIRECT_VOLTAGE_CONTROL]) {
            if (inputs[VOLTAGE_IN_LOCATION] < 0.8) {
                if (!opFlags[NO_VOLTAGE_DERATE]) {
                    output *= inputs[VOLTAGE_IN_LOCATION] * 1.25;
                }
            }
        } else if (inputs[VOLTAGE_IN_LOCATION] < 0.8) {
            if (!opFlags[NO_VOLTAGE_DERATE]) {
                output *= inputs[VOLTAGE_IN_LOCATION] * 1.25;
            }
        }
    }

    // printf("t=%f (%s ) voltage=%f T=%f, P=%f\n", time, parent->name.c_str(),
    // inputs[VOLTAGE_IN_LOCATION], inputs[ANGLE_IN_LOCATION], output[POUT_LOCATION]);
    return output;
}
double Generator::getReactivePower(const IOdata& inputs,
                                   const StateData& stateDataValue,
                                   const SolverMode& sMode) const
{
    double output = -Q;
    if (!isDynamic(sMode))  // use as a proxy for dynamic state
    {
        if (opFlags[INDIRECT_VOLTAGE_CONTROL]) {
            auto offset = offsets.getAlgOffset(sMode);
            output = stateDataValue.state[offset];
        } else if (inputs[VOLTAGE_IN_LOCATION] < 0.8) {
            if (!opFlags[NO_VOLTAGE_DERATE]) {
                output *= inputs[VOLTAGE_IN_LOCATION] * 1.25;
            }
        }
    }
    // printf("t=%f (%s ) voltage=%f T=%f, P=%f\n", time, parent->name.c_str(),
    // inputs[VOLTAGE_IN_LOCATION], inputs[ANGLE_IN_LOCATION], output[POUT_LOCATION]);
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
                                const StateData& stateDataValue,
                                double update[],
                                const SolverMode& sMode,
                                double /*alpha*/)
{
    if ((!isDynamic(sMode)) &&
        (opFlags[INDIRECT_VOLTAGE_CONTROL])) {  // the bus is managing a remote bus voltage
        const double voltage = remoteBus->getVoltage(stateDataValue, sMode);
        auto offset = offsets.getAlgOffset(sMode);
        // printf("Q=%f\n",sD.state[offset]);
        if (!opFlags[AT_LIMIT]) {
            update[offset] = -Qbias + ((voltage - m_Vtarget) * vRegFraction * 10000.0);
        } else {
            update[offset] = -Q;
        }
    }
}
// compute the residual for the dynamic states
void Generator::residual(const IOdata& /*inputs*/,
                         const StateData& stateDataValue,
                         double resid[],
                         const SolverMode& sMode)
{
    if ((!isDynamic(sMode)) &&
        (opFlags[INDIRECT_VOLTAGE_CONTROL])) {  // the bus is managing a remote bus voltage
        const double voltage = remoteBus->getVoltage(stateDataValue, sMode);
        auto offset = offsets.getAlgOffset(sMode);
        // printf("Q=%f\n",sD.state[offset]);
        if (!opFlags[AT_LIMIT]) {
            resid[offset] = stateDataValue.state[offset] + Qbias -
                ((voltage - m_Vtarget) * vRegFraction * 10000.0);
        } else {
            resid[offset] = stateDataValue.state[offset] + Q;
        }
    }
}

void Generator::jacobianElements(const IOdata& /*inputs*/,
                                 const StateData& /*stateDataValue*/,
                                 MatrixData<double>& matrixDataValue,
                                 const IOlocs& /*inputLocs*/,
                                 const SolverMode& sMode)
{
    if ((!isDynamic(sMode)) &&
        (opFlags[INDIRECT_VOLTAGE_CONTROL])) {  // the bus is managing a remote bus voltage
        auto voltageOffset = remoteBus->getOutputLoc(sMode, VOLTAGE_IN_LOCATION);
        auto offset = offsets.getAlgOffset(sMode);
        if (!opFlags[AT_LIMIT]) {
            // resid[offset] = sD.state[offset] - (voltage - m_Vtarget)*remoteVRegFraction * 10000;
            matrixDataValue.assignCheck(offset, offset, 1);
            matrixDataValue.assignCheck(offset, voltageOffset, -vRegFraction * 10000);
        } else {
            matrixDataValue.assignCheck(offset, offset, 1.0);
        }
    }
}

void Generator::getStateName(stringVec& stNames,
                             const SolverMode& sMode,
                             const std::string& prefix) const
{
    const std::string prefix2 = prefix + getName();
    if ((!isDynamic(sMode)) && (stateSize(sMode) > 0)) {
        auto offset = offsets.getAlgOffset(sMode);
        stNames[offset] = prefix2 + ":Q";
    }
}

CoreObject* Generator::find(std::string_view object) const
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
    return GridComponent::find(object);
}

double Generator::getAdjustableCapacityUp(CoreTime time) const
{
    if (sched != nullptr) {
        return (sched->getMax(time) - Pset);
    }
    return Pmax - Pset;
}

double Generator::getAdjustableCapacityDown(CoreTime time) const
{
    if (sched != nullptr) {
        return (Pset - sched->getMin(time));
    }
    return (Pset - Pmin);
}

IOdata Generator::predictOutputs(CoreTime predictionTime,
                                 const IOdata& /*inputs*/,
                                 const StateData& /*sD*/,
                                 const SolverMode& /*sMode*/) const
{
    IOdata out(2);
    out[POUT_LOCATION] = Pset;
    out[QOUT_LOCATION] = Q;

    if (predictionTime > prevTime + timeOneSecond) {
        if (sched != nullptr) {
            const double predictedPower = sched->predict(predictionTime);
            out[POUT_LOCATION] = predictedPower;
        }
    }
    return out;
}

double Generator::getPmax(const CoreTime time) const
{
    if (sched != nullptr) {
        return sched->getMax(time);
    }
    return Pmax;
}

double Generator::getQmax(const CoreTime /*time*/, double ptest) const
{
    if (opFlags[USE_CAPABILITY_CURVE]) {
        return bounds->getMax((ptest == kNullVal) ? P : ptest);
    }
    return Qmax;
}

double Generator::getPmin(const CoreTime time) const
{
    if (sched != nullptr) {
        return sched->getMin(time);
    }
    return Pmin;
}
double Generator::getQmin(const CoreTime /*time*/, double ptest) const
{
    if (opFlags[USE_CAPABILITY_CURVE]) {
        return bounds->getMin((ptest == kNullVal) ? P : ptest);
    }
    return Qmin;
}

double Generator::getFreq(const StateData& stateDataValue,
                          const SolverMode& sMode,
                          index_t* freqOffset) const
{
    *freqOffset = kNullLocation;
    return bus->getFreq(stateDataValue, sMode);
}

double Generator::getAngle(const StateData& stateDataValue,
                           const SolverMode& sMode,
                           index_t* angleOffset) const
{
    *angleOffset = kNullLocation;
    return bus->getAngle(stateDataValue, sMode);
}

}  // namespace griddyn
