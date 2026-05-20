/*
 * Copyright (c) 2014-2026, Lawrence Livermore National Security
 * See the top-level NOTICE for additional details. All rights reserved.
 * SPDX-License-Identifier: BSD-3-Clause
 */

#include "LongLine.h"

#include "../primary/AcBus.h"
#include "core/CoreExceptions.h"
#include "core/coreObjectTemplates.hpp"
#include <cmath>
#include <string>

namespace griddyn::links {
LongLine::LongLine(const std::string& objName): subsystem(objName) {}
CoreObject* LongLine::clone(CoreObject* obj) const
{
    auto line = cloneBase<LongLine, Link>(this, obj);
    if (line == nullptr) {
        return obj;
    }
    line->segmentationLength = segmentationLength;
    if (opFlags[pFlow_initialized]) {
        subsystem::clone(line);
    }
    return line;
}
// add components
void LongLine::add(CoreObject* /*obj*/)
{
    throw(unrecognizedObjectException(this));
}
// remove components
void LongLine::remove(CoreObject* /*obj*/) {}
void LongLine::pFlowObjectInitializeA(coreTime time0, std::uint32_t flags)
{
    generateIntermediateLinks();
    return subsystem::pFlowObjectInitializeA(time0, flags);
}

void LongLine::set(std::string_view param, std::string_view val)
{
    return Link::set(param, val);
}
void LongLine::set(std::string_view param, double val, units::unit unitType)
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

double LongLine::get(std::string_view param, units::unit unitType) const
{
    double val = kNullVal;
    if (param == "segmentationlength") {
        val = segmentationLength;
    } else {
        val = subsystem::get(param, unitType);
    }
    return val;
}

void LongLine::generateIntermediateLinks()
{
    int numLinks = static_cast<int>(std::ceil(length / segmentationLength));

    double sr = r / static_cast<double>(numLinks);
    double sx = x / static_cast<double>(numLinks);
    double sB = mp_B / static_cast<double>(numLinks);
    double sG = mp_G / static_cast<double>(numLinks);

    int clinks = getInt("linkCount");
    Link* link;

    if (clinks == 0) {
        link = new AcLine(sr, sx);
        link->set("b", sB);
        if (sG != 0) {
            link->set("g", sG);
        }
        subsystem::add(link);
        clinks = 1;
        terminalLink[0] = link;
    } else {
        for (int pp = 0; pp < clinks; ++pp) {
            link = subarea.getLink(pp);
            link->set("r", sr);
            link->set("x", sx);
            link->set("b", sB);
            if (sG != 0) {
                link->set("g", sG);
            }
        }
    }
    for (int pp = clinks; pp < numLinks; ++pp) {
        GridBus* bus = new AcBus("ibus" + std::to_string(pp));

        subsystem::add(bus);

        link = new AcLine(sr, sx);
        link->set("b", sB);
        if (sG != 0) {
            link->set("g", sG);
        }
        subsystem::add(link);
        link->updateBus(bus, 1);

        subarea.getLink(pp - 1)->updateBus(bus, 2);
        if (pp == numLinks - 1) {
            terminalLink[1] = link;
        }
    }
}

}  // namespace griddyn::links
