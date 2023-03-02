/*
 * Copyright (c) 2014-2020, Lawrence Livermore National Security
 * See the top-level NOTICE for additional details. All rights reserved.
 * SPDX-License-Identifier: BSD-3-Clause
 */

// headers
#include "../gridBus.h"

#include "../Area.h"
#include "../Generator.h"
#include "../Link.h"
#include "../loads/zipLoad.h"
#include "../measurement/objectGrabbers.h"
#include "acBus.h"
#include "core/coreExceptions.h"
#include "core/coreObjectTemplates.hpp"
#include "core/objectFactoryTemplates.hpp"
#include "dcBus.h"
#include "gmlc/utilities/stringOps.h"
#include "gmlc/utilities/vectorOps.hpp"
#include "griddyn/griddyn-config.h"
#include "infiniteBus.h"
#include <cassert>
#include <cmath>
#include <iostream>

namespace griddyn {
std::atomic<count_t> gridBus::busCount(0);
static typeFactory<gridBus> gbf("bus", stringVec{"basic"});
static childTypeFactory<acBus, gridBus>
    gbfac("bus", stringVec{"ac", "pq", "pv", "slk", "slack", "afix", "ref"}, "ac");
static childTypeFactory<dcBus, gridBus> gbfdc("bus", stringVec{"dc", "hvdc"});
static childTypeFactory<infiniteBus, gridBus> igbc("bus", stringVec{"inf", "infinite"});

using namespace units;

gridBus::gridBus(const std::string& objName): gridPrimary(objName), outputs(3), outLocs(3)
{
    // default values
    m_outputSize = 3;
    setUserID(++busCount);
    updateName();
    localBaseVoltage = 120.0;
}

gridBus::gridBus(double voltageStart, double angleStart, const std::string& objName):
    gridPrimary(objName), angle(angleStart), voltage(voltageStart)
{
    m_outputSize = 3;
    // default values
    setUserID(++busCount);
    updateName();
    localBaseVoltage = 120.0;
}

coreObject* gridBus::clone(coreObject* obj) const
{
    auto nobj = cloneBaseFactory<gridBus, gridPrimary>(this, obj, &gbf);
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

bool gridBus::checkCapable()
{
    double remainingCapacity = 0.0;
    if (!opFlags[pFlow_initialized]) {
        return true;
    }
    for (auto& load : attachedLoads) {
        if (load->isConnected()) {
            remainingCapacity -= load->getRealPower();
        }
    }
    for (auto& gen : attachedGens) {
        if (gen->isConnected()) {
            remainingCapacity += gen->getPmax();
        }
    }
    for (auto& link : attachedLinks) {
        if (link->isConnected()) {
            remainingCapacity += link->getMaxTransfer();
        }
    }
    if (remainingCapacity >= 0.0) {
        return true;
    }
    LOG_WARNING("BUS failed");
    return false;
}

void gridBus::disable()
{
    coreObject::disable();
    alert(this, STATE_COUNT_CHANGE);
    for (auto& link : attachedLinks) {
        link->disable();
    }
}

void gridBus::add(coreObject* obj)
{
    auto ld = dynamic_cast<Load*>(obj);
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
    throw(unrecognizedObjectException(this));
}

template<class X>
void addObject(gridBus* bus, X* obj, objVector<X*>& OVector)
{
    coreObject* foundObj = bus->find(obj->getName());
    if (foundObj == nullptr) {
        obj->locIndex = static_cast<index_t>(OVector.size());
        OVector.push_back(obj);
        obj->set("basevoltage", bus->localBaseVoltage);
        bus->addSubObject(obj);
        if (bus->checkFlag(pFlow_initialized)) {
            bus->alert(bus, OBJECT_COUNT_INCREASE);
        }
    } else if (!isSameObject(obj, foundObj)) {
        throw(objectAddFailure(bus));
    }
}

// add load
void gridBus::add(Load* ld)
{
    addObject(this, ld, attachedLoads);
}

// add generator
void gridBus::add(Generator* gen)
{
    addObject(this, gen, attachedGens);
}

// add link
void gridBus::add(Link* lnk)
{
    for (auto& links : attachedLinks) {
        if (isSameObject(links, lnk)) {
            return;
        }
    }
    attachedLinks.push_back(lnk);
}

void gridBus::remove(coreObject* obj)
{
    auto ld = dynamic_cast<Load*>(obj);
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

    throw(unrecognizedObjectException(this));
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
void gridBus::remove(Load* ld)
{
    if (removeObject(ld, attachedLoads)) {
        gridComponent::remove(ld);
    }
}

// remove generator
void gridBus::remove(Generator* gen)
{
    if (removeObject(gen, attachedGens)) {
        gridComponent::remove(gen);
    }
}

// remove link
void gridBus::remove(Link* lnk)
{
    auto lnkR = std::find_if(attachedLinks.begin(), attachedLinks.end(), [lnk](auto& lk) {
        return isSameObject(lk, lnk);
    });
    if (lnkR != attachedLinks.end()) {
        attachedLinks.erase(lnkR);
    }
}

void gridBus::alert(coreObject* obj, int code)
{
    switch (code) {
        case OBJECT_NAME_CHANGE:
        case OBJECT_ID_CHANGE:
            break;
        case POTENTIAL_FAULT_CHANGE:
            if (opFlags[disconnected]) {
                reconnect();
            }
            FALLTHROUGH
            // FALLTHROUGH
        default:
            gridPrimary::alert(obj, code);
    }
}

void gridBus::followNetwork(int networkID, std::queue<gridBus*>& bstk)
{
    Network = networkID;
    for (auto& link : attachedLinks) {
        link->followNetwork(networkID, bstk);
    }
}

// dynInitializeB states
void gridBus::pFlowObjectInitializeA(coreTime time0, std::uint32_t flags)
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

void gridBus::pFlowObjectInitializeB()
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

void gridBus::preEx(const IOdata& /*inputs*/, const stateData& sD, const solverMode& sMode)
{
    auto inputs = getOutputs(noInputs, sD, sMode);
    gridComponent::preEx(inputs, sD, sMode);
}
// function to reset the bus type and voltage

void gridBus::reset(reset_levels level)
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

change_code
    gridBus::powerFlowAdjust(const IOdata& /*inputs*/, std::uint32_t flags, check_level_t level)
{
    auto out = change_code::no_change;
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
void gridBus::dynObjectInitializeA(coreTime time0, std::uint32_t flags)
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
void gridBus::dynObjectInitializeB(const IOdata& /*inputs*/,
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
    // TODO:: actually us the pc outputs
}

void gridBus::generationAdjust(double /*adjustment*/)
{
    // adjust the real power flow
}

void gridBus::timestep(coreTime time, const IOdata& /*inputs*/, const solverMode& sMode)
{
    auto inputs = getOutputs(noInputs, emptyStateData, sMode);
    gridComponent::timestep(time, inputs, sMode);
}

void gridBus::setAll(const std::string& objtype,
                     const std::string& param,
                     double val,
                     units::unit unitType)
{
    if ((objtype == "gen") || (objtype == "generator")) {
        for (auto& gen : attachedGens) {
            try {
                gen->set(param, val, unitType);
            }
            catch (const unrecognizedParameter&) {
                // we ignore this exception in this function
            }
        }
    } else if (objtype == "load") {
        for (auto& ld : attachedLoads) {
            try {
                ld->set(param, val, unitType);
            }
            catch (const unrecognizedParameter&) {
                // we ignore this exception in this function
            }
        }
    } else if (objtype == "secondary") {
        for (auto& gen : attachedGens) {
            try {
                gen->set(param, val, unitType);
            }
            catch (const unrecognizedParameter&) {
                // we ignore this exception in this function
            }
        }
        for (auto& ld : attachedLoads) {
            try {
                ld->set(param, val, unitType);
            }
            catch (const unrecognizedParameter&) {
                // we ignore this exception in this function
            }
        }
    }
}

static const stringVec locNumStrings{"voltage", "angle", "basevoltage", "p", "q", "g", "b", "zone"};
static const stringVec locStrStrings{"status"};

static const stringVec flagStrings{"connected"};

void gridBus::getParameterStrings(stringVec& pstr, paramStringType pstype) const
{
    getParamString<gridBus, gridComponent>(
        this, pstr, locNumStrings, locStrStrings, flagStrings, pstype);
}

void gridBus::setFlag(const std::string& flag, bool val)
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
        gridPrimary::setFlag(flag, val);
    }
}

// set properties
void gridBus::set(const std::string& param, const std::string& val)
{
    if (param.empty()) {
    } else {
        gridPrimary::set(param, val);
    }
}

void gridBus::set(const std::string& param, double val, unit unitType)
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
                add(new zipLoad());
            } else {
                return;
            }
        }
        std::string b{param.back()};
        attachedLoads[0]->set(b, val, unitType);
    } else if ((param == "shunt b") || (param == "b")) {
        if (attachedLoads.empty()) {
            if (val != 0.0) {
                add(new zipLoad());
            } else {
                return;
            }
        }
        attachedLoads[0]->set("b", -val, unitType);
    } else if ((param == "area") || (param == "area number")) {
        // Here to catch a specific issue while the area controls are being developed
    } else {
        gridPrimary::set(param, val, unitType);
    }
}

