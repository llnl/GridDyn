/*
 * Copyright (c) 2014-2026, Lawrence Livermore National Security
 * See the top-level NOTICE for additional details. All rights reserved.
 * SPDX-License-Identifier: BSD-3-Clause
 */

// headers
#include "Hvdc.h"

#include "../GridBus.h"
#include "../Link.h"
#include "../primary/DcBus.h"
#include "AcdcConverter.h"
#include "DcLink.h"
#include "core/CoreObjectTemplates.hpp"
#include "core/ObjectFactoryTemplates.hpp"
#include "gmlc/utilities/stringOps.h"
#include "gmlc/utilities/vectorOps.hpp"
#include <string>

namespace griddyn::links {
using units::unit;

static TypeFactory<Hvdc> gf("link", std::to_array<std::string_view>({"hvdc"}));

Hvdc::Hvdc(const std::string& objName): Subsystem(4, objName)
{
    // default values

    auto dcl = new DcLink("dcline");
    dcl->set("type", "slk");
    Subsystem::add(dcl);

    auto rec1 = new AcdcConverter("rect1");
    Subsystem::add(rec1);
    auto rec2 = new AcdcConverter("rect2");
    Subsystem::add(rec2);

    auto inv1 = new AcdcConverter("inv1");
    Subsystem::add(inv1);
    auto inv2 = new AcdcConverter("inv2");
    Subsystem::add(inv2);

    auto dcb1 = new DcBus("bus1");
    Subsystem::add(dcb1);
    auto dcb2 = new DcBus("bus2");
    Subsystem::add(dcb2);

    dcl->updateBus(dcb1, 1);
    dcl->updateBus(dcb2, 2);
    inv1->updateBus(dcb1, 2);
    rec1->updateBus(dcb2, 2);

    inv2->updateBus(dcb2, 2);
    rec2->updateBus(dcb1, 2);

    Subsystem::set("connection_1", "rect1,1");
    Subsystem::set("connection_2", "inv1,1");
    Subsystem::set("connection_3", "inv2,1");
    Subsystem::set("connection_4", "rect2,1");
    inv2->set("pset", 0);
    rec2->set("pset", 0);
    inv1->set("pset", 0);
    rec2->set("pset", 0);
}

CoreObject* Hvdc::clone(CoreObject* obj) const
{
    auto nobj = cloneBase<Hvdc, Subsystem>(this, obj);
    if (nobj == nullptr) {
        return obj;
    }

    return nobj;
}

// set properties
void Hvdc::set(std::string_view param, std::string_view val)
{
    if (param == "from") {
        Subsystem::set("bus1", val);
        Subsystem::set("bus3", val);
    } else if (param == "to") {
        Subsystem::set("bus2", val);
        Subsystem::set("bus4", val);
    } else {
        Subsystem::set(param, val);
    }
}

void Hvdc::set(std::string_view param, double val, unit unitType)
{
    if (param == "r") {
        getLink(0)->set("r", val, unitType);
    } else if (param == "x") {
        getLink(0)->set("x", val, unitType);
    } else if (param == "pset") {
        if (val < 0) {
            setFlow(reverse);
            getLink(2)->set("pset", val, unitType);
        } else {
            setFlow(forward);
            getLink(1)->set("pset", val, unitType);
        }
    } else if (param == "pout") {
        if (val < 0) {
            setFlow(reverse);
            getLink(4)->set("pset", val, unitType);
        } else {
            setFlow(forward);
            getLink(3)->set("pset", val, unitType);
        }
    } else {
        Subsystem::set(param, val, unitType);
    }
}

double Hvdc::get(std::string_view param, unit unitType) const
{
    double val = kNullVal;
    if (param == "#") {
    } else {
        val = Subsystem::get(param, unitType);
    }
    return val;
}

void Hvdc::updateBus(GridBus* bus, index_t busnumber)
{
    if (busnumber == 1) {
        Subsystem::updateBus(bus, 1);
        Subsystem::updateBus(bus, 3);
    } else if (busnumber == 2) {
        Subsystem::updateBus(bus, 2);
        Subsystem::updateBus(bus, 4);
    } else {
        Subsystem::updateBus(bus, busnumber);
    }
}

void Hvdc::setFlow(int direction)
{
    if (direction == reverse) {
        if (!opFlags[reverse_flow]) {
            opFlags.set(reverse_flow);
        }
    } else {
        if (opFlags[reverse_flow]) {
            opFlags.reset(reverse_flow);
        }
    }
}
}  // namespace griddyn::links
