/*
 * Copyright (c) 2014-2026, Lawrence Livermore National Security
 * See the top-level NOTICE for additional details. All rights reserved.
 * SPDX-License-Identifier: BSD-3-Clause
 */

#include "LongLine.h"

#include "../primary/AcBus.h"
#include "core/CoreExceptions.h"
#include "core/CoreObjectTemplates.hpp"
#include <cmath>
#include <string>

namespace griddyn::links {
LongLine::LongLine(const std::string& objName): Subsystem(objName) {}
CoreObject* LongLine::clone(CoreObject* obj) const
{
    auto* line = cloneBase<LongLine, Link>(this, obj);
    if (line == nullptr) {
        return obj;
    }
    line->segmentationLength = segmentationLength;
    if (opFlags[pFlow_initialized]) {
        Subsystem::clone(line);
    }
    return line;
}
// add components
void LongLine::add(CoreObject* /*obj*/)
{
    throw(UnrecognizedObjectException(this));
}
// remove components
void LongLine::remove(CoreObject* /*obj*/) {}
void LongLine::pFlowObjectInitializeA(CoreTime time0, std::uint32_t flags)
{
    generateIntermediateLinks();
    Subsystem::pFlowObjectInitializeA(time0, flags);
}

void LongLine::set(std::string_view param, std::string_view val)
{
    Link::set(param, val);
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
                throw(UnrecognizedParameter(param));
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
                auto linkCountValue = getInt("linkcount");
                const double faultStep = 1.0 / static_cast<double>(linkCountValue);
                int faultIndex = 0;
                double cumulativeMeasure = faultStep;
                while (cumulativeMeasure < fault) {
                    ++faultIndex;
                    cumulativeMeasure += faultStep;
                }
                const double newFaultVal = fault - cumulativeMeasure + faultStep;

                if (faultLink >= 0)  // if there is an existing fault move it
                {
                    getLink(faultLink)->set("fault", -1.0);
                }
                getLink(faultIndex)->set("fault", newFaultVal);
                faultLink = faultIndex;
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
        val = Subsystem::get(param, unitType);
    }
    return val;
}

void LongLine::generateIntermediateLinks()
{
    const int numLinks = static_cast<int>(std::ceil(length / segmentationLength));

    const double segmentResistance = r / static_cast<double>(numLinks);
    const double segmentReactance = x / static_cast<double>(numLinks);
    const double segmentB = mp_B / static_cast<double>(numLinks);
    const double segmentG = mp_G / static_cast<double>(numLinks);

    int clinks = getInt("linkCount");
    Link* link;

    if (clinks == 0) {
        link = new AcLine(segmentResistance, segmentReactance);
        link->set("b", segmentB);
        if (segmentG != 0) {
            link->set("g", segmentG);
        }
        Subsystem::add(link);
        clinks = 1;
        terminalLink[0] = link;
    } else {
        for (int pp = 0; pp < clinks; ++pp) {
            link = subarea.getLink(pp);
            link->set("r", segmentResistance);
            link->set("x", segmentReactance);
            link->set("b", segmentB);
            if (segmentG != 0) {
                link->set("g", segmentG);
            }
        }
    }
    for (int pp = clinks; pp < numLinks; ++pp) {
        GridBus* bus = new AcBus("ibus" + std::to_string(pp));

        Subsystem::add(bus);

        link = new AcLine(segmentResistance, segmentReactance);
        link->set("b", segmentB);
        if (segmentG != 0) {
            link->set("g", segmentG);
        }
        Subsystem::add(link);
        link->updateBus(bus, 1);

        subarea.getLink(pp - 1)->updateBus(bus, 2);
        if (pp == numLinks - 1) {
            terminalLink[1] = link;
        }
    }
}

}  // namespace griddyn::links
