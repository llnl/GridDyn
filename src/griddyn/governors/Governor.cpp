/*
 * Copyright (c) 2014-2026, Lawrence Livermore National Security
 * See the top-level NOTICE for additional details. All rights reserved.
 * SPDX-License-Identifier: BSD-3-Clause
 */

#include "../Generator.h"
#include "../GridBus.h"
#include "GovernorTypes.h"
#include "core/CoreExceptions.h"
#include "core/CoreObjectTemplates.hpp"
#include "core/ObjectFactoryTemplates.hpp"
#include "utilities/MatrixDataSparse.hpp"
#include <string>
#include <vector>

namespace griddyn {
// Create the component factories for the various governors
static TypeFactory<Governor> gFgov1("governor",
                                    std::to_array<std::string_view>({"simple", "fast"}));
namespace governors {
    static ChildTypeFactory<GovernorIeeeSimple, Governor>
        gFgovsi("governor", std::to_array<std::string_view>({"basic", "ieeesimple"}), "basic");

    static ChildTypeFactory<GovernorReheat, Governor>
        gFgovrh("governor", std::to_array<std::string_view>({"reheat"}));
    static ChildTypeFactory<GovernorHydro, Governor>
        gFgov2("governor", std::to_array<std::string_view>({"ieeehydro", "hydro"}));
    static ChildTypeFactory<GovernorHygov, Governor>
        gFgovHygov("governor", std::to_array<std::string_view>({"hygov", "pssehygov"}));

    static ChildTypeFactory<GovernorSteamNR, Governor>
        gFgov3("governor", std::to_array<std::string_view>({"ieeesteamnr", "steamnr"}));

    static ChildTypeFactory<GovernorSteamTCSR, Governor>
        gFgov4("governor", std::to_array<std::string_view>({"ieeesteamtcsr", "steamtcsr"}));

    static ChildTypeFactory<GovernorTgov1, Governor>
        gFgov5("governor", std::to_array<std::string_view>({"tgov1"}));