void gridBus::setVoltageAngle(double Vnew, double Anew)
{
    voltage = Vnew;
    angle = Anew;
}

IOdata gridBus::getOutputs(const IOdata& /*inputs*/,
                           const stateData& sD,
                           const solverMode& sMode) const
{
    return ((sMode.local) || (sD.empty())) ?
        IOdata{voltage, angle, freq} :
        IOdata{getVoltage(sD, sMode), getAngle(sD, sMode), getFreq(sD, sMode)};
}

static const IOlocs noLocs{kNullLocation, kNullLocation, kNullLocation};

IOlocs gridBus::getOutputLocs(const solverMode& /*sMode*/) const
{
    return noLocs;
}

const IOdata& gridBus::getOutputsRef() const
{
    return outputs;
}

const IOlocs& gridBus::getOutputLocsRef() const
{
    return noLocs;
}

double gridBus::getOutput(const IOdata& /*inputs*/,
                          const stateData& sD,
                          const solverMode& sMode,
                          index_t outNum) const
{
    switch (outNum) {
        case voltageInLocation:
            return getVoltage(sD, sMode);
        case angleInLocation:
            return getAngle(sD, sMode);
        case frequencyInLocation:
            return getFreq(sD, sMode);
        default:
            return kNullVal;
    }
}

