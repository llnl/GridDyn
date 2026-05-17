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
std::unordered_map<std::string, HelicsCoordinator*> HelicsCoordinator::registry;
std::mutex HelicsCoordinator::registryProtection;

void HelicsCoordinator::registerCoordinator(const std::string& coordinatorName,
                                            HelicsCoordinator* ptr)
{
    std::lock_guard<std::mutex> lock(registryProtection);
    registry[coordinatorName] = ptr;
}

void HelicsCoordinator::unregisterCoordinator(const std::string& coordinatorName)
{
    std::lock_guard<std::mutex> lock(registryProtection);
    auto fnd = registry.find(coordinatorName);
    if (fnd != registry.end()) {
        registry.erase(fnd);
    }
}

HelicsCoordinator* HelicsCoordinator::findCoordinator(const std::string& coordinatorName)
{
    std::lock_guard<std::mutex> lock(registryProtection);
    auto fnd = registry.find(coordinatorName);
    if (fnd != registry.end()) {
        return fnd->second;
    }
    return nullptr;
}

void HelicsCoordinator::loadCommandLine(int argc, char* argv[])
{
    info_.loadInfoFromArgs(argc, argv);
}

HelicsCoordinator::HelicsCoordinator(const std::string& fedName): CoreObject("helics")
{
    registerCoordinator(fedName, this);
}

std::shared_ptr<helics::Federate> HelicsCoordinator::registerAsFederate()
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
    pubs_.resize(publicationInfo_.size());
    for (auto& publicationInfo : publicationInfo_) {
        if (publicationInfo.unitType != units::defunit) {
            pubs_[ii] = helics::Publication(helics::GLOBAL,
                                            vFed_,
                                            publicationInfo.name,
                                            publicationInfo.type,
                                            to_string(publicationInfo.unitType));
        } else {
            pubs_[ii] = helics::Publication(helics::GLOBAL,
                                            vFed_,
                                            publicationInfo.name,
                                            publicationInfo.type);
        }
        ++ii;
    }
    ii = 0;
    subs_.resize(subscriptionInfo_.size());
    for (auto& subscriptionInfo : subscriptionInfo_) {
        if (subscriptionInfo.unitType != units::defunit) {
            subs_[ii] = vFed_->registerSubscription(subscriptionInfo.name,
                                                    to_string(subscriptionInfo.unitType));
        } else {
            subs_[ii] = vFed_->registerSubscription(subscriptionInfo.name);
        }
        subs_[ii].setOption(helics::defs::options::connection_optional);
        ++ii;
    }

    ii = 0;
    epts_.resize(endpointInfo_.size());
    for (auto& endpointInfo : endpointInfo_) {
        epts_[ii] = helics::Endpoint(mFed_, endpointInfo.name, endpointInfo.type);
        if (!endpointInfo.target.empty()) {
            epts_[ii].setDefaultDestination(endpointInfo.target);
        }
        ++ii;
    }
    // register a callback for handling messages from endpoints
    mFed_->setMessageNotificationCallback(
        [this](helics::Endpoint& endpoint, helics::Time messageTime) {
            this->receiveMessage(endpoint, messageTime);
        });

    fed->enterInitializingMode();
    logging::summary(this, "entered HELICS initializing Mode");
    for (auto evnt : events) {
        if (evnt->initNeeded()) {
            evnt->initialize();
        }
    }
    return fed;
}

void HelicsCoordinator::setFlag(const std::string& flag, bool val)
{
    auto flagprop = helics::getPropertyIndex(flag);
    if (flagprop != -1) {
        info_.setFlagOption(flagprop, val);
        if (fed) {
            fed->setFlagOption(flagprop, val);
        }
    } else {
        CoreObject::setFlag(flag, val);
    }
}
void HelicsCoordinator::set(const std::string& param, const std::string& val)
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
        CoreObject::set(param, val);
    }
}

void HelicsCoordinator::set(const std::string& param, double val, units::unit unitType)
{
    auto propVal = helics::getPropertyIndex(param);
    if (propVal >= 0) {
        info_.setProperty(propVal, val);
        if (fed) {
            fed->setProperty(propVal, val);
        }
    } else {
        CoreObject::set(param, val, unitType);
    }
}

void HelicsCoordinator::receiveMessage(helics::Endpoint& endpoint, helics::Time messageTime)
{
    auto payload = endpoint.getMessage()->to_string();
    std::shared_ptr<griddyn::commMessage> msg;
    msg->from_string(payload);

    auto event = std::make_unique<griddyn::functionEventAdapter>([this, msg, &endpoint]() {
        communicationsCore::instance()->send(0, endpoint.getName(), std::move(msg));
        return griddyn::change_code::no_change;
    });

    // convert helics::Time to griddynTime
    event->m_nextTime = messageTime;
    gridDynSimulation::getInstance()->add(std::shared_ptr<griddyn::eventAdapter>(std::move(event)));
}

void HelicsCoordinator::sendMessage(int32_t index, const char* data, count_t size)
{
    if (isValidIndex(index, epts_)) {
        epts_[index].send(data, size);
    }
}
void HelicsCoordinator::sendMessage(int32_t index,
                                    const std::string& dest,
                                    const char* data,
                                    count_t size)
{
    if (isValidIndex(index, epts_)) {
        epts_[index].send(dest, data, size);
    }
}

