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
// #include "MatrixDataSparse.hpp"
#include "gmlc/utilities/stringOps.h"
#include <algorithm>
#include <array>
#include <cassert>
#include <cmath>
#include <iostream>
#include <limits>
#include <memory>
#include <string>
#include <utility>
#include <vector>

namespace griddyn {
// factory is for the cloning function
static ChildTypeFactory<AcBus, GridBus> gBfac("bus", std::to_array<std::string_view>({"psystem"}));

using gmlc::utilities::convertToLowerCase;
using gmlc::utilities::solve2x2;
using units::convert;
using units::puMW;
using units::puV;
using units::rad;
using units::s;
using units::unit;

namespace {
    template<class T>
    void boundedIncrement(T& value)
    {
        if (value < std::numeric_limits<T>::max()) {
            ++value;
        }
    }

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
    auto* nobj = cloneBaseFactory<AcBus, GridBus>(this, obj, &gBfac);
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

    if (opFlags[COMPUTE_FREQUENCY]) {
        if (fblock) {
            nobj->fblock =
                CoreOwningPtr<GridBlock>(static_cast<GridBlock*>(fblock->clone(nullptr)));
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
    bus->opFlags.set(DIRECTCONNECT);
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
        if (bus->checkFlag(DIRECTCONNECT)) {
            bus->opFlags.reset(DIRECTCONNECT);
            bus->busController.directBus = nullptr;
        }
        unmergeBus(bus);
    }
}

void AcBus::alert(CoreObject* obj, int code)
{
    switch (code) {
        case VOLTAGE_CONTROL_UPDATE:
            if (opFlags[POWERFLOW_INITIALIZED]) {
                busController.updateVoltageControls();
            }
            break;
        case VERY_LOW_VOLTAGE_ALERT:
            // set an internal flag
            opFlags.set(PREV_LOW_VOLTAGE_ALERT);
            // forward the alert
            getParent()->alert(obj, code);
            break;
        case POWER_CONTROL_UDPATE:
            if (opFlags[POWERFLOW_INITIALIZED]) {
                busController.updatePowerControls();
            }
            break;
        case PV_CONTROL_UDPATE:
            if (opFlags[POWERFLOW_INITIALIZED]) {
                busController.updateVoltageControls();
                busController.updatePowerControls();
            }
            break;
        case OBJECT_NAME_CHANGE:
        case OBJECT_ID_CHANGE:
            break;
        case POTENTIAL_FAULT_CHANGE:
            if (opFlags[DISCONNECTED]) {
                reconnect();
            }
            [[fallthrough]];
        default:
            GridPrimary::alert(obj, code);
    }
}

// dynInitializeB states
void AcBus::pFlowObjectInitializeA(CoreTime time0, std::uint32_t flags)
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
    if (!(opFlags[USE_AUTOGEN])) {
        if (CHECK_CONTROLFLAG(flags, AUTO_BUS_DISCONNECT)) {
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
    if (type == BusType::PV) {
        if (busController.vControlObjects.empty()) {
            logging::normal(this, "PV BUS with no controllers: converting to PQ");
            type = BusType::PQ;
        }
    }

    if ((type == BusType::PV) || (type == BusType::SLK)) {
        voltage = vTarget;
    }
    if ((type == BusType::SLK) || (type == BusType::AFIX)) {
        angle = aTarget;
        bool padj = opFlags[USE_AUTOGEN];

        if (!busController.pControlObjects.empty()) {
            padj = true;  // We have a P control object
        }

        if (!padj) {  // if there is no generator listed on SLK or afix bus we need one for
                      // accounting purposes so add a
            // default one
            if (!CHECK_CONTROLFLAG(flags, NO_AUTO_AUTOGEN)) {
                logging::normal(this,
                                "SLK BUS with No adjustable power elements, enabling auto_gen");
                opFlags.set(USE_AUTOGEN);
            }
        }
    } else {
        if (CHECK_CONTROLFLAG(flags, AUTO_BUS_DISCONNECT)) {
            if ((attachedGens.empty()) && (attachedLoads.empty()) && (attachedLinks.size() == 1)) {
                if (!opFlags[USE_AUTOGEN]) {
                    logging::warning(this, "No load no gen, 1 line ,bus is irrelevant disabling");
                    disconnect();
                    return;
                }
            }
        }
    }

    // if there is only a single control then forward the bus max and mins to the control objects

    if ((type == BusType::PV) || (type == BusType::SLK)) {
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
    if ((type == BusType::AFIX) || (type == BusType::SLK)) {
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
    if ((type == BusType::PV) || (type == BusType::SLK)) {
        busController.updateVoltageControls();
    }
    if ((type == BusType::AFIX) || (type == BusType::SLK)) {
        busController.updatePowerControls();
    }

    if (CHECK_CONTROLFLAG(flags, LOW_VOLTAGE_CHECKING)) {
        opFlags.set(LOW_VOLTAGE_CHECK_FLAG);
    }
    updateFlags();
}

void AcBus::pFlowObjectInitializeB()
{
    GridBus::pFlowObjectInitializeB();

    m_dstate_dt.resize(3, 0);
    m_dstate_dt[ANGLE_IN_LOCATION] = systemBaseFrequency * (freq - 1.0);
    m_state = {voltage, angle, freq};
    outputs[VOLTAGE_IN_LOCATION] = voltage;
    outputs[ANGLE_IN_LOCATION] = angle;
    outputs[FREQUENCY_IN_LOCATION] = freq;
    lastSetTime = prevTime;
    computePowerAdjustments();
    if (opFlags[USE_AUTOGEN]) {
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
    while ((sourceRoot != nullptr) && sourceRoot->opFlags[SLAVE_BUS]) {
        auto* nextRoot = dynamic_cast<AcBus*>(sourceRoot->busController.masterBus);
        if (nextRoot == nullptr) {
            return;
        }
        sourceRoot = nextRoot;
    }

    auto* targetRoot = targetBus;
    while ((targetRoot != nullptr) && targetRoot->opFlags[SLAVE_BUS]) {
        auto* nextRoot = dynamic_cast<AcBus*>(targetRoot->busController.masterBus);
        if (nextRoot == nullptr) {
            return;
        }
        targetRoot = nextRoot;
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
    slaveRoot->opFlags.set(SLAVE_BUS);
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
    auto* currentMaster = opFlags[SLAVE_BUS] ? dynamic_cast<AcBus*>(busController.masterBus) : this;
    auto* targetMaster = targetBus->checkFlag(SLAVE_BUS) ?
        dynamic_cast<AcBus*>(targetBus->busController.masterBus) :
        targetBus;
    if ((currentMaster == nullptr) || (targetMaster == nullptr) ||
        (currentMaster != targetMaster)) {
        return;
    }

    for (auto& slaveBus : currentMaster->busController.slaveBusses) {
        slaveBus->opFlags.reset(SLAVE_BUS);
    }
    currentMaster->checkMerge();
    targetBus->checkMerge();
}

void AcBus::checkMerge()
{
    if (!isEnabled()) {
        return;
    }
    if (opFlags[DIRECTCONNECT]) {
        busController.directBus->mergeBus(this);
    }
    for (auto& lnk : attachedLinks) {
        lnk->checkMerge();
    }
}

// function to reset the bus type and voltage

void AcBus::reset(ResetLevels level)
{
    GridBus::reset(level);
    oCount = 0;
    if (prevType != type) {
        type = prevType;
        alert(this, JAC_COUNT_CHANGE);
    }
    switch (level) {
        case ResetLevels::MINIMAL:
            break;
        case ResetLevels::FULL:
        case ResetLevels::VOLTAGE_ANGLE:
            if ((type == BusType::PV) || (type == BusType::SLK)) {
                voltage = vTarget;
            } else {
                voltage = 1.0;
            }

            if ((type == BusType::SLK) || (type == BusType::AFIX)) {
                angle = aTarget;
            } else {
                angle = 0.0;
            }

            break;
        case ResetLevels::VOLTAGE:
            if ((type == BusType::PV) || (type == BusType::SLK)) {
                voltage = vTarget;
            } else {
                voltage = 1.0;
            }
            break;
        case ResetLevels::ANGLE:
            if ((type == BusType::SLK) || (type == BusType::AFIX)) {
                angle = aTarget;
            } else {
                angle = 0.0;
            }
            break;
        case ResetLevels::LOW_VOLTAGE_PFLOW:
            if (voltage < 0.6) {
                voltage = 0.9;
                angle = getAverageAngle();
            }
            break;
        case ResetLevels::LOW_VOLTAGE_DYN0:
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
        case ResetLevels::LOW_VOLTAGE_DYN1:
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
        case ResetLevels::LOW_VOLTAGE_DYN2:
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

ChangeCode AcBus::powerFlowAdjust(const IOdata& /*inputs*/, std::uint32_t flags, CheckLevel level)
{
    auto out = ChangeCode::NO_CHANGE;
    if (level == CheckLevel::LOW_VOLTAGE_CHECK) {
        if (!isConnected()) {
            return out;
        }
        if (voltage < 1e-8) {
            disconnect();
            out = ChangeCode::JACOBIAN_CHANGE;
        }
        if (opFlags[PREV_LOW_VOLTAGE_ALERT]) {
            disconnect();
            opFlags.reset(PREV_LOW_VOLTAGE_ALERT);
            out = ChangeCode::JACOBIAN_CHANGE;
        }
        return out;
    }

    if (!CHECK_CONTROLFLAG(flags, IGNORE_BUS_LIMITS)) {
        computePowerAdjustments();
        S.genQ = S.sumQ();
        S.genP = S.sumP();

        switch (type) {
            case BusType::SLK:

                if (S.genQ < busController.Qmin) {
                    S.genQ = busController.Qmin;
                    for (auto& vco : busController.vControlObjects) {
                        vco->set("q", "min");
                    }
                    type = BusType::AFIX;
                    alert(this, JAC_COUNT_CHANGE);
                    out = ChangeCode::JACOBIAN_CHANGE;
                } else if (S.genQ > busController.Qmax) {
                    S.genQ = busController.Qmax;
                    for (auto& vco : busController.vControlObjects) {
                        vco->set("q", "max");
                    }
                    type = BusType::AFIX;
                    alert(this, JAC_COUNT_CHANGE);
                    out = ChangeCode::JACOBIAN_CHANGE;
                }

                break;
            case BusType::PQ:
                if (prevType == BusType::PV) {
                    if (std::abs(S.genQ - busController.Qmin) < 0.00001) {
                        if (voltage < vTarget) {
                            if (oCount < 5) {
                                voltage = vTarget;
                                type = BusType::PV;
                                oCount++;
                                alert(this, JAC_COUNT_CHANGE);
                                out = ChangeCode::JACOBIAN_CHANGE;
                                logging::trace(this, "changing from PQ to PV from low voltage");
                            }
                        }
                    } else {
                        if (voltage > vTarget) {
                            if (oCount < 5) {
                                voltage = vTarget;
                                type = BusType::PV;
                                oCount++;
                                alert(this, JAC_COUNT_CHANGE);
                                out = ChangeCode::JACOBIAN_CHANGE;
                                logging::trace(this, "changing from PQ to PV from high voltage");
                            }
                        }
                    }
                } else if (prevType == BusType::SLK) {
                    if (std::abs(S.genQ - busController.Qmin) < 0.00001) {
                        if (voltage < vTarget) {
                            if (oCount < 5) {
                                voltage = vTarget;
                                type = BusType::SLK;
                                oCount++;
                                alert(this, JAC_COUNT_CHANGE);
                                out = ChangeCode::JACOBIAN_CHANGE;
                            }
                        }
                    } else {
                        if (voltage > vTarget) {
                            if (oCount < 5) {
                                voltage = vTarget;
                                type = BusType::SLK;
                                oCount++;
                                alert(this, JAC_COUNT_CHANGE);
                                out = ChangeCode::JACOBIAN_CHANGE;
                            }
                        }
                    }
                }

                break;
            case BusType::PV:
                if (S.genQ < busController.Qmin) {
                    S.genQ = busController.Qmin;
                    for (auto& vco : busController.vControlObjects) {
                        vco->set("q", "min");
                    }
                    type = BusType::PQ;
                    alert(this, JAC_COUNT_CHANGE);
                    out = ChangeCode::JACOBIAN_CHANGE;
                    logging::trace(this, "changing from PV to PQ from Qmin");
                } else if (S.genQ > busController.Qmax) {
                    S.genQ = busController.Qmax;
                    for (auto& vco : busController.vControlObjects) {
                        vco->set("q", "max");
                    }
                    type = BusType::PQ;
                    alert(this, JAC_COUNT_CHANGE);
                    out = ChangeCode::JACOBIAN_CHANGE;
                    logging::trace(this, "changing from PV to PQ from Qmax");
                }
                break;
            case BusType::AFIX:
                if (prevType == BusType::SLK) {
                    if (std::abs(S.genQ - busController.Qmin) < 0.00001) {
                        if (voltage < vTarget) {
                            if (oCount < 5) {
                                voltage = vTarget;
                                type = BusType::SLK;
                                oCount++;
                                alert(this, JAC_COUNT_CHANGE);
                                out = ChangeCode::JACOBIAN_CHANGE;
                            }
                        }
                    } else {
                        if (voltage > vTarget) {
                            if (oCount < 5) {
                                voltage = vTarget;
                                type = BusType::SLK;
                                oCount++;
                                alert(this, JAC_COUNT_CHANGE);
                                out = ChangeCode::JACOBIAN_CHANGE;
                            }
                        }
                    }
                }

                if (S.genP < busController.Pmin) {
                    S.genP = busController.Pmin;
                    for (auto& pco : busController.pControlObjects) {
                        pco->set("p", "min");
                    }
                    type = BusType::PQ;
                    alert(this, JAC_COUNT_CHANGE);
                    out = ChangeCode::JACOBIAN_CHANGE;
                    if (prevType == BusType::SLK) {
                        alert(this, SLACK_BUS_CHANGE);
                    }
                } else if (S.genP > busController.Pmax) {
                    S.genP = busController.Pmax;
                    type = BusType::PQ;
                    for (auto& pco : busController.pControlObjects) {
                        pco->set("p", "max");
                    }
                    alert(this, JAC_COUNT_CHANGE);
                    out = ChangeCode::JACOBIAN_CHANGE;
                    if (prevType == BusType::SLK) {
                        alert(this, SLACK_BUS_CHANGE);
                    }
                }
        }
        updateLocalCache();
    }
    ChangeCode pout;
    for (auto& gen : attachedGens) {
        if (gen->checkFlag(HAS_POWERFLOW_ADJUSTMENTS)) {
            pout = gen->powerFlowAdjust({voltage, angle}, flags, level);
            out = (std::max)(pout, out);
        }
    }
    for (auto& load : attachedLoads) {
        if (load->checkFlag(HAS_POWERFLOW_ADJUSTMENTS)) {
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
void AcBus::dynObjectInitializeA(CoreTime time0, std::uint32_t flags)
{
    GridBus::dynObjectInitializeA(time0, flags);
    // find a
    if (!(attachedGens.empty())) {
        double mxpower = 0;
        keyGen = nullptr;
        for (auto& gen : attachedGens) {
            if (gen->isConnected()) {
                if (gen->checkFlag(Generator::GeneratorFlags::INTERNAL_FREQUENCY_CALCULATION)) {
                    if (gen->getPmax() > mxpower) {
                        keyGen = gen;
                        mxpower = gen->getPmax();
                    }
                }
            }
        }
    }
    if (opFlags[USES_BUS_FREQUENCY]) {
        if (attachedGens.empty() || keyGen == nullptr) {
            opFlags.set(COMPUTE_FREQUENCY);
        }
    }
    if (opFlags[COMPUTE_FREQUENCY]) {
        opFlags.set(USES_BUS_FREQUENCY);
        logging::trace(this, "computing bus frequency using frequency block");
        if (!fblock) {
            fblock = makeOwningPtr<blocks::DerivativeBlock>(Tw);
            fblock->setName("frequency_calc");
            fblock->set("k", 1.0 / systemBaseFrequency);
            fblock->addOwningReference();
            addSubObject(fblock.get());
            fblock->parentSetFlag(SEPARATE_PROCESSING, true, this);
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
        if (desiredOutput[VOLTAGE_IN_LOCATION] > 0) {
            voltage = desiredOutput[VOLTAGE_IN_LOCATION];
        }
        if (desiredOutput[ANGLE_IN_LOCATION] > -kHalfBigNum) {
            angle = desiredOutput[ANGLE_IN_LOCATION];
        }
        if (std::abs(desiredOutput[FREQUENCY_IN_LOCATION] - 1.0) < 0.5) {
            freq = desiredOutput[FREQUENCY_IN_LOCATION];
        }
    }
    updateLocalCache();
    lastSetTime = prevTime;
    m_state[VOLTAGE_IN_LOCATION] = voltage;
    m_state[ANGLE_IN_LOCATION] = angle;
    m_state[FREQUENCY_IN_LOCATION] = freq;
    if (opFlags[USE_AUTOGEN]) {
        if ((busController.autogenQ > kHalfBigNum) && (attachedGens.empty())) {
            busController.autogenQact = -(S.linkQ + S.loadQ);
        }
        S.genP = busController.autogenPact;
        S.genQ = busController.autogenQact;
    }
    // first get the state size for the internal state ordering
    const auto initialOutputs = getOutputs(noInputs, emptyStateData, cLocalSolverMode);
    double qgap;
    double pgap;
    int vci = 0;
    int poi = 0;
    auto cid = getID();
    switch (type) {
        case BusType::PQ:
            break;
        case BusType::PV:
            computePowerAdjustments();
            qgap = -S.sumQ();
            for (auto& vco : busController.vControlObjects) {
                if (vco->checkFlag(LOCAL_VOLTAGE_CONTROL)) {
                    vco->set("q", -qgap * busController.vcfrac[vci]);
                } else {
                    busController.proxyVControlObject[poi]->fixPower(
                        busController.proxyVControlObject[poi]->getRealPower(cid),
                        qgap * busController.vcfrac[vci],
                        cid,
                        cid);
                    ++poi;
                }
                ++vci;
            }
            break;
        case BusType::SLK:

            computePowerAdjustments();
            qgap = -(S.sumQ());
            pgap = -(S.sumP());

            if (opFlags[IDENTICAL_PQ_CONTROL_OBJECTS])  // adjust the power levels together
            {
                for (auto& vco : busController.vControlObjects) {
                    if (vco->checkFlag(LOCAL_VOLTAGE_CONTROL)) {
                        if (busController.vcfrac[vci] > 0.0) {
                            vco->set("q", -qgap * busController.vcfrac[vci]);
                        }
                        if (busController.pcfrac[vci] > 0.0) {
                            vco->set("p", -pgap * busController.pcfrac[vci]);
                        }
                    } else {  // use both together on fixpower function
                        busController.proxyVControlObject[poi]->fixPower(
                            -pgap * busController.pcfrac[vci],
                            -qgap * busController.vcfrac[vci],
                            cid,
                            cid);
                        ++poi;
                    }
                    ++vci;
                }
            } else {  // adjust the power levels separately
                // adjust the real power flow
                for (auto& pco : busController.pControlObjects) {
                    if (pco->checkFlag(LOCAL_VOLTAGE_CONTROL)) {
                        pco->set("p", -pgap * busController.pcfrac[vci]);
                    } else {
                        busController.proxyVControlObject[poi]->fixPower(
                            -pgap * busController.pcfrac[vci],
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
                    if (vco->checkFlag(LOCAL_VOLTAGE_CONTROL)) {
                        vco->set("q", -qgap * busController.vcfrac[vci]);
                    } else {
                        busController.proxyVControlObject[poi]->fixPower(
                            busController.proxyVControlObject[poi]->getRealPower(cid),
                            -qgap * busController.vcfrac[vci],
                            cid,
                            cid);
                        ++poi;
                    }
                    ++vci;
                }
            }
            break;
        case BusType::AFIX:
            pgap = -(S.sumP());
            // adjust the real power flow
            for (auto& pco : busController.pControlObjects) {
                if (pco->checkFlag(LOCAL_VOLTAGE_CONTROL)) {
                    pco->set("p", -pgap * busController.pcfrac[vci]);
                } else {
                    busController.proxyVControlObject[poi]->fixPower(
                        -pgap * busController.pcfrac[vci],
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
    if (opFlags[COMPUTE_FREQUENCY]) {
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

void AcBus::timestep(CoreTime time, const IOdata& /*inputs*/, const SolverMode& sMode)
{
    const double timeDelta = time - prevTime;
    if (timeDelta < 1.0) {
        if (!m_dstate_dt.empty()) {
            voltage += m_dstate_dt[VOLTAGE_IN_LOCATION] * timeDelta;
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
    if (opFlags[COMPUTE_FREQUENCY]) {
        fblock->step(time, angle);
    }
    prevTime = time;
}

static constexpr auto locNumStrings =
    std::array<std::string_view, 4>{"vtarget", "atarget", "p", "q"};
static constexpr auto locStrStrings = std::array<std::string_view, 2>{"pflowtype", "dyntype"};

static constexpr auto flagStrings = std::array<std::string_view, 1>{"use_frequency"};

void AcBus::getParameterStrings(stringVec& pstr, ParamStringType pstype) const
{
    getParamString<AcBus, GridBus>(this, pstr, locNumStrings, locStrStrings, flagStrings, pstype);
}

void AcBus::setFlag(std::string_view flag, bool val)
{
    if (flag == "compute_frequency") {
        if (!opFlags[DYN_INITIALIZED]) {
            opFlags.set(COMPUTE_FREQUENCY);
            if (!fblock) {
                fblock = makeOwningPtr<blocks::DerivativeBlock>(Tw);
                fblock->setName("frequency_calc");
                fblock->set("k", 1.0 / systemBaseFrequency);
                fblock->addOwningReference();
                addSubObject(fblock.get());
                fblock->parentSetFlag(SEPARATE_PROCESSING, true, this);
            }
        }
    } else {
        GridBus::setFlag(flag, val);
    }
}

// set properties
void AcBus::set(std::string_view param, std::string_view val)
{
    auto valLowerCase = convertToLowerCase(val);
    if ((param == "type") || (param == "bustype") || (param == "pflowtype")) {
        if ((valLowerCase == "slk") || (valLowerCase == "swing") || (valLowerCase == "slack")) {
            type = BusType::SLK;
            prevType = BusType::SLK;
        } else if (valLowerCase == "pv") {
            type = BusType::PV;
            prevType = BusType::PV;
        } else if (valLowerCase == "pq") {
            type = BusType::PQ;
            prevType = BusType::PQ;
        } else if ((valLowerCase == "dynslk") || (valLowerCase == "inf") ||
                   (valLowerCase == "infinite")) {
            type = BusType::SLK;
            prevType = BusType::SLK;
            dynType = DynBusType::DYN_SLK;
        } else if ((valLowerCase == "fixedangle") || (valLowerCase == "fixangle") ||
                   (valLowerCase == "ref")) {
            dynType = DynBusType::FIX_ANGLE;
        } else if ((valLowerCase == "fixedvoltage") || (valLowerCase == "fixvoltage") ||
                   (valLowerCase == "vfix")) {
            dynType = DynBusType::FIX_VOLTAGE;
        } else if (valLowerCase == "afix") {
            type = BusType::AFIX;
            prevType = BusType::AFIX;
        } else if (valLowerCase == "normal") {
            dynType = DynBusType::NORMAL;
        } else {
            throw(InvalidParameterValue(val));
        }
    } else if (param == "dyntype") {
        if ((valLowerCase == "dynslk") || (valLowerCase == "inf") || (valLowerCase == "slk")) {
            dynType = DynBusType::DYN_SLK;
            type = BusType::SLK;
        } else if ((valLowerCase == "fixedvoltage") || (valLowerCase == "fixvoltage") ||
                   (valLowerCase == "vfix")) {
            dynType = DynBusType::FIX_VOLTAGE;
        } else if ((valLowerCase == "fixedangle") || (valLowerCase == "fixangle") ||
                   (valLowerCase == "ref") || (valLowerCase == "afix")) {
            dynType = DynBusType::FIX_ANGLE;
        } else if ((valLowerCase == "normal") || (valLowerCase == "pq")) {
            dynType = DynBusType::NORMAL;
        } else {
            throw(InvalidParameterValue(val));
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
        if ((type == BusType::PV) || (type == BusType::SLK)) {
            vTarget = voltage;
        }
    } else if ((param == "angle") || (param == "ang") || (param == "a") || (param == "theta") ||
               (param == "angle0")) {
        angle = convert(val, unitType, rad);
        if ((type == BusType::SLK) || (type == BusType::AFIX)) {
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
        if (opFlags[COMPUTE_FREQUENCY]) {
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
        if (opFlags[POWERFLOW_INITIALIZED]) {
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
        if (opFlags[POWERFLOW_INITIALIZED]) {
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
        if (opFlags[POWERFLOW_INITIALIZED]) {
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
        if (opFlags[POWERFLOW_INITIALIZED]) {
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
        opFlags.set(USE_AUTOGEN);
    } else if (param == "autogenq") {
        busController.autogenQ = convert(val, unitType, puMW, systemBasePower, localBaseVoltage);
        opFlags.set(USE_AUTOGEN);
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
        if (opFlags[COMPUTE_FREQUENCY]) {
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

void AcBus::setVoltageAngle(double vnew, double anew)
{
    voltage = vnew;
    angle = anew;
    switch (type) {
        case BusType::PQ:
            break;
        case BusType::PV:
            vTarget = voltage;
            break;
        case BusType::SLK:
            vTarget = voltage;
            aTarget = angle;
            break;
        case BusType::AFIX:
            aTarget = angle;
            break;
        default:
            break;
    }
}

static const IOdata K_NULL_VEC;

IOdata AcBus::getOutputs(const IOdata& /*inputs*/,
                         const StateData& stateDataValue,
                         const SolverMode& sMode) const
{
    if (isLocal(sMode) || stateDataValue.empty()) {
        return {voltage, angle, freq};
    }
    return {getVoltage(stateDataValue, sMode),
            getAngle(stateDataValue, sMode),
            getFreq(stateDataValue, sMode)};
}

static const IOlocs K_NULL_LOCATIONS{kNullLocation, kNullLocation, kNullLocation};

IOlocs AcBus::getOutputLocs(const SolverMode& sMode) const
{
    if ((!hasAlgebraic(sMode)) || (!isConnected())) {
        return K_NULL_LOCATIONS;
    }
    if (sMode.offsetIndex == lastSmode) {
        return outLocs;
    }

    IOlocs newOutLocs(3);
    // auto Aoffset = useAngle(sMode) ? offsets.getAOffset(sMode) : kNullLocation;
    // auto Voffset = useVoltage(sMode) ? offsets.getVOffset(sMode) : kNullLocation;
    auto aoffset = offsets.getAOffset(sMode);
    auto voffset = offsets.getVOffset(sMode);

    newOutLocs[VOLTAGE_IN_LOCATION] = voffset;
    newOutLocs[ANGLE_IN_LOCATION] = aoffset;
    if (opFlags[COMPUTE_FREQUENCY]) {
        index_t toff = kNullLocation;
        if (opFlags[COMPUTE_FREQUENCY]) {
            toff = fblock->getOutputLoc(sMode);
        } else if (keyGen != nullptr) {
            keyGen->getFreq(emptyStateData, sMode, &toff);
        }

        newOutLocs[FREQUENCY_IN_LOCATION] = toff;
    } else {
        newOutLocs[FREQUENCY_IN_LOCATION] = kNullLocation;
    }
    return newOutLocs;
}

index_t AcBus::getOutputLoc(const SolverMode& sMode, index_t num) const
{
    if (sMode.offsetIndex == lastSmode) {
        if (num < 3) {
            return outLocs[num];
        }
        return kNullLocation;
    }

    switch (num) {
        case VOLTAGE_IN_LOCATION:
            // return useVoltage(sMode) ? offsets.getVOffset(sMode) : kNullLocation;
            return offsets.getVOffset(sMode);
        case ANGLE_IN_LOCATION:
            // return useAngle(sMode) ? offsets.getAOffset(sMode) : kNullLocation;
            return offsets.getAOffset(sMode);
        case FREQUENCY_IN_LOCATION: {
            if (opFlags[COMPUTE_FREQUENCY]) {
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

double AcBus::getVoltage(const double state[], const SolverMode& sMode) const
{
    if (isLocal(sMode)) {
        return voltage;
    }
    const auto voltageOffset = offsets.getVOffset(sMode);
    return (voltageOffset != kNullLocation) ? state[voltageOffset] : voltage;
}

double AcBus::getAngle(const double state[], const SolverMode& sMode) const
{
    if (isLocal(sMode)) {
        return angle;
    }
    const auto angleOffset = offsets.getAOffset(sMode);
    return (angleOffset != kNullLocation) ? state[angleOffset] : angle;
}

double AcBus::getVoltage(const StateData& stateDataValue, const SolverMode& sMode) const
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

double AcBus::getAngle(const StateData& stateDataValue, const SolverMode& sMode) const
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

double AcBus::getFreq(const StateData& stateDataValue, const SolverMode& sMode) const
{
    double frequencyValue = freq;
    if (opFlags[USES_BUS_FREQUENCY]) {
        if (isDynamic(sMode)) {
            if (opFlags[COMPUTE_FREQUENCY]) {
                frequencyValue = fblock->getOutput(K_NULL_VEC, stateDataValue, sMode) + 1.0;
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
        type = BusType::SLK;
    }
    int unfixedLines = 0;
    Link* unfixedLine = nullptr;
    double pexp = 0;
    double qexp = 0;
    for (auto& lnk : attachedLinks) {
        if (lnk->checkFlag(Link::FIXED_TARGET_POWER)) {
            pexp += lnk->getRealPower(getID());
            qexp += lnk->getReactivePower(getID());
            continue;
        }
        ++unfixedLines;
        unfixedLine = lnk;
    }
    if (unfixedLines > 1) {
        return 0;
    }

    int adjPSecondary = 0;
    int adjQSecondary = 0;
    for (auto& load : attachedLoads) {
        if (load->checkFlag(ADJUSTABLE_P)) {
            ++adjPSecondary;
        } else {
            pexp += load->getRealPower();
        }
        if (load->checkFlag(ADJUSTABLE_Q)) {
            ++adjQSecondary;
        } else {
            qexp += load->getReactivePower();
        }
    }
    for (auto& gen : attachedGens) {
        if (gen->checkFlag(ADJUSTABLE_P)) {
            ++adjPSecondary;
        } else {
            pexp -= gen->getRealPower();
        }
        if (gen->checkFlag(ADJUSTABLE_Q)) {
            ++adjQSecondary;
        } else {
            qexp -= gen->getReactivePower();
        }
    }
    if (unfixedLines == 1) {
        if ((adjPSecondary == 0) && (adjQSecondary == 0)) {
            /*ret = */ unfixedLine->fixPower(-pexp, -qexp, getID(), getID());
        }
    } else {  // no lines so adjust the generators and load
        if ((adjPSecondary == 1) && (adjQSecondary == 1)) {
            int found = 0;
            for (auto& gen : attachedGens) {
                if (gen->checkFlag(ADJUSTABLE_P)) {
                    gen->set("p", pexp);
                    ++found;
                }
                if (gen->checkFlag(ADJUSTABLE_Q)) {
                    gen->set("q", qexp);
                    ++found;
                }
                if (found == 2) {
                    return 1;
                }
            }
            for (auto& load : attachedLoads) {
                if (load->checkFlag(ADJUSTABLE_P)) {
                    load->set("p", -pexp);
                    ++found;
                }
                if (load->checkFlag(ADJUSTABLE_Q)) {
                    load->set("q", -qexp);
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
    const bool update = (opFlags[POWERFLOW_INITIALIZED]) && (type != BusType::PQ);
    busController.addVoltageControlObject(comp, update);
}

void AcBus::removeVoltageControl(GridComponent* comp)
{
    busController.removeVoltageControlObject(comp->getID(), opFlags[POWERFLOW_INITIALIZED]);
}

void AcBus::registerPowerControl(GridComponent* comp)
{
    const bool update = (opFlags[POWERFLOW_INITIALIZED]) && (type != BusType::PQ);
    busController.addPowerControlObject(comp, update);
}

void AcBus::removePowerControl(GridComponent* comp)
{
    busController.removePowerControlObject(comp->getID(), opFlags[POWERFLOW_INITIALIZED]);
}

// guessState the solution
void AcBus::guessState(CoreTime time, double state[], double dstateDt[], const SolverMode& sMode)
{
    auto voffset = offsets.getVOffset(sMode);
    auto aoffset = offsets.getAOffset(sMode);

    if (!opFlags[SLAVE_BUS]) {
        if (voffset != kNullLocation) {
            state[voffset] = voltage;

            if (hasDifferential(sMode)) {
                dstateDt[voffset] = 0.0;
            }
        }
        if (aoffset != kNullLocation) {
            state[aoffset] = angle;
            if (hasDifferential(sMode)) {
                dstateDt[aoffset] = 0.0;
            }
        }
    }
    GridComponent::guessState(time, state, dstateDt, sMode);
}

// set algebraic and dynamic variables assume preset to differential
void AcBus::getVariableType(double sdata[], const SolverMode& sMode)
{
    auto voffset = offsets.getVOffset(sMode);
    if (voffset != kNullLocation) {
        sdata[voffset] = ALGEBRAIC_VARIABLE;
    }

    auto aoffset = offsets.getAOffset(sMode);
    if (aoffset != kNullLocation) {
        sdata[aoffset] = ALGEBRAIC_VARIABLE;
    }
    GridComponent::getVariableType(sdata, sMode);
}

void AcBus::getTols(double tols[], const SolverMode& sMode)
{
    auto voffset = offsets.getVOffset(sMode);
    if (voffset != kNullLocation) {
        tols[voffset] = Vtol;
    }
    auto aoffset = offsets.getAOffset(sMode);
    if (aoffset != kNullLocation) {
        tols[aoffset] = Atol;
    }

    GridComponent::getTols(tols, sMode);
}

// pass the solution
void AcBus::setState(CoreTime time,
                     const double state[],
                     const double dstateDt[],
                     const SolverMode& sMode)
{
    auto aoffset = offsets.getAOffset(sMode);
    auto voffset = offsets.getVOffset(sMode);

    if (isDAE(sMode)) {
        if (voffset != kNullLocation) {
            voltage = state[voffset];
            m_dstate_dt[VOLTAGE_IN_LOCATION] = dstateDt[voffset];
        }
        if (aoffset != kNullLocation) {
            angle = state[aoffset];
            m_dstate_dt[ANGLE_IN_LOCATION] = dstateDt[aoffset];
        }
    } else if (hasAlgebraic(sMode)) {
        if (voffset != kNullLocation) {
            if (time > prevTime) {
                m_dstate_dt[VOLTAGE_IN_LOCATION] =
                    (state[voffset] - m_state[VOLTAGE_IN_LOCATION]) / (time - lastSetTime);
            }
            voltage = state[voffset];
        }
        if (aoffset != kNullLocation) {
            if (time > prevTime) {
                m_dstate_dt[ANGLE_IN_LOCATION] =
                    (state[aoffset] - -m_state[ANGLE_IN_LOCATION]) / (time - lastSetTime);
            }
            angle = state[aoffset];
        }
        lastSetTime = time;
    }
    GridBus::setState(time, state, dstateDt, sMode);

    if (opFlags[COMPUTE_FREQUENCY]) {
        // fblock->setState(time, state, dstate_dt, sMode);
    } else if ((isDynamic(sMode)) && (keyGen != nullptr)) {
        freq = keyGen->getFreq(emptyStateData, sMode);
    }
    //    assert(voltage > 0.0);
}

// residual
void AcBus::residual(const IOdata& inputs,
                     const StateData& stateDataValue,
                     double resid[],
                     const SolverMode& sMode)
{
    GridBus::residual(inputs, stateDataValue, resid, sMode);

    auto aoffset = offsets.getAOffset(sMode);
    auto voffset = offsets.getVOffset(sMode);

    // output
    if (hasAlgebraic(sMode)) {
        if (voffset != kNullLocation) {
            if (useVoltage(sMode)) {
                assert(!std::isnan(S.linkQ));

                resid[voffset] = S.sumQ();
                if (std::abs(resid[voffset]) > 0.5) {
                    logging::trace(this,
                                   "sid={}::high voltage resid = {}",
                                   stateDataValue.seqID,
                                   resid[voffset]);
                }
            } else {
                resid[voffset] = stateDataValue.state[voffset] - voltage;
            }
        }
        if (aoffset != kNullLocation) {
            if (useAngle(sMode)) {
                assert(!std::isnan(S.linkP));
                resid[aoffset] = S.sumP();
                if (std::abs(resid[aoffset]) > 0.5) {
                    logging::trace(this,
                                   "sid={}::high angle resid = {}",
                                   stateDataValue.seqID,
                                   resid[aoffset]);
                }
                // assert(std::abs(resid[aoffset])<0.1);
            } else {
                resid[aoffset] = stateDataValue.state[aoffset] - angle;
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
                       const StateData& stateDataValue,
                       double deriv[],
                       const SolverMode& sMode)
{
    GridBus::derivative(inputs, stateDataValue, deriv, sMode);
    if (opFlags[COMPUTE_FREQUENCY]) {
        fblock->blockDerivative(getAngle(stateDataValue, sMode), 0.0, stateDataValue, deriv, sMode);
    }
}

// Jacobian
void AcBus::jacobianElements(const IOdata& inputs,
                             const StateData& stateDataValue,
                             MatrixData<double>& matrixDataValue,
                             const IOlocs& inputLocs,
                             const SolverMode& sMode)
{
    GridBus::jacobianElements(inputs, stateDataValue, matrixDataValue, inputLocs, sMode);

    // deal with the frequency block
    auto aoffset = offsets.getAOffset(sMode);
    if ((fblock) && (isDynamic(sMode))) {
        fblock->blockJacobianElements(
            outputs[ANGLE_IN_LOCATION], 0.0, stateDataValue, matrixDataValue, aoffset, sMode);
    }

    computeDerivatives(stateDataValue, sMode);
    if (isDifferentialOnly(sMode)) {
        return;
    }

    // compute the bus Jacobian elements themselves
    // printf("t=%f,id=%d, dpdt=%f, dpdv=%f, dqdt=%f, dqdv=%f\n", time, id, Ptii, Pvii, Qvii, Qtii);

    auto voffset = offsets.getVOffset(sMode);

    if (voffset != kNullLocation) {
        if (useVoltage(sMode)) {
            matrixDataValue.assignCheckCol(voffset,
                                           aoffset,
                                           partDeriv.at(QOUT_LOCATION, ANGLE_IN_LOCATION));
            matrixDataValue.assign(voffset,
                                   voffset,
                                   partDeriv.at(QOUT_LOCATION, VOLTAGE_IN_LOCATION));
            if (opFlags[USES_BUS_FREQUENCY]) {
                matrixDataValue.assignCheckCol(voffset,
                                               outLocs[FREQUENCY_IN_LOCATION],
                                               partDeriv.at(QOUT_LOCATION, FREQUENCY_IN_LOCATION));
            }
        } else {
            matrixDataValue.assign(voffset, voffset, 1);
        }
    }
    if (aoffset != kNullLocation) {
        if (useAngle(sMode)) {
            matrixDataValue.assign(aoffset,
                                   aoffset,
                                   partDeriv.at(POUT_LOCATION, ANGLE_IN_LOCATION));
            matrixDataValue.assignCheckCol(aoffset,
                                           voffset,
                                           partDeriv.at(POUT_LOCATION, VOLTAGE_IN_LOCATION));
            if (opFlags[USES_BUS_FREQUENCY]) {
                matrixDataValue.assignCheckCol(aoffset,
                                               outLocs[FREQUENCY_IN_LOCATION],
                                               partDeriv.at(POUT_LOCATION, FREQUENCY_IN_LOCATION));
            }
        } else {
            matrixDataValue.assign(aoffset, aoffset, 1);
        }
    }

    if (!isConnected()) {
        return;
    }
    of.setArray(matrixDataValue);

    of.setTranslation(POUT_LOCATION, useAngle(sMode) ? outLocs[ANGLE_IN_LOCATION] : kNullLocation);
    of.setTranslation(QOUT_LOCATION,
                      useVoltage(sMode) ? outLocs[VOLTAGE_IN_LOCATION] : kNullLocation);
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
        of.assign(POUT_LOCATION, offset, 1);
        of.assign(QOUT_LOCATION, offset + 1, 1);
    }
    auto gid = getID();
    for (auto& link : attachedLinks) {
        link->outputPartialDerivatives(gid, stateDataValue, of, sMode);
    }
}

void AcBus::voltageUpdate(const StateData& stateDataValue,
                          double update[],
                          const SolverMode& sMode,
                          double alpha)
{
    if (!isConnected()) {
        return;
    }
    auto voffset = offsets.getVOffset(sMode);
    const double voltageValue = getVoltage(stateDataValue, sMode);
    if (voltageValue < Vtol) {
        alert(this, VERY_LOW_VOLTAGE_ALERT);
        lowVtime = stateDataValue.time;
        return;
    }
    if (!useVoltage(sMode) || (voffset == kNullLocation)) {
        update[voffset] = voltageValue;
        return;
    }
    const bool useAngleState = useAngle(sMode);

    updateLocalCache(noInputs, stateDataValue, sMode);
    computeDerivatives(stateDataValue, sMode);

    const double realPowerDelta = S.sumP();
    const double reactivePowerDelta = useAngleState ? S.sumQ() : 0;

    const double realPowerByVoltage =
        useAngleState ? partDeriv.at(POUT_LOCATION, VOLTAGE_IN_LOCATION) : 1.0;
    const double reactivePowerByVoltage = partDeriv.at(QOUT_LOCATION, VOLTAGE_IN_LOCATION);

    double voltageDelta =
        (reactivePowerDelta / reactivePowerByVoltage) + (realPowerDelta / realPowerByVoltage);
    if (!std::isfinite(voltageDelta)) {
        voltageDelta = reactivePowerDelta / reactivePowerByVoltage;
    }
    voltageDelta = checkVoltageDelta(voltageDelta, voltageValue, 0.75, 0.15, 1.05);

    assert(std::isfinite(voltageDelta));
    assert(voltageValue - voltageDelta > 0);
    update[voffset] = voltageValue - (voltageDelta * alpha);
}

void AcBus::algebraicUpdate(const IOdata& inputs,
                            const StateData& stateDataValue,
                            double update[],
                            const SolverMode& sMode,
                            double alpha)
{
    auto voffset = offsets.getVOffset(sMode);
    auto aoffset = offsets.getAOffset(sMode);
    const double voltageValue = getVoltage(stateDataValue, sMode);
    const double angleValue = getAngle(stateDataValue, sMode);
    const bool useVoltageState = useVoltage(sMode) && (voffset != kNullLocation);
    const bool useAngleState =
        (!(opFlags[IGNORE_ANGLE])) && useAngle(sMode) && (aoffset != kNullLocation);

    if (useVoltageState && useAngleState) {
        updateLocalCache(inputs, stateDataValue, sMode);
        computeDerivatives(stateDataValue, sMode);

        const double realPowerDelta = S.sumP();
        const double reactivePowerDelta = S.sumQ();
        double voltageDelta;
        double angleDelta;
        const double realPowerByVoltage = partDeriv.at(POUT_LOCATION, VOLTAGE_IN_LOCATION);
        const double realPowerByAngle = partDeriv.at(POUT_LOCATION, ANGLE_IN_LOCATION);
        const double reactivePowerByVoltage = partDeriv.at(QOUT_LOCATION, VOLTAGE_IN_LOCATION);
        const double reactivePowerByAngle = partDeriv.at(QOUT_LOCATION, ANGLE_IN_LOCATION);
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
        update[voffset] = voltageValue - (voltageDelta * alpha);
        assert(std::isfinite(angleDelta));
        update[aoffset] = angleValue - (angleDelta * alpha);
    } else if (useAngleState) {
        updateLocalCache(noInputs, stateDataValue, sMode);
        computeDerivatives(stateDataValue, sMode);

        const double realPowerDelta = S.sumP();
        const double realPowerByAngle = partDeriv.at(POUT_LOCATION, ANGLE_IN_LOCATION);
        if (realPowerByAngle != 0) {
            const double angleDelta =
                checkAngleDelta(realPowerDelta / realPowerByAngle, angleValue);
            assert(std::isfinite(angleDelta));
            update[aoffset] = angleValue - (angleDelta * alpha);
        } else {
            update[aoffset] = angleValue;
        }

        if (voffset != kNullLocation) {
            update[voffset] = voltageValue;
        }
    } else if (useVoltageState) {
        updateLocalCache(noInputs, stateDataValue, sMode);
        computeDerivatives(stateDataValue, sMode);

        const double reactivePowerDelta = S.sumQ();
        const double reactivePowerByVoltage = partDeriv.at(QOUT_LOCATION, VOLTAGE_IN_LOCATION);
        if (reactivePowerByVoltage != 0) {
            const double voltageDelta =
                checkVoltageDelta(reactivePowerDelta / reactivePowerByVoltage, voltageValue);
            assert(std::isfinite(voltageDelta));
            update[voffset] = voltageValue - (voltageDelta * alpha);
        } else {
            update[aoffset] = angleValue;
        }
        if (aoffset != kNullLocation) {
            update[aoffset] = angleValue;
        }
    } else {
        if (aoffset != kNullLocation) {
            update[aoffset] = angleValue;
        }
        if (voffset != kNullLocation) {
            update[voffset] = voltageValue;
        }
    }
    GridBus::algebraicUpdate(noInputs, stateDataValue, update, sMode, alpha);
}

void AcBus::localConverge(const SolverMode& sMode, int mode, double tol)
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
        realPowerByVoltage = partDeriv.at(POUT_LOCATION, VOLTAGE_IN_LOCATION);
        realPowerByAngle = partDeriv.at(POUT_LOCATION, ANGLE_IN_LOCATION);
        reactivePowerByVoltage = partDeriv.at(QOUT_LOCATION, VOLTAGE_IN_LOCATION);
        reactivePowerByAngle = partDeriv.at(QOUT_LOCATION, ANGLE_IN_LOCATION);
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
        bool notConverged = true;
        while (notConverged) {
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
                realPowerByVoltage = partDeriv.at(POUT_LOCATION, VOLTAGE_IN_LOCATION);
                realPowerByAngle = partDeriv.at(POUT_LOCATION, ANGLE_IN_LOCATION);
                reactivePowerByVoltage = partDeriv.at(QOUT_LOCATION, VOLTAGE_IN_LOCATION);
                reactivePowerByAngle = partDeriv.at(QOUT_LOCATION, ANGLE_IN_LOCATION);
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
                    notConverged = false;
                }
                voltage -= voltageDelta;
                angle += angleDelta;
                if (++iteration > 10) {
                    notConverged = false;
                    voltage = voltageValue;
                    angle = angleValue;
                }
            } else {
                notConverged = false;
            }
        }
    }
}

void AcBus::convergeHighErrorOnly(const StateData& stateDataValue,
                                  double state[],
                                  const SolverMode& sMode,
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

bool AcBus::convergeStrongIteration(const StateData& stateDataValue,
                                    double state[],
                                    const SolverMode& sMode,
                                    ConvergeMode& mode,
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
            (mode != ConvergeMode::FORCE_STRONG_ITERATION)) {
            mode = ConvergeMode::FORCE_VOLTAGE_ONLY;
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
        if (iteration >= 10) {
            break;
        }
        boundedIncrement(iteration);
    }
    return false;
}

bool AcBus::convergeVoltageOnly(const StateData& stateDataValue,
                                double state[],
                                const SolverMode& sMode,
                                ConvergeMode& mode,
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
            // A previous local update can drive the state to zero between
            // iterations. Keep the same positive-voltage invariant enforced
            // at the start of converge() before limiting the next correction.
            if (voltageValue <= 0.0) {
                voltageValue = std::abs(voltageValue - 0.001);
                if (useVoltageState) {
                    state[voltageOffset] = voltageValue;
                }
            }
            if ((voltageValue > vTarget * 1.1) && (mode != ConvergeMode::FORCE_VOLTAGE_ONLY)) {
                mode = ConvergeMode::FORCE_STRONG_ITERATION;
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
        const double realPowerByVoltage = partDeriv.at(POUT_LOCATION, VOLTAGE_IN_LOCATION);
        const double reactivePowerByVoltage = partDeriv.at(QOUT_LOCATION, VOLTAGE_IN_LOCATION);
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
                    boundedIncrement(forceCount);
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
                StateData generatorState;
                generatorState.state = state;
                gen->algebraicUpdate({voltageValue - voltageDelta, angleValue, frequencyValue},
                                     generatorState,
                                     state,
                                     sMode,
                                     1.0);
            }
        }
        if (iteration >= 10) {
            notConverged = false;
        } else {
            boundedIncrement(iteration);
        }
    }

    return false;
}

void AcBus::converge(CoreTime time,
                     double state[],
                     double dstateDt[],
                     const SolverMode& sMode,
                     ConvergeMode mode,
                     double tol)
{
    if (!isEnabled() || isDifferentialOnly(sMode) || opFlags[DISCONNECTED]) {
        return;
    }

    auto voffset = offsets.getVOffset(sMode);
    auto aoffset = offsets.getAOffset(sMode);

    const bool useVoltageState = useVoltage(sMode) && (voffset != kNullLocation);
    const bool useAngleState = useAngle(sMode) && (aoffset != kNullLocation);
    const StateData stateDataValue(time, state, dstateDt);
    double voltageValue = useVoltageState ? state[voffset] : voltage;
    double angleValue = useAngleState ? state[aoffset] : angle;
    const double frequencyValue = getFreq(stateDataValue, sMode);
    if (voltageValue <= 0.0) {
        voltageValue = std::abs(voltageValue - 0.001);
        if (voffset != kNullLocation) {
            state[voffset] = voltageValue;
        }
    }
    double currentModeVlimit = 0.02 * vTarget;
    bool forceVoltageUp = false;
    int iteration = 1;
    if (isDAE(sMode)) {
        currentModeVlimit = (!attachedGens.empty()) ? 0.4 : 0.05;
        currentModeVlimit *= vTarget;
    }
    if ((voltageValue < currentModeVlimit) && (mode != ConvergeMode::FORCE_VOLTAGE_ONLY)) {
        mode = ConvergeMode::VOLTAGE_ONLY;
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
            case ConvergeMode::HIGH_ERROR_ONLY:
                convergeHighErrorOnly(stateDataValue, state, sMode, err, tol);
                break;
            case ConvergeMode::SINGLE_ITERATION:
            case ConvergeMode::BLOCK_ITERATION:
                algebraicUpdate(noInputs, stateDataValue, state, sMode, 1.0);
                break;
            case ConvergeMode::LOCAL_ITERATION:
            case ConvergeMode::STRONG_ITERATION:
            case ConvergeMode::FORCE_STRONG_ITERATION:
                restartConvergence = convergeStrongIteration(stateDataValue,
                                                             state,
                                                             sMode,
                                                             mode,
                                                             err,
                                                             voltageValue,
                                                             angleValue,
                                                             useVoltageState,
                                                             useAngleState,
                                                             voffset,
                                                             aoffset,
                                                             currentModeVlimit,
                                                             tol,
                                                             iteration);
                break;
            case ConvergeMode::VOLTAGE_ONLY:
            case ConvergeMode::FORCE_VOLTAGE_ONLY:
                restartConvergence = convergeVoltageOnly(stateDataValue,
                                                         state,
                                                         sMode,
                                                         mode,
                                                         voltageValue,
                                                         angleValue,
                                                         frequencyValue,
                                                         useVoltageState,
                                                         voffset,
                                                         tol,
                                                         forceVoltageUp,
                                                         iteration);
                break;
            default:
                break;
        }
    }
}

double AcBus::computeError(const StateData& stateDataValue, const SolverMode& sMode)
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

static const stringVec ST_NAMES{"voltage", "angle"};
stringVec AcBus::localStateNames() const
{
    return ST_NAMES;
}

void AcBus::setOffsets(const SolverOffsets& newOffsets, const SolverMode& sMode)
{
    offsets.setOffsets(newOffsets, sMode);
    SolverOffsets newLocalOffsets(newOffsets);
    newLocalOffsets.localIncrement(offsets.getOffsets(sMode));
    for (auto* load : attachedLoads) {
        load->setOffsets(newLocalOffsets, sMode);
        newLocalOffsets.increment(load->getOffsets(sMode));
    }
    for (auto* gen : attachedGens) {
        gen->setOffsets(newLocalOffsets, sMode);
        newLocalOffsets.increment(gen->getOffsets(sMode));
    }
    if (opFlags[SLAVE_BUS]) {
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

void AcBus::setOffset(index_t offset, const SolverMode& sMode)
{
    for (auto* load : attachedLoads) {
        load->setOffset(offset, sMode);
        offset += load->stateSize(sMode);
    }
    for (auto* gen : attachedGens) {
        gen->setOffset(offset, sMode);
        offset += gen->stateSize(sMode);
    }
    if (opFlags[SLAVE_BUS]) {
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

void AcBus::setRootOffset(index_t roffset, const SolverMode& sMode)
{
    offsets.setRootOffset(roffset, sMode);
    auto& solverOffsetData = offsets.getOffsets(sMode);
    auto rootCount = solverOffsetData.local.algRoots + solverOffsetData.local.diffRoots;
    for (auto& gen : attachedGens) {
        gen->setRootOffset(roffset + rootCount, sMode);
        rootCount += gen->rootSize(sMode);
    }
    for (auto& load : attachedLoads) {
        load->setRootOffset(roffset + rootCount, sMode);
        rootCount += load->rootSize(sMode);
    }
    if (opFlags[COMPUTE_FREQUENCY]) {
        fblock->setRootOffset(roffset + rootCount, sMode);
        // nR += fblock->rootSize (sMode);
    }
}

void AcBus::reconnect(GridBus* mapBus)
{
    if (!opFlags[DISCONNECTED]) {
        return;
    }

    GridBus::reconnect(mapBus);

    std::vector<GridBus*> pendingReconnects(busController.slaveBusses.begin(),
                                            busController.slaveBusses.end());
    while (!pendingReconnects.empty()) {
        auto* slaveBus = pendingReconnects.back();
        pendingReconnects.pop_back();
        if (!slaveBus->checkFlag(DISCONNECTED)) {
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
bool AcBus::useAngle(const SolverMode& sMode) const
{
    if ((hasAlgebraic(sMode)) && (isConnected())) {
        if (isDynamic(sMode)) {
            if ((dynType == DynBusType::NORMAL) || (dynType == DynBusType::FIX_VOLTAGE)) {
                return true;
            }
        } else if ((type == BusType::PQ) || (type == BusType::PV)) {
            return true;
        }
    }
    return false;
}

bool AcBus::useVoltage(const SolverMode& sMode) const
{
    if ((hasAlgebraic(sMode)) && (isConnected()) && (!isDC(sMode))) {
        if (isDynamic(sMode)) {
            if ((dynType == DynBusType::NORMAL) || (dynType == DynBusType::FIX_ANGLE)) {
                return true;
            }
        } else if ((type == BusType::PQ) || (type == BusType::AFIX)) {
            return true;
        }
    }
    return false;
}

count_t AcBus::getDependencyCount(const SolverMode& sMode) const
{
    count_t sum = 0;
    if (isDC(sMode)) {
        for (const auto& load : attachedLoads) {
            sum += load->outputDependencyCount(POUT_LOCATION, sMode);
        }
        for (const auto& gen : attachedGens) {
            sum += gen->outputDependencyCount(POUT_LOCATION, sMode);
        }
        for (const auto& lnk : attachedLinks) {
            sum += lnk->outputDependencyCount(POUT_LOCATION, sMode);
        }
    } else {
        for (const auto& load : attachedLoads) {
            sum += load->outputDependencyCount(POUT_LOCATION, sMode);
            sum += load->outputDependencyCount(QOUT_LOCATION, sMode);
        }
        for (const auto& gen : attachedGens) {
            sum += gen->outputDependencyCount(POUT_LOCATION, sMode);
            sum += gen->outputDependencyCount(QOUT_LOCATION, sMode);
        }
        for (const auto& lnk : attachedLinks) {
            sum += lnk->outputDependencyCount(POUT_LOCATION, sMode);
            sum += lnk->outputDependencyCount(QOUT_LOCATION, sMode);
        }
    }
    return sum;
}

StateSizes AcBus::localStateSizes(const SolverMode& sMode) const
{
    StateSizes busSS;
    if (hasAlgebraic(sMode)) {
        busSS.aSize = 1;
        if (isAC(sMode)) {
            busSS.vSize = 1;
        }
        // check for slave bus mode
        if (opFlags[SLAVE_BUS]) {
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

count_t AcBus::localJacobianCount(const SolverMode& sMode) const
{
    count_t totaljacSize = 0;
    if (hasAlgebraic(sMode)) {
        if (isDC(sMode)) {
            totaljacSize = 1 + getDependencyCount(sMode);
        } else {
            totaljacSize = 4 + getDependencyCount(sMode);
        }
        // check for slave bus mode
        if (opFlags[SLAVE_BUS]) {
            totaljacSize -= (isDC(sMode)) ? 1 : 4;
        }
    }
    return totaljacSize;
}

int AcBus::getMode(const SolverMode& sMode) const
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
    opFlags.reset(PRE_EX_REQUESTED);
    opFlags.reset(HAS_POWERFLOW_ADJUSTMENTS);
    if (prevType == BusType::SLK) {
        // check for P limits
        if ((busController.Pmin > -kHalfBigNum) || (busController.Pmax < kHalfBigNum)) {
            opFlags[HAS_POWERFLOW_ADJUSTMENTS] = true;
        }

        // check for Qlimits
        if ((busController.Qmin > -kHalfBigNum) || (busController.Qmax < kHalfBigNum)) {
            opFlags[HAS_POWERFLOW_ADJUSTMENTS] = true;
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
    if (opFlags[COMPUTE_FREQUENCY]) {
        opFlags |= fblock->cascadingFlags();
    }
    if (prevType == BusType::PV) {
        if ((busController.Qmin > -kHalfBigNum) || (busController.Qmax < kHalfBigNum)) {
            opFlags[HAS_POWERFLOW_ADJUSTMENTS] = true;
        }
    } else if (prevType == BusType::AFIX) {
        if ((busController.Pmin > -kHalfBigNum) || (busController.Pmax < kHalfBigNum)) {
            opFlags[HAS_POWERFLOW_ADJUSTMENTS] = true;
        }
    }
}

void AcBus::updateControlLimits()
{
    if ((type == BusType::PV) || (type == BusType::SLK)) {
        busController.updateVoltageControlLimits();
    }
    if ((type == BusType::AFIX) || (type == BusType::SLK)) {
        busController.updatePowerControlLimits();
    }
}

static const IOlocs IN_LOC{0, 1, 2};

void AcBus::computeDerivatives(const StateData& stateDataValue, const SolverMode& sMode)
{
    if (!isConnected()) {
        return;
    }
    partDeriv.clear();

    for (auto& link : attachedLinks) {
        if (link->isEnabled()) {
            link->updateLocalCache(noInputs, stateDataValue, sMode);
            link->ioPartialDerivatives(getID(), stateDataValue, partDeriv, IN_LOC, sMode);
        }
    }
    if (!isExtended(sMode)) {
        for (auto& gen : attachedGens) {
            if (gen->isConnected()) {
                gen->ioPartialDerivatives(outputs, stateDataValue, partDeriv, IN_LOC, sMode);
            }
        }
        for (auto& load : attachedLoads) {
            if (load->isConnected()) {
                load->ioPartialDerivatives(outputs, stateDataValue, partDeriv, IN_LOC, sMode);
            }
        }
    }
}

// computed power at bus
void AcBus::updateLocalCache(const IOdata& inputs,
                             const StateData& stateDataValue,
                             const SolverMode& sMode)
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

double AcBus::getAdjustableCapacityUp(CoreTime time) const
{
    return busController.getAdjustableCapacityUp(time);
}

double AcBus::getAdjustableCapacityDown(CoreTime time) const
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

ChangeCode AcBus::rootCheck(const IOdata& inputs,
                            const StateData& stateDataValue,
                            const SolverMode& sMode,
                            CheckLevel level)
{
    const double currentVoltage = getVoltage(stateDataValue, sMode);
    ChangeCode ret = ChangeCode::NO_CHANGE;
    if (level == CheckLevel::LOW_VOLTAGE_CHECK) {
        if (!isConnected()) {
            return ret;
        }
        if (currentVoltage < 1e-8) {
            disconnect();
            ret = ChangeCode::JACOBIAN_CHANGE;
            logging::debug(this, "Bus low voltage disconnect");
        }
        if (opFlags[PREV_LOW_VOLTAGE_ALERT]) {
            if (stateDataValue.time <= lowVtime) {
                disconnect();
                opFlags.reset(PREV_LOW_VOLTAGE_ALERT);
                ret = ChangeCode::JACOBIAN_CHANGE;
                logging::debug(this, "Bus low voltage disconnect");
            } else {
                opFlags.reset(PREV_LOW_VOLTAGE_ALERT);
            }
        }
        return ret;
    }
    if (level == CheckLevel::COMPLETE_STATE_CHECK) {
        if (currentVoltage < 1e-5) {
            logging::normal(this, "bus disconnecting from low voltage");
            disconnect();
        } else if (isDAE(sMode)) {
            if (dynType == DynBusType::NORMAL) {
                if (currentVoltage < 0.001) {
                    prevDynType = DynBusType::NORMAL;
                    refAngle = static_cast<GridArea*>(getParent())
                                   ->getMasterAngle(emptyStateData, cLocalSolverMode);

                    dynType = DynBusType::FIX_ANGLE;
                    alert(this, JAC_COUNT_DECREASE);
                    ret = ChangeCode::JACOBIAN_CHANGE;
                }
            } else if (dynType == DynBusType::FIX_ANGLE) {
                if (prevDynType == DynBusType::NORMAL) {
                    if (currentVoltage > 0.1) {
                        dynType = DynBusType::NORMAL;
                        const double newAngle =
                            static_cast<GridArea*>(getParent())
                                ->getMasterAngle(emptyStateData, cLocalSolverMode);
                        angle = angle + (newAngle - refAngle);
                        alert(this, JAC_COUNT_INCREASE);
                        ret = ChangeCode::JACOBIAN_CHANGE;
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
