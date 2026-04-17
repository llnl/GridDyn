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
    triggerTime = maxTime;
    timePeriod = maxTime;
}

std::unique_ptr<collector> fmiCollector::clone() const
{
    std::unique_ptr<collector> fmicol = std::make_unique<fmiCollector>();
    fmiCollector::cloneTo(fmicol.get());
    return fmicol;
}

void fmiCollector::cloneTo(collector* gr) const
{
    collector::cloneTo(gr);

    auto nrec = dynamic_cast<fmiCollector*>(gr);
    if (nrec == nullptr) {
        return;
    }
}

change_code fmiCollector::trigger(coreTime time)
{
    collector::trigger(time);
    return change_code::no_change;
}

void fmiCollector::set(const std::string& param, double val)
{
    if (param.empty()) {
    } else {
        collector::set(param, val);
    }
}
void fmiCollector::set(const std::string& param, const std::string& val)
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
    if (coord != nullptr) {
        return coord->getFMIName();
    }
    return defaultFMIName;
}

coreObject* fmiCollector::getOwner() const
{
    return coord;
}

void fmiCollector::dataPointAdded(const collectorPoint& cp)
{
    if (coord == nullptr) {
        // find the coordinator first
        auto gobj = cp.dataGrabber->getObject();
        if (gobj != nullptr) {
            auto rto = gobj->getRoot();
            if (rto != nullptr) {
                auto fmiCont = rto->find("fmiCoordinator");
                if (dynamic_cast<fmiCoordinator*>(fmiCont) != nullptr) {
                    coord = static_cast<fmiCoordinator*>(fmiCont);
                }
            }
        }
    }
    if (coord != nullptr) {
        if (cp.columnCount == 1) {
            coord->registerOutput(cp.colname, cp.column, this);
        } else {
            // TODO(phlpt): Deal with output vectors later.
        }
    }
}

}  // namespace griddyn::fmi
