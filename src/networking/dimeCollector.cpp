/*
 * LLNS Copyright Start
 * Copyright (c) 2017, Lawrence Livermore National Security
 * This work was performed under the auspices of the U.S. Department
 * of Energy by Lawrence Livermore National Laboratory in part under
 * Contract W-7405-Eng-48 and in part under Contract DE-AC52-07NA27344.
 * Produced at the Lawrence Livermore National Laboratory.
 * All rights reserved.
 * For details, see the LICENSE file.
 * LLNS Copyright End
 */

#include "dimeCollector.h"

#include "dimeClientInterface.h"
#include <memory>
#include <string>

namespace griddyn::dimeLib {
    dimeCollector::dimeCollector(coreTime time0, coreTime period): collector(time0, period) {}

    dimeCollector::dimeCollector(const std::string& collectorName): collector(collectorName) {}

    dimeCollector::~dimeCollector()
    {
        if (dime) {
            dime->close();
        }
    }
    std::unique_ptr<collector> dimeCollector::clone() const
    {
        std::unique_ptr<collector> col = std::make_unique<dimeCollector>();
        dimeCollector::cloneTo(col.get());
        return col;
    }

    void dimeCollector::cloneTo(collector* col) const
    {
        collector::cloneTo(col);
        auto dc = dynamic_cast<dimeCollector*>(col);
        if (dc == nullptr) {
            return;
        }
        dc->server = server;
        dc->processName = processName;
    }

    change_code dimeCollector::trigger(coreTime time)
    {
        if (!dime) {
            dime = std::make_unique<dimeClientInterface>(processName, server);
            dime->init();
        }
        auto out = collector::trigger(time);
        // figure out what to do with the data
        for (size_t kk = 0; kk < points.size(); ++kk) {
            dime->send_var(points[kk].colname, data[kk]);
        }

        return out;
    }

    void dimeCollector::set(const std::string& param, double val)
    {
        collector::set(param, val);
    }

    void dimeCollector::set(const std::string& param, const std::string& val)
    {
        if (param == "server") {
            server = val;
        } else if (param == "processname") {
            processName = val;
        } else {
            collector::set(param, val);
        }
    }

    const std::string& dimeCollector::getSinkName() const
    {
        return server;
    }

}  // namespace griddyn::dimeLib
