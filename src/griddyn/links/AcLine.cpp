/*
 * Copyright (c) 2014-2026, Lawrence Livermore National Security
 * See the top-level NOTICE for additional details. All rights reserved.
 * SPDX-License-Identifier: BSD-3-Clause
 */

// headers
#include "AcLine.h"

#include "../GridArea.h"
#include "../GridBus.h"
#include "../simulation/Contingency.h"
#include "core/CoreExceptions.h"
#include "core/CoreObjectTemplates.hpp"
#include "core/ObjectFactoryTemplates.hpp"
#include "core/ObjectInterpreter.h"
#include "gmlc/utilities/stringOps.h"
#include "gmlc/utilities/vectorOps.hpp"
#include "utilities/MatrixDataCompact.hpp"
#include <array>
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
AcLine::AcLine(const std::string& objName): Link(objName)
{
    // default values
    loadApproxFunctions();
    opFlags.set(NETWORK_CONNECTED);
}

// NOLINTNEXTLINE
AcLine::AcLine(double rP, double xP, const std::string& objName): Link(objName), r(rP), x(xP)
{
    // default values
    setAdmit();
    loadApproxFunctions();
    opFlags.set(NETWORK_CONNECTED);
    // load up the member function pointer array to point to the correct function
}

static TypeFactory<AcLine> gLf("link", "tie");

CoreObject* AcLine::clone(CoreObject* obj) const
{
    auto* lnk = cloneBaseFactory<AcLine, Link>(this, obj, &gLf);
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

void AcLine::pFlowObjectInitializeB()
{
    updateLocalCache();
}
void AcLine::pFlowCheck(std::vector<Violation>& violationVector)
{
    Link::pFlowCheck(violationVector);
    const double angle = linkInfo.theta1;
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

        violationVector.push_back(violation);
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

        violationVector.push_back(violation);
    }
}

void AcLine::switchChange(int switchNum)
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

double AcLine::quickupdateP()
{
    linkComp.Vmx = linkInfo.v1 * linkInfo.v2 / tap;
    linkFlows.P1 = ((g + (0.5 * mp_G)) / (tap * tap) * linkInfo.v1 * linkInfo.v1) -
        (g * linkComp.Vmx * (1 - linkInfo.theta1)) - (b * linkComp.Vmx * linkInfo.theta1);
    linkFlows.P2 = ((g + (0.5 * mp_G)) * linkInfo.v2 * linkInfo.v2) -
        (g * linkComp.Vmx * (1 - linkInfo.theta2)) - (b * linkComp.Vmx * linkInfo.theta2);
    return linkFlows.P1;
}

void AcLine::timestep(const CoreTime time, const IOdata& /*inputs*/, const SolverMode& /*sMode*/)
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

void AcLine::checkMerge() {}

static constexpr auto locNumStrings = std::array<std::string_view, 11>{"r",
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
static constexpr auto locStrStrings = std::array<std::string_view, 2>{"from", "to"};
static constexpr std::array<std::string_view, 0> flagStrings{};
void AcLine::getParameterStrings(stringVec& pstr, ParamStringType pstype) const
{
    getParamString<AcLine, GridComponent>(
        this, pstr, locNumStrings, locStrStrings, flagStrings, pstype);
}

// set properties
void AcLine::set(std::string_view param, std::string_view val)
{
    if (param == "approximation") {
        if (val == "auto") {
            loadApproxFunctions();
        } else if (val == "none") {
            for (int kk = 0; kk < APPROXIMATION_LEVELS; ++kk) {
                flowCalc[kk] = &AcLine::fullCalc;
                derivCalc[kk] = &AcLine::fullDeriv;
            }
        } else if (val == "simplified") {
            for (int kk = 0; kk < APPROXIMATION_LEVELS; ++kk) {
                flowCalc[kk] = &AcLine::simplifiedCalc;
                derivCalc[kk] = &AcLine::simplifiedDeriv;
            }
        } else if (val == "decoupled") {
            for (int kk = 0; kk < APPROXIMATION_LEVELS; ++kk) {
                flowCalc[kk] = &AcLine::decoupledCalc;
                derivCalc[kk] = &AcLine::decoupledDeriv;
            }
        } else if (val == "simplified_decoupled") {
            for (int kk = 0; kk < APPROXIMATION_LEVELS; ++kk) {
                flowCalc[kk] = &AcLine::simplifiedDecoupledCalc;
                derivCalc[kk] = &AcLine::simplifiedDecoupledDeriv;
            }
        } else if (val == "small_angle_decoupled") {
            for (int kk = 0; kk < APPROXIMATION_LEVELS; ++kk) {
                flowCalc[kk] = &AcLine::smallAngleDecoupledCalc;
                derivCalc[kk] = &AcLine::smallAngleDecoupledDeriv;
            }
        } else if (val == "simplified_small_angle") {
            for (int kk = 0; kk < APPROXIMATION_LEVELS; ++kk) {
                flowCalc[kk] = &AcLine::smallAngleSimplifiedCalc;
                derivCalc[kk] = &AcLine::smallAngleSimplifiedDeriv;
            }
        } else if (val == "small_angle") {
            for (int kk = 0; kk < APPROXIMATION_LEVELS; ++kk) {
                flowCalc[kk] = &AcLine::smallAngleCalc;
                derivCalc[kk] = &AcLine::smallAngleDeriv;
            }
        } else if (val == "linear") {
            for (int kk = 0; kk < APPROXIMATION_LEVELS; ++kk) {
                flowCalc[kk] = &AcLine::linearCalc;
                derivCalc[kk] = &AcLine::linearDeriv;
            }
        } else if (val == "fastdecoupled") {
            for (int kk = 0; kk < APPROXIMATION_LEVELS; ++kk) {
                flowCalc[kk] = &AcLine::fastDecoupledCalc;
                derivCalc[kk] = &AcLine::fastDecoupledDeriv;
            }
        }
    } else {
        Link::set(param, val);
    }
}

void AcLine::set(std::string_view param, double val, unit unitType)
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
                opFlags.set(FIXED_TARGET_POWER);
                break;
            default:
                throw(UnrecognizedParameter(param));
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
            if ((!opFlags[SWITCH1_OPEN_FLAG]) || (!opFlags[SWITCH2_OPEN_FLAG])) {
                alert(this, POTENTIAL_FAULT_CHANGE);
            }
            if ((temp < 0.0) || (temp > 1.0)) {
                logging::normal(this, "fault cleared");
            }
        } else if ((temp > 0.0) && (temp < 1.0)) {
            logging::normal(this, "Line fault at {} of line", temp);
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

double AcLine::get(std::string_view param, unit unitType) const
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

int AcLine::fixRealPower(double power,
                         id_type_t measureTerminal,
                         id_type_t fixedTerminal,
                         unit unitType)
{
    Pset = convert(power, unitType, puMW, systemBasePower);
    updateLocalCache();
    const double ang = asin(Pset / b / linkComp.Vmx);
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
        const double newAng = B1->getAngle() - ang - tapAngle;
        B2->set("angle", newAng);
    } else if ((fixedTerminal == 2) || (isSameObject(fixedTerminal, B2))) {
        const double newAng = ang + B2->getAngle() - tapAngle;
        B1->set("angle", newAng);
    } else {
        return 0;
    }
    opFlags.set(FIXED_TARGET_POWER);
    return 1;
}

static IOlocs gALoc{0, 1};

