/*
 * Copyright (c) 2014-2026, Lawrence Livermore National Security
 * See the top-level NOTICE for additional details. All rights reserved.
 * SPDX-License-Identifier: BSD-3-Clause
 */

#include "AcBus.h"

#include "../Generator.h"
#include "../GridArea.h"
#include "../Link.h"
#include "../Load.h"
#include "../blocks/DerivativeBlock.h"
#include "../simulation/Contingency.h"
#include "core/CoreExceptions.h"
#include "core/CoreObjectTemplates.hpp"
#include "core/CoreOwningPtr.hpp"
#include "core/ObjectFactoryTemplates.hpp"
#include "gmlc/utilities/vectorOps.hpp"
// #include "matrixDataSparse.hpp"
#include "gmlc/utilities/stringOps.h"
#include <algorithm>
#include <cassert>
#include <cmath>
#include <iostream>
#include <memory>
#include <string>
#include <utility>
#include <vector>

namespace griddyn {
// factory is for the cloning function
static childTypeFactory<AcBus, GridBus> gbfac("bus", std::to_array<std::string_view>({"psystem"}));

using gmlc::utilities::convertToLowerCase;
using gmlc::utilities::solve2x2;
using units::convert;
using units::puMW;
using units::puV;
using units::rad;
using units::s;
using units::unit;

namespace {
    double checkVoltageDelta(double voltageDelta,
                             double currentVoltage,
                             double dropFraction = 0.75,
                             double maxRise = 0.2,
                             double riseCheck = 0.0)
    {
        if ((currentVoltage - voltageDelta) > riseCheck) {
            voltageDelta = std::max(voltageDelta, -maxRise);
        }
        voltageDelta = std::min(voltageDelta, dropFraction * currentVoltage);
        return voltageDelta;
    }

    double checkAngleDelta(double angleDelta, double /*currentAngle*/, double maxChange = kPI / 8.0)
    {
        if (std::abs(angleDelta) > maxChange) {
            angleDelta = std::copysign(maxChange, angleDelta);
        }
        return angleDelta;
    }
}  // namespace

AcBus::AcBus(const std::string& objName): GridBus(objName), busController(this)
{
    // default values
}

AcBus::AcBus(double vStart, double angleStart, const std::string& objName):
    GridBus(vStart, angleStart, objName), aTarget(angleStart), vTarget(vStart), busController(this)
{
    // default values
}

AcBus::~AcBus() = default;

CoreObject* AcBus::clone(CoreObject* obj) const
{
    auto* nobj = cloneBaseFactory<AcBus, GridBus>(this, obj, &gbfac);
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
            nobj->fblock =
                coreOwningPtr<GridBlock>(static_cast<GridBlock*>(fblock->clone(nullptr)));
            nobj->addSubObject(nobj->fblock.get());
        }
    }
    return nobj;
}

void AcBus::disable()
{
    CoreObject::disable();
    alert(this, STATE_COUNT_CHANGE);
    for (auto& link : attachedLinks) {
        link->disable();
    }
}

void AcBus::add(CoreObject* obj)
{
    auto* bus = dynamic_cast<AcBus*>(obj);
    if (bus != nullptr) {
        add(bus);
        return;
    }
    GridBus::add(obj);
}

void AcBus::add(AcBus* bus)
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

void AcBus::remove(CoreObject* obj)
{
    auto* bus = dynamic_cast<AcBus*>(obj);
    if (bus != nullptr) {
        remove(bus);
        return;
    }
    GridBus::remove(obj);
}

void AcBus::remove(AcBus* bus)
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

void AcBus::alert(CoreObject* obj, int code)
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
            [[fallthrough]];
        default:
            gridPrimary::alert(obj, code);
    }
}

