/*
 * Copyright (c) 2014-2026, Lawrence Livermore National Security
 * See the top-level NOTICE for additional details. All rights reserved.
 * SPDX-License-Identifier: BSD-3-Clause
 */

#pragma once

#include "../core/coreExceptions.h"
#include "../core/coreObject.h"
#include "gmlc/utilities/stringConversion.h"
#include "helics/helics.hpp"
#include "helicsSupport.h"
#include <complex>
#include <deque>
#include <memory>
#include <mutex>
#include <string>
#include <type_traits>
#include <unordered_map>
#include <vector>

namespace griddyn::helicsLib {
/** simple structure containing the publication information*/
class PublicationInfo {
  public:
    helics::data_type type;
    units::unit unitType = units::defunit;
    std::string name;
};

/** simple structure containing the subscription information*/
class SubscriptionInfo {
  public:
    bool isValid = false;
    units::unit unitType = units::defunit;
    std::string name;
    helics::defV defaults;
};

/** simple structure containing the subscription information*/
class EndpointInfo {
  public:
    std::string name;
    std::string type;
    std::string target;
};

class HelicsCollector;
class HelicsEvent;

/** class to manage the linkages from HELICS to the GridDyn objects*/
class HelicsCoordinator: public CoreObject {
  private:
    static std::unordered_map<std::string, HelicsCoordinator*> registry;
    static std::mutex registryProtection;

  public:
    /** register a coordinator in the registry*/
    static void registerCoordinator(const std::string& coordinatorName, HelicsCoordinator* ptr);
    /** remove a coordinator from the registry*/
    static void unregisterCoordinator(const std::string& coordinatorName);
    /** find a coordinator in the registry*/
    static HelicsCoordinator* findCoordinator(const std::string& coordinatorName);

  private:
    std::deque<helics::Publication> pubs_;  //!< list of all the publication
    std::deque<helics::Input> subs_;  //!< container for all the subscription information
    std::deque<helics::Endpoint> epts_;  //!< container for all the endpoints
    std::vector<SubscriptionInfo> subscriptionInfo_;
    std::vector<PublicationInfo> publicationInfo_;
    std::vector<EndpointInfo> endpointInfo_;
    helics::FederateInfo info_;  //!< container with the federate information
    std::string connectionInfo;  //!< string with the connection info
    std::shared_ptr<helics::Federate> fed;
    std::vector<HelicsCollector*> collectors;  //!< storage for the collectors
    std::vector<HelicsEvent*> events;  //!< storage for the events
    helics::ValueFederate* vFed_;  //!< pointer to the federate
    helics::MessageFederate* mFed_;  //!< pointer to a message Federate
    std::mutex helperProtector;  //!< mutex lock to accept incoming helpers in a parallel system
    std::vector<std::shared_ptr<HelperObject>> helpers;  //!< storage to keep helper objects active
    std::unordered_map<std::string, int32_t> subMap_;  //!< map of all the subscription names
    std::unordered_map<std::string, int32_t> pubMap_;  //!< map of all the publication names
    std::unordered_map<std::string, int32_t> eptMap_;  //!< map of all the endpoints
  public:
    explicit HelicsCoordinator(const std::string& fedName = std::string{});

    /** load the info structure from command line arguments*/
    void loadCommandLine(int argc, char* argv[]);
    /** register the information as part of a federate in HELICS
@return a shared pointer to the federate object itself
*/
    std::shared_ptr<helics::Federate> registerAsFederate();
    /** get a pointer to the federate
@details will return an empty point if registerAsFederate hasn't been called yet*/
    std::shared_ptr<helics::Federate> getFederate() { return fed; }
    /** set the value of a publication*/
    template<class ValueType>
    void publish(int32_t index, const ValueType& val)
    {
        if (isValidIndex(index, pubs_)) {
            auto& pub = pubs_[index];
            pub.publish(val);
        } else {
            throw(invalidParameterValue());
        }
    }

    template<class ValueType>
    ValueType getValueAs(int32_t index)
    {
        if (isValidIndex(index, subs_)) {
            auto& sub = subs_[index];
            return sub.getValue<ValueType>();
        }
        throw(invalidParameterValue());
    }

