/*
 * Copyright (c) 2014-2020, Lawrence Livermore National Security
 * See the top-level NOTICE for additional details. All rights reserved.
 * SPDX-License-Identifier: BSD-3-Clause
 */

#include "acBus.h"

#include "../Area.h"
#include "../Generator.h"
#include "../Link.h"
#include "../Load.h"
#include "../blocks/derivativeBlock.h"
#include "../simulation/contingency.h"
#include "core/coreExceptions.h"
#include "core/coreObjectTemplates.hpp"
#include "core/coreOwningPtr.hpp"
#include "core/objectFactoryTemplates.hpp"
#include "gmlc/utilities/vectorOps.hpp"
//#include "matrixDataSparse.hpp"
#include "gmlc/utilities/stringOps.h"
#include "griddyn/compiler-config.h"
#include <cassert>
#include <cmath>
#include <iostream>
#include <memory>

namespace griddyn {
// factory is for the cloning function
static childTypeFactory<acBus, gridBus> gbfac("bus", stringVec{"psystem"});

using namespace units;
using namespace gmlc::utilities;

acBus::acBus(const std::string& objName): gridBus(objName), busController(this)
{
    // default values
}

acBus::acBus(double vStart, double angleStart, const std::string& objName):
    gridBus(vStart, angleStart, objName), aTarget(angleStart), vTarget(vStart), busController(this)
{
    // default values
}

acBus::~acBus() = default;

coreObject* acBus::clone(coreObject* obj) const
{
    auto* nobj = cloneBaseFactory<acBus, gridBus>(this, obj, &gbfac);
    if (nobj == nullptr) {
        return obj;
    }

    nobj->vTarget = vTarget;

    nobj->Vmin = Vmin;
    nobj->Vmax = Vmax;
    nobj->prevType = prevType;
    nobj->freq = freq;
    nobj->prevPower = prevPower;
    nobj->participation = participation;
    nobj->Tw = Tw;

    nobj->busController.autogenP = busController.autogenP;
    nobj->busController.autogenQ = busController.autogenQ;
    nobj->busController.autogenDelay = busController.autogenDelay;

    if (opFlags[compute_frequency]) {
        if (fblock) {
            nobj->fblock = coreOwningPtr<Block>(static_cast<Block*>(fblock->clone(nullptr)));
            nobj->addSubObject(nobj->fblock.get());
        }
    }
    return nobj;
}

void acBus::disable()
{
    coreObject::disable();  // NOLINT
    alert(this, STATE_COUNT_CHANGE);
    for (auto& link : attachedLinks) {
        link->disable();
    }
}

void acBus::add(coreObject* obj)
{
    auto* bus = dynamic_cast<acBus*>(obj);
    if (bus != nullptr) {
        return add(bus);
    }
    gridBus::add(obj);
}

void acBus::add(acBus* bus)
{
    if (bus == nullptr) {
        return;
    }
    bus->busController.directBus = this;
    bus->opFlags.set(directconnect);
    if (getID() > bus->getID()) {
        bus->makeNewOID();  // update the ID to make it higher
    }
    mergeBus(bus);  // load into the merge controls
    getParent()->add(
        bus);  // now add the bus to the parent object since buses can't directly contain other
    // buses
}

void acBus::remove(coreObject* obj)
{
    auto* bus = dynamic_cast<acBus*>(obj);
    if (bus != nullptr) {
        return (remove(bus));
    }
    gridBus::remove(obj);
}

void acBus::remove(acBus* bus)
{
    if (bus == nullptr) {
        return;
    }
    if (bus->busController.masterBus->getID() == getID()) {
        if (bus->checkFlag(directconnect)) {
            bus->opFlags.reset(directconnect);
            bus->busController.directBus = nullptr;
        }
        unmergeBus(bus);
    }
}

void acBus::alert(coreObject* obj, int code)
{
    switch (code) {
        case VOLTAGE_CONTROL_UPDATE:
            if (opFlags[pFlow_initialized]) {
                busController.updateVoltageControls();
            }
            break;
        case VERY_LOW_VOLTAGE_ALERT:
            // set an internal flag
            opFlags.set(prev_low_voltage_alert);
            // forward the alert
            getParent()->alert(obj, code);
            break;
        case POWER_CONTROL_UDPATE:
            if (opFlags[pFlow_initialized]) {
                busController.updatePowerControls();
            }
            break;
        case PV_CONTROL_UDPATE:
            if (opFlags[pFlow_initialized]) {
                busController.updateVoltageControls();
                busController.updatePowerControls();
            }
            break;
        case OBJECT_NAME_CHANGE:
        case OBJECT_ID_CHANGE:
            break;
        case POTENTIAL_FAULT_CHANGE:
            if (opFlags[disconnected]) {
                reconnect();
            }
            FALLTHROUGH
        default:
            gridPrimary::alert(obj, code);  // NOLINT
    }
}

// dynInitializeB states
void acBus::pFlowObjectInitializeA(coreTime time0, std::uint32_t flags)
{
    if (Vtol < 0) {
        Vtol = getRoot()->get("voltagetolerance");
    }
    if (Atol < 0) {
        Atol = getRoot()->get("angletolerance");
    }

    offsets.local().local.vSize = 1;
    offsets.local().local.aSize = 1;
    // run the subObjects

    int activeSecondary = 0;

    for (auto& gen : attachedGens) {
        gen->pFlowInitializeA(time0, flags);
        if (gen->isConnected()) {
            ++activeSecondary;
        }
    }
    for (auto& load : attachedLoads) {
        load->pFlowInitializeA(time0, flags);
        if (load->isConnected()) {
            ++activeSecondary;
        }
    }
    if (!(opFlags[use_autogen])) {
        if (CHECK_CONTROLFLAG(flags, auto_bus_disconnect)) {
            int activeLink = 0;
            for (auto* lnk : attachedLinks) {
                if (lnk->isConnected()) {
                    ++activeLink;
                }
            }

            if (activeSecondary == 0) {
                if (activeLink < 2) {
                    LOG_WARNING("No load no gen, 1 active line ,bus is irrelevant disconnecting");
                    disconnect();
                }
            }
        }
    }
    // load up control objects
    if (type == busType::PV) {
        if (busController.vControlObjects.empty()) {
            LOG_NORMAL("PV BUS with no controllers: converting to PQ");
            type = busType::PQ;
        }
    }

    if ((type == busType::PV) || (type == busType::SLK)) {
        voltage = vTarget;
    }
    if ((type == busType::SLK) || (type == busType::afix)) {
        angle = aTarget;
        bool Padj = opFlags[use_autogen];

        if (!busController.pControlObjects.empty()) {
            Padj = true;  // We have a P control object
        }

        if (!Padj) {  // if there is no generator listed on SLK or afix bus we need one for
                      // accounting purposes so add a
            // default one
            if (!CHECK_CONTROLFLAG(flags, no_auto_autogen)) {
                LOG_NORMAL("SLK BUS with No adjustable power elements, enabling auto_gen");
                opFlags.set(use_autogen);
            }
        }
    } else {
        if (CHECK_CONTROLFLAG(flags, auto_bus_disconnect)) {
            if ((attachedGens.empty()) && (attachedLoads.empty()) && (attachedLinks.size() == 1)) {
                if (!opFlags[use_autogen]) {
                    LOG_WARNING("No load no gen, 1 line ,bus is irrelevant disabling");
                    disconnect();
                    return;
                }
            }
        }
    }

    // if there is only a single control then forward the bus max and mins to the control objects

    if ((type == busType::PV) || (type == busType::SLK)) {
        if (busController.vControlObjects.size() == 1) {
            if (busController.Qmax < kHalfBigNum) {
                auto temp = busController.vControlObjects[0]->get("qmax");
                if (temp > kHalfBigNum) {
                    busController.vControlObjects[0]->set("qmax", busController.Qmax);
                }
            }
            if (busController.Qmin > -kHalfBigNum) {
                auto temp = busController.vControlObjects[0]->get("qmin");
                if (temp < -kHalfBigNum) {
                    busController.vControlObjects[0]->set("qmin", busController.Qmin);
                }
            }
        }
    }
    if ((type == busType::afix) || (type == busType::SLK)) {
        if (busController.pControlObjects.size() == 1) {
            if (busController.Pmax < kHalfBigNum) {
                auto temp = busController.pControlObjects[0]->get("pmax");
                if (temp > kHalfBigNum) {
                    busController.pControlObjects[0]->set("pmax", busController.Pmax);
                }
            }
            if (busController.Pmin > -kHalfBigNum) {
                auto temp = busController.pControlObjects[0]->get("pmin");
                if (temp < -kHalfBigNum) {
                    busController.pControlObjects[0]->set("qmin", busController.Pmin);
                }
            }
        }
    }

    // update the controls
    if ((type == busType::PV) || (type == busType::SLK)) {
        busController.updateVoltageControls();
    }
    if ((type == busType::afix) || (type == busType::SLK)) {
        busController.updatePowerControls();
    }

    if (CHECK_CONTROLFLAG(flags, low_voltage_checking)) {
        opFlags.set(low_voltage_check_flag);
    }
    updateFlags();
}

void acBus::pFlowObjectInitializeB()
{
    gridBus::pFlowObjectInitializeB();

    m_dstate_dt.resize(3, 0);
    m_dstate_dt[angleInLocation] = systemBaseFrequency * (freq - 1.0);
    m_state = {voltage, angle, freq};
    outputs[voltageInLocation] = voltage;
    outputs[angleInLocation] = angle;
    outputs[frequencyInLocation] = freq;
    lastSetTime = prevTime;
    computePowerAdjustments();
    if (opFlags[use_autogen]) {
        if (busController.autogenP < kHalfBigNum) {
            busController.autogenPact = -(S.loadP + S.genP - busController.autogenP);
            LOG_TRACE("autogen P=" + std::to_string(busController.autogenPact));
        }
        if (busController.autogenQ < kHalfBigNum) {
            busController.autogenQact = -(S.loadQ + S.genQ - busController.autogenQ);
        }
    }
}

// TODO(PT):: transfer these functions to the busController
void acBus::mergeBus(gridBus* mbus)
{
    auto* acmbus = dynamic_cast<acBus*>(mbus);
    if (acmbus == nullptr) {
        return;
    }
    // bus with the lowest ID is the master
    if (getID() < mbus->getID()) {
        if (opFlags[slave_bus])  // if we are already a slave, forward the merge to the master
        {
            busController.masterBus->mergeBus(mbus);
        } else {
            if (mbus->checkFlag(slave_bus)) {
                if (getID() != acmbus->busController.masterBus->getID()) {
                    mergeBus(acmbus->busController.masterBus);
                }
            } else {
                // This bus becomes the master of mbus
                acmbus->busController.masterBus = this;
                acmbus->opFlags.set(slave_bus);
                busController.slaveBusses.push_back(acmbus);
                for (auto* sb : acmbus->busController.slaveBusses) {
                    busController.slaveBusses.push_back(sb);
                    sb->busController.masterBus = this;
                }
                acmbus->busController.slaveBusses.clear();
            }
        }
    } else if (getID() > mbus->getID()) {
        // mbus is now this buses master
        if (opFlags[slave_bus]) {
            // if we are already a slave forward the merge to the master
            if (busController.masterBus->getID() != mbus->getID()) {
                busController.masterBus->mergeBus(mbus);
            }
        } else {  // we were a master now mbus is the master
            if (busController.slaveBusses.empty()) {  // no slave buses
                busController.masterBus = mbus;
                acmbus->busController.slaveBusses.push_back(this);
            } else {
                if (mbus->checkFlag(slave_bus)) {
                    acmbus->busController.masterBus->mergeBus(this);
                } else {
                    busController.masterBus = mbus;
                    acmbus->busController.slaveBusses.push_back(this);
                    for (auto* sb : busController.slaveBusses) {
                        acmbus->busController.slaveBusses.push_back(sb);
                        sb->busController.masterBus = mbus;
                    }
                    busController.slaveBusses.clear();
                }
            }
        }
    }
}

void acBus::unmergeBus(gridBus* mbus)
{
    auto* acmbus = dynamic_cast<acBus*>(mbus);
    if (acmbus == nullptr) {
        return;
    }
    if (opFlags[slave_bus]) {
        if (mbus->checkFlag(slave_bus)) {
            if (acmbus->busController.masterBus->getID() == busController.masterBus->getID()) {
                busController.masterBus->unmergeBus(mbus);
            }
        } else if (busController.masterBus->getID() == mbus->getID()) {
            mbus->unmergeBus(this);  // flip it around so this bus is unmerged from mbus
        }
    } else {  // in the masterbus
        if ((mbus->checkFlag(slave_bus)) && (getID() == acmbus->busController.masterBus->getID())) {
            for (auto& eb : busController.slaveBusses) {
                eb->opFlags.reset(slave_bus);
            }
            checkMerge();
            mbus->checkMerge();
        }
    }
}

void acBus::checkMerge()
{
    if (!isEnabled()) {
        return;
    }
    if (opFlags[directconnect]) {
        busController.directBus->mergeBus(this);
    }
    for (auto& lnk : attachedLinks) {
        lnk->checkMerge();
    }
}

// function to reset the bus type and voltage

void acBus::reset(reset_levels level)
{
    gridBus::reset(level);
    oCount = 0;
    if (prevType != type) {
        type = prevType;
        alert(this, JAC_COUNT_CHANGE);
    }
    switch (level) {
        case reset_levels::minimal:
            break;
        case reset_levels::full:
        case reset_levels::voltage_angle:
            if ((type == busType::PV) || (type == busType::SLK)) {
                voltage = vTarget;
            } else {
                voltage = 1.0;
            }

            if ((type == busType::SLK) || (type == busType::afix)) {
                angle = aTarget;
            } else {
                angle = 0.0;
            }

            break;
        case reset_levels::voltage:
            if ((type == busType::PV) || (type == busType::SLK)) {
                voltage = vTarget;
            } else {
                voltage = 1.0;
            }
            break;
        case reset_levels::angle:
            if ((type == busType::SLK) || (type == busType::afix)) {
                angle = aTarget;
            } else {
                angle = 0.0;
            }
            break;
        case reset_levels::low_voltage_pflow:
            if (voltage < 0.6) {
                voltage = 0.9;
                angle = getAverageAngle();
            }
            break;
        case reset_levels::low_voltage_dyn0:
            if (prevDynType != dynType) {
                dynType = prevDynType;
                double nAngle = static_cast<Area*>(getParent())
                                    ->getMasterAngle(emptyStateData, cLocalSolverMode);
                angle = angle + (nAngle - refAngle);
                alert(this, JAC_COUNT_CHANGE);
            } else if (voltage < 0.1) {
                voltage = 1.0;
                angle = getAverageAngle();
            }
            break;
        case reset_levels::low_voltage_dyn1:
            if (prevDynType != dynType) {
                dynType = prevDynType;
                double nAngle = static_cast<Area*>(getParent())
                                    ->getMasterAngle(emptyStateData, cLocalSolverMode);
                angle = angle + (nAngle - refAngle);
                alert(this, JAC_COUNT_CHANGE);
            }
            if (!attachedGens.empty()) {
            } else if (voltage < 0.5) {
                voltage = 0.7;
                angle = getAverageAngle();
            }
            break;
        case reset_levels::low_voltage_dyn2:
            if (prevDynType != dynType) {
                dynType = prevDynType;
                double nAngle = static_cast<Area*>(getParent())
                                    ->getMasterAngle(emptyStateData, cLocalSolverMode);
                angle = angle + (nAngle - refAngle);
                alert(this, JAC_COUNT_CHANGE);
            }
            if (!attachedGens.empty()) {
                if (voltage < 0.5) {
                    voltage = 0.7;
                    for (auto& gen : attachedGens) {
                        gen->algebraicUpdate(
                            {voltage, angle}, emptyStateData, nullptr, cLocalSolverMode, 1.0);
                    }
                }
            } else if (voltage < 0.6) {
                voltage = 0.9;
                angle = getAverageAngle();
            }
            break;
    }
}

double acBus::getAverageAngle() const
{
    if (!attachedLinks.empty()) {
        double a = 0.0;
        double rel = 0.0;
        for (const auto* lnk : attachedLinks) {
            a += lnk->getBusAngle(getID());
            rel += 1.0;
        }
        if (rel > 0.9) {
            return (a / rel);
        }
    }
    return angle;
}

change_code
    acBus::powerFlowAdjust(const IOdata& /*inputs*/, std::uint32_t flags, check_level_t level)
{
    auto out = change_code::no_change;
    if (level == check_level_t::low_voltage_check) {
        if (!isConnected()) {
            return out;
        }
        if (voltage < 1e-8) {
            disconnect();
            out = change_code::jacobian_change;
        }
        if (opFlags[prev_low_voltage_alert]) {
            disconnect();
            opFlags.reset(prev_low_voltage_alert);
            out = change_code::jacobian_change;
        }
        return out;
    }

    if (!CHECK_CONTROLFLAG(flags, ignore_bus_limits)) {
        computePowerAdjustments();
        S.genQ = S.sumQ();
        S.genP = S.sumP();

        switch (type) {
            case busType::SLK:

                if (S.genQ < busController.Qmin) {
                    S.genQ = busController.Qmin;
                    for (auto& vco : busController.vControlObjects) {
                        vco->set("q", "min");
                    }
                    type = busType::afix;
                    alert(this, JAC_COUNT_CHANGE);
                    out = change_code::jacobian_change;
                } else if (S.genQ > busController.Qmax) {
                    S.genQ = busController.Qmax;
                    for (auto& vco : busController.vControlObjects) {
                        vco->set("q", "max");
                    }
                    type = busType::afix;
                    alert(this, JAC_COUNT_CHANGE);
                    out = change_code::jacobian_change;
                }

                break;
            case busType::PQ:
                if (prevType == busType::PV) {
                    if (std::abs(S.genQ - busController.Qmin) < 0.00001) {
                        if (voltage < vTarget) {
                            if (oCount < 5) {
                                voltage = vTarget;
                                type = busType::PV;
                                oCount++;
                                alert(this, JAC_COUNT_CHANGE);
                                out = change_code::jacobian_change;
                                LOG_TRACE("changing from PQ to PV from low voltage");
                            }
                        }
                    } else {
                        if (voltage > vTarget) {
                            if (oCount < 5) {
                                voltage = vTarget;
                                type = busType::PV;
                                oCount++;
                                alert(this, JAC_COUNT_CHANGE);
                                out = change_code::jacobian_change;
                                LOG_TRACE("changing from PQ to PV from high voltage");
                            }
                        }
                    }
                } else if (prevType == busType::SLK) {
                    if (std::abs(S.genQ - busController.Qmin) < 0.00001) {
                        if (voltage < vTarget) {
                            if (oCount < 5) {
                                voltage = vTarget;
                                type = busType::SLK;
                                oCount++;
                                alert(this, JAC_COUNT_CHANGE);
                                out = change_code::jacobian_change;
                            }
                        }
                    } else {
                        if (voltage > vTarget) {
                            if (oCount < 5) {
                                voltage = vTarget;
                                type = busType::SLK;
                                oCount++;
                                alert(this, JAC_COUNT_CHANGE);
                                out = change_code::jacobian_change;
                            }
                        }
                    }
                }

                break;
            case busType::PV:
                if (S.genQ < busController.Qmin) {
                    S.genQ = busController.Qmin;
                    for (auto& vco : busController.vControlObjects) {
                        vco->set("q", "min");
                    }
                    type = busType::PQ;
                    alert(this, JAC_COUNT_CHANGE);
                    out = change_code::jacobian_change;
                    LOG_TRACE("changing from PV to PQ from Qmin");
                } else if (S.genQ > busController.Qmax) {
                    S.genQ = busController.Qmax;
                    for (auto& vco : busController.vControlObjects) {
                        vco->set("q", "max");
                    }
                    type = busType::PQ;
                    alert(this, JAC_COUNT_CHANGE);
                    out = change_code::jacobian_change;
                    LOG_TRACE("changing from PV to PQ from Qmax");
                }
                break;
            case busType::afix:
                if (prevType == busType::SLK) {
                    if (std::abs(S.genQ - busController.Qmin) < 0.00001) {
                        if (voltage < vTarget) {
                            if (oCount < 5) {
                                voltage = vTarget;
                                type = busType::SLK;
                                oCount++;
                                alert(this, JAC_COUNT_CHANGE);
                                out = change_code::jacobian_change;
                            }
                        }
                    } else {
                        if (voltage > vTarget) {
                            if (oCount < 5) {
                                voltage = vTarget;
                                type = busType::SLK;
                                oCount++;
                                alert(this, JAC_COUNT_CHANGE);
                                out = change_code::jacobian_change;
                            }
                        }
                    }
                }

                if (S.genP < busController.Pmin) {
                    S.genP = busController.Pmin;
                    for (auto& pco : busController.pControlObjects) {
                        pco->set("p", "min");
                    }
                    type = busType::PQ;
                    alert(this, JAC_COUNT_CHANGE);
                    out = change_code::jacobian_change;
                    if (prevType == busType::SLK) {
                        alert(this, SLACK_BUS_CHANGE);
                    }
                } else if (S.genP > busController.Pmax) {
                    S.genP = busController.Pmax;
                    type = busType::PQ;
                    for (auto& pco : busController.pControlObjects) {
                        pco->set("p", "max");
                    }
                    alert(this, JAC_COUNT_CHANGE);
                    out = change_code::jacobian_change;
                    if (prevType == busType::SLK) {
                        alert(this, SLACK_BUS_CHANGE);
                    }
                }
        }
        updateLocalCache();
    }
    change_code pout;
    for (auto& gen : attachedGens) {
        if (gen->checkFlag(has_powerflow_adjustments)) {
            pout = gen->powerFlowAdjust({voltage, angle}, flags, level);
            out = (std::max)(pout, out);
        }
    }
    for (auto& ld : attachedLoads) {
        if (ld->checkFlag(has_powerflow_adjustments)) {
            pout = ld->powerFlowAdjust({voltage, angle}, flags, level);
            out = (std::max)(pout, out);
        }
    }
    return out;
}
/*function to check the current status for any limit violations*/
void acBus::pFlowCheck(std::vector<Violation>& violations)
{
    if (voltage > Vmax) {
        Violation violation(getName(), VOLTAGE_OVER_LIMIT_VIOLATION);

        violation.level = voltage;
        violation.limit = Vmax;
        violation.percentViolation =
            (voltage - Vmax) * 100;  // assumes nominal voltage level at 1.0;
        violations.push_back(violation);
    } else if (voltage < Vmin) {
        Violation violation(getName(), VOLTAGE_UNDER_LIMIT_VIOLATION);
        violation.level = voltage;
        violation.limit = Vmin;
        violation.percentViolation =
            (Vmin - voltage) * 100;  // assumes nominal voltage level at 1.0;
        violations.push_back(violation);
    }
}

// dynInitializeB states for dynamic solution
void acBus::dynObjectInitializeA(coreTime time0, std::uint32_t flags)
{
    gridBus::dynObjectInitializeA(time0, flags);
    // find a
    if (!(attachedGens.empty())) {
        double mxpower = 0;
        keyGen = nullptr;
        for (auto& gen : attachedGens) {
            if (gen->isConnected()) {
                if (gen->checkFlag(Generator::generator_flags::internal_frequency_calculation)) {
                    if (gen->getPmax() > mxpower) {
                        keyGen = gen;
                        mxpower = gen->getPmax();
                    }
                }
            }
        }
    }
    if (opFlags[uses_bus_frequency]) {
        if (attachedGens.empty() || keyGen == nullptr) {
            opFlags.set(compute_frequency);
        }
    }
    if (opFlags[compute_frequency]) {
        opFlags.set(uses_bus_frequency);
        LOG_TRACE("computing bus frequency using frequency block");
        if (!fblock) {
            fblock = make_owningPtr<blocks::derivativeBlock>(Tw);
            fblock->setName("frequency_calc");
            fblock->set("k", 1.0 / systemBaseFrequency);
            fblock->addOwningReference();
            addSubObject(fblock.get());
            fblock->parentSetFlag(separate_processing, true, this);
        }
        fblock->dynInitializeA(time0, flags);
    }
    lastSetTime = time0;
}

// dynInitializeB states for dynamic solution part 2  //final clean up
void acBus::dynObjectInitializeB(const IOdata& /*inputs*/,
                                 const IOdata& desiredOutput,
                                 IOdata& fieldSet)
{
    // TODO(PT):: clean up this function
    if (!desiredOutput.empty()) {
        if (desiredOutput[voltageInLocation] > 0) {
            voltage = desiredOutput[voltageInLocation];
        }
        if (desiredOutput[angleInLocation] > -kHalfBigNum) {
            angle = desiredOutput[angleInLocation];
        }
        if (std::abs(desiredOutput[frequencyInLocation] - 1.0) < 0.5) {
            freq = desiredOutput[frequencyInLocation];
        }
    }
    updateLocalCache();
    lastSetTime = prevTime;
    m_state[voltageInLocation] = voltage;
    m_state[angleInLocation] = angle;
    m_state[frequencyInLocation] = freq;
    if (opFlags[use_autogen]) {
        if ((busController.autogenQ > kHalfBigNum) && (attachedGens.empty())) {
            busController.autogenQact = -(S.linkQ + S.loadQ);
        }
        S.genP = busController.autogenPact;
        S.genQ = busController.autogenQact;
    }
    // first get the state size for the internal state ordering
    auto inputs = getOutputs(noInputs, emptyStateData, cLocalSolverMode);
    double Qgap;
    double Pgap;
    int vci = 0;
    int poi = 0;
    auto cid = getID();
    switch (type) {
        case busType::PQ:
            break;
        case busType::PV:
            computePowerAdjustments();
            Qgap = -S.sumQ();
            for (auto& vco : busController.vControlObjects) {
                if (vco->checkFlag(local_voltage_control)) {
                    vco->set("q", -Qgap * busController.vcfrac[vci]);
                } else {
                    busController.proxyVControlObject[poi]->fixPower(
                        busController.proxyVControlObject[poi]->getRealPower(cid),
                        Qgap * busController.vcfrac[vci],
                        cid,
                        cid);
                    ++poi;
                }
                ++vci;
            }
            break;
        case busType::SLK:

            computePowerAdjustments();
            Qgap = -(S.sumQ());
            Pgap = -(S.sumP());

            if (opFlags[identical_PQ_control_objects])  // adjust the power levels together
            {
                for (auto& vco : busController.vControlObjects) {
                    if (vco->checkFlag(local_voltage_control)) {
                        if (busController.vcfrac[vci] > 0.0) {
                            vco->set("q", -Qgap * busController.vcfrac[vci]);
                        }
                        if (busController.pcfrac[vci] > 0.0) {
                            vco->set("p", -Pgap * busController.pcfrac[vci]);
                        }
                    } else {  // use both together on fixpower function
                        busController.proxyVControlObject[poi]->fixPower(
                            -Pgap * busController.pcfrac[vci],
                            -Qgap * busController.vcfrac[vci],
                            cid,
                            cid);
                        ++poi;
                    }
                    ++vci;
                }
            } else {  // adjust the power levels separately
                // adjust the real power flow
                for (auto& pco : busController.pControlObjects) {
                    if (pco->checkFlag(local_voltage_control)) {
                        pco->set("p", -Pgap * busController.pcfrac[vci]);
                    } else {
                        busController.proxyVControlObject[poi]->fixPower(
                            -Pgap * busController.pcfrac[vci],
                            busController.proxyPControlObject[poi]->getReactivePower(cid),
                            cid,
                            cid);
                        ++poi;
                    }
                    ++vci;
                }
                // adjust the reactive power

                vci = 0;
                poi = 0;
                for (auto& vco : busController.vControlObjects) {
                    if (vco->checkFlag(local_voltage_control)) {
                        vco->set("q", -Qgap * busController.vcfrac[vci]);
                    } else {
                        busController.proxyVControlObject[poi]->fixPower(
                            busController.proxyVControlObject[poi]->getRealPower(cid),
                            -Qgap * busController.vcfrac[vci],
                            cid,
                            cid);
                        ++poi;
                    }
                    ++vci;
                }
            }
            break;
        case busType::afix:
            Pgap = -(S.sumP());
            // adjust the real power flow
            for (auto& pco : busController.pControlObjects) {
                if (pco->checkFlag(local_voltage_control)) {
                    pco->set("p", -Pgap * busController.pcfrac[vci]);
                } else {
                    busController.proxyVControlObject[poi]->fixPower(
                        -Pgap * busController.pcfrac[vci],
                        busController.proxyPControlObject[poi]->getReactivePower(cid),
                        cid,
                        cid);
                    ++poi;
                }
                ++vci;
            }
            break;
    }
    IOdata pc;
    // TODO(PT):: Do some thing with the fieldSet
    for (auto& gen : attachedGens) {
        gen->dynInitializeB(inputs, pc, fieldSet);
    }
    for (auto& load : attachedLoads) {
        load->dynInitializeB(inputs, pc, fieldSet);
    }
    if (opFlags[compute_frequency]) {
        IOdata iset(2);
        fblock->dynInitializeB({angle}, {0.0}, iset);
    }
}

void acBus::generationAdjust(double adjustment)
{
    // adjust the real power flow
    int vci = 0;
    for (auto& pco :
         busController.pControlObjects) {  // don't worry about proxy objects for this purpose
        pco->set("adjustment", adjustment * busController.pcfrac[vci]);
        ++vci;
    }
}

void acBus::timestep(coreTime time, const IOdata& /*inputs*/, const solverMode& sMode)
{
    double dt = time - prevTime;
    if (dt < 1.0) {
        if (!m_dstate_dt.empty()) {
            voltage += m_dstate_dt[voltageInLocation] * dt;
        }

        if (isDynamic(sMode)) {
            angle += (freq - 1.0) * systemBaseFrequency * dt;
        }
    }
    IOdata inputs{voltage, angle, freq};
    for (auto& load : attachedLoads) {
        load->timestep(time, inputs, sMode);
    }
    for (auto& gen : attachedGens) {
        gen->timestep(time, inputs, sMode);
    }
    // localConverge (sMode, 0);
    // updateLocalCache ();
    if (opFlags[compute_frequency]) {
        fblock->step(time, angle);
    }
    prevTime = time;
}

static const stringVec locNumStrings{
    "vtarget",
    "atarget",
    "p",
    "q",
};
static const stringVec locStrStrings{"pflowtype", "dyntype"};

static const stringVec flagStrings{"use_frequency"};

void acBus::getParameterStrings(stringVec& pstr, paramStringType pstype) const
{
    getParamString<acBus, gridBus>(this, pstr, locNumStrings, locStrStrings, flagStrings, pstype);
}

void acBus::setFlag(const std::string& flag, bool val)
{
    if (flag == "compute_frequency") {
        if (!opFlags[dyn_initialized]) {
            opFlags.set(compute_frequency);
            if (!fblock) {
                fblock = make_owningPtr<blocks::derivativeBlock>(Tw);
                fblock->setName("frequency_calc");
                fblock->set("k", 1.0 / systemBaseFrequency);
                fblock->addOwningReference();
                addSubObject(fblock.get());
                fblock->parentSetFlag(separate_processing, true, this);
            }
        }
    } else {
        gridBus::setFlag(flag, val);
    }
}

// set properties
void acBus::set(const std::string& param, const std::string& val)
{
    auto val_lowerCase = convertToLowerCase(val);
    if ((param == "type") || (param == "bustype") || (param == "pflowtype")) {
        if ((val_lowerCase == "slk") || (val_lowerCase == "swing") || (val_lowerCase == "slack")) {
            type = busType::SLK;
            prevType = busType::SLK;
        } else if (val_lowerCase == "pv") {
            type = busType::PV;
            prevType = busType::PV;
        } else if (val_lowerCase == "pq") {
            type = busType::PQ;
            prevType = busType::PQ;
        } else if ((val_lowerCase == "dynslk") || (val_lowerCase == "inf") ||
                   (val_lowerCase == "infinite")) {
            type = busType::SLK;
            prevType = busType::SLK;
            dynType = dynBusType::dynSLK;
        } else if ((val_lowerCase == "fixedangle") || (val_lowerCase == "fixangle") ||
                   (val_lowerCase == "ref")) {
            dynType = dynBusType::fixAngle;
        } else if ((val_lowerCase == "fixedvoltage") || (val_lowerCase == "fixvoltage") ||
                   (val_lowerCase == "vfix")) {
            dynType = dynBusType::fixVoltage;
        } else if (val_lowerCase == "afix") {
            type = busType::afix;
            prevType = busType::afix;
        } else if (val_lowerCase == "normal") {
            dynType = dynBusType::normal;
        } else {
            throw(invalidParameterValue(val));
        }
    } else if (param == "dyntype") {
        if ((val_lowerCase == "dynslk") || (val_lowerCase == "inf") || (val_lowerCase == "slk")) {
            dynType = dynBusType::dynSLK;
            type = busType::SLK;
        } else if ((val_lowerCase == "fixedvoltage") || (val_lowerCase == "fixvoltage") ||
                   (val_lowerCase == "vfix")) {
            dynType = dynBusType::fixVoltage;
        } else if ((val_lowerCase == "fixedangle") || (val_lowerCase == "fixangle") ||
                   (val_lowerCase == "ref") || (val_lowerCase == "afix")) {
            dynType = dynBusType::fixAngle;
        } else if ((val_lowerCase == "normal") || (val_lowerCase == "pq")) {
            dynType = dynBusType::normal;
        } else {
            throw(invalidParameterValue(val));
        }
    } else {
        gridBus::set(param, val);
    }
}

void acBus::set(const std::string& param, double val, unit unitType)
{
    if ((param == "voltage") || (param == "vol") || (param == "v") || (param == "vmag") ||
        (param == "v0") || (param == "voltage0")) {
        voltage = convert(val, unitType, puV, systemBasePower, localBaseVoltage);
        if ((type == busType::PV) || (type == busType::SLK)) {
            vTarget = voltage;
        }
    } else if ((param == "angle") || (param == "ang") || (param == "a") || (param == "theta") ||
               (param == "angle0")) {
        angle = convert(val, unitType, rad);
        if ((type == busType::SLK) || (type == busType::afix)) {
            aTarget = angle;
        }
    } else if ((param == "basefrequency") || (param == "basefreq")) {
        systemBaseFrequency = convert(val, unitType, rad / s);

        for (auto& gen : attachedGens) {
            gen->set("basefreq", systemBaseFrequency);
        }
        for (auto& ld : attachedLoads) {
            ld->set("basefreq", systemBaseFrequency);
        }
        if (opFlags[compute_frequency]) {
            fblock->set("k", 1.0 / systemBaseFrequency);
        }
    } else if (param == "vtarget") {
        vTarget = convert(val, unitType, puV, systemBasePower, localBaseVoltage);
        /*powerFlowAdjust the target in all the generators as well*/
        for (auto& gen : attachedGens) {
            gen->set(param, vTarget);
        }
    } else if (param == "atarget") {
        aTarget = convert(val, unitType, rad);
    } else if (param == "qmax") {
        if (opFlags[pFlow_initialized]) {
            if (busController.vControlObjects.size() == 1) {
                busController.vControlObjects[0]->set("qmax", val, unitType);
            } else {
                busController.Qmax =
                    convert(val, unitType, puMW, systemBasePower, localBaseVoltage);
            }
        } else {
            busController.Qmax = convert(val, unitType, puMW, systemBasePower, localBaseVoltage);
        }
    } else if (param == "qmin") {
        if (opFlags[pFlow_initialized]) {
            if (busController.vControlObjects.size() == 1) {
                busController.vControlObjects[0]->set("qmin", val, unitType);
            } else {
                busController.Qmin =
                    convert(val, unitType, puMW, systemBasePower, localBaseVoltage);
            }
        } else {
            busController.Qmin = convert(val, unitType, puMW, systemBasePower, localBaseVoltage);
        }
    } else if (param == "pmax") {
        if (opFlags[pFlow_initialized]) {
            if (busController.pControlObjects.size() == 1) {
                busController.pControlObjects[0]->set("pmax", val, unitType);
            } else {
                busController.Pmax =
                    convert(val, unitType, puMW, systemBasePower, localBaseVoltage);
            }
        } else {
            busController.Pmax = convert(val, unitType, puMW, systemBasePower, localBaseVoltage);
        }
    } else if (param == "pmin") {
        if (opFlags[pFlow_initialized]) {
            if (busController.pControlObjects.size() == 1) {
                busController.pControlObjects[0]->set("pmin", val, unitType);
            } else {
                busController.Pmin =
                    convert(val, unitType, puMW, systemBasePower, localBaseVoltage);
            }
        } else {
            busController.Pmin = convert(val, unitType, puMW, systemBasePower, localBaseVoltage);
        }
    } else if (param == "vmax") {
        Vmax = val;
    } else if (param == "vmin") {
        Vmin = val;
    } else if (param == "autogenp") {
        busController.autogenP = convert(val, unitType, puMW, systemBasePower, localBaseVoltage);
        opFlags.set(use_autogen);
    } else if (param == "autogenq") {
        busController.autogenQ = convert(val, unitType, puMW, systemBasePower, localBaseVoltage);
        opFlags.set(use_autogen);
    } else if (param == "autogendelay") {
        busController.autogenDelay = val;
    } else if ((param == "voltagetolerance") || (param == "vtol")) {
        Vtol = val;
    } else if ((param == "angletolerance") || (param == "atol")) {
        Atol = val;
    } else if (param == "participation") {
        participation = val;
    } else if (param == "tw") {
        Tw = val;
        if (opFlags[compute_frequency]) {
            fblock->set("t1", Tw);
        }
    } else if (param == "lowvdisconnect") {
        if (voltage <= val) {
            disconnect();
        }
    } else {
        gridBus::set(param, val, unitType);
    }
}

void acBus::setVoltageAngle(double Vnew, double Anew)
{
    voltage = Vnew;
    angle = Anew;
    switch (type) {
        case busType::PQ:
            break;
        case busType::PV:
            vTarget = voltage;
            break;
        case busType::SLK:
            vTarget = voltage;
            aTarget = angle;
            break;
        case busType::afix:
            aTarget = angle;
            break;
        default:
            break;
    }
}

static const IOdata kNullVec;

IOdata
    acBus::getOutputs(const IOdata& /*inputs*/, const stateData& sD, const solverMode& sMode) const
{
    if ((isLocal(sMode)) || (sD.empty())) {
        return {voltage, angle, freq};
    }
    return {getVoltage(sD, sMode), getAngle(sD, sMode), getFreq(sD, sMode)};
}

static const IOlocs kNullLocations{kNullLocation, kNullLocation, kNullLocation};

IOlocs acBus::getOutputLocs(const solverMode& sMode) const
{
    if ((!hasAlgebraic(sMode)) || (!isConnected())) {
        return kNullLocations;
    }
    if (sMode.offsetIndex == lastSmode) {
        return outLocs;
    }

    IOlocs newOutLocs(3);
    // auto Aoffset = useAngle(sMode) ? offsets.getAOffset(sMode) : kNullLocation;
    // auto Voffset = useVoltage(sMode) ? offsets.getVOffset(sMode) : kNullLocation;
    auto Aoffset = offsets.getAOffset(sMode);
    auto Voffset = offsets.getVOffset(sMode);

    newOutLocs[voltageInLocation] = Voffset;
    newOutLocs[angleInLocation] = Aoffset;
    if (opFlags[compute_frequency]) {
        index_t toff = kNullLocation;
        if (opFlags[compute_frequency]) {
            toff = fblock->getOutputLoc(sMode);
        } else if (keyGen != nullptr) {
            keyGen->getFreq(emptyStateData, sMode, &toff);
        }

        newOutLocs[frequencyInLocation] = toff;
    } else {
        newOutLocs[frequencyInLocation] = kNullLocation;
    }
    return newOutLocs;
}

index_t acBus::getOutputLoc(const solverMode& sMode, index_t num) const
{
    if (sMode.offsetIndex == lastSmode) {
        if (num < 3) {
            return outLocs[num];
        }
        return kNullLocation;
    }

    switch (num) {
        case voltageInLocation:
            // return useVoltage(sMode) ? offsets.getVOffset(sMode) : kNullLocation;
            return offsets.getVOffset(sMode);
        case angleInLocation:
            // return useAngle(sMode) ? offsets.getAOffset(sMode) : kNullLocation;
            return offsets.getAOffset(sMode);
        case frequencyInLocation: {
            if (opFlags[compute_frequency]) {
                return fblock->getOutputLoc(sMode);
            }
            if (keyGen != nullptr) {
                index_t loc;
                keyGen->getFreq(emptyStateData, sMode, &loc);
                return loc;
            }
            return kNullLocation;
        }
        default:
            return kNullLocation;
    }
}

double acBus::getVoltage(const double state[], const solverMode& sMode) const
{
    if (isLocal(sMode)) {
        return voltage;
    }
    // if (useVoltage(sMode))
    {
        auto Voffset = offsets.getVOffset(sMode);
        return (Voffset != kNullLocation) ? state[Voffset] : voltage;
    }
    // return voltage;
}

double acBus::getAngle(const double state[], const solverMode& sMode) const
{
    if (isLocal(sMode)) {
        return angle;
    }
    // if (useAngle(sMode))
    {
        auto Aoffset = offsets.getAOffset(sMode);
        return (Aoffset != kNullLocation) ? state[Aoffset] : angle;
    }
    // return angle;
}

double acBus::getVoltage(const stateData& sD, const solverMode& sMode) const
{
    if (isLocal(sMode)) {
        return voltage;
    }
    if (hasAlgebraic(sMode)) {
        auto Voffset = offsets.getVOffset(sMode);
        return (Voffset != kNullLocation) ? sD.state[Voffset] : voltage;
    }
    if (sD.algState != nullptr) {
        auto Voffset = offsets.getVOffset(offsets.getSolverMode(sMode.pairedOffsetIndex));
        return (Voffset != kNullLocation) ? sD.algState[Voffset] : voltage;
    }
    if (sD.fullState != nullptr) {
        auto Voffset = offsets.getVOffset(offsets.getSolverMode(sMode.pairedOffsetIndex));
        return (Voffset != kNullLocation) ? sD.fullState[Voffset] : voltage;
    }
    return voltage;
}

double acBus::getAngle(const stateData& sD, const solverMode& sMode) const
{
    if (isLocal(sMode)) {
        return angle;
    }
    if (hasAlgebraic(sMode)) {
        auto Aoffset = offsets.getAOffset(sMode);
        return (Aoffset != kNullLocation) ? sD.state[Aoffset] : angle;
    }
    if (sD.algState != nullptr) {
        auto Aoffset = offsets.getAOffset(offsets.getSolverMode(sMode.pairedOffsetIndex));
        return (Aoffset != kNullLocation) ? sD.algState[Aoffset] : angle;
    }
    if (sD.fullState != nullptr) {
        auto Aoffset = offsets.getAOffset(offsets.getSolverMode(sMode.pairedOffsetIndex));
        return (Aoffset != kNullLocation) ? sD.fullState[Aoffset] : angle;
    }
    return angle;
}

double acBus::getFreq(const stateData& sD, const solverMode& sMode) const
{
    double f = freq;
    if (opFlags[uses_bus_frequency]) {
        if (isDynamic(sMode)) {
            if (opFlags[compute_frequency]) {
                f = fblock->getOutput(kNullVec, sD, sMode) + 1.0;
            } else if (keyGen != nullptr) {
                f = keyGen->getFreq(sD, sMode);
            }
        }
    }
    return f;
}

int acBus::propogatePower(bool makeSlack)
{
    int ret = 0;
    if (makeSlack) {
        prevType = type;
        type = busType::SLK;
    }
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
        return ret;
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
            /*ret = */ unfixed_line->fixPower(-Pexp, -Qexp, getID(), getID());
        }
    } else {  // no lines so adjust the generators and load
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
        } else {  // TODO(PT):deal with multiple adjustable controls
            return 0;
        }
    }
    return 0;
}
// -------------------- Power Flow --------------------

