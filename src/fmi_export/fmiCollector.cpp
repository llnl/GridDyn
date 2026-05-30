/*
 * Copyright (c) 2014-2026, Lawrence Livermore National Security
 * See the top-level NOTICE for additional details. All rights reserved.
 * SPDX-License-Identifier: BSD-3-Clause
 */

#include "fmiCollector.h"

#include "fmiCoordinator.h"
#include "griddyn/measurement/GridGrabbers.h"
#include <memory>
#include <string>

namespace griddyn::fmi {
FmiCollector::FmiCollector(): Collector(maxTime, maxTime) {}
FmiCollector::FmiCollector(const std::string& name): Collector(name)
{
    mTriggerTime = maxTime;
    mTimePeriod = maxTime;
}

std::unique_ptr<Collector> FmiCollector::clone() const
{
    std::unique_ptr<Collector> fmicol = std::make_unique<FmiCollector>();
    FmiCollector::cloneTo(fmicol.get());
    return fmicol;
}

void FmiCollector::cloneTo(Collector* collectorClone) const
{
    Collector::cloneTo(collectorClone);

    auto* newCollector = dynamic_cast<FmiCollector*>(collectorClone);
    if (newCollector == nullptr) {
        return;
    }
}

ChangeCode FmiCollector::trigger(coreTime time)
{
    Collector::trigger(time);
    return ChangeCode::NO_CHANGE;
}

void FmiCollector::set(std::string_view param, double val)
{
    if (param.empty()) {
    } else {
        Collector::set(param, val);
    }
}
void FmiCollector::set(std::string_view param, std::string_view val)
{
    if (param.empty()) {
    } else {
        Collector::set(param, val);
    }
}

const std::string& FmiCollector::getSinkName() const
{
    static const std::string defaultFMIName{"fmi"};
    if (mCoordinator != nullptr) {
        return mCoordinator->getFmiName();
    }
    return defaultFMIName;
}

CoreObject* FmiCollector::getOwner() const
{
    return mCoordinator;
}

void FmiCollector::dataPointAdded(const CollectorPoint& collectorDataPoint)
{
    if (mCoordinator == nullptr) {
        // find the coordinator first
        auto* gridObject = collectorDataPoint.mDataGrabber->getObject();
        if (gridObject != nullptr) {
            auto* rootObject = gridObject->getRoot();
            if (rootObject != nullptr) {
                auto* fmiCoordinatorObject = rootObject->find("fmiCoordinator");
                if (dynamic_cast<FmiCoordinator*>(fmiCoordinatorObject) != nullptr) {
                    mCoordinator = static_cast<FmiCoordinator*>(fmiCoordinatorObject);
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
