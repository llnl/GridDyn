/*
 * Copyright (c) 2014-2020, Lawrence Livermore National Security
 * See the top-level NOTICE for additional details. All rights reserved.
 * SPDX-License-Identifier: BSD-3-Clause
 */

// headers
#include "adjustableTransformer.h"

#include "../gridBus.h"
#include "core/coreExceptions.h"
#include "core/coreObjectTemplates.hpp"
#include "core/objectInterpreter.h"
#include "gmlc/utilities/stringOps.h"
#include "gmlc/utilities/vectorOps.hpp"
#include "griddyn/griddyn-config.h"
#include "utilities/matrixData.hpp"
#include "utilities/matrixDataTranslate.hpp"
#include <algorithm>
#include <cassert>
#include <cmath>
#include <iostream>
#include <queue>
#include <string>
#include <vector>

/*
enum control_mode_t{ manual_control=0, voltage_control=1, MW_control=2, MVar_control=3};
enum change_mode_t{ stepped = 0, continuous = 1 };
*/
namespace griddyn {
namespace links {
    using gmlc::utilities::convertToLowerCase;
    using gmlc::utilities::signn;
    using units::convert;
    using units::puMW;
    using units::rad;
    using units::unit;
    adjustableTransformer::adjustableTransformer(const std::string& objName): acLine(objName) {}
    adjustableTransformer::adjustableTransformer(double rP, double xP, const std::string& objName):
        acLine(rP, xP, objName)
    {
    }

    coreObject* adjustableTransformer::clone(coreObject* obj) const
    {
        auto* lnk = cloneBase<adjustableTransformer, acLine>(this, obj);
        if (lnk == nullptr) {
            return obj;
        }
        lnk->cMode = cMode;
        lnk->stepSize = stepSize;
        lnk->minTapAngle = minTapAngle;
        lnk->maxTapAngle = maxTapAngle;
        lnk->minTap = minTap;
        lnk->maxTap = maxTap;

        lnk->Vmax = Vmax;
        lnk->Vmin = Vmin;
        lnk->Vtarget = Vtarget;

        lnk->Pmin = Pmin;
        lnk->Pmax = Pmax;
        lnk->Ptarget = Ptarget;

        lnk->Qmax = Qmax;
        lnk->Qmin = Qmin;
        lnk->Qtarget = Qtarget;

        lnk->direction = direction;
        lnk->controlBus = nullptr;
        lnk->controlNum = controlNum;
        lnk->dTapdt = dTapdt;
        lnk->dTapAdt = dTapAdt;
        return lnk;
    }

    static const stringVec locNumStrings{"vmin",
                                         "vmax",
                                         "vtarget",
                                         "pmin",
                                         "pmax",
                                         "ptarget",
                                         "qmin",
                                         "qmax",
                                         "qtarget",
                                         "direction",
                                         "mintap",
                                         "maxtap",
                                         "mintapangle",
                                         "maxtapangle",
                                         "stepsize",
                                         "nsteps",
                                         "dtapdt",
                                         "dtapadt"};
    static const stringVec locStrStrings{"controlmode", "changemode", "centermode"};
    static const stringVec flagStrings{"no_pflow_adjustments"};
    void adjustableTransformer::getParameterStrings(stringVec& pstr, paramStringType pstype) const
    {
        getParamString<adjustableTransformer, acLine>(
            this, pstr, locNumStrings, locStrStrings, flagStrings, pstype);
    }

    // set properties
    void adjustableTransformer::set(const std::string& param, const std::string& val)
    {
        if ((param == "controlmode") || (param == "mode") || (param == "control_mode")) {
            auto cmstr = convertToLowerCase(val);

            if ((cmstr == "mw") || (cmstr == "power")) {
                cMode = control_mode_t::MW_control;
                opFlags.set(adjustable_P);
            } else if ((cmstr == "mvar") || (cmstr == "reactive")) {
                cMode = control_mode_t::MVar_control;
            } else if ((cmstr == "v") || (cmstr == "voltage")) {
                cMode = control_mode_t::voltage_control;
            } else if (cmstr == "manual") {
                cMode = control_mode_t::manual_control;
            } else {
                throw(invalidParameterValue(cmstr));
            }
        } else if ((param == "change") || (param == "change_mode") || (param == "changemode") ||
                   (param == "stepmode")) {
            auto cmstr = convertToLowerCase(val);
            if (cmstr == "continuous") {
                opFlags.set(continuous_flag);
            } else if ((cmstr == "stepped") || (cmstr == "step")) {
                opFlags.set(continuous_flag, false);
            } else {
                throw(invalidParameterValue(cmstr));
            }
        } else if ((param == "center") || (param == "center_mode") || (param == "centermode")) {
            auto cmstr = convertToLowerCase(val);
            if (cmstr == "bounds") {
                opFlags.set(use_target_mode, false);
            } else if ((cmstr == "target") || (cmstr == "center")) {
                opFlags.set(use_target_mode);
            } else {
                throw(invalidParameterValue(cmstr));
            }
        } else if ((param == "bus") || (param == "controlbus")) {
            auto* bus = dynamic_cast<gridBus*>(locateObject(val, getParent()));
            if (bus != nullptr) {
                controlBus = bus;
            } else {
                controlName = val;
            }
        } else {
            acLine::set(param, val);
        }
    }

