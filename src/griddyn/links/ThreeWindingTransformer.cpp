/*
 * Copyright (c) 2014-2026, Lawrence Livermore National Security
 * See the top-level NOTICE for additional details. All rights reserved.
 * SPDX-License-Identifier: BSD-3-Clause
 */

#include "ThreeWindingTransformer.h"

#include "../primary/AcBus.h"
#include "core/CoreExceptions.h"
#include "core/CoreObjectTemplates.hpp"
#include <cmath>
#include <string>

namespace griddyn::links {
ThreeWindingTransformer::ThreeWindingTransformer(const std::string& objName): Subsystem(objName)
{
    auto* bus = new AcBus("ibus_mid");
    Subsystem::add(bus);

    auto* primaryLine = new AcLine("primary");
    auto* secondaryLine = new AcLine("secondary");
    auto* tertiaryLine = new AcLine("tertiary");
    Subsystem::add(primaryLine);
    Subsystem::add(secondaryLine);
    Subsystem::add(tertiaryLine);
    primaryLine->updateBus(bus, 2);
    secondaryLine->updateBus(bus, 1);
    tertiaryLine->updateBus(bus, 1);
    m_terminals = 3;
    terminalLink.resize(3);
    terminalLink[0] = primaryLine;
    terminalLink[1] = secondaryLine;
    terminalLink[2] = tertiaryLine;
    cterm.resize(3);
    cterm[0] = 1;
    cterm[1] = 2;
    cterm[2] = 2;
}
CoreObject* ThreeWindingTransformer::clone(CoreObject* obj) const
{
    auto* line = cloneBase<ThreeWindingTransformer, Link>(this, obj);
    if (line == nullptr) {
        return obj;
    }

    return line;
}
// add components
void ThreeWindingTransformer::add(CoreObject* /*obj*/)
{
    throw(UnrecognizedObjectException(this));
}
// remove components
void ThreeWindingTransformer::remove(CoreObject* /*obj*/) {}

void ThreeWindingTransformer::set(std::string_view param, std::string_view val)
{
    if (param == "primary") {
        Subsystem::set("from", val);
    } else if (param == "secondary") {
        Subsystem::set("to", val);
    } else if (param == "tertiary") {
        Subsystem::set("connection:3", val);
    } else {
        Subsystem::set(param, val);
        return;
    }
}

void ThreeWindingTransformer::set(std::string_view param, double val, units::unit unitType)
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
        if (opFlags[POWERFLOW_INITIALIZED]) {
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

double ThreeWindingTransformer::get(std::string_view param, units::unit unitType) const
{
    double val = kNullVal;
    if (param == "NULL") {
        val = kNullVal;
    } else {
        val = Subsystem::get(param, unitType);
    }
    return val;
}

}  // namespace griddyn::links
