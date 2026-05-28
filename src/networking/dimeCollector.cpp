/*
 * Copyright (c) 2014-2026, Lawrence Livermore National Security
 * See the top-level NOTICE for additional details. All rights reserved.
 * SPDX-License-Identifier: BSD-3-Clause
 */

#include "dimeCollector.h"

#include "dimeClientInterface.h"
#include <memory>
#include <string>

namespace griddyn::dimeLib {
DimeCollector::DimeCollector(coreTime time0, coreTime period): Collector(time0, period) {}

DimeCollector::DimeCollector(const std::string& collectorName): Collector(collectorName) {}

DimeCollector::~DimeCollector()
{
    if (dime) {
        dime->close();
    }
}
std::unique_ptr<Collector> DimeCollector::clone() const
{
    std::unique_ptr<Collector> col = std::make_unique<DimeCollector>();
    DimeCollector::cloneTo(col.get());
    return col;
}

void DimeCollector::cloneTo(Collector* col) const
{
    Collector::cloneTo(col);
    auto* dimeCollectorClone = dynamic_cast<DimeCollector*>(col);
    if (dimeCollectorClone == nullptr) {
        return;
    }
    dimeCollectorClone->server = server;
    dimeCollectorClone->processName = processName;
}

ChangeCode DimeCollector::trigger(coreTime time)
{
    if (!dime) {
        dime = std::make_unique<DimeClientInterface>(processName, server);
        dime->init();
    }
    auto out = Collector::trigger(time);
    // figure out what to do with the data
    for (size_t kk = 0; kk < mPoints.size(); ++kk) {
        dime->sendVar(mPoints[kk].mColumnName, mData[kk]);
    }

    return out;
}

void DimeCollector::set(std::string_view param, double val)
{
    Collector::set(param, val);
}

void DimeCollector::set(std::string_view param, std::string_view val)
{
    if (param == "server") {
        server = val;
    } else if (param == "processname") {
        processName = val;
    } else {
        Collector::set(param, val);
    }
}

const std::string& DimeCollector::getSinkName() const
{
    return server;
}

}  // namespace griddyn::dimeLib