    void adjustableTransformer::set(const std::string& param, double val, unit unitType)
    {
        if (param == "tap") {
            tap = val;
            tap0 = val;
        } else if (param == "tapangle") {
            tapAngle = convert(val, unitType, rad);
            tapAngle0 = tapAngle;
        } else if ((param == "no_pflow_control") || (param == "no_pflow_adjustments")) {
            opFlags.set(no_pFlow_adjustments, (val > 0));
        } else if ((param == "cbus") || (param == "controlbus")) {
            if (val > 2.5) {
                auto cbnum = static_cast<int>(val);
                if ((B1 != nullptr) && (cbnum == B1->getUserID())) {
                    controlNum = 1;
                    controlBus = B1;
                } else if ((B2 != nullptr) && (cbnum == B2->getUserID())) {
                    controlNum = 2;
                    controlBus = B2;
                } else {
                    controlNum = cbnum;
                    controlBus = static_cast<gridBus*>(getParent()->findByUserID("bus", cbnum));
                }
            } else if (val > 1.5) {
                if (B2 == nullptr) {
                    controlNum = 2;
                } else {
                    controlNum = 2;
                    controlBus = B2;
                }
                direction = 1;
            } else {
                if (B1 == nullptr) {
                    controlNum = 1;
                } else {
                    controlNum = 1;
                    controlBus = B1;
                }
                direction = -1;
            }
        } else if (param == "vmin") {
            Vmin = val;
        } else if (param == "vmax") {
            Vmax = val;
        } else if (param == "vtarget") {
            Vtarget = val;
        } else if (param == "pmin") {
            Pmin = convert(val, unitType, puMW, systemBasePower);
        } else if (param == "pmax") {
            Pmax = convert(val, unitType, puMW, systemBasePower);
        } else if (param == "ptarget") {
            Ptarget = convert(val, unitType, puMW, systemBasePower);
        } else if (param == "qmin") {
            Qmin = convert(val, unitType, puMW, systemBasePower);
        } else if (param == "qmax") {
            Qmax = convert(val, unitType, puMW, systemBasePower);
        } else if (param == "qtarget") {
            Qtarget = convert(val, unitType, puMW, systemBasePower);
        } else if (param == "target") {
            if (cMode == control_mode_t::MVar_control) {
                Qtarget = convert(val, unitType, puMW, systemBasePower);
            } else if (cMode == control_mode_t::MW_control) {
                Ptarget = convert(val, unitType, puMW, systemBasePower);
            } else {
                Vtarget = val;
            }
        } else if (param == "min") {
            if (cMode == control_mode_t::MVar_control) {
                Qmin = convert(val, unitType, puMW, systemBasePower);
            } else if (cMode == control_mode_t::MW_control) {
                Pmin = convert(val, unitType, puMW, systemBasePower);
            } else {
                Vmin = val;
            }
        } else if (param == "max") {
            if (cMode == control_mode_t::MVar_control) {
                Qmax = convert(val, unitType, puMW, systemBasePower);
            } else if (cMode == control_mode_t::MW_control) {
                Pmax = convert(val, unitType, puMW, systemBasePower);
            } else {
                Vmax = val;
            }
        } else if (param == "direction") {
            if (val < 0) {
                direction = -1;
            } else {
                direction = 1;
            }
        } else if (param == "mintap") {
            minTap = val;
            if (tap < minTap) {
                tap = minTap;
            }
        } else if (param == "maxtap") {
            maxTap = val;
            if (tap > maxTap) {
                tap = maxTap;
            }
        } else if (param == "mintapangle") {
            minTapAngle = convert(val, unitType, rad);
            if (tapAngle < minTapAngle) {
                LOG_WARNING("specified tap angle below minimum");
                tapAngle = minTapAngle;
            }
        } else if (param == "fault") {
            // not faults not allowed on adjustable transformoers change shunt conductance  or
            // impedance to simulate a fault
            LOG_ERROR(
                "faults not allowed on adjustable transformers change shunt conductance  or impedance to "
                "simulate a fault");
            throw(unrecognizedParameter(param));
        } else if (param == "maxtapangle") {
            maxTapAngle = convert(val, unitType, rad);
            if (tapAngle > maxTapAngle) {
                LOG_WARNING("specified tap angle above maximum");
                tapAngle = maxTapAngle;
            }
        } else if ((param == "stepsize") || (param == "tapchange")) {
            if (cMode == control_mode_t::MW_control) {
                stepSize = convert(val, unitType, rad);
            } else {
                stepSize = val;
            }
            if (stepSize == 0)  // if stepsize==0 we are continuous otherwise leave it where it was
            {
                opFlags.set(continuous_flag);
            }
        } else if (param == "nsteps") {
            if (cMode == control_mode_t::MW_control) {
                stepSize = (maxTapAngle - minTapAngle) / val;
            } else {
                stepSize = (maxTap - minTap) / val;
            }
        } else if (param == "dtapdt") {
            dTapdt = val;
        } else if (param == "dtapadt") {
            dTapAdt = val;
        } else {
            acLine::set(param, val, unitType);
        }
    }

