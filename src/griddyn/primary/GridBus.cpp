/*
 * Copyright (c) 2014-2026, Lawrence Livermore National Security
 * See the top-level NOTICE for additional details. All rights reserved.
 * SPDX-License-Identifier: BSD-3-Clause
 */

// headers
#include "../GridBus.h"

#include "../Generator.h"
#include "../GridArea.h"
#include "../Link.h"
#include "../loads/ZipLoad.h"
#include "../measurement/ObjectGrabbers.h"
#include "AcBus.h"
#include "DcBus.h"
#include "InfiniteBus.h"
#include "core/CoreExceptions.h"
#include "core/CoreObjectTemplates.hpp"
#include "core/ObjectFactoryTemplates.hpp"
#include "gmlc/utilities/stringOps.h"
#include "gmlc/utilities/vectorOps.hpp"
#include "griddyn/griddyn-config.h"
#include <cassert>
#include <cmath>
#include <cstdio>
#include <iostream>
#include <print>
#include <queue>
#include <string>
#include <vector>

namespace griddyn {
std::atomic<count_t> GridBus::busCount(0);
static TypeFactory<GridBus> gbf("bus", std::to_array<std::string_view>({"basic"}));
static ChildTypeFactory<AcBus, GridBus>
    gbfac("bus",
          std::to_array<std::string_view>({"ac", "pq", "pv", "slk", "slack", "afix", "ref"}),
          "ac");
static ChildTypeFactory<DcBus, GridBus> gbfdc("bus",
                                              std::to_array<std::string_view>({"dc", "hvdc"}));
static ChildTypeFactory<infiniteBus, GridBus>
    igbc("bus", std::to_array<std::string_view>({"inf", "infinite"}));

using units::convert;
using units::defunit;
using units::kV;
using units::puHz;
using units::puMW;
using units::puV;
using units::rad;
using units::unit;

GridBus::GridBus(const std::string& objName): GridPrimary(objName), outputs(3), outLocs(3)
{
    // default values
    m_outputSize = 3;
    setUserID(++busCount);
    updateName();
    localBaseVoltage = 120.0;
}

GridBus::GridBus(double voltageStart, double angleStart, const std::string& objName):
    GridPrimary(objName), angle(angleStart), voltage(voltageStart)
{
    m_outputSize = 3;
    // default values
    setUserID(++busCount);
    updateName();
    localBaseVoltage = 120.0;
}

CoreObject* GridBus::clone(CoreObject* obj) const
{
    auto nobj = cloneBaseFactory<GridBus, GridPrimary>(this, obj, &gbf);
    if (nobj == nullptr) {
        return obj;
    }
    nobj->type = type;
    nobj->dynType = dynType;
    nobj->angle = angle;
    nobj->voltage = voltage;
    nobj->set("basevoltage", localBaseVoltage);  // this is to set all the sub objects as well
    nobj->freq = freq;
    nobj->Vtol = Vtol;
    nobj->Atol = Atol;
    nobj->Network = Network;
    nobj->zone = zone;
    nobj->lowVtime = lowVtime;

    return nobj;
}

bool GridBus::checkCapable()
{
    double remainingCapacity{0.0};
    double excessCapacity{0.0};
    if (!opFlags[pFlow_initialized]) {
        return true;
    }
    for (auto& load : attachedLoads) {
        if (load->isConnected()) {
            remainingCapacity -= load->getRealPower();
            excessCapacity -= load->getRealPower();
        }
    }
    for (auto& gen : attachedGens) {
        if (gen->isConnected()) {
            remainingCapacity += gen->getPmax();
            excessCapacity += gen->getPmin();
        }
    }
    for (auto& link : attachedLinks) {
        if (link->isConnected()) {
            remainingCapacity += link->getMaxTransfer();
            excessCapacity -= link->getMaxTransfer();
        }
    }
    if (remainingCapacity >= 0.0) {
        return true;
    }

    if (excessCapacity <= 0.0) {
        return true;
    }
    logging::warning(this, "BUS failed");
    return false;
}

void GridBus::disable()
{
    CoreObject::disable();
    alert(this, STATE_COUNT_CHANGE);
    for (auto& link : attachedLinks) {
        link->disable();
    }
}

void GridBus::add(CoreObject* obj)
{
    auto ld = dynamic_cast<GridLoad*>(obj);
    if (ld != nullptr) {
        return add(ld);
    }

    auto gen = dynamic_cast<Generator*>(obj);
    if (gen != nullptr) {
        return add(gen);
    }

    auto lnk = dynamic_cast<Link*>(obj);
    if (lnk != nullptr) {
        return add(lnk);
    }
    throw(UnrecognizedObjectException(this));
}

template<class X>
void addObject(GridBus* bus, X* obj, objVector<X*>& OVector)
{
    CoreObject* foundObj = bus->find(obj->getName());
    if (foundObj == nullptr) {
        obj->locIndex = static_cast<index_t>(OVector.size());
        OVector.push_back(obj);
        obj->set("basevoltage", bus->localBaseVoltage);
        bus->addSubObject(obj);
        if (bus->checkFlag(pFlow_initialized)) {
            bus->alert(bus, OBJECT_COUNT_INCREASE);
        }
    } else if (!isSameObject(obj, foundObj)) {
        throw(ObjectAddFailure(bus));
    }
}

// add load
void GridBus::add(GridLoad* ld)
{
    addObject(this, ld, attachedLoads);
}

// add generator
void GridBus::add(Generator* gen)
{
    addObject(this, gen, attachedGens);
}

// add link
void GridBus::add(Link* lnk)
{
    for (auto& links : attachedLinks) {
        if (isSameObject(links, lnk)) {
            return;
        }
    }
    attachedLinks.push_back(lnk);
}

void GridBus::remove(CoreObject* obj)
{
    auto ld = dynamic_cast<GridLoad*>(obj);
    if (ld != nullptr) {
        return (remove(ld));
    }

    auto gen = dynamic_cast<Generator*>(obj);
    if (gen != nullptr) {
        return (remove(gen));
    }

    auto lnk = dynamic_cast<Link*>(obj);
    if (lnk != nullptr) {
        return (remove(lnk));
    }

    throw(UnrecognizedObjectException(this));
}

template<class X>
bool removeObject(X* obj, objVector<X*>& OVector)
{
    if (!isValidIndex(obj->locIndex, OVector)) {
        return false;
    }
    if (isSameObject(obj, OVector[obj->locIndex])) {
        // alert that the states might have changed
        if (obj->checkFlag(has_dyn_states)) {
            obj->getParent()->alert(obj->getParent(), STATE_COUNT_DECREASE);
        } else if (obj->checkFlag(has_pflow_states)) {
            obj->getParent()->alert(obj->getParent(), STATE_COUNT_DECREASE);
        }

        OVector.erase(OVector.begin() + obj->locIndex);
        return true;
    }
    return false;
}

// remove load
void GridBus::remove(GridLoad* ld)
{
    if (removeObject(ld, attachedLoads)) {
        GridComponent::remove(ld);
    }
}

// remove generator
void GridBus::remove(Generator* gen)
{
    if (removeObject(gen, attachedGens)) {
        GridComponent::remove(gen);
    }
}

// remove link
void GridBus::remove(Link* lnk)
{
    auto lnkR = std::find_if(attachedLinks.begin(), attachedLinks.end(), [lnk](auto& lk) {
        return isSameObject(lk, lnk);
    });
    if (lnkR != attachedLinks.end()) {
        attachedLinks.erase(lnkR);
    }
}

void GridBus::alert(CoreObject* obj, int code)
{
    switch (code) {
        case OBJECT_NAME_CHANGE:
        case OBJECT_ID_CHANGE:
            break;
        case POTENTIAL_FAULT_CHANGE:
            if (opFlags[disconnected]) {
                reconnect();
            }
            [[fallthrough]];
        default:
            GridPrimary::alert(obj, code);
    }
}

void GridBus::followNetwork(int networkID, std::queue<GridBus*>& bstk)
{
    Network = networkID;
    for (auto& link : attachedLinks) {
        link->followNetwork(networkID, bstk);
    }
}

// dynInitializeB states
void GridBus::pFlowObjectInitializeA(coreTime time0, std::uint32_t flags)
{
    // run the subObjects
    if (Vtol < 0) {
        Vtol = getRoot()->get("voltagetolerance");
    }
    if (Atol < 0) {
        Atol = getRoot()->get("angletolerance");
    }
    for (auto& gen : attachedGens) {
        gen->pFlowInitializeA(time0, flags);
    }
    for (auto& load : attachedLoads) {
        load->pFlowInitializeA(time0, flags);
    }
    if (CHECK_CONTROLFLAG(flags, low_voltage_checking)) {
        opFlags.set(low_voltage_check_flag);
    }
}

void GridBus::pFlowObjectInitializeB()
{
    for (auto& gen : attachedGens) {
        gen->pFlowInitializeB();
    }
    for (auto& load : attachedLoads) {
        load->pFlowInitializeB();
    }
    m_dstate_dt.resize(3, 0);
    m_dstate_dt[angleInLocation] = systemBaseFrequency * (freq - 1.0);
    m_state = {voltage, angle, freq};
}

void GridBus::preEx(const IOdata& /*inputs*/,
                    const StateData& stateDataValue,
                    const SolverMode& sMode)
{
    auto inputs = getOutputs(noInputs, stateDataValue, sMode);
    GridComponent::preEx(inputs, stateDataValue, sMode);
}
// function to reset the bus type and voltage

void GridBus::reset(ResetLevels level)
{
    if (opFlags[disconnected]) {
        for (auto& link : attachedLinks) {
            if (link->isConnected()) {
                reconnect();
                break;
            }
        }
    }

    for (auto& gen : attachedGens) {
        if (gen->checkFlag(has_powerflow_adjustments)) {
            gen->reset(level);
        }
    }
    for (auto& ld : attachedLoads) {
        if (ld->checkFlag(has_powerflow_adjustments)) {
            ld->reset(level);
        }
    }
}

ChangeCode GridBus::powerFlowAdjust(const IOdata& /*inputs*/, std::uint32_t flags, CheckLevel level)
{
    auto out = ChangeCode::NO_CHANGE;
    IOdata inputs = {voltage, angle, freq};
    for (auto& gen : attachedGens) {
        if (gen->checkFlag(has_powerflow_adjustments)) {
            auto pout = gen->powerFlowAdjust(inputs, flags, level);
            out = (std::max)(pout, out);
        }
    }
    for (auto& ld : attachedLoads) {
        if (ld->checkFlag(has_powerflow_adjustments)) {
            auto pout = ld->powerFlowAdjust(inputs, flags, level);
            out = (std::max)(pout, out);
        }
    }
    return out;
}

// dynInitializeB states for dynamic solution
void GridBus::dynObjectInitializeA(coreTime time0, std::uint32_t flags)
{
    opFlags[preEx_requested] = false;
    opFlags[has_constraints] = false;
    offsets.unload(true);
    for (auto& gen : attachedGens) {
        gen->dynInitializeA(time0, flags);
    }
    for (auto& load : attachedLoads) {
        load->dynInitializeA(time0, flags);
    }
    // check for any roots
    // localRoots = 0;
}

// dynInitializeB states for dynamic solution part 2  //final clean up
void GridBus::dynObjectInitializeB(const IOdata& /*inputs*/,
                                   const IOdata& desiredOutput,
                                   IOdata& fieldSet)
{
    if (desiredOutput.size() > voltageInLocation) {
        if (desiredOutput[voltageInLocation] > 0) {
            voltage = desiredOutput[voltageInLocation];
        }
    }
    if (desiredOutput.size() > angleInLocation) {
        if (desiredOutput[angleInLocation] > -kHalfBigNum) {
            angle = desiredOutput[angleInLocation];
        }
    }
    if (desiredOutput.size() > frequencyInLocation) {
        if (std::abs(desiredOutput[frequencyInLocation] - 1.0) < 0.5) {
            freq = desiredOutput[frequencyInLocation];
        }
    }
    updateLocalCache();

    m_state[voltageInLocation] = voltage;
    m_state[angleInLocation] = angle;
    m_state[frequencyInLocation] = freq;

    // first get the state size for the internal state ordering
    IOdata inputs{voltage, angle, freq};
    fieldSet = inputs;

    IOdata pc;
    for (auto& gen : attachedGens) {
        gen->dynInitializeB(inputs, noInputs, pc);
    }
    for (auto& load : attachedLoads) {
        load->dynInitializeB(inputs, noInputs, pc);
    }
    // TODO(phlpt): Actually use the pc outputs.
}

void GridBus::generationAdjust(double /*adjustment*/)
{
    // adjust the real power flow
}

void GridBus::timestep(coreTime time, const IOdata& /*inputs*/, const SolverMode& sMode)
{
    auto inputs = getOutputs(noInputs, emptyStateData, sMode);
    GridComponent::timestep(time, inputs, sMode);
}

void GridBus::setAll(std::string_view objtype,
                     std::string_view param,
                     double val,
                     units::unit unitType)
{
    if ((objtype == "gen") || (objtype == "generator")) {
        for (auto& gen : attachedGens) {
            try {
                gen->set(param, val, unitType);
            }
            catch (const UnrecognizedParameter&) {
                // we ignore this exception in this function
            }
        }
    } else if (objtype == "load") {
        for (auto& ld : attachedLoads) {
            try {
                ld->set(param, val, unitType);
            }
            catch (const UnrecognizedParameter&) {
                // we ignore this exception in this function
            }
        }
    } else if (objtype == "secondary") {
        for (auto& gen : attachedGens) {
            try {
                gen->set(param, val, unitType);
            }
            catch (const UnrecognizedParameter&) {
                // we ignore this exception in this function
            }
        }
        for (auto& ld : attachedLoads) {
            try {
                ld->set(param, val, unitType);
            }
            catch (const UnrecognizedParameter&) {
                // we ignore this exception in this function
            }
        }
    }
}

static const stringVec locNumStrings{"voltage", "angle", "basevoltage", "p", "q", "g", "b", "zone"};
static const stringVec locStrStrings{"status"};

static const stringVec flagStrings{"connected"};

void GridBus::getParameterStrings(stringVec& pstr, ParamStringType pstype) const
{
    getParamString<GridBus, GridComponent>(
        this, pstr, locNumStrings, locStrStrings, flagStrings, pstype);
}

void GridBus::setFlag(std::string_view flag, bool val)
{
    if (flag == "connected") {
        if (val) {
            if (isConnected()) {
                disconnect();
            }
        } else {
            if (!isConnected()) {
                reconnect();
            }
        }
    } else {
        GridPrimary::setFlag(flag, val);
    }
}

// set properties
void GridBus::set(std::string_view param, std::string_view val)
{
    if (param.empty()) {
    } else {
        GridPrimary::set(param, val);
    }
}

void GridBus::set(std::string_view param, double val, unit unitType)
{
    if ((param == "voltage") || (param == "vol")) {
        if (voltage < 0.25) {
            if (opFlags[dyn_initialized]) {
                alert(this, POTENTIAL_FAULT_CHANGE);
            }
        }
        voltage = convert(val, unitType, puV, systemBasePower, localBaseVoltage);
    } else if ((param == "angle") || (param == "ang")) {
        angle = convert(val, unitType, rad);
    } else if ((param == "freq") || (param == "frequency") || (param == "dadt")) {
        freq = convert(val, unitType, puHz, systemBaseFrequency);
    } else if ((param == "basevoltage") || (param == "base vol") || (param == "vbase") ||
               (param == "voltagebase")) {
        localBaseVoltage = convert(val, unitType, kV);
        for (auto& gen : attachedGens) {
            gen->set("basevoltage", val);
        }
        for (auto& ld : attachedLoads) {
            ld->set("basevoltage", val);
        }
    } else if ((param == "p") || (param == "gen p")) {
        S.genP = convert(val, unitType, puMW, systemBasePower, localBaseVoltage);
        if (attachedGens.size() == 1) {
            attachedGens[0]->set("p", S.genP);
        } else if (attachedGens.empty()) {
            if (val != 0.0) {
                // not sure this is the wisest thing to do here should be smarter about it
                add(new Generator());
                attachedGens[0]->set("p", S.genP);
            } else {
                return;
            }
        }
    } else if ((param == "q") || (param == "gen q")) {
        S.genQ = convert(val, unitType, puMW);
        if (attachedGens.size() == 1) {
            attachedGens[0]->set("q", S.genQ);
        } else if (attachedGens.empty()) {
            if (val != 0.0) {
                add(new Generator());
                attachedGens[0]->set("q", S.genQ);
            } else {
                return;
            }
        }
    } else if ((param == "load p") || (param == "load q") || (param == "shunt g") ||
               (param == "g")) {
        if (attachedLoads.empty()) {
            if (val != 0.0) {
                add(new ZipLoad());
            } else {
                return;
            }
        }
        std::string b{param.back()};
        attachedLoads[0]->set(b, val, unitType);
    } else if ((param == "shunt b") || (param == "b")) {
        if (attachedLoads.empty()) {
            if (val != 0.0) {
                add(new ZipLoad());
            } else {
                return;
            }
        }
        attachedLoads[0]->set("b", -val, unitType);
    } else if ((param == "area") || (param == "area number")) {
        // Here to catch a specific issue while the area controls are being developed
    } else {
        GridPrimary::set(param, val, unitType);
    }
}

void GridBus::setVoltageAngle(double voltageNew, double angleNew)
{
    voltage = voltageNew;
    angle = angleNew;
}

IOdata GridBus::getOutputs(const IOdata& /*inputs*/,
                           const StateData& stateDataValue,
                           const SolverMode& sMode) const
{
    return ((sMode.local) || (stateDataValue.empty())) ? IOdata{voltage, angle, freq} :
                                                         IOdata{getVoltage(stateDataValue, sMode),
                                                                getAngle(stateDataValue, sMode),
                                                                getFreq(stateDataValue, sMode)};
}

static const IOlocs noLocs{kNullLocation, kNullLocation, kNullLocation};

IOlocs GridBus::getOutputLocs(const SolverMode& /*sMode*/) const
{
    return noLocs;
}

const IOdata& GridBus::getOutputsRef() const
{
    return outputs;
}

const IOlocs& GridBus::getOutputLocsRef() const
{
    return noLocs;
}

double GridBus::getOutput(const IOdata& /*inputs*/,
                          const StateData& stateDataValue,
                          const SolverMode& sMode,
                          index_t outNum) const
{
    switch (outNum) {
        case voltageInLocation:
            return getVoltage(stateDataValue, sMode);
        case angleInLocation:
            return getAngle(stateDataValue, sMode);
        case frequencyInLocation:
            return getFreq(stateDataValue, sMode);
        default:
            return kNullVal;
    }
}

double GridBus::getOutput(index_t outNum) const
{
    switch (outNum) {
        case voltageInLocation:
            return getVoltage();
        case angleInLocation:
            return getAngle();
        case frequencyInLocation:
            return getFreq();
        default:
            return kNullVal;
    }
}

double GridBus::getVoltage(const double /*state*/[], const SolverMode& /*sMode*/) const
{
    return voltage;
}

double GridBus::getAngle(const double /*state*/[], const SolverMode& /*sMode*/) const
{
    return angle;
}

double GridBus::getVoltage(const StateData& /*stateDataValue*/, const SolverMode& /*sMode*/) const
{
    return voltage;
}

double GridBus::getAngle(const StateData& /*stateDataValue*/, const SolverMode& /*sMode*/) const
{
    return angle;
}

bool GridBus::hasInertialAngle() const
{
    return ((!attachedGens.empty()) && (isConnected()));
}

double GridBus::getFreq(const StateData& /*stateDataValue*/, const SolverMode& /*sMode*/) const
{
    return freq;
}

bool GridBus::directPath(GridComponent* target, GridComponent* source)
{
    auto tid = target->getID();
    if (isSameObject(tid, this)) {
        return true;
    }
    for (auto& gen : attachedGens) {
        if (isSameObject(tid, gen)) {
            return true;
        }
    }
    for (auto& ld : attachedLoads) {
        if (isSameObject(tid, ld)) {
            return true;
        }
    }
    auto sid = (source != nullptr) ? source->getID() : 0;
    int lnkcnt = 0;
    Link* nLink = nullptr;
    for (auto& lnk : attachedLinks) {
        if (lnk->isConnected()) {
            if (isSameObject(lnk, sid)) {
                ++lnkcnt;
                if (lnkcnt > 1) {
                    return false;
                }
                nLink = lnk;
            }
        }
    }
    if (nLink != nullptr) {
        if (isSameObject(nLink->getBus(1), tid)) {
            return true;
        }
        if (isSameObject(nLink->getBus(2), tid)) {
            return true;
        }
        if (isSameObject(nLink->getBus(1), this)) {
            return nLink->getBus(2)->directPath(target, nLink);
        }
        return nLink->getBus(1)->directPath(target, nLink);
    }
    return false;
}

std::vector<GridComponent*> GridBus::getDirectPath(GridComponent* target, GridComponent* source)
{
    std::vector<GridComponent*> opath{source};

    auto tid = target->getID();
    if (isSameObject(tid, this)) {
        opath.push_back(target);
        return opath;
    }
    for (auto& gen : attachedGens) {
        if (isSameObject(tid, gen)) {
            opath.push_back(target);
            return opath;
        }
    }
    for (auto& ld : attachedLoads) {
        if (isSameObject(tid, ld)) {
            opath.push_back(target);
            return opath;
        }
    }
    auto sid = (source != nullptr) ? source->getID() : 0;
    int lnkcnt = 0;
    Link* nLink = nullptr;
    for (auto& lnk : attachedLinks) {
        if (lnk->isConnected()) {
            if (isSameObject(lnk, sid)) {
                ++lnkcnt;
                if (lnkcnt > 1) {
                    return {};
                }
                nLink = lnk;
            }
        }
    }
    if (nLink != nullptr) {
        if (isSameObject(nLink->getBus(1), tid)) {
            opath.push_back(nLink);
            opath.push_back(target);
            return opath;
        }
        if (isSameObject(nLink->getBus(2), tid)) {
            opath.push_back(nLink);
            opath.push_back(target);
            return opath;
        }
        if (isSameObject(nLink->getBus(1), this)) {
            auto npath = nLink->getBus(2)->getDirectPath(target, nLink);
            if (npath.empty()) {
                return npath;
            }

            for (auto& pp : npath) {
                opath.push_back(pp);
            }
            return opath;
        }
        auto npath = nLink->getBus(1)->getDirectPath(target, nLink);
        if (npath.empty()) {
            return npath;
        }

        for (auto& pp : npath) {
            opath.push_back(pp);
        }
        return opath;
    }
    return {};
}

int GridBus::propogatePower(bool /*makeSlack*/)
{
    int unfixed_lines = 0;
    Link* unfixed_line = nullptr;
    double Pexp = 0;
    double Qexp = 0;
    for (auto& lnk : attachedLinks) {
        if (lnk->checkFlag(Link::FIXED_TARGET_POWER)) {
            Pexp += lnk->getRealPower(getID());
            Qexp += lnk->getReactivePower(getID());
            continue;
        }
        ++unfixed_lines;
        unfixed_line = lnk;
    }
    if (unfixed_lines > 1) {
        return 0;
    }

    int adjPSecondary = 0;
    int adjQSecondary = 0;
    for (auto& ld : attachedLoads) {
        if (ld->checkFlag(adjustable_P)) {
            ++adjPSecondary;
        } else {
            Pexp += ld->getRealPower();
        }
        if (ld->checkFlag(adjustable_Q)) {
            ++adjQSecondary;
        } else {
            Qexp += ld->getReactivePower();
        }
    }
    for (auto& gen : attachedGens) {
        if (gen->checkFlag(adjustable_P)) {
            ++adjPSecondary;
        } else {
            Pexp -= gen->getRealPower();
        }
        if (gen->checkFlag(adjustable_Q)) {
            ++adjQSecondary;
        } else {
            Qexp -= gen->getReactivePower();
        }
    }
    if (unfixed_lines == 1) {
        if ((adjPSecondary == 0) && (adjQSecondary == 0)) {
            unfixed_line->fixPower(-Pexp, -Qexp, getID(), getID());
        }
    } else {
        // no lines so adjust the generators and load
        if ((adjPSecondary == 1) && (adjQSecondary == 1)) {
            int found = 0;
            for (auto& gen : attachedGens) {
                if (gen->checkFlag(adjustable_P)) {
                    gen->set("p", Pexp);
                    ++found;
                }
                if (gen->checkFlag(adjustable_Q)) {
                    gen->set("q", Qexp);
                    ++found;
                }
                if (found == 2) {
                    return 1;
                }
            }
            for (auto& ld : attachedLoads) {
                if (ld->checkFlag(adjustable_P)) {
                    ld->set("p", -Pexp);
                    ++found;
                }
                if (ld->checkFlag(adjustable_Q)) {
                    ld->set("q", -Qexp);
                    ++found;
                }
                if (found == 2) {
                    return 1;
                }
            }
        } else {
            // TODO(phlpt): Deal with multiple adjustable controls.
            return 0;
        }
    }
    return 0;
}
// -------------------- Power Flow --------------------

// residual
void GridBus::residual(const IOdata& inputs,
                       const StateData& stateDataValue,
                       double resid[],
                       const SolverMode& sMode)
{
    updateLocalCache(inputs, stateDataValue, sMode);
    if ((opFlags[low_voltage_check_flag]) && (outputs[voltageInLocation] < Vtol / 2.0) &&
        (isConnected())) {
        alert(this, INVALID_STATE_ALERT);
        alert(this, VERY_LOW_VOLTAGE_ALERT);
        lowVtime = (!stateDataValue.empty()) ? stateDataValue.time : prevTime;
        return;
    }
    GridComponent::residual(outputs, stateDataValue, resid, sMode);
}

void GridBus::derivative(const IOdata& inputs,
                         const StateData& stateDataValue,
                         double deriv[],
                         const SolverMode& sMode)
{
    updateLocalCache(inputs, stateDataValue, sMode);
    GridComponent::derivative(outputs, stateDataValue, deriv, sMode);
}

static const IOlocs kNullLocations{kNullLocation, kNullLocation, kNullLocation};

// Jacobian
void GridBus::jacobianElements(const IOdata& inputs,
                               const StateData& stateDataValue,
                               matrixData<double>& matrixDataValue,
                               const IOlocs& /*inputLocs*/,
                               const SolverMode& sMode)
{
    updateLocalCache(inputs, stateDataValue, sMode);
    // import bus values (current theta and voltage)

    // printf("t=%f,id=%d, dpdt=%f, dpdv=%f, dqdt=%f, dqdv=%f\n", time, id, Ptii, Pvii, Qvii, Qtii);

    const IOlocs& coutLocs = (hasAlgebraic(sMode)) ? outLocs : kNullLocations;
    GridComponent::jacobianElements(outputs, stateDataValue, matrixDataValue, coutLocs, sMode);
}

double GridBus::lastError() const
{
    return std::abs(S.sumP()) + std::abs(S.sumQ());
}

inline double
    dVcheck(double dV, double currV, double drFrac = 0.75, double mxRise = 0.2, double cRcheck = 0)
{
    if (currV - dV > cRcheck) {
        if (dV < -mxRise) {
            dV = -mxRise;
        }
    }
    if (dV > drFrac * currV) {
        dV = drFrac * currV;
    }
    return dV;
}

inline double dAcheck(double dT, double /*currA*/, double mxch = kPI / 8.0)
{
    if (std::abs(dT) > mxch) {
        dT = std::copysign(mxch, dT);
    }
    return dT;
}

void GridBus::voltageUpdate(const StateData& /*stateDataValue*/,
                            double /*update*/[],
                            const SolverMode& /*sMode*/,
                            double /*alpha*/)
{
}

void GridBus::algebraicUpdate(const IOdata& inputs,
                              const StateData& stateDataValue,
                              double update[],
                              const SolverMode& sMode,
                              double alpha)
{
    if (algSize(sMode) == offsets.getOffsets(sMode).local.algSize) {
        // no algebraic states in the secondary objects
        return;
    }
    updateLocalCache(inputs, stateDataValue, sMode);
    GridComponent::algebraicUpdate(outputs, stateDataValue, update, sMode, alpha);
}

void GridBus::converge(coreTime /*time*/,
                       double /*state*/[],
                       double /*dstateDt*/[],
                       const SolverMode& /*sMode*/,
                       ConvergeMode /*mode*/,
                       double /*tol*/)
{
}

double GridBus::computeError(const StateData& stateDataValue, const SolverMode& sMode)
{
    updateLocalCache(noInputs, stateDataValue, sMode);

    double err = std::abs(S.sumP()) + std::abs(S.sumQ());

    return err;
}

void GridBus::disconnect()
{
    if (!opFlags[disconnected]) {
        opFlags.set(disconnected);
        outLocs[voltageInLocation] = kNullLocation;
        outLocs[angleInLocation] = kNullLocation;
        outLocs[frequencyInLocation] = kNullLocation;
        alert(this, JAC_COUNT_DECREASE);
        logging::debug(this, "disconnecting bus");
        voltage = 0.0;
        angle = 0.0;
        for (auto& lnk : attachedLinks) {
            lnk->disconnect();
        }
    }
}

void GridBus::reconnect(GridBus* mapBus)
{
    if (opFlags[disconnected]) {
        logging::debug(this, "reconnecting to network");
        opFlags.reset(disconnected);
        alert(this, JAC_COUNT_INCREASE);
        if (mapBus != nullptr) {
            angle = mapBus->angle;
            voltage = mapBus->voltage;
            freq = mapBus->freq;
        } else {
            reset(ResetLevels::low_voltage_dyn1);
        }
        for (auto& lnk : attachedLinks) {
            lnk->reconnect();
        }
    }
}

void GridBus::reconnect()
{
    reconnect(nullptr);
}

void GridBus::updateFlags(bool dynOnly)
{
    opFlags.reset(preEx_requested);
    opFlags.reset(has_powerflow_adjustments);
    GridComponent::updateFlags(dynOnly);
}

static const IOlocs inLoc{0, 1, 2};

#define DEBUG_KEY_BUS 0
// computed power at bus
void GridBus::updateLocalCache(const IOdata& /*inputs*/,
                               const StateData& stateDataValue,
                               const SolverMode& sMode)
{
    if (!S.needsUpdate(stateDataValue)) {
        return;
    }
    S.reset();
    if (!isConnected()) {
        return;
    }
    outputs[voltageInLocation] = getVoltage(stateDataValue, sMode);
    outputs[angleInLocation] = getAngle(stateDataValue, sMode);
    outputs[frequencyInLocation] = getFreq(stateDataValue, sMode);
#if DEBUG_KEY_BUS > 0
    if (getID() == DEBUG_KEY_BUS) {
        std::println("{} V={}, A={} voltage={}, angle={} ",
                     DEBUG_KEY_BUS,
                     outputs[voltageInLocation],
                     outputs[angleInLocation] * 180.0 / kPI,
                     voltage,
                     angle * 180 / kPI);
    }
#endif
    auto cid = getID();
    for (auto& link : attachedLinks) {
        if (link->isEnabled()) {
            link->updateLocalCache(noInputs, stateDataValue, sMode);
            S.linkP += link->getRealPower(cid);
            S.linkQ += link->getReactivePower(cid);
#if DEBUG_KEY_BUS > 0
            if (getID() == DEBUG_KEY_BUS) {
                std::println("{} linkP={}, linkQ={} line {}",
                             DEBUG_KEY_BUS,
                             link->getRealPower(cid),
                             link->getReactivePower(cid),
                             link->getName().c_str());
            }
#endif
        }
    }
    if (isExtended(sMode)) {
        auto offset = offsets.getAlgOffset(sMode);
        S.loadP = stateDataValue.state[offset];
        S.loadQ = stateDataValue.state[offset + 1];
        return;
    }

    for (auto& ld : attachedLoads) {
        if (ld->isConnected() && ld->isEnabled()) {
            ld->updateLocalCache(outputs, stateDataValue, sMode);
            S.loadP += ld->getRealPower(outputs, stateDataValue, sMode);
            S.loadQ += ld->getReactivePower(outputs, stateDataValue, sMode);
        }
    }

    for (auto& gen : attachedGens) {
        if (gen->isConnected() && gen->isEnabled()) {
            gen->updateLocalCache(outputs, stateDataValue, sMode);
            S.genP += gen->getRealPower(outputs, stateDataValue, sMode);
            S.genQ += gen->getReactivePower(outputs, stateDataValue, sMode);
        }
    }
    S.seqID = stateDataValue.seqID;
}

void busPowers::reset()
{
    linkP = 0.0;
    loadP = 0.0;
    genP = 0.0;

    linkQ = 0.0;
    loadQ = 0.0;
    genQ = 0.0;
    seqID = 0;
}

bool busPowers::needsUpdate(const StateData& stateDataValue) const
{
    bool empty = stateDataValue.empty();
    bool zeroSeqID = stateDataValue.seqID == 0;
    bool differentSeqID = stateDataValue.seqID != seqID;
    return (empty || zeroSeqID || differentSeqID);
}

// computed power at bus
void GridBus::updateLocalCache()
{
    S.reset();
    auto cid = getID();
    for (auto& link : attachedLinks) {
        if (link->isEnabled()) {
            link->updateLocalCache();
            S.linkP += link->getRealPower(cid);
            S.linkQ += link->getReactivePower(cid);
        }
    }
    for (auto& load : attachedLoads) {
        if (load->isConnected() && load->isEnabled()) {
            S.loadP += load->getRealPower(voltage);
            S.loadQ += load->getReactivePower(voltage);
        }
    }
    for (auto& gen : attachedGens) {
        if (gen->isConnected() && gen->isEnabled()) {
            S.genP += gen->getRealPower();
            S.genQ += gen->getReactivePower();
        }
    }

    if (!opFlags[dyn_initialized]) {
        if ((type == BusType::SLK) || (type == BusType::afix)) {
            S.genP = -(S.loadP + S.linkP);
        }
        if ((type == BusType::SLK) || (type == BusType::PV)) {
            // genQ = -(loadQ + linkQ);
        }
    }
    // now adjust the generation values for non PQ buses

    /*else
    {
    if (std::abs(linkP + loadP) > 0.001)
    {
    std::println("Bus {} has spurious generation requirement of {}", name.c_str(), linkP + loadP);
    }
    }*/
}

double GridBus::getGenerationRealNominal() const
{
    if ((type == BusType::SLK) || (type == BusType::afix)) {
        double general = 0.0;
        for (auto gen : attachedGens) {
            general += gen->getRealPower();
        }
        return general;
    }
    return S.genP;
}

double GridBus::getGenerationReactiveNominal() const
{
    if ((type == BusType::SLK) || (type == BusType::PV)) {
        double genreactive = 0.0;
        for (auto gen : attachedGens) {
            genreactive += gen->getReactivePower();
        }
        return genreactive;
    }
    return S.genQ;
}
double GridBus::getAdjustableCapacityUp(coreTime /*time*/) const
{
    return 0.0;
}

double GridBus::getAdjustableCapacityDown(coreTime /*time*/) const
{
    return 0.0;
}

double GridBus::getFreqResp() const
{
    return 0.0;
}

double GridBus::getRegTotal() const
{
    return 0.0;
}

double GridBus::getSched() const
{
    return 0.0;
}
Link* GridBus::findLink(GridBus* bs) const
{
    Link* lnk = nullptr;

    for (auto lnk2 : attachedLinks) {
        if (isSameObject(lnk2->getBus(1), bs)) {
            lnk = lnk2;
            break;
        }
        if (isSameObject(lnk2->getBus(2), bs)) {
            lnk = lnk2;
            break;
        }
    }

    return lnk;
}

CoreObject* GridBus::find(std::string_view objName) const
{
    if ((objName == getName()) || (objName == "bus")) {
        return const_cast<GridBus*>(this);
    }
    if (objName == "area") {
        return getParent()->find(objName);
    }
    // finding links by naming the opposite end
    auto fnd_Ex = objName.find_first_of('!');
    if (fnd_Ex != std::string::npos) {
        if (fnd_Ex == 4) {
            if (objName.compare(0, 4, "link") == 0) {
                auto bobj = getParent()->find(objName.substr(fnd_Ex + 1));
                if (bobj != nullptr) {
                    for (auto& lnk : attachedLinks) {
                        if (isSameObject(bobj, lnk->getBus(1))) {
                            return lnk;
                        }
                        if (isSameObject(bobj, lnk->getBus(2))) {
                            return lnk;
                        }
                    }
                    return nullptr;
                }
            }
        }
    }
    return GridComponent::find(objName);
}

CoreObject* GridBus::getSubObject(std::string_view typeName, index_t num) const
{
    if (typeName == "link") {
        return getLink(num);
    }
    if (typeName == "load") {
        return getLoad(num);
    }
    if ((typeName == "gen") || (typeName == "generator")) {
        return getGen(num);
    }

    return GridComponent::getSubObject(typeName, num);
}

CoreObject* GridBus::findByUserID(std::string_view typeName, index_t searchID) const
{
    if (typeName == "load") {
        for (auto& LD : attachedLoads) {
            if (LD->getUserID() == searchID) {
                return LD;
            }
        }
    } else if ((typeName == "gen") || (typeName == "generator")) {
        for (auto& gen : attachedGens) {
            if (gen->getUserID() == searchID) {
                return gen;
            }
        }
    } else if (typeName == "link") {
        for (auto& link : attachedLinks) {
            if (link->getUserID() == searchID) {
                return link;
            }
        }
    }
    return GridComponent::findByUserID(typeName, searchID);
}

Link* GridBus::getLink(index_t x) const
{
    return (isValidIndex(x, attachedLinks)) ? attachedLinks[x] : nullptr;
}

GridLoad* GridBus::getLoad(index_t x) const
{
    return (isValidIndex(x, attachedLoads)) ? attachedLoads[x] : nullptr;
}

Generator* GridBus::getGen(index_t x) const
{
    return (isValidIndex(x, attachedGens)) ? attachedGens[x] : nullptr;
}

void GridBus::mergeBus(GridBus* /*bus*/) {}

void GridBus::unmergeBus(GridBus* /*bus*/) {}

void GridBus::checkMerge() {}

void GridBus::registerVoltageControl(GridComponent* /*obj*/) {}
/** @brief  remove an object from voltage control on a bus*/
void GridBus::removeVoltageControl(GridComponent* /*obj*/) {}

void GridBus::registerPowerControl(GridComponent* /*obj*/) {}

void GridBus::removePowerControl(GridComponent* /*obj*/) {}

double GridBus::get(std::string_view param, unit unitType) const
{
    double val;
    if (param == "voltage") {
        val = convert(voltage, puV, unitType, systemBasePower, localBaseVoltage);
    } else if (param == "angle") {
        val = convert(angle, rad, unitType);
    } else if (param == "vtol") {
        val = Vtol;
    } else if (param == "atol") {
        val = Atol;
    } else if ((param == "basevoltage") || (param == "vbase")) {
        val = localBaseVoltage;
    } else if ((param == "general") || (param == "generationreal")) {
        val = convert(getGenerationReal(), puMW, unitType, systemBasePower, localBaseVoltage);
    } else if ((param == "genreactive") || (param == "generationreactive")) {
        val = convert(getGenerationReactive(), puMW, unitType, systemBasePower, localBaseVoltage);
    } else if (param == "loadreal") {
        val = convert(getLoadReal(), puMW, unitType, systemBasePower, localBaseVoltage);
    } else if (param == "loadreactive") {
        val = convert(getLoadReactive(), puMW, unitType, systemBasePower, localBaseVoltage);
    } else if (param == "linkreal") {
        val = convert(getLinkReal(), puMW, unitType, systemBasePower, localBaseVoltage);
    } else if (param == "linkreactive") {
        val = convert(getLinkReactive(), puMW, unitType, systemBasePower, localBaseVoltage);
    } else if (param == "gencount") {
        val = static_cast<double>(attachedGens.size());
    } else if (param == "linkcount") {
        val = static_cast<double>(attachedLinks.size());
    } else if (param == "loadcount") {
        val = static_cast<double>(attachedLoads.size());
    } else if ((param == "p") || (param == "q") || (param == "yp") || (param == "yq") ||
               (param == "ip") || (param == "iq")) {
        val = 0.0;
        for (const auto& ld : attachedLoads) {
            val += ld->get(param, unitType);
        }
    } else {
        auto fptr = getObjectFunction(this, std::string{param});
        if (fptr.first) {
            CoreObject* tobj = const_cast<GridBus*>(this);
            val =
                convert(fptr.first(tobj), fptr.second, unitType, systemBasePower, localBaseVoltage);
        } else {
            val = GridPrimary::get(param, unitType);
        }
    }
    return val;
}

ChangeCode GridBus::rootCheck(const IOdata& /*inputs*/,
                              const StateData& stateDataValue,
                              const SolverMode& sMode,
                              CheckLevel level)
{
    auto inputs = getOutputs(noInputs, stateDataValue, sMode);
    return GridComponent::rootCheck(inputs, stateDataValue, sMode, level);
}

void GridBus::rootTest(const IOdata& /*inputs*/,
                       const StateData& stateDataValue,
                       double roots[],
                       const SolverMode& sMode)
{
    auto inputs = getOutputs(noInputs, stateDataValue, sMode);
    GridComponent::rootTest(inputs, stateDataValue, roots, sMode);
}

void GridBus::rootTrigger(coreTime time,
                          const IOdata& /*inputs*/,
                          const std::vector<int>& rootMask,
                          const SolverMode& sMode)
{
    size_t rootCount = 0;
    int rootOffset = offsets.getRootOffset(sMode);

    auto rootsfound = gmlc::utilities::vecFindne(rootMask,
                                                 0,
                                                 rootOffset + rootCount,
                                                 rootOffset + rootSize(sMode));

    if (!rootsfound.empty()) {
        size_t rootFoundIndex = 0;
        auto inputs = getOutputs(noInputs, emptyStateData, cLocalSolverMode);
        auto nR = rootsfound[rootFoundIndex];
        for (auto& gen : attachedGens) {
            if ((gen->checkFlag(has_roots)) && (gen->isEnabled())) {
                rootCount += gen->rootSize(sMode);
                if (nR < rootOffset + rootCount) {
                    gen->rootTrigger(time, inputs, rootMask, sMode);
                    do {
                        ++rootFoundIndex;
                        if (rootFoundIndex >= rootsfound.size()) {
                            return;
                        }
                        nR = rootsfound[rootFoundIndex];
                    } while (nR < rootOffset + rootCount);
                }
            }
        }
        for (auto& load : attachedLoads) {
            if ((load->checkFlag(has_roots)) && (load->isEnabled())) {
                rootCount += load->rootSize(sMode);
                if (nR < rootOffset + rootCount) {
                    load->rootTrigger(time, inputs, rootMask, sMode);
                    do {
                        ++rootFoundIndex;
                        if (rootFoundIndex >= rootsfound.size()) {
                            return;
                        }
                        nR = rootsfound[rootFoundIndex];
                    } while (nR < rootOffset + rootCount);
                }
            }
        }
    }
}

static const std::vector<stringVec> outputNamesStr{
    {"voltage", "v", "volt"},
    {"angle", "theta", "ang", "a"},
    {"frequency", "freq", "f", "omega"},
};

const std::vector<stringVec>& GridBus::outputNames() const
{
    return outputNamesStr;
}

units::unit GridBus::outputUnits(index_t outputNum) const
{
    switch (outputNum) {
        case voltageInLocation:
            return units::puV;
        case angleInLocation:
            return units::rad;
        case frequencyInLocation:
            return units::puHz;
        default:
            return units::defunit;
    }
}

bool compareBus(GridBus* bus1, GridBus* bus2, bool cmpValues, bool printDiff)
{
    bool cmp = true;

    if (bus1->dynType != bus2->dynType) {
        cmp = false;
        if (printDiff) {
            std::println("dynamic type is different");
        }
    }

    if (std::abs(bus1->localBaseVoltage - bus2->localBaseVoltage) > 0.00001) {
        cmp = false;
        if (printDiff) {
            std::println("base voltage is different b1={} <> b2={}",
                         bus1->localBaseVoltage,
                         bus2->localBaseVoltage);
        }
    }
    if (std::abs(bus1->systemBasePower - bus2->systemBasePower) > 0.00001) {
        cmp = false;
        if (printDiff) {
            std::println("base power is different b1={} <> b2={}",
                         bus1->systemBasePower,
                         bus2->systemBasePower);
        }
    }
    if (cmpValues) {
        bus1->updateLocalCache();
        bus2->updateLocalCache();
        if (std::abs(bus1->getVoltage() - bus2->getVoltage()) > 0.001) {
            cmp = false;
            if (printDiff) {
                std::println("bus voltages are different b1={} <> b2={}",
                             bus1->getVoltage(),
                             bus2->getVoltage());
            }
        }
        if (std::abs(bus1->getAngle() - bus2->getAngle()) > 0.001) {
            cmp = false;
            if (printDiff) {
                std::println("bus angles are different b1={} <> b2={}",
                             bus1->getAngle(),
                             bus2->getAngle());
            }
        }
        auto diff = std::abs(bus1->getLoadReal() - bus2->getLoadReal());
        if ((diff > 0.02) && (diff / std::abs(bus1->getLoadReal()) > 0.01)) {
            cmp = false;
            if (printDiff) {
                std::println("real load is different b1={} <> b2={}",
                             bus1->getLoadReal(),
                             bus2->getLoadReal());
            }
        }
        diff = std::abs(bus1->getLoadReactive() - bus2->getLoadReactive());
        if ((diff > 0.02) && (diff / std::abs(bus1->getLoadReactive()) > 0.01)) {
            cmp = false;
            if (printDiff) {
                std::println("reactive load is different b1={} <> b2={}",
                             bus1->getLoadReactive(),
                             bus2->getLoadReactive());
            }
        }
        diff = std::abs(bus1->getGenerationReal() - bus2->getGenerationReal());
        if ((diff > 0.02) && (diff / std::abs(bus1->getGenerationReal()) > 0.01)) {
            cmp = false;
            if (printDiff) {
                std::println("real generation is different b1={} <> b2={}",
                             bus1->getGenerationReal(),
                             bus2->getGenerationReal());
            }
        }
        diff = std::abs(bus1->getGenerationReactive() - bus2->getGenerationReactive());
        if ((diff > 0.02) && (diff / std::abs(bus1->getGenerationReactive()) > 0.01)) {
            cmp = false;
            if (printDiff) {
                std::println("reactive generation is different b1={} <> b2={}",
                             bus1->getGenerationReactive(),
                             bus2->getGenerationReactive());
            }
        }
        diff = std::abs(bus1->getLinkReal() - bus2->getLinkReal());
        if ((diff > 0.02) && (diff / std::abs(bus1->getLinkReal()) > 0.01)) {
            cmp = false;
            if (printDiff) {
                std::println("real link is different b1={} <> b2={}",
                             bus1->getLinkReal(),
                             bus2->getLinkReal());
            }
        }
        diff = std::abs(bus1->getLinkReactive() - bus2->getLinkReactive());
        if ((diff > 0.02) && (diff / std::abs(bus1->getLinkReactive()) > 0.01)) {
            cmp = false;
            if (printDiff) {
                std::println("reactive link is different b1={} <> b2={}",
                             bus1->getLinkReactive(),
                             bus2->getLinkReactive());
            }
        }
    } else {
        if (bus1->attachedLoads.size() != bus2->attachedLoads.size()) {
            cmp = false;
        }
        if (bus1->attachedGens.size() != bus2->attachedGens.size()) {
            cmp = false;
        }
    }

    if (bus1->attachedLinks.size() != bus2->attachedLinks.size()) {
        cmp = false;
    } else {
        for (auto& bus1Link : bus1->attachedLinks) {
            auto b1id1 = bus1Link->getBus(1)->getID();
            auto b1id2 = bus1Link->getBus(2)->getID();
            for (auto& bus2Link : bus2->attachedLinks) {
                if ((isSameObject(bus2Link->getBus(1), b1id1)) &&
                    (isSameObject(bus2Link->getBus(2), b1id2))) {
                    if (!compareLink(bus1Link, bus2Link, false, printDiff)) {
                        cmp = false;
                    }
                    break;
                }
                if ((isSameObject(bus2Link->getBus(2), b1id1)) &&
                    (isSameObject(bus2Link->getBus(1), b1id2))) {
                    if (!compareLink(bus1Link, bus2Link, false, printDiff)) {
                        cmp = false;
                    }
                    break;
                }
            }
        }
    }
    return cmp;
}

GridBus* getMatchingBus(GridBus* bus, const GridPrimary* src, GridPrimary* sec)
{
    if (bus->isRoot()) {
        return nullptr;
    }
    if (isSameObject(bus->getParent(), src))  // if this is true then things are easy
    {
        return sec->getBus(bus->locIndex);
    }

    auto par = dynamic_cast<GridPrimary*>(bus->getParent());
    if (par == nullptr) {
        return nullptr;
    }
    std::vector<index_t> lkind = {bus->locIndex};
    while (!isSameObject(par, src)) {
        lkind.push_back(par->locIndex);
        par = dynamic_cast<GridPrimary*>(par->getParent());
        if (par == nullptr) {
            return nullptr;
        }
    }
    // now work our way backwards through the secondary
    par = sec;
    for (auto kk = lkind.size() - 1; kk > 0; --kk) {
        par = static_cast<GridPrimary*>(par->getGridArea(lkind[kk]));
    }
    return par->getBus(lkind[0]);
}

}  // namespace griddyn