bool HelicsCoordinator::isUpdated(int32_t index)
{
    if (isValidIndex(index, subs_)) {
        return subs_[index].isUpdated();
    }
    return false;
}

bool HelicsCoordinator::hasMessage(int32_t index) const
{
    if (isValidIndex(index, epts_)) {
        return epts_[index].hasMessage();
    }
    return false;
}

helics::Publication* HelicsCoordinator::getPublicationPointer(int32_t index)
{
    if (isValidIndex(index, pubs_)) {
        return &pubs_[index];
    }
    return nullptr;
}

helics::Input* HelicsCoordinator::getInputPointer(int32_t index)
{
    if (isValidIndex(index, subs_)) {
        return &subs_[index];
    }
    return nullptr;
}

helics::Endpoint* HelicsCoordinator::getEndpointPointer(int32_t index)
{
    if (isValidIndex(index, epts_)) {
        return &epts_[index];
    }
    return nullptr;
}

int32_t HelicsCoordinator::addPublication(const std::string& pubName,
                                          helics::data_type type,
                                          units::unit unitType)
{
    PublicationInfo publicationInfo;
    publicationInfo.name = pubName;
    publicationInfo.type = type;
    publicationInfo.unitType = unitType;
    publicationInfo_.push_back(publicationInfo);
    auto ind = static_cast<int32_t>(publicationInfo_.size()) - 1;
    pubMap_.emplace(pubName, ind);
    return ind;
}

int32_t HelicsCoordinator::addSubscription(const std::string& subName, units::unit unitType)
{
    SubscriptionInfo subscriptionInfo;
    subscriptionInfo.name = subName;
    subscriptionInfo.unitType = unitType;
    subscriptionInfo_.push_back(subscriptionInfo);
    auto ind = static_cast<int32_t>(subscriptionInfo_.size()) - 1;
    subMap_.emplace(subName, ind);
    return ind;
}

void HelicsCoordinator::updatePublication(int32_t index,
                                          const std::string& pubName,
                                          helics::data_type type,
                                          units::unit unitType)
{
    if (isValidIndex(index, publicationInfo_)) {
        if (!pubName.empty()) {
            publicationInfo_[index].name = pubName;
            pubMap_[pubName] = index;
        }
        if (type != helics::data_type::helics_any) {
            publicationInfo_[index].type = type;
        }
        if (unitType != units::defunit) {
            publicationInfo_[index].unitType = unitType;
        }
    }
}

void HelicsCoordinator::updateSubscription(int32_t index,
                                           const std::string& subName,
                                           units::unit unitType)
{
    if (isValidIndex(index, subscriptionInfo_)) {
        if (!subName.empty()) {
            subscriptionInfo_[index].name = subName;
            subMap_[subName] = index;
        }
        if (unitType != units::defunit) {
            subscriptionInfo_[index].unitType = unitType;
        }
    }
}

int32_t HelicsCoordinator::addEndpoint(const std::string& eptName,
                                       const std::string& type,
                                       const std::string& target)
{
    EndpointInfo endpointInfo;
    endpointInfo.name = eptName;
    endpointInfo.type = type;
    endpointInfo.target = target;
    endpointInfo_.push_back(endpointInfo);
    auto ind = static_cast<int32_t>(endpointInfo_.size()) - 1;
    eptMap_.emplace(eptName, ind);
    return ind;
}

void HelicsCoordinator::updateEndpoint(int32_t index,
                                       const std::string& eptName,
                                       const std::string& type)
{
    if (isValidIndex(index, endpointInfo_)) {
        if (!eptName.empty()) {
            endpointInfo_[index].name = eptName;
            eptMap_[eptName] = index;
        }
        endpointInfo_[index].type = type;
    }
}

/** set the target destination for an endpoint
 */
void HelicsCoordinator::setEndpointTarget(int32_t index, const std::string& target)
{
    if (isValidIndex(index, endpointInfo_)) {
        endpointInfo_[index].target = target;
    }
    if (isValidIndex(index, epts_)) {
        epts_[index].setDefaultDestination(target);
    }
}

void HelicsCoordinator::addHelper(std::shared_ptr<helperObject> helper)
{
    std::lock_guard<std::mutex> hLock(helperProtector);
    helpers.push_back(std::move(helper));
}

void HelicsCoordinator::addEvent(HelicsEvent* eventObject)
{
    events.push_back(eventObject);
}
void HelicsCoordinator::addCollector(HelicsCollector* collectorObject)
{
    collectors.push_back(collectorObject);
}

int32_t HelicsCoordinator::getSubscriptionIndex(const std::string& subName) const
{
    return mapFind(subMap_, subName, -1);
}

int32_t HelicsCoordinator::getPublicationIndex(const std::string& pubName) const
{
    return mapFind(pubMap_, pubName, -1);
}

int32_t HelicsCoordinator::getEndpointIndex(const std::string& eptName) const
{
    return mapFind(eptMap_, eptName, -1);
}

void HelicsCoordinator::finalize()
{
    if (fed) {
        fed->finalize();
    }
}
}  // namespace griddyn::helicsLib