    double adjustableTransformer::get(const std::string& param, units::unit unitType) const
    {
        double val = kNullVal;

        if (param == "controlbus") {
            val = controlNum;
        } else if (param == "controlbusid") {
            val = static_cast<double>(controlBus->getUserID());
        } else if (param == "vmin") {
            val = Vmin;
        } else if (param == "vmax") {
            val = Vmax;
        } else if (param == "vtarget") {
            val = Vtarget;
        } else if (param == "pmin") {
            val = convert(Pmin, puMW, unitType, systemBasePower);
        } else if (param == "pmax") {
            val = convert(Pmax, puMW, unitType, systemBasePower);
        } else if (param == "ptarget") {
            val = convert(Ptarget, puMW, unitType, systemBasePower);
        } else if (param == "qmin") {
            val = convert(Qmin, puMW, unitType, systemBasePower);
        } else if (param == "qmax") {
            val = convert(Qmax, puMW, unitType, systemBasePower);
        } else if (param == "qtarget") {
            val = convert(Qtarget, puMW, unitType, systemBasePower);
        } else if (param == "target") {
            if (cMode == control_mode_t::MVar_control) {
                val = convert(Qtarget, puMW, unitType, systemBasePower);
            } else if (cMode == control_mode_t::MW_control) {
                val = convert(Ptarget, puMW, unitType, systemBasePower);
            } else {
                val = Vtarget;
            }
        } else if (param == "min") {
            if (cMode == control_mode_t::MVar_control) {
                val = convert(Qmin, puMW, unitType, systemBasePower);
            } else if (cMode == control_mode_t::MW_control) {
                val = convert(Pmin, puMW, unitType, systemBasePower);
            } else {
                val = Vmin;
            }
        } else if (param == "max") {
            if (cMode == control_mode_t::MVar_control) {
                val = convert(Qmax, puMW, unitType, systemBasePower);
            } else if (cMode == control_mode_t::MW_control) {
                val = convert(Pmax, puMW, unitType, systemBasePower);
            } else {
                val = Vmax;
            }
        } else if (param == "direction") {
            val = direction;
        } else if (param == "mintap") {
            val = minTap;
        } else if (param == "maxtap") {
            val = maxTap;
        } else if (param == "mintapangle") {
            val = convert(minTapAngle, rad, unitType);
        } else if (param == "maxtapangle") {
            val = convert(maxTapAngle, rad, unitType);
        } else if ((param == "stepsize") || (param == "tapchange")) {
            val = stepSize;
        } else if (param == "nsteps") {
            if (cMode == control_mode_t::MW_control) {
                val = (maxTapAngle - minTapAngle) / stepSize;
            } else {
                val = (maxTap - minTap) / stepSize;
            }
        } else if (param == "control_mode") {
            val = static_cast<double>(cMode);
        } else if (param == "dtapdt") {
            val = dTapdt;
        } else if (param == "dtapadt") {
            val = dTapAdt;
        } else {
            val = acLine::get(param, unitType);
        }
        return val;
    }

    void adjustableTransformer::setControlBus(gridBus* cBus)
    {
        controlBus = cBus;
    }
    void adjustableTransformer::setControlBus(index_t busNumber)
    {
        if ((busNumber == 1) || (busNumber == B1->getID())) {
            controlBus = B1;
            controlNum = 1;
            direction = -1;
        } else if ((busNumber == 2) || (busNumber == B2->getID())) {
            controlBus = B2;
            controlNum = 2;
            direction = 1;
        } else {
            auto* cb = getParent()->findByUserID("bus", busNumber);
            if (cb != nullptr) {
                controlBus = static_cast<gridBus*>(cb);
                controlNum = 0;
            }
        }
    }

    void adjustableTransformer::followNetwork(int network, std::queue<gridBus*>& stk)
    {
        if (isConnected()) {
            if (cMode == control_mode_t::MW_control) {
                if (!opFlags[no_pFlow_adjustments]) {
                    return;  // no network connection if the MW flow is controlled since the angle
                             // relationship is
                    // variable
                }
            }
            if (B1->Network != network) {
                stk.push(B1);
            }
            if (B2->Network != network) {
                stk.push(B2);
            }
        }
    }

    void adjustableTransformer::pFlowObjectInitializeA(coreTime time0, std::uint32_t flags)
    {
        if (cMode != control_mode_t::manual_control) {
            if (cMode == control_mode_t::voltage_control) {
                if (controlBus == nullptr) {
                    if (controlNum == 1) {
                        controlBus = B1;
                        direction = -1;
                    } else if (controlNum == 2) {
                        controlBus = B2;
                        direction = 1;
                    } else if (!controlName.empty()) {
                        coreObject* obj = locateObject(controlName, getParent());
                        if (obj != nullptr) {
                            controlBus = dynamic_cast<gridBus*>(obj);
                        }
                    }
                } else {
                    direction = (controlBus == B1) ? (-1.0) : 1.0;
                }
                if (controlBus == nullptr) {
                    controlBus = B2;
                    controlNum = 2;
                    direction = 1;
                }
            }
            if ((opFlags[continuous_flag]) && (!opFlags[no_pFlow_adjustments])) {
                opFlags.set(has_pflow_states);
                opFlags.set(has_powerflow_adjustments);
            } else {
                opFlags[has_powerflow_adjustments] = !opFlags[no_pFlow_adjustments];
                opFlags.reset(has_pflow_states);
            }
            if (cMode == control_mode_t::voltage_control) {
                if (Vtarget < Vmin) {
                    Vtarget = (Vmin + Vmax) / 2.0;
                }
            } else if (cMode == control_mode_t::MW_control) {
                if (Ptarget < Pmin) {
                    Ptarget = (Pmin + Pmax) / 2.0;
                }
            } else if (cMode == control_mode_t::MVar_control) {
                if (Qtarget < Qmin) {
                    Qtarget = (Qmin + Qmax) / 2.0;
                }
            }
        } else {
            opFlags.reset(has_pflow_states);
            opFlags.reset(has_powerflow_adjustments);
        }
        adjCount = 0;
        oCount = 0;
        tap0 = tap;
        tapAngle0 = tapAngle;
        return acLine::pFlowObjectInitializeA(time0, flags);
    }

    stateSizes adjustableTransformer::LocalStateSizes(const solverMode& sMode) const
    {
        stateSizes lcStates;
        if (isDynamic(sMode)) {
            if (opFlags[continuous_flag]) {
                switch (cMode) {
                    case control_mode_t::manual_control:
                        break;
                    case control_mode_t::voltage_control:
                    case control_mode_t::MVar_control:
                    case control_mode_t::MW_control:
                        lcStates.algSize = 1;
                        break;
                }
            }
        } else {
            if ((opFlags[continuous_flag]) && (!opFlags[no_pFlow_adjustments])) {
                lcStates.algSize = 1;
            }
        }
        return lcStates;
    }