int AcLine::fixPower(double rPower,
                     double qPower,
                     id_type_t measureTerminal,
                     id_type_t fixedTerminal,
                     units::unit unitType)
{
    const double realPower = convert(rPower, unitType, puMW, systemBasePower);
    const double reactivePower = convert(qPower, unitType, puMW, systemBasePower);
    opFlags.set(FIXED_TARGET_POWER);
    double voltage1 = B1->getVoltage();
    double voltage2 = B2->getVoltage();
    double angle = 0;
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
        logging::warning(this, "invalid measure terminal identification");
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
        logging::warning(this, "invalid fixed terminal identification");
        return ret;
    }
    angle = asin(-realPower / b / (voltage1 * voltage2 / tap));
    if (measureTerminal == 1) {
        if (fixedTerminal == 1) {
            voltage2 = -((-reactivePower) - ((b * voltage1 * voltage1) / (tap * tap))) * tap /
                (b * voltage1 * cos(angle));
        } else {
            voltage1 = std::sqrt(
                (-(reactivePower - ((b * voltage1 * voltage2 / tap) * cos(angle))) * tap * tap) /
                b);
        }
    } else {
        if (fixedTerminal == 2) {
            voltage1 = (-(reactivePower - ((b * voltage2 * voltage2) / (tap * tap))) * tap) /
                (b * voltage2 * cos(angle));
        } else {
            voltage2 =
                std::sqrt((-(reactivePower - ((b * voltage1 * voltage2 / tap) * cos(angle)))) / b);
        }
    }
    linkInfo.v1 = voltage1;
    linkInfo.v2 = voltage2;
    angle = asin(-realPower / b / (voltage1 * voltage2 / tap));
    if (measureTerminal == 1) {
        linkInfo.theta1 = angle;
        linkInfo.theta2 = -angle;
    } else {
        linkInfo.theta1 = -angle;
        linkInfo.theta2 = angle;
    }
    linkComp.Vmx = linkInfo.v1 * linkInfo.v2 / tap;
    DEFAULTPOWERCOMP();
    // basePowerComp ();
    double err = (measureTerminal == 1) ?
        (std::abs(linkFlows.P1 - realPower) + std::abs(linkFlows.Q1 - reactivePower)) :
        (std::abs(linkFlows.P2 - realPower) + std::abs(linkFlows.Q2 - reactivePower));
    double previousError = err;

    MatrixDataCompact<2, 2> matrixData;
    double deltaP;
    double deltaQ;
    double deltaAngle;
    double deltaVoltage;
    double pVoltageDerivative;
    double pAngleDerivative;
    double qVoltageDerivative;
    double qAngleDerivative;
    bool aboveTol = ((err > atol) || (err > vtol));

    while (aboveTol) {
        matrixData.clear();
        if (measureTerminal == fixedTerminal) {
            outputPartialDerivatives(measureTerminal, emptyStateData, matrixData, cLocalSolverMode);
        } else {
            ioPartialDerivatives(
                measureTerminal, emptyStateData, matrixData, gALoc, cLocalSolverMode);
        }
        if (measureTerminal == 1) {
            deltaP = realPower - linkFlows.P1;
            deltaQ = reactivePower - linkFlows.Q1;
        } else {
            deltaP = realPower - linkFlows.P2;
            deltaQ = reactivePower - linkFlows.Q2;
        }
        // printf("A dP=%f dQ=%f\n",deltaP,deltaQ);
        pVoltageDerivative = matrixData.at(POUT_LOCATION, VOLTAGE_IN_LOCATION);
        pAngleDerivative = matrixData.at(POUT_LOCATION, ANGLE_IN_LOCATION);
        qVoltageDerivative = matrixData.at(QOUT_LOCATION, VOLTAGE_IN_LOCATION);
        qAngleDerivative = matrixData.at(QOUT_LOCATION, ANGLE_IN_LOCATION);
        const double determinant = solve2x2(pVoltageDerivative,
                                            pAngleDerivative,
                                            qVoltageDerivative,
                                            qAngleDerivative,
                                            deltaP,
                                            deltaQ,
                                            deltaVoltage,
                                            deltaAngle);
        if (!(std::isnormal(determinant))) {
            break;
        }

        if (fixedTerminal == 1) {
            voltage2 += deltaVoltage;
            linkInfo.v2 = voltage2;
        } else {
            voltage1 += deltaVoltage;
            linkInfo.v1 = voltage1;
        }
        if (measureTerminal == 1) {
            if (fixedTerminal == 1) {
                angle -= deltaAngle;
            } else {
                angle += deltaAngle;
            }
            linkInfo.theta1 = angle;
            linkInfo.theta2 = -angle;
        } else {
            if (fixedTerminal == 1) {
                angle += deltaAngle;
            } else {
                angle -= deltaAngle;
            }
            linkInfo.theta1 = -angle;
            linkInfo.theta2 = angle;
        }
        // update the Vmx term
        linkComp.Vmx = linkInfo.v1 * linkInfo.v2 / tap;
        DEFAULTPOWERCOMP();
        if (measureTerminal == 1) {
            deltaP = realPower - linkFlows.P1;
            deltaQ = reactivePower - linkFlows.Q1;
        } else {
            deltaP = realPower - linkFlows.P2;
            deltaQ = reactivePower - linkFlows.Q2;
        }
        // printf("B dP=%f dQ=%f\n", deltaP, deltaQ);

        if ((std::abs(deltaP) <= atol) && (std::abs(deltaQ) <= vtol)) {
            aboveTol = false;
        } else {
            err = std::abs(deltaP) + std::abs(deltaQ);
            if (err >= previousError) {
                logging::warning(this, "convergence break increasing");
                break;
            }
            previousError = err;
        }
    }
    if (angle > kPI) {
        angle -= 2 * kPI;
    }
    if (angle < -kPI) {
        angle += 2 * kPI;
    }
    if (std::abs(angle) > kPI / 2) {
        logging::warning(this, "large angle");
    }
    if (fixedTerminal == 2) {
        const double newAngle = (measureTerminal == 2) ? (B2->getAngle() - angle + tapAngle) :
                                                         (angle + B2->getAngle() + tapAngle);

        B1->set("angle", newAngle);
        B1->set("voltage", voltage1);
        ret = B1->propogatePower(false);
    } else {
        if (voltage2 > 1.5) {
            logging::warning(this, "high voltage");
        }
        const double newAngle = (measureTerminal == 1) ? (B1->getAngle() - angle - tapAngle) :
                                                         (angle + B1->getAngle() - tapAngle);
        B2->set("angle", newAngle);
        B2->set("voltage", voltage2);
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

void AcLine::ioPartialDerivatives(id_type_t busId,
                                  const StateData& /*sD*/,
                                  MatrixData<double>& matrixData,
                                  const IOlocs& inputLocs,
                                  const SolverMode& sMode)
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
    auto voltageLoc = inputLocs[VOLTAGE_IN_LOCATION];
    auto angleLoc = inputLocs[ANGLE_IN_LOCATION];

    if ((busId == 2) || (busId == B2->getID())) {
        if (!opFlags[SWITCH2_OPEN_FLAG]) {
            if (voltageLoc != kNullLocation) {
                matrixData.assign(POUT_LOCATION, voltageLoc, LinkDeriv.dP2dv2);
                matrixData.assign(QOUT_LOCATION, voltageLoc, LinkDeriv.dQ2dv2);
            }
            if (angleLoc != kNullLocation) {
                matrixData.assign(POUT_LOCATION, angleLoc, LinkDeriv.dP2dt2);
                matrixData.assign(QOUT_LOCATION, angleLoc, LinkDeriv.dQ2dt2);
            }
        }
    } else {
        if (!opFlags[SWITCH1_OPEN_FLAG]) {
            if (voltageLoc != kNullLocation) {
                matrixData.assign(POUT_LOCATION, voltageLoc, LinkDeriv.dP1dv1);
                matrixData.assign(QOUT_LOCATION, voltageLoc, LinkDeriv.dQ1dv1);
            }
            if (angleLoc != kNullLocation) {
                matrixData.assign(POUT_LOCATION, angleLoc, LinkDeriv.dP1dt1);

                matrixData.assign(QOUT_LOCATION, angleLoc, LinkDeriv.dQ1dt1);
            }
        }
    }
}