void acBus::registerVoltageControl(gridComponent* comp)
{
    bool update = ((opFlags[pFlow_initialized]) && (type != busType::PQ));
    busController.addVoltageControlObject(comp, update);
}

void acBus::removeVoltageControl(gridComponent* comp)
{
    busController.removeVoltageControlObject(comp->getID(), opFlags[pFlow_initialized]);
}

void acBus::registerPowerControl(gridComponent* comp)
{
    bool update = ((opFlags[pFlow_initialized]) && (type != busType::PQ));
    busController.addPowerControlObject(comp, update);
}

void acBus::removePowerControl(gridComponent* comp)
{
    busController.removePowerControlObject(comp->getID(), opFlags[pFlow_initialized]);
}

// guessState the solution
void acBus::guessState(coreTime time, double state[], double dstate_dt[], const solverMode& sMode)
{
    auto Voffset = offsets.getVOffset(sMode);
    auto Aoffset = offsets.getAOffset(sMode);

    if (!opFlags[slave_bus]) {
        if (Voffset != kNullLocation) {
            state[Voffset] = voltage;

            if (hasDifferential(sMode)) {
                dstate_dt[Voffset] = 0.0;
            }
        }
        if (Aoffset != kNullLocation) {
            state[Aoffset] = angle;
            if (hasDifferential(sMode)) {
                dstate_dt[Aoffset] = 0.0;
            }
        }
    }
    gridComponent::guessState(time, state, dstate_dt, sMode);
}

