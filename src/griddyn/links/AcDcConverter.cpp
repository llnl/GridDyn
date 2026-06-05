/*
 * Copyright (c) 2014-2026, Lawrence Livermore National Security
 * See the top-level NOTICE for additional details. All rights reserved.
 * SPDX-License-Identifier: BSD-3-Clause
 */

#include "AcDcConverter.h"

#include "../GridArea.h"
#include "../blocks/DelayBlock.h"
#include "../blocks/PidBlock.h"
#include "../primary/DcBus.h"
#include "core/CoreExceptions.h"
#include "core/CoreObjectTemplates.hpp"
#include "gmlc/utilities/vectorOps.hpp"
#include "utilities/MatrixDataSparse.hpp"
#include <cmath>
#include <cstring>
#include <numbers>
#include <string>
namespace griddyn::links {
static constexpr double kSqrt2 = std::numbers::sqrt2_v<double>;
static constexpr double k3sq2 = 3.0 * kSqrt2 / kPI;
static constexpr double k3sq2sq = k3sq2 * k3sq2;

using units::convert;
using units::puMW;
using units::unit;
constexpr char rectifierName[] = "rectifier_$";
constexpr char inverterName[] = "inverter_$";
constexpr char bidirectionalName[] = "acdcConveter_$";

static std::string modeToName(AcDcConverter::Mode mode, const std::string& name)
{
    if (!name.empty()) {
        return name;
    }
    switch (mode) {
        case AcDcConverter::Mode::RECTIFIER:
            return rectifierName;
        case AcDcConverter::Mode::INVERTER:
            return inverterName;
        case AcDcConverter::Mode::BIDIRECTIONAL:
        default:
            return bidirectionalName;
    }
}

AcDcConverter::AcDcConverter(double resistanceParameter,
                             double reactanceParameter,
                             const std::string& objName):
    Link(objName), r(resistanceParameter), x(reactanceParameter)
{
    buildSubsystem();
}

AcDcConverter::AcDcConverter(Mode opType, const std::string& objName):
    Link(modeToName(opType, objName)), type(opType)
{
    if (opType == Mode::INVERTER) {
        dirMult = -1.0;
    }
    buildSubsystem();
}
AcDcConverter::AcDcConverter(const std::string& objName): Link(objName)
{
    buildSubsystem();
}
void AcDcConverter::buildSubsystem()
{
    tap = kBigNum;
    opFlags.set(DC_CAPABLE);
    opFlags.set(ADJUSTABLE_P);
    opFlags.set(ADJUSTABLE_Q);
    firingAngleControl =
        makeOwningPtr<blocks::PidBlock>(-dirMult * mp_Kp, -dirMult * mp_Ki, 0, "angleControl");
    addSubObject(firingAngleControl.get());
    powerLevelControl =
        makeOwningPtr<blocks::PidBlock>(mp_controlKp, mp_controlKi, 0, "powerControl");
    addSubObject(powerLevelControl.get());
    controlDelay = makeOwningPtr<blocks::DelayBlock>(tD, "controlDelay");
    addSubObject(controlDelay.get());
}

AcDcConverter::~AcDcConverter() = default;
CoreObject* AcDcConverter::clone(CoreObject* obj) const
{
    auto* nobj = cloneBase<AcDcConverter, Link>(this, obj);
    if (nobj == nullptr) {
        return obj;
    }
    nobj->Idcmax = Idcmax;
    nobj->Idcmin = Idcmin;
    nobj->mp_Ki = mp_Ki;
    nobj->mp_Kp = mp_Kp;
    nobj->mp_controlKi = mp_controlKi;
    nobj->mp_controlKp = mp_controlKp;
    nobj->tD = tD;
    nobj->controlMode = controlMode;
    nobj->vTarget = vTarget;
    nobj->type = type;
    nobj->dirMult = dirMult;
    nobj->tap = tap;
    nobj->angle = angle;
    nobj->maxAngle = maxAngle;
    nobj->minAngle = minAngle;
    firingAngleControl->clone(nobj->firingAngleControl.get());
    powerLevelControl->clone(nobj->powerLevelControl.get());
    controlDelay->clone(nobj->controlDelay.get());
    return nobj;
}

void AcDcConverter::timestep(CoreTime time, const IOdata& /*inputs*/, const SolverMode& /*sMode*/)
{
    // TODO(phlpt): This function is incorrect.
    if (!isEnabled()) {
        return;
    }
    updateLocalCache();

    /*if (scheduled)
{
Psched=sched->timestepP(time);
}*/
    prevTime = time;
}

// it may make more sense to have the dc bus as bus 1 but then the equations wouldn't be
// symmetric with the the rectifier
void AcDcConverter::updateBus(GridBus* bus, index_t /*busnumber*/)
{
    if (dynamic_cast<DcBus*>(bus) != nullptr) {
        Link::updateBus(bus, 2);  // bus 2 must be the dc bus
    } else {
        Link::updateBus(bus, 1);
    }
}

double AcDcConverter::getMaxTransfer() const
{
    if (isConnected()) {
        return linkInfo.v2 * (std::max)(std::abs(Idcmax), std::abs(Idcmin));
    }
    return 0.0;
}

// set properties
void AcDcConverter::set(std::string_view param, std::string_view val)
{
    if (param == "mode") {
        if (val == "rectifier") {
            type = Mode::RECTIFIER;
            if (dirMult < 0.0) {
                firingAngleControl->set("p", -mp_Kp);
                firingAngleControl->set("i", -mp_Ki);
            }
            dirMult = 1.0;
        } else if (val == "inverter") {
            type = Mode::INVERTER;
            if (dirMult > 0) {
                firingAngleControl->set("p", mp_Kp);
                firingAngleControl->set("i", mp_Ki);
            }
            dirMult = -1.0;
        } else if (val == "bidirectional") {
            type = Mode::BIDIRECTIONAL;
            if (dirMult < 0) {
                firingAngleControl->set("p", -mp_Kp);
                firingAngleControl->set("i", -mp_Ki);
            }
            dirMult = 1.0;
        } else {
            throw(InvalidParameterValue(param));
        }
    } else {
        Link::set(param, val);
    }
}

void AcDcConverter::set(std::string_view param, double val, unit unitType)
{
    if (param == "r") {
        r = val;
    } else if ((param == "l") || (param == "x")) {
        x = val;
    } else if ((param == "p") || (param == "pset")) {
        Pset = convert(val, unitType, puMW, systemBasePower);
        Pset = (Pset < 0) ? dirMult * Pset : Pset;
        opFlags.set(FIXED_TARGET_POWER);
        controlMode = ControlMode::POWER;
        if (opFlags[DYN_INITIALIZED]) {
            tap = linkInfo.v2 * linkInfo.v1 / Pset;
        }
    } else if ((param == "tapi") || (param == "mi") || (param == "tap")) {
        tap = val;
        baseTap = val;
    } else if ((param == "angle") || (param == "tapangle") || (param == "alpha") ||
               (param == "gamma")) {
        angle = val;
    } else if ((param == "idcmax") || (param == "imax")) {
        Idcmax = val;
        powerLevelControl->set("omax", Idcmax);
    } else if ((param == "idcmin") || (param == "imin")) {
        Idcmin = val;
        powerLevelControl->set("omax", Idcmin);
    } else if ((param == "gammamax") || (param == "alphamax") || (param == "anglemax") ||
               (param == "maxangle")) {
        maxAngle = val;
        if (type == Mode::INVERTER) {
            firingAngleControl->set("max", cos(kPI - maxAngle));
        } else {
            firingAngleControl->set("max", cos(maxAngle));
        }
    } else if ((param == "gammamin") || (param == "alphamin") || (param == "anglemin") ||
               (param == "minangle")) {
        minAngle = val;
        if (type == Mode::INVERTER) {
            firingAngleControl->set("min", cos(kPI - minAngle));
        } else {
            firingAngleControl->set("min", cos(minAngle));
        }
    } else if ((param == "ki") || (param == "igain") || (param == "i")) {
        mp_Ki = val;
        firingAngleControl->set("i", dirMult * val);
    } else if ((param == "kp") || (param == "pgain")) {
        mp_Kp = val;
        firingAngleControl->set("p", dirMult * val);
    } else if ((param == "t") || (param == "tp")) {
        mp_Ki = 1.0 / val;
        firingAngleControl->set("i", val);
    } else if ((param == "controlki") || (param == "controli")) {
        mp_controlKi = val;
        powerLevelControl->set("i", val);
    } else if ((param == "controlkp") || (param == "controlgain")) {
        mp_controlKp = val;
        powerLevelControl->set("p", val);
    } else if ((param == "tm") || (param == "td")) {
        tD = val;
        controlDelay->set("t", tD);
    } else {
        Link::set(param, val, unitType);
    }
}

void AcDcConverter::pFlowObjectInitializeA(CoreTime /*time0*/, std::uint32_t /*flags*/)
{
    const double voltage1 = B1->getVoltage();
    const double voltage2 = B2->getVoltage();
    if (opFlags[FIXED_TARGET_POWER]) {
        Idc = Pset / voltage2;
    } else {
        Idc = voltage1 / tap;
    }
    angle = (voltage1 + ((3 / kPI) * x * Idc)) / (k3sq2 * voltage1);
    updateLocalCache();
    offsets.local().local.algSize = 1;
    offsets.local().local.jacSize = 4;
}

void AcDcConverter::dynObjectInitializeA(CoreTime time0, std::uint32_t flags)
{
    updateLocalCache();
    if (opFlags[FIXED_TARGET_POWER]) {
        tap = linkInfo.v2 * linkInfo.v1 / Pset;
    }
    baseTap = tap;
    firingAngleControl->dynInitializeA(time0, flags);

    vTarget = B2->getVoltage();
    powerLevelControl->dynInitializeA(time0, flags);
    controlDelay->dynInitializeA(time0, flags);
    if (tD < 0.0001)  // check if control are needed
    {
        controlDelay->disable();
    }
    if (controlMode != ControlMode::VOLTAGE) {
        powerLevelControl->disable();
        controlDelay->disable();
    }
    offsets.local().local.algSize = 1;
    offsets.local().local.jacSize = 4;
}

static IOdata gZeroVec{0.0, 0.0, 0.0};

void AcDcConverter::dynObjectInitializeB(const IOdata& /*inputs*/,
                                         const IOdata& /*desiredOutput*/,
                                         IOdata& fieldSet)
{
    firingAngleControl->dynInitializeB(noInputs, {angle}, fieldSet);
    if (controlMode == ControlMode::VOLTAGE) {
        vTarget = B2->getVoltage();
        powerLevelControl->dynInitializeB({vTarget}, noInputs, fieldSet);
        if (tD > 0.0001) {
            controlDelay->dynInitializeB(noInputs, fieldSet, fieldSet);
        }
    }
}

void AcDcConverter::ioPartialDerivatives(id_type_t busId,
                                         const StateData& stateDataValue,
                                         MatrixData<double>& matrixDataValue,
                                         const IOlocs& inputLocs,
                                         const SolverMode& sMode)
{
    if (!(isEnabled())) {
        return;
    }
    if (inputLocs[VOLTAGE_IN_LOCATION] == kNullLocation) {
        return;
    }
    updateLocalCache(noInputs, stateDataValue, sMode);
    // int mode = B1->getMode(sMode) * 4 + B2->getMode(sMode);

    /*
linkInfo.P1 = dirMult*linkInfo.v2 * Idc;
linkInfo.P2 = -linkInfo.P1;
double sr = k3sq2*linkInfo.v1*Idc;

linkInfo.Q1 = -std::sqrt(sr*sr - linkInfo.P1*linkInfo.P1);
*/
    const index_t vLoc = inputLocs[VOLTAGE_IN_LOCATION];
    if (isDynamic(sMode)) {
        if (busId == B2->getID()) {
            matrixDataValue.assign(POUT_LOCATION, vLoc, -dirMult * Idc);
        } else {
            matrixDataValue.assign(QOUT_LOCATION,
                                   vLoc,
                                   -Idc * k3sq2sq * linkInfo.v1 /
                                       std::sqrt((k3sq2sq * linkInfo.v1 * linkInfo.v1) -
                                                 (linkInfo.v2 * linkInfo.v2)));
        }
    } else {
        /*
    Idc = (opFlags[FIXED_TARGET_POWER]) ? Pset / linkInfo.v2 : linkInfo.v1 / tap;

    linkInfo.P1 = linkInfo.v2 * Idc;
    linkInfo.P2 = -linkInfo.P1;
    double sr = k3sq2*linkInfo.v1*Idc;

    linkInfo.Q1 = std::sqrt(sr*sr - linkInfo.P1*linkInfo.P1);
    */
        if (busId == B2->getID()) {
            if (!opFlags[FIXED_TARGET_POWER]) {
                matrixDataValue.assign(POUT_LOCATION, vLoc, dirMult * linkInfo.v1 / tap);
            }
        } else {
            const double temp =
                std::sqrt((k3sq2sq * linkInfo.v1 * linkInfo.v1) - (linkInfo.v2 * linkInfo.v2));
            if (opFlags[FIXED_TARGET_POWER]) {
                matrixDataValue.assign(QOUT_LOCATION,
                                       vLoc,
                                       -k3sq2sq * Pset * linkInfo.v1 / (linkInfo.v2 * temp));
            } else {
                matrixDataValue.assign(POUT_LOCATION, vLoc, -dirMult * linkInfo.v2 / tap);
                matrixDataValue.assign(QOUT_LOCATION,
                                       vLoc,
                                       ((-1.0 / tap) * temp) -
                                           ((linkInfo.v1 * linkInfo.v1 / (tap * temp)) * k3sq2sq));
            }
        }
    }
}

void AcDcConverter::outputPartialDerivatives(const IOdata& /*inputs*/,
                                             const StateData& stateDataValue,
                                             MatrixData<double>& matrixDataValue,
                                             const SolverMode& sMode)
{
    if (!(isEnabled())) {
        return;
    }
    updateLocalCache(noInputs, stateDataValue, sMode);
    auto algOffset = offsets.getAlgOffset(sMode);
    if (isDynamic(sMode)) {
        /*
    linkInfo.P1 = dirMult*linkInfo.v2 * Idc;
    linkInfo.P2 = -linkInfo.P1;
    double sr = k3sq2*linkInfo.v1*Idc;

    linkInfo.Q1 = -std::sqrt(sr*sr - linkInfo.P1*linkInfo.P1);
    */
        matrixDataValue.assign(POUT_LOCATION, algOffset, dirMult * linkInfo.v2);
        matrixDataValue.assign(QOUT_LOCATION, algOffset, linkFlows.Q1 / Idc);
        matrixDataValue.assign(POUT_LOCATION + 2, algOffset, -dirMult * linkInfo.v2);
    }
}
void AcDcConverter::outputPartialDerivatives(id_type_t busId,
                                             const StateData& stateDataValue,
                                             MatrixData<double>& matrixDataValue,
                                             const SolverMode& sMode)
{
    if (!(isEnabled())) {
        return;
    }
    updateLocalCache(noInputs, stateDataValue, sMode);

    // int mode = B1->getMode(sMode) * 4 + B2->getMode(sMode);
    auto bus1VoltageOffset = B1->getOutputLoc(sMode, VOLTAGE_IN_LOCATION);
    auto bus2VoltageOffset = B2->getOutputLoc(sMode, VOLTAGE_IN_LOCATION);

    auto algOffset = offsets.getAlgOffset(sMode);

    //    md.assign(B1Voffset, B2Voffset, Q1V2);
    //    md.assign(B2Voffset, B1Voffset, Q2V1);
    if (isDynamic(sMode)) {
        /*
      linkInfo.P1 = dirMult*linkInfo.v2 * Idc;
      linkInfo.P2 = -linkInfo.P1;
      double sr = k3sq2*linkInfo.v1*Idc;

      linkInfo.Q1 = -std::sqrt(sr*sr - linkInfo.P1*linkInfo.P1);
      */
        if (busId == B2->getID()) {
            matrixDataValue.assign(POUT_LOCATION, algOffset, -dirMult * linkInfo.v2);
        } else {
            matrixDataValue.assignCheckCol(POUT_LOCATION, bus2VoltageOffset, dirMult * Idc);
            matrixDataValue.assign(POUT_LOCATION, algOffset, dirMult * linkInfo.v2);
            matrixDataValue.assignCheckCol(QOUT_LOCATION,
                                           bus2VoltageOffset,
                                           Idc * linkInfo.v2 /
                                               std::sqrt(
                                                   ((k3sq2 * k3sq2) * linkInfo.v1 * linkInfo.v1) -
                                                   (linkInfo.v2 * linkInfo.v2)));
            matrixDataValue.assign(QOUT_LOCATION, algOffset, linkFlows.Q1 / Idc);
        }
    } else {
        /*
    Idc = (opFlags[FIXED_TARGET_POWER]) ? Pset / linkInfo.v2 : linkInfo.v1 / tap;

    linkInfo.P1 = linkInfo.v2 * Idc;
    linkInfo.P2 = -linkInfo.P1;
    double sr = k3sq2*linkInfo.v1*Idc;

    linkInfo.Q1 = std::sqrt(sr*sr - linkInfo.P1*linkInfo.P1);
    */
        if (busId == B2->getID()) {
            if (!opFlags[FIXED_TARGET_POWER]) {
                matrixDataValue.assignCheckCol(POUT_LOCATION,
                                               bus1VoltageOffset,
                                               dirMult * linkInfo.v2 / tap);
            }
        } else {
            if (bus2VoltageOffset != kNullLocation) {
                const double temp =
                    std::sqrt((k3sq2sq * linkInfo.v1 * linkInfo.v1) - (linkInfo.v2 * linkInfo.v2));
                if (opFlags[FIXED_TARGET_POWER]) {
                    matrixDataValue.assignCheckCol(QOUT_LOCATION,
                                                   bus2VoltageOffset,
                                                   (Pset / temp) +
                                                       ((Pset * temp) /
                                                        (linkInfo.v2 * linkInfo.v2)));
                } else {
                    matrixDataValue.assignCheckCol(POUT_LOCATION,
                                                   bus2VoltageOffset,
                                                   -dirMult * linkInfo.v1 / tap);
                    matrixDataValue.assignCheckCol(QOUT_LOCATION,
                                                   bus2VoltageOffset,
                                                   (linkInfo.v1 / tap) * linkInfo.v2 / temp);
                }
            }
        }
    }
}

count_t AcDcConverter::outputDependencyCount(index_t /*num*/, const SolverMode& sMode) const
{
    return (isDynamic(sMode)) ? 2 : 1;
}

void AcDcConverter::jacobianElements(const IOdata& /*inputs*/,
                                     const StateData& stateDataValue,
                                     MatrixData<double>& matrixDataValue,
                                     const IOlocs& /*inputLocs*/,
                                     const SolverMode& sMode)
{
    auto bus1Locs = B1->getOutputLocs(sMode);
    auto bus1VoltageOffset = bus1Locs[VOLTAGE_IN_LOCATION];
    auto bus2Locs = B2->getOutputLocs(sMode);
    auto bus2VoltageOffset = bus2Locs[VOLTAGE_IN_LOCATION];
    updateLocalCache(noInputs, stateDataValue, sMode);
    if (isDynamic(sMode)) {
        auto loc = offsets.getLocations(stateDataValue, sMode, this);
        auto refAlg = loc.algOffset;
        IOlocs argL{bus2VoltageOffset};
        IOdata controlSignalInput{linkInfo.v2 - vTarget};

        index_t refLoc;
        MatrixDataSparse<double> translatedAngleJacobian;
        MatrixDataSparse<double> translatedInputJacobian;

        if (refAlg != kNullLocation) {
            if (controlMode == ControlMode::VOLTAGE) {
                double powerAdjustment;
                if (tD > 0.0001) {
                    powerAdjustment =
                        controlDelay->getOutput(controlSignalInput, stateDataValue, sMode);
                    refLoc = controlDelay->getOutputLoc(sMode);
                } else {
                    powerAdjustment =
                        powerLevelControl->getOutput(controlSignalInput, stateDataValue, sMode);
                    refLoc = powerLevelControl->getOutputLoc(sMode);
                }

                tap = baseTap / (1.0 + (dirMult * baseTap * powerAdjustment));
                translatedInputJacobian.assign(0, refLoc, -dirMult * linkInfo.v1);
            }
            const double currentReference = linkInfo.v1 / tap;
            controlSignalInput[0] = loc.algStateLoc[0] - currentReference;
            const double controlAngle =
                firingAngleControl->getOutput(controlSignalInput, stateDataValue, sMode);
            refLoc = firingAngleControl->getOutputLoc(sMode);
            matrixDataValue.assignCheckCol(refAlg, bus1VoltageOffset, k3sq2 * controlAngle);
            matrixDataValue.assignCheckCol(refAlg, bus2VoltageOffset, -1);
            matrixDataValue.assign(refAlg, refAlg, -(3 / kPI) * x);
            matrixDataValue.assign(refAlg, refLoc, k3sq2 * linkInfo.v1);

            translatedInputJacobian.assign(0, refAlg, 1);
        }
        // manage the input for the
        argL[0] = 0;
        firingAngleControl->jacobianElements(
            controlSignalInput, stateDataValue, translatedAngleJacobian, argL, sMode);

        translatedInputJacobian.assign(0, bus1VoltageOffset, -(1.0 / tap));
        translatedAngleJacobian.cascade(translatedInputJacobian, 0);
        matrixDataValue.merge(translatedAngleJacobian);

        if (controlMode == ControlMode::VOLTAGE) {
            controlSignalInput[0] = linkInfo.v2 - vTarget;
            argL[0] = bus2VoltageOffset;
            powerLevelControl->jacobianElements(
                controlSignalInput, stateDataValue, matrixDataValue, argL, sMode);
            if (tD > 0.0001) {
                controlSignalInput[0] =
                    powerLevelControl->getOutput(controlSignalInput, stateDataValue, sMode);
                argL[0] = powerLevelControl->getOutputLoc(sMode);
                controlDelay->jacobianElements(
                    controlSignalInput, stateDataValue, matrixDataValue, argL, sMode);
            }
        }
    } else {
        auto offset = offsets.getAlgOffset(sMode);

        // resid[offset] = k3sq2*linkInfo.v1*sD.state[offset] - 3 / kPI*x*Idc - linkInfo.v2;

        matrixDataValue.assign(offset, offset, k3sq2 * linkInfo.v1);
        if (opFlags[FIXED_TARGET_POWER]) {
            matrixDataValue.assignCheckCol(offset,
                                           bus1VoltageOffset,
                                           k3sq2 * stateDataValue.state[offset]);
            matrixDataValue.assignCheckCol(offset,
                                           bus2VoltageOffset,
                                           ((3.0 / kPI) * x * Pset / (linkInfo.v2 * linkInfo.v2)) -
                                               1.0);
        } else {
            matrixDataValue.assignCheckCol(offset,
                                           bus1VoltageOffset,
                                           (k3sq2 * stateDataValue.state[offset]) -
                                               ((3 / kPI) * x / tap));
            matrixDataValue.assignCheckCol(offset, bus2VoltageOffset, -1);
        }
    }
}

void AcDcConverter::residual(const IOdata& inputs,
                             const StateData& stateDataValue,
                             double resid[],
                             const SolverMode& sMode)
{
    updateLocalCache(inputs, stateDataValue, sMode);
    if (isDynamic(sMode)) {
        auto loc = offsets.getLocations(stateDataValue, resid, sMode, this);
        IOdata controlSignalInput{linkInfo.v2 - vTarget};
        if (controlMode == ControlMode::VOLTAGE) {
            const double powerAdjustment = (tD > 0.0001) ?
                controlDelay->getOutput(controlSignalInput, stateDataValue, sMode) :
                powerLevelControl->getOutput(controlSignalInput, stateDataValue, sMode);
            tap = baseTap / (1.0 + (dirMult * baseTap * powerAdjustment));
        }
        const double currentReference = linkInfo.v1 / tap;
        controlSignalInput[0] = loc.algStateLoc[0] - currentReference;
        const auto controlAngle =
            firingAngleControl->getOutput(controlSignalInput, stateDataValue, sMode);
        loc.destLoc[0] = (k3sq2 * linkInfo.v1 * controlAngle) -
            ((3.0 / kPI) * x * loc.algStateLoc[0]) - linkInfo.v2;

        firingAngleControl->residual(controlSignalInput, stateDataValue, resid, sMode);
        if (controlMode == ControlMode::VOLTAGE) {
            controlSignalInput[0] = linkInfo.v2 - vTarget;
            powerLevelControl->residual(controlSignalInput, stateDataValue, resid, sMode);
            if (tD > 0.0001) {
                controlSignalInput[0] =
                    powerLevelControl->getOutput(controlSignalInput, stateDataValue, sMode);
                controlDelay->residual(controlSignalInput, stateDataValue, resid, sMode);
            }
        }
    } else {
        auto offset = offsets.getAlgOffset(sMode);
        Idc = (opFlags[FIXED_TARGET_POWER]) ? Pset / linkInfo.v2 : linkInfo.v1 / tap;

        resid[offset] = (k3sq2 * linkInfo.v1 * stateDataValue.state[offset]) -
            ((3 / kPI) * x * Idc) - linkInfo.v2;
    }
}

void AcDcConverter::setState(CoreTime time,
                             const double state[],
                             const double dstateDt[],
                             const SolverMode& sMode)
{
    if (isDynamic(sMode)) {
        Idc = state[offsets.getAlgOffset(sMode)];
        for (const auto& sub : getSubObjects()) {
            if (sub->isEnabled()) {
                sub->setState(time, state, dstateDt, sMode);
            }
        }
        angle = firingAngleControl->getOutput();
    } else {
        auto offset = offsets.getAlgOffset(sMode);
        angle = state[offset];
        if (opFlags[FIXED_TARGET_POWER]) {
            Idc = Pset / B2->getVoltage(state, sMode);
        } else {
            Idc = B1->getVoltage(state, sMode) / tap;
        }
    }
    prevTime = time;
    updateLocalCache();
}

void AcDcConverter::guessState(CoreTime time,
                               double state[],
                               double dstateDt[],
                               const SolverMode& sMode)
{
    if (isDynamic(sMode)) {
        state[offsets.getAlgOffset(sMode)] = Idc;
        for (const auto& sub : getSubObjects()) {
            if (sub->isEnabled()) {
                sub->guessState(time, state, dstateDt, sMode);
            }
        }
    } else {
        state[offsets.getAlgOffset(sMode)] = angle;
    }
}

void AcDcConverter::updateLocalCache(const IOdata& /*inputs*/,
                                     const StateData& stateDataValue,
                                     const SolverMode& sMode)
{
    if (!stateDataValue.updateRequired(linkInfo.seqID)) {
        return;
    }

    if (!isEnabled()) {
        return;
    }
    linkInfo = {};
    linkInfo.seqID = stateDataValue.seqID;

    linkInfo.v1 = B1->getVoltage(stateDataValue, sMode);
    linkInfo.v2 = B2->getVoltage(stateDataValue, sMode);

    if (isDynamic(sMode)) {
        auto loc = offsets.getLocations(stateDataValue, sMode, this);
        Idc = loc.algStateLoc[0];
    } else {
        Idc = opFlags[FIXED_TARGET_POWER] ? Pset / linkInfo.v2 : linkInfo.v1 / tap;
    }

    linkFlows.P1 = dirMult * linkInfo.v2 * Idc;
    linkFlows.P2 = -linkFlows.P1;
    const double reactive = k3sq2 * linkInfo.v1 * Idc;

    linkFlows.Q1 = -std::sqrt((reactive * reactive) - (linkFlows.P1 * linkFlows.P1));

    // Q2 is 0 since bus k is a DC bus.
    /*
if (type == Mode::INVERTER)
{
  printf ("inv sid=%d P1=%f P2=%f Q1=%f\n", linkInfo.seqID, linkFlows.P1, linkFlows.P2,
linkFlows.Q1);
}
else
{
  printf ("rect sid=%d P1=%f P2=%f Q1=%f\n", linkInfo.seqID, linkFlows.P1, linkFlows.P2,
linkFlows.Q1);
}
*     */
}

void AcDcConverter::updateLocalCache()
{
    linkInfo = {};

    if (isEnabled()) {
        linkInfo.v1 = B1->getVoltage();
        linkInfo.v2 = B2->getVoltage();
        linkFlows.P1 = dirMult * linkInfo.v2 * Idc;
        linkFlows.P2 = -linkFlows.P1;
        const double sourceP = k3sq2 * linkInfo.v1 * Idc;

        linkFlows.Q1 = -std::sqrt((sourceP * sourceP) - (linkFlows.P1 * linkFlows.P1));
    }
}

int AcDcConverter::fixRealPower(double power,
                                id_type_t /*measureTerminal*/,
                                id_type_t fixedTerminal,
                                units::unit unitType)
{
    if (fixedTerminal != 1) {
        Pset = (power < 0) ? dirMult * power : power;
        Pset = convert(Pset, unitType, puMW, systemBasePower);
        opFlags.set(FIXED_TARGET_POWER);
        Idc = Pset / B2->getVoltage();
        updateLocalCache();
        return 1;
    }
    return 0;
}

int AcDcConverter::fixPower(double /*power*/,
                            double /*qpower*/,
                            id_type_t /*measureTerminal*/,
                            id_type_t /*fixedTerminal*/,
                            units::unit /*unitType*/)
{
    return 0;
}

void AcDcConverter::getStateName(stringVec& stNames,
                                 const SolverMode& sMode,
                                 const std::string& prefix) const
{
    auto offset = offsets.getAlgOffset(sMode);

    const std::string prefix2 = prefix + getName() + ':';

    if (isDynamic(sMode)) {
        if (offset > 0) {
            stNames[offset] = prefix2 + "Idc";
        }
        firingAngleControl->getStateName(stNames, sMode, prefix2);
        if (powerLevelControl->isEnabled()) {
            powerLevelControl->getStateName(stNames, sMode, prefix2);
            if (controlDelay->isEnabled()) {
                controlDelay->getStateName(stNames, sMode, prefix2);
            }
        }
    } else {
        stNames[offset] = prefix2 + "cos(firing_angle)";
    }
}

}  // namespace griddyn::links