void AcLine::outputPartialDerivatives(const IOdata& /*inputs*/,
                                      const StateData& /*sD*/,
                                      MatrixData<double>& /*md*/,
                                      const SolverMode& /*sMode*/)
{
    // there are theoretically 4 outputs for a standard ac line,  but no internal states therefore
    // if this function is called from an external entity there are no output partial derivatives
}

void AcLine::outputPartialDerivatives(id_type_t busId,
                                      const StateData& /*sD*/,
                                      MatrixData<double>& matrixData,
                                      const SolverMode& sMode)
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

    index_t b1Voffset = VOLTAGE_IN_LOCATION;
    index_t b2Voffset = VOLTAGE_IN_LOCATION;
    index_t b1Aoffset = ANGLE_IN_LOCATION;
    index_t b2Aoffset = ANGLE_IN_LOCATION;

    if (!isLocal(sMode)) {
        b1Voffset = B1->getOutputLoc(sMode, VOLTAGE_IN_LOCATION);
        b2Voffset = B2->getOutputLoc(sMode, VOLTAGE_IN_LOCATION);
        b1Aoffset = B1->getOutputLoc(sMode, ANGLE_IN_LOCATION);
        b2Aoffset = B2->getOutputLoc(sMode, ANGLE_IN_LOCATION);
    }

    if ((busId == 2) || (busId == B2->getID())) {
        if (b1Voffset != kNullLocation) {
            matrixData.assign(POUT_LOCATION, b1Voffset, LinkDeriv.dP2dv1);
            // reactive power vs Voltage
            matrixData.assign(QOUT_LOCATION, b1Voffset, LinkDeriv.dQ2dv1);
        }
        if (b1Aoffset != kNullLocation) {
            // power vs angle
            matrixData.assign(POUT_LOCATION, b1Aoffset, LinkDeriv.dP2dt1);
            // reactive power vs Angle
            matrixData.assign(QOUT_LOCATION, b1Aoffset, LinkDeriv.dQ2dt1);
        }
    } else {
        if (b2Voffset != kNullLocation) {
            matrixData.assign(POUT_LOCATION, b2Voffset, LinkDeriv.dP1dv2);
            // reactive power vs Voltage
            matrixData.assign(QOUT_LOCATION, b2Voffset, LinkDeriv.dQ1dv2);
        }
        if (b2Aoffset != kNullLocation) {
            // power vs angle
            matrixData.assign(POUT_LOCATION, b2Aoffset, LinkDeriv.dP1dt2);
            // reactive power vs Angle
            matrixData.assign(QOUT_LOCATION, b2Aoffset, LinkDeriv.dQ1dt2);
        }
    }
}

count_t AcLine::outputDependencyCount(index_t /*num*/, const SolverMode& /*sMode*/) const
{
    return 2;
}
// set admittance values y := g + jb
void AcLine::setAdmit()
{
    const auto admittanceSquared = 1.0 / ((r * r) + (x * x));
    g = r * admittanceSquared;
    b = -x * admittanceSquared;
}

void AcLine::disable()
{
    if (!isEnabled()) {
        return;
    }
    Link::disable();
    linkFlows = {};
    LinkDeriv = {};
}

double AcLine::getMaxTransfer() const
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

void AcLine::setState(CoreTime time,
                      const double state[],
                      const double dstateDt[],
                      const SolverMode& sMode)
{
    prevTime = time;
    const StateData stateData(time, state, dstateDt);

    if (sMode.approx[DECOUPLED]) {  // recompute power with new state updates for the decoupled
                                    // system
        updateLocalCache(noInputs, stateData, sMode);
        constLinkInfo = linkInfo;  // update the constant linkInfo
        constLinkComp = linkComp;
        linkInfo.seqID = 0;
        // update the cache twice to get the correct values with the decoupled mode
        updateLocalCache(noInputs, stateData, sMode);
    } else if (sMode.approx[LINEAR]) {
        // reLinearize at each step
        loadLinkInfo(stateData, sMode);
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
        updateLocalCache(noInputs, stateData, sMode);
        constLinkInfo = linkInfo;  // update the constant linkInfo
        constLinkComp = linkComp;
    }
    constLinkFlows = linkFlows;  // update the constant linkFlows
}

double AcLine::getAngle(const double state[], const SolverMode& sMode) const
{
    const double angle1 = B1->getAngle(state, sMode);
    const double angle2 = B2->getAngle(state, sMode);
    return angle1 - angle2 - tapAngle;
}

ChangeCode
    AcLine::powerFlowAdjust(const IOdata& /*inputs*/, std::uint32_t /*flags*/, CheckLevel level)
{
    if ((level == CheckLevel::HIGH_ANGLE_TRIP) && (isConnected())) {
        if (std::abs(linkInfo.theta1) > ((kPI / 2.0) + 0.01)) {
            disconnect();
            return ChangeCode::JACOBIAN_CHANGE;
        }
    }
    return ChangeCode::NO_CHANGE;
}