// set algebraic and dynamic variables assume preset to differential
void acBus::getVariableType(double sdata[], const solverMode& sMode)
{
    auto Voffset = offsets.getVOffset(sMode);
    if (Voffset != kNullLocation) {
        sdata[Voffset] = ALGEBRAIC_VARIABLE;
    }

    auto Aoffset = offsets.getAOffset(sMode);
    if (Aoffset != kNullLocation) {
        sdata[Aoffset] = ALGEBRAIC_VARIABLE;
    }
    gridComponent::getVariableType(sdata, sMode);
}

void acBus::getTols(double tols[], const solverMode& sMode)
{
    auto Voffset = offsets.getVOffset(sMode);
    if (Voffset != kNullLocation) {
        tols[Voffset] = Vtol;
    }
    auto Aoffset = offsets.getAOffset(sMode);
    if (Aoffset != kNullLocation) {
        tols[Aoffset] = Atol;
    }

    gridComponent::getTols(tols, sMode);
}

// pass the solution
void acBus::setState(coreTime time,
                     const double state[],
                     const double dstate_dt[],
                     const solverMode& sMode)
{
    auto Aoffset = offsets.getAOffset(sMode);
    auto Voffset = offsets.getVOffset(sMode);

    if (isDAE(sMode)) {
        if (Voffset != kNullLocation) {
            voltage = state[Voffset];
            m_dstate_dt[voltageInLocation] = dstate_dt[Voffset];
        }
        if (Aoffset != kNullLocation) {
            angle = state[Aoffset];
            m_dstate_dt[angleInLocation] = dstate_dt[Aoffset];
        }
    } else if (hasAlgebraic(sMode)) {
        if (Voffset != kNullLocation) {
            if (time > prevTime) {
                m_dstate_dt[voltageInLocation] =
                    (state[Voffset] - m_state[voltageInLocation]) / (time - lastSetTime);
            }
            voltage = state[Voffset];
        }
        if (Aoffset != kNullLocation) {
            if (time > prevTime) {
                m_dstate_dt[angleInLocation] =
                    (state[Aoffset] - -m_state[angleInLocation]) / (time - lastSetTime);
            }
            angle = state[Aoffset];
        }
        lastSetTime = time;
    }
    gridBus::setState(time, state, dstate_dt, sMode);

    if (opFlags[compute_frequency]) {
        // fblock->setState(time, state, dstate_dt, sMode);
    } else if ((isDynamic(sMode)) && (keyGen != nullptr)) {
        freq = keyGen->getFreq(emptyStateData, sMode);
    }
    //    assert(voltage > 0.0);
}

