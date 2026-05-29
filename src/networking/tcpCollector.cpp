/*
 * Copyright (c) 2014-2026, Lawrence Livermore National Security
 * See the top-level NOTICE for additional details. All rights reserved.
 * SPDX-License-Identifier: BSD-3-Clause
 */

#include "tcpCollector.h"

#include "gmlc/networking/AsioContextManager.h"
#include <memory>
#include <string>

namespace griddyn::tcpLib {
TcpCollector::TcpCollector(coreTime time0, coreTime period): Collector(time0, period) {}

TcpCollector::TcpCollector(const std::string& collectorName): Collector(collectorName) {}

TcpCollector::~TcpCollector()
{
    if (connection) {
        connection->close();
    }
}
std::unique_ptr<Collector> TcpCollector::clone() const
{
    std::unique_ptr<Collector> col = std::make_unique<TcpCollector>();
    TcpCollector::cloneTo(col.get());
    return col;
}

void TcpCollector::cloneTo(Collector* col) const
{
    Collector::cloneTo(col);
    auto* tcpCollectorClone = dynamic_cast<TcpCollector*>(col);
    if (tcpCollectorClone == nullptr) {
        return;
    }
    tcpCollectorClone->server = server;
    tcpCollectorClone->port = port;
}

ChangeCode TcpCollector::trigger(coreTime time)
{
    if (!connection) {
        connection = gmlc::networking::TcpConnection::create(
            gmlc::networking::AsioContextManager::getContext(), server, port);
    }
    auto out = Collector::trigger(time);
    // figure out what to do with the data
    for (size_t kk = 0; kk < mPoints.size(); ++kk) {
        // connection->sendVar(mPoints[kk].mColumnName, mData[kk]);
    }

    return out;
}

void TcpCollector::set(std::string_view param, double val)
{
    if (param == "port") {
        port = std::to_string(val);
    } else {
        Collector::set(param, val);
    }
}

void TcpCollector::set(std::string_view param, std::string_view val)
{
    if (param == "server") {
        server = val;
    } else if (param == "port") {
        port = val;
    } else {
        Collector::set(param, val);
    }
}

const std::string& TcpCollector::getSinkName() const
{
    return server;
}

}  // namespace griddyn::tcpLib
