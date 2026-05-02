/*
 * Copyright (c) 2014-2026, Lawrence Livermore National Security
 * See the top-level NOTICE for additional details. All rights reserved.
 * SPDX-License-Identifier: BSD-3-Clause
 */

#include "helicsCoordinator.h"

#include "gmlc/containers/mapOps.hpp"
#include "gmlc/utilities/stringConversion.h"
#include "griddyn/comms/commMessage.h"
#include "griddyn/comms/communicationsCore.h"
#include "griddyn/events/eventAdapters.h"
#include "griddyn/gridDynSimulation.h"
#include "helics/application_api.hpp"
#include "helics/application_api/typeOperations.hpp"
#include "helics/helicsEvent.h"
#include <algorithm>
#include <memory>
#include <string>
#include <unordered_map>
#include <utility>

namespace griddyn::helicsLib {
std::unordered_map<std::string, helicsCoordinator*> helicsCoordinator::registry;
std::mutex helicsCoordinator::registry_protection;

void helicsCoordinator::registerCoordinator(const std::string& coordinatorName,
                                            helicsCoordinator* ptr)
{
    std::lock_guard<std::mutex> lock(registry_protection);
    registry[coordinatorName] = ptr;
}

void helicsCoordinator::unregisterCoordinator(const std::string& coordinatorName)
{
    std::lock_guard<std::mutex> lock(registry_protection);
    auto fnd = registry.find(coordinatorName);
    if (fnd != registry.end()) {
        registry.erase(fnd);
    }
}

helicsCoordinator* helicsCoordinator::findCoordinator(const std::string& coordinatorName)
{
    std::lock_guard<std::mutex> lock(registry_protection);
    auto fnd = registry.find(coordinatorName);
    if (fnd != registry.end()) {
        return fnd->second;
    }
    return nullptr;
}

void helicsCoordinator::loadCommandLine(int argc, char* argv[])
{
    info_.loadInfoFromArgs(argc, argv);
}

helicsCoordinator::helicsCoordinator(const std::string& fedName): coreObject("helics")
{
    registerCoordinator(fedName, this);
}

std::shared_ptr<helics::Federate> helicsCoordinator::RegisterAsFederate()
{
    if (info_.defName.empty()) {
        info_.defName = getParent()->getName();
    }
    std::shared_ptr<helics::Federate> cfed;
    try {
        cfed = std::make_shared<helics::CombinationFederate>(std::string{}, info_);
    }
    catch (const helics::RegistrationFailure& e) {
        logging::warning(this, "failed to register as HELICS federate:{}", e.what());
        return nullptr;
    }
    vFed_ = dynamic_cast<helics::ValueFederate*>(cfed.get());
    mFed_ = dynamic_cast<helics::MessageFederate*>(cfed.get());
    fed = std::move(cfed);

    int ii = 0;
    pubs_.resize(pubI.size());
    for (auto& p : pubI) {
        if (p.unitType != units::defunit) {
            pubs_[ii] =
                helics::Publication(helics::GLOBAL, vFed_, p.name, p.type, to_string(p.unitType));
        } else {
            pubs_[ii] = helics::Publication(helics::GLOBAL, vFed_, p.name, p.type);
        }
        ++ii;
    }
    ii = 0;
    subs_.resize(subI.size());
    for (auto& s : subI) {
        if (s.unitType != units::defunit) {
            subs_[ii] = vFed_->registerSubscription(s.name, to_string(s.unitType));
        } else {
            subs_[ii] = vFed_->registerSubscription(s.name);
        }
        subs_[ii].setOption(helics::defs::options::connection_optional);
        ++ii;
    }

    ii = 0;
    epts_.resize(eptI.size());
    for (auto& e : eptI) {
        epts_[ii] = helics::Endpoint(mFed_, e.name, e.type);
        if (!e.target.empty()) {
            epts_[ii].setDefaultDestination(e.target);
        }
        ++ii;
    }
    // register a callback for handling messages from endpoints
    mFed_->setMessageNotificationCallback(
        [this](helics::Endpoint& ep, helics::Time t) { this->receiveMessage(ep, t); });

    fed->enterInitializingMode();
    logging::summary(this, "entered HELICS initializing Mode");
    for (auto evnt : events) {
        if (evnt->initNeeded()) {
            evnt->initialize();
        }
    }
    return fed;
}

void helicsCoordinator::setFlag(const std::string& flag, bool val)
{
    auto flagprop = helics::getPropertyIndex(flag);
    if (flagprop != -1) {
        info_.setFlagOption(flagprop, val);
        if (fed) {
            fed->setFlagOption(flagprop, val);
        }
    } else {
        coreObject::setFlag(flag, val);
    }
}
void helicsCoordinator::set(const std::string& param, const std::string& val)
{
    if ((param == "corename") || (param == "core_name")) {
        info_.coreName = val;
    } else if ((param == "initstring") || (param == "init") || (param == "core_init")) {
        info_.coreInitString = val;
    } else if ((param == "coretype") || (param == "core_type")) {
        info_.coreType = helics::coreTypeFromString(val);
    } else if ((param == "name") || (param == "fed_name")) {
        if (val != info_.defName) {
            unregisterCoordinator(info_.defName);
            registerCoordinator(val, this);
            info_.defName = val;
        }
    } else if ((param == "broker") || (param == "connection_info")) {
        connectionInfo = val;
    } else {
        coreObject::set(param, val);
    }
}

void helicsCoordinator::set(const std::string& param, double val, units::unit unitType)
{
    auto propVal = helics::getPropertyIndex(param);
    if (propVal >= 0) {
        info_.setProperty(propVal, val);
        if (fed) {
            fed->setProperty(propVal, val);
        }
    } else {
        coreObject::set(param, val, unitType);
    }
}

void helicsCoordinator::receiveMessage(helics::Endpoint& ep, helics::Time t)
{
    auto payload = ep.getMessage()->to_string();
    std::shared_ptr<griddyn::commMessage> msg;
    msg->from_string(payload);

    auto event = std::make_unique<griddyn::functionEventAdapter>([this, msg, &ep]() {
        communicationsCore::instance()->send(0, ep.getName(), std::move(msg));
        return griddyn::change_code::no_change;
    });

    // convert helics::Time to griddynTime
    event->m_nextTime = t;
    gridDynSimulation::getInstance()->add(std::shared_ptr<griddyn::eventAdapter>(std::move(event)));
}

void helicsCoordinator::sendMessage(int32_t index, const char* data, count_t size)
{
    if (isValidIndex(index, epts_)) {
        epts_[index].send(data, size);
    }
}
void helicsCoordinator::sendMessage(int32_t index,
                                    const std::string& dest,
                                    const char* data,
                                    count_t size)
{
    if (isValidIndex(index, epts_)) {
        epts_[index].send(dest, data, size);
    }
}

bool helicsCoordinator::isUpdated(int32_t index)
{
    if (isValidIndex(index, subs_)) {
        return subs_[index].isUpdated();
    }
    return false;
}

bool helicsCoordinator::hasMessage(int32_t index) const
{
    if (isValidIndex(index, epts_)) {
        return epts_[index].hasMessage();
    }
    return false;
}

helics::Publication* helicsCoordinator::getPublicationPointer(int32_t index)
{
    if (isValidIndex(index, pubs_)) {
        return &pubs_[index];
    }
    return nullptr;
}

helics::Input* helicsCoordinator::getInputPointer(int32_t index)
{
    if (isValidIndex(index, subs_)) {
        return &subs_[index];
    }
    return nullptr;
}

helics::Endpoint* helicsCoordinator::getEndpointPointer(int32_t index)
{
    if (isValidIndex(index, epts_)) {
        return &epts_[index];
    }
    return nullptr;
}

int32_t helicsCoordinator::addPublication(const std::string& pubName,
                                          helics::data_type type,
                                          units::unit unitType)
{
    PubInfo p;
    p.name = pubName;
    p.type = type;
    p.unitType = unitType;
    pubI.push_back(p);
    auto ind = static_cast<int32_t>(pubI.size()) - 1;
    pubMap_.emplace(pubName, ind);
    return ind;
}

int32_t helicsCoordinator::addSubscription(const std::string& subName, units::unit unitType)
{
    SubInfo s;
    s.name = subName;
    s.unitType = unitType;
    subI.push_back(s);
    auto ind = static_cast<int32_t>(subI.size()) - 1;
    subMap_.emplace(subName, ind);
    return ind;
}

void helicsCoordinator::updatePublication(int32_t index,
                                          const std::string& pubName,
                                          helics::data_type type,
                                          units::unit unitType)
{
    if (isValidIndex(index, pubI)) {
        if (!pubName.empty()) {
            pubI[index].name = pubName;
            pubMap_[pubName] = index;
        }
        if (type != helics::data_type::helics_any) {
            pubI[index].type = type;
        }
        if (unitType != units::defunit) {
            pubI[index].unitType = unitType;
        }
    }
}

void helicsCoordinator::updateSubscription(int32_t index,
                                           const std::string& subName,
                                           units::unit unitType)
{
    if (isValidIndex(index, subI)) {
        if (!subName.empty()) {
            subI[index].name = subName;
            subMap_[subName] = index;
        }
        if (unitType != units::defunit) {
            subI[index].unitType = unitType;
        }
    }
}

int32_t helicsCoordinator::addEndpoint(const std::string& eptName,
                                       const std::string& type,
                                       const std::string& target)
{
    EptInfo e;
    e.name = eptName;
    e.type = type;
    e.target = target;
    eptI.push_back(e);
    auto ind = static_cast<int32_t>(eptI.size()) - 1;
    eptMap_.emplace(eptName, ind);
    return ind;
}

void helicsCoordinator::updateEndpoint(int32_t index,
                                       const std::string& eptName,
                                       const std::string& type)
{
    if (isValidIndex(index, eptI)) {
        if (!eptName.empty()) {
            eptI[index].name = eptName;
            eptMap_[eptName] = index;
        }
        eptI[index].type = type;
    }
}

/** set the target destination for an endpoint
 */
void helicsCoordinator::setEndpointTarget(int32_t index, const std::string& target)
{
    if (isValidIndex(index, eptI)) {
        eptI[index].target = target;
    }
    if (isValidIndex(index, epts_)) {
        epts_[index].setDefaultDestination(target);
    }
}

void helicsCoordinator::addHelper(std::shared_ptr<helperObject> ho)
{
    std::lock_guard<std::mutex> hLock(helperProtector);
    helpers.push_back(std::move(ho));
}

void helicsCoordinator::addEvent(helicsEvent* evnt)
{
    events.push_back(evnt);
}
void helicsCoordinator::addCollector(helicsCollector* col)
{
    collectors.push_back(col);
}

int32_t helicsCoordinator::getSubscriptionIndex(const std::string& subName) const
{
    return mapFind(subMap_, subName, -1);
}

int32_t helicsCoordinator::getPublicationIndex(const std::string& pubName) const
{
    return mapFind(pubMap_, pubName, -1);
}

int32_t helicsCoordinator::getEndpointIndex(const std::string& eptName) const
{
    return mapFind(eptMap_, eptName, -1);
}

void helicsCoordinator::finalize()
{
    if (fed) {
        fed->finalize();
    }
}
}  // namespace griddyn::helicsLib
