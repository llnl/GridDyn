/*
 * Copyright (c) 2014-2026, Lawrence Livermore National Security
 * See the top-level NOTICE for additional details. All rights reserved.
 * SPDX-License-Identifier: BSD-3-Clause
 */

#include "ExciterIEEEtype2.h"

#include "../Generator.h"
#include "../GridBus.h"
#include "core/CoreExceptions.h"
#include "core/CoreObjectTemplates.hpp"
#include "core/ModelParameter.hpp"
#include "utilities/MatrixData.hpp"
#include <algorithm>
#include <cmath>
#include <string>

namespace griddyn::exciters {
namespace {
    constexpr double initialLimitContactTolerance = 1e-7;
}  // namespace

const ModelDescription& ExciterIEEEtype2::getModelDescription() const
{
    static const auto description = ModelDescription::define<ExciterIEEEtype2>(
        "ExciterIEEEtype2",
        &ExciterIEEEtype1::getModelDescription(),
        {parameter({"tf2"}, &ExciterIEEEtype2::Tf2, units::second, "Tf2 time constant"),
         state("ef", units::one, "Field voltage"),
         state("vr", units::one, "Regulator state"),
         state("x1", units::one, "x1 solver state"),
         state("x2", units::one, "x2 solver state"),
         command({"tf1"}, units::second, "Feedback time constant alias")});
    return description;
}

void ExciterIEEEtype2::getParameterStrings(stringVec& pstr, ParamStringType pstype) const
{
    const auto& description = ExciterIEEEtype2::getModelDescription();
    getParamString<ExciterIEEEtype2, ExciterIEEEtype1>(this,
                                                       pstr,
                                                       description.numericNames(),
                                                       description.stringNames(),
                                                       description.flagNames(),
                                                       pstype);
}

ExciterIEEEtype2::ExciterIEEEtype2(const std::string& objName): ExciterIEEEtype1(objName) {}

// cloning function
CoreObject* ExciterIEEEtype2::clone(CoreObject* obj) const
{
    auto* gdE = cloneBase<ExciterIEEEtype2, ExciterIEEEtype1>(this, obj);
    if (gdE == nullptr) {
        return obj;
    }

    gdE->Tf2 = Tf2;
    return gdE;
}

void ExciterIEEEtype2::dynObjectInitializeA(CoreTime /*time*/, std::uint32_t flags)
{
    setInitialLimitPolicy(flags);
    offsets.local().local.diffSize = 4;
    offsets.local().local.jacSize = 16;
    checkForLimits();
}

// initial conditions
void ExciterIEEEtype2::dynObjectInitializeB(const IOdata& inputs,
                                            const IOdata& desiredOutput,
                                            IOdata& fieldSet)
{
    const double originalUpperLimit = Vrmax;
    Exciter::dynObjectInitializeB(inputs,
                                  desiredOutput,
                                  fieldSet);  // this will dynInitializeB the field state if need be
    double* stateValues = m_state.data();
    stateValues[1] = (Ke + (Aex * exp(Bex * stateValues[0]))) * stateValues[0];  // Vr
    if ((stateValues[1] < Vrmin - 1e-7) ||
        !adjustInitialUpperLimit(stateValues[1], Vrmax, "IEEE Type 2 initial regulator output")) {
        throw InvalidParameterValue("IEEE Type 2 initial regulator output outside limits");
    }
    stateValues[2] = 0;  // X1
    stateValues[3] = stateValues[1];  // X2

    vBias = inputs[VOLTAGE_IN_LOCATION] + (stateValues[1] / Ka) - Vref;
    const double correction = std::max(0.0, Vrmax - originalUpperLimit);
    const double guard = 1e-3 + std::abs(Ka) * 1e-5;
    if ((Vrmax != 0.0) && (stateValues[1] >= Vrmax - initialLimitContactTolerance)) {
        vBias += std::max(0.2 * correction, guard) / Ka;
        opFlags.set(TRIGGER_HIGH);
        opFlags.set(OUTSIDE_VOLTAGE_LIMITS);
    } else if (stateValues[1] <= Vrmin + initialLimitContactTolerance) {
        vBias -= guard / Ka;
        opFlags.reset(TRIGGER_HIGH);
        opFlags.set(OUTSIDE_VOLTAGE_LIMITS);
    }
    fieldSet[exciterVsetInLocation] = Vref;
}

// residual
void ExciterIEEEtype2::residual(const IOdata& inputs,
                                const StateData& stateData,
                                double resid[],
                                const SolverMode& sMode)
{
    if (isAlgebraicOnly(sMode)) {
        return;
    }
    const auto offset = offsets.getDiffOffset(sMode);
    const double* exciterState = stateData.state + offset;
    const double* stateDerivative = stateData.dstate_dt + offset;
    double* residualValues = resid + offset;
    residualValues[0] =
        (((-(Ke + (Aex * exp(Bex * exciterState[0]))) * exciterState[0]) + exciterState[1]) / Te) -
        stateDerivative[0];
    if (opFlags[OUTSIDE_VOLTAGE_LIMITS]) {
        residualValues[1] = -stateDerivative[1];
    } else {
        residualValues[1] = ((-exciterState[1] + (Ka * Kf * exciterState[2]) +
                              (Ka * (Vref + vBias - inputs[VOLTAGE_IN_LOCATION]))) /
                             Ta) -
            stateDerivative[1];
    }
    residualValues[2] =
        ((-exciterState[2] + (exciterState[1] / Tf2) - (exciterState[3] / Tf2)) / Tf) -
        stateDerivative[2];
    residualValues[3] = ((-exciterState[3] + exciterState[1]) / Tf2) - stateDerivative[3];
}

void ExciterIEEEtype2::derivative(const IOdata& inputs,
                                  const StateData& stateData,
                                  double deriv[],
                                  const SolverMode& sMode)
{
    const auto locations = offsets.getLocations(stateData, deriv, sMode, this);
    const double* exciterState = locations.diffStateLoc;
    double* derivatives = locations.destDiffLoc;
    derivatives[0] =
        ((-(Ke + (Aex * exp(Bex * exciterState[0]))) * exciterState[0]) + exciterState[1]) / Te;
    if (opFlags[OUTSIDE_VOLTAGE_LIMITS]) {
        derivatives[1] = 0;
    } else {
        derivatives[1] = (-exciterState[1] + (Ka * Kf * exciterState[2]) +
                          (Ka * (Vref + vBias - inputs[VOLTAGE_IN_LOCATION]))) /
            Ta;
    }
    derivatives[2] = (-exciterState[2] + (exciterState[1] / Tf2) - (exciterState[3] / Tf2)) / Tf;
    derivatives[3] = (-exciterState[3] + exciterState[1]) / Tf2;
}

// compute the bus element contributions

// Jacobian
void ExciterIEEEtype2::jacobianElements(const IOdata& /*inputs*/,
                                        const StateData& stateData,
                                        MatrixData<double>& matrixData,
                                        const IOlocs& inputLocs,
                                        const SolverMode& sMode)
{
    if (isAlgebraicOnly(sMode)) {
        return;
    }
    const auto offset = offsets.getDiffOffset(sMode);
    const int refIndex = offset;
    const auto voltageLoc = inputLocs[0];
    // use the md.assign Macro defined in basicDefs
    // md.assign(arrayIndex, RowIndex, ColIndex, value)

    // Ef
    const double fieldVoltageSlope =
        (-(Ke +
           (Aex * exp(Bex * stateData.state[offset]) * (1.0 + (Bex * stateData.state[offset])))) /
         Te) -
        stateData.cj;
    matrixData.assign(refIndex, refIndex, fieldVoltageSlope);
    matrixData.assign(refIndex, refIndex + 1, 1.0 / Te);

    if (opFlags[OUTSIDE_VOLTAGE_LIMITS]) {
        matrixData.assign(refIndex + 1, refIndex + 1, -stateData.cj);
    } else {
        // Vr
        if (voltageLoc != kNullLocation) {
            matrixData.assign(refIndex + 1, voltageLoc, -Ka / Ta);
        }
        matrixData.assign(refIndex + 1, refIndex + 1, (-1.0 / Ta) - stateData.cj);
        matrixData.assign(refIndex + 1, refIndex + 2, (Ka * Kf) / Ta);
    }

    // X1
    matrixData.assign(refIndex + 2, refIndex + 1, 1.0 / (Tf * Tf2));
    matrixData.assign(refIndex + 2, refIndex + 2, (-1.0 / Tf) - stateData.cj);
    matrixData.assign(refIndex + 2, refIndex + 3, -1.0 / (Tf * Tf2));

    // X2
    matrixData.assign(refIndex + 3, refIndex + 1, 1.0 / Tf2);
    matrixData.assign(refIndex + 3, refIndex + 3, (-1.0 / Tf2) - stateData.cj);

    // printf("%f\n",sD.cj);
}

const stringVec K_IEEE_TYPE2_FIELDS{"ef", "vr", "x1", "x2"};

stringVec ExciterIEEEtype2::localStateNames() const
{
    return K_IEEE_TYPE2_FIELDS;
}
void ExciterIEEEtype2::rootTest(const IOdata& inputs,
                                const StateData& stateData,
                                double roots[],
                                const SolverMode& sMode)
{
    const auto offset = offsets.getAlgOffset(sMode);
    const int rootOffset = offsets.getRootOffset(sMode);
    const double* exciterState = stateData.state + offset;

    if (opFlags[OUTSIDE_VOLTAGE_LIMITS]) {
        roots[rootOffset] = (Ka * Kf * exciterState[2]) +
            (Ka * (Vref + vBias - inputs[VOLTAGE_IN_LOCATION])) - exciterState[1];
    } else {
        roots[rootOffset] = std::min(Vrmax - exciterState[1], exciterState[1] - Vrmin) + 0.0001;
        if (exciterState[1] > Vrmax) {
            opFlags.set(TRIGGER_HIGH);
        }
    }
}

ChangeCode ExciterIEEEtype2::rootCheck(const IOdata& inputs,
                                       const StateData& /*sD*/,
                                       const SolverMode& /*sMode*/,
                                       CheckLevel /*level*/)
{
    double* exciterState = m_state.data();
    ChangeCode ret = ChangeCode::NO_CHANGE;
    if (opFlags[OUTSIDE_VOLTAGE_LIMITS]) {
        const double test = (Ka * Kf * exciterState[2]) +
            (Ka * (Vref + vBias - inputs[VOLTAGE_IN_LOCATION])) - exciterState[1];
        if (opFlags[TRIGGER_HIGH]) {
            if (test < 0.0) {
                ret = ChangeCode::JACOBIAN_CHANGE;
                opFlags.reset(OUTSIDE_VOLTAGE_LIMITS);
                opFlags.reset(TRIGGER_HIGH);
                alert(this, JAC_COUNT_INCREASE);
            }
        } else {
            if (test > 0.0) {
                ret = ChangeCode::JACOBIAN_CHANGE;
                opFlags.reset(OUTSIDE_VOLTAGE_LIMITS);
                alert(this, JAC_COUNT_INCREASE);
            }
        }
    } else {
        if (exciterState[1] > Vrmax + 0.0001) {
            opFlags.set(TRIGGER_HIGH);
            opFlags.set(OUTSIDE_VOLTAGE_LIMITS);
            exciterState[1] = Vrmax;
            ret = ChangeCode::JACOBIAN_CHANGE;
            alert(this, JAC_COUNT_DECREASE);
        } else if (exciterState[1] < Vrmin - 0.0001) {
            opFlags.reset(TRIGGER_HIGH);
            opFlags.set(OUTSIDE_VOLTAGE_LIMITS);
            exciterState[1] = Vrmin;
            ret = ChangeCode::JACOBIAN_CHANGE;
            alert(this, JAC_COUNT_DECREASE);
        }
    }

    return ret;
}

void ExciterIEEEtype2::set(std::string_view param, std::string_view val)
{
    if (!isSetHandled(ExciterIEEEtype2::getModelDescription().trySetLocal(*this, param, val))) {
        ExciterIEEEtype1::set(param, val);
    }
}

// set parameters
void ExciterIEEEtype2::set(std::string_view param, double val, units::unit unitType)
{
    if (isSetHandled(
            ExciterIEEEtype2::getModelDescription().trySetLocal(*this, param, val, unitType))) {
        return;
    }
    if (param == "tf1") {
        ExciterIEEEtype1::set("tf", val, unitType);
        return;
    }
    ExciterIEEEtype1::set(param, val, unitType);
}

double ExciterIEEEtype2::get(std::string_view param, units::unit unitType) const
{
    if (param == "tf1") {
        return ExciterIEEEtype1::get("tf", unitType);
    }
    if (const auto value =
            ExciterIEEEtype2::getModelDescription().tryGetLocal(*this, param, unitType)) {
        return *value;
    }
    return ExciterIEEEtype1::get(param, unitType);
}

}  // namespace griddyn::exciters
