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

namespace griddyn {
namespace {
constexpr std::array<RenewablePort,2> inputs{{
    {RenewableSignal::electricalPower,0,RenewableBase::machine},
    {RenewableSignal::generatorSpeed,1},
}};
constexpr std::array<RenewablePort,2> outputs{{
    {RenewableSignal::activeReference,0,RenewableBase::machine},
    {RenewableSignal::speedReference,1},
}};
constexpr index_t pref=0,pef=0,wref=1,integral=2;
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
        if (val!=0.0 && val!=1.0)
            throw InvalidParameterValue("WTTQA1 Tflag must be zero or one");
        Tflag=static_cast<int>(val);
    }
    else if (key=="kpp") Kpp=val;
    else if (key=="kip") Kip=val;
    else if (key=="tp") Tp=val;
    else if (key=="twref") Twref=val;
    else if (key=="temax") Temax=val;
    else if (key=="temin") Temin=val;
    else if (key=="trate") TRATE=val;
    else if (key.size()==2 && key[1]>='1' && key[1]<='4' && key[0]=='p')
        power[key[1]-'1']=val;
    else if (key.size()==4 && key.starts_with("spd") && key[3]>='1' && key[3]<='4')
        speed[key[3]-'1']=val;
    else RenewableComponent::set(param,val,unitType);
}

double WTTQA1::get(std::string_view param,units::unit unitType) const
{
    const auto key=gmlc::utilities::convertToLowerCase(std::string{param});
    if (key=="tflag") return Tflag;
    if (key=="kpp") return Kpp;
    if (key=="kip") return Kip;
    if (key=="tp") return Tp;
    if (key=="twref") return Twref;
    if (key=="temax") return Temax;
    if (key=="temin") return Temin;
    if (key=="trate") return TRATE;
    if (key.size()==2 && key[1]>='1' && key[1]<='4' && key[0]=='p')
        return power[key[1]-'1'];
    if (key.size()==4 && key.starts_with("spd") && key[3]>='1' && key[3]<='4')
        return speed[key[3]-'1'];
    return RenewableComponent::get(param,unitType);
}

void WTTQA1::dynObjectInitializeA(CoreTime time0,std::uint32_t)
{
    if ((Tflag!=0 && Tflag!=1) || !std::isfinite(Kpp) || !std::isfinite(Kip) ||
        !std::isfinite(Tp) || !std::isfinite(Twref) || !std::isfinite(Temax) ||
        !std::isfinite(Temin) || !std::isfinite(TRATE) || Kpp<0 || Kip<0 ||
        Tp<=0 || Twref<=0 || Temax<=Temin || TRATE<=0) {
        throw InvalidParameterValue("WTTQA1 invalid torque controller parameters");
    }
    for (index_t i=0;i<4;++i) {
        if (!std::isfinite(power[i]) || !std::isfinite(speed[i]) || speed[i]<=0 ||
            (i>0 && (power[i]<=power[i-1] || speed[i]<speed[i-1]))) {
            throw InvalidParameterValue("WTTQA1 power-speed curve must increase");
        }
    }
    auto& local=offsets.local().local;
    local.algSize=1;
    local.diffSize=3;
    local.jacSize=28;
    prevTime=time0;
}

double WTTQA1::curve(double pe) const
{
    if (pe<=power[0]) return speed[0];
    for (index_t i=1;i<4;++i)
        if (pe<=power[i])
            return speed[i-1]+(pe-power[i-1])*(speed[i]-speed[i-1])/
                (power[i]-power[i-1]);
    return speed[3];
}

double WTTQA1::error(const IOdata& values,const double state[]) const
{
    return Tflag==1 ? (values[0]-initialPower)/std::max(values[1],0.01) :
        state[wref]-values[1];
}

double WTTQA1::reference(const IOdata& values,const double state[]) const
{
    const double torque=std::clamp(Kpp*error(values,state)+state[integral],
                                   Temin,Temax);
    return torque*values[1];
}

std::array<double,3> WTTQA1::rates(const IOdata& values,const double state[]) const
{
    const double err=error(values,state);
    const double raw=Kpp*err+state[integral];
    const bool windup=(raw>=Temax && err>0)||(raw<=Temin && err<0);
    return {(values[0]-state[pef])/Tp,
            (curve(state[pef])-state[wref])/Twref,
            windup?0.0:std::clamp(Kip*err,-TRATE,TRATE)};
}

void WTTQA1::dynObjectInitializeB(const IOdata& values,const IOdata&,
                                   IOdata& fieldSet)
{
    if (values.size()<2 || !std::isfinite(values[0]) ||
        !std::isfinite(values[1]) || values[1]<=0) {
        throw InvalidParameterValue("WTTQA1 initial Pe and generator speed");
    }
    initialPower=values[0];
    m_state[1+pef]=values[0];
    m_state[1+wref]=curve(values[0]);
    const double err=error(values,m_state.data()+1);
    m_state[1+integral]=values[0]/values[1]-Kpp*err;
    if (values[0]/values[1]<Temin || values[0]/values[1]>Temax) {
        throw InvalidParameterValue("WTTQA1 initial torque exceeds limits");
    }
    m_state[pref]=values[0];
    fieldSet={values[0],m_state[1+wref]};
}