#define TRACE_LOG_ENABLE
// residual
void acBus::residual(const IOdata& inputs,
                     const stateData& sD,
                     double resid[],
                     const solverMode& sMode)
{
    gridBus::residual(inputs, sD, resid, sMode);

    auto Aoffset = offsets.getAOffset(sMode);
    auto Voffset = offsets.getVOffset(sMode);

    // output
    if (hasAlgebraic(sMode)) {
        if (Voffset != kNullLocation) {
            if (useVoltage(sMode)) {
                assert(!std::isnan(S.linkQ));

                resid[Voffset] = S.sumQ();
#ifdef TRACE_LOG_ENABLE
                if (std::abs(resid[Voffset]) > 0.5) {
                    LOG_TRACE("sid=" + std::to_string(sD.seqID) +
                              "::high voltage resid = " + std::to_string(resid[Voffset]));
                }
#endif
            } else {
                resid[Voffset] = sD.state[Voffset] - voltage;
            }
        }
        if (Aoffset != kNullLocation) {
            if (useAngle(sMode)) {
                assert(!std::isnan(S.linkP));
                resid[Aoffset] = S.sumP();
#ifdef TRACE_LOG_ENABLE
                if (std::abs(resid[Aoffset]) > 0.5) {
                    LOG_TRACE("sid=" + std::to_string(sD.seqID) +
                              "::high angle resid = " + std::to_string(resid[Aoffset]));
                }
#endif
                // assert(std::abs(resid[Aoffset])<0.1);
            } else {
                resid[Aoffset] = sD.state[Aoffset] - angle;
            }
        }
        if (isExtended(sMode)) {
            auto offset = offsets.getAlgOffset(sMode);
            // there is no function for this, the control must come from elsewhere in the state
            resid[offset] = 0;
            resid[offset + 1] = 0;
        }
    }

    if ((fblock) && (isDynamic(sMode))) {
        fblock->blockResidual(getAngle(sD, sMode), 0, sD, resid, sMode);
    }
}

