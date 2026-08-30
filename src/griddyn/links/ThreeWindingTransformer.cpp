/*
 * Copyright (c) 2014-2026, Lawrence Livermore National Security
 * See the top-level NOTICE for additional details. All rights reserved.
 * SPDX-License-Identifier: BSD-3-Clause
 */

#include "ThreeWindingTransformer.h"

#include "../primary/AcBus.h"
#include "core/CoreExceptions.h"
#include "core/CoreObjectTemplates.hpp"
#include "core/ObjectFactoryTemplates.hpp"
#include "gmlc/utilities/stringOps.h"
#include <cmath>
#include <string>

namespace griddyn::links {
static TypeFactory<ThreeWindingTransformer>
    threeWindingTransformerFactory("link",
                                   std::to_array<std::string_view>({"three winding transformer",
                                                                    "threewindingtransformer",
                                                                    "threewinding"}));

ThreeWindingTransformer::ThreeWindingTransformer(const std::string& objName): Subsystem(objName)
{
    // Subsystem defaults to two terminals.  Use its resize routine so the
    // terminal buses and the per-terminal power-flow caches grow together.
    resize(3);

    starBus = new AcBus("ibus_mid");
    Subsystem::add(starBus);

    windingLegs[0] = new AcLine("primary");
    windingLegs[1] = new AcLine("secondary");
    windingLegs[2] = new AcLine("tertiary");
    for (auto* leg : windingLegs) {
        Subsystem::add(leg);
        // Keep every external winding on terminal 1.  This gives all three
        // PSS/E winding tap ratios and phase shifts the same orientation.
        leg->updateBus(starBus, 2);
    }
    terminalLink[0] = windingLegs[0];
    terminalLink[1] = windingLegs[1];
    terminalLink[2] = windingLegs[2];
    cterm.resize(3);
    cterm[0] = 1;
    cterm[1] = 1;
    cterm[2] = 1;
}

AcLine* ThreeWindingTransformer::windingLeg(index_t winding) const
{
    if ((winding < 1) || (winding > windingLegs.size())) {
        throw InvalidParameterValue("three-winding transformer winding");
    }
    return windingLegs[winding - 1];
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
        Subsystem::set("bus1", val);
    } else if (param == "secondary") {
        Subsystem::set("bus2", val);
    } else if (param == "tertiary") {
        Subsystem::set("bus3", val);
    } else {
        Subsystem::set(param, val);
        return;
    }
}

void ThreeWindingTransformer::set(std::string_view param, double val, units::unit unitType)
{
    std::string baseParam;
    const int winding = gmlc::utilities::stringOps::trailingStringInt(param, baseParam, -1);
    if ((winding >= 1) && (winding <= 3)) {
        auto* leg = windingLeg(static_cast<index_t>(winding));
        if ((baseParam == "r") || (baseParam == "x") || (baseParam == "b") || (baseParam == "g") ||
            (baseParam == "tap") || (baseParam == "tapangle") || (baseParam == "ratinga") ||
            (baseParam == "ratingb") || (baseParam == "ratingc")) {
            leg->set(baseParam, val, unitType);
            return;
        }
    }
    if (param.length() == 1) {
        switch (param[0]) {
            case 'r':
                for (auto* leg : windingLegs) {
                    leg->set("r", val, unitType);
                }
                break;
            case 'x':
                for (auto* leg : windingLegs) {
                    leg->set("x", val, unitType);
                }
                break;
            case 'b':
                setMagnetizing(0.0, val, unitType);
                break;
            case 'g':
                setMagnetizing(val, 0.0, unitType);
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

void ThreeWindingTransformer::setWindingImpedance(index_t winding,
                                                  double resistance,
                                                  double reactance,
                                                  units::unit unitType)
{
    auto* leg = windingLeg(winding);
    leg->set("r", resistance, unitType);
    leg->set("x", reactance, unitType);
}

void ThreeWindingTransformer::setWindingTap(index_t winding,
                                            double tap,
                                            double phaseShift,
                                            units::unit phaseUnit)
{
    auto* leg = windingLeg(winding);
    if (tap != 0.0) {
        leg->set("tap", tap);
    }
    if (phaseShift != 0.0) {
        leg->set("tapangle", phaseShift, phaseUnit);
    }
}

void ThreeWindingTransformer::setWindingRatings(index_t winding,
                                                double ratingAValue,
                                                double ratingBValue,
                                                double ratingCValue,
                                                units::unit unitType)
{
    auto* leg = windingLeg(winding);
    if (ratingAValue != 0.0) {
        leg->set("ratinga", ratingAValue, unitType);
    }
    if (ratingBValue != 0.0) {
        leg->set("ratingb", ratingBValue, unitType);
    }
    if (ratingCValue != 0.0) {
        leg->set("ratingc", ratingCValue, unitType);
    }
}

void ThreeWindingTransformer::setWindingStatus(index_t winding, bool isEnabled)
{
    auto* leg = windingLeg(winding);
    if (isEnabled) {
        leg->enable();
    } else {
        leg->disable();
    }
}

void ThreeWindingTransformer::setMagnetizing(double conductance,
                                             double susceptance,
                                             units::unit unitType)
{
    windingLegs[0]->set("g", conductance, unitType);
    windingLegs[0]->set("b", susceptance, unitType);
}

void ThreeWindingTransformer::setStarVoltageAngle(double voltage,
                                                  double angle,
                                                  units::unit angleUnit)
{
    starBus->setVoltageAngle(voltage, units::convert(angle, angleUnit, units::rad));
}

void ThreeWindingTransformer::followNetwork(int network, std::queue<GridBus*>& stk)
{
    for (auto* leg : windingLegs) {
        leg->followNetwork(network, stk);
    }
}

void ThreeWindingTransformer::updateBus(GridBus* bus, index_t busnumber)
{
    Subsystem::updateBus(bus, busnumber);
    // Register the enclosing link on two exterior buses as well.  GridDyn's
    // top-level network discovery starts from these attachments; the override
    // above expands that two-terminal discovery into all three star legs.
    if (busnumber <= 2) {
        Link::updateBus(bus, busnumber);
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