    count_t adjustableTransformer::LocalJacobianCount(const solverMode& sMode) const
    {
        count_t localJacSize = 0;
        if (isDynamic(sMode)) {
            if (opFlags[continuous_flag]) {
                switch (cMode) {
                    case control_mode_t::manual_control:
                        break;
                    case control_mode_t::voltage_control:
                        localJacSize = 2;
                        break;
                    case control_mode_t::MVar_control:
                    case control_mode_t::MW_control:
                        localJacSize = 5;
                        break;
                }
            }
        } else {
            if ((opFlags[continuous_flag]) && (!opFlags[no_pFlow_adjustments])) {
                switch (cMode) {
                    case control_mode_t::manual_control:
                        break;
                    case control_mode_t::voltage_control:
                        localJacSize = 2;
                        break;
                    case control_mode_t::MVar_control:
                    case control_mode_t::MW_control:
                        localJacSize = 5;
                        break;
                }
            }
        }
        return localJacSize;
    }

    void adjustableTransformer::getStateName(stringVec& stNames,
                                             const solverMode& sMode,
                                             const std::string& prefix) const
    {
        if (stateSize(sMode) > 0) {
            std::string prefix2 = prefix + getName() + ':';
            if (isDynamic(sMode)) {
            } else {
                auto offset = offsets.getAlgOffset(sMode);
                if (cMode == control_mode_t::MW_control) {
                    stNames[offset] = prefix2 + "tapAngle";
                } else {
                    stNames[offset] = prefix2 + "tap";
                }
            }
        }
    }

    void adjustableTransformer::reset(reset_levels level)
    {
        if (level == reset_levels::full) {
            adjCount = 0;
            oCount = 0;
            switch (cMode) {
                case control_mode_t::manual_control:
                    break;
                case control_mode_t::voltage_control:
                    if ((Vmin <= 0.8) && (Vmax >= 1.2))  // check to make sure the actual controls
                                                         // are not effectively disabled
                    {
                        break;
                    }
                    FALLTHROUGH
                    // FALLTHROUGH
                case control_mode_t::MVar_control: {
                    double midTap = (minTap + maxTap) / 2.0;
                    if (opFlags[continuous_flag]) {
                        tap = midTap;
                        tap0 = tap;
                    } else {
                        double ttap = tap;
                        // making sure we stay in the bounds with the quantization from the current
                        // point
                        if (ttap >= midTap) {
                            while (ttap > midTap) {
                                ttap -= stepSize;
                            }
                            tap = ((midTap - ttap) < (ttap + stepSize - midTap)) ?
                                ttap :
                                (ttap + stepSize);
                        } else {
                            while (ttap < midTap) {
                                ttap += stepSize;
                            }
                            tap = ((ttap - midTap) < (midTap - ttap + stepSize)) ?
                                ttap :
                                (ttap - stepSize);
                        }
                        tap0 = tap;
                    }
                } break;
                case control_mode_t::MW_control: {
                    double midTap = (minTapAngle + maxTapAngle) / 2.0;
                    if (opFlags[continuous_flag]) {
                        tapAngle = midTap;
                        tapAngle0 = tapAngle;
                    } else {
                        double ttap = tapAngle;
                        // making sure we stay in the bounds with the quantization from the current
                        // point
                        if (ttap >= midTap) {
                            while (ttap > midTap) {
                                ttap -= stepSize;
                            }
                            tapAngle = ((midTap - ttap) < (ttap + stepSize - midTap)) ?
                                ttap :
                                (ttap + stepSize);
                        } else {
                            while (ttap < midTap) {
                                ttap += stepSize;
                            }
                            tapAngle = ((ttap - midTap) < (midTap - ttap + stepSize)) ?
                                ttap :
                                (ttap - stepSize);
                        }
                        tapAngle0 = tapAngle;
                    }
                } break;
            }
        }
    }