double gridBus::getOutput(index_t outNum) const
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

double gridBus::getVoltage(const double /*state*/[], const solverMode& /*sMode*/) const
{
    return voltage;
}

double gridBus::getAngle(const double /*state*/[], const solverMode& /*sMode*/) const
{
    return angle;
}

double gridBus::getVoltage(const stateData& /*sD*/, const solverMode& /*sMode*/) const
{
    return voltage;
}

double gridBus::getAngle(const stateData& /*sD*/, const solverMode& /*sMode*/) const
{
    return angle;
}

bool gridBus::hasInertialAngle() const
{
    return ((!attachedGens.empty()) && (isConnected()));
}

double gridBus::getFreq(const stateData& /*sD*/, const solverMode& /*sMode*/) const
{
    return freq;
}

bool gridBus::directPath(gridComponent* target, gridComponent* source)
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

std::vector<gridComponent*> gridBus::getDirectPath(gridComponent* target, gridComponent* source)
{
    std::vector<gridComponent*> opath{source};

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

int gridBus::propogatePower(bool /*makeSlack*/)
{
    int unfixed_lines = 0;
    Link* unfixed_line = nullptr;
    double Pexp = 0;
    double Qexp = 0;
    for (auto& lnk : attachedLinks) {
        if (lnk->checkFlag(Link::fixed_target_power)) {
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
            // TODO::PT:deal with multiple adjustable controls
            return 0;
        }
    }
    return 0;
}
// -------------------- Power Flow --------------------

// residual
void gridBus::residual(const IOdata& inputs,
                       const stateData& sD,
                       double resid[],
                       const solverMode& sMode)
{
    updateLocalCache(inputs, sD, sMode);
    if ((opFlags[low_voltage_check_flag]) && (outputs[voltageInLocation] < Vtol / 2.0) &&
        (isConnected())) {
        alert(this, INVALID_STATE_ALERT);
        alert(this, VERY_LOW_VOLTAGE_ALERT);
        lowVtime = (!sD.empty()) ? sD.time : prevTime;
        return;
    }
    gridComponent::residual(outputs, sD, resid, sMode);
}

void gridBus::derivative(const IOdata& inputs,
                         const stateData& sD,
                         double deriv[],
                         const solverMode& sMode)
{
    updateLocalCache(inputs, sD, sMode);
    gridComponent::derivative(outputs, sD, deriv, sMode);
}

static const IOlocs kNullLocations{kNullLocation, kNullLocation, kNullLocation};

// Jacobian
void gridBus::jacobianElements(const IOdata& inputs,
                               const stateData& sD,
                               matrixData<double>& md,
                               const IOlocs& /*inputLocs*/,
                               const solverMode& sMode)
{
    updateLocalCache(inputs, sD, sMode);
    // import bus values (current theta and voltage)

    // printf("t=%f,id=%d, dpdt=%f, dpdv=%f, dqdt=%f, dqdv=%f\n", time, id, Ptii, Pvii, Qvii, Qtii);

    const IOlocs& coutLocs = (hasAlgebraic(sMode)) ? outLocs : kNullLocations;
    gridComponent::jacobianElements(outputs, sD, md, coutLocs, sMode);
}

double gridBus::lastError() const
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

void gridBus::voltageUpdate(const stateData& /*sD*/,
                            double /*update*/[],
                            const solverMode& /*sMode*/,
                            double /*alpha*/)
{
}

void gridBus::algebraicUpdate(const IOdata& inputs,
                              const stateData& sD,
                              double update[],
                              const solverMode& sMode,
                              double alpha)
{
    if (algSize(sMode) == offsets.getOffsets(sMode).local.algSize) {
        // no algebraic states in the secondary objects
        return;
    }
    updateLocalCache(inputs, sD, sMode);
    gridComponent::algebraicUpdate(outputs, sD, update, sMode, alpha);
}

void gridBus::converge(coreTime /*time*/,
                       double /*state*/[],
                       double /*dstate_dt*/[],
                       const solverMode& /*sMode*/,
                       converge_mode /*mode*/,
                       double /*tol*/)
{
}

double gridBus::computeError(const stateData& sD, const solverMode& sMode)
{
    updateLocalCache(noInputs, sD, sMode);

    double err = std::abs(S.sumP()) + std::abs(S.sumQ());

    return err;
}

void gridBus::disconnect()
{
    if (!opFlags[disconnected]) {
        opFlags.set(disconnected);
        outLocs[voltageInLocation] = kNullLocation;
        outLocs[angleInLocation] = kNullLocation;
        outLocs[frequencyInLocation] = kNullLocation;
        alert(this, JAC_COUNT_DECREASE);
        LOG_DEBUG("disconnecting bus");
        voltage = 0.0;
        angle = 0.0;
    }
}

void gridBus::reconnect(gridBus* mapBus)
{
    if (opFlags[disconnected]) {
        LOG_DEBUG("reconnecting to network");
        opFlags.reset(disconnected);
        alert(this, JAC_COUNT_INCREASE);
        if (mapBus != nullptr) {
            angle = mapBus->angle;
            voltage = mapBus->voltage;
            freq = mapBus->freq;
        } else {
            reset(reset_levels::low_voltage_dyn1);
        }
        for (auto& lnk : attachedLinks) {
            lnk->reconnect();
        }
    }
}

void gridBus::reconnect()
{
    reconnect(nullptr);
}

void gridBus::updateFlags(bool dynOnly)
{
    opFlags.reset(preEx_requested);
    opFlags.reset(has_powerflow_adjustments);
    gridComponent::updateFlags(dynOnly);
}

static const IOlocs inLoc{0, 1, 2};

#define DEBUG_KEY_BUS 0
// computed power at bus
void gridBus::updateLocalCache(const IOdata& /*inputs*/,
                               const stateData& sD,
                               const solverMode& sMode)
{
    if (!S.needsUpdate(sD)) {
        return;
    }
    S.reset();
    if (!isConnected()) {
        return;
    }
    outputs[voltageInLocation] = getVoltage(sD, sMode);
    outputs[angleInLocation] = getAngle(sD, sMode);
    outputs[frequencyInLocation] = getFreq(sD, sMode);
#if DEBUG_KEY_BUS > 0
    if (getID() == DEBUG_KEY_BUS) {
        printf("%d V=%f, A=%f voltage=%f, angle=%f \n",
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
            link->updateLocalCache(noInputs, sD, sMode);
            S.linkP += link->getRealPower(cid);
            S.linkQ += link->getReactivePower(cid);
#if DEBUG_KEY_BUS > 0
            if (getID() == DEBUG_KEY_BUS) {
                printf("%d linkP=%f, linkQ=%f line %s\n",
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
        S.loadP = sD.state[offset];
        S.loadQ = sD.state[offset + 1];
        return;
    }

    for (auto& ld : attachedLoads) {
        if (ld->isConnected()) {
            ld->updateLocalCache(outputs, sD, sMode);
            S.loadP += ld->getRealPower(outputs, sD, sMode);
            S.loadQ += ld->getReactivePower(outputs, sD, sMode);
        }
    }

    for (auto& gen : attachedGens) {
        if (gen->isConnected()) {
            gen->updateLocalCache(outputs, sD, sMode);
            S.genP += gen->getRealPower(outputs, sD, sMode);
            S.genQ += gen->getReactivePower(outputs, sD, sMode);
        }
    }
    S.seqID = sD.seqID;
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

bool busPowers::needsUpdate(const stateData& sD) const
{
    bool empty = sD.empty();
    bool zeroSeqID = sD.seqID == 0;
    bool differentSeqID = sD.seqID != seqID;
    return (empty || zeroSeqID || differentSeqID);
}

// computed power at bus
void gridBus::updateLocalCache()
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
        if (load->isConnected()) {
            S.loadP += load->getRealPower(voltage);
            S.loadQ += load->getReactivePower(voltage);
        }
    }
    for (auto& gen : attachedGens) {
        if (gen->isConnected()) {
            S.genP += gen->getRealPower();
            S.genQ += gen->getReactivePower();
        }
    }

    if (!opFlags[dyn_initialized]) {
        if ((type == busType::SLK) || (type == busType::afix)) {
            S.genP = -(S.loadP + S.linkP);
        }
        if ((type == busType::SLK) || (type == busType::PV)) {
            // genQ = -(loadQ + linkQ);
        }
    }
    // now adjust the generation values for non PQ buses

    /*else
    {
    if (std::abs(linkP + loadP) > 0.001)
    {
    printf("Bus %s has spurious generation requirement of %f\n",  name.c_str(), linkP + loadP);
    }
    }*/
}

double gridBus::getGenerationRealNominal() const
{
    if ((type == busType::SLK) || (type == busType::afix)) {
        double genreal = 0.0;
        for (auto gen : attachedGens) {
            genreal += gen->getRealPower();
        }
        return genreal;
    }
    return S.genP;
}

double gridBus::getGenerationReactiveNominal() const
{
    if ((type == busType::SLK) || (type == busType::PV)) {
        double genreactive = 0.0;
        for (auto gen : attachedGens) {
            genreactive += gen->getReactivePower();
        }
        return genreactive;
    }
    return S.genQ;
}
double gridBus::getAdjustableCapacityUp(coreTime /*time*/) const
{
    return 0.0;
}

double gridBus::getAdjustableCapacityDown(coreTime /*time*/) const
{
    return 0.0;
}

double gridBus::getFreqResp() const
{
    return 0.0;
}

double gridBus::getRegTotal() const
{
    return 0.0;
}

double gridBus::getSched() const
{
    return 0.0;
}
Link* gridBus::findLink(gridBus* bs) const
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

coreObject* gridBus::find(const std::string& objName) const
{
    if ((objName == getName()) || (objName == "bus")) {
        return const_cast<gridBus*>(this);
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
    return gridComponent::find(objName);
}

coreObject* gridBus::getSubObject(const std::string& typeName, index_t num) const
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

    return gridComponent::getSubObject(typeName, num);
}

coreObject* gridBus::findByUserID(const std::string& typeName, index_t searchID) const
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
    return gridComponent::findByUserID(typeName, searchID);
}

Link* gridBus::getLink(index_t x) const
{
    return (isValidIndex(x, attachedLinks)) ? attachedLinks[x] : nullptr;
}

Load* gridBus::getLoad(index_t x) const
{
    return (isValidIndex(x, attachedLoads)) ? attachedLoads[x] : nullptr;
}

Generator* gridBus::getGen(index_t x) const
{
    return (isValidIndex(x, attachedGens)) ? attachedGens[x] : nullptr;
}

void gridBus::mergeBus(gridBus* /*bus*/) {}

void gridBus::unmergeBus(gridBus* /*bus*/) {}

void gridBus::checkMerge() {}

void gridBus::registerVoltageControl(gridComponent* /*obj*/) {}
/** @brief  remove an object from voltage control on a bus*/
void gridBus::removeVoltageControl(gridComponent* /*obj*/) {}

void gridBus::registerPowerControl(gridComponent* /*obj*/) {}

void gridBus::removePowerControl(gridComponent* /*obj*/) {}

double gridBus::get(const std::string& param, unit unitType) const
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
    } else if ((param == "genreal") || (param == "generationreal")) {
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
        auto fptr = getObjectFunction(this, param);
        if (fptr.first) {
            coreObject* tobj = const_cast<gridBus*>(this);
            val =
                convert(fptr.first(tobj), fptr.second, unitType, systemBasePower, localBaseVoltage);
        } else {
            val = gridPrimary::get(param, unitType);
        }
    }
    return val;
}

