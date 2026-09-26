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

namespace griddyn {
namespace {
constexpr std::array<RenewablePort,5> inputs{{
    {RenewableSignal::turbineSpeed,0},
    {RenewableSignal::orderedPower,1,RenewableBase::machine},
    {RenewableSignal::activeReference,2,RenewableBase::machine},
    {RenewableSignal::speedReference,3,RenewableBase::none,false},
    {RenewableSignal::initialPitchAngle,4},
}};
constexpr std::array<RenewablePort,1> outputs{{
    {RenewableSignal::pitchAngle,0},
}};
constexpr index_t theta=0,speedIntegral=1,powerIntegral=2;
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
    if (key=="kiw") Kiw=val;
    else if (key=="kpw") Kpw=val;
    else if (key=="kic") Kic=val;
    else if (key=="kpc") Kpc=val;
    else if (key=="kcc") Kcc=val;
    else if (key=="tp") Tp=val;
    else if (key=="tetamax") thetaMax=val;
    else if (key=="tetamin") thetaMin=val;
    else if (key=="rtetamax") rateMax=val;
    else if (key=="rtetamin") rateMin=val;
    else RenewableComponent::set(param,val,unitType);
}

double WTPTA1::get(std::string_view param,units::unit unitType) const
{
    const auto key=gmlc::utilities::convertToLowerCase(std::string{param});
    if (key=="kiw") return Kiw;
    if (key=="kpw") return Kpw;
    if (key=="kic") return Kic;
    if (key=="kpc") return Kpc;
    if (key=="kcc") return Kcc;
    if (key=="tp") return Tp;
    if (key=="tetamax") return thetaMax;
    if (key=="tetamin") return thetaMin;
    if (key=="rtetamax") return rateMax;
    if (key=="rtetamin") return rateMin;
    return RenewableComponent::get(param,unitType);
}

void WTPTA1::dynObjectInitializeA(CoreTime time0,std::uint32_t)
{
    const std::array<double,10> values{Kiw,Kpw,Kic,Kpc,Kcc,Tp,thetaMax,
                                       thetaMin,rateMax,rateMin};
    if (std::any_of(values.begin(),values.end(),[](double x){return !std::isfinite(x);}) ||
        Kiw<0 || Kpw<0 || Kic<0 || Kpc<0 || Tp<=0 || thetaMax<=thetaMin ||
        rateMax<=0 || rateMin>=0) {
        throw InvalidParameterValue("WTPTA1 invalid pitch controller parameters");
    }
    auto& local=offsets.local().local;
    local.diffSize=3;local.jacSize=24;
    prevTime=time0;
}

void WTPTA1::dynObjectInitializeB(const IOdata& values,const IOdata&,
                                   IOdata& fieldSet)
{
    if (values.size()<5 || !std::isfinite(values[0]) || !std::isfinite(values[4]) ||
        values[4]<thetaMin || values[4]>thetaMax) {
        throw InvalidParameterValue("WTPTA1 initial speed or pitch outside limits");
    }
    initialSpeed=values[0];
    m_state[theta]=values[4];
    m_state[speedIntegral]=values[4];
    m_state[powerIntegral]=0.0;
    fieldSet={values[4]};
}

std::array<double,3> WTPTA1::rates(const IOdata& values,const double state[]) const
{
    const double powerError=values[1]-values[2];
    const double speedRef=values.size()>3 && values[3]!=kNullVal?values[3]:initialSpeed;
    const double speedError=Kcc*powerError+values[0]-speedRef;
    const double speedCommand=std::clamp(Kpw*speedError+state[speedIntegral],
                                         thetaMin,thetaMax);
    const double powerCommand=std::clamp(Kpc*powerError+state[powerIntegral],
                                         thetaMin,thetaMax);
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

void WTPTA1::derivative(const IOdata& values,const StateData& stateData,
                        double deriv[],const SolverMode& sMode)
{
    if (!hasDifferential(sMode)) return;
    const auto loc=offsets.getLocations(stateData,deriv,sMode,this);
    const auto result=rates(values,loc.diffStateLoc);
    for (index_t i=0;i<3;++i) loc.destDiffLoc[i]=result[i];
}

void WTPTA1::residual(const IOdata& values,const StateData& stateData,
                      double resid[],const SolverMode& sMode)
{
    if (!hasDifferential(sMode)) return;
    const auto loc=offsets.getLocations(stateData,resid,sMode,this);
    derivative(values,stateData,resid,sMode);
    for (index_t i=0;i<3;++i) loc.destDiffLoc[i]-=loc.dstateLoc[i];
}

void WTPTA1::jacobianElements(const IOdata& values,const StateData& stateData,
                              MatrixData<double>& matrixData,const IOlocs& inputLocs,
                              const SolverMode& sMode)
{
    if (!hasDifferential(sMode)) return;
    const auto loc=offsets.getLocations(stateData,sMode,this);
    constexpr double step=1e-6;
    for (index_t j=0;j<3;++j) {
        std::array<double,3> plus{loc.diffStateLoc[0],loc.diffStateLoc[1],
                                  loc.diffStateLoc[2]};
        const auto base=rates(values,plus.data());
        plus[j]+=step;
        const auto upper=rates(values,plus.data());
        for (index_t i=0;i<3;++i)
            matrixData.assign(loc.diffOffset+i,loc.diffOffset+j,
                (upper[i]-base[i])/step-(i==j?stateData.cj:0.0));
    }
    for (index_t j=0;j<4 && j<inputLocs.size();++j) {
        if (inputLocs[j]==kNullLocation || (j==3 && values[j]==kNullVal)) continue;
        auto plus=values;
        plus[j]+=step;
        const auto upper=rates(plus,loc.diffStateLoc),base=rates(values,loc.diffStateLoc);
        for (index_t i=0;i<3;++i)
            matrixData.assign(loc.diffOffset+i,inputLocs[j],
                (upper[i]-base[i])/step);
    }
}

void WTPTA1::timestep(CoreTime time,const IOdata& values,const SolverMode&)
{
    const double dt=time-prevTime;
    if (dt<0) throw InvalidParameterValue("WTPTA1 timestep precedes current time");
    const auto result=rates(values,m_state.data());
    for (index_t i=0;i<3;++i) m_state[i]+=dt*result[i];
    m_state[theta]=std::clamp(m_state[theta],thetaMin,thetaMax);
    prevTime=time;
}

stringVec WTPTA1::localStateNames() const
{
    return {"theta","speedIntegral","powerIntegral"};
}

} // namespace griddyn
