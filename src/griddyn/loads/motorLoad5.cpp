/*
 * Copyright (c) 2014-2026, Lawrence Livermore National Security
 * See the top-level NOTICE for additional details. All rights reserved.
 * SPDX-License-Identifier: BSD-3-Clause
 */

#include "motorLoad5.h"

#include "../gridBus.h"
#include "core/coreObjectTemplates.hpp"
#include "core/objectFactory.hpp"
#include "gmlc/utilities/vectorOps.hpp"
#include "utilities/matrixData.hpp"
#include <iostream>
#include <string>
#include <vector>
namespace griddyn::loads {
// setup the load object factories

motorLoad5::motorLoad5(const std::string& objName): motorLoad3(objName)
{
    H = 4;
}
CoreObject* motorLoad5::clone(CoreObject* obj) const
{
    auto* ld = cloneBase<motorLoad5, motorLoad3>(this, obj);
    if (ld == nullptr) {
        return obj;
    }

    ld->T0pp = T0pp;
    ld->xpp = xpp;
    ld->r2 = r2;
    ld->x2 = x2;
    return ld;
}

void motorLoad5::pFlowObjectInitializeA(coreTime time0, std::uint32_t flags)
{
    // setup the parameters
    x0 = x + xm;
    xp = x + x1 * xm / (x1 + xm);
    T0p = (x1 + xm) / (systemBaseFrequency * r1);

    T0pp = (x2 + x1 * xm / (x1 + xm)) / (systemBaseFrequency * r2);
    xpp = x + x1 * x2 * xm / (x1 * x2 + x1 * xm + x2 * xm);

    scale = mBase / systemBasePower;
    m_state.resize(7, 0);
    if (opFlags[init_transient]) {
        m_state[2] = init_slip;
    } else if (Pmot > -kHalfBigNum) {
        m_state[2] = computeSlip(Pmot);
    } else {
        m_state[2] = 1.0;
        opFlags.set(init_transient);
    }
    // NOLINTNEXTLINE
    Load::pFlowObjectInitializeA(time0, flags);
    converge();

    loadStateSizes(cLocalSolverMode);
    setOffset(0, cLocalSolverMode);
}

void motorLoad5::converge()
{
    double voltage = bus->getVoltage();
    double theta = bus->getAngle();
    double slip = m_state[2];
    double Qtest = qPower(voltage, m_state[2]);
    double im;
    double em;
    double ir;
    double er;
    double Vr = -voltage * Vcontrol * sin(theta);
    double Vm = voltage * Vcontrol * cos(theta);
    gmlc::utilities::solve2x2(Vr, Vm, Vm, -Vr, getP(), Qtest, ir, im);
    double err{10.0};
    int ccnt{0};
    double fbs = slip * systemBaseFrequency;

    double perr{10.0};
    while (err > 1e-6) {
        double erp = Vr - r * ir + xp * im;
        double emp = Vm - r * im - xp * ir;
        gmlc::utilities::solve2x2(fbs,
                                  -1.0 / T0pp,
                                  1.0 / T0pp,
                                  fbs,
                                  fbs * erp - erp / T0pp + (xp - xpp) / T0pp * ir,
                                  fbs * emp - emp / T0pp + (xp - xpp) / T0pp * im,
                                  er,
                                  em);

        double slipp = (er + (x0 - xp) * im) / T0p / systemBaseFrequency / em;
        double dslip = slipp - slip;
        if (getP() > 0) {
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
        m_state[0] = ir;
        m_state[1] = im;
        m_state[2] = slip;
        m_state[3] = er;
        m_state[4] = em;
        m_state[5] = erp;
        m_state[6] = emp;
        if (++ccnt > 50) {
            break;
        }

        perr = err;

        fbs = slip * systemBaseFrequency;
        ir = (-fbs * er * T0p - em) / (-(x0 - xp));
        im = (mechPower(slip) - erp * ir) / emp;
    }
}

void motorLoad5::dynObjectInitializeA(coreTime /*time0*/, std::uint32_t /*flags*/) {}
void motorLoad5::dynObjectInitializeB(const IOdata& inputs,
                                      const IOdata& /*desiredOutput*/,
                                      IOdata& /*fieldSet*/)
{
    if (opFlags[init_transient]) {
        derivative(inputs, emptyStateData, m_dstate_dt.data(), cLocalSolverMode);
    }
}

stateSizes motorLoad5::LocalStateSizes(const solverMode& sMode) const
{
    stateSizes SS;
    if (isDynamic(sMode)) {
        SS.algSize = 2;
        if (!isAlgebraicOnly(sMode)) {
            SS.diffSize = 5;
        }
    } else {
        SS.algSize = 7;
    }
    return SS;
}

count_t motorLoad5::LocalJacobianCount(const solverMode& sMode) const
{
    count_t localJacSize = 0;
    if (isDynamic(sMode)) {
        localJacSize = 8;
        if (!isAlgebraicOnly(sMode)) {
            localJacSize += 27;
        }
    } else {
        if (opFlags[init_transient]) {
            localJacSize = 31;
        } else {
            localJacSize = 35;
        }
    }
    return localJacSize;
}

// set properties
void motorLoad5::set(std::string_view param, std::string_view val)
{
    if (param.empty()) {
    } else {
        motorLoad3::set(param, val);
    }
}

void motorLoad5::set(std::string_view param, double val, units::unit unitType)
{
    if (param == "r2") {
        r2 = val;
    } else if (param == "x2") {
        x2 = val;
    } else {
        motorLoad3::set(param, val, unitType);
    }
}

// residual
void motorLoad5::residual(const IOdata& inputs,
                          const stateData& sD,
                          double resid[],
                          const solverMode& sMode)
{
    if (isDynamic(sMode)) {
        auto Loc = offsets.getLocations(sD, resid, sMode, this);

        double voltage = inputs[voltageInLocation];
        double theta = inputs[angleInLocation];
        const double* gm = Loc.algStateLoc;
        const double* gmd = Loc.diffStateLoc;
        const double* gmp = Loc.dstateLoc;

        double* rva = Loc.destLoc;
        double* rvd = Loc.destDiffLoc;

        double Vr = -voltage * Vcontrol * sin(theta);
        double Vm = voltage * Vcontrol * cos(theta);

        // ir
        rva[irA] = Vm - gmd[emppD] - r * gm[imA] - xpp * gm[irA];
        // im
        rva[imA] = Vr - gmd[erppD] - r * gm[irA] + xpp * gm[imA];

        if (isAlgebraicOnly(sMode)) {
            return;
        }
        derivative(inputs, sD, resid, sMode);
        // Get the exciter field

        // delta
        rvd[0] -= gmp[0];
        rvd[1] -= gmp[1];
        rvd[2] -= gmp[2];
        rvd[3] -= gmp[3];
        rvd[4] -= gmp[4];
    } else {
        auto offset = offsets.getAlgOffset(sMode);
        double voltage = inputs[voltageInLocation];
        double theta = inputs[angleInLocation];

        const double* gm = sD.state + offset;
        double* rv = resid + offset;

        double Vr = -voltage * Vcontrol * sin(theta);
        double Vm = voltage * Vcontrol * cos(theta);

        // ir
        rv[irA] = Vm - gm[emppA] - r * gm[imA] - xpp * gm[irA];
        // im
        rv[imA] = Vr - gm[erppA] - r * gm[irA] + xpp * gm[imA];

        double slip = gm[slipA];
        // printf("angle=%f, slip=%f\n", theta, slip);
        // slip
        if (opFlags[init_transient]) {
            rv[slipA] = slip - m_state[slipA];
        } else {
            double Te = gm[erppA] * gm[irA] + gm[emppA] * gm[imA];
            rv[slipA] = (mechPower(slip) - Te) / (2 * H);
        }
        // Erp and Emp
        rv[erpA] = systemBaseFrequency * slip * gm[empA] - (gm[erpA] + (x0 - xp) * gm[imA]) / T0p;
        rv[empA] = -systemBaseFrequency * slip * gm[erpA] - (gm[empA] - (x0 - xp) * gm[irA]) / T0p;
        rv[erppA] = -systemBaseFrequency * slip * (gm[empA] - gm[emppA]) -
            (gm[erpA] - gm[emppA] - (xp - xpp) * gm[imA]) / T0pp;
        rv[emppA] = systemBaseFrequency * slip * (gm[erpA] - gm[erppA]) -
            (gm[empA] - gm[erppA] + (xp - xpp) * gm[irA]) / T0pp;
    }
}

void motorLoad5::getStateName(stringVec& stNames,
                              const solverMode& sMode,
                              const std::string& prefix) const
{
    std::string prefix2 = prefix + getName();
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
        stNames[offsetD + 3] = prefix2 + ":erpp";
        stNames[offsetD + 4] = prefix2 + ":empp";
    } else {
        auto offset = offsets.getAlgOffset(sMode);
        stNames[offset] = prefix2 + ":ir";
        stNames[offset + 1] = prefix2 + ":im";
        stNames[offset + 2] = prefix2 + ":slip";
        stNames[offset + 3] = prefix2 + ":erp";
        stNames[offset + 4] = prefix2 + ":emp";
        stNames[offset + 5] = prefix2 + ":erpp";
        stNames[offset + 6] = prefix2 + ":empp";
    }
}
void motorLoad5::timestep(coreTime time, const IOdata& inputs, const solverMode& /*sMode*/)
{
    stateData sD(time, m_state.data());

    derivative(inputs, sD, m_dstate_dt.data(), cLocalSolverMode);
    double dt = time - prevTime;
    m_state[2] += dt * m_dstate_dt[2];
    m_state[3] += dt * m_dstate_dt[3];
    m_state[4] += dt * m_dstate_dt[4];
    m_state[5] += dt * m_dstate_dt[5];
    m_state[6] += dt * m_dstate_dt[6];
    prevTime = time;
    updateCurrents(inputs, sD, cLocalSolverMode);
}

void motorLoad5::updateCurrents(const IOdata& inputs, const stateData& sD, const solverMode& sMode)
{
    auto Loc = offsets.getLocations(sD, const_cast<double*>(sD.state), sMode, this);
    double voltage = inputs[voltageInLocation];
    double theta = inputs[angleInLocation];

    double vr = -voltage * Vcontrol * sin(theta);
    double vm = voltage * Vcontrol * cos(theta);

    gmlc::utilities::solve2x2(r,
                              -xpp,
                              xpp,
                              r,
                              vr - Loc.diffStateLoc[3],
                              vm - Loc.diffStateLoc[4],
                              Loc.destLoc[0],
                              Loc.destLoc[1]);
}

void motorLoad5::derivative(const IOdata& /*inputs*/,
                            const stateData& sD,
                            double deriv[],
                            const solverMode& sMode)
{
    auto Loc = offsets.getLocations(sD, deriv, sMode, this);
    const double* ast = Loc.algStateLoc;
    const double* dst = Loc.diffStateLoc;
    const double* ddt = Loc.dstateLoc;
    double* dv = Loc.destDiffLoc;
    // Get the exciter field
    double slip = dst[slipD];

    if (Loc.time >= 1.0) {
        mechPower(slip);
    }

    // slip
    if (opFlags[stalled]) {
        dv[slipD] = 0;
    } else {
        double Te = dst[erppD] * ast[irA] + dst[emppD] * ast[imA];
        dv[slipD] = (mechPower(slip) - Te) / (2 * H);
    }
    // printf("t=%f, slip=%f mp=%f, te=%f, dslip=%e\n", sD.time, slip,mechPower(slip), Te,dv[0]
    // ); Edp and Eqp
    dv[erpD] = systemBaseFrequency * slip * dst[empD] - (dst[erpD] + (x0 - xp) * ast[imA]) / T0p;
    dv[empD] = -systemBaseFrequency * slip * dst[erpD] - (dst[empD] - (x0 - xp) * ast[irA]) / T0p;
    dv[erppD] = -systemBaseFrequency * slip * (dst[empD] - dst[emppD]) + ddt[erpD] -
        (dst[erpD] - dst[emppD] - (xp - xpp) * ast[imA]) / T0pp;
    dv[emppD] = systemBaseFrequency * slip * (dst[erpD] - dst[erppD]) + ddt[empD] -
        (dst[empD] - dst[erppD] + (xp - xpp) * ast[irA]) / T0pp;
}

void motorLoad5::jacobianElements(const IOdata& inputs,
                                  const stateData& sD,
                                  matrixData<double>& md,
                                  const IOlocs& inputLocs,
                                  const solverMode& sMode)
{
    index_t refAlg;
    const double* gm;
    index_t refDiff;
    const double* dst;
    double cj = sD.cj;
    if (isDynamic(sMode)) {
        auto Loc = offsets.getLocations(sD, sMode, this);

        refAlg = Loc.algOffset;
        refDiff = Loc.diffOffset;
        gm = Loc.algStateLoc;
        dst = Loc.diffStateLoc;
        // auto mx = &(offsets.getOffsets (sMode)->jacSize);
        // auto js = *mx;
    } else {
        auto offset = offsets.getAlgOffset(sMode);
        refAlg = offset;
        refDiff = offset + 2;
        gm = sD.state + offset;
        dst = sD.state + offset + 2;
        cj = 0;
    }

    double voltage = inputs[voltageInLocation];
    double theta = inputs[angleInLocation];
    auto VLoc = inputLocs[voltageInLocation];
    auto TLoc = inputLocs[angleInLocation];

    double Vr = -voltage * Vcontrol * sin(theta);
    double Vm = voltage * Vcontrol * cos(theta);

    // ir
    // rva[0] = Vm - gmd[2] - r*gm[1] - xp*gm[0];
    // im
    // rva[1] = Vr - gmd[1] - r*gm[0] + xp*gm[1];

    // P
    if (TLoc != kNullLocation) {
        md.assign(refAlg, TLoc, Vr);
        md.assign(refAlg + 1, TLoc, -Vm);
    }
    // Q
    if (VLoc != kNullLocation) {
        md.assign(refAlg, VLoc, Vm / voltage);
        md.assign(refAlg + 1, VLoc, Vr / voltage);
    }

    md.assign(refAlg, refAlg, -xpp);
    md.assign(refAlg, refAlg + 1, -r);

    md.assign(refAlg + 1, refAlg, -r);
    md.assign(refAlg + 1, refAlg + 1, xpp);
    if ((isDynamic(sMode)) && (isAlgebraicOnly(sMode))) {
        return;
    }
    // Ir Differential

    md.assign(refAlg, refDiff + 4, -1);
    // Im Differential
    md.assign(refAlg + 1, refDiff + 3, -1);

    double slip = dst[0];
    if ((isDynamic(sMode)) || (!opFlags[init_transient])) {
        /*
    // slip
    double Te = dst[1] * ast[0] + dst[2] * ast[1];
    dv[0] = (mechPower(slip) - Te) / (2 * H);

    */
        // slip
        if (opFlags[stalled]) {
            md.assign(refDiff, refDiff, -cj);
        } else {
            md.assign(refDiff, refDiff, dmechds(slip) / (2 * H) - cj);
            md.assign(refDiff, refDiff + 3, -gm[0] / (2 * H));
            md.assign(refDiff, refDiff + 4, -gm[1] / (2 * H));
            md.assign(refDiff, refAlg, -dst[3] / (2 * H));
            md.assign(refDiff, refAlg + 1, -dst[4] / (2 * H));
        }
    } else {
        md.assign(refDiff, refDiff, 1);
    }
    // omega

    // Erp and Emp
    // dv[1] = systemBaseFrequency*slip*dst[2] - (dst[1] + (x0 - xp)*ast[1]) / T0p;
    // dv[2] = -systemBaseFrequency*slip*dst[1] - (dst[2] + (x0 - xp)*ast[0]) / T0p;

    md.assign(refDiff + 1, refAlg + 1, -(x0 - xp) / T0p);
    md.assign(refDiff + 1, refDiff, systemBaseFrequency * dst[2]);
    md.assign(refDiff + 1, refDiff + 1, -1 / T0p - cj);
    md.assign(refDiff + 1, refDiff + 2, systemBaseFrequency * slip);

    md.assign(refDiff + 2, refAlg, (x0 - xp) / T0p);
    md.assign(refDiff + 2, refDiff, -systemBaseFrequency * dst[1]);
    md.assign(refDiff + 2, refDiff + 1, -systemBaseFrequency * slip);
    md.assign(refDiff + 2, refDiff + 2, -1 / T0p - cj);

    // Erpp and Empp
    // dv[3] = -systemBaseFrequency*slip*(dst[2] - dst[4]) + ddt[1] - (dst[1] - dst[4] - (xp -
    // xpp)*ast[1]) / T0pp; dv[4] = systemBaseFrequency*slip*(dst[1] - dst[3]) + ddt[2] -
    // (dst[2] - dst[3] + (xp - xpp)*ast[0]) / T0pp;
    md.assign(refDiff + 3, refAlg + 1, (xp - xpp) / T0pp);
    md.assign(refDiff + 3, refDiff, -systemBaseFrequency * (dst[2] - dst[4]));
    md.assign(refDiff + 3, refDiff + 1, -1 / T0pp + cj);
    md.assign(refDiff + 3, refDiff + 2, -systemBaseFrequency * slip);
    md.assign(refDiff + 3, refDiff + 3, -cj);
    md.assign(refDiff + 3, refDiff + 4, systemBaseFrequency * slip + 1 / T0pp);

    md.assign(refDiff + 4, refAlg, -(xp - xpp) / T0pp);
    md.assign(refDiff + 4, refDiff, systemBaseFrequency * (dst[1] - dst[3]));
    md.assign(refDiff + 4, refDiff + 1, systemBaseFrequency * slip);
    md.assign(refDiff + 4, refDiff + 2, -1 / T0pp + cj);
    md.assign(refDiff + 4, refDiff + 3, -systemBaseFrequency * slip + 1 / T0pp);
    md.assign(refDiff + 4, refDiff + 4, -cj);
}

index_t motorLoad5::findIndex(std::string_view field, const solverMode& sMode) const
{
    index_t ret = kInvalidLocation;
    if (field == "erpp") {
        if (isLocal(sMode)) {
            ret = 5;
        } else if (isDynamic(sMode)) {
            ret = offsets.getDiffOffset(sMode);
            ret = (ret != kNullLocation) ? ret + 3 : ret;
        } else {
            ret = offsets.getAlgOffset(sMode);
            ret = (ret != kNullLocation) ? ret + 5 : ret;
        }
    } else if (field == "empp") {
        if (isLocal(sMode)) {
            ret = 6;
        } else if (isDynamic(sMode)) {
            ret = offsets.getDiffOffset(sMode);
            ret = (ret != kNullLocation) ? ret + 4 : ret;
        } else {
            ret = offsets.getAlgOffset(sMode);
            ret = (ret != kNullLocation) ? ret + 6 : ret;
        }
    } else {
        ret = motorLoad3::findIndex(field, sMode);
    }

    return ret;
}

void motorLoad5::rootTest(const IOdata& /*inputs*/,
                          const stateData& sD,
                          double roots[],
                          const solverMode& sMode)
{
    auto Loc = offsets.getLocations(sD, sMode, this);
    auto ro = offsets.getRootOffset(sMode);
    if (opFlags[stalled]) {
        double Te = Loc.diffStateLoc[erppD] * Loc.algStateLoc[irA] +
            Loc.diffStateLoc[emppD] * Loc.algStateLoc[imA];
        roots[ro] = Te - mechPower(1.0);
    } else {
        double slip = Loc.diffStateLoc[0];
        roots[ro] = 1.0 - slip;
    }
}

void motorLoad5::rootTrigger(coreTime /*time*/,
                             const IOdata& inputs,
                             const std::vector<int>& rootMask,
                             const solverMode& sMode)
{
    if (rootMask[offsets.getRootOffset(sMode)] == 0) {
        return;
    }
    if (opFlags[stalled]) {
        if (inputs[voltageInLocation] > 0.5) {
            opFlags.reset(stalled);
            alert(this, JAC_COUNT_INCREASE);
            m_state[slipA] = 1.0 - 1e-7;
        }
    } else {
        opFlags.set(stalled);
        alert(this, JAC_COUNT_DECREASE);
        m_state[slipA] = 1.0;
    }
}

change_code motorLoad5::rootCheck(const IOdata& /*inputs*/,
                                  const stateData& sD,
                                  const solverMode& sMode,
                                  check_level_t /*level*/)
{
    if (opFlags[stalled]) {
        auto Loc = offsets.getLocations(sD, sMode, this);
        double Te = Loc.diffStateLoc[erppD] * Loc.algStateLoc[irA] +
            Loc.diffStateLoc[emppD] * Loc.algStateLoc[imA];
        if (Te - mechPower(1.0) > 0) {
            opFlags.reset(stalled);
            alert(this, JAC_COUNT_INCREASE);
            return change_code::jacobian_change;
        }
    }
    return change_code::no_change;
}
}  // namespace griddyn::loads
