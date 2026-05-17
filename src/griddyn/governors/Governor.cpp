/*
 * Copyright (c) 2014-2026, Lawrence Livermore National Security
 * See the top-level NOTICE for additional details. All rights reserved.
 * SPDX-License-Identifier: BSD-3-Clause
 */

#include "../Generator.h"
#include "../gridBus.h"
#include "GovernorTypes.h"
#include "core/coreExceptions.h"
#include "core/coreObjectTemplates.hpp"
#include "core/objectFactoryTemplates.hpp"
#include "utilities/matrixDataSparse.hpp"
#include <string>
#include <vector>

namespace griddyn {
// Create the component factories for the various governors
static typeFactory<Governor> gfgov1("governor",
                                    std::to_array<std::string_view>({"simple", "fast"}));
namespace governors {
    static childTypeFactory<GovernorIeeeSimple, Governor>
        gfgovsi("governor", std::to_array<std::string_view>({"basic", "ieeesimple"}), "basic");

    static childTypeFactory<GovernorReheat, Governor>
        gfgovrh("governor", std::to_array<std::string_view>({"reheat"}));
    static childTypeFactory<GovernorHydro, Governor>
        gfgov2("governor", std::to_array<std::string_view>({"ieeehydro", "hydro"}));

    static childTypeFactory<GovernorSteamNR, Governor>
        gfgov3("governor", std::to_array<std::string_view>({"ieeesteamnr", "steamnr"}));

    static childTypeFactory<GovernorSteamTCSR, Governor>
        gfgov4("governor", std::to_array<std::string_view>({"ieeesteamtcsr", "steamtcsr"}));

