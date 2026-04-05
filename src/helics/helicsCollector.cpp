/*
 * LLNS Copyright Start
 * Copyright (c) 2014-2018, Lawrence Livermore National Security
 * This work was performed under the auspices of the U.S. Department
 * of Energy by Lawrence Livermore National Laboratory in part under
 * Contract W-7405-Eng-48 and in part under Contract DE-AC52-07NA27344.
 * Produced at the Lawrence Livermore National Laboratory.
 * All rights reserved.
 * For details, see the LICENSE file.
 * LLNS Copyright End
 */

#include "helicsCollector.h"

#include "core/coreObject.h"
#include "gmlc/utilities/stringOps.h"
#include "griddyn/measurement/gridGrabbers.h"
#include "helicsCoordinator.h"
#include "helicsLibrary.h"
#include "helicsSupport.h"

namespace griddyn {
namespace helicsLib {
    helicsCollector::helicsCollector(coreTime time0, coreTime period): collector(time0, period) {}

    helicsCollector::helicsCollector(const std::string& collectorName): collector(collectorName) {}

    std::unique_ptr<collector> helicsCollector::clone() const
    {
        std::unique_ptr<collector> col = std::make_unique<helicsCollector>();
        helicsCollector::cloneTo(col.get());
        return col;
    }

    void helicsCollector::cloneTo(collector* col) const
    {
        collector::cloneTo(col);
        auto hcol = dynamic_cast<helicsCollector*>(col);
        if (hcol == nullptr) {
            return;
        }
    }

    void helicsCollector::dataPointAdded(const collectorPoint& cp)
    {
        if (coord == nullptr) {
            // find the coordinator first
            auto gobj = cp.dataGrabber->getObject();
            if (gobj) {
                auto rto = gobj->getRoot();
                if (rto) {
                    auto hCoord = rto->find("helics");
                    if (dynamic_cast<helicsCoordinator*>(hCoord)) {
                        coord = static_cast<helicsCoordinator*>(hCoord);

                        coord->addCollector(this);
                        switch (pubType) {
                            case collectorPubType::as_vector:
                                if (!pubName.empty()) {
                                    mpubIndex =
                                        coord->addPublication(pubName,
                                                              helics::data_type::helics_vector);
                                }
                                break;
                            case collectorPubType::as_string:
                                if (!pubName.empty()) {
                                    mpubIndex =
                                        coord->addPublication(pubName,
                                                              helics::data_type::helics_vector);
                                }
                                break;
                            default:
                                break;
                        }
                    }
                }
            }
        }
        if (coord != nullptr) {
            if (cp.columnCount == 1) {
                if (pubType == collectorPubType::as_individual) {
                    auto index = coord->addPublication(cp.colname,
                                                       helics::data_type::helics_double,
                                                       cp.dataGrabber->outputUnits);
                    pubs.emplace_back(cp.colname, index, false);
                } else {
                    pubs.emplace_back(cp.colname, -1, false);
                }
            } else {
                // TODO:: deal with output vectors later
            }
        }
    }

    change_code helicsCollector::trigger(coreTime time)
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
            // helicsSendComplex(cnames[ii], data[index1], data[index2]);
        }

        switch (pubType) {
            case collectorPubType::as_individual:
                for (size_t ii = 0; ii < data.size(); ++ii) {
                    if (subscribe[ii]) {
                        coord->publish(pubs[ii].pubIndex, data[ii]);
                    }
                }
                break;
            case collectorPubType::as_vector:
            case collectorPubType::as_string:
                coord->publish(mpubIndex, data);
                break;
        }

        return out;
    }

    void helicsCollector::set(const std::string& param, double val)
    {
        collector::set(param, val);
    }

    void helicsCollector::set(const std::string& param, const std::string& val)
    {
        using namespace gmlc::utilities::stringOps;
        if (param == "complex") {
            auto asLoc = val.find("as");
            cnames.push_back(trim(val.substr(asLoc + 2)));
            auto commaLoc = val.find_first_of(',');
            complexPairs.emplace_back(trim(val.substr(0, commaLoc)),
                                      trim(val.substr(commaLoc + 1, asLoc - 1 - commaLoc)));
            // helicsRegister::instance()->registerPublication(cnames.back(),
            // helicsRegister::dataType::helicsComplex);
        } else if (param == "pubtype") {
            if (val == "vector") {
                pubType = collectorPubType::as_vector;
                if (mpubIndex >= 0) {
                    coord->updatePublication(mpubIndex, pubName, helics::data_type::helics_vector);
                }
            } else if (val == "string") {
                pubType = collectorPubType::as_string;
                if (mpubIndex >= 0) {
                    coord->updatePublication(mpubIndex, pubName, helics::data_type::helics_string);
                }
            } else if (val == "individual") {
                pubType = collectorPubType::as_individual;
            } else {
                throw(invalidParameterValue(
                    "pubtype must be one of \"vector\",\"string\",\"individual\""));
            }
        } else if (param == "pubname") {
            pubName = val;
            if (mpubIndex >= 0) {
                coord->updatePublication(mpubIndex, pubName, helics::data_type::helics_any);
            } else if (coord) {
                switch (pubType) {
                    case collectorPubType::as_vector:
                        if (!pubName.empty()) {
                            mpubIndex =
                                coord->addPublication(pubName, helics::data_type::helics_vector);
                        }
                        break;
                    case collectorPubType::as_string:
                        if (!pubName.empty()) {
                            mpubIndex =
                                coord->addPublication(pubName, helics::data_type::helics_string);
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

    const std::string helicsName("helics");

    const std::string& helicsCollector::getSinkName() const
    {
        return helicsName;
    }

}  // namespace helicsLib
}  // namespace griddyn