    void receiveMessage(helics::Endpoint& endpoint,
                        helics::Time messageTime); /* catch-all callback for HELICS messages */
    void sendMessage(int32_t index, const char* data, count_t size);
    void sendMessage(int32_t index, const std::string& dest, const char* data, count_t size);
    void addHelper(std::shared_ptr<HelperObject> helper) override;
    void addEvent(HelicsEvent* eventObject);
    void addCollector(HelicsCollector* collectorObject);
    void set(const std::string& param, const std::string& val) override;
    void set(const std::string& param, double val, units::unit unitType = units::defunit) override;
    void setFlag(const std::string& flag, bool val) override;
    /** add a publication to the helics federate
@param[in] pubName the name of the value to publish
@param[in] type  the type of value one of helicsValueType
@param[in] unitType the units of the publication
@return an identifier value for the publication
*/
    int32_t addPublication(const std::string& pubName,
                           helics::data_type type,
                           units::unit unitType = units::defunit);
    /** update a publication
@param[in] index the identifier for the publication
@param[in] pubName the name of the value to publish
@param[in] type  the type of value one of helicsValueType
@param[in] unitType the units of the publication
*/
    void updatePublication(int32_t index,
                           const std::string& pubName,
                           helics::data_type type,
                           units::unit unitType = units::defunit);
    /** add a subscription to the helics federate
@param[in] pubName the name of the value to subscribe to
@param[in] unitType the units of the publication
@return an identifier value for the publication
*/
    int32_t addSubscription(const std::string& pubName, units::unit unitType = units::defunit);
    /** update a subscription
@param[in] index the identifier for the subscription
@param[in] pubName the name of the value to subscribe to
@param[in] unitType the units of the publication
*/
    void updateSubscription(int32_t index,
                            const std::string& subName,
                            units::unit unitType = units::defunit);

    /** add an endpoint to the helics federate
@param[in] eptName the name of the endpoint
@param[in] type the type of the endpoint(empty string is acceptable)
@param[in] target the default destination for the endpoint
@return an identifier value for endpoint
*/
    int32_t addEndpoint(const std::string& eptName,
                        const std::string& type = std::string(),
                        const std::string& target = std::string());
    /** update an endpoint
@param[in] eptName the name of the endpoint
@param[in] type the type of the endpoint(empty string is acceptable)
@return an identifier value for endpoint
*/
    void updateEndpoint(int32_t index,
                        const std::string& eptName,
                        const std::string& type = std::string());
    /** set the target destination for an endpoint
     */
    void setEndpointTarget(int32_t index, const std::string& target);

    template<class ValueType>
    void setDefault(int32_t index, const ValueType& val)
    {
        if (isValidIndex(index, subs_)) {
            auto& sub = subs_[index];
            sub.setDefault(val);
        } else if (isValidIndex(index, subscriptionInfo_)) {
            auto& sub = subscriptionInfo_[index];
            sub.defaults = val;
        } else {
            throw(invalidParameterValue());
        }
    }

  public:
    /** lookup an identifier for a previously declared subscription
@return the subscription index or -1 if not existent*/
    int32_t getSubscriptionIndex(const std::string& name) const;

    /** lookup an identifier for a previously declared publication
@return the publication index or -1 if not existent*/
    int32_t getPublicationIndex(const std::string& name) const;
    /** lookup an identifier for a previously declared endpoint
@return the endpoint index or -1 if not existent*/
    int32_t getEndpointIndex(const std::string& eptName) const;
    /** finalize the federate and disconnect from Helics*/
    void finalize();

    /** check whether a value has been updated*/
    bool isUpdated(int32_t index);
    /** check whether an endpoint has a message*/
    bool hasMessage(int32_t index) const;

    helics::Publication* getPublicationPointer(int32_t index);
    helics::Input* getInputPointer(int32_t index);
    helics::Endpoint* getEndpointPointer(int32_t index);
};

}  // namespace griddyn::helicsLib
