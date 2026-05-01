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
tcpCollector::tcpCollector(coreTime time0, coreTime period): collector(time0, period) {}

tcpCollector::tcpCollector(const std::string& collectorName): collector(collectorName) {}

tcpCollector::~tcpCollector()
{
    if (connection) {
        connection->close();
    }
}
std::unique_ptr<collector> tcpCollector::clone() const
{
    std::unique_ptr<collector> col = std::make_unique<tcpCollector>();
    tcpCollector::cloneTo(col.get());
    return col;
}

void tcpCollector::cloneTo(collector* col) const
{
    collector::cloneTo(col);
    auto dc = dynamic_cast<tcpCollector*>(col);
    if (dc == nullptr) {
        return;
    }
    dc->server = server;
    dc->port = port;
}

change_code tcpCollector::trigger(coreTime time)
{
    if (!connection) {
        connection = gmlc::networking::TcpConnection::create(
            gmlc::networking::AsioContextManager::getContext(), server, port);
    }
    auto out = collector::trigger(time);
    // figure out what to do with the data
    for (size_t kk = 0; kk < points.size(); ++kk) {
        // connection->send_var(points[kk].colname, data[kk]);
    }

    return out;
}

void tcpCollector::set(std::string_view param, double val)
{
    if (param == "port") {
        port = std::to_string(val);
    } else {
        collector::set(param, val);
    }
}

void tcpCollector::set(std::string_view param, std::string_view val)
{
    if (param == "server") {
        server = val;
    } else if (param == "port") {
        port = val;
    } else {
        collector::set(param, val);
    }
}

const std::string& tcpCollector::getSinkName() const
{
    return server;
}

}  // namespace griddyn::tcpLib