    static childTypeFactory<GovernorTgov1, Governor>
        gfgov5("governor", std::to_array<std::string_view>({"tgov1"}));

}  // namespace governors
using units::convert;
using units::puHz;
using units::puMW;
using units::rad;
using units::second;
using units::unit;

Governor::Governor(const std::string& objName):
    gridSubModel(objName), dbb("deadband"), cb(T1, "filter"), delay(T3, "outFilter")
{
    // default values
    cb.set("bias", -1.0);
    dbb.set("k", -K);

    // since they are members vs dynamic we set the blocks to own themselves
    dbb.addOwningReference();
    cb.addOwningReference();
    delay.addOwningReference();
    m_inputSize = 2;
    m_outputSize = 1;
}

CoreObject* Governor::clone(CoreObject* obj) const
{
    auto* gov = cloneBase<Governor, gridSubModel>(this, obj);
    if (gov == nullptr) {
        return obj;
    }
    gov->K = K;
    gov->T1 = T1;
    gov->T2 = T2;
    gov->T3 = T3;
    gov->Pmax = Pmax;
    gov->Pmin = Pmin;
    gov->Pset = Pset;
    gov->Wref = Wref;
    gov->deadbandHigh = deadbandHigh;
    gov->deadbandLow = deadbandLow;
    gov->machineBasePower = machineBasePower;
    cb.clone(&(gov->cb));
    dbb.clone(&(gov->dbb));

    delay.clone(&(gov->delay));
    return gov;
}

// destructor
Governor::~Governor() = default;

void Governor::dynObjectInitializeA(coreTime time0, std::uint32_t flags)
{
    prevTime = time0;
    if (Wref < 0) {
        Wref = systemBaseFrequency;
    }
    if (!opFlags[ignore_throttle]) {
        addSubObject(&delay);  // delay block comes first to set the first state as the output
    }
    if (!opFlags[ignore_filter]) {
        addSubObject(&cb);
    }
    if (!opFlags[ignore_deadband]) {
        addSubObject(&dbb);
    }
    gridSubModel::dynObjectInitializeA(time0, flags);
}
// initial conditions
static IOdata kNullVec;

void Governor::dynObjectInitializeB(const IOdata& inputs,
                                    const IOdata& desiredOutput,
                                    IOdata& fieldSet)
{
    if (desiredOutput.empty()) {
        fieldSet[0] = inputs[govOmegaInLocation];
        cb.dynInitializeB(fieldSet, kNullVec, fieldSet);
        dbb.dynInitializeB(fieldSet, kNullVec, fieldSet);
        double omP = fieldSet[0];

        fieldSet[0] = Pset + omP;
        delay.dynInitializeB(fieldSet, kNullVec, fieldSet);
        fieldSet[0] = Pset + omP;
    } else {
        double P = desiredOutput[0];
        fieldSet[0] = inputs[govOmegaInLocation];
        cb.dynInitializeB(fieldSet, kNullVec, fieldSet);
        dbb.dynInitializeB(fieldSet, kNullVec, fieldSet);
        double omP = fieldSet[0];

        fieldSet[0] = P;
        delay.dynInitializeB(kNullVec, fieldSet, fieldSet);
        fieldSet.resize(2);
        fieldSet[1] = Pset = P - omP;
    }
}

// residual
void Governor::residual(const IOdata& inputs,
                        const stateData& sD,
                        double resid[],
                        const solverMode& sMode)
{
    cb.blockResidual(inputs[govOmegaInLocation], 0, sD, resid, sMode);
    dbb.blockResidual(cb.getBlockOutput(sD, sMode), 0, sD, resid, sMode);
    delay.blockResidual(
        dbb.getBlockOutput(sD, sMode) + inputs[govpSetInLocation], 0, sD, resid, sMode);
}

void Governor::timestep(coreTime time, const IOdata& inputs, const solverMode& /*sMode*/)
{
    double out = cb.step(time, inputs[govOmegaInLocation]);

    out = dbb.step(time, out);
    delay.step(time, out + inputs[govpSetInLocation]);
}

void Governor::derivative(const IOdata& inputs,
                          const stateData& sD,
                          double deriv[],
                          const solverMode& sMode)
{
    IOdata i{inputs[govOmegaInLocation]};  // deadband doesn't have any derivatives
    cb.derivative(i, sD, deriv, sMode);
    i[0] = dbb.getOutput(i, sD, sMode) + inputs[govpSetInLocation];  // gain from deadband +Pset
    delay.derivative(i, sD, deriv, sMode);
}

void Governor::jacobianElements(const IOdata& inputs,
                                const stateData& sD,
                                matrixData<double>& md,
                                const IOlocs& inputLocs,
                                const solverMode& sMode)
{
    cb.blockJacobianElements(
        inputs[govOmegaInLocation], 0, sD, md, inputLocs[govOmegaInLocation], sMode);

    matrixDataSparse<double> kp;
    index_t wloc = cb.getOutputLoc(sMode);
    double out = cb.getOutput(kNullVec, sD, sMode);
    dbb.blockJacobianElements(out, 0, sD, md, wloc, sMode);

    out = dbb.getOutput(kNullVec, sD, sMode);
    wloc = dbb.getOutputLoc(sMode);
    delay.blockJacobianElements(out + inputs[govpSetInLocation], 0, sD, kp, 0, sMode);

    if (inputLocs[govpSetInLocation] != kNullLocation) {
        for (index_t pp = 0; pp < kp.size(); ++pp) {
            auto el = kp.element(pp);
            if (el.col == 0) {
                md.assign(el.row, wloc, el.data);
            } else {
                md.assign(el.row, el.col, el.data);
            }
        }
    } else {
        for (index_t pp = 0; pp < kp.size(); ++pp) {
            auto el = kp.element(pp);
            if (el.col == 0) {
                md.assign(el.row, wloc, el.data);
                md.assign(el.row, inputLocs[govpSetInLocation], el.data);
            } else {
                md.assign(el.row, el.col, el.data);
            }
        }
    }

    /*
    copyReplicate(matrixDataSparse *a2, index_t origCol, std::vector<index_t>
    newIndices, std::vector<double> mult)
    auto res = a2->dVec.begin();
          auto term = a2->dVec.end();

          while (res != term)
          {
                  if (std::get<adCol>(*res) == origCol)
                  {
                          for (index_t nn = 0; nn<newIndices.size(); ++nn)
                          {
                                  //dVec.push_back(cLoc(std::get<adRow>(*res),
    newIndices[nn], std::get<adVal>(*res)*mult[nn]));
                                  dVec.emplace_back(std::get<adRow>(*res),
    newIndices[nn], std::get<adVal>(*res)*mult[nn]);
                          }
                  }
                  else
                  {
                          dVec.push_back(*res);
                  }
                  ++res;
          }
          */
}

void Governor::rootTest(const IOdata& /*inputs*/,
                        const stateData& sD,
                        double roots[],
                        const solverMode& sMode)
{
    IOdata i{cb.getOutput(kNullVec, sD, sMode)};
    if (dbb.checkFlag(has_roots)) {
        dbb.rootTest(i, sD, roots, sMode);
    }
    // cb should not have roots
    if (delay.checkFlag(has_roots)) {
        delay.rootTest(i, sD, roots, sMode);
    }
}

index_t Governor::findIndex(std::string_view field, const solverMode& sMode) const
{
    index_t ret = kInvalidLocation;
    if (field == "pm") {
        ret = delay.getOutputLoc(sMode, 0);
    } else if (field == "dbo") {
        ret = dbb.getOutputLoc(sMode, 0);
    } else if (field == "w") {
        ret = cb.getOutputLoc(sMode, 0);
    }
    return ret;
}

void Governor::setFlag(std::string_view flag, bool val)
{
    try {
        gridSubModel::setFlag(flag, val);
    }
    catch (const unrecognizedParameter&) {
        dbb.setFlag(flag, val);
    }
}
// set parameters
void Governor::set(std::string_view param, std::string_view val)
{
    try {
        gridSubModel::set(param, val);
    }
    catch (const unrecognizedParameter&) {
        dbb.set(param, val);
    }
}

void Governor::set(std::string_view param, double val, unit unitType)
{
    if ((param == "k") || (param == "droop")) {
        K = val;
        dbb.set(param, -K);
    } else if (param == "r") {
        K = 1.0 / val;
        dbb.set("k", -K);
    } else if (param == "t1") {
        T1 = val;
        cb.set("t1", val);
    } else if (param == "t2") {
        T2 = val;
        cb.set("t2", val);
    } else if (param == "t3") {
        T3 = val;
        delay.set("t1", val);
    } else if ((param == "omegaref") || (param == "wref")) {
        Wref = convert(val, unitType, rad / second);
        // TODO(phlpt): Decide how to handle changes to the reference frequency after init.
    } else if (param == "pmax") {
        Pmax = convert(val, unitType, puMW, systemBasePower);
        delay.set("max", Pmax);
    } else if (param == "pmin") {
        Pmin = convert(val, unitType, puMW, systemBasePower);
        delay.set("min", Pmin);
    } else if (param == "deadband") {
        deadbandHigh = convert(val, unitType, puHz, systemBaseFrequency);
        if (deadbandHigh > 1.0) {
            deadbandHigh = deadbandHigh - 1.0;
        }
        deadbandLow = -deadbandHigh;
        dbb.set("deadband", deadbandHigh);
    } else if (param == "deadbandhigh") {
        deadbandHigh = convert(val, unitType, puHz, systemBaseFrequency);
        if (deadbandHigh > 1.0) {
            deadbandHigh = deadbandHigh - 1.0;
        }
        dbb.set("deadbandhigh", deadbandHigh);
    } else if (param == "deadbandlow") {
        deadbandLow = -convert(val, unitType, puHz, systemBaseFrequency);
        if (deadbandLow > 0.95) {
            deadbandLow = deadbandLow - 1.0;
        }
        if (deadbandLow > 0) {
            deadbandLow = -deadbandLow;
        }
        dbb.set("deadbandhigh", deadbandLow);
    } else {
        gridSubModel::set(param, val, unitType);
    }
}

double Governor::get(std::string_view param, units::unit unitType) const
{
    double out = kNullVal;
    if (param == "k") {
        out = K;
    } else if (param == "r") {
        out = 1.0 / K;
    } else if (param == "t1") {
        out = T1;
    } else if (param == "t2") {
        out = T2;
    } else if (param == "t3") {
        out = T3;
    } else if ((param == "omegaref") || (param == "wref")) {
        out = convert(Wref, rad / second, unitType);
    } else if (param == "pmax") {
        out = convert(Pmax, puMW, unitType, systemBasePower);
    } else if (param == "pmin") {
        out = convert(Pmin, puMW, unitType, systemBasePower);
    } else if (param == "deadband") {
        out = convert(deadbandHigh, puHz, unitType, systemBaseFrequency);
    } else if (param == "deadbandhigh") {
        out = convert(deadbandHigh, puHz, unitType, systemBaseFrequency);
    } else if (param == "deadbandlow") {
        out = convert(deadbandHigh, puHz, unitType, systemBaseFrequency);
    } else {
        out = gridSubModel::get(param, unitType);
    }
    return out;
}

static const std::vector<stringVec> inputNamesStr{
    {"omega", "frequency", "w", "f"},
    {"pset", "setpoint", "power"},
};

const std::vector<stringVec>& Governor::inputNames() const
{
    return inputNamesStr;
}

static const std::vector<stringVec> outputNamesStr{
    {"pmech", "power", "output", "p"},
};

const std::vector<stringVec>& Governor::outputNames() const
{
    return outputNamesStr;
}

}  // namespace griddyn
