/*
 * Copyright (c) 2014-2026, Lawrence Livermore National Security
 * See the top-level NOTICE for additional details. All rights reserved.
 * SPDX-License-Identifier: BSD-3-Clause
 */

#include "fmiCollector.h"

#include "fmiCoordinator.h"
#include "griddyn/measurement/gridGrabbers.h"
#include <memory>
#include <string>

namespace griddyn::fmi {
fmiCollector::fmiCollector(): collector(maxTime, maxTime) {}
fmiCollector::fmiCollector(const std::string& name): collector(name)
{
    mTriggerTime = maxTime;
    mTimePeriod = maxTime;
}

std::unique_ptr<collector> fmiCollector::clone() const
{
    std::unique_ptr<collector> fmicol = std::make_unique<fmiCollector>();
    fmiCollector::cloneTo(fmicol.get());
    return fmicol;
}

void fmiCollector::cloneTo(collector* collectorClone) const
{
    collector::cloneTo(collectorClone);

    auto* newCollector = dynamic_cast<fmiCollector*>(collectorClone);
    if (newCollector == nullptr) {
        return;
    }
}

change_code fmiCollector::trigger(coreTime time)
{
    collector::trigger(time);
    return change_code::no_change;
}

void fmiCollector::set(std::string_view param, double val)
{
    if (param.empty()) {
    } else {
        collector::set(param, val);
    }
}
void fmiCollector::set(std::string_view param, std::string_view val)
{
    if (param.empty()) {
    } else {
        collector::set(param, val);
    }
}

static const char defFMIName[] = "fmi";
const std::string& fmiCollector::getSinkName() const
{
    static const std::string defaultFMIName{defFMIName};
    if (mCoordinator != nullptr) {
        return mCoordinator->getFMIName();
    }
    return defaultFMIName;
}

coreObject* fmiCollector::getOwner() const
{
    return mCoordinator;
}

void fmiCollector::dataPointAdded(const collectorPoint& collectorDataPoint)
{
    if (mCoordinator == nullptr) {
        // find the coordinator first
        auto* gridObject = collectorDataPoint.mDataGrabber->getObject();
        if (gridObject != nullptr) {
            auto* rootObject = gridObject->getRoot();
            if (rootObject != nullptr) {
                auto* fmiCoordinatorObject = rootObject->find("fmiCoordinator");
                if (dynamic_cast<fmiCoordinator*>(fmiCoordinatorObject) != nullptr) {
                    mCoordinator = static_cast<fmiCoordinator*>(fmiCoordinatorObject);
                }
            }
        }
    }
    if (mCoordinator != nullptr) {
        if (collectorDataPoint.mColumnCount == 1) {
            mCoordinator->registerOutput(collectorDataPoint.mColumnName,
                                         collectorDataPoint.mColumn,
                                         this);
        } else {
            // TODO(phlpt): Deal with output vectors later.
        }
    }
}

}  // namespace griddyn::fmi
