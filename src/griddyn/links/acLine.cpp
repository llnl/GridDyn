/*
 * Copyright (c) 2014-2026, Lawrence Livermore National Security
 * See the top-level NOTICE for additional details. All rights reserved.
 * SPDX-License-Identifier: BSD-3-Clause
 */

// headers
#include "acLine.h"

#include "../Area.h"
#include "../gridBus.h"
#include "../simulation/contingency.h"
#include "core/coreExceptions.h"
#include "core/coreObjectTemplates.hpp"
#include "core/objectFactoryTemplates.hpp"
#include "core/objectInterpreter.h"
#include "gmlc/utilities/stringOps.h"
#include "gmlc/utilities/vectorOps.hpp"
#include "utilities/matrixDataCompact.hpp"
#include <cmath>
#include <complex>
#include <cstring>
#include <string>
namespace griddyn {
using gmlc::utilities::solve2x2;
using units::convert;
using units::defunit;
using units::km;
using units::puMW;
using units::rad;
using units::unit;
// make the object factory types

// helper defines to have things make more sense
#define DEFAULTPOWERCOMP (this->*(flowCalc[0]))
#define MODEPOWERCOMP (this->*(flowCalc[getLinkApprox(sMode)]))
#define DERIVCOMP (this->*(derivCalc[getLinkApprox(sMode)]))
#define DEFAULTDERIVCOMP (this->*(derivCalc[0]))

// NOLINTNEXTLINE
acLine::acLine(const std::string& objName): Link(objName)
{
    // default values
    loadApproxFunctions();
    opFlags.set(network_connected);
}

// NOLINTNEXTLINE
acLine::acLine(double rP, double xP, const std::string& objName): Link(objName), r(rP), x(xP)
{
    // default values
    setAdmit();
    loadApproxFunctions();
    opFlags.set(network_connected);
    // load up the member function pointer array to point to the correct function
}

static typeFactory<acLine> glf("link", "tie");

coreObject* acLine::clone(coreObject* obj) const
{
    auto* lnk = cloneBaseFactory<acLine, Link>(this, obj, &glf);
    if (lnk == nullptr) {
        return obj;
    }

    lnk->length = length;
    lnk->r = r;
    lnk->x = x;
    lnk->mp_B = mp_B;
    lnk->mp_G = mp_G;
    lnk->g = g;
    lnk->b = b;
    lnk->tap = tap;
    lnk->tapAngle = tapAngle;
    lnk->minAngle = minAngle;
    lnk->maxAngle = maxAngle;
    return lnk;
}

void acLine::pFlowObjectInitializeB()
{
    updateLocalCache();
}
void acLine::pFlowCheck(std::vector<Violation>& Violation_vector)
{
    Link::pFlowCheck(Violation_vector);
    double angle = linkInfo.theta1;
    if (angle < minAngle) {
        Violation violation;
        violation.m_objectName = getName();
        violation.violationCode = MINIMUM_ANGLE_EXCEEDED;
        violation.level = angle;
        violation.limit = minAngle;
        if (minAngle != 0.0) {
            violation.percentViolation = (minAngle - angle) / std::abs(minAngle) * 100;
        } else {
            violation.percentViolation = 100.0;
        }

        Violation_vector.push_back(violation);
    } else if (angle > maxAngle) {
        Violation violation;
        violation.m_objectName = getName();
        violation.violationCode = MAXIMUM_ANGLE_EXCEEDED;
        violation.level = angle;
        violation.limit = maxAngle;
        if (maxAngle != 0.0) {
            violation.percentViolation = (angle - maxAngle) / std::abs(maxAngle) * 100.0;
        } else {
            violation.percentViolation = 100.0;
        }

        Violation_vector.push_back(violation);
    }
}

void acLine::switchChange(int switchNum)
{
    if (switchNum == 1) {
        if ((linkInfo.v1 < 0.3) || (fault > 0.0)) {
            alert(this, POTENTIAL_FAULT_CHANGE);
        }
    } else {
        if ((linkInfo.v2 < 0.3) || (fault > 0.0)) {
            alert(this, POTENTIAL_FAULT_CHANGE);
        }
    }
}

double acLine::quickupdateP()
{
    linkComp.Vmx = linkInfo.v1 * linkInfo.v2 / tap;
    linkFlows.P1 = (g + 0.5 * mp_G) / (tap * tap) * linkInfo.v1 * linkInfo.v1 -
        g * linkComp.Vmx * (1 - linkInfo.theta1) - b * linkComp.Vmx * linkInfo.theta1;
    linkFlows.P2 = (g + 0.5 * mp_G) * linkInfo.v2 * linkInfo.v2 -
        g * linkComp.Vmx * (1 - linkInfo.theta2) - b * linkComp.Vmx * linkInfo.theta2;
    return linkFlows.P1;
}

void acLine::timestep(const coreTime time, const IOdata& /*inputs*/, const solverMode& /*sMode*/)
{
    if (!isEnabled()) {
        return;
    }

    updateLocalCache();
    prevTime = time;
    /*if (scheduled)
    {
    Psched=sched->timestepP(time);
    }*/
}

void acLine::checkMerge() {}

static const stringVec locNumStrings{"r",
                                     "x",
                                     "link",
                                     "b",
                                     "g",
                                     "tap",
                                     "tapangle",
                                     "switch1",
                                     "switch2",
                                     "fault",
                                     "p"};
static const stringVec locStrStrings{"from", "to"};
static const stringVec flagStrings{};
void acLine::getParameterStrings(stringVec& pstr, paramStringType pstype) const
{
    getParamString<acLine, gridComponent>(
        this, pstr, locNumStrings, locStrStrings, flagStrings, pstype);
}

// set properties
void acLine::set(const std::string& param, const std::string& val)
{
    if (param == "approximation") {
        if (val == "auto") {
            loadApproxFunctions();
        } else if (val == "none") {
            for (int kk = 0; kk < APPROXIMATION_LEVELS; ++kk) {
                flowCalc[kk] = &acLine::fullCalc;
                derivCalc[kk] = &acLine::fullDeriv;
            }
        } else if (val == "simplified") {
            for (int kk = 0; kk < APPROXIMATION_LEVELS; ++kk) {
                flowCalc[kk] = &acLine::simplifiedCalc;
                derivCalc[kk] = &acLine::simplifiedDeriv;
            }
        } else if (val == "decoupled") {
            for (int kk = 0; kk < APPROXIMATION_LEVELS; ++kk) {
                flowCalc[kk] = &acLine::decoupledCalc;
                derivCalc[kk] = &acLine::decoupledDeriv;
            }
        } else if (val == "simplified_decoupled") {
            for (int kk = 0; kk < APPROXIMATION_LEVELS; ++kk) {
                flowCalc[kk] = &acLine::simplifiedDecoupledCalc;
                derivCalc[kk] = &acLine::simplifiedDecoupledDeriv;
            }
        } else if (val == "small_angle_decoupled") {
            for (int kk = 0; kk < APPROXIMATION_LEVELS; ++kk) {
                flowCalc[kk] = &acLine::smallAngleDecoupledCalc;
                derivCalc[kk] = &acLine::smallAngleDecoupledDeriv;
            }
        } else if (val == "simplified_small_angle") {
            for (int kk = 0; kk < APPROXIMATION_LEVELS; ++kk) {
                flowCalc[kk] = &acLine::smallAngleSimplifiedCalc;
                derivCalc[kk] = &acLine::smallAngleSimplifiedDeriv;
            }
        } else if (val == "small_angle") {
            for (int kk = 0; kk < APPROXIMATION_LEVELS; ++kk) {
                flowCalc[kk] = &acLine::smallAngleCalc;
                derivCalc[kk] = &acLine::smallAngleDeriv;
            }
        } else if (val == "linear") {
            for (int kk = 0; kk < APPROXIMATION_LEVELS; ++kk) {
                flowCalc[kk] = &acLine::linearCalc;
                derivCalc[kk] = &acLine::linearDeriv;
            }
        } else if (val == "fastdecoupled") {
            for (int kk = 0; kk < APPROXIMATION_LEVELS; ++kk) {
                flowCalc[kk] = &acLine::fastDecoupledCalc;
                derivCalc[kk] = &acLine::fastDecoupledDeriv;
            }
        }
    } else {
        Link::set(param, val);
    }
}

void acLine::set(const std::string& param, double val, unit unitType)
{
    if (param.length() == 1) {
        switch (param[0]) {
            case 'r':
                r = val;
                // set line admittance
                setAdmit();
                break;
            case 'x':
                x = val;
                // set line admittance
                setAdmit();
                break;
            case 'b':
                mp_B = val;
                break;
            case 'g':
                mp_G = val;
                break;
            case 'p':
                Pset = convert(val, unitType, puMW, systemBasePower);
                opFlags.set(fixed_target_power);
                break;
            default:
                throw(unrecognizedParameter(param));
        }
        return;
    }
    std::string outparam;
    gmlc::utilities::stringOps::trailingStringInt(param, outparam, 1);
    if (outparam == "length") {
        length = convert(val, unitType, km);
    } else if ((outparam == "tap") || (param == "ratio")) {
        tap = val;
    } else if (outparam == "tapangle") {
        tapAngle = convert(val, unitType, rad);
    } else if (outparam == "fault") {
        double temp = val;
        if (unitType != defunit) {
            if (length > 0.0) {
                temp = convert(val, unitType, km);
                temp = temp / length;
            }
        }
        if ((fault > 0.0) && (fault < 1.0)) {
            if ((!opFlags[switch1_open_flag]) || (!opFlags[switch2_open_flag])) {
                alert(this, POTENTIAL_FAULT_CHANGE);
            }
            if ((temp < 0.0) || (temp > 1.0)) {
                LOG_NORMAL("fault cleared");
            }
        } else if ((temp > 0.0) && (temp < 1.0)) {
            LOG_NORMAL("Line fault at " + std::to_string(temp) + " of line");
        }

        fault = ((temp < 1.0) && (temp > 0.0)) ?
            temp :
            (-1.0);  // fault must have some value between 0 and 1 and cannot be 0 or 1
    } else if (outparam == "minangle") {
        minAngle = convert(val, unitType, rad);
    } else if (outparam == "maxangle") {
        maxAngle = convert(val, unitType, rad);
    } else {
        Link::set(param, val, unitType);
    }
}

double acLine::get(const std::string& param, unit unitType) const
{
    double val = kNullVal;
    if (param.length() == 1) {
        switch (param[0]) {
            case 'r':
                val = r;
                break;
            case 'x':
                val = x;
                break;
            case 'b':
                val = mp_B;
                break;
            case 'g':
                val = mp_G;
                break;
            case 'z':
                val = std::hypot(r, x);
                break;
            case 'i':
                val = getCurrent();
                break;
            default:
                break;
        }
        return val;
    }
    std::string outparam;
    gmlc::utilities::stringOps::trailingStringInt(param, outparam, 1);
    if (outparam == "impedance") {
        val = std::hypot(r, x);
    } else if (outparam == "tap") {
        val = tap;
    } else if (outparam == "tapangle") {
        val = tapAngle;
    } else {
        val = Link::get(param, unitType);
    }
    return val;
}

int acLine::fixRealPower(double power,
                         id_type_t measureTerminal,
                         id_type_t fixedTerminal,
                         unit unitType)
{
    Pset = convert(power, unitType, puMW, systemBasePower);
    updateLocalCache();
    double ang = asin(Pset / b / linkComp.Vmx);
    if (!std::isnormal(ang)) {
        return 0;
    }
    if (fixedTerminal == 0) {
        // NOLINTNEXTLINE
        if (measureTerminal == 1) {
        } else {
        }
        // TODO(PT) automatically figure out appropriate terminal to adjust
    } else if ((fixedTerminal == 1) || (isSameObject(fixedTerminal, B1))) {
        double newAng = B1->getAngle() - ang - tapAngle;
        B2->set("angle", newAng);
    } else if ((fixedTerminal == 2) || (isSameObject(fixedTerminal, B2))) {
        double newAng = ang + B2->getAngle() - tapAngle;
        B1->set("angle", newAng);
    } else {
        return 0;
    }
    opFlags.set(fixed_target_power);
    return 1;
}

static IOlocs aLoc{0, 1};

int acLine::fixPower(double rPower,
                     double qPower,
                     id_type_t measureTerminal,
                     id_type_t fixedTerminal,
                     units::unit unitType)
{
    double valp = convert(rPower, unitType, puMW, systemBasePower);
    double valq = convert(qPower, unitType, puMW, systemBasePower);
    opFlags.set(fixed_target_power);
    double v1 = B1->getVoltage();
    double v2 = B2->getVoltage();
    double ang = 0;
    double atol = 1e-7;
    double vtol = 1e-7;
    int ret = 0;
    if (isSameObject(measureTerminal, B1) || (measureTerminal <= 1)) {
        measureTerminal = 1;
        atol = B1->get("atol") / 2;
        vtol = B1->get("vtol") / 2;
    } else if (isSameObject(measureTerminal, B2) || (measureTerminal == 2)) {
        measureTerminal = 2;
        atol = B2->get("atol") / 2;
        vtol = B2->get("vtol") / 2;
    }
    if (atol < 0) {
        atol = 1e-5;
    }
    if (vtol < 0) {
        vtol = 1e-5;
    } else if (measureTerminal > 2) {
        LOG_WARNING("invalid measure terminal identification");
        return ret;
    }

    if (isSameObject(fixedTerminal, B1))  // trying to convert to 1 or 2 so don't need to check that
    {
        fixedTerminal = 1;
    } else if (isSameObject(fixedTerminal, B2)) {
        fixedTerminal = 2;
    } else if (fixedTerminal == 0) {
        // Just go by the greater type number
        // 3 is fixed 0 is PQ 2 is PV,  if they are tied go with bus 1
        // might be a bit odd in the case of comparing afix with PV but
        fixedTerminal = (static_cast<int>(B2->getType()) > static_cast<int>(B1->getType())) ? 2 : 1;
    } else if (measureTerminal > 2) {
        LOG_WARNING("invalid fixed terminal identification");
        return ret;
    }
    ang = asin(-valp / b / (v1 * v2 / tap));
    if (measureTerminal == 1) {
        if (fixedTerminal == 1) {
            v2 = -(-valq - b * v1 * v1 / (tap * tap)) * tap / (b * v1 * cos(ang));
        } else {
            v1 = std::sqrt(-(valq - b * v1 * v2 / tap * cos(ang)) * tap * tap / b);
        }
    } else {
        if (fixedTerminal == 2) {
            v1 = -(valq - b * v2 * v2 / (tap * tap)) * tap / (b * v2 * cos(ang));
        } else {
            v2 = std::sqrt(-(valq - b * v1 * v2 / tap * cos(ang)) / b);
        }
    }
    linkInfo.v1 = v1;
    linkInfo.v2 = v2;
    ang = asin(-valp / b / (v1 * v2 / tap));
    if (measureTerminal == 1) {
        linkInfo.theta1 = ang;
        linkInfo.theta2 = -ang;
    } else {
        linkInfo.theta1 = -ang;
        linkInfo.theta2 = ang;
    }
    linkComp.Vmx = linkInfo.v1 * linkInfo.v2 / tap;
    DEFAULTPOWERCOMP();
    // basePowerComp ();
    double err = (measureTerminal == 1) ?
        (std::abs(linkFlows.P1 - valp) + std::abs(linkFlows.Q1 - valq)) :
        (std::abs(linkFlows.P2 - valp) + std::abs(linkFlows.Q2 - valq));
    double pErr = err;

    matrixDataCompact<2, 2> md;
    double dP;
    double dQ;
    double dA;
    double dV;
    double Pvii;
    double Ptii;
    double Qvii;
    double Qtii;
    bool aboveTol = ((err > atol) || (err > vtol));

    while (aboveTol) {
        md.clear();
        if (measureTerminal == fixedTerminal) {
            outputPartialDerivatives(measureTerminal, emptyStateData, md, cLocalSolverMode);
        } else {
            ioPartialDerivatives(measureTerminal, emptyStateData, md, aLoc, cLocalSolverMode);
        }
        if (measureTerminal == 1) {
            dP = valp - linkFlows.P1;
            dQ = valq - linkFlows.Q1;
        } else {
            dP = valp - linkFlows.P2;
            dQ = valq - linkFlows.Q2;
        }
        // printf("A dP=%f dQ=%f\n",dP,dQ);
        Pvii = md.at(PoutLocation, voltageInLocation);
        Ptii = md.at(PoutLocation, angleInLocation);
        Qvii = md.at(QoutLocation, voltageInLocation);
        Qtii = md.at(QoutLocation, angleInLocation);
        double detA = solve2x2(Pvii, Ptii, Qvii, Qtii, dP, dQ, dV, dA);
        if (!(std::isnormal(detA))) {
            break;
        }

        if (fixedTerminal == 1) {
            v2 += dV;
            linkInfo.v2 = v2;
        } else {
            v1 += dV;
            linkInfo.v1 = v1;
        }
        if (measureTerminal == 1) {
            if (fixedTerminal == 1) {
                ang -= dA;
            } else {
                ang += dA;
            }
            linkInfo.theta1 = ang;
            linkInfo.theta2 = -ang;
        } else {
            if (fixedTerminal == 1) {
                ang += dA;
            } else {
                ang -= dA;
            }
            linkInfo.theta1 = -ang;
            linkInfo.theta2 = ang;
        }
        // update the Vmx term
        linkComp.Vmx = linkInfo.v1 * linkInfo.v2 / tap;
        DEFAULTPOWERCOMP();
        if (measureTerminal == 1) {
            dP = valp - linkFlows.P1;
            dQ = valq - linkFlows.Q1;
        } else {
            dP = valp - linkFlows.P2;
            dQ = valq - linkFlows.Q2;
        }
        // printf("B dP=%f dQ=%f\n", dP, dQ);

        if ((std::abs(dP) <= atol) && (std::abs(dQ) <= vtol)) {
            aboveTol = false;
        } else {
            err = std::abs(dP) + std::abs(dQ);
            if (err >= pErr) {
                LOG_WARNING("convergence break increasing");
                break;
            }
            pErr = err;
        }
    }
    if (ang > kPI) {
        ang -= 2 * kPI;
    }
    if (ang < -kPI) {
        ang += 2 * kPI;
    }
    if (std::abs(ang) > kPI / 2) {
        LOG_WARNING("large angle");
    }
    if (fixedTerminal == 2) {
        double newAng = (measureTerminal == 2) ? (B2->getAngle() - ang + tapAngle) :
                                                 (ang + B2->getAngle() + tapAngle);

        B1->set("angle", newAng);
        B1->set("voltage", v1);
        ret = B1->propogatePower(false);
    } else {
        if (v2 > 1.5) {
            LOG_WARNING("high voltage");
        }
        double newAng = (measureTerminal == 1) ? (B1->getAngle() - ang - tapAngle) :
                                                 (ang + B1->getAngle() - tapAngle);
        B2->set("angle", newAng);
        B2->set("voltage", v2);
        ret = B2->propogatePower(false);
    }

    updateLocalCache();
    /*
    if (measureTerminal == 1) {
        err = std::abs(linkFlows.P1 - valp) + std::abs(linkFlows.Q1 - valq);
    } else {
        err = std::abs(linkFlows.P2 - valp) + std::abs(linkFlows.Q2 - valq);
    }
    */
    return ret;
}

void acLine::ioPartialDerivatives(id_type_t busId,
                                  const stateData& /*sD*/,
                                  matrixData<double>& md,
                                  const IOlocs& inputLocs,
                                  const solverMode& sMode)
{
    // check if line is enabled

    if (!(isEnabled())) {
        return;
    }
    if ((LinkDeriv.seqID != linkInfo.seqID) || (linkInfo.seqID == 0)) {
        if (fault >= 0) {
            faultDeriv();
        } else if (isConnected()) {
            DERIVCOMP();
        } else {
            swOpenDeriv();
        }
    }
    auto voltageLoc = inputLocs[voltageInLocation];
    auto angleLoc = inputLocs[angleInLocation];

    if ((busId == 2) || (busId == B2->getID())) {
        if (!opFlags[switch2_open_flag]) {
            if (voltageLoc != kNullLocation) {
                md.assign(PoutLocation, voltageLoc, LinkDeriv.dP2dv2);
                md.assign(QoutLocation, voltageLoc, LinkDeriv.dQ2dv2);
            }
            if (angleLoc != kNullLocation) {
                md.assign(PoutLocation, angleLoc, LinkDeriv.dP2dt2);
                md.assign(QoutLocation, angleLoc, LinkDeriv.dQ2dt2);
            }
        }
    } else {
        if (!opFlags[switch1_open_flag]) {
            if (voltageLoc != kNullLocation) {
                md.assign(PoutLocation, voltageLoc, LinkDeriv.dP1dv1);
                md.assign(QoutLocation, voltageLoc, LinkDeriv.dQ1dv1);
            }
            if (angleLoc != kNullLocation) {
                md.assign(PoutLocation, angleLoc, LinkDeriv.dP1dt1);

                md.assign(QoutLocation, angleLoc, LinkDeriv.dQ1dt1);
            }
        }
    }
}

void acLine::outputPartialDerivatives(const IOdata& /*inputs*/,
                                      const stateData& /*sD*/,
                                      matrixData<double>& /*md*/,
                                      const solverMode& /*sMode*/)
{
    // there are theoretically 4 outputs for a standard ac line,  but no internal states therefore
    // if this function is called from an external entity there are no output partial derivatives
}

void acLine::outputPartialDerivatives(id_type_t busId,
                                      const stateData& /*sD*/,
                                      matrixData<double>& md,
                                      const solverMode& sMode)
{
    if (!isConnected()) {  // if there is no connection there is no coupling
        return;
    }

    if (fault >= 0) {  // if there is a fault there is no coupling
        return;
    }

    if ((LinkDeriv.seqID != linkInfo.seqID) || (linkInfo.seqID == 0)) {
        DERIVCOMP();
    }

    index_t B1Voffset = voltageInLocation;
    index_t B2Voffset = voltageInLocation;
    index_t B1Aoffset = angleInLocation;
    index_t B2Aoffset = angleInLocation;

    if (!isLocal(sMode)) {
        B1Voffset = B1->getOutputLoc(sMode, voltageInLocation);
        B2Voffset = B2->getOutputLoc(sMode, voltageInLocation);
        B1Aoffset = B1->getOutputLoc(sMode, angleInLocation);
        B2Aoffset = B2->getOutputLoc(sMode, angleInLocation);
    }

    if ((busId == 2) || (busId == B2->getID())) {
        if (B1Voffset != kNullLocation) {
            md.assign(PoutLocation, B1Voffset, LinkDeriv.dP2dv1);
            // reactive power vs Voltage
            md.assign(QoutLocation, B1Voffset, LinkDeriv.dQ2dv1);
        }
        if (B1Aoffset != kNullLocation) {
            // power vs angle
            md.assign(PoutLocation, B1Aoffset, LinkDeriv.dP2dt1);
            // reactive power vs Angle
            md.assign(QoutLocation, B1Aoffset, LinkDeriv.dQ2dt1);
        }
    } else {
        if (B2Voffset != kNullLocation) {
            md.assign(PoutLocation, B2Voffset, LinkDeriv.dP1dv2);
            // reactive power vs Voltage
            md.assign(QoutLocation, B2Voffset, LinkDeriv.dQ1dv2);
        }
        if (B2Aoffset != kNullLocation) {
            // power vs angle
            md.assign(PoutLocation, B2Aoffset, LinkDeriv.dP1dt2);
            // reactive power vs Angle
            md.assign(QoutLocation, B2Aoffset, LinkDeriv.dQ1dt2);
        }
    }
}

count_t acLine::outputDependencyCount(index_t /*num*/, const solverMode& /*sMode*/) const
{
    return 2;
}
// set admittance values y := g + jb
void acLine::setAdmit()
{
    auto y2 = 1.0 / (r * r + x * x);
    g = r * y2;
    b = -x * y2;
}

void acLine::disable()
{
    if (!isEnabled()) {
        return;
    }
    Link::disable();
    std::memset(&linkFlows, 0, sizeof(linkF));
    std::memset(&LinkDeriv, 0, sizeof(linkPart));
}

double acLine::getMaxTransfer() const
{
    if (!isConnected()) {
        return 0;
    }
    if (Erating > 0) {
        return Erating;
    }
    if (ratingB > 0) {
        return ratingB;
    }
    if (ratingA > 0) {
        return ratingA;
    }

    return (std::abs(b / tap));
}

void acLine::setState(coreTime time,
                      const double state[],
                      const double dstate_dt[],
                      const solverMode& sMode)
{
    prevTime = time;
    stateData sD(time, state, dstate_dt);

    if (sMode.approx[decoupled]) {  // recompute power with new state updates for the decoupled
                                    // system
        updateLocalCache(noInputs, sD, sMode);
        constLinkInfo = linkInfo;  // update the constant linkInfo
        constLinkComp = linkComp;
        linkInfo.seqID = 0;
        // update the cache twice to get the correct values with the decoupled mode
        updateLocalCache(noInputs, sD, sMode);
    } else if (sMode.approx[linear]) {
        // reLinearize at each step
        loadLinkInfo(sD, sMode);
        if (!isConnected()) {
            if (fault >= 0) {
                faultCalc();
            }
            return;
        }

        if (fault >= 0) {
            faultCalc();
        } else {
            DEFAULTPOWERCOMP();
        }
        DEFAULTDERIVCOMP();
        constLinkInfo = linkInfo;  // update the constant linkInfo
        constLinkComp = linkComp;
    } else {  // the other states are normal
        updateLocalCache(noInputs, sD, sMode);
        constLinkInfo = linkInfo;  // update the constant linkInfo
        constLinkComp = linkComp;
    }
    constLinkFlows = linkFlows;  // update the constant linkFlows
}

double acLine::getAngle(const double state[], const solverMode& sMode) const
{
    double t1 = B1->getAngle(state, sMode);
    double t2 = B2->getAngle(state, sMode);
    return t1 - t2 - tapAngle;
}

change_code
    acLine::powerFlowAdjust(const IOdata& /*inputs*/, std::uint32_t /*flags*/, check_level_t level)
{
    if ((level == check_level_t::high_angle_trip) && (isConnected())) {
        if (std::abs(linkInfo.theta1) > kPI / 2.0 + 0.01) {
            disconnect();
            return change_code::jacobian_change;
        }
    }
    return change_code::no_change;
}

change_code acLine::rootCheck(const IOdata& /*inputs*/,
                              const stateData& sD,
                              const solverMode& sMode,
                              check_level_t level)
{
    auto ret = change_code::no_change;
    if (level == check_level_t::complete_state_check) {
        updateLocalCache(noInputs, sD, sMode);
        if (std::abs(linkInfo.theta1) > maxAngle) {
            LOG_WARNING("max angle 1 exceeded");
        } else if (std::abs(linkInfo.theta2) > maxAngle) {
            LOG_WARNING("max angle 2 exceeded");
        }
    }
    return ret;
}
void acLine::updateLocalCache(const IOdata& /*inputs*/,
                              const stateData& sD,
                              const solverMode& sMode)
{
    if (!isEnabled()) {
        return;
    }
    if (!sD.updateRequired(linkInfo.seqID)) {
        return;  // already computed
    }
    loadLinkInfo(sD, sMode);
    // set everything to 0

    if (fault >= 0) {
        faultCalc();
    } else {
        if (isConnected()) {
            MODEPOWERCOMP();
        } else {
            swOpenCalc();
        }
    }
}

bool acLine::testAndTrip(int tripLevel)
{
    if (!isConnected()) {
        return false;
    }
    if (tripLevel >= 1) {
        if (std::abs(linkInfo.theta1) > maxAngle) {
            disconnect();
            return true;
        } else if (std::abs(linkInfo.theta2) > maxAngle) {
            disconnect();
            return true;
        }
    }
    if (tripLevel >= 2) {
        if (checkFlag(angle_slip_on_test)) {
            disconnect();
            return true;
        }
    }
    return false;
}
void acLine::updateLocalCache()
{
    // set everything to 0
    if (!isEnabled()) {
        return;
    }
    loadLinkInfo();

    if (fault >= 0) {
        faultCalc();
    } else {
        if (isConnected()) {
            DEFAULTPOWERCOMP();
        } else {
            swOpenCalc();
        }
    }
}

void acLine::faultCalc()
{
    if (opFlags[switch1_open_flag]) {
        linkFlows.P1 = 0;
        linkFlows.Q1 = 0;
    } else {
        linkFlows.P1 = (g / fault + fault * mp_G) / (tap * tap) * linkInfo.v1 * linkInfo.v1;

        linkFlows.Q1 = -(b / fault + fault * mp_B) / (tap * tap) * linkInfo.v1 * linkInfo.v1;
    }
    if (opFlags[switch2_open_flag]) {
        linkFlows.P2 = 0;
        linkFlows.Q2 = 0;
    } else {
        linkFlows.P2 = (g / (1.0 - fault) + (1.0 - fault) * mp_G) * linkInfo.v2 * linkInfo.v2;

        linkFlows.Q2 = -(b / (1.0 - fault) + (1.0 - fault) * mp_B) * linkInfo.v2 * linkInfo.v2;
    }
}

void acLine::loadLinkInfo()
{
    if (isConnected()) {
        linkInfo.v1 = B1->getVoltage();
        linkInfo.v2 = B2->getVoltage();
        linkInfo.theta1 = B1->getAngle() - B2->getAngle() - tapAngle;
        linkInfo.theta2 = -linkInfo.theta1;

        linkComp.sinTheta1 = sin(linkInfo.theta1);
        linkComp.cosTheta1 = cos(linkInfo.theta1);
        linkComp.sinTheta2 = -linkComp.sinTheta1;
        linkComp.cosTheta2 = linkComp.cosTheta1;

        linkComp.Vmx = linkInfo.v1 * linkInfo.v2 / tap;
    } else {
        linkInfo.theta1 = 0.0;
        linkInfo.theta2 = 0.0;
        if (!opFlags[switch1_open_flag]) {
            linkInfo.v1 = B1->getVoltage();
        }
        if (!opFlags[switch2_open_flag]) {
            linkInfo.v2 = B2->getVoltage();
        }
        linkComp.sinTheta1 = 0.0;
        linkComp.cosTheta1 = 1.0;
        linkComp.sinTheta2 = 0.0;
        linkComp.cosTheta2 = 1.0;
        return;
    }

    linkInfo.seqID = 0;
    constLinkInfo = linkInfo;  // update the constant linkInfo
    constLinkComp = linkComp;
}

void acLine::loadLinkInfo(const stateData& sD, const solverMode& sMode)
{
    if ((linkInfo.seqID == sD.seqID) && (sD.seqID != 0)) {
        return;
    }
    // std::memset (&linkInfo, 0, sizeof(linkI));
    linkInfo.v1 = B1->getVoltage(sD, sMode);

    linkInfo.v2 = B2->getVoltage(sD, sMode);

    linkInfo.theta1 = B1->getAngle(sD, sMode) - B2->getAngle(sD, sMode) - tapAngle;
    if (std::fabs(linkInfo.theta1) > kPI / 2.0) {
        if (isConnected()) {
            opFlags.set(angle_slip_on_test);
        }
    }
    linkInfo.theta2 = -linkInfo.theta1;
    linkComp.Vmx = linkInfo.v1 * linkInfo.v2 / tap;
    linkInfo.seqID = sD.seqID;
    // don't compute the trig functions yet as that may not be necessary
}

void acLine::fullCalc()
{
    // compute the trig functions
    linkComp.sinTheta1 = sin(linkInfo.theta1);
    linkComp.cosTheta1 = cos(linkInfo.theta1);
    linkComp.sinTheta2 = -linkComp.sinTheta1;
    linkComp.cosTheta2 = linkComp.cosTheta1;

    double vsq = linkInfo.v1 * linkInfo.v1 / (tap * tap);
    double tempc = linkComp.Vmx * linkComp.cosTheta1;
    double temps = linkComp.Vmx * linkComp.sinTheta1;
    // flows from bus 1 to bus 2
    linkFlows.P1 = (g + 0.5 * mp_G) * vsq - g * tempc - b * temps;
    linkFlows.Q1 = -(b + 0.5 * mp_B) * vsq - g * temps + b * tempc;

    // flows from bus 2 to bus 1

    vsq = linkInfo.v2 * linkInfo.v2;
    tempc = linkComp.Vmx * linkComp.cosTheta2;
    temps = linkComp.Vmx * linkComp.sinTheta2;

    linkFlows.P2 = (g + 0.5 * mp_G) * vsq - g * tempc - b * temps;
    linkFlows.Q2 = -(b + 0.5 * mp_B) * vsq - g * temps + b * tempc;

    linkFlows.seqID = linkInfo.seqID;
}

void acLine::simplifiedCalc()
{
    // compute the trig functions
    linkComp.sinTheta1 = sin(linkInfo.theta1);
    linkComp.cosTheta1 = cos(linkInfo.theta1);
    linkComp.sinTheta2 = -linkComp.sinTheta1;
    linkComp.cosTheta2 = linkComp.cosTheta1;

    // flows from bus 1 to bus 2
    linkFlows.P1 = -b * linkComp.Vmx * linkComp.sinTheta1;

    linkFlows.Q1 = -(b + 0.5 * mp_B) / (tap * tap) * linkInfo.v1 * linkInfo.v1;
    linkFlows.Q1 += b * linkComp.Vmx * linkComp.cosTheta1;
    // flows from bus 2 to bus 1
    linkFlows.P2 = -b * linkComp.Vmx * linkComp.sinTheta2;
    linkFlows.Q2 = -(b + 0.5 * mp_B) * linkInfo.v2 * linkInfo.v2;
    linkFlows.Q2 += b * linkComp.Vmx * linkComp.cosTheta2;
    linkFlows.seqID = linkInfo.seqID;
}

void acLine::decoupledCalc()
{
    linkComp.sinTheta1 = sin(linkInfo.theta1);
    linkComp.cosTheta1 = cos(linkInfo.theta1);
    linkComp.sinTheta2 = -linkComp.sinTheta1;
    linkComp.cosTheta2 = linkComp.cosTheta1;

    linkFlows.P1 = (g + 0.5 * mp_G) / (tap * tap) * constLinkInfo.v1 * constLinkInfo.v1;
    linkFlows.P1 -= g * constLinkComp.Vmx * linkComp.cosTheta1;
    linkFlows.P1 -= b * constLinkComp.Vmx * linkComp.sinTheta1;

    linkFlows.Q1 = -(b + 0.5 * mp_B) / (tap * tap) * linkInfo.v1 * linkInfo.v1;
    linkFlows.Q1 -= g * linkComp.Vmx * constLinkComp.sinTheta1;
    linkFlows.Q1 += b * linkComp.Vmx * constLinkComp.cosTheta1;

    linkFlows.P2 = (g + 0.5 * mp_G) * constLinkInfo.v2 * constLinkInfo.v2;
    linkFlows.P2 -= g * constLinkComp.Vmx * linkComp.cosTheta2;
    linkFlows.P2 -= b * constLinkComp.Vmx * linkComp.sinTheta2;

    linkFlows.Q2 = -(b + 0.5 * mp_B) * linkInfo.v2 * linkInfo.v2;
    linkFlows.Q2 -= g * linkComp.Vmx * constLinkComp.sinTheta2;
    linkFlows.Q2 += b * linkComp.Vmx * constLinkComp.cosTheta2;
    linkFlows.seqID = linkInfo.seqID;
}

void acLine::smallAngleCalc()
{
    // compute the trig functions
    linkComp.sinTheta1 = linkInfo.theta1;
    linkComp.cosTheta1 = 1.0;
    linkComp.sinTheta2 = linkInfo.theta2;
    linkComp.cosTheta2 = 1.0;

    // flows from bus 1 to bus 2
    linkFlows.P1 = (g + 0.5 * mp_G) / (tap * tap) * linkInfo.v1 * linkInfo.v1;
    linkFlows.P1 -= g * linkComp.Vmx;
    linkFlows.P1 -= b * linkComp.Vmx * linkComp.sinTheta1;

    linkFlows.Q1 = -(b + 0.5 * mp_B) / (tap * tap) * linkInfo.v1 * linkInfo.v1;
    linkFlows.Q1 -= g * linkComp.Vmx * linkComp.sinTheta1;
    linkFlows.Q1 += b * linkComp.Vmx;

    // flows from bus 2 to bus 1
    linkFlows.P2 = (g + 0.5 * mp_G) * linkInfo.v2 * linkInfo.v2;
    linkFlows.P2 -= g * linkComp.Vmx;
    linkFlows.P2 -= b * linkComp.Vmx * linkComp.sinTheta2;

    linkFlows.Q2 = -(b + 0.5 * mp_B) * linkInfo.v2 * linkInfo.v2;
    linkFlows.Q2 -= g * linkComp.Vmx * linkComp.sinTheta2;
    linkFlows.Q2 += b * linkComp.Vmx;
    linkFlows.seqID = linkInfo.seqID;
}

void acLine::smallAngleSimplifiedCalc()
{
    // compute the trig functions
    linkComp.sinTheta1 = linkInfo.theta1;
    linkComp.cosTheta1 = 1.0;
    linkComp.sinTheta2 = linkInfo.theta2;
    linkComp.cosTheta2 = 1.0;

    // flows from bus 1 to bus 2
    linkFlows.P1 = -b * linkComp.Vmx * linkComp.sinTheta1;

    linkFlows.Q1 = -(b + 0.5 * mp_B) / (tap * tap) * linkInfo.v1 * linkInfo.v1;
    linkFlows.Q1 += b * linkComp.Vmx;
    // flows from bus 2 to bus 1
    linkFlows.P2 = -b * linkComp.Vmx * linkComp.sinTheta2;
    linkFlows.Q2 = -(b + 0.5 * mp_B) * linkInfo.v2 * linkInfo.v2;
    linkFlows.Q2 += b * linkComp.Vmx;
    linkFlows.seqID = linkInfo.seqID;
}

void acLine::simplifiedDecoupledCalc()
{
    linkComp.sinTheta1 = sin(linkInfo.theta1);
    linkComp.cosTheta1 = cos(linkInfo.theta1);
    linkComp.sinTheta2 = -linkComp.sinTheta1;
    linkComp.cosTheta2 = linkComp.cosTheta1;

    linkFlows.P1 = -b * constLinkComp.Vmx * linkComp.sinTheta1;

    linkFlows.Q1 = -(b + 0.5 * mp_B) / (tap * tap) * linkInfo.v1 * linkInfo.v1;
    linkFlows.Q1 += b * linkComp.Vmx * constLinkComp.cosTheta1;

    linkFlows.P2 = -b * constLinkComp.Vmx * linkComp.sinTheta2;

    linkFlows.Q2 = -(b + 0.5 * mp_B) * linkInfo.v2 * linkInfo.v2;
    linkFlows.Q2 += b * linkComp.Vmx * constLinkComp.cosTheta2;
    linkFlows.seqID = linkInfo.seqID;
}

void acLine::smallAngleDecoupledCalc()
{
    linkComp.sinTheta1 = linkInfo.theta1;
    linkComp.cosTheta1 = 1;
    linkComp.sinTheta2 = linkInfo.theta2;
    linkComp.cosTheta2 = 1;

    linkFlows.P1 = (g + 0.5 * mp_G) / (tap * tap) * constLinkInfo.v1 * constLinkInfo.v1;
    linkFlows.P1 -= g * constLinkComp.Vmx;
    linkFlows.P1 -= b * constLinkComp.Vmx * linkComp.sinTheta1;

    linkFlows.Q1 = -(b + 0.5 * mp_B) / (tap * tap) * linkInfo.v1 * linkInfo.v1;
    linkFlows.Q1 -= g * linkComp.Vmx * constLinkComp.sinTheta1;
    linkFlows.Q1 += b * linkComp.Vmx * constLinkComp.cosTheta1;

    linkFlows.P2 = (g + 0.5 * mp_G) * constLinkInfo.v2 * constLinkInfo.v2;
    linkFlows.P2 -= g * constLinkComp.Vmx;
    linkFlows.P2 -= b * constLinkComp.Vmx * linkComp.sinTheta2;

    linkFlows.Q2 = -(b + 0.5 * mp_B) * linkInfo.v2 * linkInfo.v2;
    linkFlows.Q2 -= g * linkComp.Vmx * constLinkComp.sinTheta2;
    linkFlows.Q2 += b * linkComp.Vmx * constLinkComp.cosTheta2;
    linkFlows.seqID = linkInfo.seqID;
}

void acLine::linearCalc()
{
    double dT1 = (linkInfo.theta1 - constLinkInfo.theta1) /
        2.0;  // divide by 2 so the angle change is attributed to both sides evenly
    double dT2 = (linkInfo.theta2 - constLinkInfo.theta2) /
        2.0;  // divide by 2 so the angle change is attributed to both sides evenly
    double dV1 = (linkInfo.v1 - constLinkInfo.v1);
    double dV2 = (linkInfo.v2 - constLinkInfo.v2);
    linkFlows.P1 = constLinkFlows.P1 + LinkDeriv.dP1dt1 * dT1;
    linkFlows.P1 += LinkDeriv.dP1dt2 * dT2;
    linkFlows.P1 += LinkDeriv.dP1dv1 * dV1;
    linkFlows.P1 += LinkDeriv.dP1dv2 * dV2;

    linkFlows.P2 = constLinkFlows.P2 + LinkDeriv.dP2dt1 * dT1;
    linkFlows.P2 += LinkDeriv.dP2dt2 * dT2;
    linkFlows.P2 += LinkDeriv.dP2dv1 * dV1;
    linkFlows.P2 += LinkDeriv.dP2dv2 * dV2;

    linkFlows.Q1 = constLinkFlows.Q1 + LinkDeriv.dQ1dt1 * dT1;
    linkFlows.Q1 += LinkDeriv.dQ1dt2 * dT2;
    linkFlows.Q1 += LinkDeriv.dQ1dv1 * dV1;
    linkFlows.Q1 += LinkDeriv.dQ1dv2 * dV2;

    linkFlows.Q2 = constLinkFlows.P2 + LinkDeriv.dQ2dt1 * dT1;
    linkFlows.Q2 += LinkDeriv.dQ2dt2 * dT2;
    linkFlows.Q2 += LinkDeriv.dQ2dv1 * dV1;
    linkFlows.Q2 += LinkDeriv.dQ2dv2 * dV2;

    linkFlows.seqID = linkInfo.seqID;
}

void acLine::fastDecoupledCalc()
{
    linkComp.sinTheta1 = linkInfo.theta1;
    linkComp.cosTheta1 = 1.0;

    linkComp.sinTheta2 = linkInfo.theta2;
    linkComp.cosTheta2 = 1.0;

    linkFlows.P1 = -b * constLinkComp.Vmx * linkComp.sinTheta1;

    linkFlows.Q1 = -(b + 0.5 * mp_B) / (tap * tap) * linkInfo.v1 * linkInfo.v1 + b * linkComp.Vmx;

    linkFlows.P2 = -b * constLinkComp.Vmx * linkComp.sinTheta2;

    linkFlows.Q2 = -(b + 0.5 * mp_B) * linkInfo.v2 * linkInfo.v2 + b * linkComp.Vmx;

    linkFlows.seqID = linkInfo.seqID;
}

void acLine::swOpenCalc()
{
    if (opFlags[switch1_open_flag]) {
        linkFlows.P1 = 0;
        linkFlows.Q1 = 0;
    } else {
        double V2 = b * linkInfo.v1 / tap / (b + 0.5 * mp_B);
        double dT = -((g + 0.5 * mp_G) / (b + 0.5 * mp_B) - g / b);

        double vm2 = linkInfo.v1 * V2 / tap;
        linkFlows.P1 =
            (g + 0.5 * mp_G) / (tap * tap) * linkInfo.v1 * linkInfo.v1 - g * vm2 - b * vm2 * dT;

        linkFlows.Q1 = -(b + 0.5 * mp_B) / (tap * tap) * linkInfo.v1 * linkInfo.v1 + b * vm2;
    }
    if (opFlags[switch2_open_flag]) {
        linkFlows.P2 = 0;
        linkFlows.Q2 = 0;
    } else {
        // flows from bus 2 to bus
        double V1 = b * linkInfo.v2 * tap / (b + 0.5 * mp_B);
        double dT = -((g + 0.5 * mp_G) / (b + 0.5 * mp_B) - g / b);

        double vm2 = linkInfo.v2 * V1 / tap;

        linkFlows.P2 = (g + 0.5 * mp_G) * linkInfo.v2 * linkInfo.v2 - g * vm2 - b * vm2 * dT;
        linkFlows.Q2 = -(b + 0.5 * mp_B) * linkInfo.v2 * linkInfo.v2 + b * vm2;
    }

    linkFlows.seqID = linkInfo.seqID;
}

void acLine::faultDeriv()
{
    std::memset(&LinkDeriv, 0, sizeof(linkPart));
    if (!opFlags[switch1_open_flag]) {
        LinkDeriv.dP1dv1 = 2 * (g / fault + fault * mp_G) / (tap * tap) * linkInfo.v1;
        LinkDeriv.dQ1dv1 = -2 * (b / fault + fault * mp_B) / (tap * tap) * linkInfo.v1;
    }

    if (!opFlags[switch2_open_flag]) {
        LinkDeriv.dP2dv2 = 2 * (g / (1.0 - fault) + (1.0 - fault) * mp_G) * linkInfo.v2;
        LinkDeriv.dQ2dv2 = -2 * (b / (1.0 - fault) + (1.0 - fault) * mp_B) * linkInfo.v2;
    }
    LinkDeriv.seqID = linkInfo.seqID;
}
void acLine::fullDeriv()
{
    // real power vs local states
    LinkDeriv.dP1dt1 =
        g * linkComp.Vmx * linkComp.sinTheta1 - b * linkComp.Vmx * linkComp.cosTheta1;
    LinkDeriv.dP1dv1 = 2 * (g + 0.5 * mp_G) / (tap * tap) * linkInfo.v1 -
        g / tap * linkInfo.v2 * linkComp.cosTheta1 - b / tap * linkInfo.v2 * linkComp.sinTheta1;

    LinkDeriv.dP2dt2 =
        g * linkComp.Vmx * linkComp.sinTheta2 - b * linkComp.Vmx * linkComp.cosTheta2;
    LinkDeriv.dP2dv2 = 2 * (g + 0.5 * mp_G) * linkInfo.v2 -
        g / tap * linkInfo.v1 * linkComp.cosTheta2 - b / tap * linkInfo.v1 * linkComp.sinTheta2;

    // reactive power vs local states
    LinkDeriv.dQ1dt1 =
        -g * linkComp.Vmx * linkComp.cosTheta1 - b * linkComp.Vmx * linkComp.sinTheta1;
    LinkDeriv.dQ2dt2 =
        -g * linkComp.Vmx * linkComp.cosTheta2 - b * linkComp.Vmx * linkComp.sinTheta2;
    LinkDeriv.dQ1dv1 = -2 * (b + 0.5 * mp_B) / (tap * tap) * linkInfo.v1 -
        g / tap * linkInfo.v2 * linkComp.sinTheta1 + b / tap * linkInfo.v2 * linkComp.cosTheta1;
    LinkDeriv.dQ2dv2 = -2 * (b + 0.5 * mp_B) * linkInfo.v2 -
        g / tap * linkInfo.v1 * linkComp.sinTheta2 + b / tap * linkInfo.v1 * linkComp.cosTheta2;

    // real power vs remote states
    LinkDeriv.dP1dv2 = -linkInfo.v1 * (g * linkComp.cosTheta1 + b * linkComp.sinTheta1) / tap;
    LinkDeriv.dP2dv1 = -linkInfo.v2 * (g * linkComp.cosTheta2 + b * linkComp.sinTheta2) / tap;
    LinkDeriv.dP1dt2 = -linkComp.Vmx * (g * linkComp.sinTheta1 - b * linkComp.cosTheta1);
    LinkDeriv.dP2dt1 = -linkComp.Vmx * (g * linkComp.sinTheta2 - b * linkComp.cosTheta2);

    // reactive power vs remote states

    LinkDeriv.dQ1dv2 = -linkInfo.v1 * (g * linkComp.sinTheta1 - b * linkComp.cosTheta1) / tap;
    LinkDeriv.dQ2dv1 = -linkInfo.v2 * (g * linkComp.sinTheta2 - b * linkComp.cosTheta2) / tap;
    LinkDeriv.dQ1dt2 = linkComp.Vmx * (g * linkComp.cosTheta1 + b * linkComp.sinTheta1);
    LinkDeriv.dQ2dt1 = linkComp.Vmx * (g * linkComp.cosTheta2 + b * linkComp.sinTheta2);
    LinkDeriv.seqID = linkInfo.seqID;
}
void acLine::simplifiedDeriv()
{
    /*
    linkFlows.P1 = -b * linkComp.Vmx * linkComp.sinTheta1;

    linkFlows.Q1 = -(b + 0.5 * mp_B) / (tap * tap) * linkInfo.v1 * linkInfo.v1;
    linkFlows.Q1 += b * linkComp.Vmx * linkComp.cosTheta1;
    //flows from bus 2 to bus 1
    linkFlows.P2 = -b * linkComp.Vmx * linkComp.sinTheta2;
    linkFlows.Q2 = -(b + 0.5 * mp_B) * linkInfo.v2 * linkInfo.v2;
    linkFlows.Q2 += b * linkComp.Vmx * linkComp.cosTheta2;
    */
    // real power vs local states
    double btap = b / tap;
    double bvmx = b * linkComp.Vmx;

    LinkDeriv.dP1dt1 = -bvmx * linkComp.cosTheta1;
    LinkDeriv.dP1dv1 = -btap * linkInfo.v2 * linkComp.sinTheta1;

    LinkDeriv.dP2dt2 = -bvmx * linkComp.cosTheta2;
    LinkDeriv.dP2dv2 = -btap * linkInfo.v1 * linkComp.sinTheta2;

    // reactive power vs local states
    LinkDeriv.dQ1dt1 = -bvmx * linkComp.sinTheta1;
    LinkDeriv.dQ2dt2 = -bvmx * linkComp.sinTheta2;
    LinkDeriv.dQ1dv1 =
        -2 * (b + 0.5 * mp_B) / (tap * tap) * linkInfo.v1 + btap * linkInfo.v2 * linkComp.cosTheta1;
    LinkDeriv.dQ2dv2 =
        -2 * (b + 0.5 * mp_B) * linkInfo.v2 + btap * linkInfo.v1 * linkComp.cosTheta2;

    // real power vs remote states
    LinkDeriv.dP1dv2 = -linkInfo.v1 * (btap * linkComp.sinTheta1);
    LinkDeriv.dP2dv1 = -linkInfo.v2 * (btap * linkComp.sinTheta2);
    LinkDeriv.dP1dt2 = bvmx * linkComp.cosTheta1;
    LinkDeriv.dP2dt1 = bvmx * linkComp.cosTheta2;

    // reactive power vs remote states

    LinkDeriv.dQ1dv2 = -linkInfo.v1 * (-btap * linkComp.cosTheta1);
    LinkDeriv.dQ2dv1 = -linkInfo.v2 * (-btap * linkComp.cosTheta2);
    LinkDeriv.dQ1dt2 = bvmx * linkComp.sinTheta1;
    LinkDeriv.dQ2dt1 = bvmx * linkComp.sinTheta2;
    LinkDeriv.seqID = linkInfo.seqID;
}

void acLine::decoupledDeriv()
{
    /*
    linkFlows.P1 = (g + 0.5 * mp_G) / (tap * tap) * constLinkInfo.v1 * constLinkInfo.v1;
    linkFlows.P1 -= g * constLinkComp.Vmx * linkComp.cosTheta1;
    linkFlows.P1 -= b * constLinkComp.Vmx * linkComp.sinTheta1;

    linkFlows.Q1 = -(b + 0.5 * mp_B) / (tap * tap) * linkInfo.v1 * linkInfo.v1;
    linkFlows.Q1 -= g * linkComp.Vmx * constLinkComp.sinTheta1;
    linkFlows.Q1 += b * linkComp.Vmx * constLinkComp.cosTheta1;

    linkFlows.P2 = (g + 0.5 * mp_G) * constLinkInfo.v2 * constLinkInfo.v2;
    linkFlows.P2 -= g * constLinkComp.Vmx * linkComp.cosTheta2;
    linkFlows.P2 -= b * constLinkComp.Vmx * linkComp.sinTheta2;

    linkFlows.Q2 = -(b + 0.5 * mp_B) * linkInfo.v2 * linkInfo.v2;
    linkFlows.Q2 -= g * linkComp.Vmx * constLinkComp.sinTheta2;
    linkFlows.Q2 += b * linkComp.Vmx * constLinkComp.cosTheta2;
    */
    // real power vs local states
    LinkDeriv.dP1dt1 =
        g * constLinkComp.Vmx * linkComp.sinTheta1 - b * constLinkComp.Vmx * linkComp.cosTheta1;
    LinkDeriv.dP1dv1 = 0;

    LinkDeriv.dP2dt2 =
        g * constLinkComp.Vmx * linkComp.sinTheta2 - b * constLinkComp.Vmx * linkComp.cosTheta2;
    LinkDeriv.dP2dv2 = 0;

    // reactive power vs local states
    LinkDeriv.dQ1dt1 = 0;
    LinkDeriv.dQ2dt2 = 0;
    LinkDeriv.dQ1dv1 = -2 * (b + 0.5 * mp_B) / (tap * tap) * linkInfo.v1 -
        g / tap * linkInfo.v2 * constLinkComp.sinTheta1 +
        b / tap * linkInfo.v2 * constLinkComp.cosTheta1;
    LinkDeriv.dQ2dv2 = -2 * (b + 0.5 * mp_B) * linkInfo.v2 -
        g / tap * linkInfo.v1 * constLinkComp.sinTheta2 +
        b / tap * linkInfo.v1 * constLinkComp.cosTheta2;

    // real power vs remote states
    LinkDeriv.dP1dv2 = 0;
    LinkDeriv.dP2dv1 = 0;
    LinkDeriv.dP1dt2 = -constLinkComp.Vmx * (g * linkComp.sinTheta1 - b * linkComp.cosTheta1);
    LinkDeriv.dP2dt1 = -constLinkComp.Vmx * (g * linkComp.sinTheta2 - b * linkComp.cosTheta2);

    // reactive power vs remote states

    LinkDeriv.dQ1dv2 =
        -linkInfo.v1 * (g * constLinkComp.sinTheta1 - b * constLinkComp.cosTheta1) / tap;
    LinkDeriv.dQ2dv1 =
        -linkInfo.v2 * (g * constLinkComp.sinTheta2 - b * constLinkComp.cosTheta2) / tap;
    LinkDeriv.dQ1dt2 = 0;
    LinkDeriv.dQ2dt1 = 0;
    LinkDeriv.seqID = linkInfo.seqID;
}

void acLine::linearDeriv()
{
    // there is no update since the derivatives are constant

    LinkDeriv.seqID = linkInfo.seqID;
}

void acLine::smallAngleDeriv()
{
    /*
    linkFlows.P1 = (g + 0.5 * mp_G) / (tap * tap) * linkInfo.v1 * linkInfo.v1;
    linkFlows.P1 -= g * linkComp.Vmx;
    linkFlows.P1 -= b * linkComp.Vmx * linkComp.sinTheta1;

    linkFlows.Q1 = -(b + 0.5 * mp_B) / (tap * tap) * linkInfo.v1 * linkInfo.v1;
    linkFlows.Q1 -= g * linkComp.Vmx * linkComp.sinTheta1;
    linkFlows.Q1 += b * linkComp.Vmx;

    //flows from bus 2 to bus 1
    linkFlows.P2 = (g + 0.5 * mp_G) * linkInfo.v2 * linkInfo.v2;
    linkFlows.P2 -= g * linkComp.Vmx;
    linkFlows.P2 -= b * linkComp.Vmx * linkComp.sinTheta2;

    linkFlows.Q2 = -(b + 0.5 * mp_B) * linkInfo.v2 * linkInfo.v2;
    linkFlows.Q2 -= g * linkComp.Vmx * linkComp.sinTheta2;
    linkFlows.Q2 += b * linkComp.Vmx;
    linkFlows.seqID = linkInfo.seqID;
    */
    LinkDeriv.dP1dt1 = -b * linkComp.Vmx;
    LinkDeriv.dP1dv1 = 2 * (g + 0.5 * mp_G) / (tap * tap) * linkInfo.v1 - g / tap * linkInfo.v2 -
        b / tap * linkInfo.v2 * linkComp.sinTheta1;

    LinkDeriv.dP2dt2 = -b * linkComp.Vmx * linkComp.cosTheta2;
    LinkDeriv.dP2dv2 = 2 * (g + 0.5 * mp_G) * linkInfo.v2 - g / tap * linkInfo.v1 -
        b / tap * linkInfo.v1 * linkComp.sinTheta2;

    // reactive power vs local states
    LinkDeriv.dQ1dt1 = -g * linkComp.Vmx;
    LinkDeriv.dQ2dt2 = -g * linkComp.Vmx;
    LinkDeriv.dQ1dv1 = -2 * (b + 0.5 * mp_B) / (tap * tap) * linkInfo.v1 -
        g / tap * linkInfo.v2 * linkComp.sinTheta1 + b / tap * linkInfo.v2;
    LinkDeriv.dQ2dv2 = -2 * (b + 0.5 * mp_B) * linkInfo.v2 -
        g / tap * linkInfo.v1 * linkComp.sinTheta2 + b / tap * linkInfo.v1;

    // real power vs remote states
    LinkDeriv.dP1dv2 = -linkInfo.v1 * (g + b * linkComp.sinTheta1) / tap;
    LinkDeriv.dP2dv1 = -linkInfo.v2 * (g + b * linkComp.sinTheta2) / tap;
    LinkDeriv.dP1dt2 = linkComp.Vmx * (b * linkComp.cosTheta1);
    LinkDeriv.dP2dt1 = linkComp.Vmx * (b * linkComp.cosTheta2);

    // reactive power vs remote states

    LinkDeriv.dQ1dv2 = -linkInfo.v1 * (g * linkComp.sinTheta1 - b * linkComp.cosTheta1) / tap;
    LinkDeriv.dQ2dv1 = -linkInfo.v2 * (g * linkComp.sinTheta2 - b * linkComp.cosTheta2) / tap;
    LinkDeriv.dQ1dt2 = linkComp.Vmx * g;
    LinkDeriv.dQ2dt1 = linkComp.Vmx * g;
    LinkDeriv.seqID = linkInfo.seqID;
}

void acLine::simplifiedDecoupledDeriv()
{
    /*
    linkFlows.P1 = -b * constLinkComp.Vmx * linkComp.sinTheta1;

    linkFlows.Q1 = -(b + 0.5 * mp_B) / (tap * tap) * linkInfo.v1 * linkInfo.v1;
    linkFlows.Q1 += b * linkComp.Vmx * constLinkComp.cosTheta1;

    linkFlows.P2 = -b * constLinkComp.Vmx * linkComp.sinTheta2;

    linkFlows.Q2 = -(b + 0.5 * mp_B) * linkInfo.v2 * linkInfo.v2;
    linkFlows.Q2 += b * linkComp.Vmx * constLinkComp.cosTheta2;*/

    LinkDeriv.dP1dt1 = -b * constLinkComp.Vmx * linkComp.cosTheta1;
    LinkDeriv.dP1dv1 = 0;

    LinkDeriv.dP2dt2 = -b * constLinkComp.Vmx * linkComp.cosTheta2;
    LinkDeriv.dP2dv2 = 0;

    double btap = b / tap;
    // reactive power vs local states
    LinkDeriv.dQ1dt1 = 0;
    LinkDeriv.dQ2dt2 = 0;
    LinkDeriv.dQ1dv1 = -2 * (b + 0.5 * mp_B) / (tap * tap) * linkInfo.v1 +
        btap * linkInfo.v2 * constLinkComp.cosTheta1;
    LinkDeriv.dQ2dv2 =
        -2 * (b + 0.5 * mp_B) * linkInfo.v2 + btap * linkInfo.v1 * constLinkComp.cosTheta2;

    // real power vs remote states
    LinkDeriv.dP1dv2 = 0;
    LinkDeriv.dP2dv1 = 0;
    LinkDeriv.dP1dt2 = b * constLinkComp.Vmx * linkComp.cosTheta1;
    LinkDeriv.dP2dt1 = b * constLinkComp.Vmx * linkComp.cosTheta2;

    // reactive power vs remote states

    LinkDeriv.dQ1dv2 = linkInfo.v1 * (btap * constLinkComp.cosTheta1);
    LinkDeriv.dQ2dv1 = linkInfo.v2 * (btap * constLinkComp.cosTheta2);
    LinkDeriv.dQ1dt2 = 0;
    LinkDeriv.dQ2dt1 = 0;
    LinkDeriv.seqID = linkInfo.seqID;
}

void acLine::smallAngleDecoupledDeriv()
{
    // real power vs local states
    LinkDeriv.dP1dt1 = -b * constLinkComp.Vmx;
    LinkDeriv.dP1dv1 = 0;

    LinkDeriv.dP2dt2 = -b * constLinkComp.Vmx;
    LinkDeriv.dP2dv2 = 0;

    // reactive power vs local states
    LinkDeriv.dQ1dt1 = 0;
    LinkDeriv.dQ2dt2 = 0;
    LinkDeriv.dQ1dv1 = -2 * (b + 0.5 * mp_B) / (tap * tap) * linkInfo.v1 -
        g / tap * linkInfo.v2 * constLinkComp.sinTheta1 + b / tap * linkInfo.v2;
    LinkDeriv.dQ2dv2 = -2 * (b + 0.5 * mp_B) * linkInfo.v2 -
        g / tap * linkInfo.v1 * constLinkComp.sinTheta2 + b / tap * linkInfo.v1;

    // real power vs remote states
    LinkDeriv.dP1dv2 = 0;
    LinkDeriv.dP2dv1 = 0;
    LinkDeriv.dP1dt2 = constLinkComp.Vmx * (b * linkComp.cosTheta1);
    LinkDeriv.dP2dt1 = constLinkComp.Vmx * (b * linkComp.cosTheta2);

    // reactive power vs remote states

    LinkDeriv.dQ1dv2 = -linkInfo.v1 * (g * constLinkComp.sinTheta1 - b) / tap;
    LinkDeriv.dQ2dv1 = -linkInfo.v2 * (g * constLinkComp.sinTheta2 - b) / tap;
    LinkDeriv.dQ1dt2 = 0;
    LinkDeriv.dQ2dt1 = 0;
    LinkDeriv.seqID = linkInfo.seqID;
}

void acLine::smallAngleSimplifiedDeriv()
{
    /*
    //flows from bus 1 to bus 2
    linkFlows.P1 = -b * linkComp.Vmx * linkComp.sinTheta1;

    linkFlows.Q1 = -(b + 0.5 * mp_B) / (tap * tap) * linkInfo.v1 * linkInfo.v1;
    linkFlows.Q1 += b * linkComp.Vmx ;
    //flows from bus 2 to bus 1
    linkFlows.P2 = -b * linkComp.Vmx * linkComp.sinTheta2;
    linkFlows.Q2 = -(b + 0.5 * mp_B) * linkInfo.v2 * linkInfo.v2;
    linkFlows.Q2 += b * linkComp.Vmx;
    linkFlows.seqID = linkInfo.seqID;
    */
    // real power vs local states
    double btap = b / tap;
    double bvmx = b * linkComp.Vmx;

    LinkDeriv.dP1dt1 = -bvmx;
    LinkDeriv.dP1dv1 = -btap * linkInfo.v2 * linkComp.sinTheta1;

    LinkDeriv.dP2dt2 = -bvmx;
    LinkDeriv.dP2dv2 = -btap * linkInfo.v1 * linkComp.sinTheta2;

    // reactive power vs local states
    LinkDeriv.dQ1dt1 = 0;
    LinkDeriv.dQ2dt2 = 0;
    LinkDeriv.dQ1dv1 = -2 * (b + 0.5 * mp_B) / (tap * tap) * linkInfo.v1 + btap * linkInfo.v2;
    LinkDeriv.dQ2dv2 = -2 * (b + 0.5 * mp_B) * linkInfo.v2 + btap * linkInfo.v1;

    // real power vs remote states
    LinkDeriv.dP1dv2 = -linkInfo.v1 * (btap * linkComp.sinTheta1);
    LinkDeriv.dP2dv1 = -linkInfo.v2 * (btap * linkComp.sinTheta2);
    LinkDeriv.dP1dt2 = bvmx;
    LinkDeriv.dP2dt1 = bvmx;

    // reactive power vs remote states

    LinkDeriv.dQ1dv2 = -linkInfo.v1 * (-btap);
    LinkDeriv.dQ2dv1 = -linkInfo.v2 * (-btap);
    LinkDeriv.dQ1dt2 = 0;
    LinkDeriv.dQ2dt1 = 0;
    LinkDeriv.seqID = linkInfo.seqID;
}

void acLine::fastDecoupledDeriv()
{
    /*
    linkFlows.P1 = -b * constLinkComp.Vmx * linkComp.sinTheta1;

    linkFlows.Q1 = -(b + 0.5 * mp_B) / (tap * tap) * linkInfo.v1 * linkInfo.v1+ b * linkComp.Vmx;

    linkFlows.P2 = -b * constLinkComp.Vmx;

    linkFlows.Q2 = -(b + 0.5 * mp_B) * linkInfo.v2 * linkInfo.v2+ b * linkComp.Vmx;
    */
    // real power vs local states
    LinkDeriv.dP1dt1 = -b * constLinkComp.Vmx;
    LinkDeriv.dP1dv1 = 0;

    LinkDeriv.dP2dt2 = -b * constLinkComp.Vmx;
    LinkDeriv.dP2dv2 = 0;

    // reactive power vs local states
    LinkDeriv.dQ1dt1 = 0;
    LinkDeriv.dQ2dt2 = 0;
    LinkDeriv.dQ1dv1 = -2 * (b + 0.5 * mp_B) / (tap * tap) * linkInfo.v1 + b / tap * linkInfo.v2;
    LinkDeriv.dQ2dv2 = -2 * (b + 0.5 * mp_B) * linkInfo.v2 + b / tap * linkInfo.v1;

    // real power vs remote states
    LinkDeriv.dP1dv2 = 0;
    LinkDeriv.dP2dv1 = 0;
    LinkDeriv.dP1dt2 = constLinkComp.Vmx * b;
    LinkDeriv.dP2dt1 = constLinkComp.Vmx * b;

    // reactive power vs remote states

    LinkDeriv.dQ1dv2 = linkInfo.v1 * (b) / tap;
    LinkDeriv.dQ2dv1 = linkInfo.v2 * (b) / tap;
    LinkDeriv.dQ1dt2 = 0;
    LinkDeriv.dQ2dt1 = 0;
    LinkDeriv.seqID = linkInfo.seqID;
}

void acLine::swOpenDeriv()
{
    std::memset(&LinkDeriv, 0, sizeof(linkPart));

    // flows from bus 2 to bus

    double Y = 1.0 / (b + 0.5 * mp_B);
    const double dT = -((g + 0.5 * mp_G) * Y - g / b);

    if (!opFlags[switch1_open_flag]) {
        double it2 = 1.0 / (tap * tap);
        LinkDeriv.dP1dv1 = 2.0 * (g + 0.5 * mp_G) * it2 * linkInfo.v1;
        LinkDeriv.dP1dv1 -= 2.0 * g * b * it2 * linkInfo.v1 * Y;
        LinkDeriv.dP1dv1 += 2.0 * b * b * it2 * linkInfo.v1 * Y * dT;

        LinkDeriv.dQ1dv1 = -2.0 * (b + 0.5 * mp_B) * it2 * linkInfo.v1;
        LinkDeriv.dQ1dv1 += 2.0 * b * b * it2 * linkInfo.v1 * Y;
    }

    if (!opFlags[switch2_open_flag]) {
        LinkDeriv.dP2dv2 = 2.0 * (g + 0.5 * mp_G) * linkInfo.v2;
        LinkDeriv.dP2dv2 -= 2.0 * g * b * linkInfo.v2 * Y;
        LinkDeriv.dP2dv2 += 2.0 * b * b * linkInfo.v2 * Y * dT;

        LinkDeriv.dQ2dv2 = -2.0 * (b + 0.5 * mp_B) * linkInfo.v2;
        LinkDeriv.dQ2dv2 += 2.0 * b * b * linkInfo.v2 * Y;
    }
    LinkDeriv.seqID = linkInfo.seqID;

    /*
    if (!opFlags[switch1_open_flag])
    {
    LinkDeriv.dP1dv1 = 2 * (g + mp_G) / (tap * tap) * linkInfo.v1;
    LinkDeriv.dQ1dv1 = -2 * (b  + mp_B) / (tap * tap) * linkInfo.v1;
    }

    if (!opFlags[switch2_open_flag])
    {
    LinkDeriv.dP2dv2 = 2 * (g  + mp_G) * linkInfo.v2;
    LinkDeriv.dQ2dv2 = -2 * (b  + mp_B) * linkInfo.v2;
    }
    */
    LinkDeriv.seqID = linkInfo.seqID;
}

void acLine::loadApproxFunctions()
{
    // load up the member function pointer array to point to the correct function
    flowCalc[indexVal(approxKeyMask::none)] = &acLine::fullCalc;
    flowCalc[indexVal(approxKeyMask::decoupled)] = &acLine::decoupledCalc;
    flowCalc[indexVal(approxKeyMask::sm_angle)] = &acLine::smallAngleCalc;
    flowCalc[indexVal(approxKeyMask::sm_angle_decoupled)] = &acLine::smallAngleDecoupledCalc;
    flowCalc[indexVal(approxKeyMask::simplified)] = &acLine::simplifiedCalc;
    flowCalc[indexVal(approxKeyMask::simplified_decoupled)] = &acLine::simplifiedDecoupledCalc;
    flowCalc[indexVal(approxKeyMask::simplified_sm_angle)] = &acLine::smallAngleSimplifiedCalc;
    flowCalc[indexVal(approxKeyMask::fast_decoupled)] = &acLine::fastDecoupledCalc;
    flowCalc[indexVal(approxKeyMask::linear)] = &acLine::linearCalc;

    derivCalc[indexVal(approxKeyMask::none)] = &acLine::fullDeriv;
    derivCalc[indexVal(approxKeyMask::decoupled)] = &acLine::decoupledDeriv;
    derivCalc[indexVal(approxKeyMask::sm_angle)] = &acLine::smallAngleDeriv;
    derivCalc[indexVal(approxKeyMask::sm_angle_decoupled)] = &acLine::smallAngleDecoupledDeriv;
    derivCalc[indexVal(approxKeyMask::simplified)] = &acLine::simplifiedDeriv;
    derivCalc[indexVal(approxKeyMask::simplified_decoupled)] = &acLine::simplifiedDecoupledDeriv;
    derivCalc[indexVal(approxKeyMask::simplified_sm_angle)] = &acLine::smallAngleSimplifiedDeriv;
    derivCalc[indexVal(approxKeyMask::fast_decoupled)] = &acLine::fastDecoupledDeriv;
    derivCalc[indexVal(approxKeyMask::linear)] = &acLine::linearDeriv;
}

}  // namespace griddyn
