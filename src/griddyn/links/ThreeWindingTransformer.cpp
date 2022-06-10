/*
 * Copyright (c) 2014-2022, Lawrence Livermore National Security
 * See the top-level NOTICE for additional details. All rights reserved.
 * SPDX-License-Identifier: BSD-3-Clause
 */

#include "../primary/acBus.h"
#include "core/coreExceptions.h"
#include "core/coreObjectTemplates.hpp"
#include "ThreeWindingTransformer.h"
#include <cmath>

namespace griddyn {
namespace links {
    ThreeWindingTransformer::ThreeWindingTransformer(const std::string& objName): subsystem(objName)
    {
        gridBus* bus = new acBus("ibus_mid");
        subsystem::add(bus);

        acLine* t1 = new acLine("primary");
        acLine* t2 = new acLine("secondary");
        acLine* t3 = new acLine("tertiary");
        subsystem::add(t1);
        subsystem::add(t2);
        subsystem::add(t3);
        t1->updateBus(bus, 2);
        t2->updateBus(bus, 1);
        t3->updateBus(bus, 1);
        m_terminals = 3;
        terminalLink.resize(3);
        terminalLink[0] = t1;
        terminalLink[1] = t2;
        terminalLink[2] = t3;
        cterm.resize(3);
        cterm[0] = 1;
        cterm[1] = 2;
        cterm[2] = 2;

    }
    coreObject* ThreeWindingTransformer::clone(coreObject* obj) const
    {
        auto line = cloneBase<ThreeWindingTransformer, Link>(this, obj);
        if (line == nullptr) {
            return obj;
        }
        
        return line;
    }
    // add components
    void ThreeWindingTransformer::add(coreObject* /*obj*/) { throw(unrecognizedObjectException(this)); }
    // remove components
    void ThreeWindingTransformer::remove(coreObject* /*obj*/) {}
   

    void ThreeWindingTransformer::set(const std::string& param, const std::string& val)
    {
        if (param == "primary") {
            subsystem::set("from", val);
        } else if (param == "secondary") {
            subsystem::set("to", val);
        } else if (param == "tertiary") {
            subsystem::set("connection:3", val);
        } else {
            return subsystem::set(param, val);
        }
    }

    void ThreeWindingTransformer::set(const std::string& param, double val, units::unit unitType)
    {
        if (param.length() == 1) {
            switch (param[0]) {
                case 'r':
                    r = val;
                    break;
                case 'x':
                    x = val;
                    break;
                case 'b':
                    mp_B = val;
                    break;
                case 'g':
                    mp_G = val;
                    break;

                default:
                    throw(unrecognizedParameter(param));
            }
            return;
        }

        if ((param == "segmentationlength") || (param == "segmentlength")) {
            segmentationLength = convert(val, unitType, units::km);
        } else if (param == "length") {
            length = convert(val, unitType, units::km);
        } else if (param == "fault") {
            if (opFlags[pFlow_initialized]) {
                fault = val;
                if (fault > 1.0) {
                    fault = -1;
                }
                if (fault >= 0) {
                    auto nb = getInt("linkcount");
                    double fs = 1.0 / static_cast<double>(nb);
                    int kk = 0;
                    double cm = fs;
                    while (cm < fault) {
                        ++kk;
                        cm += fs;
                    }
                    double newfaultVal = fault - cm + fs;

                    if (faultLink >= 0)  // if there is an existing fault move it
                    {
                        getLink(faultLink)->set("fault", -1.0);
                    }
                    getLink(kk)->set("fault", newfaultVal);
                    faultLink = kk;
                } else {
                    if (faultLink >= 0) {
                        getLink(faultLink)->set("fault", -1.0);
                        faultLink = -1;
                    }
                }
            } else {
                fault = val;
            }
        } else {
            Link::set(param, val, unitType);  // bypass subsystem set function
        }
    }

    double ThreeWindingTransformer::get(const std::string& param, units::unit unitType) const
    {
        double val = kNullVal;
        if (param == "NULL") {
            val = kNullVal;
        } else {
            val = subsystem::get(param, unitType);
        }
        return val;
    }

}  // namespace links
}  // namespace griddyn