    change_code adjustableTransformer::powerFlowAdjust(const IOdata& /*inputs*/,
                                                       std::uint32_t flags,
                                                       check_level_t /*level*/)
    {
        if (CHECK_CONTROLFLAG(flags, no_link_adjustments)) {
            return change_code::no_change;
        }
        auto ret = change_code::no_change;
        if (cMode == control_mode_t::MW_control) {
            if (opFlags[continuous_flag])  // if continuous mode just check the min and max angle
            {
                if (tapAngle < minTapAngle) {
                    tapAngle = minTapAngle;
                    opFlags.set(at_limit);
                    alert(this, JAC_COUNT_DECREASE);
                    return change_code::jacobian_change;
                }
                if (tapAngle > maxTapAngle) {
                    tapAngle = maxTapAngle;
                    opFlags.reset(has_pflow_states);
                    alert(this, JAC_COUNT_DECREASE);
                    return change_code::jacobian_change;
                }
            } else {
                if (linkFlows.P1 > Pmax) {
                    if (tapAngle + stepSize < maxTapAngle) {
                        tapAngle = tapAngle + stepSize;
                        ret = change_code::parameter_change;
                    }
                    if (adjCount > 0) {
                        if (signn(prevAdjust) != 1) {
                            oCount++;
                            if (oCount > 5) {
                                if ((linkFlows.P1 - Pmax) < (Pmin - prevValue)) {
                                    ret = change_code::no_change;
                                }
                            }
                        }
                    }
                    if (ret > change_code::no_change) {
                        prevAdjust = stepSize;
                    }
                } else if (linkFlows.P1 < Pmin) {
                    if (tapAngle - stepSize > minTapAngle) {
                        tapAngle = tapAngle - stepSize;
                        ret = change_code::parameter_change;
                    }
                    if (adjCount > 0) {
                        if (signn(prevAdjust) != -1) {
                            oCount++;
                            if (oCount > 5) {
                                if ((prevValue - Pmax) > (Pmin - linkFlows.P1)) {
                                    ret = change_code::no_change;
                                }
                            }
                        }
                    }
                    if (ret > change_code::no_change) {
                        prevAdjust = -stepSize;
                    }
                }

                prevValue = linkFlows.P1;
            }
        } else if (cMode == control_mode_t::voltage_control) {
            if (opFlags[continuous_flag]) {
                // if continuous mode just check the min and max tap angle
                if (tap < minTap) {
                    tap = minTap;
                    opFlags.set(at_limit);
                    alert(this, JAC_COUNT_DECREASE);
                    return change_code::jacobian_change;
                }
                if (tap > maxTap) {
                    tap = maxTap;
                    opFlags.set(at_limit);
                    alert(this, JAC_COUNT_DECREASE);
                    return change_code::jacobian_change;
                }
            } else {
                ret = voltageControlAdjust();
            }
        } else if (cMode == control_mode_t::MVar_control) {
            if (opFlags[continuous_flag]) {
                if (tap < minTap) {
                    tap = minTap;
                    opFlags.set(at_limit);
                    alert(this, JAC_COUNT_DECREASE);
                    return change_code::jacobian_change;
                }
                if (tap > maxTap) {
                    tap = maxTap;
                    opFlags.set(at_limit);
                    alert(this, JAC_COUNT_DECREASE);
                    return change_code::jacobian_change;
                }
            } else {
                if (linkFlows.Q2 < Qmin) {
                    if (tap + stepSize < maxTap) {
                        tap = tap + stepSize;
                        ret = change_code::parameter_change;
                    }
                    if (adjCount > 0) {
                        if (signn(prevAdjust) != 1) {
                            oCount++;
                            if (oCount > 5) {
                                if ((prevValue - Qmax) > (Qmin - linkFlows.Q2)) {
                                    ret = change_code::no_change;
                                }
                            }
                        }
                    }
                    if (ret > change_code::no_change) {
                        prevAdjust = stepSize;
                    }
                } else if (linkFlows.Q2 > Qmax) {
                    if (tap - stepSize > minTap) {
                        tap = tap - stepSize;
                        ret = change_code::parameter_change;
                    }
                    if (adjCount > 0) {
                        if (signn(prevAdjust) != -1) {
                            oCount++;
                            if (oCount > 5) {
                                if ((linkFlows.Q2 - Qmax) < (Qmin - prevValue)) {
                                    ret = change_code::no_change;
                                }
                            }
                        }
                    }
                    if (ret > change_code::no_change) {
                        prevAdjust = -stepSize;
                    }
                }

                prevValue = linkFlows.Q2;
            }
        }
        if (ret > change_code::no_change) {
            adjCount++;
        }
        return ret;
    }

    void adjustableTransformer::guessState(coreTime /*time*/,
                                           double state[],
                                           double dstate_dt[],
                                           const solverMode& sMode)
    {
        auto offset = offsets.getAlgOffset(sMode);
        if ((!(isDynamic(sMode))) && (opFlags[has_pflow_states])) {
            if (cMode == control_mode_t::MW_control) {
                state[offset] = tapAngle0;
            } else {
                state[offset] = tap0;
            }
        } else if ((isDynamic(sMode)) && (opFlags[has_dyn_states])) {
            auto dOffset = offsets.getDiffOffset(sMode);
            dstate_dt[dOffset] = 0;
            // TODO(PT): guessState dynamic states
        }
    }

    void adjustableTransformer::ioPartialDerivatives(id_type_t busId,
                                                     const stateData& sD,
                                                     matrixData<double>& md,
                                                     const IOlocs& inputLocs,
                                                     const solverMode& sMode)
    {
        if ((!(isDynamic(sMode))) && (opFlags[has_pflow_states])) {
            auto offset = offsets.getAlgOffset(sMode);
            if (cMode == control_mode_t::MW_control) {
                tapAngle = sD.state[offset];
            } else {
                tap = sD.state[offset];
            }
        } else if ((isDynamic(sMode)) && (opFlags[has_dyn_states])) {
        }
        return acLine::ioPartialDerivatives(busId, sD, md, inputLocs, sMode);
    }

    void adjustableTransformer::outputPartialDerivatives(const IOdata& inputs,
                                                         const stateData& sD,
                                                         matrixData<double>& md,
                                                         const solverMode& sMode)
    {
        // if the terminal is not specified assume there are 4 outputs
        if ((!(isDynamic(sMode))) && (opFlags[has_pflow_states])) {
            auto offset = offsets.getAlgOffset(sMode);
            matrixDataTranslate<2> mtrans(md);
            mtrans.setTranslation(0, 2);
            mtrans.setTranslation(1, 3);
            if (cMode == control_mode_t::MW_control) {
                tapAngle = sD.state[offset];
                tapAnglePartial(1, sD, md, sMode);
                tapAnglePartial(2, sD, mtrans, sMode);
            } else {
                tap = sD.state[offset];
                tapPartial(1, sD, md, sMode);
                tapPartial(2, sD, mtrans, sMode);
            }
        } else if ((isDynamic(sMode)) && (opFlags[has_dyn_states])) {
        }
        return acLine::outputPartialDerivatives(inputs, sD, md, sMode);
    }

    void adjustableTransformer::outputPartialDerivatives(id_type_t busId,
                                                         const stateData& sD,
                                                         matrixData<double>& md,
                                                         const solverMode& sMode)
    {
        if ((!(isDynamic(sMode))) && (opFlags[has_pflow_states])) {
            auto offset = offsets.getAlgOffset(sMode);
            if (cMode == control_mode_t::MW_control) {
                tapAngle = sD.state[offset];
                tapAnglePartial(static_cast<index_t>(busId), sD, md, sMode);
            } else {
                tap = sD.state[offset];
                tapPartial(static_cast<index_t>(busId), sD, md, sMode);
            }
        } else if ((isDynamic(sMode)) && (opFlags[has_dyn_states])) {
        }
        return acLine::outputPartialDerivatives(busId, sD, md, sMode);
    }