ChangeCode AcLine::rootCheck(const IOdata& /*inputs*/,
                             const StateData& stateData,
                             const SolverMode& sMode,
                             CheckLevel level)
{
    auto ret = ChangeCode::NO_CHANGE;
    if (level == CheckLevel::COMPLETE_STATE_CHECK) {
        updateLocalCache(noInputs, stateData, sMode);
        if (std::abs(linkInfo.theta1) > maxAngle) {
            logging::warning(this, "max angle 1 exceeded");
        } else if (std::abs(linkInfo.theta2) > maxAngle) {
            logging::warning(this, "max angle 2 exceeded");
        }
    }
    return ret;
}
void AcLine::updateLocalCache(const IOdata& /*inputs*/,
                              const StateData& stateData,
                              const SolverMode& sMode)
{
    if (!isEnabled()) {
        return;
    }
    if (!stateData.updateRequired(linkInfo.seqID)) {
        return;  // already computed
    }
    loadLinkInfo(stateData, sMode);
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

bool AcLine::testAndTrip(int tripLevel)
{
    if (!isConnected()) {
        return false;
    }
    if (tripLevel >= 1) {
        if (std::abs(linkInfo.theta1) > maxAngle) {
            disconnect();
            return true;
        }
        if (std::abs(linkInfo.theta2) > maxAngle) {
            disconnect();
            return true;
        }
    }
    if (tripLevel >= 2) {
        if (checkFlag(ANGLE_SLIP_ON_TEST)) {
            disconnect();
            return true;
        }
    }
    return false;
}
void AcLine::updateLocalCache()
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

void AcLine::faultCalc()
{
    if (opFlags[SWITCH1_OPEN_FLAG]) {
        linkFlows.P1 = 0;
        linkFlows.Q1 = 0;
    } else {
        linkFlows.P1 = (((g / fault) + (fault * mp_G)) / (tap * tap)) * linkInfo.v1 * linkInfo.v1;

        linkFlows.Q1 = -(((b / fault) + (fault * mp_B)) / (tap * tap)) * linkInfo.v1 * linkInfo.v1;
    }
    if (opFlags[SWITCH2_OPEN_FLAG]) {
        linkFlows.P2 = 0;
        linkFlows.Q2 = 0;
    } else {
        linkFlows.P2 = ((g / (1.0 - fault)) + ((1.0 - fault) * mp_G)) * linkInfo.v2 * linkInfo.v2;

        linkFlows.Q2 = -((b / (1.0 - fault)) + ((1.0 - fault) * mp_B)) * linkInfo.v2 * linkInfo.v2;
    }
}

void AcLine::loadLinkInfo()
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
        if (!opFlags[SWITCH1_OPEN_FLAG]) {
            linkInfo.v1 = B1->getVoltage();
        }
        if (!opFlags[SWITCH2_OPEN_FLAG]) {
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

void AcLine::loadLinkInfo(const StateData& stateData, const SolverMode& sMode)
{
    if ((linkInfo.seqID == stateData.seqID) && (stateData.seqID != 0)) {
        return;
    }
    // std::memset (&linkInfo, 0, sizeof(LinkInfo));
    linkInfo.v1 = B1->getVoltage(stateData, sMode);

    linkInfo.v2 = B2->getVoltage(stateData, sMode);

    linkInfo.theta1 = B1->getAngle(stateData, sMode) - B2->getAngle(stateData, sMode) - tapAngle;
    if (std::fabs(linkInfo.theta1) > (kPI / 2.0)) {
        if (isConnected()) {
            opFlags.set(ANGLE_SLIP_ON_TEST);
        }
    }
    linkInfo.theta2 = -linkInfo.theta1;
    linkComp.Vmx = linkInfo.v1 * linkInfo.v2 / tap;
    linkInfo.seqID = stateData.seqID;
    // don't compute the trig functions yet as that may not be necessary
}

void AcLine::fullCalc()
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
    linkFlows.P1 = ((g + (0.5 * mp_G)) * vsq) - (g * tempc) - (b * temps);
    linkFlows.Q1 = (-(b + (0.5 * mp_B)) * vsq) - (g * temps) + (b * tempc);

    // flows from bus 2 to bus 1

    vsq = linkInfo.v2 * linkInfo.v2;
    tempc = linkComp.Vmx * linkComp.cosTheta2;
    temps = linkComp.Vmx * linkComp.sinTheta2;

    linkFlows.P2 = ((g + (0.5 * mp_G)) * vsq) - (g * tempc) - (b * temps);
    linkFlows.Q2 = (-(b + (0.5 * mp_B)) * vsq) - (g * temps) + (b * tempc);

    linkFlows.seqID = linkInfo.seqID;
}

void AcLine::simplifiedCalc()
{
    // compute the trig functions
    linkComp.sinTheta1 = sin(linkInfo.theta1);
    linkComp.cosTheta1 = cos(linkInfo.theta1);
    linkComp.sinTheta2 = -linkComp.sinTheta1;
    linkComp.cosTheta2 = linkComp.cosTheta1;

    // flows from bus 1 to bus 2
    linkFlows.P1 = -b * linkComp.Vmx * linkComp.sinTheta1;

    linkFlows.Q1 = -((b + (0.5 * mp_B)) / (tap * tap)) * linkInfo.v1 * linkInfo.v1;
    linkFlows.Q1 += b * linkComp.Vmx * linkComp.cosTheta1;
    // flows from bus 2 to bus 1
    linkFlows.P2 = -b * linkComp.Vmx * linkComp.sinTheta2;
    linkFlows.Q2 = -(b + (0.5 * mp_B)) * linkInfo.v2 * linkInfo.v2;
    linkFlows.Q2 += b * linkComp.Vmx * linkComp.cosTheta2;
    linkFlows.seqID = linkInfo.seqID;
}

void AcLine::decoupledCalc()
{
    linkComp.sinTheta1 = sin(linkInfo.theta1);
    linkComp.cosTheta1 = cos(linkInfo.theta1);
    linkComp.sinTheta2 = -linkComp.sinTheta1;
    linkComp.cosTheta2 = linkComp.cosTheta1;

    linkFlows.P1 = ((g + (0.5 * mp_G)) / (tap * tap)) * constLinkInfo.v1 * constLinkInfo.v1;
    linkFlows.P1 -= g * constLinkComp.Vmx * linkComp.cosTheta1;
    linkFlows.P1 -= b * constLinkComp.Vmx * linkComp.sinTheta1;

    linkFlows.Q1 = -((b + (0.5 * mp_B)) / (tap * tap)) * linkInfo.v1 * linkInfo.v1;
    linkFlows.Q1 -= g * linkComp.Vmx * constLinkComp.sinTheta1;
    linkFlows.Q1 += b * linkComp.Vmx * constLinkComp.cosTheta1;

    linkFlows.P2 = (g + (0.5 * mp_G)) * constLinkInfo.v2 * constLinkInfo.v2;
    linkFlows.P2 -= g * constLinkComp.Vmx * linkComp.cosTheta2;
    linkFlows.P2 -= b * constLinkComp.Vmx * linkComp.sinTheta2;

    linkFlows.Q2 = -(b + (0.5 * mp_B)) * linkInfo.v2 * linkInfo.v2;
    linkFlows.Q2 -= g * linkComp.Vmx * constLinkComp.sinTheta2;
    linkFlows.Q2 += b * linkComp.Vmx * constLinkComp.cosTheta2;
    linkFlows.seqID = linkInfo.seqID;
}

void AcLine::smallAngleCalc()
{
    // compute the trig functions
    linkComp.sinTheta1 = linkInfo.theta1;
    linkComp.cosTheta1 = 1.0;
    linkComp.sinTheta2 = linkInfo.theta2;
    linkComp.cosTheta2 = 1.0;

    // flows from bus 1 to bus 2
    linkFlows.P1 = ((g + (0.5 * mp_G)) / (tap * tap)) * linkInfo.v1 * linkInfo.v1;
    linkFlows.P1 -= g * linkComp.Vmx;
    linkFlows.P1 -= b * linkComp.Vmx * linkComp.sinTheta1;

    linkFlows.Q1 = -((b + (0.5 * mp_B)) / (tap * tap)) * linkInfo.v1 * linkInfo.v1;
    linkFlows.Q1 -= g * linkComp.Vmx * linkComp.sinTheta1;
    linkFlows.Q1 += b * linkComp.Vmx;

    // flows from bus 2 to bus 1
    linkFlows.P2 = (g + (0.5 * mp_G)) * linkInfo.v2 * linkInfo.v2;
    linkFlows.P2 -= g * linkComp.Vmx;
    linkFlows.P2 -= b * linkComp.Vmx * linkComp.sinTheta2;

    linkFlows.Q2 = -(b + (0.5 * mp_B)) * linkInfo.v2 * linkInfo.v2;
    linkFlows.Q2 -= g * linkComp.Vmx * linkComp.sinTheta2;
    linkFlows.Q2 += b * linkComp.Vmx;
    linkFlows.seqID = linkInfo.seqID;
}

void AcLine::smallAngleSimplifiedCalc()
{
    // compute the trig functions
    linkComp.sinTheta1 = linkInfo.theta1;
    linkComp.cosTheta1 = 1.0;
    linkComp.sinTheta2 = linkInfo.theta2;
    linkComp.cosTheta2 = 1.0;

    // flows from bus 1 to bus 2
    linkFlows.P1 = -b * linkComp.Vmx * linkComp.sinTheta1;

    linkFlows.Q1 = -((b + (0.5 * mp_B)) / (tap * tap)) * linkInfo.v1 * linkInfo.v1;
    linkFlows.Q1 += b * linkComp.Vmx;
    // flows from bus 2 to bus 1
    linkFlows.P2 = -b * linkComp.Vmx * linkComp.sinTheta2;
    linkFlows.Q2 = -(b + (0.5 * mp_B)) * linkInfo.v2 * linkInfo.v2;
    linkFlows.Q2 += b * linkComp.Vmx;
    linkFlows.seqID = linkInfo.seqID;
}

void AcLine::simplifiedDecoupledCalc()
{
    linkComp.sinTheta1 = sin(linkInfo.theta1);
    linkComp.cosTheta1 = cos(linkInfo.theta1);
    linkComp.sinTheta2 = -linkComp.sinTheta1;
    linkComp.cosTheta2 = linkComp.cosTheta1;

    linkFlows.P1 = -b * constLinkComp.Vmx * linkComp.sinTheta1;

    linkFlows.Q1 = -((b + (0.5 * mp_B)) / (tap * tap)) * linkInfo.v1 * linkInfo.v1;
    linkFlows.Q1 += b * linkComp.Vmx * constLinkComp.cosTheta1;

    linkFlows.P2 = -b * constLinkComp.Vmx * linkComp.sinTheta2;

    linkFlows.Q2 = -(b + (0.5 * mp_B)) * linkInfo.v2 * linkInfo.v2;
    linkFlows.Q2 += b * linkComp.Vmx * constLinkComp.cosTheta2;
    linkFlows.seqID = linkInfo.seqID;
}

void AcLine::smallAngleDecoupledCalc()
{
    linkComp.sinTheta1 = linkInfo.theta1;
    linkComp.cosTheta1 = 1;
    linkComp.sinTheta2 = linkInfo.theta2;
    linkComp.cosTheta2 = 1;

    linkFlows.P1 = ((g + (0.5 * mp_G)) / (tap * tap)) * constLinkInfo.v1 * constLinkInfo.v1;
    linkFlows.P1 -= g * constLinkComp.Vmx;
    linkFlows.P1 -= b * constLinkComp.Vmx * linkComp.sinTheta1;

    linkFlows.Q1 = -((b + (0.5 * mp_B)) / (tap * tap)) * linkInfo.v1 * linkInfo.v1;
    linkFlows.Q1 -= g * linkComp.Vmx * constLinkComp.sinTheta1;
    linkFlows.Q1 += b * linkComp.Vmx * constLinkComp.cosTheta1;

    linkFlows.P2 = (g + (0.5 * mp_G)) * constLinkInfo.v2 * constLinkInfo.v2;
    linkFlows.P2 -= g * constLinkComp.Vmx;
    linkFlows.P2 -= b * constLinkComp.Vmx * linkComp.sinTheta2;

    linkFlows.Q2 = -(b + (0.5 * mp_B)) * linkInfo.v2 * linkInfo.v2;
    linkFlows.Q2 -= g * linkComp.Vmx * constLinkComp.sinTheta2;
    linkFlows.Q2 += b * linkComp.Vmx * constLinkComp.cosTheta2;
    linkFlows.seqID = linkInfo.seqID;
}

void AcLine::linearCalc()
{
    const double deltaTheta1 = (linkInfo.theta1 - constLinkInfo.theta1) /
        2.0;  // divide by 2 so the angle change is attributed to both sides evenly
    const double deltaTheta2 = (linkInfo.theta2 - constLinkInfo.theta2) /
        2.0;  // divide by 2 so the angle change is attributed to both sides evenly
    const double deltaVoltage1 = (linkInfo.v1 - constLinkInfo.v1);
    const double deltaVoltage2 = (linkInfo.v2 - constLinkInfo.v2);
    linkFlows.P1 = constLinkFlows.P1 + (LinkDeriv.dP1dt1 * deltaTheta1);
    linkFlows.P1 += LinkDeriv.dP1dt2 * deltaTheta2;
    linkFlows.P1 += LinkDeriv.dP1dv1 * deltaVoltage1;
    linkFlows.P1 += LinkDeriv.dP1dv2 * deltaVoltage2;

    linkFlows.P2 = constLinkFlows.P2 + (LinkDeriv.dP2dt1 * deltaTheta1);
    linkFlows.P2 += LinkDeriv.dP2dt2 * deltaTheta2;
    linkFlows.P2 += LinkDeriv.dP2dv1 * deltaVoltage1;
    linkFlows.P2 += LinkDeriv.dP2dv2 * deltaVoltage2;

    linkFlows.Q1 = constLinkFlows.Q1 + (LinkDeriv.dQ1dt1 * deltaTheta1);
    linkFlows.Q1 += LinkDeriv.dQ1dt2 * deltaTheta2;
    linkFlows.Q1 += LinkDeriv.dQ1dv1 * deltaVoltage1;
    linkFlows.Q1 += LinkDeriv.dQ1dv2 * deltaVoltage2;

    linkFlows.Q2 = constLinkFlows.P2 + (LinkDeriv.dQ2dt1 * deltaTheta1);
    linkFlows.Q2 += LinkDeriv.dQ2dt2 * deltaTheta2;
    linkFlows.Q2 += LinkDeriv.dQ2dv1 * deltaVoltage1;
    linkFlows.Q2 += LinkDeriv.dQ2dv2 * deltaVoltage2;

    linkFlows.seqID = linkInfo.seqID;
}

void AcLine::fastDecoupledCalc()
{
    linkComp.sinTheta1 = linkInfo.theta1;
    linkComp.cosTheta1 = 1.0;

    linkComp.sinTheta2 = linkInfo.theta2;
    linkComp.cosTheta2 = 1.0;

    linkFlows.P1 = -b * constLinkComp.Vmx * linkComp.sinTheta1;

    linkFlows.Q1 =
        (-((b + (0.5 * mp_B)) / (tap * tap)) * linkInfo.v1 * linkInfo.v1) + (b * linkComp.Vmx);

    linkFlows.P2 = -b * constLinkComp.Vmx * linkComp.sinTheta2;

    linkFlows.Q2 = (-(b + (0.5 * mp_B)) * linkInfo.v2 * linkInfo.v2) + (b * linkComp.Vmx);

    linkFlows.seqID = linkInfo.seqID;
}

void AcLine::swOpenCalc()
{
    if (opFlags[SWITCH1_OPEN_FLAG]) {
        linkFlows.P1 = 0;
        linkFlows.Q1 = 0;
    } else {
        const double voltage2 = (b * linkInfo.v1) / tap / (b + (0.5 * mp_B));
        const double deltaTheta = -(((g + (0.5 * mp_G)) / (b + (0.5 * mp_B))) - (g / b));

        const double voltageMagnitude = linkInfo.v1 * voltage2 / tap;
        linkFlows.P1 = (((g + (0.5 * mp_G)) / (tap * tap)) * linkInfo.v1 * linkInfo.v1) -
            (g * voltageMagnitude) - (b * voltageMagnitude * deltaTheta);

        linkFlows.Q1 = (-((b + (0.5 * mp_B)) / (tap * tap)) * linkInfo.v1 * linkInfo.v1) +
            (b * voltageMagnitude);
    }
    if (opFlags[SWITCH2_OPEN_FLAG]) {
        linkFlows.P2 = 0;
        linkFlows.Q2 = 0;
    } else {
        // flows from bus 2 to bus
        const double voltage1 = (b * linkInfo.v2 * tap) / (b + (0.5 * mp_B));
        const double deltaTheta = -(((g + (0.5 * mp_G)) / (b + (0.5 * mp_B))) - (g / b));

        const double voltageMagnitude = linkInfo.v2 * voltage1 / tap;

        linkFlows.P2 = ((g + (0.5 * mp_G)) * linkInfo.v2 * linkInfo.v2) - (g * voltageMagnitude) -
            (b * voltageMagnitude * deltaTheta);
        linkFlows.Q2 = (-(b + (0.5 * mp_B)) * linkInfo.v2 * linkInfo.v2) + (b * voltageMagnitude);
    }

    linkFlows.seqID = linkInfo.seqID;
}

void AcLine::faultDeriv()
{
    LinkDeriv = {};
    if (!opFlags[SWITCH1_OPEN_FLAG]) {
        LinkDeriv.dP1dv1 = ((2 * ((g / fault) + (fault * mp_G))) / (tap * tap)) * linkInfo.v1;
        LinkDeriv.dQ1dv1 = ((-2 * ((b / fault) + (fault * mp_B))) / (tap * tap)) * linkInfo.v1;
    }

    if (!opFlags[SWITCH2_OPEN_FLAG]) {
        LinkDeriv.dP2dv2 = (2 * ((g / (1.0 - fault)) + ((1.0 - fault) * mp_G))) * linkInfo.v2;
        LinkDeriv.dQ2dv2 = (-2 * ((b / (1.0 - fault)) + ((1.0 - fault) * mp_B))) * linkInfo.v2;
    }
    LinkDeriv.seqID = linkInfo.seqID;
}
void AcLine::fullDeriv()
{
    // real power vs local states
    LinkDeriv.dP1dt1 =
        (g * linkComp.Vmx * linkComp.sinTheta1) - (b * linkComp.Vmx * linkComp.cosTheta1);
    LinkDeriv.dP1dv1 = ((2 * (g + (0.5 * mp_G)) / (tap * tap)) * linkInfo.v1) -
        ((g / tap) * linkInfo.v2 * linkComp.cosTheta1) -
        ((b / tap) * linkInfo.v2 * linkComp.sinTheta1);

    LinkDeriv.dP2dt2 =
        (g * linkComp.Vmx * linkComp.sinTheta2) - (b * linkComp.Vmx * linkComp.cosTheta2);
    LinkDeriv.dP2dv2 = ((2 * (g + (0.5 * mp_G))) * linkInfo.v2) -
        ((g / tap) * linkInfo.v1 * linkComp.cosTheta2) -
        ((b / tap) * linkInfo.v1 * linkComp.sinTheta2);

    // reactive power vs local states
    LinkDeriv.dQ1dt1 =
        (-g * linkComp.Vmx * linkComp.cosTheta1) - (b * linkComp.Vmx * linkComp.sinTheta1);
    LinkDeriv.dQ2dt2 =
        (-g * linkComp.Vmx * linkComp.cosTheta2) - (b * linkComp.Vmx * linkComp.sinTheta2);
    LinkDeriv.dQ1dv1 = ((-2 * (b + (0.5 * mp_B)) / (tap * tap)) * linkInfo.v1) -
        ((g / tap) * linkInfo.v2 * linkComp.sinTheta1) +
        ((b / tap) * linkInfo.v2 * linkComp.cosTheta1);
    LinkDeriv.dQ2dv2 = ((-2 * (b + (0.5 * mp_B))) * linkInfo.v2) -
        ((g / tap) * linkInfo.v1 * linkComp.sinTheta2) +
        ((b / tap) * linkInfo.v1 * linkComp.cosTheta2);

    // real power vs remote states
    LinkDeriv.dP1dv2 = -linkInfo.v1 * ((g * linkComp.cosTheta1) + (b * linkComp.sinTheta1)) / tap;
    LinkDeriv.dP2dv1 = -linkInfo.v2 * ((g * linkComp.cosTheta2) + (b * linkComp.sinTheta2)) / tap;
    LinkDeriv.dP1dt2 = -linkComp.Vmx * ((g * linkComp.sinTheta1) - (b * linkComp.cosTheta1));
    LinkDeriv.dP2dt1 = -linkComp.Vmx * ((g * linkComp.sinTheta2) - (b * linkComp.cosTheta2));

    // reactive power vs remote states

    LinkDeriv.dQ1dv2 = -linkInfo.v1 * ((g * linkComp.sinTheta1) - (b * linkComp.cosTheta1)) / tap;
    LinkDeriv.dQ2dv1 = -linkInfo.v2 * ((g * linkComp.sinTheta2) - (b * linkComp.cosTheta2)) / tap;
    LinkDeriv.dQ1dt2 = linkComp.Vmx * ((g * linkComp.cosTheta1) + (b * linkComp.sinTheta1));
    LinkDeriv.dQ2dt1 = linkComp.Vmx * ((g * linkComp.cosTheta2) + (b * linkComp.sinTheta2));
    LinkDeriv.seqID = linkInfo.seqID;
}
void AcLine::simplifiedDeriv()
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
    const double btap = b / tap;
    const double bvmx = b * linkComp.Vmx;

    LinkDeriv.dP1dt1 = -bvmx * linkComp.cosTheta1;
    LinkDeriv.dP1dv1 = -btap * linkInfo.v2 * linkComp.sinTheta1;

    LinkDeriv.dP2dt2 = -bvmx * linkComp.cosTheta2;
    LinkDeriv.dP2dv2 = -btap * linkInfo.v1 * linkComp.sinTheta2;

    // reactive power vs local states
    LinkDeriv.dQ1dt1 = -bvmx * linkComp.sinTheta1;
    LinkDeriv.dQ2dt2 = -bvmx * linkComp.sinTheta2;
    LinkDeriv.dQ1dv1 = ((-2 * (b + (0.5 * mp_B)) / (tap * tap)) * linkInfo.v1) +
        (btap * linkInfo.v2 * linkComp.cosTheta1);
    LinkDeriv.dQ2dv2 =
        ((-2 * (b + (0.5 * mp_B))) * linkInfo.v2) + (btap * linkInfo.v1 * linkComp.cosTheta2);

    // real power vs remote states
    LinkDeriv.dP1dv2 = -linkInfo.v1 * (btap * linkComp.sinTheta1);
    LinkDeriv.dP2dv1 = -linkInfo.v2 * (btap * linkComp.sinTheta2);
    LinkDeriv.dP1dt2 = bvmx * linkComp.cosTheta1;
    LinkDeriv.dP2dt1 = bvmx * linkComp.cosTheta2;

    // reactive power vs remote states

    LinkDeriv.dQ1dv2 = linkInfo.v1 * (btap * linkComp.cosTheta1);
    LinkDeriv.dQ2dv1 = linkInfo.v2 * (btap * linkComp.cosTheta2);
    LinkDeriv.dQ1dt2 = bvmx * linkComp.sinTheta1;
    LinkDeriv.dQ2dt1 = bvmx * linkComp.sinTheta2;
    LinkDeriv.seqID = linkInfo.seqID;
}

