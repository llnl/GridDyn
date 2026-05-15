/*
 * Copyright (c) 2014-2026, Lawrence Livermore National Security
 * See the top-level NOTICE for additional details. All rights reserved.
 * SPDX-License-Identifier: BSD-3-Clause
 */

// headers
#include "subsystem.h"

#include "../Link.h"
#include "../Relay.h"
#include "../gridBus.h"
#include "core/coreExceptions.h"
#include "core/coreObjectTemplates.hpp"
#include "core/objectFactoryTemplates.hpp"
#include "core/objectInterpreter.h"
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

static typeFactory<subsystem> gf("link", std::to_array<std::string_view>({"subsystem", "simple"}));

subsystem::subsystem(const std::string& objName): Link(objName)
{
    resize(2);
    cterm[0] = 1;
    cterm[1] = 2;
    subarea.addOwningReference();
    addSubObject(&subarea);  // add the subArea to the subObject list to take advantage of the code
                             // in gridComponent.
}

subsystem::subsystem(count_t terminals, const std::string& objName): Link(objName)
{
    // default values

    resize(terminals);
    if (terminals == 2) {
        cterm[0] = 1;
        cterm[1] = 2;
    }

    subarea.addOwningReference();
    addSubObject(&subarea);  // add the subArea to the subObject list to take advantage of the code
                             // in gridComponent.
}

coreObject* subsystem::clone(coreObject* obj) const
{
    auto sub = cloneBase<subsystem, Link>(this, obj);
    if (sub == nullptr) {
        return obj;
    }

    sub->resize(m_terminals);
    sub->cterm = cterm;

    // TODO(phlpt): Find and copy the terminalLink information appropriately.

    return sub;
}

void subsystem::add(coreObject* obj)
{
    subarea.add(obj);
}
// --------------- remove components ---------------

void subsystem::remove(coreObject* obj)
{
    subarea.remove(obj);
}
gridBus* subsystem::getBus(index_t num) const
{
    return subarea.getBus(num);
}
Link* subsystem::getLink(index_t num) const
{
    return subarea.getLink(num);
}
Relay* subsystem::getRelay(index_t num) const
{
    return subarea.getRelay(num);
}
Area* subsystem::getArea(index_t num) const
{
    return (num == 0) ? static_cast<Area*>(getSubObjects()[0]) : subarea.getArea(num - 1);
}

coreObject* subsystem::find(std::string_view objName) const
{
    return subarea.find(objName);
}
coreObject* subsystem::getSubObject(std::string_view typeName, index_t num) const
{
    if (typeName == "area") {
        return getArea(num);
    }
    return subarea.getSubObject(typeName, num);
}

void subsystem::setAll(std::string_view type,
                       std::string_view param,
                       double val,
                       units::unit unitType)
{
    subarea.setAll(type, param, val, unitType);
}

coreObject* subsystem::findByUserID(std::string_view typeName, index_t searchID) const
{
    return subarea.findByUserID(typeName, searchID);
}

// reset the bus parameters
void subsystem::reset(reset_levels level)
{
    subarea.reset(level);
}
// dynInitializeB states
void subsystem::pFlowObjectInitializeA(coreTime time0, std::uint32_t flags)
{
    // make sure the buses are set to the right terminal
    for (index_t ii = 0; ii < m_terminals; ++ii) {
        if (terminalLink[ii] != nullptr) {
            terminalLink[ii]->updateBus(terminalBus[ii], cterm[ii]);
        }
    }

    return subarea.pFlowInitializeA(time0, flags);
}

void subsystem::updateLocalCache()
{
    subarea.updateLocalCache();
}
void subsystem::updateLocalCache(const IOdata& inputs, const stateData& sD, const solverMode& sMode)
{
    subarea.updateLocalCache(inputs, sD, sMode);
}

change_code
    subsystem::powerFlowAdjust(const IOdata& inputs, std::uint32_t flags, check_level_t level)
{
    return subarea.powerFlowAdjust(inputs, flags, level);
}

void subsystem::pFlowCheck(std::vector<Violation>& Violation_vector)
{
    subarea.pFlowCheck(Violation_vector);
}
// dynInitializeB states for dynamic solution
void subsystem::dynObjectInitializeA(coreTime time0, std::uint32_t flags)
{
    return subarea.dynInitializeA(time0, flags);
}

void subsystem::converge(coreTime time,
                         double state[],
                         double dstate_dt[],
                         const solverMode& sMode,
                         converge_mode mode,
                         double tol)
{
    subarea.converge(time, state, dstate_dt, sMode, mode, tol);
}

