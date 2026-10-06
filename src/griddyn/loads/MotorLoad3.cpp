/*
 * Copyright (c) 2014-2026, Lawrence Livermore National Security
 * See the top-level NOTICE for additional details. All rights reserved.
 * SPDX-License-Identifier: BSD-3-Clause
 */

#include "MotorLoad3.h"

#include "../GridBus.h"
#include "core/CoreObjectTemplates.hpp"
#include "gmlc/utilities/vectorOps.hpp"
#include "utilities/MatrixData.hpp"
#include <iostream>
#include <string>
#include <vector>

namespace griddyn::loads {
// setup the load object factories

MotorLoad3::MotorLoad3(const std::string& objName): MotorLoad(objName) {}
CoreObject* MotorLoad3::clone(CoreObject* obj) const
{
    auto* load = cloneBase<MotorLoad3, MotorLoad>(this, obj);
    if (load == nullptr) {
        return obj;
    }

    return load;
}

void MotorLoad3::pFlowObjectInitializeA(CoreTime time0, std::uint32_t flags)
{
    // setup the parameters
    x0 = x + xm;
    xp = x + x1 * xm / (x1 + xm);
    T0p = (x1 + xm) / (systemBaseFrequency * r1);
    scale = mBase / systemBasePower;
    m_state.resize(5, 0);
    if (opFlags[INIT_TRANSIENT]) {
        m_state[2] = init_slip;
    } else if (Pmot > -kHalfBigNum) {
        m_state[2] = computeSlip(Pmot / scale);
    } else {
        m_state[2] = 1.0;
        opFlags.set(INIT_TRANSIENT);
    }

    GridLoad::pFlowObjectInitializeA(time0, flags);  // NOLINT
    converge();

    loadStateSizes(cLocalSolverMode);
    setOffset(0, cLocalSolverMode);
}

void MotorLoad3::converge()
{
    double const voltage = bus->getVoltage();
    double const theta = bus->getAngle();
    double slip = m_state[2];
    double const reactivePowerTest = qPower(voltage, m_state[2]);
    double currentImaginary;
    double internalVoltageImaginary;
    double currentReal;
    double internalVoltageReal;
    double const voltageReal = -voltage * Vcontrol * sin(theta);
    double const voltageImaginary = voltage * Vcontrol * cos(theta);
    gmlc::utilities::solve2x2(voltageReal, voltageImaginary, voltageImaginary, -voltageReal, Pmot / scale, reactivePowerTest, currentReal, currentImaginary);
    double err = 10;
    int ccnt = 0;
    double perr = 10;
    double dslip = 0;
    while (err > 1e-6) {
        internalVoltageReal = voltageReal - r * currentReal + xp * currentImaginary;
        internalVoltageImaginary = voltageImaginary - r * currentImaginary - xp * currentReal;
        double const slipp = (internalVoltageReal + (x0 - xp) * currentImaginary) / T0p / systemBaseFrequency / internalVoltageImaginary;
        dslip = slipp - slip;
        if (Pmot > 0) {
            if (slipp < 0) {
                slip = slip / 2.0;
            } else {
                slip = slipp;
            }
        }

        err = std::abs(dslip);
        if (err > perr) {
            break;
        }
        // just archiving the states in case we need to break;
        m_state[0] = currentReal;
        m_state[1] = currentImaginary;
        m_state[2] = slip;
        m_state[3] = internalVoltageReal;
        m_state[4] = internalVoltageImaginary;
        if (++ccnt > 50) {
            break;
        }

        perr = err;
        currentReal = (-systemBaseFrequency * slip * internalVoltageReal * T0p - internalVoltageImaginary) / (-(x0 - xp));
        currentImaginary = (mechPower(slip) - internalVoltageReal * currentReal) / internalVoltageImaginary;
    }
}

void MotorLoad3::dynObjectInitializeA(CoreTime /*time0*/, std::uint32_t /*flags*/) {}
void MotorLoad3::dynObjectInitializeB(const IOdata& inputs,
                                      const IOdata& /*desiredOutput*/,
                                      IOdata& /*fieldSet*/)
{
    if (opFlags[INIT_TRANSIENT]) {
        derivative(inputs, emptyStateData, m_dstate_dt.data(), cLocalSolverMode);
    }
}

StateSizes MotorLoad3::localStateSizes(const SolverMode& sMode) const
{
    StateSizes stateSizes;
    if (isDynamic(sMode)) {
        stateSizes.algSize = 2;
        if (!isAlgebraicOnly(sMode)) {
            stateSizes.diffSize = 3;
        }
    } else {
        stateSizes.algSize = 5;
    }
    return stateSizes;
}

count_t MotorLoad3::localJacobianCount(const SolverMode& sMode) const
{
    count_t localJacSize = 0;
    if (isDynamic(sMode)) {
        localJacSize = 8;
        if (!isAlgebraicOnly(sMode)) {
            localJacSize += 15;
        }
    } else {
        if (opFlags[INIT_TRANSIENT]) {
            localJacSize = 19;
        } else {
            localJacSize = 23;
        }
    }
    return localJacSize;
}

// set properties
void MotorLoad3::set(std::string_view param, std::string_view val)
{
    if (param.empty()) {
    } else {
        MotorLoad::set(param, val);
    }
}

void MotorLoad3::set(std::string_view param, double val, units::unit unitType)
{
    if (param == "rs") {
        r = val;
    } else {
        MotorLoad::set(param, val, unitType);
    }
}

void MotorLoad3::setState(CoreTime time,
                          const double state[],
                          const double dstateDt[],
                          const SolverMode& sMode)
{
    // NOLINTNEXTLINE
    GridComponent::setState(time, state, dstateDt, sMode);
}

void MotorLoad3::guessState(CoreTime time,
                            double state[],
                            double dstateDt[],
                            const SolverMode& sMode)
{
    // NOLINTNEXTLINE
    GridComponent::guessState(time, state, dstateDt, sMode);
}

// residual
void MotorLoad3::residual(const IOdata& inputs,
                          const StateData& stateData,
                          double resid[],
                          const SolverMode& sMode)
{
    if (isDynamic(sMode)) {
        auto loc = offsets.getLocations(stateData, resid, sMode, this);

        double const voltage = inputs[VOLTAGE_IN_LOCATION];
        double const theta = inputs[ANGLE_IN_LOCATION];
        const double* algebraicState = loc.algStateLoc;
        const double* gmd = loc.diffStateLoc;
        const double* gmp = loc.dstateLoc;

        double* rva = loc.destLoc;
        double* rvd = loc.destDiffLoc;

        double const voltageReal = -voltage * Vcontrol * sin(theta);
        double const voltageImaginary = voltage * Vcontrol * cos(theta);

        if (hasAlgebraic(sMode)) {
            // currentReal
            rva[0] = voltageImaginary - gmd[2] - r * algebraicState[1] - xp * algebraicState[0];
            // currentImaginary
            rva[1] = voltageReal - gmd[1] - r * algebraicState[0] + xp * algebraicState[1];
        }

        if (isAlgebraicOnly(sMode)) {
            return;
        }
        derivative(inputs, stateData, resid, sMode);
        // Get the exciter field

        // delta
        rvd[0] -= gmp[0];
        rvd[1] -= gmp[1];
        rvd[2] -= gmp[2];
        // printf("t=%f:motor state a1=%f a2=%f, d1=%f, d2=%f,d3=%f\n", stateData.time, algebraicState[0], algebraicState[1],
        // gmd[0], gmd[1], gmd[2]); printf("t=%f:motor resid a1=%e a2=%e, d1=%e,
        // d2=%e,d3=%e\n",stateData.time,rva[0],rva[1],rvd[0],rvd[1],rvd[2]); printf("t=%f, voltage=%f,
        // currentReal=%f, currentImaginary=%f, r1=%e, r2=%e\n",stateData.time,voltage,algebraicState[0],algebraicState[1],rva[0],rva[1]);
    } else {
        auto offset = offsets.getAlgOffset(sMode);
        const double voltage = inputs[VOLTAGE_IN_LOCATION];
        double const theta = inputs[ANGLE_IN_LOCATION];

        const double* algebraicState = stateData.state + offset;
        double* residualVector = resid + offset;

        double const voltageReal = -voltage * Vcontrol * sin(theta);
        double const voltageImaginary = voltage * Vcontrol * cos(theta);

        // currentReal
        residualVector[0] = voltageImaginary - algebraicState[4] - r * algebraicState[1] - xp * algebraicState[0];
        // currentImaginary
        residualVector[1] = voltageReal - algebraicState[3] - r * algebraicState[0] + xp * algebraicState[1];

        double const slip = algebraicState[2];
        // printf("angle=%f, slip=%f\n",theta,slip);
        // slip
        if (opFlags[INIT_TRANSIENT]) {
            residualVector[2] = slip - m_state[2];
        } else {
            double const electricalTorque = (algebraicState[3] * algebraicState[0]) + (algebraicState[4] * algebraicState[1]);
            residualVector[2] = (mechPower(slip) - electricalTorque) / (2 * H);
        }
        // Erp and Emp
        residualVector[3] = systemBaseFrequency * slip * algebraicState[4] - (algebraicState[3] + (x0 - xp) * algebraicState[1]) / T0p;
        residualVector[4] = -systemBaseFrequency * slip * algebraicState[3] - (algebraicState[4] - (x0 - xp) * algebraicState[0]) / T0p;
    }
}

void MotorLoad3::algebraicUpdate(const IOdata& inputs,
                                 const StateData& stateData,
                                 double update[],
                                 const SolverMode& sMode,
                                 double /*alpha*/)
{
    if (!hasAlgebraic(sMode)) {
        return;
    }
    auto loc = offsets.getLocations(stateData, update, sMode, this);
    const double voltage = inputs[VOLTAGE_IN_LOCATION];
    const double theta = inputs[ANGLE_IN_LOCATION];
    const double voltageReal = -voltage * Vcontrol * sin(theta);
    const double voltageImaginary = voltage * Vcontrol * cos(theta);

    gmlc::utilities::solve2x2(r,
                              -xp,
                              xp,
                              r,
                              voltageReal - loc.diffStateLoc[1],
                              voltageImaginary - loc.diffStateLoc[2],
                              loc.destLoc[0],
                              loc.destLoc[1]);
}

void MotorLoad3::getStateName(stringVec& stNames,
                              const SolverMode& sMode,
                              const std::string& prefix) const
{
    std::string const prefix2 = prefix + getName();
    if (isDynamic(sMode)) {
        if (isAlgebraicOnly(sMode)) {
            return;
        }
        auto offsetA = offsets.getAlgOffset(sMode);
        auto offsetD = offsets.getDiffOffset(sMode);
        stNames[offsetA] = prefix2 + ":ir";
        stNames[offsetA + 1] = prefix2 + ":im";
        stNames[offsetD] = prefix2 + ":slip";
        stNames[offsetD + 1] = prefix2 + ":erp";
        stNames[offsetD + 2] = prefix2 + ":emp";
    } else {
        auto offset = offsets.getAlgOffset(sMode);
        stNames[offset] = prefix2 + ":ir";
        stNames[offset + 1] = prefix2 + ":im";
        stNames[offset + 2] = prefix2 + ":slip";
        stNames[offset + 3] = prefix2 + ":erp";
        stNames[offset + 4] = prefix2 + ":emp";
    }
}

void MotorLoad3::timestep(CoreTime time, const IOdata& inputs, const SolverMode& /*sMode*/)
{
    StateData const stateData(time, m_state.data());
    derivative(inputs, stateData, m_dstate_dt.data(), cLocalSolverMode);
    double const timeStep = time - prevTime;
    m_state[2] += timeStep * m_dstate_dt[2];
    m_state[3] += timeStep * m_dstate_dt[3];
    m_state[4] += timeStep * m_dstate_dt[4];
    prevTime = time;
    updateCurrents(inputs, stateData, cLocalSolverMode);
}

void MotorLoad3::updateCurrents(const IOdata& inputs, const StateData& stateData, const SolverMode& sMode)
{
    auto loc = offsets.getLocations(stateData, const_cast<double*>(stateData.state), sMode, this);
    double const voltage = inputs[VOLTAGE_IN_LOCATION];
    double const theta = inputs[ANGLE_IN_LOCATION];

    double const voltageReal = -voltage * Vcontrol * sin(theta);
    double const voltageImaginary = voltage * Vcontrol * cos(theta);

    gmlc::utilities::solve2x2(r,
                              -xp,
                              xp,
                              r,
                              voltageReal - loc.diffStateLoc[1],
                              voltageImaginary - loc.diffStateLoc[2],
                              loc.destLoc[0],
                              loc.destLoc[1]);
}

void MotorLoad3::derivative(const IOdata& /*inputs*/,
                            const StateData& stateData,
                            double deriv[],
                            const SolverMode& sMode)
{
    auto loc = offsets.getLocations(stateData, deriv, sMode, this);
    const double* ast = loc.algStateLoc;
    const double* dst = loc.diffStateLoc;
    double* derivativeVector = loc.destDiffLoc;
    // Get the exciter field
    double const slip = dst[0];

    // if (stateData.time>=1.0)
    // {
    //  mechPower(slip);
    //}

    // slip
    if (opFlags[STALLED]) {
        derivativeVector[0] = 0;
    } else {
        double const electricalTorque = (dst[1] * ast[0]) + (dst[2] * ast[1]);
        derivativeVector[0] = (mechPower(slip) - electricalTorque) / (2 * H);
    }
    // printf("t=%f, slip=%f mp=%f, electricalTorque=%f, dslip=%e\n", stateData.time, slip,mechPower(slip), Te,derivativeVector[0]
    // ); Edp and Eqp
    derivativeVector[1] = systemBaseFrequency * slip * dst[2] - (dst[1] + (x0 - xp) * ast[1]) / T0p;
    derivativeVector[2] = -systemBaseFrequency * slip * dst[1] - (dst[2] - (x0 - xp) * ast[0]) / T0p;
}

void MotorLoad3::jacobianElements(const IOdata& inputs,
                                  const StateData& stateData,
                                  MatrixData<double>& matrixData,
                                  const IOlocs& inputLocs,
                                  const SolverMode& sMode)
{
    index_t refAlg;
    index_t refDiff;
    const double* algebraicState;
    const double* dst;
    double solverCoefficient = stateData.cj;
    if (isDynamic(sMode)) {
        auto loc = offsets.getLocations(stateData, sMode, this);

        refAlg = loc.algOffset;
        refDiff = loc.diffOffset;
        algebraicState = loc.algStateLoc;
        dst = loc.diffStateLoc;
    } else {
        auto offset = offsets.getAlgOffset(sMode);
        refAlg = offset;
        refDiff = offset + 2;
        algebraicState = stateData.state + offset;
        dst = stateData.state + offset + 2;
        solverCoefficient = 0;
    }

    double const voltage = inputs[VOLTAGE_IN_LOCATION];
    double const theta = inputs[ANGLE_IN_LOCATION];
    auto voltageInputLocation = inputLocs[VOLTAGE_IN_LOCATION];
    auto angleInputLocation = inputLocs[ANGLE_IN_LOCATION];

    double const voltageReal = -voltage * Vcontrol * sin(theta);
    double const voltageImaginary = voltage * Vcontrol * cos(theta);

    // currentReal
    // rva[0] = Vm - gmd[2] - r*algebraicState[1] - xp*algebraicState[0];
    // currentImaginary
    // rva[1] = Vr - gmd[1] - r*algebraicState[0] + xp*algebraicState[1];

    const bool hasAlgebraicRows = !isDynamic(sMode) || hasAlgebraic(sMode);
    if (hasAlgebraicRows) {
        // P
        if (angleInputLocation != kNullLocation) {
            matrixData.assign(refAlg, angleInputLocation, voltageReal);
            matrixData.assign(refAlg + 1, angleInputLocation, -voltageImaginary);
        }
        // Q
        if (voltageInputLocation != kNullLocation) {
            matrixData.assign(refAlg, voltageInputLocation, voltageImaginary / voltage);
            matrixData.assign(refAlg + 1, voltageInputLocation, voltageReal / voltage);
        }

        matrixData.assign(refAlg, refAlg, -xp);
        matrixData.assign(refAlg, refAlg + 1, -r);

        matrixData.assign(refAlg + 1, refAlg, -r);
        matrixData.assign(refAlg + 1, refAlg + 1, xp);
    }
    if (isDynamic(sMode) && !hasDifferential(sMode)) {
        return;
    }
    // Ir Differential

    if (hasAlgebraicRows && (!isDynamic(sMode) || hasDifferential(sMode))) {
        matrixData.assign(refAlg, refDiff + 2, -1.0);
        // Im Differential
        matrixData.assign(refAlg + 1, refDiff + 1, -1.0);
    }

    double const slip = dst[0];
    if ((isDynamic(sMode)) || (!opFlags[INIT_TRANSIENT])) {
        /*
    // slip
    double Te = dst[1] * ast[0] + dst[2] * ast[1];
    derivativeVector[0] = (mechPower(slip) - Te) / (2 * H);

    */
        // slip
        if (opFlags[STALLED]) {
            matrixData.assign(refDiff, refDiff, -solverCoefficient);
        } else {
            matrixData.assign(refDiff, refDiff, (dmechds(slip) / (2.0 * H)) - solverCoefficient);
            matrixData.assign(refDiff, refDiff + 1, -algebraicState[0] / (2.0 * H));
            matrixData.assign(refDiff, refDiff + 2, -algebraicState[1] / (2.0 * H));
            if (hasAlgebraicRows) {
                matrixData.assign(refDiff, refAlg, -dst[1] / (2.0 * H));
                matrixData.assign(refDiff, refAlg + 1, -dst[2] / (2.0 * H));
            }
        }
    } else {
        matrixData.assign(refDiff, refDiff, 1.0);
    }
    // omega

    // Edp and Eqp
    // derivativeVector[1] = systemBaseFrequency*slip*dst[2] - (dst[1] + (x0 - xp)*ast[1]) / T0p;
    // derivativeVector[2] = -systemBaseFrequency*slip*dst[1] - (dst[2] - (x0 - xp)*ast[0]) / T0p;

    if (hasAlgebraicRows) {
        matrixData.assign(refDiff + 1, refAlg + 1, -(x0 - xp) / T0p);
    }
    matrixData.assign(refDiff + 1, refDiff, systemBaseFrequency * dst[2]);
    matrixData.assign(refDiff + 1, refDiff + 1, (-1.0 / T0p) - solverCoefficient);
    matrixData.assign(refDiff + 1, refDiff + 2, systemBaseFrequency * slip);

    if (hasAlgebraicRows) {
        matrixData.assign(refDiff + 2, refAlg, (x0 - xp) / T0p);
    }
    matrixData.assign(refDiff + 2, refDiff, -systemBaseFrequency * dst[1]);
    matrixData.assign(refDiff + 2, refDiff + 1, -systemBaseFrequency * slip);
    matrixData.assign(refDiff + 2, refDiff + 2, (-1.0 / T0p) - solverCoefficient);
}

void MotorLoad3::outputPartialDerivatives(const IOdata& inputs,
                                          const StateData& /*stateData*/,
                                          MatrixData<double>& matrixData,
                                          const SolverMode& sMode)
{
    auto refAlg = offsets.getAlgOffset(sMode);
    double const voltage = inputs[VOLTAGE_IN_LOCATION];
    double const theta = inputs[ANGLE_IN_LOCATION];

    double const voltageReal = -voltage * Vcontrol * sin(theta);
    double const voltageImaginary = voltage * Vcontrol * cos(theta);

    // voltageReal*m_state[0] + voltageImaginary*m_state[1];

    // output P
    matrixData.assign(POUT_LOCATION, refAlg, voltageReal * scale);
    matrixData.assign(POUT_LOCATION, refAlg + 1, voltageImaginary * scale);

    // voltageImaginary*m_state[0] - voltageReal*m_state[1];
    // output Q
    matrixData.assign(QOUT_LOCATION, refAlg, voltageImaginary * scale);
    matrixData.assign(QOUT_LOCATION, refAlg + 1, -voltageReal * scale);
}

count_t MotorLoad3::outputDependencyCount(index_t /*num*/, const SolverMode& /*sMode*/) const
{
    return 2;
}
void MotorLoad3::ioPartialDerivatives(const IOdata& inputs,
                                      const StateData& stateData,
                                      MatrixData<double>& matrixData,
                                      const IOlocs& inputLocs,
                                      const SolverMode& sMode)
{
    auto loc = offsets.getLocations(stateData, sMode, this);

    double const voltage = inputs[VOLTAGE_IN_LOCATION];
    double const angle = inputs[ANGLE_IN_LOCATION];

    double const voltageReal = -voltage * Vcontrol * sin(angle);
    double const voltageImaginary = voltage * Vcontrol * cos(angle);

    const double* algebraicState = loc.algStateLoc;

    double const currentReal = algebraicState[0] * scale;
    double const currentImaginary = algebraicState[1] * scale;

    // P=voltageReal*m_state[0] + voltageImaginary*m_state[1];

    // Q=voltageImaginary*m_state[0] - voltageReal*m_state[1];
    matrixData.assignCheckCol(POUT_LOCATION, inputLocs[ANGLE_IN_LOCATION], (-currentReal * voltageImaginary) + (voltageReal * currentImaginary));
    matrixData.assignCheckCol(POUT_LOCATION,
                      inputLocs[VOLTAGE_IN_LOCATION],
                      (currentReal * voltageReal / voltage) + (voltageImaginary * currentImaginary / voltage));
    matrixData.assignCheckCol(QOUT_LOCATION, inputLocs[ANGLE_IN_LOCATION], (voltageReal * currentReal) + (voltageImaginary * currentImaginary));
    matrixData.assignCheckCol(QOUT_LOCATION,
                      inputLocs[VOLTAGE_IN_LOCATION],
                      (voltageImaginary * currentReal / voltage) - (voltageReal * currentImaginary / voltage));
}

index_t MotorLoad3::findIndex(std::string_view field, const SolverMode& sMode) const
{
    index_t ret = kInvalidLocation;
    if (field == "slip") {
        if (isLocal(sMode)) {
            ret = 2;
        } else if (isDynamic(sMode)) {
            ret = offsets.getDiffOffset(sMode);
        } else {
            ret = offsets.getAlgOffset(sMode);
            ret = (ret != kNullLocation) ? ret + 2 : ret;
        }
    } else if (field == "erp") {
        if (isLocal(sMode)) {
            ret = 3;
        } else if (isDynamic(sMode)) {
            ret = offsets.getDiffOffset(sMode);
            ret = (ret != kNullLocation) ? ret + 1 : ret;
        } else {
            ret = offsets.getAlgOffset(sMode);
            ret = (ret != kNullLocation) ? ret + 3 : ret;
        }
    } else if (field == "emp") {
        if (isLocal(sMode)) {
            ret = 4;
        } else if (isDynamic(sMode)) {
            ret = offsets.getDiffOffset(sMode);
            ret = (ret != kNullLocation) ? ret + 2 : ret;
        } else {
            ret = offsets.getAlgOffset(sMode);
            ret = (ret != kNullLocation) ? ret + 4 : ret;
        }
    } else if (field == "ir") {
        ret = offsets.getAlgOffset(sMode);
    } else if (field == "im") {
        ret = offsets.getAlgOffset(sMode);
        ret = (ret != kNullLocation) ? ret + 1 : ret;
    }
    return ret;
}

void MotorLoad3::rootTest(const IOdata& /*inputs*/,
                          const StateData& stateData,
                          double roots[],
                          const SolverMode& sMode)
{
    auto loc = offsets.getLocations(stateData, sMode, this);
    auto rootOffset = offsets.getRootOffset(sMode);
    if (opFlags[STALLED]) {
        double const electricalTorque =
            (loc.diffStateLoc[1] * loc.algStateLoc[0]) + (loc.diffStateLoc[2] * loc.algStateLoc[1]);
        roots[rootOffset] = electricalTorque - mechPower(1.0);
        // printf ("[%f]look power =%f\n",stateData.time,roots[rootOffset]);
    } else {
        double const slip = loc.diffStateLoc[0];
        roots[rootOffset] = 1.0 - slip;
        //  printf("[%f] slip=%f\n", static_cast<double>(stateData.time), slip);
    }
}

void MotorLoad3::rootTrigger(CoreTime /*time*/,
                             const IOdata& inputs,
                             const std::vector<int>& rootMask,
                             const SolverMode& sMode)
{
    if (rootMask[offsets.getRootOffset(sMode)] == 0) {
        return;
    }
    if (opFlags[STALLED]) {
        if (inputs[VOLTAGE_IN_LOCATION] > 0.5) {
            opFlags.reset(STALLED);
            alert(this, JAC_COUNT_INCREASE);
            m_state[2] = 1.0 - 1e-7;
        }
    } else {
        opFlags.set(STALLED);
        alert(this, JAC_COUNT_DECREASE);
        if (inputs[VOLTAGE_IN_LOCATION] < 0.25) {
            alert(this, POTENTIAL_FAULT_CHANGE);
        }
        m_state[2] = 1.0;
    }
}

ChangeCode MotorLoad3::rootCheck(const IOdata& /*inputs*/,
                                 const StateData& stateData,
                                 const SolverMode& sMode,
                                 CheckLevel /*level*/)
{
    if (opFlags[STALLED]) {
        auto loc = offsets.getLocations(stateData, sMode, this);
        const double electricalTorque =
            (loc.diffStateLoc[1] * loc.algStateLoc[0]) + (loc.diffStateLoc[2] * loc.algStateLoc[1]);
        if (electricalTorque - mechPower(1.0) > 0) {
            opFlags.reset(STALLED);
            alert(this, JAC_COUNT_INCREASE);
            return ChangeCode::JACOBIAN_CHANGE;
        }
    }
    return ChangeCode::NO_CHANGE;
}

double MotorLoad3::getRealPower() const
{
    double const voltage = bus->getVoltage();
    double const ang = bus->getAngle();
    double const voltageReal = -voltage * Vcontrol * sin(ang);
    double const voltageImaginary = voltage * Vcontrol * cos(ang);
    double const realPower = (voltageReal * m_state[0]) + (voltageImaginary * m_state[1]);
    return realPower * scale;
}

double MotorLoad3::getReactivePower() const
{
    double const voltage = bus->getVoltage();
    double const ang = bus->getAngle();
    double const voltageReal = -voltage * Vcontrol * sin(ang);
    double const voltageImaginary = voltage * Vcontrol * cos(ang);
    double const reactivePower = (voltageImaginary * m_state[0]) - (voltageReal * m_state[1]);

    return reactivePower * scale;
}

double MotorLoad3::getRealPower(const IOdata& inputs,
                                const StateData& stateData,
                                const SolverMode& sMode) const
{
    const double voltage = inputs[VOLTAGE_IN_LOCATION];
    double const angle = inputs[ANGLE_IN_LOCATION];

    double const voltageReal = -voltage * Vcontrol * sin(angle);
    double const voltageImaginary = voltage * Vcontrol * cos(angle);

    auto offset = offsets.getAlgOffset(sMode);
    double const currentImaginary = stateData.state[offset + 1];
    double const currentReal = stateData.state[offset];
    double const realPower = (voltageReal * currentReal) + (voltageImaginary * currentImaginary);

    return realPower * scale;
}

double MotorLoad3::getReactivePower(const IOdata& inputs,
                                    const StateData& stateData,
                                    const SolverMode& sMode) const
{
    const double voltage = inputs[VOLTAGE_IN_LOCATION];
    double const angle = inputs[ANGLE_IN_LOCATION];

    double const voltageReal = -voltage * Vcontrol * sin(angle);
    double const voltageImaginary = voltage * Vcontrol * cos(angle);

    auto offset = offsets.getAlgOffset(sMode);
    double const currentImaginary = stateData.state[offset + 1];
    double const currentReal = stateData.state[offset];
    double const reactivePower = (voltageImaginary * currentReal) - (voltageReal * currentImaginary);

    return reactivePower * scale;
}

double MotorLoad3::getRealPower(double voltage) const
{
    double const ang = bus->getAngle();

    double const voltageReal = -voltage * Vcontrol * sin(ang);
    double const voltageImaginary = voltage * Vcontrol * cos(ang);
    double const realPower = (voltageReal * m_state[0]) + (voltageImaginary * m_state[1]);
    return realPower * scale;
}

double MotorLoad3::getReactivePower(double voltage) const
{
    double const ang = bus->getAngle();

    double const voltageReal = -voltage * Vcontrol * sin(ang);
    double const voltageImaginary = voltage * Vcontrol * cos(ang);
    double const reactivePower = (voltageImaginary * m_state[0]) - (voltageReal * m_state[1]);

    return reactivePower * scale;
}
}  // namespace griddyn::loads