change_code gridBus::rootCheck(const IOdata& /*inputs*/,
                               const stateData& sD,
                               const solverMode& sMode,
                               check_level_t level)
{
    auto inputs = getOutputs(noInputs, sD, sMode);
    return gridComponent::rootCheck(inputs, sD, sMode, level);
}

void gridBus::rootTest(const IOdata& /*inputs*/,
                       const stateData& sD,
                       double roots[],
                       const solverMode& sMode)
{
    auto inputs = getOutputs(noInputs, sD, sMode);
    gridComponent::rootTest(inputs, sD, roots, sMode);
}

void gridBus::rootTrigger(coreTime time,
                          const IOdata& /*inputs*/,
                          const std::vector<int>& rootMask,
                          const solverMode& sMode)
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

const std::vector<stringVec>& gridBus::outputNames() const
{
    return outputNamesStr;
}

units::unit gridBus::outputUnits(index_t outputNum) const
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

bool compareBus(gridBus* bus1, gridBus* bus2, bool cmpValues, bool printDiff)
{
    bool cmp = true;

    if (bus1->dynType != bus2->dynType) {
        cmp = false;
        if (printDiff) {
            printf("dynamic type is different\n");
        }
    }

    if (std::abs(bus1->localBaseVoltage - bus2->localBaseVoltage) > 0.00001) {
        cmp = false;
        if (printDiff) {
            printf("base voltage is different b1=%f <> b2=%f\n",
                   bus1->localBaseVoltage,
                   bus2->localBaseVoltage);
        }
    }
    if (std::abs(bus1->systemBasePower - bus2->systemBasePower) > 0.00001) {
        cmp = false;
        if (printDiff) {
            printf("base power is different b1=%f <> b2=%f\n",
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
                printf("bus voltages are different b1=%f <> b2=%f\n",
                       bus1->getVoltage(),
                       bus2->getVoltage());
            }
        }
        if (std::abs(bus1->getAngle() - bus2->getAngle()) > 0.001) {
            cmp = false;
            if (printDiff) {
                printf("bus angles are different b1=%f <> b2=%f\n",
                       bus1->getAngle(),
                       bus2->getAngle());
            }
        }
        auto diff = std::abs(bus1->getLoadReal() - bus2->getLoadReal());
        if ((diff > 0.02) && (diff / std::abs(bus1->getLoadReal()) > 0.01)) {
            cmp = false;
            if (printDiff) {
                printf("real load is different b1=%f <> b2=%f\n",
                       bus1->getLoadReal(),
                       bus2->getLoadReal());
            }
        }
        diff = std::abs(bus1->getLoadReactive() - bus2->getLoadReactive());
        if ((diff > 0.02) && (diff / std::abs(bus1->getLoadReactive()) > 0.01)) {
            cmp = false;
            if (printDiff) {
                printf("reactive load is different b1=%f <> b2=%f\n",
                       bus1->getLoadReactive(),
                       bus2->getLoadReactive());
            }
        }
        diff = std::abs(bus1->getGenerationReal() - bus2->getGenerationReal());
        if ((diff > 0.02) && (diff / std::abs(bus1->getGenerationReal()) > 0.01)) {
            cmp = false;
            if (printDiff) {
                printf("real generation is different b1=%f <> b2=%f\n",
                       bus1->getGenerationReal(),
                       bus2->getGenerationReal());
            }
        }
        diff = std::abs(bus1->getGenerationReactive() - bus2->getGenerationReactive());
        if ((diff > 0.02) && (diff / std::abs(bus1->getGenerationReactive()) > 0.01)) {
            cmp = false;
            if (printDiff) {
                printf("reactive generation is different b1=%f <> b2=%f\n",
                       bus1->getGenerationReactive(),
                       bus2->getGenerationReactive());
            }
        }
        diff = std::abs(bus1->getLinkReal() - bus2->getLinkReal());
        if ((diff > 0.02) && (diff / std::abs(bus1->getLinkReal()) > 0.01)) {
            cmp = false;
            if (printDiff) {
                printf("real link is different b1=%f <> b2=%f\n",
                       bus1->getLinkReal(),
                       bus2->getLinkReal());
            }
        }
        diff = std::abs(bus1->getLinkReactive() - bus2->getLinkReactive());
        if ((diff > 0.02) && (diff / std::abs(bus1->getLinkReactive()) > 0.01)) {
            cmp = false;
            if (printDiff) {
                printf("reactive link is different b1=%f <> b2=%f\n",
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

gridBus* getMatchingBus(gridBus* bus, const gridPrimary* src, gridPrimary* sec)
{
    if (bus->isRoot()) {
        return nullptr;
    }
    if (isSameObject(bus->getParent(), src))  // if this is true then things are easy
    {
        return sec->getBus(bus->locIndex);
    }

    auto par = dynamic_cast<gridPrimary*>(bus->getParent());
    if (par == nullptr) {
        return nullptr;
    }
    std::vector<index_t> lkind = {bus->locIndex};
    while (!isSameObject(par, src)) {
        lkind.push_back(par->locIndex);
        par = dynamic_cast<gridPrimary*>(par->getParent());
        if (par == nullptr) {
            return nullptr;
        }
    }
    // now work our way backwards through the secondary
    par = sec;
    for (auto kk = lkind.size() - 1; kk > 0; --kk) {
        par = static_cast<gridPrimary*>(par->getArea(lkind[kk]));
    }
    return par->getBus(lkind[0]);
}

}  // namespace griddyn