void subsystem::resize(count_t newSize)
{
    m_terminals = newSize;
    terminalBus.resize(newSize);
    terminalLink.resize(newSize);
    Pout.resize(newSize, 0);
    Qout.resize(newSize, 0);
    cterm.resize(newSize);
}

// set properties
void subsystem::set(std::string_view param, std::string_view val)

{
    std::string iparam;
    int num = gmlc::utilities::stringOps::trailingStringInt(param, iparam, -1);
    if (iparam == "bus") {
        auto bus = dynamic_cast<gridBus*>(locateObject(std::string{val}, getParent()));
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
            throw(invalidParameterValue(param));
        }
    } else if (param == "from") {
        auto bus = dynamic_cast<gridBus*>(locateObject(std::string{val}, getParent()));
        if (bus != nullptr) {
            updateBus(bus, 1);
        } else {
            throw(invalidParameterValue(param));
        }
    } else if (param == "to") {
        auto bus = dynamic_cast<gridBus*>(locateObject(std::string{val}, getParent()));
        if (bus != nullptr) {
            updateBus(bus, 2);
        } else {
            throw(invalidParameterValue(param));
        }
    } else if (iparam == "connection") {
        auto pos1 = val.find_first_of(":,");
        index_t term1 = kNullLocation;
        if (pos1 != std::string::npos) {
            term1 = numeric_conversion<index_t>(std::string{val.substr(pos1 + 1)}, 0);
        }
        auto lnk = dynamic_cast<Link*>(locateObject(std::string{val}, this, false));
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
                throw(invalidParameterValue(param));
            }
        }
    } else {
        try {
            gridPrimary::set(param, val);
        }
        catch (const unrecognizedParameter&) {
            subarea.set(param, val);
        }
    }
}

void subsystem::set(std::string_view param, double val, unit unitType)
{
    if (param == "terminals") {
        resize(static_cast<count_t>(val));
    } else {
        try {
            gridPrimary::set(param, val, unitType);  // skipping Link set function
        }
        catch (const unrecognizedParameter&) {
            subarea.set(param, val, unitType);
        }
    }
}

double subsystem::get(std::string_view param, unit unitType) const
{
    double val = subarea.get(param, unitType);
    if (val == kNullVal) {
        val = gridPrimary::get(param, unitType);
    }
    return val;
}

void subsystem::timestep(const coreTime time, const IOdata& inputs, const solverMode& sMode)
{
    subarea.timestep(time, inputs, sMode);
    prevTime = time;
}

count_t subsystem::getBusVector(std::vector<gridBus*>& busVector, index_t start)
{
    return subarea.getBusVector(busVector, start);
}

// single value return functions
double subsystem::getLoss() const
{
    return subarea.getLoss();
}
// -------------------- Power Flow --------------------

// pass the solution
void subsystem::setState(const coreTime time,
                         const double state[],
                         const double dstate_dt[],
                         const solverMode& sMode)
{
    subarea.setState(time, state, dstate_dt, sMode);
    prevTime = time;
    updateLocalCache();
    // next do any internal area states
}

void subsystem::getVoltageStates(double vStates[], const solverMode& sMode)

{
    subarea.getVoltageStates(vStates, sMode);
}

bool subsystem::switchTest() const
{
    for (size_t kk = 0; kk < terminalLink.size(); ++kk) {
        if (terminalLink[kk]->switchTest(cterm[kk])) {
            return true;
        }
    }
    return false;
}

bool subsystem::switchTest(index_t num) const
{
    if (num <= m_terminals) {
        return terminalLink[num - 1]->switchTest(cterm[num - 1]);
    }

    return false;
}
void subsystem::switchMode(index_t num, bool mode)
{
    if (num <= m_terminals) {
        terminalLink[num - 1]->switchMode(cterm[num - 1], mode);
    }
}
// is connected
bool subsystem::isConnected() const
{
    for (index_t kk = 0; kk < m_terminals; ++kk) {
        if (!terminalLink[kk]->isConnected()) {
            return false;
        }
    }
    return true;
}