void AcLine::decoupledDeriv()
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
        (g * constLinkComp.Vmx * linkComp.sinTheta1) - (b * constLinkComp.Vmx * linkComp.cosTheta1);
    LinkDeriv.dP1dv1 = 0;

    LinkDeriv.dP2dt2 =
        (g * constLinkComp.Vmx * linkComp.sinTheta2) - (b * constLinkComp.Vmx * linkComp.cosTheta2);
    LinkDeriv.dP2dv2 = 0;

    // reactive power vs local states
    LinkDeriv.dQ1dt1 = 0;
    LinkDeriv.dQ2dt2 = 0;
    LinkDeriv.dQ1dv1 = ((-2 * (b + (0.5 * mp_B)) / (tap * tap)) * linkInfo.v1) -
        ((g / tap) * linkInfo.v2 * constLinkComp.sinTheta1) +
        ((b / tap) * linkInfo.v2 * constLinkComp.cosTheta1);
    LinkDeriv.dQ2dv2 = ((-2 * (b + (0.5 * mp_B))) * linkInfo.v2) -
        ((g / tap) * linkInfo.v1 * constLinkComp.sinTheta2) +
        ((b / tap) * linkInfo.v1 * constLinkComp.cosTheta2);

    // real power vs remote states
    LinkDeriv.dP1dv2 = 0;
    LinkDeriv.dP2dv1 = 0;
    LinkDeriv.dP1dt2 = -constLinkComp.Vmx * ((g * linkComp.sinTheta1) - (b * linkComp.cosTheta1));
    LinkDeriv.dP2dt1 = -constLinkComp.Vmx * ((g * linkComp.sinTheta2) - (b * linkComp.cosTheta2));

    // reactive power vs remote states

    LinkDeriv.dQ1dv2 =
        -linkInfo.v1 * ((g * constLinkComp.sinTheta1) - (b * constLinkComp.cosTheta1)) / tap;
    LinkDeriv.dQ2dv1 =
        -linkInfo.v2 * ((g * constLinkComp.sinTheta2) - (b * constLinkComp.cosTheta2)) / tap;
    LinkDeriv.dQ1dt2 = 0;
    LinkDeriv.dQ2dt1 = 0;
    LinkDeriv.seqID = linkInfo.seqID;
}