    void adjustableTransformer::jacobianElements(const IOdata& /*inputs*/,
                                                 const stateData& sD,
                                                 matrixData<double>& md,
                                                 const IOlocs& /*inputLocs*/,
                                                 const solverMode& sMode)
    {
        if ((!(isDynamic(sMode))) && (opFlags[has_pflow_states])) {
            auto offset = offsets.getAlgOffset(sMode);
            if (cMode == control_mode_t::MW_control) {
                tapAngle = sD.state[offset];
            } else {
                tap = sD.state[offset];
            }
            if (opFlags[at_limit]) {
                md.assign(offset, offset, 1);
            } else {
                if (cMode == control_mode_t::MW_control) {
                    MWJac(sD, md, sMode);
                } else if (cMode == control_mode_t::voltage_control) {
                    md.assignCheckCol(offset,
                                      controlBus->getOutputLoc(sMode, voltageInLocation),
                                      1);
                } else if (cMode == control_mode_t::MVar_control) {
                    MVarJac(sD, md, sMode);
                }
            }
        } else if ((isDynamic(sMode)) && (opFlags[has_dyn_states])) {
        }
    }

    void adjustableTransformer::residual(const IOdata& /*inputs*/,
                                         const stateData& sD,
                                         double resid[],
                                         const solverMode& sMode)
    {
        double v1;

        auto offset = offsets.getAlgOffset(sMode);
        if ((!(isDynamic(sMode))) && (opFlags[has_pflow_states])) {
            switch (cMode) {
                case control_mode_t::voltage_control:
                    if (opFlags[at_limit]) {
                        if (tap > tap0) {
                            resid[offset] = sD.state[offset] - maxTap;
                        } else {
                            resid[offset] = sD.state[offset] - minTap;
                        }
                    } else {
                        v1 = controlBus->getVoltage(sD, sMode);
                        resid[offset] = v1 - Vtarget;
                    }
                    break;
                case control_mode_t::MW_control:
                    if (opFlags[at_limit]) {
                        if (tapAngle > tapAngle0) {
                            resid[offset] = sD.state[offset] - maxTapAngle;
                        } else {
                            resid[offset] = sD.state[offset] - minTapAngle;
                        }
                    } else {
                        tapAngle = sD.state[offset];
                        updateLocalCache(noInputs, sD, sMode);
                        resid[offset] = linkFlows.P1 - Ptarget;
                    }
                    break;
                case control_mode_t::MVar_control:
                    tap = sD.state[offset];
                    if (opFlags[at_limit]) {
                        if (tap > tap0) {
                            resid[offset] = sD.state[offset] - maxTap;
                        } else {
                            resid[offset] = sD.state[offset] - minTap;
                        }
                    } else {
                        updateLocalCache(noInputs, sD, sMode);
                        resid[offset] = linkFlows.Q2 - Qtarget;
                    }

                    break;
                default:
                    assert(false);
            }
        } else if (isDynamic(sMode) && (opFlags[has_dyn_states])) {
        }
    }

    void adjustableTransformer::setState(coreTime time,
                                         const double state[],
                                         const double dstate_dt[],
                                         const solverMode& sMode)
    {
        auto offset = offsets.getAlgOffset(sMode);
        if ((!(isDynamic(sMode))) && (opFlags[has_pflow_states])) {
            if (cMode == control_mode_t::MW_control) {
                tapAngle = state[offset];
                if (tapAngle < maxTapAngle && tapAngle > minTapAngle) {
                    tapAngle0 = tapAngle;
                }
            } else {
                tap = state[offset];
                if (tap < maxTap && tap > minTap) {
                    tap0 = tap;
                }
            }
        } else if ((isDynamic(sMode)) && (opFlags[has_dyn_states])) {
        }
        acLine::setState(time, state, dstate_dt, sMode);
    }

    void adjustableTransformer::dynObjectInitializeA(coreTime time0, std::uint32_t flags)
    {
        return acLine::dynObjectInitializeA(time0, flags);
    }

    void adjustableTransformer::updateLocalCache()
    {
        acLine::updateLocalCache();
    }
    void adjustableTransformer::updateLocalCache(const IOdata& inputs,
                                                 const stateData& sD,
                                                 const solverMode& sMode)
    {
        if ((!(isDynamic(sMode))) && (opFlags[has_pflow_states])) {
            auto offset = offsets.getAlgOffset(sMode);
            if (cMode == control_mode_t::MW_control) {
                tapAngle = sD.state[offset];
            } else {
                tap = sD.state[offset];
            }
            acLine::updateLocalCache(inputs, sD, sMode);
        } else if ((isDynamic(sMode)) && (opFlags[has_dyn_states])) {
        } else {
            acLine::updateLocalCache(inputs, sD, sMode);
        }
    }

    count_t adjustableTransformer::outputDependencyCount(index_t /*num*/,
                                                         const solverMode& sMode) const
    {
        if ((!(isDynamic(sMode))) && (opFlags[has_pflow_states])) {
            return 3;
        }
        return 2;
    }

    void adjustableTransformer::rootTest(const IOdata& /*inputs*/,
                                         const stateData& sD,
                                         double roots[],
                                         const solverMode& sMode)
    {
        double controlVoltage;

        auto offset = offsets.getAlgOffset(sMode);
        auto rootOffset = offsets.getRootOffset(sMode);
        switch (cMode) {
            case control_mode_t::voltage_control:
                controlVoltage = controlBus->getVoltage(sD, sMode);
                roots[rootOffset] = std::min(Vmax - controlVoltage, controlVoltage - Vmin);
                break;
            case control_mode_t::MW_control:
                tap = sD.state[offset];
                updateLocalCache(noInputs, sD, sMode);
                roots[rootOffset] = std::min(Pmax - linkFlows.P1, linkFlows.P1 - Pmin);
                break;
            case control_mode_t::MVar_control:
                tap = sD.state[offset];
                updateLocalCache(noInputs, sD, sMode);
                roots[rootOffset] = std::min(Qmax - linkFlows.Q2, linkFlows.Q2 - Qmin);
                break;
            default:
                assert(false);
        }
    }