void WTTQA1::derivative(const IOdata& values,const StateData& stateData,
                        double deriv[],const SolverMode& sMode)
{
    if (!hasDifferential(sMode)) return;
    const auto loc=offsets.getLocations(stateData,deriv,sMode,this);
    const auto result=rates(values,loc.diffStateLoc);
    for (index_t i=0;i<3;++i) loc.destDiffLoc[i]=result[i];
}

void WTTQA1::residual(const IOdata& values,const StateData& stateData,
                      double resid[],const SolverMode& sMode)
{
    const auto loc=offsets.getLocations(stateData,resid,sMode,this);
    if (hasAlgebraic(sMode))
        loc.destLoc[pref]=reference(values,loc.diffStateLoc)-loc.algStateLoc[pref];
    if (hasDifferential(sMode)) {
        derivative(values,stateData,resid,sMode);
        for (index_t i=0;i<3;++i) loc.destDiffLoc[i]-=loc.dstateLoc[i];
    }
}

void WTTQA1::algebraicUpdate(const IOdata& values,const StateData& stateData,
                             double update[],const SolverMode& sMode,double)
{
    if (!hasAlgebraic(sMode)) return;
    const auto loc=offsets.getLocations(stateData,update,sMode,this);
    loc.destLoc[pref]=reference(values,loc.diffStateLoc);
}

void WTTQA1::jacobianElements(const IOdata& values,const StateData& stateData,
                              MatrixData<double>& matrixData,const IOlocs& inputLocs,
                              const SolverMode& sMode)
{
    const auto loc=offsets.getLocations(stateData,sMode,this);
    constexpr double step=1e-6;
    if (hasAlgebraic(sMode)) matrixData.assign(loc.algOffset,loc.algOffset,-1.0);
    if (hasDifferential(sMode)) {
        for (index_t j=0;j<3;++j) {
            std::array<double,3> plus{loc.diffStateLoc[0],loc.diffStateLoc[1],
                                      loc.diffStateLoc[2]};
            const auto baseReference=reference(values,plus.data());
            const auto baseRates=rates(values,plus.data());
            plus[j]+=step;
            if (hasAlgebraic(sMode))
                matrixData.assign(loc.algOffset,loc.diffOffset+j,
                    (reference(values,plus.data())-baseReference)/step);
            const auto upper=rates(values,plus.data());
            for (index_t i=0;i<3;++i)
                matrixData.assign(loc.diffOffset+i,loc.diffOffset+j,
                    (upper[i]-baseRates[i])/step-(i==j?stateData.cj:0.0));
        }
    }
    for (index_t j=0;j<2 && j<inputLocs.size();++j) {
        if (inputLocs[j]==kNullLocation) continue;
        auto plus=values;
        plus[j]+=step;
        if (hasAlgebraic(sMode))
            matrixData.assign(loc.algOffset,inputLocs[j],
                (reference(plus,loc.diffStateLoc)-reference(values,loc.diffStateLoc))/step);
        if (hasDifferential(sMode)) {
            const auto upper=rates(plus,loc.diffStateLoc),base=rates(values,loc.diffStateLoc);
            for (index_t i=0;i<3;++i)
                matrixData.assign(loc.diffOffset+i,inputLocs[j],
                    (upper[i]-base[i])/step);
        }
    }
}

void WTTQA1::timestep(CoreTime time,const IOdata& values,const SolverMode&)
{
    const double dt=time-prevTime;
    if (dt<0) throw InvalidParameterValue("WTTQA1 timestep precedes current time");
    const auto result=rates(values,m_state.data()+1);
    for (index_t i=0;i<3;++i) m_state[1+i]+=dt*result[i];
    m_state[pref]=reference(values,m_state.data()+1);
    prevTime=time;
}

double WTTQA1::getOutput(const IOdata& values,const StateData& stateData,
                         const SolverMode& sMode,index_t outputNum) const
{
    if (outputNum==1) return offsets.getLocations(stateData,sMode,this).diffStateLoc[wref];
    return RenewableComponent::getOutput(values,stateData,sMode,outputNum);
}

index_t WTTQA1::getOutputLoc(const SolverMode& sMode,index_t outputNum) const
{
    if (outputNum==1) return offsets.getDiffOffset(sMode)+wref;
    return RenewableComponent::getOutputLoc(sMode,outputNum);
}

stringVec WTTQA1::localStateNames() const
{
    return {"Pref","PeFiltered","speedReference","torqueIntegral"};
}

} // namespace griddyn