void acBus::derivative(const IOdata& inputs,
                       const stateData& sD,
                       double deriv[],
                       const solverMode& sMode)
{
    gridBus::derivative(inputs, sD, deriv, sMode);
    if (opFlags[compute_frequency]) {
        fblock->blockDerivative(getAngle(sD, sMode), 0.0, sD, deriv, sMode);
    }
}

// Jacobian
void acBus::jacobianElements(const IOdata& inputs,
                             const stateData& sD,
                             matrixData<double>& md,
                             const IOlocs& inputLocs,
                             const solverMode& sMode)
{
    gridBus::jacobianElements(inputs, sD, md, inputLocs, sMode);

    // deal with the frequency block
    auto Aoffset = offsets.getAOffset(sMode);
    if ((fblock) && (isDynamic(sMode))) {
        fblock->blockJacobianElements(outputs[angleInLocation], 0.0, sD, md, Aoffset, sMode);
    }

    computeDerivatives(sD, sMode);
    if (isDifferentialOnly(sMode)) {
        return;
    }

    // compute the bus Jacobian elements themselves
    // printf("t=%f,id=%d, dpdt=%f, dpdv=%f, dqdt=%f, dqdv=%f\n", time, id, Ptii, Pvii, Qvii, Qtii);

    auto Voffset = offsets.getVOffset(sMode);

    if (Voffset != kNullLocation) {
        if (useVoltage(sMode)) {
            md.assignCheckCol(Voffset, Aoffset, partDeriv.at(QoutLocation, angleInLocation));
            md.assign(Voffset, Voffset, partDeriv.at(QoutLocation, voltageInLocation));
            if (opFlags[uses_bus_frequency]) {
                md.assignCheckCol(Voffset,
                                  outLocs[frequencyInLocation],
                                  partDeriv.at(QoutLocation, frequencyInLocation));
            }
        } else {
            md.assign(Voffset, Voffset, 1);
        }
    }
    if (Aoffset != kNullLocation) {
        if (useAngle(sMode)) {
            md.assign(Aoffset, Aoffset, partDeriv.at(PoutLocation, angleInLocation));
            md.assignCheckCol(Aoffset, Voffset, partDeriv.at(PoutLocation, voltageInLocation));
            if (opFlags[uses_bus_frequency]) {
                md.assignCheckCol(Aoffset,
                                  outLocs[frequencyInLocation],
                                  partDeriv.at(PoutLocation, frequencyInLocation));
            }
        } else {
            md.assign(Aoffset, Aoffset, 1);
        }
    }

    if (!isConnected()) {
        return;
    }
    of.setArray(md);

    of.setTranslation(PoutLocation, useAngle(sMode) ? outLocs[angleInLocation] : kNullLocation);
    of.setTranslation(QoutLocation, useVoltage(sMode) ? outLocs[voltageInLocation] : kNullLocation);
    if (!isExtended(sMode)) {
        for (auto& gen : attachedGens) {
            if (gen->jacSize(sMode) > 0) {
                gen->outputPartialDerivatives(outputs, sD, of, sMode);
            }
        }
        for (auto& load : attachedLoads) {
            if (load->jacSize(sMode) > 0) {
                load->outputPartialDerivatives(outputs, sD, of, sMode);
            }
        }
    } else {  // make the assignments for the extended state
        auto offset = offsets.getAlgOffset(sMode);
        of.assign(PoutLocation, offset, 1);
        of.assign(QoutLocation, offset + 1, 1);
    }
    auto gid = getID();
    for (auto& link : attachedLinks) {
        link->outputPartialDerivatives(gid, sD, of, sMode);
    }
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

void acBus::voltageUpdate(const stateData& sD,
                          double update[],
                          const solverMode& sMode,
                          double alpha)
{
    if (!isConnected()) {
        return;
    }
    auto Voffset = offsets.getVOffset(sMode);
    double v1 = getVoltage(sD, sMode);
    if (v1 < Vtol) {
        alert(this, VERY_LOW_VOLTAGE_ALERT);
        lowVtime = sD.time;
        return;
    }
    if (!((useVoltage(sMode)) && (Voffset != kNullLocation))) {
        update[Voffset] = v1;
        return;
    }
    bool uA = useAngle(sMode);

    updateLocalCache(noInputs, sD, sMode);
    computeDerivatives(sD, sMode);

    double DP = S.sumP();
    double DQ = (uA) ? (S.sumQ()) : 0;

    double Pvii =
        (uA) ? (partDeriv.at(PoutLocation, voltageInLocation)) : 1.0;  // so not to divide by 0
    double Qvii = partDeriv.at(QoutLocation, voltageInLocation);

    double dV = DQ / Qvii + DP / Pvii;
    if (!std::isfinite(dV))  // probably means the real power computation is invalid
    {
        dV = DQ / Qvii;
    }
    dV = dVcheck(dV, v1, 0.75, 0.15, 1.05);

    assert(std::isfinite(dV));
    assert(v1 - dV > 0);
    update[Voffset] = v1 - dV * alpha;
}

void acBus::algebraicUpdate(const IOdata& inputs,
                            const stateData& sD,
                            double update[],
                            const solverMode& sMode,
                            double alpha)
{
    auto Voffset = offsets.getVOffset(sMode);
    auto Aoffset = offsets.getAOffset(sMode);
    double v1 = getVoltage(sD, sMode);
    double t1 = getAngle(sD, sMode);
    bool uV = (useVoltage(sMode)) && (Voffset != kNullLocation);
    bool uA = (!(opFlags[ignore_angle])) && (useAngle(sMode)) && (Aoffset != kNullLocation);

    if (uV && uA) {
        updateLocalCache(inputs, sD, sMode);
        computeDerivatives(sD, sMode);

        double DP = S.sumP();
        double DQ = S.sumQ();
        double dV;
        double dT;
        double Pvii = partDeriv.at(PoutLocation, voltageInLocation);
        double Ptii = partDeriv.at(PoutLocation, angleInLocation);
        double Qvii = partDeriv.at(QoutLocation, voltageInLocation);
        double Qtii = partDeriv.at(QoutLocation, angleInLocation);
        double detA = solve2x2(Pvii, Ptii, Qvii, Qtii, DP, DQ, dV, dT);
        if (std::isnormal(detA)) {
            dV = dVcheck(dV, v1);
            dT = dAcheck(dT, t1);
        } else if (Ptii != 0) {
            dT = dAcheck(DP / Ptii, t1);
            dV = 0;
        } else {
            dV = dT = 0;
        }
        if (uV) {
            assert(std::isfinite(dV));
            assert(v1 - dV > 0);
            update[Voffset] = v1 - dV * alpha;
        }
        if (uA) {
            assert(std::isfinite(dT));
            update[Aoffset] = t1 - dT * alpha;
        }
    } else if (uA) {
        updateLocalCache(noInputs, sD, sMode);
        computeDerivatives(sD, sMode);

        double DP = S.sumP();
        double Ptii = partDeriv.at(PoutLocation, angleInLocation);
        if (Ptii != 0) {
            double dT = dAcheck(DP / Ptii, t1);
            assert(std::isfinite(dT));
            update[Aoffset] = t1 - dT * alpha;
        } else {
            update[Aoffset] = t1;
        }

        if (Voffset != kNullLocation) {
            update[Voffset] = v1;
        }
    } else if (uV) {
        updateLocalCache(noInputs, sD, sMode);
        computeDerivatives(sD, sMode);

        double DQ = S.sumQ();
        double Qvii = partDeriv.at(QoutLocation, voltageInLocation);
        if (Qvii != 0) {
            double dV = dVcheck(DQ / Qvii, v1);
            assert(std::isfinite(dV));
            update[Voffset] = v1 - dV * alpha;
        } else {
            update[Aoffset] = t1;
        }
        if (Aoffset != kNullLocation) {
            update[Aoffset] = t1;
        }
    } else {
        if (Aoffset != kNullLocation) {
            update[Aoffset] = t1;
        }
        if (Voffset != kNullLocation) {
            update[Voffset] = v1;
        }
    }
    gridBus::algebraicUpdate(noInputs, sD, update, sMode, alpha);
}

void acBus::localConverge(const solverMode& sMode, int mode, double tol)
{
    if (isDifferentialOnly(sMode)) {
        return;
    }
    double v1{voltage};
    double t1{angle};
    double dV;
    double dT;
    double Pvii;
    double Ptii;
    double Qvii;
    double Qtii;
    double err{kBigNum};
    int iteration{1};

    updateLocalCache();
    double DP = S.sumP();
    double DQ = S.sumQ();
    if ((std::abs(DP) < Atol) && (std::abs(DQ) < Vtol)) {
        return;
    }
    if ((S.loadP == 0) && (S.linkP == 0) && (S.loadQ == 0) && (S.linkQ == 0)) {
        if (!checkCapable()) {
            LOG_WARNING("Bus disconnected");
            disconnect();
        }
        return;
    }
    computeDerivatives(emptyStateData, sMode);
    if (mode == 0) {
        Pvii = partDeriv.at(PoutLocation, voltageInLocation);
        Ptii = partDeriv.at(PoutLocation, angleInLocation);
        Qvii = partDeriv.at(QoutLocation, voltageInLocation);
        Qtii = partDeriv.at(QoutLocation, angleInLocation);
        double detA = solve2x2(Pvii, Ptii, Qvii, Qtii, DP, DQ, dV, dT);
        if (std::isnormal(detA)) {
            dV = dVcheck(dV, voltage);
            dT = dAcheck(dT, angle);
        } else if (Ptii != 0) {
            dT = dAcheck(DP / Ptii, angle);
            dV = 0;
        } else {
            dV = dT = 0;
        }
        voltage -= dV;
        angle -= dT;
    } else if (mode == 1) {
        bool not_converged = true;
        while (not_converged) {
            if (iteration > 1) {
                v1 = voltage;
                t1 = angle;

                updateLocalCache();
                computeDerivatives(emptyStateData, sMode);
                DP = S.sumP();
                DQ = S.sumQ();
            }
            switch (getMode(sMode)) {
                case 0:
                    err = std::abs(DP) + std::abs(DQ);
                    break;
                case 1:  // fixA
                    err = std::abs(DQ);
                    break;
                case 2:
                    err = std::abs(DP);
                    break;
            }
            if (err > tol) {
                Pvii = partDeriv.at(PoutLocation, voltageInLocation);
                Ptii = partDeriv.at(PoutLocation, angleInLocation);
                Qvii = partDeriv.at(QoutLocation, voltageInLocation);
                Qtii = partDeriv.at(QoutLocation, angleInLocation);
                double detA = gmlc::utilities::solve2x2(Pvii, Ptii, Qvii, Qtii, DP, DQ, dV, dT);
                if (std::isnormal(detA)) {
                    dV = dVcheck(dV, v1);
                    dT = dAcheck(dT, t1);
                } else if (Ptii != 0) {
                    dT = dAcheck(DP / Ptii, t1);
                    dV = 0;
                } else {
                    dV = dT = 0;
                    not_converged = false;
                }
                voltage -= dV;
                angle -= -dT;
                if (++iteration > 10) {
                    not_converged = false;
                    voltage = v1;
                    angle = t1;
                }
            } else {
                not_converged = false;
            }
        }
    }
}

void acBus::converge(coreTime time,
                     double state[],
                     double dstate_dt[],
                     const solverMode& sMode,
                     converge_mode mode,
                     double tol)
{
    if ((!isEnabled()) || (isDifferentialOnly(sMode)) ||
        (opFlags[disconnected]))  // nothing to do if differential
    {
        return;
    }

    auto Voffset = offsets.getVOffset(sMode);
    auto Aoffset = offsets.getAOffset(sMode);

    bool uV = (useVoltage(sMode)) && (Voffset != kNullLocation);
    bool uA = (useAngle(sMode)) && (Aoffset != kNullLocation);
    stateData sD(time, state, dstate_dt);
    double v1 = uV ? state[Voffset] : voltage;
    double t1 = uA ? state[Aoffset] : angle;
    double v2;
    double t2;
    double f = getFreq(sD, sMode);
    if (v1 <= 0.0) {
        v1 = std::abs(v1 - 0.001);
        if (Voffset != kNullLocation) {
            state[Voffset] = v1;
        }
    }
    double currentModeVlimit = 0.02 * vTarget;
    bool forceUp = false;
    int iteration = 1;
    if (isDAE(sMode)) {
        currentModeVlimit = (!attachedGens.empty()) ? 0.4 : 0.05;
        currentModeVlimit *= vTarget;
    }
    if ((v1 < currentModeVlimit) && (mode != converge_mode::force_voltage_only)) {
        mode = converge_mode::voltage_only;
    }

    double err = computeError(sD, sMode);
    if ((S.loadP == 0) && (S.linkP == 0) && (S.loadQ == 0) && (S.linkQ == 0)) {
        if (!checkCapable()) {
            LOG_WARNING("Bus disconnected");
            disconnect();
        }
        return;
    }
    switch (mode) {
        case converge_mode::high_error_only:
            if (err > 0.5) {
                if (err > 2.0) {
                    algebraicUpdate(noInputs, sD, state, sMode, 1.0);
                    err = computeError(sD, sMode);
                    int loopcnt = 0;
                    while ((err > tol) && (loopcnt < 6)) {
                        voltageUpdate(sD, state, sMode, 1.0);
                        err = computeError(sD, sMode);
                        ++loopcnt;
                    }
                } else {
                    // do the algebraic update twice
                    algebraicUpdate(noInputs, sD, state, sMode, 1.0);
                    algebraicUpdate(noInputs, sD, state, sMode, 1.0);
                }
            }
            break;
        case converge_mode::single_iteration:
        case converge_mode::block_iteration:
            algebraicUpdate(noInputs, sD, state, sMode, 1.0);
            break;
        case converge_mode::local_iteration:
        case converge_mode::strong_iteration:
        case converge_mode::force_strong_iteration:
            while (err > tol) {
                v1 = uV ? state[Voffset] : voltage;
                t1 = uA ? state[Aoffset] : angle;
                if ((v1 < currentModeVlimit) && (mode != converge_mode::force_strong_iteration)) {
                    mode = converge_mode::force_voltage_only;
                    converge(time, state, dstate_dt, sMode, mode, tol);
                    break;
                }
                algebraicUpdate(noInputs, sD, state, sMode, 1.0);
                v2 = uV ? state[Voffset] : voltage;
                t2 = uA ? state[Aoffset] : angle;
                if ((std::abs(v2 - v1) < 1e-9) && (std::abs(t2 - t1) < 1e-9)) {
                    break;
                }
                err = computeError(sD, sMode);
                if (++iteration > 10) {
                    break;
                }
            }
            break;
        case converge_mode::voltage_only:
        case converge_mode::force_voltage_only: {
            bool not_converged = true;
            if (v1 > 0.6) {
                not_converged = false;
            }
            double minV = -kBigNum;
            double pcerr = 120000;
            while (not_converged) {
                if (iteration > 1) {
                    v1 = uV ? state[Voffset] : voltage;
                    if ((v1 > vTarget * 1.1) && (mode != converge_mode::force_voltage_only)) {
                        converge(time,
                                 state,
                                 dstate_dt,
                                 sMode,
                                 converge_mode::force_strong_iteration,
                                 tol);
                        break;
                    }
                }
                updateLocalCache(noInputs, sD, sMode);
                computeDerivatives(sD, sMode);
                double DP = S.sumP();
                double DQ = S.sumQ();
                double cerr1 = DP / v1;
                double cerr2 = DQ / v1;

                if (iteration == 1) {
                    pcerr = cerr2;
                }
                double Pvii = partDeriv.at(PoutLocation, voltageInLocation);
                double Qvii = partDeriv.at(QoutLocation, voltageInLocation);
                double dV;
                if (std::abs(cerr1) + std::abs(cerr2) > tol) {
                    dV = 0.0;
                    if (std::abs(cerr2) > tol) {
                        if (cerr2 < 0) {
                            if ((forceUp) || (iteration == 1)) {
                                dV = -0.1;
                                forceUp = true;
                                iteration = (iteration > 5) ? 5 : iteration;
                            } else {
                                dV = DQ / Qvii + DP / Pvii;
                                if ((!std::isfinite(dV)) ||
                                    ((minV > 0.35) &&
                                     (v1 - dV < minV)))  // probably means the real power
                                                         // computation is invalid
                                {
                                    dV = DQ / Qvii;
                                }
                                dV = dVcheck(dV, v1, 0.75, 0.15, 1.05);
                            }
                        } else {
                            if (pcerr < 0) {
                                if (forceUp) {
                                    minV = v1 - 0.1;
                                }
                            }
                            forceUp = false;
                            dV = DQ / Qvii + DP / Pvii;
                            if ((!std::isfinite(dV)) ||
                                ((minV > 0.35) &&
                                 (v1 - dV <
                                  minV)))  // probably means the real power computation is invalid
                            {
                                dV = DQ / Qvii;
                            }
                            dV = dVcheck(dV, v1, 0.75, 0.15, 1.05);
                        }
                    } else if (std::abs(cerr1) > tol) {
                        dV = DQ / Qvii + DP / Pvii;
                        if ((!std::isfinite(dV)) ||
                            ((minV > 0.35) &&
                             (v1 - dV <
                              minV)))  // probably means the real power computation is invalid
                        {
                            dV = DQ / Qvii;
                            not_converged = false;
                        }
                        dV = dVcheck(dV, v1, 0.75, 0.15, 1.05);
                    } else {
                        not_converged = false;
                    }
                    if (uV) {
                        assert(std::isfinite(dV));
                        assert(v1 - dV > 0);
                        state[Voffset] = v1 - dV;
                    }

                    if (isDynamic(sMode)) {
                        for (auto& gen : attachedGens) {
                            stateData s1;
                            s1.state = state;

                            gen->algebraicUpdate({v1 - dV, t1, f}, s1, state, sMode, 1.0);
                        }
                    }
                    if (++iteration > 10) {
                        not_converged = false;
                    }
                } else {
                    not_converged = false;
                }
            }
        } break;
    }
}

double acBus::computeError(const stateData& sD, const solverMode& sMode)
{
    updateLocalCache(noInputs, sD, sMode);
    double err = 0;
    switch (getMode(sMode)) {
        case 0:  // 0 most common
            err = std::abs(S.sumP()) + std::abs(S.sumQ());
            break;
        case 2:  // PV bus
            err = std::abs(S.sumP());
            break;
        case 1:  // fixA
            err = std::abs(S.sumQ());
            break;
        default:
            break;
    }
    return err;
}

static const stringVec stNames{"voltage", "angle"};
stringVec acBus::localStateNames() const
{
    return stNames;
}

void acBus::setOffsets(const solverOffsets& newOffsets, const solverMode& sMode)
{
    offsets.setOffsets(newOffsets, sMode);
    solverOffsets no(newOffsets);
    no.localIncrement(offsets.getOffsets(sMode));
    for (auto* ld : attachedLoads) {
        ld->setOffsets(no, sMode);
        no.increment(ld->getOffsets(sMode));
    }
    for (auto* gen : attachedGens) {
        gen->setOffsets(no, sMode);
        no.increment(gen->getOffsets(sMode));
    }
    if (opFlags[slave_bus]) {
        auto& so = offsets.getOffsets(sMode);
        const auto& mboffsets = busController.masterBus->getOffsets(sMode);
        so.vOffset = mboffsets.vOffset;
        so.aOffset = mboffsets.aOffset;
    } else {
        if ((fblock) && (isDynamic(sMode))) {
            fblock->setOffsets(no, sMode);
            no.increment(fblock->getOffsets(sMode));
        }
    }
}

void acBus::setOffset(index_t offset, const solverMode& sMode)
{
    for (auto* ld : attachedLoads) {
        ld->setOffset(offset, sMode);
        offset += ld->stateSize(sMode);
    }
    for (auto* gen : attachedGens) {
        gen->setOffset(offset, sMode);
        offset += gen->stateSize(sMode);
    }
    if (opFlags[slave_bus]) {
        auto& so = offsets.getOffsets(sMode);
        const auto& mboffsets = busController.masterBus->getOffsets(sMode);
        so.vOffset = mboffsets.vOffset;
        so.aOffset = mboffsets.aOffset;
    } else {
        if ((fblock) && (isDynamic(sMode))) {
            fblock->setOffset(offset, sMode);
            offset += fblock->stateSize(sMode);
        }
        offsets.setOffset(offset, sMode);
    }
}

void acBus::setRootOffset(index_t Roffset, const solverMode& sMode)
{
    offsets.setRootOffset(Roffset, sMode);
    auto& so = offsets.getOffsets(sMode);
    auto nR = so.local.algRoots + so.local.diffRoots;
    for (auto& gen : attachedGens) {
        gen->setRootOffset(Roffset + nR, sMode);
        nR += gen->rootSize(sMode);
    }
    for (auto& ld : attachedLoads) {
        ld->setRootOffset(Roffset + nR, sMode);
        nR += ld->rootSize(sMode);
    }
    if (opFlags[compute_frequency]) {
        fblock->setRootOffset(Roffset + nR, sMode);
        // nR += fblock->rootSize (sMode);
    }
}

void acBus::reconnect(gridBus* mapBus)
{
    if (opFlags[disconnected]) {
        gridBus::reconnect(mapBus);
        for (auto& sB : busController.slaveBusses) {
            sB->reconnect(this);
        }
    } else {
        return;
    }
}
bool acBus::useAngle(const solverMode& sMode) const
{
    if ((hasAlgebraic(sMode)) && (isConnected())) {
        if (isDynamic(sMode)) {
            if ((dynType == dynBusType::normal) || (dynType == dynBusType::fixVoltage)) {
                return true;
            }
        } else if ((type == busType::PQ) || (type == busType::PV)) {
            return true;
        }
    }
    return false;
}

bool acBus::useVoltage(const solverMode& sMode) const
{
    if ((hasAlgebraic(sMode)) && (isConnected()) && (!isDC(sMode))) {
        if (isDynamic(sMode)) {
            if ((dynType == dynBusType::normal) || (dynType == dynBusType::fixAngle)) {
                return true;
            }
        } else if ((type == busType::PQ) || (type == busType::afix)) {
            return true;
        }
    }
    return false;
}

count_t acBus::getDependencyCount(const solverMode& sMode) const
{
    count_t sum = 0;
    if (isDC(sMode)) {
        for (const auto& load : attachedLoads) {
            sum += load->outputDependencyCount(PoutLocation, sMode);
        }
        for (const auto& gen : attachedGens) {
            sum += gen->outputDependencyCount(PoutLocation, sMode);
        }
        for (const auto& lnk : attachedLinks) {
            sum += lnk->outputDependencyCount(PoutLocation, sMode);
        }
    } else {
        for (const auto& load : attachedLoads) {
            sum += load->outputDependencyCount(PoutLocation, sMode);
            sum += load->outputDependencyCount(QoutLocation, sMode);
        }
        for (const auto& gen : attachedGens) {
            sum += gen->outputDependencyCount(PoutLocation, sMode);
            sum += gen->outputDependencyCount(QoutLocation, sMode);
        }
        for (const auto& lnk : attachedLinks) {
            sum += lnk->outputDependencyCount(PoutLocation, sMode);
            sum += lnk->outputDependencyCount(QoutLocation, sMode);
        }
    }
    return sum;
}

stateSizes acBus::LocalStateSizes(const solverMode& sMode) const
{
    stateSizes busSS;
    if (hasAlgebraic(sMode)) {
        busSS.aSize = 1;
        if (isAC(sMode)) {
            busSS.vSize = 1;
        }
        // check for slave bus mode
        if (opFlags[slave_bus]) {
            busSS.vSize = 0;
            busSS.aSize = 0;
        }

        if (isExtended(sMode))  // in extended state mode we have P and Q as states
        {
            if (isDC(sMode)) {
                busSS.algSize = 1;
            } else {
                busSS.algSize = 2;
            }
        }
    }
    return busSS;
}

count_t acBus::LocalJacobianCount(const solverMode& sMode) const
{
    count_t totaljacSize = 0;
    if (hasAlgebraic(sMode)) {
        if (isDC(sMode)) {
            totaljacSize = 1 + getDependencyCount(sMode);
        } else {
            totaljacSize = 4 + getDependencyCount(sMode);
        }
        // check for slave bus mode
        if (opFlags[slave_bus]) {
            totaljacSize -= (isDC(sMode)) ? 1 : 4;
        }
    }
    return totaljacSize;
}

int acBus::getMode(const solverMode& sMode) const
{
    if (isDynamic(sMode)) {
        if (isDifferentialOnly(sMode)) {
            return 3;
        }
        if (isDC(sMode)) {
            return static_cast<int>(static_cast<unsigned int>(dynType) | 2U);
        }
        return static_cast<int>(dynType);
    }

    if (isDC(sMode)) {
        return static_cast<int>(static_cast<unsigned int>(type) | 2U);
    }

    return static_cast<int>(type);
}

void acBus::updateFlags(bool /*dynOnly*/)
{
    opFlags.reset(preEx_requested);
    opFlags.reset(has_powerflow_adjustments);
    if (prevType == busType::SLK) {
        // check for P limits
        if ((busController.Pmin > -kHalfBigNum) || (busController.Pmax < kHalfBigNum)) {
            opFlags[has_powerflow_adjustments] = true;
        }

        // check for Qlimits
        if ((busController.Qmin > -kHalfBigNum) || (busController.Qmax < kHalfBigNum)) {
            opFlags[has_powerflow_adjustments] = true;
        }
    }

    busController.Qmin = 0;
    busController.Qmax = 0;
    for (auto& gen : attachedGens) {
        if (gen->isEnabled()) {
            opFlags |= gen->cascadingFlags();
            busController.Qmin += gen->getQmin();
            busController.Qmax += gen->getQmax();
        }
    }
    for (auto& load : attachedLoads) {
        if (load->isEnabled()) {
            opFlags |= load->cascadingFlags();
        }
    }
    if (opFlags[compute_frequency]) {
        opFlags |= fblock->cascadingFlags();
    }
    if (prevType == busType::PV) {
        if ((busController.Qmin > -kHalfBigNum) || (busController.Qmax < kHalfBigNum)) {
            opFlags[has_powerflow_adjustments] = true;
        }
    } else if (prevType == busType::afix) {
        if ((busController.Pmin > -kHalfBigNum) || (busController.Pmax < kHalfBigNum)) {
            opFlags[has_powerflow_adjustments] = true;
        }
    }
}

static const IOlocs inLoc{0, 1, 2};

void acBus::computeDerivatives(const stateData& sD, const solverMode& sMode)
{
    if (!isConnected()) {
        return;
    }
    partDeriv.clear();

    for (auto& link : attachedLinks) {
        if (link->isEnabled()) {
            link->updateLocalCache(noInputs, sD, sMode);
            link->ioPartialDerivatives(getID(), sD, partDeriv, inLoc, sMode);
        }
    }
    if (!isExtended(sMode)) {
        for (auto& gen : attachedGens) {
            if (gen->isConnected()) {
                gen->ioPartialDerivatives(outputs, sD, partDeriv, inLoc, sMode);
            }
        }
        for (auto& load : attachedLoads) {
            if (load->isConnected()) {
                load->ioPartialDerivatives(outputs, sD, partDeriv, inLoc, sMode);
            }
        }
    }
}

// computed power at bus
void acBus::updateLocalCache(const IOdata& inputs, const stateData& sD, const solverMode& sMode)
{
    if (!S.needsUpdate(sD)) {
        return;
    }

    if (!isConnected()) {
        return;
    }
    gridBus::updateLocalCache(inputs, sD, sMode);
    if (sMode.offsetIndex != lastSmode) {
        outLocs = getOutputLocs(sMode);
    }
}

// computed power at bus
void acBus::updateLocalCache()
{
    gridBus::updateLocalCache();
}

void acBus::computePowerAdjustments()
{
    // declaring an embedded function
    auto cid = getID();

    S.reset();

    for (auto& link : attachedLinks) {
        if ((link->isConnected()) && (!busController.hasVoltageAdjustments(link->getID()))) {
            S.linkQ += link->getReactivePower(cid);
        }
        if ((link->isConnected()) && (!busController.hasPowerAdjustments(link->getID()))) {
            S.linkP += link->getRealPower(cid);
        }
    }
    for (auto& load : attachedLoads) {
        if ((load->isConnected()) && (!busController.hasVoltageAdjustments(load->getID()))) {
            S.loadQ += load->getReactivePower(voltage);
        }
        if ((load->isConnected()) && (!busController.hasPowerAdjustments(load->getID()))) {
            S.loadP += load->getRealPower(voltage);
        }
    }
    for (auto& gen : attachedGens) {
        if ((gen->isConnected()) && (!busController.hasVoltageAdjustments(gen->getID()))) {
            S.genQ += gen->getReactivePower();
        }
        if ((gen->isConnected()) && (!busController.hasPowerAdjustments(gen->getID()))) {
            S.genP += gen->getRealPower();
        }
    }
}

double acBus::getMaxGenReal() const
{
    return busController.Pmax;
}

double acBus::getMaxGenReactive() const
{
    return busController.Qmax;
}

double acBus::getAdjustableCapacityUp(coreTime time) const
{
    return busController.getAdjustableCapacityUp(time);
}

double acBus::getAdjustableCapacityDown(coreTime time) const
{
    return busController.getAdjustableCapacityDown(time);
}

double acBus::getdPdf() const
{
    return 0;
}
/** @brief get the tie error (may be deprecated in the future)
 * @return the tie error
 **/
double acBus::getTieError() const
{
    return tieError;
}
/** @brief get the frequency response
 * @return the tie error
 **/
double acBus::getFreqResp() const
{
    return 0;
}
/** @brief get available regulation
 * @return the available regulation
 **/
double acBus::getRegTotal() const
{
    return 0;
}
/** @brief get the scheduled power
 * @return the scheduled power
 **/
double acBus::getSched() const
{
    return 0;
}

double acBus::get(const std::string& param, unit unitType) const
{
    double val = kNullVal;
    if (param == "vtarget") {
        val = convert(vTarget, puV, unitType, systemBasePower, localBaseVoltage);
    } else if (param == "atarget") {
        val = convert(aTarget, rad, unitType);
    } else if (param == "participation") {
        val = participation;
    } else if (param == "vmax") {
        val = Vmax;
    } else if (param == "vmin") {
        val = Vmin;
    } else if (param == "qmin") {
        val = busController.Qmin;
    } else if (param == "qmax") {
        val = busController.Qmax;
    } else if (param == "tw") {
        val = Tw;
    } else {
        return gridBus::get(param, unitType);
    }
    return val;
}

change_code acBus::rootCheck(const IOdata& inputs,
                             const stateData& sD,
                             const solverMode& sMode,
                             check_level_t level)
{
    double vcurr = getVoltage(sD, sMode);
    change_code ret = change_code::no_change;
    if (level == check_level_t::low_voltage_check) {
        if (!isConnected()) {
            return ret;
        }
        if (vcurr < 1e-8) {
            disconnect();
            ret = change_code::jacobian_change;
            LOG_DEBUG("Bus low voltage disconnect");
        }
        if (opFlags[prev_low_voltage_alert]) {
            if (sD.time <= lowVtime) {
                disconnect();
                opFlags.reset(prev_low_voltage_alert);
                ret = change_code::jacobian_change;
                LOG_DEBUG("Bus low voltage disconnect");
            } else {
                opFlags.reset(prev_low_voltage_alert);
            }
        }
        return ret;
    }
    if (level == check_level_t::complete_state_check) {
        if (vcurr < 1e-5) {
            LOG_NORMAL("bus disconnecting from low voltage");
            disconnect();
        } else if (isDAE(sMode)) {
            if (dynType == dynBusType::normal) {
                if (vcurr < 0.001) {
                    prevDynType = dynBusType::normal;
                    refAngle = static_cast<Area*>(getParent())
                                   ->getMasterAngle(emptyStateData, cLocalSolverMode);

                    dynType = dynBusType::fixAngle;
                    alert(this, JAC_COUNT_DECREASE);
                    ret = change_code::jacobian_change;
                }
            } else if (dynType == dynBusType::fixAngle) {
                if (prevDynType == dynBusType::normal) {
                    if (vcurr > 0.1) {
                        dynType = dynBusType::normal;
                        double nAngle = static_cast<Area*>(getParent())
                                            ->getMasterAngle(emptyStateData, cLocalSolverMode);
                        angle = angle + (nAngle - refAngle);
                        alert(this, JAC_COUNT_INCREASE);
                        ret = change_code::jacobian_change;
                    }
                }
            }
        }
    }
    // make sure we are not in a fault condition
    auto iret = gridBus::rootCheck(inputs, sD, sMode, level);
    if (iret > ret) {
        ret = iret;
    }

    return ret;
}

}  // namespace griddyn