int subsystem::fixRealPower(double power,
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

int subsystem::fixPower(double rPower,
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

void subsystem::followNetwork(int network, std::queue<gridBus*>& stk)
{
    terminalLink[0]->followNetwork(network, stk);
}

void subsystem::updateBus(gridBus* bus, index_t busnumber)
{
    if (busnumber <= m_terminals) {
        terminalLink[busnumber - 1]->updateBus(bus, cterm[busnumber - 1]);
        terminalBus[busnumber - 1] = bus;
    } else {
        if (opFlags[direct_connection]) {
            Link::updateBus(bus, busnumber);
        } else {
            throw(objectAddFailure(this));
        }
    }
}

double subsystem::quickupdateP()
{
    return 0;
}
double subsystem::remainingCapacity() const
{
    return terminalLink[0]->remainingCapacity();
}
double subsystem::getAngle() const
{
    const double t1 = terminalBus[0]->getAngle();
    double t2 = terminalBus[m_terminals - 1]->getAngle();
    return t1 - t2;
}

double subsystem::getAngle(const double state[], const solverMode& sMode) const
{
    double t1 = terminalBus[0]->getAngle(state, sMode);
    double t2 = terminalBus[m_terminals - 1]->getAngle(state, sMode);
    return t1 - t2;
}

double subsystem::getRealImpedance(id_type_t busId) const
{
    if (busId == invalid_id_value) {
        busId = 1;
    }
    for (index_t kk = 0; kk < m_terminals; ++kk) {
        if ((busId == kk + 1) || (busId == terminalBus[kk]->getID())) {
            double vb = terminalBus[kk]->getVoltage();
            std::complex<double> Z = (vb * vb) / std::complex<double>(Pout[kk], Qout[kk]);
            return std::isnormal(Z.real()) ? Z.real() : kBigNum;
        }
    }
    return kBigNum;
}

double subsystem::getImagImpedance(id_type_t busId) const
{
    if (busId == invalid_id_value) {
        busId = 1;
    }
    for (index_t kk = 0; kk < m_terminals; ++kk) {
        if ((busId == kk + 1) || (busId == terminalBus[kk]->getID())) {
            double vb = terminalBus[kk]->getVoltage();
            std::complex<double> Z = (vb * vb) / std::complex<double>(Pout[kk], Qout[kk]);
            return std::isnormal(Z.imag()) ? Z.imag() : kBigNum;
        }
    }
    return kBigNum;
}
double subsystem::getTotalImpedance(id_type_t busId) const
{
    if (busId == invalid_id_value) {
        busId = 1;
    }
    for (index_t kk = 0; kk < m_terminals; ++kk) {
        if ((busId == kk + 1) || (busId == terminalBus[kk]->getID())) {
            double vp = terminalBus[kk]->getVoltage();
            // printf("id1 impedance=%f\n", signn(linkInfo.P1 +
            // linkInfo.Q1)*(linkInfo.v1*linkInfo.v1) / std::hypot(linkInfo.P1, linkInfo.Q1));
            double val = signn(Pout[kk] + Qout[kk]) * (vp * vp) / std::hypot(Pout[kk], Qout[kk]);
            return (std::isnormal(val) ? val : kBigNum);
        }
    }
    return kBigNum;
}

double subsystem::getCurrent(id_type_t busId) const
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
double subsystem::getRealCurrent(id_type_t busId) const
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
double subsystem::getImagCurrent(id_type_t busId) const
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

double subsystem::getRealPower(id_type_t busId) const
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
double subsystem::getReactivePower(id_type_t busId) const
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

double subsystem::getReactiveLoss() const
{
    return std::abs(sum(Qout));
}
double subsystem::getMaxTransfer() const
{
    return 0;
}
// dynInitializeB power flow

// for computing all the Jacobian elements at once
void subsystem::ioPartialDerivatives(id_type_t busId,
                                     const stateData& sD,
                                     matrixData<double>& md,
                                     const IOlocs& inputLocs,
                                     const solverMode& sMode)
{
    if (busId <= 0) {
        busId = 1;
    }
    for (index_t kk = 0; kk < m_terminals; ++kk) {
        if ((busId == kk + 1) || (isSameObject(busId, terminalBus[kk]))) {
            terminalLink[kk]->ioPartialDerivatives(cterm[kk], sD, md, inputLocs, sMode);
            break;
        }
    }
}

void subsystem::outputPartialDerivatives(id_type_t busId,
                                         const stateData& sD,
                                         matrixData<double>& md,
                                         const solverMode& sMode)
{
    if (busId <= 0) {
        busId = 1;
    }
    for (index_t kk = 0; kk < m_terminals; ++kk) {
        if ((busId == kk + 1) || (isSameObject(busId, terminalBus[kk]))) {
            terminalLink[kk]->outputPartialDerivatives(cterm[kk], sD, md, sMode);
            break;
        }
    }
}

IOdata subsystem::getOutputs(const IOdata& /*inputs*/,
                             const stateData& sD,
                             const solverMode& sMode) const
{
    return getOutputs(1, sD, sMode);
}

IOdata subsystem::getOutputs(id_type_t busId,
                             const stateData& /*sD*/,
                             const solverMode& /*sMode*/) const
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
