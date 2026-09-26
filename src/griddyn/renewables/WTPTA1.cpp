/*
 * Copyright (c) 2014-2026, Lawrence Livermore National Security
 * See the top-level NOTICE for additional details. All rights reserved.
 * SPDX-License-Identifier: BSD-3-Clause
 */
#include "WTPTA1.h"
#include "core/CoreExceptions.h"
#include "core/CoreObjectTemplates.hpp"
#include "gmlc/utilities/stringOps.h"
#include "utilities/MatrixData.hpp"
#include <algorithm>
#include <cmath>
#include <string>

namespace griddyn {
namespace {
    constexpr std::array<RenewablePort, 5> inputs{{
        {.signal = RenewableSignal::turbineSpeed, .ioIndex = 0},
        {.signal = RenewableSignal::orderedPower, .ioIndex = 1, .base = RenewableBase::machine},
        {.signal = RenewableSignal::activeReference, .ioIndex = 2, .base = RenewableBase::machine},
        {.signal = RenewableSignal::speedReference,
         .ioIndex = 3,
         .base = RenewableBase::none,
         .required = false},
        {.signal = RenewableSignal::initialPitchAngle, .ioIndex = 4},
    }};
    constexpr std::array<RenewablePort, 1> outputs{{
        {.signal = RenewableSignal::pitchAngle, .ioIndex = 0},
    }};
    constexpr index_t theta = 0, speedIntegral = 1, powerIntegral = 2;
} // namespace

WTPTA1::WTPTA1(const std::string& name):RenewableComponent(name)
{
    m_inputSize=5;m_outputSize=1;
}

CoreObject* WTPTA1::clone(CoreObject* obj) const
{
    auto* out=cloneBase<WTPTA1,RenewableComponent>(this,obj);
    if (out!=nullptr) {
        out->Kiw=Kiw;out->Kpw=Kpw;out->Kic=Kic;out->Kpc=Kpc;
        out->Kcc=Kcc;out->Tp=Tp;out->thetaMax=thetaMax;out->thetaMin=thetaMin;
        out->rateMax=rateMax;out->rateMin=rateMin;out->initialSpeed=initialSpeed;
    }
    return out;
}

std::span<const RenewablePort> WTPTA1::inputPorts() const {return inputs;}
std::span<const RenewablePort> WTPTA1::outputPorts() const {return outputs;}

void WTPTA1::set(std::string_view param,double val,units::unit unitType)
{
    const auto key=gmlc::utilities::convertToLowerCase(std::string{param});
    if (key == "kiw") {
        Kiw = val;
    } else if (key == "kpw") {
        Kpw = val;
    } else if (key == "kic") {
        Kic = val;
    } else if (key == "kpc") {
        Kpc = val;
    } else if (key == "kcc") {
        Kcc = val;
    } else if (key == "tp") {
        Tp = val;
    } else if (key == "tetamax") {
        thetaMax = val;
    } else if (key == "tetamin") {
        thetaMin = val;
    } else if (key == "rtetamax") {
        rateMax = val;
    } else if (key == "rtetamin") {
        rateMin = val;
    } else {
        RenewableComponent::set(param, val, unitType);
    }
}

double WTPTA1::get(std::string_view param,units::unit unitType) const
{
    const auto key=gmlc::utilities::convertToLowerCase(std::string{param});
    if (key == "kiw") {
        return Kiw;
    }
    if (key == "kpw") {
        return Kpw;
    }
    if (key == "kic") {
        return Kic;
    }
    if (key == "kpc") {
        return Kpc;
    }
    if (key == "kcc") {
        return Kcc;
    }
    if (key == "tp") {
        return Tp;
    }
    if (key == "tetamax") {
        return thetaMax;
    }
    if (key == "tetamin") {
        return thetaMin;
    }
    if (key == "rtetamax") {
        return rateMax;
    }
    if (key == "rtetamin") {
        return rateMin;
    }
    return RenewableComponent::get(param,unitType);
}

void WTPTA1::dynObjectInitializeA(CoreTime time0, std::uint32_t /*flags*/)
{
    const std::array<double,10> inputs{Kiw,Kpw,Kic,Kpc,Kcc,Tp,thetaMax,
                                       thetaMin,rateMax,rateMin};
    if (std::any_of(inputs.begin(),inputs.end(),[](double stateValue){return !std::isfinite(stateValue);}) ||
        Kiw<0 || Kpw<0 || Kic<0 || Kpc<0 || Tp<=0 || thetaMax<=thetaMin ||
        rateMax<=0 || rateMin>=0) {
        throw InvalidParameterValue("WTPTA1 invalid pitch controller parameters");
    }
    auto& local=offsets.local().local;
    local.diffSize=3;local.jacSize=24;
    prevTime=time0;
}

void WTPTA1::dynObjectInitializeB(const IOdata& inputs,
                                  const IOdata& /*desiredOutput*/,
                                  IOdata& fieldSet)
{
    if (inputs.size()<5 || !std::isfinite(inputs[0]) || !std::isfinite(inputs[4]) ||
        inputs[4]<thetaMin || inputs[4]>thetaMax) {
        throw InvalidParameterValue("WTPTA1 initial speed or pitch outside limits");
    }
    initialSpeed=inputs[0];
    m_state[theta]=inputs[4];
    m_state[speedIntegral]=inputs[4];
    m_state[powerIntegral]=0.0;
    fieldSet={inputs[4]};
}

std::array<double,3> WTPTA1::rates(const IOdata& inputs,const double state[]) const
{
    const double powerError=inputs[1]-inputs[2];
    const double speedRef=inputs.size()>3 && inputs[3]!=kNullVal?inputs[3]:initialSpeed;
    const double speedError = (Kcc * powerError) + inputs[0] - speedRef;
    const double speedCommand =
        std::clamp((Kpw * speedError) + state[speedIntegral], thetaMin, thetaMax);
    const double powerCommand =
        std::clamp((Kpc * powerError) + state[powerIntegral], thetaMin, thetaMax);
    const double target=std::clamp(speedCommand+powerCommand,thetaMin,thetaMax);
    const double rawRate=(target-state[theta])/Tp;
    const double dtheta=std::clamp(rawRate,rateMin,rateMax);
    const bool speedWindup=(speedCommand>=thetaMax && speedError>0) ||
        (speedCommand<=thetaMin && speedError<0);
    const bool powerWindup=(powerCommand>=thetaMax && powerError>0) ||
        (powerCommand<=thetaMin && powerError<0);
    return {dtheta,speedWindup?0.0:Kiw*speedError,
            powerWindup?0.0:Kic*powerError};
}

void WTPTA1::derivative(const IOdata& inputs,const StateData& stateData,
                        double deriv[],const SolverMode& sMode)
{
    if (!hasDifferential(sMode)) {
        return;
    }
    const auto loc=offsets.getLocations(stateData,deriv,sMode,this);
    const auto result=rates(inputs,loc.diffStateLoc);
    for (index_t index = 0; index < 3; ++index) {
        loc.destDiffLoc[index] = result[index];
    }
}

void WTPTA1::residual(const IOdata& inputs,const StateData& stateData,
                      double resid[],const SolverMode& sMode)
{
    if (!hasDifferential(sMode)) {
        return;
    }
    const auto loc=offsets.getLocations(stateData,resid,sMode,this);
    derivative(inputs,stateData,resid,sMode);
    for (index_t index = 0; index < 3; ++index) {
        loc.destDiffLoc[index] -= loc.dstateLoc[index];
    }
}

void WTPTA1::jacobianElements(const IOdata& inputs,const StateData& stateData,
                              MatrixData<double>& matrixData,const IOlocs& inputLocs,
                              const SolverMode& sMode)
{
    if (!hasDifferential(sMode)) {
        return;
    }
    const auto loc=offsets.getLocations(stateData,sMode,this);
    constexpr double step=1e-6;
    for (index_t column=0;column<3;++column) {
        std::array<double,3> plus{loc.diffStateLoc[0],loc.diffStateLoc[1],
                                  loc.diffStateLoc[2]};
        const auto base=rates(inputs,plus.data());
        plus[column]+=step;
        const auto upper=rates(inputs,plus.data());
        for (index_t index = 0; index < 3; ++index) {
            matrixData.assign(loc.diffOffset + index,
                              loc.diffOffset + column,
                              ((upper[index] - base[index]) / step) - (index == column ? stateData.cj : 0.0));
        }
    }
    for (index_t column=0;column<4 && column<inputLocs.size();++column) {
        if (inputLocs[column] == kNullLocation || (column == 3 && inputs[column] == kNullVal)) {
            continue;
        }
        auto plus=inputs;
        plus[column]+=step;
        const auto upper = rates(plus, loc.diffStateLoc);
        const auto base = rates(inputs, loc.diffStateLoc);
        for (index_t index = 0; index < 3; ++index) {
            matrixData.assign(loc.diffOffset + index, inputLocs[column], (upper[index] - base[index]) / step);
        }
    }
}

void WTPTA1::timestep(CoreTime time, const IOdata& inputs, const SolverMode& /*sMode*/)
{
    const double deltaTime=time-prevTime;
    if (deltaTime < 0) {
        throw InvalidParameterValue("WTPTA1 timestep precedes current time");
    }
    const auto result=rates(inputs,m_state.data());
    for (index_t index = 0; index < 3; ++index) {
        m_state[index] += deltaTime * result[index];
    }
    m_state[theta]=std::clamp(m_state[theta],thetaMin,thetaMax);
    prevTime=time;
}

stringVec WTPTA1::localStateNames() const
{
    return {"theta","speedIntegral","powerIntegral"};
}

} // namespace griddyn