void AcLine::linearDeriv()
{
    // there is no update since the derivatives are constant

    LinkDeriv.seqID = linkInfo.seqID;
}

void AcLine::smallAngleDeriv()
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
    LinkDeriv.dP1dv1 = ((2 * (g + (0.5 * mp_G)) / (tap * tap)) * linkInfo.v1) -
        ((g / tap) * linkInfo.v2) - ((b / tap) * linkInfo.v2 * linkComp.sinTheta1);

    LinkDeriv.dP2dt2 = -b * linkComp.Vmx * linkComp.cosTheta2;
    LinkDeriv.dP2dv2 = ((2 * (g + (0.5 * mp_G))) * linkInfo.v2) - ((g / tap) * linkInfo.v1) -
        ((b / tap) * linkInfo.v1 * linkComp.sinTheta2);

    // reactive power vs local states
    LinkDeriv.dQ1dt1 = -g * linkComp.Vmx;
    LinkDeriv.dQ2dt2 = -g * linkComp.Vmx;
    LinkDeriv.dQ1dv1 = ((-2 * (b + (0.5 * mp_B)) / (tap * tap)) * linkInfo.v1) -
        ((g / tap) * linkInfo.v2 * linkComp.sinTheta1) + ((b / tap) * linkInfo.v2);
    LinkDeriv.dQ2dv2 = ((-2 * (b + (0.5 * mp_B))) * linkInfo.v2) -
        ((g / tap) * linkInfo.v1 * linkComp.sinTheta2) + ((b / tap) * linkInfo.v1);

    // real power vs remote states
    LinkDeriv.dP1dv2 = -linkInfo.v1 * (g + (b * linkComp.sinTheta1)) / tap;
    LinkDeriv.dP2dv1 = -linkInfo.v2 * (g + (b * linkComp.sinTheta2)) / tap;
    LinkDeriv.dP1dt2 = linkComp.Vmx * (b * linkComp.cosTheta1);
    LinkDeriv.dP2dt1 = linkComp.Vmx * (b * linkComp.cosTheta2);

    // reactive power vs remote states

    LinkDeriv.dQ1dv2 = -linkInfo.v1 * ((g * linkComp.sinTheta1) - (b * linkComp.cosTheta1)) / tap;
    LinkDeriv.dQ2dv1 = -linkInfo.v2 * ((g * linkComp.sinTheta2) - (b * linkComp.cosTheta2)) / tap;
    LinkDeriv.dQ1dt2 = linkComp.Vmx * g;
    LinkDeriv.dQ2dt1 = linkComp.Vmx * g;
    LinkDeriv.seqID = linkInfo.seqID;
}