// dynInitializeB states
void AcBus::pFlowObjectInitializeA(coreTime time0, std::uint32_t flags)
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
        if (gen->isConnected() && gen->isEnabled()) {
            ++activeSecondary;
        }
    }
    for (auto& load : attachedLoads) {
        load->pFlowInitializeA(time0, flags);
        if (load->isConnected() && load->isEnabled()) {
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
                    logging::warning(
                        this, "No load no gen, 1 active line ,bus is irrelevant disconnecting");
                    disconnect();
                }
            }
        }
    }
    // load up control objects
    if (type == busType::PV) {
        if (busController.vControlObjects.empty()) {
            logging::normal(this, "PV BUS with no controllers: converting to PQ");
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
                logging::normal(this,
                                "SLK BUS with No adjustable power elements, enabling auto_gen");
                opFlags.set(use_autogen);
            }
        }
    } else {
        if (CHECK_CONTROLFLAG(flags, auto_bus_disconnect)) {
            if ((attachedGens.empty()) && (attachedLoads.empty()) && (attachedLinks.size() == 1)) {
                if (!opFlags[use_autogen]) {
                    logging::warning(this, "No load no gen, 1 line ,bus is irrelevant disabling");
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

void AcBus::pFlowObjectInitializeB()
{
    GridBus::pFlowObjectInitializeB();

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
            logging::trace(this, "autogen P={}", busController.autogenPact);
        }
        if (busController.autogenQ < kHalfBigNum) {
            busController.autogenQact = -(S.loadQ + S.genQ - busController.autogenQ);
        }
    }
}

// TODO(PT):: transfer these functions to the busController
void AcBus::mergeBus(GridBus* mbus)
{
    auto* targetBus = dynamic_cast<AcBus*>(mbus);
    if (targetBus == nullptr) {
        return;
    }

    auto* sourceRoot = this;
    while (sourceRoot->opFlags[slave_bus]) {
        sourceRoot = dynamic_cast<AcBus*>(sourceRoot->busController.masterBus);
    }

    auto* targetRoot = targetBus;
    while (targetRoot->opFlags[slave_bus]) {
        targetRoot = dynamic_cast<AcBus*>(targetRoot->busController.masterBus);
    }

    if ((sourceRoot == nullptr) || (targetRoot == nullptr) || (sourceRoot == targetRoot)) {
        return;
    }

    auto* masterBus = sourceRoot;
    auto* slaveRoot = targetRoot;
    if (masterBus->getID() > slaveRoot->getID()) {
        std::swap(masterBus, slaveRoot);
    }

    slaveRoot->busController.masterBus = masterBus;
    slaveRoot->opFlags.set(slave_bus);
    masterBus->busController.slaveBusses.push_back(slaveRoot);

    for (auto* slaveBus : slaveRoot->busController.slaveBusses) {
        masterBus->busController.slaveBusses.push_back(slaveBus);
        slaveBus->busController.masterBus = masterBus;
    }
    slaveRoot->busController.slaveBusses.clear();
}

void AcBus::unmergeBus(GridBus* mbus)
{
    auto* targetBus = dynamic_cast<AcBus*>(mbus);
    if (targetBus == nullptr) {
        return;
    }
    auto* currentMaster = opFlags[slave_bus] ? dynamic_cast<AcBus*>(busController.masterBus) : this;
    auto* targetMaster = targetBus->checkFlag(slave_bus) ?
        dynamic_cast<AcBus*>(targetBus->busController.masterBus) :
        targetBus;
    if ((currentMaster == nullptr) || (targetMaster == nullptr) ||
        (currentMaster != targetMaster)) {
        return;
    }

    for (auto& slaveBus : currentMaster->busController.slaveBusses) {
        slaveBus->opFlags.reset(slave_bus);
    }
    currentMaster->checkMerge();
    targetBus->checkMerge();
}

void AcBus::checkMerge()
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

void AcBus::reset(reset_levels level)
{
    GridBus::reset(level);
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
                const double newAngle = static_cast<GridArea*>(getParent())
                                            ->getMasterAngle(emptyStateData, cLocalSolverMode);
                angle = angle + (newAngle - refAngle);
                alert(this, JAC_COUNT_CHANGE);
            } else if (voltage < 0.1) {
                voltage = 1.0;
                angle = getAverageAngle();
            }
            break;
        case reset_levels::low_voltage_dyn1:
            if (prevDynType != dynType) {
                dynType = prevDynType;
                const double newAngle = static_cast<GridArea*>(getParent())
                                            ->getMasterAngle(emptyStateData, cLocalSolverMode);
                angle = angle + (newAngle - refAngle);
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
                const double newAngle = static_cast<GridArea*>(getParent())
                                            ->getMasterAngle(emptyStateData, cLocalSolverMode);
                angle = angle + (newAngle - refAngle);
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

double AcBus::getAverageAngle() const
{
    if (!attachedLinks.empty()) {
        double averageAngle = 0.0;
        double rel = 0.0;
        for (const auto* lnk : attachedLinks) {
            averageAngle += lnk->getBusAngle(getID());
            rel += 1.0;
        }
        if (rel > 0.9) {
            return averageAngle / rel;
        }
    }
    return angle;
}

change_code
    AcBus::powerFlowAdjust(const IOdata& /*inputs*/, std::uint32_t flags, check_level_t level)
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
                                logging::trace(this, "changing from PQ to PV from low voltage");
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
                                logging::trace(this, "changing from PQ to PV from high voltage");
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
                    logging::trace(this, "changing from PV to PQ from Qmin");
                } else if (S.genQ > busController.Qmax) {
                    S.genQ = busController.Qmax;
                    for (auto& vco : busController.vControlObjects) {
                        vco->set("q", "max");
                    }
                    type = busType::PQ;
                    alert(this, JAC_COUNT_CHANGE);
                    out = change_code::jacobian_change;
                    logging::trace(this, "changing from PV to PQ from Qmax");
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
    for (auto& load : attachedLoads) {
        if (load->checkFlag(has_powerflow_adjustments)) {
            pout = load->powerFlowAdjust({voltage, angle}, flags, level);
            out = (std::max)(pout, out);
        }
    }
    return out;
}
/*function to check the current status for any limit violations*/
void AcBus::pFlowCheck(std::vector<Violation>& violations)
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
void AcBus::dynObjectInitializeA(coreTime time0, std::uint32_t flags)
{
    GridBus::dynObjectInitializeA(time0, flags);
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
        logging::trace(this, "computing bus frequency using frequency block");
        if (!fblock) {
            fblock = make_owningPtr<blocks::DerivativeBlock>(Tw);
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
void AcBus::dynObjectInitializeB(const IOdata& /*inputs*/,
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
    const auto initialOutputs = getOutputs(noInputs, emptyStateData, cLocalSolverMode);
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
    const IOdata parameterCache;
    // TODO(PT):: Do some thing with the fieldSet
    for (auto& gen : attachedGens) {
        gen->dynInitializeB(initialOutputs, parameterCache, fieldSet);
    }
    for (auto& load : attachedLoads) {
        load->dynInitializeB(initialOutputs, parameterCache, fieldSet);
    }
    if (opFlags[compute_frequency]) {
        IOdata iset(2);
        fblock->dynInitializeB({angle}, {0.0}, iset);
    }
}

void AcBus::generationAdjust(double adjustment)
{
    // adjust the real power flow
    int vci = 0;
    for (auto& pco :
         busController.pControlObjects) {  // don't worry about proxy objects for this purpose
        pco->set("adjustment", adjustment * busController.pcfrac[vci]);
        ++vci;
    }
}

void AcBus::timestep(coreTime time, const IOdata& /*inputs*/, const solverMode& sMode)
{
    const double timeDelta = time - prevTime;
    if (timeDelta < 1.0) {
        if (!m_dstate_dt.empty()) {
            voltage += m_dstate_dt[voltageInLocation] * timeDelta;
        }

        if (isDynamic(sMode)) {
            angle += (freq - 1.0) * systemBaseFrequency * timeDelta;
        }
    }
    const IOdata timestepInputs{voltage, angle, freq};
    for (auto& load : attachedLoads) {
        load->timestep(time, timestepInputs, sMode);
    }
    for (auto& gen : attachedGens) {
        gen->timestep(time, timestepInputs, sMode);
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

void AcBus::getParameterStrings(stringVec& pstr, paramStringType pstype) const
{
    getParamString<AcBus, GridBus>(this, pstr, locNumStrings, locStrStrings, flagStrings, pstype);
}

void AcBus::setFlag(std::string_view flag, bool val)
{
    if (flag == "compute_frequency") {
        if (!opFlags[dyn_initialized]) {
            opFlags.set(compute_frequency);
            if (!fblock) {
                fblock = make_owningPtr<blocks::DerivativeBlock>(Tw);
                fblock->setName("frequency_calc");
                fblock->set("k", 1.0 / systemBaseFrequency);
                fblock->addOwningReference();
                addSubObject(fblock.get());
                fblock->parentSetFlag(separate_processing, true, this);
            }
        }
    } else {
        GridBus::setFlag(flag, val);
    }
}

// set properties
void AcBus::set(std::string_view param, std::string_view val)
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
        GridBus::set(param, val);
    }
}

void AcBus::set(std::string_view param, double val, unit unitType)
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
        for (auto& load : attachedLoads) {
            load->set("basefreq", systemBaseFrequency);
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
        GridBus::set(param, val, unitType);
    }
}

void AcBus::setVoltageAngle(double Vnew, double Anew)
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

IOdata AcBus::getOutputs(const IOdata& /*inputs*/,
                         const stateData& stateDataValue,
                         const solverMode& sMode) const
{
    if (isLocal(sMode) || stateDataValue.empty()) {
        return {voltage, angle, freq};
    }
    return {getVoltage(stateDataValue, sMode),
            getAngle(stateDataValue, sMode),
            getFreq(stateDataValue, sMode)};
}

static const IOlocs kNullLocations{kNullLocation, kNullLocation, kNullLocation};

IOlocs AcBus::getOutputLocs(const solverMode& sMode) const
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

index_t AcBus::getOutputLoc(const solverMode& sMode, index_t num) const
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

double AcBus::getVoltage(const double state[], const solverMode& sMode) const
{
    if (isLocal(sMode)) {
        return voltage;
    }
    const auto voltageOffset = offsets.getVOffset(sMode);
    return (voltageOffset != kNullLocation) ? state[voltageOffset] : voltage;
}

double AcBus::getAngle(const double state[], const solverMode& sMode) const
{
    if (isLocal(sMode)) {
        return angle;
    }
    const auto angleOffset = offsets.getAOffset(sMode);
    return (angleOffset != kNullLocation) ? state[angleOffset] : angle;
}

double AcBus::getVoltage(const stateData& stateDataValue, const solverMode& sMode) const
{
    if (isLocal(sMode)) {
        return voltage;
    }
    if (hasAlgebraic(sMode)) {
        const auto voltageOffset = offsets.getVOffset(sMode);
        return (voltageOffset != kNullLocation) ? stateDataValue.state[voltageOffset] : voltage;
    }
    if (stateDataValue.algState != nullptr) {
        const auto voltageOffset =
            offsets.getVOffset(offsets.getSolverMode(sMode.pairedOffsetIndex));
        return (voltageOffset != kNullLocation) ? stateDataValue.algState[voltageOffset] : voltage;
    }
    if (stateDataValue.fullState != nullptr) {
        const auto voltageOffset =
            offsets.getVOffset(offsets.getSolverMode(sMode.pairedOffsetIndex));
        return (voltageOffset != kNullLocation) ? stateDataValue.fullState[voltageOffset] : voltage;
    }
    return voltage;
}

double AcBus::getAngle(const stateData& stateDataValue, const solverMode& sMode) const
{
    if (isLocal(sMode)) {
        return angle;
    }
    if (hasAlgebraic(sMode)) {
        const auto angleOffset = offsets.getAOffset(sMode);
        return (angleOffset != kNullLocation) ? stateDataValue.state[angleOffset] : angle;
    }
    if (stateDataValue.algState != nullptr) {
        const auto angleOffset = offsets.getAOffset(offsets.getSolverMode(sMode.pairedOffsetIndex));
        return (angleOffset != kNullLocation) ? stateDataValue.algState[angleOffset] : angle;
    }
    if (stateDataValue.fullState != nullptr) {
        const auto angleOffset = offsets.getAOffset(offsets.getSolverMode(sMode.pairedOffsetIndex));
        return (angleOffset != kNullLocation) ? stateDataValue.fullState[angleOffset] : angle;
    }
    return angle;
}

double AcBus::getFreq(const stateData& stateDataValue, const solverMode& sMode) const
{
    double frequencyValue = freq;
    if (opFlags[uses_bus_frequency]) {
        if (isDynamic(sMode)) {
            if (opFlags[compute_frequency]) {
                frequencyValue = fblock->getOutput(kNullVec, stateDataValue, sMode) + 1.0;
            } else if (keyGen != nullptr) {
                frequencyValue = keyGen->getFreq(stateDataValue, sMode);
            }
        }
    }
    return frequencyValue;
}

int AcBus::propogatePower(bool makeSlack)
{
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
        return 0;
    }

    int adjPSecondary = 0;
    int adjQSecondary = 0;
    for (auto& load : attachedLoads) {
        if (load->checkFlag(adjustable_P)) {
            ++adjPSecondary;
        } else {
            Pexp += load->getRealPower();
        }
        if (load->checkFlag(adjustable_Q)) {
            ++adjQSecondary;
        } else {
            Qexp += load->getReactivePower();
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
            for (auto& load : attachedLoads) {
                if (load->checkFlag(adjustable_P)) {
                    load->set("p", -Pexp);
                    ++found;
                }
                if (load->checkFlag(adjustable_Q)) {
                    load->set("q", -Qexp);
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

void AcBus::registerVoltageControl(GridComponent* comp)
{
    const bool update = (opFlags[pFlow_initialized]) && (type != busType::PQ);
    busController.addVoltageControlObject(comp, update);
}

void AcBus::removeVoltageControl(GridComponent* comp)
{
    busController.removeVoltageControlObject(comp->getID(), opFlags[pFlow_initialized]);
}

void AcBus::registerPowerControl(GridComponent* comp)
{
    const bool update = (opFlags[pFlow_initialized]) && (type != busType::PQ);
    busController.addPowerControlObject(comp, update);
}

void AcBus::removePowerControl(GridComponent* comp)
{
    busController.removePowerControlObject(comp->getID(), opFlags[pFlow_initialized]);
}

// guessState the solution
void AcBus::guessState(coreTime time, double state[], double dstate_dt[], const solverMode& sMode)
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
    GridComponent::guessState(time, state, dstate_dt, sMode);
}

// set algebraic and dynamic variables assume preset to differential
void AcBus::getVariableType(double sdata[], const solverMode& sMode)
{
    auto Voffset = offsets.getVOffset(sMode);
    if (Voffset != kNullLocation) {
        sdata[Voffset] = ALGEBRAIC_VARIABLE;
    }

    auto Aoffset = offsets.getAOffset(sMode);
    if (Aoffset != kNullLocation) {
        sdata[Aoffset] = ALGEBRAIC_VARIABLE;
    }
    GridComponent::getVariableType(sdata, sMode);
}

void AcBus::getTols(double tols[], const solverMode& sMode)
{
    auto Voffset = offsets.getVOffset(sMode);
    if (Voffset != kNullLocation) {
        tols[Voffset] = Vtol;
    }
    auto Aoffset = offsets.getAOffset(sMode);
    if (Aoffset != kNullLocation) {
        tols[Aoffset] = Atol;
    }

    GridComponent::getTols(tols, sMode);
}

// pass the solution
void AcBus::setState(coreTime time,
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
    GridBus::setState(time, state, dstate_dt, sMode);

    if (opFlags[compute_frequency]) {
        // fblock->setState(time, state, dstate_dt, sMode);
    } else if ((isDynamic(sMode)) && (keyGen != nullptr)) {
        freq = keyGen->getFreq(emptyStateData, sMode);
    }
    //    assert(voltage > 0.0);
}

// residual
void AcBus::residual(const IOdata& inputs,
                     const stateData& stateDataValue,
                     double resid[],
                     const solverMode& sMode)
{
    GridBus::residual(inputs, stateDataValue, resid, sMode);

    auto Aoffset = offsets.getAOffset(sMode);
    auto Voffset = offsets.getVOffset(sMode);

    // output
    if (hasAlgebraic(sMode)) {
        if (Voffset != kNullLocation) {
            if (useVoltage(sMode)) {
                assert(!std::isnan(S.linkQ));

                resid[Voffset] = S.sumQ();
                if (std::abs(resid[Voffset]) > 0.5) {
                    logging::trace(this,
                                   "sid={}::high voltage resid = {}",
                                   stateDataValue.seqID,
                                   resid[Voffset]);
                }
            } else {
                resid[Voffset] = stateDataValue.state[Voffset] - voltage;
            }
        }
        if (Aoffset != kNullLocation) {
            if (useAngle(sMode)) {
                assert(!std::isnan(S.linkP));
                resid[Aoffset] = S.sumP();
                if (std::abs(resid[Aoffset]) > 0.5) {
                    logging::trace(this,
                                   "sid={}::high angle resid = {}",
                                   stateDataValue.seqID,
                                   resid[Aoffset]);
                }
                // assert(std::abs(resid[Aoffset])<0.1);
            } else {
                resid[Aoffset] = stateDataValue.state[Aoffset] - angle;
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
        fblock->blockResidual(getAngle(stateDataValue, sMode), 0, stateDataValue, resid, sMode);
    }
}

void AcBus::derivative(const IOdata& inputs,
                       const stateData& stateDataValue,
                       double deriv[],
                       const solverMode& sMode)
{
    GridBus::derivative(inputs, stateDataValue, deriv, sMode);
    if (opFlags[compute_frequency]) {
        fblock->blockDerivative(getAngle(stateDataValue, sMode), 0.0, stateDataValue, deriv, sMode);
    }
}

// Jacobian
void AcBus::jacobianElements(const IOdata& inputs,
                             const stateData& stateDataValue,
                             matrixData<double>& matrixDataValue,
                             const IOlocs& inputLocs,
                             const solverMode& sMode)
{
    GridBus::jacobianElements(inputs, stateDataValue, matrixDataValue, inputLocs, sMode);

    // deal with the frequency block
    auto Aoffset = offsets.getAOffset(sMode);
    if ((fblock) && (isDynamic(sMode))) {
        fblock->blockJacobianElements(
            outputs[angleInLocation], 0.0, stateDataValue, matrixDataValue, Aoffset, sMode);
    }

    computeDerivatives(stateDataValue, sMode);
    if (isDifferentialOnly(sMode)) {
        return;
    }

    // compute the bus Jacobian elements themselves
    // printf("t=%f,id=%d, dpdt=%f, dpdv=%f, dqdt=%f, dqdv=%f\n", time, id, Ptii, Pvii, Qvii, Qtii);

    auto Voffset = offsets.getVOffset(sMode);

    if (Voffset != kNullLocation) {
        if (useVoltage(sMode)) {
            matrixDataValue.assignCheckCol(Voffset,
                                           Aoffset,
                                           partDeriv.at(QoutLocation, angleInLocation));
            matrixDataValue.assign(Voffset, Voffset, partDeriv.at(QoutLocation, voltageInLocation));
            if (opFlags[uses_bus_frequency]) {
                matrixDataValue.assignCheckCol(Voffset,
                                               outLocs[frequencyInLocation],
                                               partDeriv.at(QoutLocation, frequencyInLocation));
            }
        } else {
            matrixDataValue.assign(Voffset, Voffset, 1);
        }
    }
    if (Aoffset != kNullLocation) {
        if (useAngle(sMode)) {
            matrixDataValue.assign(Aoffset, Aoffset, partDeriv.at(PoutLocation, angleInLocation));
            matrixDataValue.assignCheckCol(Aoffset,
                                           Voffset,
                                           partDeriv.at(PoutLocation, voltageInLocation));
            if (opFlags[uses_bus_frequency]) {
                matrixDataValue.assignCheckCol(Aoffset,
                                               outLocs[frequencyInLocation],
                                               partDeriv.at(PoutLocation, frequencyInLocation));
            }
        } else {
            matrixDataValue.assign(Aoffset, Aoffset, 1);
        }
    }

    if (!isConnected()) {
        return;
    }
    of.setArray(matrixDataValue);

    of.setTranslation(PoutLocation, useAngle(sMode) ? outLocs[angleInLocation] : kNullLocation);
    of.setTranslation(QoutLocation, useVoltage(sMode) ? outLocs[voltageInLocation] : kNullLocation);
    if (!isExtended(sMode)) {
        for (auto& gen : attachedGens) {
            if (gen->jacSize(sMode) > 0) {
                gen->outputPartialDerivatives(outputs, stateDataValue, of, sMode);
            }
        }
        for (auto& load : attachedLoads) {
            if (load->jacSize(sMode) > 0) {
                load->outputPartialDerivatives(outputs, stateDataValue, of, sMode);
            }
        }
    } else {  // make the assignments for the extended state
        auto offset = offsets.getAlgOffset(sMode);
        of.assign(PoutLocation, offset, 1);
        of.assign(QoutLocation, offset + 1, 1);
    }
    auto gid = getID();
    for (auto& link : attachedLinks) {
        link->outputPartialDerivatives(gid, stateDataValue, of, sMode);
    }
}

void AcBus::voltageUpdate(const stateData& stateDataValue,
                          double update[],
                          const solverMode& sMode,
                          double alpha)
{
    if (!isConnected()) {
        return;
    }
    auto Voffset = offsets.getVOffset(sMode);
    const double voltageValue = getVoltage(stateDataValue, sMode);
    if (voltageValue < Vtol) {
        alert(this, VERY_LOW_VOLTAGE_ALERT);
        lowVtime = stateDataValue.time;
        return;
    }
    if (!useVoltage(sMode) || (Voffset == kNullLocation)) {
        update[Voffset] = voltageValue;
        return;
    }
    const bool useAngleState = useAngle(sMode);

    updateLocalCache(noInputs, stateDataValue, sMode);
    computeDerivatives(stateDataValue, sMode);

    const double realPowerDelta = S.sumP();
    const double reactivePowerDelta = useAngleState ? S.sumQ() : 0;

    const double realPowerByVoltage =
        useAngleState ? partDeriv.at(PoutLocation, voltageInLocation) : 1.0;
    const double reactivePowerByVoltage = partDeriv.at(QoutLocation, voltageInLocation);

    double voltageDelta =
        (reactivePowerDelta / reactivePowerByVoltage) + (realPowerDelta / realPowerByVoltage);
    if (!std::isfinite(voltageDelta)) {
        voltageDelta = reactivePowerDelta / reactivePowerByVoltage;
    }
    voltageDelta = checkVoltageDelta(voltageDelta, voltageValue, 0.75, 0.15, 1.05);

    assert(std::isfinite(voltageDelta));
    assert(voltageValue - voltageDelta > 0);
    update[Voffset] = voltageValue - (voltageDelta * alpha);
}

void AcBus::algebraicUpdate(const IOdata& inputs,
                            const stateData& stateDataValue,
                            double update[],
                            const solverMode& sMode,
                            double alpha)
{
    auto Voffset = offsets.getVOffset(sMode);
    auto Aoffset = offsets.getAOffset(sMode);
    const double voltageValue = getVoltage(stateDataValue, sMode);
    const double angleValue = getAngle(stateDataValue, sMode);
    const bool useVoltageState = useVoltage(sMode) && (Voffset != kNullLocation);
    const bool useAngleState =
        (!(opFlags[ignore_angle])) && useAngle(sMode) && (Aoffset != kNullLocation);

    if (useVoltageState && useAngleState) {
        updateLocalCache(inputs, stateDataValue, sMode);
        computeDerivatives(stateDataValue, sMode);

        const double realPowerDelta = S.sumP();
        const double reactivePowerDelta = S.sumQ();
        double voltageDelta;
        double angleDelta;
        const double realPowerByVoltage = partDeriv.at(PoutLocation, voltageInLocation);
        const double realPowerByAngle = partDeriv.at(PoutLocation, angleInLocation);
        const double reactivePowerByVoltage = partDeriv.at(QoutLocation, voltageInLocation);
        const double reactivePowerByAngle = partDeriv.at(QoutLocation, angleInLocation);
        const double determinant = solve2x2(realPowerByVoltage,
                                            realPowerByAngle,
                                            reactivePowerByVoltage,
                                            reactivePowerByAngle,
                                            realPowerDelta,
                                            reactivePowerDelta,
                                            voltageDelta,
                                            angleDelta);
        if (std::isnormal(determinant)) {
            voltageDelta = checkVoltageDelta(voltageDelta, voltageValue);
            angleDelta = checkAngleDelta(angleDelta, angleValue);
        } else if (realPowerByAngle != 0) {
            angleDelta = checkAngleDelta(realPowerDelta / realPowerByAngle, angleValue);
            voltageDelta = 0;
        } else {
            voltageDelta = 0;
            angleDelta = 0;
        }
        assert(std::isfinite(voltageDelta));
        assert(voltageValue - voltageDelta > 0);
        update[Voffset] = voltageValue - (voltageDelta * alpha);
        assert(std::isfinite(angleDelta));
        update[Aoffset] = angleValue - (angleDelta * alpha);
    } else if (useAngleState) {
        updateLocalCache(noInputs, stateDataValue, sMode);
        computeDerivatives(stateDataValue, sMode);

        const double realPowerDelta = S.sumP();
        const double realPowerByAngle = partDeriv.at(PoutLocation, angleInLocation);
        if (realPowerByAngle != 0) {
            const double angleDelta =
                checkAngleDelta(realPowerDelta / realPowerByAngle, angleValue);
            assert(std::isfinite(angleDelta));
            update[Aoffset] = angleValue - (angleDelta * alpha);
        } else {
            update[Aoffset] = angleValue;
        }

        if (Voffset != kNullLocation) {
            update[Voffset] = voltageValue;
        }
    } else if (useVoltageState) {
        updateLocalCache(noInputs, stateDataValue, sMode);
        computeDerivatives(stateDataValue, sMode);

        const double reactivePowerDelta = S.sumQ();
        const double reactivePowerByVoltage = partDeriv.at(QoutLocation, voltageInLocation);
        if (reactivePowerByVoltage != 0) {
            const double voltageDelta =
                checkVoltageDelta(reactivePowerDelta / reactivePowerByVoltage, voltageValue);
            assert(std::isfinite(voltageDelta));
            update[Voffset] = voltageValue - (voltageDelta * alpha);
        } else {
            update[Aoffset] = angleValue;
        }
        if (Aoffset != kNullLocation) {
            update[Aoffset] = angleValue;
        }
    } else {
        if (Aoffset != kNullLocation) {
            update[Aoffset] = angleValue;
        }
        if (Voffset != kNullLocation) {
            update[Voffset] = voltageValue;
        }
    }
    GridBus::algebraicUpdate(noInputs, stateDataValue, update, sMode, alpha);
}

void AcBus::localConverge(const solverMode& sMode, int mode, double tol)
{
    if (isDifferentialOnly(sMode)) {
        return;
    }
    double voltageValue{voltage};
    double angleValue{angle};
    double voltageDelta;
    double angleDelta;
    double realPowerByVoltage;
    double realPowerByAngle;
    double reactivePowerByVoltage;
    double reactivePowerByAngle;
    double err{kBigNum};
    int iteration{1};

    updateLocalCache();
    double realPowerDelta = S.sumP();
    double reactivePowerDelta = S.sumQ();
    if ((std::abs(realPowerDelta) < Atol) && (std::abs(reactivePowerDelta) < Vtol)) {
        return;
    }
    if ((S.loadP == 0) && (S.linkP == 0) && (S.loadQ == 0) && (S.linkQ == 0)) {
        if (!checkCapable()) {
            logging::warning(this, "Bus disconnected");
            disconnect();
        }
        return;
    }
    computeDerivatives(emptyStateData, sMode);
    if (mode == 0) {
        realPowerByVoltage = partDeriv.at(PoutLocation, voltageInLocation);
        realPowerByAngle = partDeriv.at(PoutLocation, angleInLocation);
        reactivePowerByVoltage = partDeriv.at(QoutLocation, voltageInLocation);
        reactivePowerByAngle = partDeriv.at(QoutLocation, angleInLocation);
        const double determinant = solve2x2(realPowerByVoltage,
                                            realPowerByAngle,
                                            reactivePowerByVoltage,
                                            reactivePowerByAngle,
                                            realPowerDelta,
                                            reactivePowerDelta,
                                            voltageDelta,
                                            angleDelta);
        if (std::isnormal(determinant)) {
            voltageDelta = checkVoltageDelta(voltageDelta, voltage);
            angleDelta = checkAngleDelta(angleDelta, angle);
        } else if (realPowerByAngle != 0) {
            angleDelta = checkAngleDelta(realPowerDelta / realPowerByAngle, angle);
            voltageDelta = 0;
        } else {
            voltageDelta = 0;
            angleDelta = 0;
        }
        voltage -= voltageDelta;
        angle -= angleDelta;
    } else if (mode == 1) {
        bool not_converged = true;
        while (not_converged) {
            if (iteration > 1) {
                voltageValue = voltage;
                angleValue = angle;

                updateLocalCache();
                computeDerivatives(emptyStateData, sMode);
                realPowerDelta = S.sumP();
                reactivePowerDelta = S.sumQ();
            }
            switch (getMode(sMode)) {
                case 0:
                    err = std::abs(realPowerDelta) + std::abs(reactivePowerDelta);
                    break;
                case 1:  // fixA
                    err = std::abs(reactivePowerDelta);
                    break;
                case 2:
                    err = std::abs(realPowerDelta);
                    break;
                default:
                    err = std::abs(realPowerDelta) + std::abs(reactivePowerDelta);
                    break;
            }
            if (err > tol) {
                realPowerByVoltage = partDeriv.at(PoutLocation, voltageInLocation);
                realPowerByAngle = partDeriv.at(PoutLocation, angleInLocation);
                reactivePowerByVoltage = partDeriv.at(QoutLocation, voltageInLocation);
                reactivePowerByAngle = partDeriv.at(QoutLocation, angleInLocation);
                const double determinant = gmlc::utilities::solve2x2(realPowerByVoltage,
                                                                     realPowerByAngle,
                                                                     reactivePowerByVoltage,
                                                                     reactivePowerByAngle,
                                                                     realPowerDelta,
                                                                     reactivePowerDelta,
                                                                     voltageDelta,
                                                                     angleDelta);
                if (std::isnormal(determinant)) {
                    voltageDelta = checkVoltageDelta(voltageDelta, voltageValue);
                    angleDelta = checkAngleDelta(angleDelta, angleValue);
                } else if (realPowerByAngle != 0) {
                    angleDelta = checkAngleDelta(realPowerDelta / realPowerByAngle, angleValue);
                    voltageDelta = 0;
                } else {
                    voltageDelta = 0;
                    angleDelta = 0;
                    not_converged = false;
                }
                voltage -= voltageDelta;
                angle += angleDelta;
                if (++iteration > 10) {
                    not_converged = false;
                    voltage = voltageValue;
                    angle = angleValue;
                }
            } else {
                not_converged = false;
            }
        }
    }
}

void AcBus::convergeHighErrorOnly(const stateData& stateDataValue,
                                  double state[],
                                  const solverMode& sMode,
                                  double& err,
                                  double tol)
{
    if (err <= 0.5) {
        return;
    }

    if (err > 2.0) {
        algebraicUpdate(noInputs, stateDataValue, state, sMode, 1.0);
        err = computeError(stateDataValue, sMode);
        int loopCount = 0;
        while ((err > tol) && (loopCount < 6)) {
            voltageUpdate(stateDataValue, state, sMode, 1.0);
            err = computeError(stateDataValue, sMode);
            ++loopCount;
        }
        return;
    }

    algebraicUpdate(noInputs, stateDataValue, state, sMode, 1.0);
    algebraicUpdate(noInputs, stateDataValue, state, sMode, 1.0);
}

bool AcBus::convergeStrongIteration(const stateData& stateDataValue,
                                    double state[],
                                    const solverMode& sMode,
                                    converge_mode& mode,
                                    double& err,
                                    double& voltageValue,
                                    double& angleValue,
                                    bool useVoltageState,
                                    bool useAngleState,
                                    index_t voltageOffset,
                                    index_t angleOffset,
                                    double currentModeVoltageLimit,
                                    double tol,
                                    int& iteration)
{
    while (err > tol) {
        voltageValue = useVoltageState ? state[voltageOffset] : voltage;
        angleValue = useAngleState ? state[angleOffset] : angle;
        if ((voltageValue < currentModeVoltageLimit) &&
            (mode != converge_mode::force_strong_iteration)) {
            mode = converge_mode::force_voltage_only;
            return true;
        }

        algebraicUpdate(noInputs, stateDataValue, state, sMode, 1.0);
        const double nextVoltageValue = useVoltageState ? state[voltageOffset] : voltage;
        const double nextAngleValue = useAngleState ? state[angleOffset] : angle;
        if ((std::abs(nextVoltageValue - voltageValue) < 1e-9) &&
            (std::abs(nextAngleValue - angleValue) < 1e-9)) {
            break;
        }
        err = computeError(stateDataValue, sMode);
        if (++iteration > 10) {
            break;
        }
    }
    return false;
}

bool AcBus::convergeVoltageOnly(const stateData& stateDataValue,
                                double state[],
                                const solverMode& sMode,
                                converge_mode& mode,
                                double& voltageValue,
                                double angleValue,
                                double frequencyValue,
                                bool useVoltageState,
                                index_t voltageOffset,
                                double tol,
                                bool& forceVoltageUp,
                                int& iteration)
{
    bool notConverged = voltageValue <= 0.6;
    double minimumVoltage = -kBigNum;
    double previousCorrectedError = 120000;
    int forceCount{0};

    while (notConverged) {
        if (iteration > 1) {
            voltageValue = useVoltageState ? state[voltageOffset] : voltage;
            if ((voltageValue > vTarget * 1.1) && (mode != converge_mode::force_voltage_only)) {
                mode = converge_mode::force_strong_iteration;
                return true;
            }
        }
        updateLocalCache(noInputs, stateDataValue, sMode);
        computeDerivatives(stateDataValue, sMode);
        const double realPowerDelta = S.sumP();
        const double reactivePowerDelta = S.sumQ();
        if ((voltageValue <= 0.0) && (iteration == 6)) {
            break;
        }
        const double correctedRealError = realPowerDelta / voltageValue;
        const double correctedReactiveError = reactivePowerDelta / voltageValue;

        if (iteration == 1) {
            previousCorrectedError = correctedReactiveError;
        }
        const double realPowerByVoltage = partDeriv.at(PoutLocation, voltageInLocation);
        const double reactivePowerByVoltage = partDeriv.at(QoutLocation, voltageInLocation);
        double voltageDelta = 0.0;
        if ((std::abs(correctedRealError) + std::abs(correctedReactiveError)) <= tol) {
            notConverged = false;
            continue;
        }

        if (std::abs(correctedReactiveError) > tol) {
            if (correctedReactiveError < 0) {
                if (forceVoltageUp || (iteration == 1)) {
                    voltageDelta = -0.1;
                    forceVoltageUp = true;
                    ++forceCount;
                    if (forceCount < 8) {
                        iteration = (iteration > 5) ? 5 : iteration;
                    }
                } else {
                    voltageDelta = (reactivePowerDelta / reactivePowerByVoltage) +
                        (realPowerDelta / realPowerByVoltage);
                    if ((!std::isfinite(voltageDelta)) ||
                        ((minimumVoltage > 0.35) &&
                         ((voltageValue - voltageDelta) < minimumVoltage))) {
                        voltageDelta = reactivePowerDelta / reactivePowerByVoltage;
                    }
                    voltageDelta = checkVoltageDelta(voltageDelta, voltageValue, 0.75, 0.15, 1.05);
                }
            } else {
                if ((previousCorrectedError < 0) && forceVoltageUp) {
                    minimumVoltage = voltageValue - 0.1;
                }
                forceVoltageUp = false;
                voltageDelta = (reactivePowerDelta / reactivePowerByVoltage) +
                    (realPowerDelta / realPowerByVoltage);
                if ((!std::isfinite(voltageDelta)) ||
                    ((minimumVoltage > 0.35) && ((voltageValue - voltageDelta) < minimumVoltage))) {
                    voltageDelta = reactivePowerDelta / reactivePowerByVoltage;
                }
                voltageDelta = checkVoltageDelta(voltageDelta, voltageValue, 0.75, 0.15, 1.05);
            }
        } else if (std::abs(correctedRealError) > tol) {
            voltageDelta = (reactivePowerDelta / reactivePowerByVoltage) +
                (realPowerDelta / realPowerByVoltage);
            if ((!std::isfinite(voltageDelta)) ||
                ((minimumVoltage > 0.35) && ((voltageValue - voltageDelta) < minimumVoltage))) {
                voltageDelta = reactivePowerDelta / reactivePowerByVoltage;
                notConverged = false;
            }
            voltageDelta = checkVoltageDelta(voltageDelta, voltageValue, 0.75, 0.15, 1.05);
        } else {
            notConverged = false;
        }

        if (useVoltageState) {
            assert(std::isfinite(voltageDelta));
            assert(voltageValue - voltageDelta > 0);
            state[voltageOffset] = voltageValue - voltageDelta;
        }

        if (isDynamic(sMode)) {
            for (auto& gen : attachedGens) {
                stateData generatorState;
                generatorState.state = state;
                gen->algebraicUpdate({voltageValue - voltageDelta, angleValue, frequencyValue},
                                     generatorState,
                                     state,
                                     sMode,
                                     1.0);
            }
        }
        if (++iteration > 10) {
            notConverged = false;
        }
    }

    return false;
}

void AcBus::converge(coreTime time,
                     double state[],
                     double dstate_dt[],
                     const solverMode& sMode,
                     converge_mode mode,
                     double tol)
{
    if (!isEnabled() || isDifferentialOnly(sMode) || opFlags[disconnected]) {
        return;
    }

    auto Voffset = offsets.getVOffset(sMode);
    auto Aoffset = offsets.getAOffset(sMode);

    const bool useVoltageState = useVoltage(sMode) && (Voffset != kNullLocation);
    const bool useAngleState = useAngle(sMode) && (Aoffset != kNullLocation);
    const stateData stateDataValue(time, state, dstate_dt);
    double voltageValue = useVoltageState ? state[Voffset] : voltage;
    double angleValue = useAngleState ? state[Aoffset] : angle;
    const double frequencyValue = getFreq(stateDataValue, sMode);
    if (voltageValue <= 0.0) {
        voltageValue = std::abs(voltageValue - 0.001);
        if (Voffset != kNullLocation) {
            state[Voffset] = voltageValue;
        }
    }
    double currentModeVlimit = 0.02 * vTarget;
    bool forceVoltageUp = false;
    int iteration = 1;
    if (isDAE(sMode)) {
        currentModeVlimit = (!attachedGens.empty()) ? 0.4 : 0.05;
        currentModeVlimit *= vTarget;
    }
    if ((voltageValue < currentModeVlimit) && (mode != converge_mode::force_voltage_only)) {
        mode = converge_mode::voltage_only;
    }

    double err = computeError(stateDataValue, sMode);
    if ((S.loadP == 0) && (S.linkP == 0) && (S.loadQ == 0) && (S.linkQ == 0)) {
        if (!checkCapable()) {
            logging::warning(this, "Bus disconnected");
            disconnect();
        }
        return;
    }
    bool restartConvergence = true;
    while (restartConvergence) {
        restartConvergence = false;
        switch (mode) {
            case converge_mode::high_error_only:
                convergeHighErrorOnly(stateDataValue, state, sMode, err, tol);
                break;
            case converge_mode::single_iteration:
            case converge_mode::block_iteration:
                algebraicUpdate(noInputs, stateDataValue, state, sMode, 1.0);
                break;
            case converge_mode::local_iteration:
            case converge_mode::strong_iteration:
            case converge_mode::force_strong_iteration:
                restartConvergence = convergeStrongIteration(stateDataValue,
                                                             state,
                                                             sMode,
                                                             mode,
                                                             err,
                                                             voltageValue,
                                                             angleValue,
                                                             useVoltageState,
                                                             useAngleState,
                                                             Voffset,
                                                             Aoffset,
                                                             currentModeVlimit,
                                                             tol,
                                                             iteration);
                break;
            case converge_mode::voltage_only:
            case converge_mode::force_voltage_only:
                restartConvergence = convergeVoltageOnly(stateDataValue,
                                                         state,
                                                         sMode,
                                                         mode,
                                                         voltageValue,
                                                         angleValue,
                                                         frequencyValue,
                                                         useVoltageState,
                                                         Voffset,
                                                         tol,
                                                         forceVoltageUp,
                                                         iteration);
                break;
            default:
                break;
        }
    }
}

double AcBus::computeError(const stateData& stateDataValue, const solverMode& sMode)
{
    updateLocalCache(noInputs, stateDataValue, sMode);
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
stringVec AcBus::localStateNames() const
{
    return stNames;
}

void AcBus::setOffsets(const solverOffsets& newOffsets, const solverMode& sMode)
{
    offsets.setOffsets(newOffsets, sMode);
    solverOffsets newLocalOffsets(newOffsets);
    newLocalOffsets.localIncrement(offsets.getOffsets(sMode));
    for (auto* load : attachedLoads) {
        load->setOffsets(newLocalOffsets, sMode);
        newLocalOffsets.increment(load->getOffsets(sMode));
    }
    for (auto* gen : attachedGens) {
        gen->setOffsets(newLocalOffsets, sMode);
        newLocalOffsets.increment(gen->getOffsets(sMode));
    }
    if (opFlags[slave_bus]) {
        auto& solverOffsetData = offsets.getOffsets(sMode);
        const auto& mboffsets = busController.masterBus->getOffsets(sMode);
        solverOffsetData.vOffset = mboffsets.vOffset;
        solverOffsetData.aOffset = mboffsets.aOffset;
    } else {
        if ((fblock) && (isDynamic(sMode))) {
            fblock->setOffsets(newLocalOffsets, sMode);
            newLocalOffsets.increment(fblock->getOffsets(sMode));
        }
    }
}

void AcBus::setOffset(index_t offset, const solverMode& sMode)
{
    for (auto* load : attachedLoads) {
        load->setOffset(offset, sMode);
        offset += load->stateSize(sMode);
    }
    for (auto* gen : attachedGens) {
        gen->setOffset(offset, sMode);
        offset += gen->stateSize(sMode);
    }
    if (opFlags[slave_bus]) {
        auto& solverOffsetData = offsets.getOffsets(sMode);
        const auto& mboffsets = busController.masterBus->getOffsets(sMode);
        solverOffsetData.vOffset = mboffsets.vOffset;
        solverOffsetData.aOffset = mboffsets.aOffset;
    } else {
        if ((fblock) && (isDynamic(sMode))) {
            fblock->setOffset(offset, sMode);
            offset += fblock->stateSize(sMode);
        }
        offsets.setOffset(offset, sMode);
    }
}

void AcBus::setRootOffset(index_t Roffset, const solverMode& sMode)
{
    offsets.setRootOffset(Roffset, sMode);
    auto& solverOffsetData = offsets.getOffsets(sMode);
    auto rootCount = solverOffsetData.local.algRoots + solverOffsetData.local.diffRoots;
    for (auto& gen : attachedGens) {
        gen->setRootOffset(Roffset + rootCount, sMode);
        rootCount += gen->rootSize(sMode);
    }
    for (auto& load : attachedLoads) {
        load->setRootOffset(Roffset + rootCount, sMode);
        rootCount += load->rootSize(sMode);
    }
    if (opFlags[compute_frequency]) {
        fblock->setRootOffset(Roffset + rootCount, sMode);
        // nR += fblock->rootSize (sMode);
    }
}

void AcBus::reconnect(GridBus* mapBus)
{
    if (!opFlags[disconnected]) {
        return;
    }

    GridBus::reconnect(mapBus);

    std::vector<GridBus*> pendingReconnects(busController.slaveBusses.begin(),
                                            busController.slaveBusses.end());
    while (!pendingReconnects.empty()) {
        auto* slaveBus = pendingReconnects.back();
        pendingReconnects.pop_back();
        if (!slaveBus->checkFlag(disconnected)) {
            continue;
        }
        slaveBus->GridBus::reconnect(this);
        if (auto* slaveAcBus = dynamic_cast<AcBus*>(slaveBus); slaveAcBus != nullptr) {
            pendingReconnects.insert(pendingReconnects.end(),
                                     slaveAcBus->busController.slaveBusses.begin(),
                                     slaveAcBus->busController.slaveBusses.end());
        }
    }
}
bool AcBus::useAngle(const solverMode& sMode) const
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

bool AcBus::useVoltage(const solverMode& sMode) const
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

count_t AcBus::getDependencyCount(const solverMode& sMode) const
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

stateSizes AcBus::LocalStateSizes(const solverMode& sMode) const
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

count_t AcBus::LocalJacobianCount(const solverMode& sMode) const
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

int AcBus::getMode(const solverMode& sMode) const
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

void AcBus::updateFlags(bool /*dynOnly*/)
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

void AcBus::computeDerivatives(const stateData& stateDataValue, const solverMode& sMode)
{
    if (!isConnected()) {
        return;
    }
    partDeriv.clear();

    for (auto& link : attachedLinks) {
        if (link->isEnabled()) {
            link->updateLocalCache(noInputs, stateDataValue, sMode);
            link->ioPartialDerivatives(getID(), stateDataValue, partDeriv, inLoc, sMode);
        }
    }
    if (!isExtended(sMode)) {
        for (auto& gen : attachedGens) {
            if (gen->isConnected()) {
                gen->ioPartialDerivatives(outputs, stateDataValue, partDeriv, inLoc, sMode);
            }
        }
        for (auto& load : attachedLoads) {
            if (load->isConnected()) {
                load->ioPartialDerivatives(outputs, stateDataValue, partDeriv, inLoc, sMode);
            }
        }
    }
}

// computed power at bus
void AcBus::updateLocalCache(const IOdata& inputs,
                             const stateData& stateDataValue,
                             const solverMode& sMode)
{
    if (!S.needsUpdate(stateDataValue)) {
        return;
    }

    if (!isConnected()) {
        return;
    }
    GridBus::updateLocalCache(inputs, stateDataValue, sMode);
    if (sMode.offsetIndex != lastSmode) {
        outLocs = getOutputLocs(sMode);
    }
}

// computed power at bus
void AcBus::updateLocalCache()
{
    GridBus::updateLocalCache();
}

void AcBus::computePowerAdjustments()
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

double AcBus::getMaxGenReal() const
{
    return busController.Pmax;
}

double AcBus::getMaxGenReactive() const
{
    return busController.Qmax;
}

double AcBus::getAdjustableCapacityUp(coreTime time) const
{
    return busController.getAdjustableCapacityUp(time);
}

double AcBus::getAdjustableCapacityDown(coreTime time) const
{
    return busController.getAdjustableCapacityDown(time);
}

double AcBus::getdPdf() const
{
    return 0;
}
/** @brief get the tie error (may be deprecated in the future)
 * @return the tie error
 **/
double AcBus::getTieError() const
{
    return tieError;
}
/** @brief get the frequency response
 * @return the tie error
 **/
double AcBus::getFreqResp() const
{
    return 0;
}
/** @brief get available regulation
 * @return the available regulation
 **/
double AcBus::getRegTotal() const
{
    return 0;
}
/** @brief get the scheduled power
 * @return the scheduled power
 **/
double AcBus::getSched() const
{
    return 0;
}

double AcBus::get(std::string_view param, unit unitType) const
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
        return GridBus::get(param, unitType);
    }
    return val;
}

change_code AcBus::rootCheck(const IOdata& inputs,
                             const stateData& stateDataValue,
                             const solverMode& sMode,
                             check_level_t level)
{
    const double currentVoltage = getVoltage(stateDataValue, sMode);
    change_code ret = change_code::no_change;
    if (level == check_level_t::low_voltage_check) {
        if (!isConnected()) {
            return ret;
        }
        if (currentVoltage < 1e-8) {
            disconnect();
            ret = change_code::jacobian_change;
            logging::debug(this, "Bus low voltage disconnect");
        }
        if (opFlags[prev_low_voltage_alert]) {
            if (stateDataValue.time <= lowVtime) {
                disconnect();
                opFlags.reset(prev_low_voltage_alert);
                ret = change_code::jacobian_change;
                logging::debug(this, "Bus low voltage disconnect");
            } else {
                opFlags.reset(prev_low_voltage_alert);
            }
        }
        return ret;
    }
    if (level == check_level_t::complete_state_check) {
        if (currentVoltage < 1e-5) {
            logging::normal(this, "bus disconnecting from low voltage");
            disconnect();
        } else if (isDAE(sMode)) {
            if (dynType == dynBusType::normal) {
                if (currentVoltage < 0.001) {
                    prevDynType = dynBusType::normal;
                    refAngle = static_cast<GridArea*>(getParent())
                                   ->getMasterAngle(emptyStateData, cLocalSolverMode);

                    dynType = dynBusType::fixAngle;
                    alert(this, JAC_COUNT_DECREASE);
                    ret = change_code::jacobian_change;
                }
            } else if (dynType == dynBusType::fixAngle) {
                if (prevDynType == dynBusType::normal) {
                    if (currentVoltage > 0.1) {
                        dynType = dynBusType::normal;
                        const double newAngle =
                            static_cast<GridArea*>(getParent())
                                ->getMasterAngle(emptyStateData, cLocalSolverMode);
                        angle = angle + (newAngle - refAngle);
                        alert(this, JAC_COUNT_INCREASE);
                        ret = change_code::jacobian_change;
                    }
                }
            }
        }
    }
    // make sure we are not in a fault condition
    const auto iret = GridBus::rootCheck(inputs, stateDataValue, sMode, level);
    ret = std::max(ret, iret);

    return ret;
}

}  // namespace griddyn
