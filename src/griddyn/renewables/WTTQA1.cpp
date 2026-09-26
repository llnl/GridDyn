/*
 * Copyright (c) 2014-2026, Lawrence Livermore National Security
 * See the top-level NOTICE for additional details. All rights reserved.
 * SPDX-License-Identifier: BSD-3-Clause
 */
#include "WTTQA1.h"
#include "core/CoreExceptions.h"
#include "core/CoreObjectTemplates.hpp"
#include "gmlc/utilities/stringOps.h"
#include "utilities/MatrixData.hpp"
#include <algorithm>
#include <cmath>
#include <string>

namespace griddyn {
namespace {
    constexpr std::array<RenewablePort, 2> inputs{{
        {.signal = RenewableSignal::electricalPower, .ioIndex = 0, .base = RenewableBase::machine},
        {.signal = RenewableSignal::generatorSpeed, .ioIndex = 1},
    }};
    constexpr std::array<RenewablePort, 2> outputs{{
        {.signal = RenewableSignal::activeReference, .ioIndex = 0, .base = RenewableBase::machine},
        {.signal = RenewableSignal::speedReference, .ioIndex = 1},
    }};
    constexpr index_t pref = 0, pef = 0, wref = 1, integral = 2;
} // namespace

WTTQA1::WTTQA1(const std::string& name):RenewableComponent(name)
{
    m_inputSize=2;
    m_outputSize=2;
}

CoreObject* WTTQA1::clone(CoreObject* obj) const
{
    auto* out=cloneBase<WTTQA1,RenewableComponent>(this,obj);
    if (out!=nullptr) {
        out->Tflag=Tflag;out->Kpp=Kpp;out->Kip=Kip;out->Tp=Tp;
        out->Twref=Twref;out->Temax=Temax;out->Temin=Temin;
        out->TRATE=TRATE;out->power=power;out->speed=speed;
        out->initialPower=initialPower;
    }
    return out;
}

std::span<const RenewablePort> WTTQA1::inputPorts() const {return inputs;}
std::span<const RenewablePort> WTTQA1::outputPorts() const {return outputs;}

void WTTQA1::set(std::string_view param,double val,units::unit unitType)
{
    const auto key=gmlc::utilities::convertToLowerCase(std::string{param});
    if (key=="tflag") {
        if (val != 0.0 && val != 1.0) {
            throw InvalidParameterValue("WTTQA1 Tflag must be zero or one");
        }
        Tflag=static_cast<int>(val);
    } else if (key == "kpp") {
        Kpp = val;
    } else if (key == "kip") {
        Kip = val;
    } else if (key == "tp") {
        Tp = val;
    } else if (key == "twref") {
        Twref = val;
    } else if (key == "temax") {
        Temax = val;
    } else if (key == "temin") {
        Temin = val;
    } else if (key == "trate") {
        TRATE = val;
    } else if (key.size() == 2 && key[1] >= '1' && key[1] <= '4' && key[0] == 'p') {
        power[key[1]-'1']=val;
    } else if (key.size() == 4 && key.starts_with("spd") && key[3] >= '1' && key[3] <= '4') {
        speed[key[3]-'1']=val;
    } else {
        RenewableComponent::set(param, val, unitType);
    }
}

double WTTQA1::get(std::string_view param,units::unit unitType) const
{
    const auto key=gmlc::utilities::convertToLowerCase(std::string{param});
    if (key == "tflag") {
        return Tflag;
    }
    if (key == "kpp") {
        return Kpp;
    }
    if (key == "kip") {
        return Kip;
    }
    if (key == "tp") {
        return Tp;
    }
    if (key == "twref") {
        return Twref;
    }
    if (key == "temax") {
        return Temax;
    }
    if (key == "temin") {
        return Temin;
    }
    if (key == "trate") {
        return TRATE;
    }
    if (key.size() == 2 && key[1] >= '1' && key[1] <= '4' && key[0] == 'p') {
        return power[key[1] - '1'];
    }
    if (key.size() == 4 && key.starts_with("spd") && key[3] >= '1' && key[3] <= '4') {
        return speed[key[3] - '1'];
    }
    return RenewableComponent::get(param,unitType);
}

void WTTQA1::dynObjectInitializeA(CoreTime time0, std::uint32_t /*flags*/)
{
    if ((Tflag!=0 && Tflag!=1) || !std::isfinite(Kpp) || !std::isfinite(Kip) ||
        !std::isfinite(Tp) || !std::isfinite(Twref) || !std::isfinite(Temax) ||
        !std::isfinite(Temin) || !std::isfinite(TRATE) || Kpp<0 || Kip<0 ||
        Tp<=0 || Twref<=0 || Temax<=Temin || TRATE<=0) {
        throw InvalidParameterValue("WTTQA1 invalid torque controller parameters");
    }
    for (index_t index=0;index<4;++index) {
        if (!std::isfinite(power[index]) || !std::isfinite(speed[index]) || speed[index]<=0 ||
            (index>0 && (power[index]<=power[index-1] || speed[index]<speed[index-1]))) {
            throw InvalidParameterValue("WTTQA1 power-speed curve must increase");
        }
    }
    auto& local=offsets.local().local;
    local.algSize=1;
    local.diffSize=3;
    local.jacSize=28;
    prevTime=time0;
}

double WTTQA1::curve(double electricalPower) const
{
    if (electricalPower <= power[0]) {
        return speed[0];
    }
    for (index_t index = 1; index < 4; ++index) {
        if (electricalPower <= power[index]) {
            return speed[index - 1] +
                ((electricalPower - power[index - 1]) * (speed[index] - speed[index - 1]) / (power[index] - power[index - 1]));
        }
    }
    return speed[3];
}

double WTTQA1::error(const IOdata& inputs,const double state[]) const
{
    return Tflag==1 ? (inputs[0]-initialPower)/std::max(inputs[1],0.01) :
        state[wref]-inputs[1];
}

double WTTQA1::reference(const IOdata& inputs,const double state[]) const
{
    const double torque = std::clamp((Kpp * error(inputs, state)) + state[integral], Temin, Temax);
    return torque*inputs[1];
}

std::array<double,3> WTTQA1::rates(const IOdata& inputs,const double state[]) const
{
    const double err=error(inputs,state);
    const double raw = (Kpp * err) + state[integral];
    const bool windup=(raw>=Temax && err>0)||(raw<=Temin && err<0);
    return {(inputs[0]-state[pef])/Tp,
            (curve(state[pef])-state[wref])/Twref,
            windup?0.0:std::clamp(Kip*err,-TRATE,TRATE)};
}

void WTTQA1::dynObjectInitializeB(const IOdata& inputs,
                                  const IOdata& /*desiredOutput*/,
                                  IOdata& fieldSet)
{
    if (inputs.size()<2 || !std::isfinite(inputs[0]) ||
        !std::isfinite(inputs[1]) || inputs[1]<=0) {
        throw InvalidParameterValue("WTTQA1 initial Pe and generator speed");
    }
    initialPower=inputs[0];
    m_state[1+pef]=inputs[0];
    m_state[1+wref]=curve(inputs[0]);
    const double err=error(inputs,m_state.data()+1);
    m_state[1+integral]=inputs[0]/inputs[1]-Kpp*err;
    if (inputs[0]/inputs[1]<Temin || inputs[0]/inputs[1]>Temax) {
        throw InvalidParameterValue("WTTQA1 initial torque exceeds limits");
    }
    m_state[pref]=inputs[0];
    fieldSet={inputs[0],m_state[1+wref]};
}

void WTTQA1::derivative(const IOdata& inputs,const StateData& stateData,
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

void WTTQA1::residual(const IOdata& inputs,const StateData& stateData,
                      double resid[],const SolverMode& sMode)
{
    const auto loc=offsets.getLocations(stateData,resid,sMode,this);
    if (hasAlgebraic(sMode)) {
        loc.destLoc[pref] = reference(inputs, loc.diffStateLoc) - loc.algStateLoc[pref];
    }
    if (hasDifferential(sMode)) {
        derivative(inputs,stateData,resid,sMode);
        for (index_t index = 0; index < 3; ++index) {
            loc.destDiffLoc[index] -= loc.dstateLoc[index];
        }
    }
}

void WTTQA1::algebraicUpdate(const IOdata& inputs,
                             const StateData& stateData,
                             double update[],
                             const SolverMode& sMode,
                             double /*alpha*/)
{
    if (!hasAlgebraic(sMode)) {
        return;
    }
    const auto loc=offsets.getLocations(stateData,update,sMode,this);
    loc.destLoc[pref]=reference(inputs,loc.diffStateLoc);
}

void WTTQA1::jacobianElements(const IOdata& inputs,const StateData& stateData,
                              MatrixData<double>& matrixData,const IOlocs& inputLocs,
                              const SolverMode& sMode)
{
    const auto loc=offsets.getLocations(stateData,sMode,this);
    constexpr double step=1e-6;
    if (hasAlgebraic(sMode)) {
        matrixData.assign(loc.algOffset, loc.algOffset, -1.0);
    }
    if (hasDifferential(sMode)) {
        for (index_t column=0;column<3;++column) {
            std::array<double,3> plus{loc.diffStateLoc[0],loc.diffStateLoc[1],
                                      loc.diffStateLoc[2]};
            const auto baseReference=reference(inputs,plus.data());
            const auto baseRates=rates(inputs,plus.data());
            plus[column]+=step;
            if (hasAlgebraic(sMode)) {
                matrixData.assign(loc.algOffset,
                                  loc.diffOffset + column,
                                  (reference(inputs, plus.data()) - baseReference) / step);
            }
            const auto upper=rates(inputs,plus.data());
            for (index_t index = 0; index < 3; ++index) {
                matrixData.assign(loc.diffOffset + index,
                                  loc.diffOffset + column,
                                  ((upper[index] - baseRates[index]) / step) -
                                      (index == column ? stateData.cj : 0.0));
            }
        }
    }
    for (index_t column=0;column<2 && column<inputLocs.size();++column) {
        if (inputLocs[column] == kNullLocation) {
            continue;
        }
        auto plus=inputs;
        plus[column]+=step;
        if (hasAlgebraic(sMode)) {
            matrixData.assign(loc.algOffset,
                              inputLocs[column],
                              (reference(plus, loc.diffStateLoc) -
                               reference(inputs, loc.diffStateLoc)) /
                                  step);
        }
        if (hasDifferential(sMode)) {
            const auto upper = rates(plus, loc.diffStateLoc);
            const auto base = rates(inputs, loc.diffStateLoc);
            for (index_t index = 0; index < 3; ++index) {
                matrixData.assign(loc.diffOffset + index, inputLocs[column], (upper[index] - base[index]) / step);
            }
        }
    }
}

void WTTQA1::timestep(CoreTime time, const IOdata& inputs, const SolverMode& /*sMode*/)
{
    const double deltaTime=time-prevTime;
    if (deltaTime < 0) {
        throw InvalidParameterValue("WTTQA1 timestep precedes current time");
    }
    const auto result=rates(inputs,m_state.data()+1);
    for (index_t index = 0; index < 3; ++index) {
        m_state[1 + index] += deltaTime * result[index];
    }
    m_state[pref]=reference(inputs,m_state.data()+1);
    prevTime=time;
}

double WTTQA1::getOutput(const IOdata& inputs,const StateData& stateData,
                         const SolverMode& sMode,index_t outputNum) const
{
    if (outputNum == 1) {
        return offsets.getLocations(stateData, sMode, this).diffStateLoc[wref];
    }
    return RenewableComponent::getOutput(inputs,stateData,sMode,outputNum);
}

index_t WTTQA1::getOutputLoc(const SolverMode& sMode,index_t outputNum) const
{
    if (outputNum == 1) {
        return offsets.getDiffOffset(sMode) + wref;
    }
    return RenewableComponent::getOutputLoc(sMode,outputNum);
}

stringVec WTTQA1::localStateNames() const
{
    return {"Pref","PeFiltered","speedReference","torqueIntegral"};
}

} // namespace griddyn
