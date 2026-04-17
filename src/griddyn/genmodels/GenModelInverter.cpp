/*
 * Copyright (c) 2014-2026, Lawrence Livermore National Security
 * See the top-level NOTICE for additional details. All rights reserved.
 * SPDX-License-Identifier: BSD-3-Clause
 */

#include "GenModelInverter.h"

#include "../Generator.h"
#include "../gridBus.h"
#include "core/coreExceptions.h"
#include "core/coreObjectTemplates.hpp"
#include "gmlc/utilities/vectorOps.hpp"
#include "utilities/matrixData.hpp"
#include <algorithm>
#include <cmath>
#include <complex>
#include <string>
#include <vector>

namespace griddyn::genmodels {
GenModelInverter::GenModelInverter(const std::string& objName): GenModel(objName) {}
coreObject* GenModelInverter::clone(coreObject* obj) const
{
    auto* gd = cloneBase<GenModelInverter, GenModel>(this, obj);
    if (gd == nullptr) {
        return obj;
    }

    gd->minAngle = minAngle;
    gd->maxAngle = maxAngle;

    return gd;
}

void GenModelInverter::dynObjectInitializeA(coreTime /*time0*/, std::uint32_t /*flags*/)
{
    offsets.local().local.algSize = 1;
    offsets.local().local.jacSize = 4;
    offsets.local().local.algRoots = 1;
}
// initial conditions
void GenModelInverter::dynObjectInitializeB(const IOdata& inputs,
                                            const IOdata& desiredOutput,
                                            IOdata& fieldSet)
{
    double* gm = m_state.data();
    double V = inputs[voltageInLocation];
    std::complex<double> outCurrent(desiredOutput[PoutLocation] / V,
                                    -desiredOutput[QoutLocation] / V);
    auto Z = std::complex<double>(Rs, Xd);
    auto Em = Z * outCurrent + V;

    gm[0] = std::arg(Em);

    fieldSet[genModelEftInLocation] = std::abs(Em) - 1.0;

    double loss = 0;
    if (Rs != 0) {
        double cosA = cos(gm[0]);
        double Vloss1 = V * V * g;
        double Vloss2 = 2.0 * V * g * std::abs(Em) * cosA;
        double Vloss3 = std::abs(Em) * std::abs(Em) * g;
        loss = Vloss1 + Vloss2 + Vloss3;
    }

    fieldSet[genModelPmechInLocation] = desiredOutput[PoutLocation] + loss;  // Pmt

    bus = static_cast<gridBus*>(find("bus"));
}

void GenModelInverter::algebraicUpdate(const IOdata& inputs,
                                       const stateData& /*sD*/,
                                       double update[],
                                       const solverMode& sMode,
                                       double /*alpha*/)
{
    auto offset = offsets.getAlgOffset(sMode);

    // double angle = std::atan2(g, b);

    double Pmt = inputs[genModelPmechInLocation];
    if (opFlags[at_angle_limits]) {
        if (Pmt > 0) {
            update[offset] = maxAngle;
        } else {
            update[offset] = minAngle;
        }
    } else {
        // Get the exciter field
        double V = inputs[voltageInLocation];
        double Eft = inputs[genModelEftInLocation] + 1.0;
        if (Rs != 0.0) {
            double R = std::hypot(2.0 * g, b);
            double gamma = std::atan(b / (2.0 * g)) - kPI / 2;
            double Vloss1 = V * V * g;
            double Vloss3 = Eft * Eft * g;
            double powerRatio = (Pmt - Vloss1 - Vloss3) / (Eft * V * R);
            if (std::abs(powerRatio) >= 1.0) {
                update[offset] = (powerRatio >= 1.0) ? kPI / 2.0 : -kPI / 2.0;
            } else {
                update[offset] = std::asin(powerRatio) + gamma;
            }
        } else {
            double powerRatio = Pmt / (Eft * V * b);
            if (std::abs(powerRatio) >= 1.0) {
                update[offset] = (powerRatio >= 1.0) ? kPI / 2.0 : -kPI / 2.0;
            } else {
                update[offset] = std::asin(powerRatio);
            }
        }
    }
}

// residual

void GenModelInverter::residual(const IOdata& inputs,
                                const stateData& sD,
                                double resid[],
                                const solverMode& sMode)
{
    if (!hasAlgebraic(sMode)) {
        return;
    }
    auto Loc = offsets.getLocations(sD, resid, sMode, this);

    double angle = *Loc.algStateLoc;
    // printf("time=%f, angle=%f\n", sD.time, angle);
    double Pmt = inputs[genModelPmechInLocation];
    if (opFlags[at_angle_limits]) {
        if (Pmt > 0) {
            Loc.destLoc[0] = maxAngle - angle;
        } else {
            Loc.destLoc[0] = minAngle - angle;
        }
    } else {
        // Get the exciter field
        double Eft = inputs[genModelEftInLocation] + 1.0;

        double V = inputs[voltageInLocation];

        double PnoR = Eft * V * b * sin(angle);
        if (Rs != 0.0) {
            double cosA = cos(angle);
            double Vloss1 = V * V * g;
            double Vloss2 = 2.0 * V * g * Eft * cosA;
            double Vloss3 = Eft * Eft * g;
            double loss = Vloss1 + Vloss2 + Vloss3;
            Loc.destLoc[0] = Pmt - PnoR - loss;
        } else {
            Loc.destLoc[0] = Pmt - PnoR;
        }
    }
}

double GenModelInverter::getFreq(const stateData& sD,
                                 const solverMode& sMode,
                                 index_t* freqOffset) const
{
    // there is no inertia in this gen model so it can't compute a frequency and
    // must use the bus frequency
    if (freqOffset != nullptr) {
        *freqOffset = bus->getOutputLoc(sMode, frequencyInLocation);
    }
    return bus->getFreq(sD, sMode);
}

double GenModelInverter::getAngle(const stateData& sD,
                                  const solverMode& sMode,
                                  index_t* angleOffset) const
{
    auto offset = offsets.getAlgOffset(sMode);
    if (angleOffset != nullptr) {
        *angleOffset = offset;
    }

    return (!sD.empty()) ? sD.state[offset] : m_state[0];
}

IOdata GenModelInverter::getOutputs(const IOdata& inputs,
                                    const stateData& sD,
                                    const solverMode& sMode) const
{
    auto Loc = offsets.getLocations(sD, sMode, this);

    IOdata out(2);
    double V = inputs[voltageInLocation];
    double Eft = inputs[genModelEftInLocation] + 1.0;
    double cAng = cos(Loc.algStateLoc[0]);
    double sAng = sin(Loc.algStateLoc[0]);

    out[PoutLocation] = realPowerCompute(V, Eft, cAng, sAng);
    out[QoutLocation] = reactivePowerCompute(V, Eft, cAng, sAng);

    return out;
}

double GenModelInverter::realPowerCompute(double V, double Ef, double cosA, double sinA) const
{
    return V * V * g - V * g * Ef * cosA - V * Ef * b * sinA;
}

double GenModelInverter::reactivePowerCompute(double V, double Ef, double cosA, double sinA) const
{
    return V * V * b - V * Ef * b * cosA + V * Ef * g * sinA;
}

double GenModelInverter::getOutput(const IOdata& inputs,
                                   const stateData& sD,
                                   const solverMode& sMode,
                                   index_t outNum) const
{
    auto Loc = offsets.getLocations(sD, sMode, this);
    double cAng = cos(Loc.algStateLoc[0]);
    double sAng = sin(Loc.algStateLoc[0]);
    double V = inputs[voltageInLocation];
    double Eft = inputs[genModelEftInLocation] + 1.0;

    if (outNum == PoutLocation) {
        return realPowerCompute(V, Eft, cAng, sAng);
    }
    if (outNum == QoutLocation) {
        return reactivePowerCompute(V, Eft, cAng, sAng);
    }
    return kNullVal;
}

void GenModelInverter::ioPartialDerivatives(const IOdata& inputs,
                                            const stateData& sD,
                                            matrixData<double>& md,
                                            const IOlocs& inputLocs,
                                            const solverMode& sMode)
{
    auto Loc = offsets.getLocations(sD, sMode, this);

    double V = inputs[voltageInLocation];

    double cAng = cos(Loc.algStateLoc[0]);
    double sAng = sin(Loc.algStateLoc[0]);

    // out[PoutLocation] = V*V*g - V*g*Eft*cAng - V*Eft*b*sAng;
    // out[QoutLocation] = V*V*b - V*Eft*b*cAng + V*Eft*g*sAng;

    if (inputLocs[genModelEftInLocation] != kNullLocation) {
        md.assign(PoutLocation, inputLocs[genModelEftInLocation], -V * g * cAng - V * b * sAng);
        md.assign(QoutLocation, inputLocs[genModelEftInLocation], -V * b * cAng + V * g * sAng);
    }

    if (inputLocs[voltageInLocation] != kNullLocation) {
        double Eft = inputs[genModelEftInLocation] + 1.0;
        md.assign(PoutLocation,
                  inputLocs[voltageInLocation],
                  2.0 * V * g - g * Eft * cAng - Eft * b * sAng);
        md.assign(QoutLocation,
                  inputLocs[voltageInLocation],
                  2.0 * V * b - Eft * b * cAng + V * Eft * g * sAng);
    }
}

void GenModelInverter::jacobianElements(const IOdata& inputs,
                                        const stateData& sD,
                                        matrixData<double>& md,
                                        const IOlocs& inputLocs,
                                        const solverMode& sMode)
{
    if (!hasAlgebraic(sMode)) {
        return;
    }
    auto Loc = offsets.getLocations(sD, sMode, this);
    auto offset = Loc.algOffset;
    if (opFlags[at_angle_limits]) {
        md.assign(offset, offset, -1.0);
    } else {
        double V = inputs[voltageInLocation];
        double Eft = inputs[genModelEftInLocation] + 1.0;
        double cAng = cos(Loc.algStateLoc[0]);
        double sAng = sin(Loc.algStateLoc[0]);

        // rva[0] = Pmt -V*V*g - Eft*Eft*g - 2.0*V * Eft*g*cos(gm[0]) - V *
        // Eft*b*sin(gm[0]);

        md.assign(offset, offset, 2.0 * V * Eft * g * sAng - V * Eft * b * cAng);

        md.assignCheckCol(offset, inputLocs[genModelPmechInLocation], 1.0);
        md.assignCheckCol(offset,
                          inputLocs[genModelEftInLocation],
                          -2.0 * Eft * g - 2.0 * V * g * cAng - V * b * sAng);
        md.assignCheckCol(offset,
                          inputLocs[voltageInLocation],
                          -2.0 * V * g - 2.0 * Eft * g * cAng - Eft * b * sAng);
    }
}

void GenModelInverter::outputPartialDerivatives(const IOdata& inputs,
                                                const stateData& sD,
                                                matrixData<double>& md,
                                                const solverMode& sMode)
{
    if (!hasAlgebraic(sMode)) {
        return;
    }
    auto Loc = offsets.getLocations(sD, sMode, this);

    double V = inputs[voltageInLocation];
    double Eft = inputs[genModelEftInLocation] + 1.0;
    double cAng = cos(Loc.algStateLoc[0]);
    double sAng = sin(Loc.algStateLoc[0]);

    // out[PoutLocation] = V*V*g - V*g*Eft*cAng - V*Eft*b*sAng;
    // out[QoutLocation] = V*V*b - V*Eft*b*cAng + V*Eft*g*sAng;

    md.assign(PoutLocation, Loc.algOffset, V * g * Eft * sAng - V * Eft * b * cAng);
    md.assign(QoutLocation, Loc.algOffset, V * Eft * b * sAng + V * Eft * g * cAng);
}

count_t GenModelInverter::outputDependencyCount(index_t /*num*/, const solverMode& /*sMode*/) const
{
    return 1;
}
static const stringVec genModelNames{"angle"};

stringVec GenModelInverter::localStateNames() const
{
    return genModelNames;
}
// set parameters
void GenModelInverter::set(const std::string& param, const std::string& val)
{
    return gridSubModel::set(param, val);
}

void GenModelInverter::set(const std::string& param, double val, units::unit unitType)
{
    if (param.length() == 1) {
        switch (param[0]) {
            case 'x':
                Xd = val;
                reCalcImpedences();
                break;
            case 'r':
                Rs = val;
                reCalcImpedences();
                break;
            default:
                throw(unrecognizedParameter(param));
        }

        return;
    }

    if ((param == "xd") || (param == "xs")) {
        Xd = val;
        reCalcImpedences();
    } else if (param == "maxangle") {
        maxAngle = units::convert(val, unitType, units::rad);
    } else if (param == "minangle") {
        minAngle = units::convert(val, unitType, units::rad);
    } else if (param == "rs") {
        Rs = val;
        reCalcImpedences();
    } else {
        GenModel::set(param, val, unitType);
    }
}

void GenModelInverter::reCalcImpedences()
{
    double Ys = 1.0 / (Rs * Rs + Xd * Xd);
    b = Xd * Ys;
    g = Rs * Ys;
}

void GenModelInverter::rootTest(const IOdata& inputs,
                                const stateData& sD,
                                double roots[],
                                const solverMode& sMode)
{
    if (rootSize(sMode) > 0) {
        auto ro = offsets.getRootOffset(sMode);
        auto so = offsets.getAlgOffset(sMode);
        double angle = sD.state[so];
        if (opFlags[at_angle_limits]) {
            if (inputs[genModelPmechInLocation] > 0) {
                double pmax = -realPowerCompute(inputs[genModelEftInLocation],
                                                inputs[voltageInLocation],
                                                cos(maxAngle),
                                                sin(maxAngle));
                roots[ro] = inputs[genModelPmechInLocation] + 0.0001 - pmax;
            } else {
                double pmin = -realPowerCompute(inputs[genModelEftInLocation],
                                                inputs[voltageInLocation],
                                                cos(minAngle),
                                                sin(minAngle));
                roots[ro] = pmin - inputs[genModelPmechInLocation] + 0.0001;
            }
        } else {
            roots[ro] = std::min(angle - minAngle, maxAngle - angle);
        }
    }
}

void GenModelInverter::rootTrigger(coreTime /*time*/,
                                   const IOdata& inputs,
                                   const std::vector<int>& rootMask,
                                   const solverMode& sMode)
{
    if (rootSize(sMode) > 0) {
        auto ro = offsets.getRootOffset(sMode);
        if (rootMask[ro] > 0) {
            if (opFlags[at_angle_limits]) {
                opFlags.reset(at_angle_limits);
                LOG_DEBUG("reset angle limit");
                algebraicUpdate(inputs, emptyStateData, m_state.data(), sMode, 1.0);
            } else {
                opFlags.set(at_angle_limits);
                LOG_DEBUG("angle at limits");
                if (inputs[genModelPmechInLocation] > 0) {
                    m_state[0] = maxAngle;
                } else {
                    m_state[0] = minAngle;
                }
            }
        }
    }
}

change_code GenModelInverter::rootCheck(const IOdata& inputs,
                                        const stateData& sD,
                                        const solverMode& sMode,
                                        check_level_t /*level*/)
{
    if (rootSize(sMode) > 0) {
        auto Loc = offsets.getLocations(sD, sMode, this);
        double angle = Loc.algStateLoc[0];
        if (opFlags[at_angle_limits]) {
            if (inputs[genModelPmechInLocation] > 0) {
                double pmax = -realPowerCompute(inputs[genModelEftInLocation],
                                                inputs[voltageInLocation],
                                                cos(maxAngle),
                                                sin(maxAngle));
                if (inputs[genModelPmechInLocation] - pmax < -0.0001) {
                    opFlags.reset(at_angle_limits);
                    LOG_DEBUG("reset angle limit-from root check");
                    algebraicUpdate(inputs, emptyStateData, m_state.data(), sMode, 1.0);
                    return change_code::jacobian_change;
                }
            } else {
                double pmin = -realPowerCompute(inputs[genModelEftInLocation],
                                                inputs[voltageInLocation],
                                                cos(minAngle),
                                                sin(minAngle));
                if (pmin - inputs[genModelPmechInLocation] < -0.0001) {
                    opFlags.reset(at_angle_limits);
                    LOG_DEBUG("reset angle limit- from root check");
                    algebraicUpdate(inputs, emptyStateData, m_state.data(), sMode, 1.0);
                    return change_code::jacobian_change;
                }
            }
        } else {
            auto remAngle = std::min(angle - minAngle, maxAngle - angle);
            if (remAngle < 0.0000001) {
                opFlags.set(at_angle_limits);
                LOG_DEBUG("angle at limit from check");
                if (inputs[genModelPmechInLocation] > 0) {
                    m_state[0] = maxAngle;
                } else {
                    m_state[0] = minAngle;
                }
                return change_code::jacobian_change;
            }
        }
    }
    return change_code::no_change;
}

}  // namespace griddyn::genmodels
