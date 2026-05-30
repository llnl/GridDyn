/*
 * Copyright (c) 2014-2026, Lawrence Livermore National Security
 * See the top-level NOTICE for additional details. All rights reserved.
 * SPDX-License-Identifier: BSD-3-Clause
 */

// headers
#include "Subsystem.h"

#include "../GridBus.h"
#include "../Link.h"
#include "../Relay.h"
#include "core/CoreExceptions.h"
#include "core/CoreObjectTemplates.hpp"
#include "core/ObjectFactoryTemplates.hpp"
#include "core/ObjectInterpreter.h"
#include "gmlc/utilities/stringConversion.h"
#include "gmlc/utilities/vectorOps.hpp"
#include <cmath>
#include <complex>
#include <queue>
#include <string>
#include <vector>

namespace griddyn {
using gmlc::utilities::numeric_conversion;
using gmlc::utilities::signn;
using gmlc::utilities::sum;
using units::unit;

static TypeFactory<Subsystem>
    gSubsystemFactory("link", std::to_array<std::string_view>({"subsystem", "simple"}));

Subsystem::Subsystem(const std::string& objName): Link(objName)
{
    resize(2);
    cterm[0] = 1;
    cterm[1] = 2;
    subarea.addOwningReference();
    addSubObject(&subarea);  // add the subArea to the subObject list to take advantage of the code
                             // in GridComponent.
}

Subsystem::Subsystem(count_t terminals, const std::string& objName): Link(objName)
{
    // default values

    resize(terminals);
    if (terminals == 2) {
        cterm[0] = 1;
        cterm[1] = 2;
    }

    subarea.addOwningReference();
    addSubObject(&subarea);  // add the subArea to the subObject list to take advantage of the code
                             // in GridComponent.
}

CoreObject* Subsystem::clone(CoreObject* obj) const
{
    auto* sub = cloneBase<Subsystem, Link>(this, obj);
    if (sub == nullptr) {
        return obj;
    }

    sub->resize(m_terminals);
    sub->cterm = cterm;

    // TODO(phlpt): Find and copy the terminalLink information appropriately.

    return sub;
}

void Subsystem::add(CoreObject* obj)
{
    subarea.add(obj);
}
// --------------- remove components ---------------

void Subsystem::remove(CoreObject* obj)
{
    subarea.remove(obj);
}
GridBus* Subsystem::getBus(index_t num) const
{
    return subarea.getBus(num);
}
Link* Subsystem::getLink(index_t num) const
{
    return subarea.getLink(num);
}
Relay* Subsystem::getRelay(index_t num) const
{
    return subarea.getRelay(num);
}
GridArea* Subsystem::getArea(index_t num) const
{
    return (num == 0) ? static_cast<GridArea*>(getSubObjects()[0]) : subarea.getArea(num - 1);
}

GridArea* Subsystem::getGridArea(index_t num) const
{
    return getArea(num);
}

CoreObject* Subsystem::find(std::string_view objName) const
{
    return subarea.find(objName);
}
CoreObject* Subsystem::getSubObject(std::string_view typeName, index_t num) const
{
    if (typeName == "area") {
        return getArea(num);
    }
    return subarea.getSubObject(typeName, num);
}

void Subsystem::setAll(std::string_view type,
                       std::string_view param,
                       double val,
                       units::unit unitType)
{
    subarea.setAll(type, param, val, unitType);
}

CoreObject* Subsystem::findByUserID(std::string_view typeName, index_t searchID) const
{
    return subarea.findByUserID(typeName, searchID);
}

// reset the bus parameters
void Subsystem::reset(ResetLevels level)
{
    subarea.reset(level);
}
// dynInitializeB states
void Subsystem::pFlowObjectInitializeA(CoreTime time0, std::uint32_t flags)
{
    // make sure the buses are set to the right terminal
    for (index_t ii = 0; ii < m_terminals; ++ii) {
        if (terminalLink[ii] != nullptr) {
            terminalLink[ii]->updateBus(terminalBus[ii], cterm[ii]);
        }
    }

    subarea.pFlowInitializeA(time0, flags);
}

void Subsystem::updateLocalCache()
{
    subarea.updateLocalCache();
}
void Subsystem::updateLocalCache(const IOdata& inputs,
                                 const StateData& stateData,
                                 const SolverMode& sMode)
{
    subarea.updateLocalCache(inputs, stateData, sMode);
}

ChangeCode Subsystem::powerFlowAdjust(const IOdata& inputs, std::uint32_t flags, CheckLevel level)
{
    return subarea.powerFlowAdjust(inputs, flags, level);
}

void Subsystem::pFlowCheck(std::vector<Violation>& violationVector)
{
    subarea.pFlowCheck(violationVector);
}
// dynInitializeB states for dynamic solution
void Subsystem::dynObjectInitializeA(CoreTime time0, std::uint32_t flags)
{
    subarea.dynInitializeA(time0, flags);
}

void Subsystem::converge(CoreTime time,
                         double state[],
                         double dstateDt[],
                         const SolverMode& sMode,
                         ConvergeMode mode,
                         double tol)
{
    subarea.converge(time, state, dstateDt, sMode, mode, tol);
}

void Subsystem::resize(count_t newSize)
{
    m_terminals = newSize;
    terminalBus.resize(newSize);
    terminalLink.resize(newSize);
    Pout.resize(newSize, 0);
    Qout.resize(newSize, 0);
    cterm.resize(newSize);
}

// set properties
void Subsystem::set(std::string_view param, std::string_view val)

{
    std::string iparam;
    int num = gmlc::utilities::stringOps::trailingStringInt(param, iparam, -1);
    if (iparam == "bus") {
        auto* bus = dynamic_cast<GridBus*>(locateObject(std::string{val}, getParent()));
        if (bus != nullptr) {
            if (num > static_cast<int>(m_terminals)) {
                resize(num);
            }
            if (num <= 0) {
                num = 1;
                while (terminalBus[num - 1] != nullptr) {
                    ++num;
                    if (num > static_cast<int>(m_terminals)) {
                        resize(num);
                        break;
                    }
                }
            }
            updateBus(bus, num);
        } else {
            throw(InvalidParameterValue(param));
        }
    } else if (param == "from") {
        auto* bus = dynamic_cast<GridBus*>(locateObject(std::string{val}, getParent()));
        if (bus != nullptr) {
            updateBus(bus, 1);
        } else {
            throw(InvalidParameterValue(param));
        }
    } else if (param == "to") {
        auto* bus = dynamic_cast<GridBus*>(locateObject(std::string{val}, getParent()));
        if (bus != nullptr) {
            updateBus(bus, 2);
        } else {
            throw(InvalidParameterValue(param));
        }
    } else if (iparam == "connection") {
        auto pos1 = val.find_first_of(":,");
        index_t term1 = kNullLocation;
        if (pos1 != std::string::npos) {
            term1 = numeric_conversion<index_t>(std::string{val.substr(pos1 + 1)}, 0);
        }
        auto* lnk = dynamic_cast<Link*>(locateObject(std::string{val}, this, false));
        if (lnk != nullptr) {
            if (num > static_cast<int>(m_terminals)) {
                resize(num);
            }
            if (num == 0) {
                num = 1;
                while (terminalLink[num - 1] != nullptr) {
                    ++num;
                    if (num > static_cast<int>(m_terminals)) {
                        resize(num);
                        break;
                    }
                }
            }

            terminalLink[num] = lnk;
            if (term1 >= 1) {
                if (term1 <= lnk->terminalCount()) {
                    cterm[num] = term1;
                }
            } else {
                for (count_t pp = 1; pp <= lnk->terminalCount(); ++pp) {
                    if (lnk->getBus(pp) == nullptr) {
                        cterm[num] = pp;
                        break;
                    }
                }
            }
            if (cterm[num] == 0) {
                throw(InvalidParameterValue(param));
            }
        }
    } else {
        try {
            GridPrimary::set(param, val);
        }
        catch (const UnrecognizedParameter&) {
            subarea.set(param, val);
        }
    }
}

void Subsystem::set(std::string_view param, double val, unit unitType)
{
    if (param == "terminals") {
        resize(static_cast<count_t>(val));
    } else {
        try {
            GridPrimary::set(param, val, unitType);  // skipping Link set function
        }
        catch (const UnrecognizedParameter&) {
            subarea.set(param, val, unitType);
        }
    }
}

double Subsystem::get(std::string_view param, unit unitType) const
{
    double val = subarea.get(param, unitType);
    if (val == kNullVal) {
        val = GridPrimary::get(param, unitType);
    }
    return val;
}

void Subsystem::timestep(const CoreTime time, const IOdata& inputs, const SolverMode& sMode)
{
    subarea.timestep(time, inputs, sMode);
    prevTime = time;
}

count_t Subsystem::getBusVector(std::vector<GridBus*>& busVector, index_t start)
{
    return subarea.getBusVector(busVector, start);
}

// single value return functions
double Subsystem::getLoss() const
{
    return subarea.getLoss();
}
// -------------------- Power Flow --------------------

// pass the solution
void Subsystem::setState(const CoreTime time,
                         const double state[],
                         const double dstateDt[],
                         const SolverMode& sMode)
{
    subarea.setState(time, state, dstateDt, sMode);
    prevTime = time;
    updateLocalCache();
    // next do any internal area states
}

void Subsystem::getVoltageStates(double vStates[], const SolverMode& sMode)

{
    subarea.getVoltageStates(vStates, sMode);
}

bool Subsystem::switchTest() const
{
    for (size_t kk = 0; kk < terminalLink.size(); ++kk) {
        if (terminalLink[kk]->switchTest(cterm[kk])) {
            return true;
        }
    }
    return false;
}

bool Subsystem::switchTest(index_t num) const
{
    if (num <= m_terminals) {
        return terminalLink[num - 1]->switchTest(cterm[num - 1]);
    }

    return false;
}
void Subsystem::switchMode(index_t num, bool mode)
{
    if (num <= m_terminals) {
        terminalLink[num - 1]->switchMode(cterm[num - 1], mode);
    }
}
// is connected
bool Subsystem::isConnected() const
{
    for (index_t kk = 0; kk < m_terminals; ++kk) {
        if (!terminalLink[kk]->isConnected()) {
            return false;
        }
    }
    return true;
}

int Subsystem::fixRealPower(double power,
                            id_type_t measureTerminal,
                            id_type_t /*fixedterminal*/,
                            units::unit unitType)
{
    if (measureTerminal <= m_terminals) {
        return terminalLink[measureTerminal - 1]->fixRealPower(power,
                                                               cterm[measureTerminal - 1],
                                                               1,
                                                               unitType);
    }
    return 0;
}

int Subsystem::fixPower(double rPower,
                        double qPower,
                        id_type_t measureTerminal,
                        id_type_t /*fixedterminal*/,
                        units::unit unitType)
{
    if (measureTerminal <= m_terminals) {
        return terminalLink[measureTerminal - 1]->fixPower(
            rPower, qPower, cterm[measureTerminal - 1], 1, unitType);
    }
    return 0;
}

void Subsystem::followNetwork(int network, std::queue<GridBus*>& stk)
{
    terminalLink[0]->followNetwork(network, stk);
}

void Subsystem::updateBus(GridBus* bus, index_t busnumber)
{
    if (busnumber <= m_terminals) {
        terminalLink[busnumber - 1]->updateBus(bus, cterm[busnumber - 1]);
        terminalBus[busnumber - 1] = bus;
    } else {
        if (opFlags[DIRECT_CONNECTION]) {
            Link::updateBus(bus, busnumber);
        } else {
            throw(ObjectAddFailure(this));
        }
    }
}

double Subsystem::quickupdateP()
{
    return 0;
}
double Subsystem::remainingCapacity() const
{
    return terminalLink[0]->remainingCapacity();
}
double Subsystem::getAngle() const
{
    const double angleTerminal1 = terminalBus[0]->getAngle();
    const double angleTerminal2 = terminalBus[m_terminals - 1]->getAngle();
    return angleTerminal1 - angleTerminal2;
}

double Subsystem::getAngle(const double state[], const SolverMode& sMode) const
{
    const double angleTerminal1 = terminalBus[0]->getAngle(state, sMode);
    const double angleTerminal2 = terminalBus[m_terminals - 1]->getAngle(state, sMode);
    return angleTerminal1 - angleTerminal2;
}

double Subsystem::getRealImpedance(id_type_t busId) const
{
    if (busId == invalid_id_value) {
        busId = 1;
    }
    for (index_t kk = 0; kk < m_terminals; ++kk) {
        if ((busId == kk + 1) || (busId == terminalBus[kk]->getID())) {
            const double busVoltage = terminalBus[kk]->getVoltage();
            const std::complex<double> impedance =
                (busVoltage * busVoltage) / std::complex<double>(Pout[kk], Qout[kk]);
            return std::isnormal(impedance.real()) ? impedance.real() : kBigNum;
        }
    }
    return kBigNum;
}

double Subsystem::getImagImpedance(id_type_t busId) const
{
    if (busId == invalid_id_value) {
        busId = 1;
    }
    for (index_t kk = 0; kk < m_terminals; ++kk) {
        if ((busId == kk + 1) || (busId == terminalBus[kk]->getID())) {
            const double busVoltage = terminalBus[kk]->getVoltage();
            const std::complex<double> impedance =
                (busVoltage * busVoltage) / std::complex<double>(Pout[kk], Qout[kk]);
            return std::isnormal(impedance.imag()) ? impedance.imag() : kBigNum;
        }
    }
    return kBigNum;
}
double Subsystem::getTotalImpedance(id_type_t busId) const
{
    if (busId == invalid_id_value) {
        busId = 1;
    }
    for (index_t kk = 0; kk < m_terminals; ++kk) {
        if ((busId == kk + 1) || (busId == terminalBus[kk]->getID())) {
            const double busVoltage = terminalBus[kk]->getVoltage();
            // printf("id1 impedance=%f\n", signn(linkInfo.P1 +
            // linkInfo.Q1)*(linkInfo.v1*linkInfo.v1) / std::hypot(linkInfo.P1, linkInfo.Q1));
            const double impedanceMagnitude = signn(Pout[kk] + Qout[kk]) *
                (busVoltage * busVoltage) / std::hypot(Pout[kk], Qout[kk]);
            return std::isnormal(impedanceMagnitude) ? impedanceMagnitude : kBigNum;
        }
    }
    return kBigNum;
}

double Subsystem::getCurrent(id_type_t busId) const
{
    if (busId == invalid_id_value) {
        busId = 1;
    }
    for (index_t kk = 0; kk < m_terminals; ++kk) {
        if ((busId == kk + 1) || (busId == terminalBus[kk]->getID())) {
            return std::hypot(Qout[kk], Pout[kk]) / terminalBus[kk]->getVoltage();
        }
    }
    return 0;
}
double Subsystem::getRealCurrent(id_type_t busId) const
{
    if (busId == invalid_id_value) {
        busId = 1;
    }
    for (index_t kk = 0; kk < m_terminals; ++kk) {
        if ((busId == kk + 1) || (busId == terminalBus[kk]->getID())) {
            return Pout[kk] / terminalBus[kk]->getVoltage();
        }
    }
    return 0;
}
double Subsystem::getImagCurrent(id_type_t busId) const
{
    if (busId == invalid_id_value) {
        busId = 1;
    }
    for (index_t kk = 0; kk < m_terminals; ++kk) {
        if ((busId == kk + 1) || (busId == terminalBus[kk]->getID())) {
            return Qout[kk] / terminalBus[kk]->getVoltage();
        }
    }
    return 0;
}

double Subsystem::getRealPower(id_type_t busId) const
{
    if (busId == invalid_id_value) {
        busId = 1;
    }
    for (index_t kk = 0; kk < m_terminals; ++kk) {
        if ((busId == kk + 1) || (busId == terminalBus[kk]->getID())) {
            return Pout[kk];
        }
    }
    return 0;
}  // function to return the real flow in
double Subsystem::getReactivePower(id_type_t busId) const
{
    if (busId == invalid_id_value) {
        busId = 1;
    }
    for (index_t kk = 0; kk < m_terminals; ++kk) {
        if ((busId == kk + 1) || (busId == terminalBus[kk]->getID())) {
            return Qout[kk];
        }
    }
    return 0;
}  // function to return the reactive power in

double Subsystem::getReactiveLoss() const
{
    return std::abs(sum(Qout));
}
double Subsystem::getMaxTransfer() const
{
    return 0;
}
// dynInitializeB power flow

// for computing all the Jacobian elements at once
void Subsystem::ioPartialDerivatives(id_type_t busId,
                                     const StateData& stateData,
                                     MatrixData<double>& jacobian,
                                     const IOlocs& inputLocs,
                                     const SolverMode& sMode)
{
    if (busId <= 0) {
        busId = 1;
    }
    for (index_t kk = 0; kk < m_terminals; ++kk) {
        if ((busId == kk + 1) || (isSameObject(busId, terminalBus[kk]))) {
            terminalLink[kk]->ioPartialDerivatives(
                cterm[kk], stateData, jacobian, inputLocs, sMode);
            break;
        }
    }
}

void Subsystem::outputPartialDerivatives(id_type_t busId,
                                         const StateData& stateData,
                                         MatrixData<double>& jacobian,
                                         const SolverMode& sMode)
{
    if (busId <= 0) {
        busId = 1;
    }
    for (index_t kk = 0; kk < m_terminals; ++kk) {
        if ((busId == kk + 1) || (isSameObject(busId, terminalBus[kk]))) {
            terminalLink[kk]->outputPartialDerivatives(cterm[kk], stateData, jacobian, sMode);
            break;
        }
    }
}

IOdata Subsystem::getOutputs(const IOdata& /*inputs*/,
                             const StateData& stateData,
                             const SolverMode& sMode) const
{
    return getOutputs(1, stateData, sMode);
}

IOdata Subsystem::getOutputs(id_type_t busId,
                             const StateData& /*StateData*/,
                             const SolverMode& /*sMode*/) const
{
    IOdata out{Pout[0], Qout[0]};

    if (busId <= 0) {
        busId = 1;
    }
    for (index_t kk = 0; kk < m_terminals; ++kk) {
        if ((busId == kk + 1) || (busId == terminalBus[kk]->getID())) {
            out[PoutLocation] = Pout[kk];
            out[QoutLocation] = Qout[kk];
            break;
        }
    }
    return out;
}

}  // namespace griddyn