    static ChildTypeFactory<GovernorIeeeG1, Governor>
        gFgov6("governor", std::to_array<std::string_view>({"ieeeg1"}));

}  // namespace governors
using units::convert;
using units::puHz;
using units::puMW;
using units::rad;
using units::second;
using units::unit;

Governor::Governor(const std::string& objName):
    GridSubModel(objName), dbb("deadband"), cb(T1, "filter"), delay(T3, "outFilter")
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
    auto* gov = cloneBase<Governor, GridSubModel>(this, obj);
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

// Embedded blocks are stack members, so detach them from the generic subobject list
// before base-class teardown walks that list.
Governor::~Governor()
{
    removeSubObject(&delay);
    removeSubObject(&cb);
    removeSubObject(&dbb);
}

void Governor::dynObjectInitializeA(CoreTime time0, std::uint32_t flags)
{
    prevTime = time0;
    if (Wref < 0) {
        Wref = systemBaseFrequency;
    }
    if (!opFlags[IGNORE_THROTTLE]) {
        addSubObject(&delay);  // delay block comes first to set the first state as the output
    }
    if (!opFlags[IGNORE_FILTER]) {
        addSubObject(&cb);
    }
    if (!opFlags[IGNORE_DEADBAND]) {
        addSubObject(&dbb);
    }
    GridSubModel::dynObjectInitializeA(time0, flags);
}
// initial conditions
static IOdata gKNullVec;

void Governor::dynObjectInitializeB(const IOdata& inputs,
                                    const IOdata& desiredOutput,
                                    IOdata& fieldSet)
{
    if (desiredOutput.empty()) {
        fieldSet[0] = inputs[govOmegaInLocation];
        cb.dynInitializeB(fieldSet, gKNullVec, fieldSet);
        dbb.dynInitializeB(fieldSet, gKNullVec, fieldSet);
        const double omegaPower = fieldSet[0];

        fieldSet[0] = Pset + omegaPower;
        delay.dynInitializeB(fieldSet, gKNullVec, fieldSet);
        fieldSet[0] = Pset + omegaPower;
    } else {
        const double power = desiredOutput[0];
        fieldSet[0] = inputs[govOmegaInLocation];
        cb.dynInitializeB(fieldSet, gKNullVec, fieldSet);
        dbb.dynInitializeB(fieldSet, gKNullVec, fieldSet);
        const double omegaPower = fieldSet[0];

        fieldSet[0] = power;
        delay.dynInitializeB(gKNullVec, fieldSet, fieldSet);
        fieldSet.resize(2);
        fieldSet[1] = Pset = power - omegaPower;
    }
}

// residual
void Governor::residual(const IOdata& inputs,
                        const StateData& stateData,
                        double resid[],
                        const SolverMode& sMode)
{
    cb.blockResidual(inputs[govOmegaInLocation], 0, stateData, resid, sMode);
    dbb.blockResidual(cb.getBlockOutput(stateData, sMode), 0, stateData, resid, sMode);
    delay.blockResidual(dbb.getBlockOutput(stateData, sMode) + inputs[govpSetInLocation],
                        0,
                        stateData,
                        resid,
                        sMode);
}

void Governor::timestep(CoreTime time, const IOdata& inputs, const SolverMode& /*sMode*/)
{
    double out = cb.step(time, inputs[govOmegaInLocation]);

    out = dbb.step(time, out);
    delay.step(time, out + inputs[govpSetInLocation]);
}

void Governor::derivative(const IOdata& inputs,
                          const StateData& stateData,
                          double deriv[],
                          const SolverMode& sMode)
{
    IOdata blockInput{inputs[govOmegaInLocation]};  // deadband doesn't have any derivatives
    cb.derivative(blockInput, stateData, deriv, sMode);
    blockInput[0] = dbb.getOutput(blockInput, stateData, sMode) +
        inputs[govpSetInLocation];  // gain from deadband +Pset
    delay.derivative(blockInput, stateData, deriv, sMode);
}

void Governor::jacobianElements(const IOdata& inputs,
                                const StateData& stateData,
                                MatrixData<double>& matrixData,
                                const IOlocs& inputLocs,
                                const SolverMode& sMode)
{
    cb.blockJacobianElements(
        inputs[govOmegaInLocation], 0, stateData, matrixData, inputLocs[govOmegaInLocation], sMode);

    MatrixDataSparse<double> delayJacobian;
    index_t frequencyLoc = cb.getOutputLoc(sMode);
    double output = cb.getOutput(gKNullVec, stateData, sMode);
    dbb.blockJacobianElements(output, 0, stateData, matrixData, frequencyLoc, sMode);

    output = dbb.getOutput(gKNullVec, stateData, sMode);
    frequencyLoc = dbb.getOutputLoc(sMode);
    delay.blockJacobianElements(
        output + inputs[govpSetInLocation], 0, stateData, delayJacobian, 0, sMode);

    if (inputLocs[govpSetInLocation] != kNullLocation) {
        for (index_t pp = 0; pp < delayJacobian.size(); ++pp) {
            const auto element = delayJacobian.element(pp);
            if (element.col == 0) {
                matrixData.assign(element.row, frequencyLoc, element.data);
            } else {
                matrixData.assign(element.row, element.col, element.data);
            }
        }
    } else {
        for (index_t pp = 0; pp < delayJacobian.size(); ++pp) {
            const auto element = delayJacobian.element(pp);
            if (element.col == 0) {
                matrixData.assign(element.row, frequencyLoc, element.data);
                matrixData.assign(element.row, inputLocs[govpSetInLocation], element.data);
            } else {
                matrixData.assign(element.row, element.col, element.data);
            }
        }
    }

    /*
    copyReplicate(MatrixDataSparse *a2, index_t origCol, std::vector<index_t>
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
                        const StateData& stateData,
                        double roots[],
                        const SolverMode& sMode)
{
    const IOdata blockInput{cb.getOutput(gKNullVec, stateData, sMode)};
    if (dbb.checkFlag(HAS_ROOTS)) {
        dbb.rootTest(blockInput, stateData, roots, sMode);
    }
    // cb should not have roots
    if (delay.checkFlag(HAS_ROOTS)) {
        delay.rootTest(blockInput, stateData, roots, sMode);
    }
}

index_t Governor::findIndex(std::string_view field, const SolverMode& sMode) const
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
        GridSubModel::setFlag(flag, val);
    }
    catch (const UnrecognizedParameter&) {
        dbb.setFlag(flag, val);
    }
}
// set parameters
void Governor::set(std::string_view param, std::string_view val)
{
    try {
        GridSubModel::set(param, val);
    }
    catch (const UnrecognizedParameter&) {
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
        GridSubModel::set(param, val, unitType);
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
    } else if ((param == "deadband") || (param == "deadbandhigh")) {
        out = convert(deadbandHigh, puHz, unitType, systemBaseFrequency);
    } else if (param == "deadbandlow") {
        out = convert(deadbandLow, puHz, unitType, systemBaseFrequency);
    } else {
        out = GridSubModel::get(param, unitType);
    }
    return out;
}

static const std::vector<stringVec> INPUT_NAMES_STR{
    {"omega", "frequency", "w", "f"},
    {"pset", "setpoint", "power"},
};

const std::vector<stringVec>& Governor::inputNames() const
{
    return INPUT_NAMES_STR;
}

static const std::vector<stringVec> OUTPUT_NAMES_STR{
    {"pmech", "power", "output", "p"},
};

const std::vector<stringVec>& Governor::outputNames() const
{
    return OUTPUT_NAMES_STR;
}

}  // namespace griddyn