    void adjustableTransformer::rootTrigger(coreTime /*time*/,
                                            const IOdata& /*inputs*/,
                                            const std::vector<int>& /*rootMask*/,
                                            const solverMode& /*sMode*/)
    {
        double v1;
        switch (cMode) {
            case control_mode_t::voltage_control:
                //       v1 = controlBus->getVoltage();
                //            if (v1 > Vmax) {
                //          } else if (v1 < Vmin) {
                //         }
                break;
            case control_mode_t::MW_control:  // NOLINT

                updateLocalCache();
                //        if (linkFlows.P1 > Pmax) {
                //       } else if (linkFlows.P1 < Pmin) {
                //       }
                break;
            case control_mode_t::MVar_control:
                updateLocalCache();
                //       if (linkFlows.Q2 > Qmax) {
                //       } else if (linkFlows.Q2 < Qmin) {
                //       }
                break;
            default:
                assert(false);
        }
    }

    void adjustableTransformer::tapAnglePartial(index_t busId,
                                                const stateData& /*sD*/,
                                                matrixData<double>& md,
                                                const solverMode& sMode)
    {
        if (!(isEnabled())) {
            return;
        }

        double v1 = linkInfo.v1;
        double v2 = linkInfo.v2;

        double cosTheta1 = linkComp.cosTheta1;
        double sinTheta1 = linkComp.sinTheta1;
        double cosTheta2 = linkComp.cosTheta2;
        double sinTheta2 = linkComp.sinTheta2;

        double tvg = g / tap * v1 * v2;
        double tvb = b / tap * v1 * v2;

        auto offset = offsets.getAlgOffset(sMode);

        if ((busId == 2) || (busId == B2->getID())) {
            // dP2/dta
            double temp = tvg * sinTheta2 - tvb * cosTheta2;
            md.assign(PoutLocation, offset, temp);
            // dQ2/dta
            temp = -tvg * cosTheta2 - tvb * sinTheta2;
            md.assign(QoutLocation, offset, temp);
        } else {
            // dP1/dta
            double temp = -tvg * sinTheta1 + tvb * cosTheta1;
            md.assign(PoutLocation, offset, temp);
            // dQ1/dta
            temp = tvg * cosTheta1 - tvb * sinTheta1;
            md.assign(QoutLocation, offset, temp);
        }
    }

    void adjustableTransformer::tapPartial(index_t busId,
                                           const stateData& /*sD*/,
                                           matrixData<double>& md,
                                           const solverMode& sMode)
    {
        if (!(isEnabled())) {
            return;
        }

        double v1 = linkInfo.v1;
        double v2 = linkInfo.v2;

        double cosTheta1 = linkComp.cosTheta1;
        double sinTheta1 = linkComp.sinTheta1;
        double cosTheta2 = linkComp.cosTheta2;
        double sinTheta2 = linkComp.sinTheta2;

        double tvg = g / tap * v1 * v2;
        double tvb = b / tap * v1 * v2;

        auto offset = offsets.getAlgOffset(sMode);

        double P1 = (g + 0.5 * mp_G) / (tap * tap) * v1 * v1;
        P1 -= tvg * cosTheta1;
        P1 -= tvb * sinTheta1;

        double Q1 = -(b + 0.5 * mp_B) / (tap * tap) * v1 * v1;
        Q1 -= tvg * sinTheta1;
        Q1 += tvb * cosTheta1;

        if ((busId == 2) || (busId == B2->getID())) {
            // dP2/dtap
            double temp = tvg / tap * cosTheta2 + tvb / tap * sinTheta2;
            md.assign(PoutLocation, offset, temp);
            // dQ2/dtap
            temp = -tvg / tap * sinTheta2 - tvb / tap * cosTheta2;
            md.assign(QoutLocation, offset, temp);
        } else {
            // dP1/dtap
            double temp = -P1 / tap - (g + 0.5 * mp_G) / (tap * tap * tap) * v1 * v1;
            md.assign(PoutLocation, offset, temp);
            // dQ1/dtap
            temp = -Q1 / tap + (b + 0.5 * mp_B) / (tap * tap * tap) * v1 * v1;
            md.assign(QoutLocation, offset, temp);
        }
    }

    void adjustableTransformer::MWJac(const stateData& /*sD*/,
                                      matrixData<double>& md,
                                      const solverMode& sMode)
    {
        if (!(isEnabled())) {
            return;
        }

        double v1 = linkInfo.v1;
        double v2 = linkInfo.v2;

        double cosTheta1 = linkComp.cosTheta1;
        double sinTheta1 = linkComp.sinTheta1;

        double tvg = g / tap * v1 * v2;
        double tvb = b / tap * v1 * v2;

        auto offset = offsets.getAlgOffset(sMode);
        int B1Aoffset = B1->getOutputLoc(sMode, angleInLocation);
        int B2Aoffset = B2->getOutputLoc(sMode, angleInLocation);
        int B1Voffset = B1->getOutputLoc(sMode, voltageInLocation);
        int B2Voffset = B2->getOutputLoc(sMode, voltageInLocation);

        // compute the DP1/dta
        double temp = -tvg * sinTheta1 + tvb * cosTheta1;
        md.assign(offset, offset, temp);

        temp = tvg * sinTheta1 - tvb * cosTheta1;
        md.assignCheckCol(offset, B1Aoffset, temp);

        // dP1/dV1
        temp =
            -v2 * (g * cosTheta1 + b * sinTheta1) / tap + 2 * (g + mp_G * 0.5) / (tap * tap) * v1;
        md.assignCheckCol(offset, B1Voffset, temp);

        // dP1/dA2
        temp = -tvg * sinTheta1 - tvb * cosTheta1;
        md.assignCheckCol(offset, B2Aoffset, temp);

        // dP1/dV2
        temp = -v1 * (g * cosTheta1 + b * sinTheta1) / tap;
        md.assignCheckCol(offset, B2Voffset, temp);
    }