void AcLine::simplifiedDecoupledDeriv()
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

    const double btap = b / tap;
    // reactive power vs local states
    LinkDeriv.dQ1dt1 = 0;
    LinkDeriv.dQ2dt2 = 0;
    LinkDeriv.dQ1dv1 = ((-2 * (b + (0.5 * mp_B)) / (tap * tap)) * linkInfo.v1) +
        (btap * linkInfo.v2 * constLinkComp.cosTheta1);
    LinkDeriv.dQ2dv2 =
        ((-2 * (b + (0.5 * mp_B))) * linkInfo.v2) + (btap * linkInfo.v1 * constLinkComp.cosTheta2);

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

void AcLine::smallAngleDecoupledDeriv()
{
    // real power vs local states
    LinkDeriv.dP1dt1 = -b * constLinkComp.Vmx;
    LinkDeriv.dP1dv1 = 0;

    LinkDeriv.dP2dt2 = -b * constLinkComp.Vmx;
    LinkDeriv.dP2dv2 = 0;

    // reactive power vs local states
    LinkDeriv.dQ1dt1 = 0;
    LinkDeriv.dQ2dt2 = 0;
    LinkDeriv.dQ1dv1 = ((-2 * (b + (0.5 * mp_B)) / (tap * tap)) * linkInfo.v1) -
        ((g / tap) * linkInfo.v2 * constLinkComp.sinTheta1) + ((b / tap) * linkInfo.v2);
    LinkDeriv.dQ2dv2 = ((-2 * (b + (0.5 * mp_B))) * linkInfo.v2) -
        ((g / tap) * linkInfo.v1 * constLinkComp.sinTheta2) + ((b / tap) * linkInfo.v1);

    // real power vs remote states
    LinkDeriv.dP1dv2 = 0;
    LinkDeriv.dP2dv1 = 0;
    LinkDeriv.dP1dt2 = constLinkComp.Vmx * (b * linkComp.cosTheta1);
    LinkDeriv.dP2dt1 = constLinkComp.Vmx * (b * linkComp.cosTheta2);

    // reactive power vs remote states

    LinkDeriv.dQ1dv2 = -linkInfo.v1 * ((g * constLinkComp.sinTheta1) - b) / tap;
    LinkDeriv.dQ2dv1 = -linkInfo.v2 * ((g * constLinkComp.sinTheta2) - b) / tap;
    LinkDeriv.dQ1dt2 = 0;
    LinkDeriv.dQ2dt1 = 0;
    LinkDeriv.seqID = linkInfo.seqID;
}

