/*
 * Copyright (c) 2014-2026, Lawrence Livermore National Security
 * See the top-level NOTICE for additional details. All rights reserved.
 * SPDX-License-Identifier: BSD-3-Clause
 */

#include "BusControls.h"

#include "../GridSecondary.h"
#include "../Link.h"
#include "AcBus.h"
#include "gmlc/utilities/vectorOps.hpp"
#include <algorithm>

namespace griddyn {
BusControls::BusControls(AcBus* busToControl): controlledBus(busToControl) {}

bool BusControls::hasVoltageAdjustments(id_type_t sid) const
{
    return std::ranges::any_of(vControlObjects,
                               [sid](const auto* adjustment) {
                                   return sid == adjustment->getID();
                               }) ||
        std::ranges::any_of(proxyVControlObject,
                            [sid](const auto* adjustment) { return sid == adjustment->getID(); });
}

bool BusControls::hasPowerAdjustments(id_type_t sid) const
{
    return std::ranges::any_of(pControlObjects,
                               [sid](const auto* adjustment) {
                                   return sid == adjustment->getID();
                               }) ||
        std::ranges::any_of(proxyPControlObject,
                            [sid](const auto* adjustment) { return sid == adjustment->getID(); });
}

double BusControls::getAdjustableCapacityUp(CoreTime time) const
{
    double cap = 0.0;

    for (const auto* adjustment : pControlObjects) {
        cap += adjustment->getAdjustableCapacityUp(time);
    }
    // TODO(phlpt): Links do not have this function yet.
    /*
    for (auto &adj : pControlLinks)
    {

        //cap += adjustment->getAdjustableCapacityUp(time);
    }
    */
    return cap;
}

double BusControls::getAdjustableCapacityDown(CoreTime time) const
{
    double cap = 0.0;

    for (const auto* adjustment : pControlObjects) {
        cap += adjustment->getAdjustableCapacityDown(time);
    }
    // TODO(phlpt): Links do not have this function yet.
    /*
    for (auto &adj : pControlLinks)
    {
        cap += adjustment->getAdjustableCapacityDown(time);
    }
    */
    return cap;
}

void BusControls::addPowerControlObject(GridComponent* comp, bool update)
{
    if (dynamic_cast<GridSecondary*>(comp) != nullptr) {
        auto objid = comp->getID();
        for (auto& rvc : pControlObjects) {
            if (objid == rvc->getID()) {
                return;
            }
        }
        pControlObjects.push_back(static_cast<GridSecondary*>(comp));
        pcfrac.push_back(comp->get("participation"));
    } else if (dynamic_cast<Link*>(comp) != nullptr) {
        auto objid = comp->getID();
        for (auto& rvc : pControlLinks) {
            if (objid == rvc->getID()) {
                return;
            }
        }
        pControlLinks.push_back(static_cast<Link*>(comp));
        pclinkFrac.push_back(comp->get("participation"));
    } else {
        return;
    }
    if (update) {
        updatePowerControls();
    }
}

void BusControls::addVoltageControlObject(GridComponent* comp, bool update)
{
    if (dynamic_cast<GridSecondary*>(comp) != nullptr) {
        auto objid = comp->getID();
        for (auto& rvc : vControlObjects) {
            if (objid == rvc->getID()) {
                return;
            }
        }
        vControlObjects.push_back(static_cast<GridSecondary*>(comp));
        vcfrac.push_back(comp->get("vcontrolfrac"));
    } else if (dynamic_cast<Link*>(comp) != nullptr) {
        auto objid = comp->getID();
        for (auto& rvc : vControlLinks) {
            if (objid == rvc->getID()) {
                return;
            }
        }
        vControlLinks.push_back(static_cast<Link*>(comp));
        vclinkFrac.push_back(comp->get("vcontrolfrac"));
    } else {
        return;
    }
    if (update) {
        updateVoltageControls();
    }
}

void BusControls::removePowerControlObject(id_type_t oid, bool update)
{
    for (size_t kk = 0; kk < pControlObjects.size(); ++kk) {
        if (oid == pControlObjects[kk]->getID()) {
            pControlObjects.erase(pControlObjects.begin() + kk);
            pcfrac.erase(pcfrac.begin() + kk);
            if (update) {
                updatePowerControls();
            }
            return;
        }
    }
    for (size_t kk = 0; kk < pControlLinks.size(); ++kk) {
        if (oid == pControlLinks[kk]->getID()) {
            pControlLinks.erase(pControlLinks.begin() + kk);
            pclinkFrac.erase(pclinkFrac.begin() + kk);
            if (update) {
                updatePowerControls();
            }
        }
    }
}

void BusControls::removeVoltageControlObject(id_type_t oid, bool update)
{
    for (size_t kk = 0; kk < vControlObjects.size(); ++kk) {
        if (oid == vControlObjects[kk]->getID()) {
            vControlObjects.erase(vControlObjects.begin() + kk);
            vcfrac.erase(vcfrac.begin() + kk);
            if (update) {
                updateVoltageControls();
            }
            return;
        }
    }
    for (size_t kk = 0; kk < vControlLinks.size(); ++kk) {
        if (oid == vControlLinks[kk]->getID()) {
            vControlLinks.erase(vControlLinks.begin() + kk);
            vclinkFrac.erase(vclinkFrac.begin() + kk);
            if (update) {
                updateVoltageControls();
            }
        }
    }
}

using gmlc::utilities::sum;

void BusControls::updateVoltageControls()
{
    const double voltageFractionSum = sum(vcfrac) + sum(vclinkFrac);
    proxyVControlObject.clear();
    bool nonDirectRemote = false;
    GridComponent* vco;
    Qmax = 0;
    Qmin = 0;
    for (size_t kk = 0; kk < vcfrac.size(); ++kk) {
        vco = vControlObjects[kk];
        if (voltageFractionSum != 1.0) {
            vcfrac[kk] /= voltageFractionSum;
            vco->set("vcontrolfrac", vcfrac[kk]);
        }
        Qmax += vco->get("qmax");
        Qmin += vco->get("qmin");
        if (vco->checkFlag(REMOTE_VOLTAGE_CONTROL)) {
            if (static_cast<AcBus*>(vco->find("bus"))->directPath(controlledBus, vco)) {
                auto opath =
                    static_cast<AcBus*>(vco->find("bus"))->getDirectPath(controlledBus, vco);
                opath.pop_back();
                if (dynamic_cast<Link*>(opath.back()) != nullptr) {
                    proxyVControlObject.push_back(static_cast<Link*>(opath.back()));
                }
            } else {
                nonDirectRemote = true;
            }
        }
    }

    for (size_t kk = 0; kk < vclinkFrac.size(); ++kk) {
        vco = vControlLinks[kk];
        if (voltageFractionSum != 1.0) {
            vclinkFrac[kk] /= voltageFractionSum;
            vco->set("vcontrolfrac", vclinkFrac[kk]);
        }
        Qmax += vco->get("qmax");
        Qmin += vco->get("qmin");
        if (vco->checkFlag(REMOTE_VOLTAGE_CONTROL)) {
            nonDirectRemote = true;
        }
    }

    if (nonDirectRemote) {
        for (auto& vcobj : vControlObjects) {
            vcobj->setFlag("indirect_voltage_control", true);
        }
        for (auto& vcobj : vControlLinks) {
            vcobj->setFlag("indirect_voltage_control", true);
        }
        controlledBus->set("type", "pq");
        controlledBus->opFlags.set(INDIRECT_VOLTAGE_CONTROL);
    }
    // check if the v and p controls are identical
    controlledBus->opFlags.set(AcBus::BusFlags::IDENTICAL_PQ_CONTROL_OBJECTS,
                               checkIdenticalControls());
}

void BusControls::updateVoltageControlLimits()
{
    Qmax = 0.0;
    Qmin = 0.0;
    for (const auto* control : vControlObjects) {
        Qmax += control->get("qmax");
        Qmin += control->get("qmin");
    }
    for (const auto* control : vControlLinks) {
        Qmax += control->get("qmax");
        Qmin += control->get("qmin");
    }
}

void BusControls::updatePowerControls()
{
    const double powerFractionSum = sum(pcfrac) + sum(pclinkFrac);
    proxyPControlObject.clear();
    GridComponent* pco;
    const auto powerControlCount = pcfrac.size() + pclinkFrac.size();
    Pmax = 0;
    Pmin = 0;
    for (size_t kk = 0; kk < pcfrac.size(); ++kk) {
        pco = pControlObjects[kk];
        if ((powerFractionSum > 1.0) && (powerControlCount > 1)) {
            pcfrac[kk] /= powerFractionSum;
            pco->set("participation", pcfrac[kk]);
        }
        Pmax += pco->get("pmax");
        Pmin += pco->get("pmin");
        if (pco->checkFlag(REMOTE_POWER_CONTROL)) {
            if (static_cast<AcBus*>(pco->find("bus"))->directPath(controlledBus, pco)) {
                auto opath =
                    static_cast<AcBus*>(pco->find("bus"))->getDirectPath(controlledBus, pco);
                opath.pop_back();
                if (dynamic_cast<Link*>(opath.back()) != nullptr) {
                    proxyPControlObject.push_back(static_cast<Link*>(opath.back()));
                }
            } else {
                controlledBus->log(controlledBus,
                                   PrintLevel::WARNING,
                                   "Generator " + pco->getName() +
                                       " on indirect path for power control to bus " +
                                       controlledBus->getName());
            }
        }
    }
    for (size_t kk = 0; kk < pclinkFrac.size(); ++kk) {
        pco = pControlLinks[kk];
        if (powerFractionSum != 1.0) {
            pclinkFrac[kk] /= powerFractionSum;
            pco->set("participation", pclinkFrac[kk]);
        }
        Pmax += pco->get("pmax");
        Pmin += pco->get("pmin");
        if (pco->checkFlag(REMOTE_POWER_CONTROL)) {
            // TODO(phlpt): Figure out what to do in this case.
        }
    }
    // override bus participation with generator participation
    if ((powerControlCount == 1) && (powerFractionSum != 1.0) &&
        (controlledBus->get("participation") == 1.0)) {
        controlledBus->set("participation", powerFractionSum);
    }
    // check if the v and p controls are identical
    controlledBus->opFlags.set(AcBus::BusFlags::IDENTICAL_PQ_CONTROL_OBJECTS,
                               checkIdenticalControls());
}

void BusControls::updatePowerControlLimits()
{
    Pmax = 0.0;
    Pmin = 0.0;
    for (const auto* control : pControlObjects) {
        Pmax += control->get("pmax");
        Pmin += control->get("pmin");
    }
    for (const auto* control : pControlLinks) {
        Pmax += control->get("pmax");
        Pmin += control->get("pmin");
    }
}

bool BusControls::checkIdenticalControls()
{
    if ((vControlObjects.size() == pControlObjects.size()) &&
        (vControlLinks.size() == pControlLinks.size())) {
        for (size_t kk = 0; kk < vControlObjects.size(); ++kk) {
            if (!isSameObject(vControlObjects[kk], pControlObjects[kk])) {
                return false;
            }
        }
        for (size_t kk = 0; kk < vControlLinks.size(); ++kk) {
            if (!isSameObject(vControlLinks[kk], pControlLinks[kk])) {
                return false;
            }
        }
    } else {
        return false;
    }
    return true;
}

// NOLINTNEXTLINE(misc-no-recursion): bus ownership determines the recursive merge target.
void BusControls::mergeBus(AcBus* mbus)
{
    // bus with the lowest ID is the master
    if (controlledBus->getID() < mbus->getID()) {
        if (controlledBus->checkFlag(
                AcBus::BusFlags::SLAVE_BUS))  // if we are already a slave forward the merge to the
                                              // master
        {
            masterBus->mergeBus(mbus);
        } else {
            if (mbus->checkFlag(AcBus::BusFlags::SLAVE_BUS)) {
                if (controlledBus->getID() != mbus->busController.masterBus->getID()) {
                    mergeBus(static_cast<AcBus*>(mbus->busController.masterBus));
                }
            } else {
                // This bus becomes the master of mbus
                mbus->busController.masterBus = controlledBus;
                mbus->opFlags.set(AcBus::BusFlags::SLAVE_BUS);
                slaveBusses.push_back(mbus);
                for (auto* slaveBus : mbus->busController.slaveBusses) {
                    slaveBusses.push_back(slaveBus);
                    slaveBus->busController.masterBus = controlledBus;
                }
                mbus->busController.slaveBusses.clear();
            }
        }
    } else if (controlledBus->getID() > mbus->getID()) {  // mbus is now this buses master
        if (controlledBus->checkFlag(AcBus::BusFlags::SLAVE_BUS)) {
            // if we are already a slave forward the merge to the master
            if (masterBus->getID() != mbus->getID()) {
                masterBus->mergeBus(mbus);
            }
        } else {  // we were a master now mbus is the master
            if (slaveBusses.empty())  // no slave buses
            {
                masterBus = mbus;
                mbus->busController.slaveBusses.push_back(controlledBus);
            } else {
                if (mbus->checkFlag(AcBus::BusFlags::SLAVE_BUS)) {
                    mbus->busController.masterBus->mergeBus(controlledBus);
                } else {
                    masterBus = mbus;
                    mbus->busController.slaveBusses.push_back(controlledBus);
                    for (auto* slaveBus : slaveBusses) {
                        mbus->busController.slaveBusses.push_back(slaveBus);
                        slaveBus->busController.masterBus = mbus;
                    }
                    slaveBusses.clear();
                }
            }
        }
    }
}

void BusControls::unmergeBus(AcBus* mbus)
{
    if (controlledBus->checkFlag(AcBus::BusFlags::SLAVE_BUS)) {
        if (mbus->checkFlag(AcBus::BusFlags::SLAVE_BUS)) {
            if (isSameObject(mbus->busController.masterBus, masterBus)) {
                masterBus->unmergeBus(mbus);
            }
        } else if (isSameObject(masterBus, mbus)) {
            mbus->unmergeBus(controlledBus);  // flip it around so this bus is unmerged from mbus
        }
    } else {  // in the masterbus
        if ((mbus->checkFlag(AcBus::BusFlags::SLAVE_BUS)) &&
            (isSameObject(controlledBus, mbus->busController.masterBus))) {
            for (auto* slaveBus : slaveBusses) {
                slaveBus->opFlags.reset(AcBus::BusFlags::SLAVE_BUS);
            }
            checkMerge();
            mbus->checkMerge();
        }
    }
}

void BusControls::checkMerge() const
{
    if (!controlledBus->isEnabled()) {
        return;
    }
    if (controlledBus->checkFlag(AcBus::BusFlags::DIRECTCONNECT)) {
        directBus->mergeBus(controlledBus);
    }
    for (auto* const link : controlledBus->attachedLinks) {
        link->checkMerge();
    }
}

}  // namespace griddyn
