/*
 * Copyright (c) 2014-2026, Lawrence Livermore National Security
 * See the top-level NOTICE for additional details. All rights reserved.
 * SPDX-License-Identifier: BSD-3-Clause
 */

#include "helicsCollector.h"

#include "core/coreObject.h"
#include "gmlc/utilities/stringOps.h"
#include "griddyn/measurement/gridGrabbers.h"
#include "helicsCoordinator.h"
#include "helicsLibrary.h"
#include "helicsSupport.h"
#include <memory>
#include <string>
#include <vector>

namespace griddyn::helicsLib {
HelicsCollector::HelicsCollector(coreTime time0, coreTime period): collector(time0, period) {}

HelicsCollector::HelicsCollector(const std::string& collectorName): collector(collectorName) {}

std::unique_ptr<collector> HelicsCollector::clone() const
{
    std::unique_ptr<collector> col = std::make_unique<HelicsCollector>();
    HelicsCollector::cloneTo(col.get());
    return col;
}

void HelicsCollector::cloneTo(collector* col) const
{
    collector::cloneTo(col);
    auto hcol = dynamic_cast<HelicsCollector*>(col);
    if (hcol == nullptr) {
        return;
    }
}

void HelicsCollector::dataPointAdded(const collectorPoint& cp)
{
    if (coordinator_ == nullptr) {
        // find the coordinator first
        auto gobj = cp.dataGrabber->getObject();
        if (gobj) {
            auto rto = gobj->getRoot();
            if (rto) {
                auto hCoord = rto->find("helics");
                if (dynamic_cast<HelicsCoordinator*>(hCoord)) {
                    coordinator_ = static_cast<HelicsCoordinator*>(hCoord);

                    coordinator_->addCollector(this);
                    switch (publicationType) {
                        case CollectorPubType::AS_VECTOR:
                            if (!publicationName.empty()) {
                                multiPublicationIndex = coordinator_->addPublication(
                                    publicationName, helics::data_type::helics_vector);
                            }
                            break;
                        case CollectorPubType::AS_STRING:
                            if (!publicationName.empty()) {
                                multiPublicationIndex = coordinator_->addPublication(
                                    publicationName, helics::data_type::helics_vector);
                            }
                            break;
                        default:
                            break;
                    }
                }
            }
        }
    }
    if (coordinator_ != nullptr) {
        if (cp.columnCount == 1) {
            if (publicationType == CollectorPubType::AS_INDIVIDUAL) {
                auto index = coordinator_->addPublication(cp.colname,
                                                          helics::data_type::helics_double,
                                                          cp.dataGrabber->outputUnits);
                publications.emplace_back(cp.colname, index, false);
            } else {
                publications.emplace_back(cp.colname, -1, false);
            }
        } else {
            // TODO(phlpt): Deal with output vectors later.
        }
    }
}

change_code HelicsCollector::trigger(coreTime time)
{
    auto out = collector::trigger(time);

    auto colNames = getColumnDescriptions();
    std::vector<bool> subscribe(colNames.size(), true);

    for (size_t ii = 0; ii < complexPairs.size(); ++ii) {
        auto& n1 = complexPairs[ii].first;
        auto& n2 = complexPairs[ii].second;
        int index1 = -1;
        int index2 = -1;
        for (int pp = 0; pp < static_cast<int>(colNames.size()); ++pp) {
            if (n1 == colNames[pp]) {
                index1 = pp;
            }
            if (n2 == colNames[pp]) {
                index2 = pp;
            }
        }
        if ((index1 >= 0) && (index2 >= 0)) {
            subscribe[index1] = false;
            subscribe[index2] = false;
        }
        // helicsSendComplex(complexNames[ii], data[index1], data[index2]);
    }

    switch (publicationType) {
        case CollectorPubType::AS_INDIVIDUAL:
            for (size_t ii = 0; ii < data.size(); ++ii) {
                if (subscribe[ii]) {
                    coordinator_->publish(publications[ii].publicationIndex, data[ii]);
                }
            }
            break;
        case CollectorPubType::AS_VECTOR:
        case CollectorPubType::AS_STRING:
            coordinator_->publish(multiPublicationIndex, data);
            break;
    }

    return out;
}

void HelicsCollector::set(std::string_view param, double val)
{
    collector::set(param, val);
}

void HelicsCollector::set(std::string_view param, std::string_view val)
{
    if (param == "complex") {
        auto asLoc = val.find("as");
        complexNames.push_back(gmlc::utilities::stringOps::trim(val.substr(asLoc + 2)));
        auto commaLoc = val.find_first_of(',');
        complexPairs.emplace_back(gmlc::utilities::stringOps::trim(val.substr(0, commaLoc)),
                                  gmlc::utilities::stringOps::trim(
                                      val.substr(commaLoc + 1, asLoc - 1 - commaLoc)));
        // helicsRegister::instance()->registerPublication(complexNames.back(),
        // helicsRegister::dataType::helicsComplex);
    } else if (param == "pubtype") {
        if (val == "vector") {
            publicationType = CollectorPubType::AS_VECTOR;
            if (multiPublicationIndex >= 0) {
                coordinator_->updatePublication(
                    multiPublicationIndex, publicationName, helics::data_type::helics_vector);
            }
        } else if (val == "string") {
            publicationType = CollectorPubType::AS_STRING;
            if (multiPublicationIndex >= 0) {
                coordinator_->updatePublication(
                    multiPublicationIndex, publicationName, helics::data_type::helics_string);
            }
        } else if (val == "individual") {
            publicationType = CollectorPubType::AS_INDIVIDUAL;
        } else {
            throw(invalidParameterValue(
                "pubtype must be one of \"vector\",\"string\",\"individual\""));
        }
    } else if (param == "pubname") {
        publicationName = val;
        if (multiPublicationIndex >= 0) {
            coordinator_->updatePublication(
                multiPublicationIndex, publicationName, helics::data_type::helics_any);
        } else if (coordinator_) {
            switch (publicationType) {
                case CollectorPubType::AS_VECTOR:
                    if (!publicationName.empty()) {
                        multiPublicationIndex = coordinator_->addPublication(
                            publicationName, helics::data_type::helics_vector);
                    }
                    break;
                case CollectorPubType::AS_STRING:
                    if (!publicationName.empty()) {
                        multiPublicationIndex = coordinator_->addPublication(
                            publicationName, helics::data_type::helics_string);
                    }
                    break;
                default:
                    break;
            }
        }
    } else {
        collector::set(param, val);
    }
}

const std::string& HelicsCollector::getSinkName() const
{
    static const std::string helicsName("helics");
    return helicsName;
}

}  // namespace griddyn::helicsLib