    void adjustableTransformer::MVarJac(const stateData& /*sD*/,
                                        matrixData<double>& md,
                                        const solverMode& sMode)
    {
        if (!(isEnabled())) {
            return;
        }

        double v1 = linkInfo.v1;
        double v2 = linkInfo.v2;

        // cosTheta1 = linkComp.cosTheta1;
        // sinTheta1 = linkComp.sinTheta1;
        auto cosTheta2 = linkComp.cosTheta2;
        auto sinTheta2 = linkComp.sinTheta2;

        double tvg = g / tap * v1 * v2;
        double tvb = b / tap * v1 * v2;

        auto offset = offsets.getAlgOffset(sMode);
        int B1Aoffset = B1->getOutputLoc(sMode, angleInLocation);
        int B2Aoffset = B2->getOutputLoc(sMode, angleInLocation);
        int B1Voffset = B1->getOutputLoc(sMode, voltageInLocation);
        int B2Voffset = B2->getOutputLoc(sMode, voltageInLocation);
        /*
    double P1 = (g + 0.5 * mp_G) / (tap * tap) * v1 * v1;
    P1 -= tvg * cosTheta1;
    P1 -= tvb * sinTheta1;

    double Q1 = -(b + 0.5 * mp_B) / (tap * tap) * v1 * v1;
    Q1 -= tvg * sinTheta1;
    Q1 += tvb * cosTheta1;
    */
        // compute the DQ2/dta
        double temp = -tvg / tap * sinTheta2 - tvb / tap * cosTheta2;
        md.assign(offset, offset, temp);

        // dQ2/dA1
        temp = tvg * cosTheta2 + tvb * sinTheta2;
        md.assignCheckCol(offset, B1Aoffset, temp);

        // dQ2/dV1
        temp = -v2 * (g * sinTheta2 - b * cosTheta2) / tap;
        md.assignCheckCol(offset, B1Voffset, temp);

        // dQ2/dA2
        temp = -tvg * cosTheta2 - tvb * sinTheta2;
        md.assignCheckCol(offset, B2Aoffset, temp);

        // dQ2/dV2
        temp = -2.0 * (b + 0.5 * mp_B) * v2 - g * v1 / tap * sinTheta2 + b * v1 / tap * cosTheta2;
        md.assignCheckCol(offset, B2Voffset, temp);
    }

    change_code adjustableTransformer::voltageControlAdjust()
    {
        auto ret = change_code::no_change;

        // check the voltage to make it is within the appropriate band
        double voltage = controlBus->getVoltage();
        if (!(opFlags[use_target_mode])) {
            if (voltage > Vmax) {
                tap = tap + direction * stepSize;
                ret = change_code::parameter_change;
                if (adjCount > 0) {
                    if (signn(prevAdjust) != signn(direction * stepSize)) {
                        oCount++;
                        if (oCount > 5) {
                            ret = change_code::no_change;
                        }
                    }
                }
                if (ret > change_code::no_change) {
                    prevAdjust = direction * stepSize;
                }
            } else if (voltage < Vmin) {
                tap = tap - direction * stepSize;
                ret = change_code::parameter_change;
                if (adjCount > 0) {
                    if (signn(prevAdjust) != signn(-direction * stepSize)) {
                        oCount++;
                        // we are giving the Vmin priority here so it will always err on protecting
                        // the low voltage side if it can. if it is oscillating and goes over Vmax
                        // it will end in a state that leaves it there.
                    }
                }
                if (ret > change_code::no_change) {
                    prevAdjust = -direction * stepSize;
                }
            }
            // check the taps to make sure they are within the appropriate range
            if (tap > maxTap) {
                tap = tap - prevAdjust;
                prevAdjust = 0;
                ret = change_code::no_change;
            }
            if (tap < minTap) {
                tap = tap - prevAdjust;
                prevAdjust = 0;
                ret = change_code::no_change;
            }
            prevValue = voltage;
        } else {
            double shift = 0;
            double dev = voltage - Vtarget;
            if (std::abs(dev) < stepSize / 2.0) {
                ret = change_code::no_change;
            } else {
                shift = direction * dev;
                if (shift > 0) {
                    shift = stepSize * round(shift / stepSize);
                } else {
                    shift = -stepSize * round(-shift / stepSize);
                }
                while (shift > maxTap - tap) {
                    shift = shift - stepSize;
                }
                while (shift < minTap - tap) {
                    shift = shift + stepSize;
                }
                tap = tap + shift;
                if (std::abs(shift) < stepSize) {
                    ret = change_code::no_change;
                } else {
                    ret = change_code::parameter_change;
                }
                if (adjCount > 0) {
                    if (std::abs(prevAdjust + shift) < stepSize / 2.0) {
                        oCount++;
                        if (oCount > 3) {
                            if (voltage > prevValue) {
                                ret = change_code::no_change;
                                tap = tap - shift;
                            }
                        }
                    }
                }
                if (ret > change_code::no_change) {
                    prevAdjust = shift;
                    prevValue = voltage;
                }
            }
        }
        return ret;
    }

    change_code adjustableTransformer::MWControlAdjust()  // NOLINT
    {
        auto ret = change_code::no_change;
        return ret;
    }

    change_code adjustableTransformer::MVarControlAdjust()  // NOLINT
    {
        auto ret = change_code::no_change;
        return ret;
    }

}  // namespace links
}  // namespace griddyn