void AcLine::smallAngleSimplifiedDeriv()
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
    const double btap = b / tap;
    const double bvmx = b * linkComp.Vmx;

    LinkDeriv.dP1dt1 = -bvmx;
    LinkDeriv.dP1dv1 = -btap * linkInfo.v2 * linkComp.sinTheta1;

    LinkDeriv.dP2dt2 = -bvmx;
    LinkDeriv.dP2dv2 = -btap * linkInfo.v1 * linkComp.sinTheta2;

    // reactive power vs local states
    LinkDeriv.dQ1dt1 = 0;
    LinkDeriv.dQ2dt2 = 0;
    LinkDeriv.dQ1dv1 =
        ((-2 * (b + (0.5 * mp_B)) / (tap * tap)) * linkInfo.v1) + (btap * linkInfo.v2);
    LinkDeriv.dQ2dv2 = ((-2 * (b + (0.5 * mp_B))) * linkInfo.v2) + (btap * linkInfo.v1);

    // real power vs remote states
    LinkDeriv.dP1dv2 = -linkInfo.v1 * (btap * linkComp.sinTheta1);
    LinkDeriv.dP2dv1 = -linkInfo.v2 * (btap * linkComp.sinTheta2);
    LinkDeriv.dP1dt2 = bvmx;
    LinkDeriv.dP2dt1 = bvmx;

    // reactive power vs remote states

    LinkDeriv.dQ1dv2 = linkInfo.v1 * btap;
    LinkDeriv.dQ2dv1 = linkInfo.v2 * btap;
    LinkDeriv.dQ1dt2 = 0;
    LinkDeriv.dQ2dt1 = 0;
    LinkDeriv.seqID = linkInfo.seqID;
}

void AcLine::fastDecoupledDeriv()
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
    LinkDeriv.dQ1dv1 =
        ((-2 * (b + (0.5 * mp_B)) / (tap * tap)) * linkInfo.v1) + ((b / tap) * linkInfo.v2);
    LinkDeriv.dQ2dv2 = ((-2 * (b + (0.5 * mp_B))) * linkInfo.v2) + ((b / tap) * linkInfo.v1);

    // real power vs remote states
    LinkDeriv.dP1dv2 = 0;
    LinkDeriv.dP2dv1 = 0;
    LinkDeriv.dP1dt2 = constLinkComp.Vmx * b;
    LinkDeriv.dP2dt1 = constLinkComp.Vmx * b;

    // reactive power vs remote states

    LinkDeriv.dQ1dv2 = (linkInfo.v1 * b) / tap;
    LinkDeriv.dQ2dv1 = (linkInfo.v2 * b) / tap;
    LinkDeriv.dQ1dt2 = 0;
    LinkDeriv.dQ2dt1 = 0;
    LinkDeriv.seqID = linkInfo.seqID;
}

void AcLine::swOpenDeriv()
{
    LinkDeriv = {};

    // flows from bus 2 to bus

    const double admittanceFactor = 1.0 / (b + (0.5 * mp_B));
    const double deltaTerm = -(((g + (0.5 * mp_G)) * admittanceFactor) - (g / b));

    if (!opFlags[SWITCH1_OPEN_FLAG]) {
        const double inverseTapSquared = 1.0 / (tap * tap);
        LinkDeriv.dP1dv1 = 2.0 * (g + (0.5 * mp_G)) * inverseTapSquared * linkInfo.v1;
        LinkDeriv.dP1dv1 -= 2.0 * g * b * inverseTapSquared * linkInfo.v1 * admittanceFactor;
        LinkDeriv.dP1dv1 +=
            2.0 * b * b * inverseTapSquared * linkInfo.v1 * admittanceFactor * deltaTerm;

        LinkDeriv.dQ1dv1 = -2.0 * (b + (0.5 * mp_B)) * inverseTapSquared * linkInfo.v1;
        LinkDeriv.dQ1dv1 += 2.0 * b * b * inverseTapSquared * linkInfo.v1 * admittanceFactor;
    }

    if (!opFlags[SWITCH2_OPEN_FLAG]) {
        LinkDeriv.dP2dv2 = 2.0 * (g + (0.5 * mp_G)) * linkInfo.v2;
        LinkDeriv.dP2dv2 -= 2.0 * g * b * linkInfo.v2 * admittanceFactor;
        LinkDeriv.dP2dv2 += 2.0 * b * b * linkInfo.v2 * admittanceFactor * deltaTerm;

        LinkDeriv.dQ2dv2 = -2.0 * (b + (0.5 * mp_B)) * linkInfo.v2;
        LinkDeriv.dQ2dv2 += 2.0 * b * b * linkInfo.v2 * admittanceFactor;
    }
    LinkDeriv.seqID = linkInfo.seqID;

    /*
    if (!opFlags[SWITCH1_OPEN_FLAG])
    {
    LinkDeriv.dP1dv1 = 2 * (g + mp_G) / (tap * tap) * linkInfo.v1;
    LinkDeriv.dQ1dv1 = -2 * (b  + mp_B) / (tap * tap) * linkInfo.v1;
    }

    if (!opFlags[SWITCH2_OPEN_FLAG])
    {
    LinkDeriv.dP2dv2 = 2 * (g  + mp_G) * linkInfo.v2;
    LinkDeriv.dQ2dv2 = -2 * (b  + mp_B) * linkInfo.v2;
    }
    */
    LinkDeriv.seqID = linkInfo.seqID;
}

void AcLine::loadApproxFunctions()
{
    // load up the member function pointer array to point to the correct function
    flowCalc[indexVal(ApproxKeyMask::NONE)] = &AcLine::fullCalc;
    flowCalc[indexVal(ApproxKeyMask::DECOUPLED)] = &AcLine::decoupledCalc;
    flowCalc[indexVal(ApproxKeyMask::SM_ANGLE)] = &AcLine::smallAngleCalc;
    flowCalc[indexVal(ApproxKeyMask::SM_ANGLE_DECOUPLED)] = &AcLine::smallAngleDecoupledCalc;
    flowCalc[indexVal(ApproxKeyMask::SIMPLIFIED)] = &AcLine::simplifiedCalc;
    flowCalc[indexVal(ApproxKeyMask::SIMPLIFIED_DECOUPLED)] = &AcLine::simplifiedDecoupledCalc;
    flowCalc[indexVal(ApproxKeyMask::SIMPLIFIED_SM_ANGLE)] = &AcLine::smallAngleSimplifiedCalc;
    flowCalc[indexVal(ApproxKeyMask::FAST_DECOUPLED)] = &AcLine::fastDecoupledCalc;
    flowCalc[indexVal(ApproxKeyMask::LINEAR)] = &AcLine::linearCalc;

    derivCalc[indexVal(ApproxKeyMask::NONE)] = &AcLine::fullDeriv;
    derivCalc[indexVal(ApproxKeyMask::DECOUPLED)] = &AcLine::decoupledDeriv;
    derivCalc[indexVal(ApproxKeyMask::SM_ANGLE)] = &AcLine::smallAngleDeriv;
    derivCalc[indexVal(ApproxKeyMask::SM_ANGLE_DECOUPLED)] = &AcLine::smallAngleDecoupledDeriv;
    derivCalc[indexVal(ApproxKeyMask::SIMPLIFIED)] = &AcLine::simplifiedDeriv;
    derivCalc[indexVal(ApproxKeyMask::SIMPLIFIED_DECOUPLED)] = &AcLine::simplifiedDecoupledDeriv;
    derivCalc[indexVal(ApproxKeyMask::SIMPLIFIED_SM_ANGLE)] = &AcLine::smallAngleSimplifiedDeriv;
    derivCalc[indexVal(ApproxKeyMask::FAST_DECOUPLED)] = &AcLine::fastDecoupledDeriv;
    derivCalc[indexVal(ApproxKeyMask::LINEAR)] = &AcLine::linearDeriv;
}
}  // namespace griddyn
