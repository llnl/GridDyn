/*
 * Copyright (c) 2014-2026, Lawrence Livermore National Security
 * See the top-level NOTICE for additional details. All rights reserved.
 * SPDX-License-Identifier: BSD-3-Clause
 */
#include "WTDTA1.h"
#include "core/CoreExceptions.h"
#include "core/CoreObjectTemplates.hpp"
#include "gmlc/utilities/stringOps.h"
#include "utilities/MatrixData.hpp"
#include <algorithm>
#include <cmath>

namespace griddyn {
namespace {
constexpr std::array<RenewablePort,3> inputs{{
    {RenewableSignal::electricalPower,0,RenewableBase::machine},
    {RenewableSignal::mechanicalPower,1,RenewableBase::machine,false},
    {RenewableSignal::speedReference,2,RenewableBase::none,false},
}};
constexpr std::array<RenewablePort,2> outputs{{
    {RenewableSignal::generatorSpeed,0},
    {RenewableSignal::turbineSpeed,1},
}};
constexpr index_t wg=0,wt=1,shaft=2;
} // namespace

WTDTA1::WTDTA1(const std::string& name):RenewableComponent(name)
{
    m_inputSize=3;
    m_outputSize=2;
}

CoreObject* WTDTA1::clone(CoreObject* obj) const
{
    auto* out=cloneBase<WTDTA1,RenewableComponent>(this,obj);
    if (out!=nullptr) {
        out->H=H; out->DAMP=DAMP; out->Htfrac=Htfrac;
        out->Freq1=Freq1; out->Dshaft=Dshaft; out->w0=w0;
        out->initialPower=initialPower;out->operatingSpeed=operatingSpeed;
    }
    return out;
}

std::span<const RenewablePort> WTDTA1::inputPorts() const {return inputs;}
std::span<const RenewablePort> WTDTA1::outputPorts() const {return outputs;}

void WTDTA1::set(std::string_view param,double val,units::unit unitType)
{
    const auto key=gmlc::utilities::convertToLowerCase(std::string{param});
    if (key=="h") H=val;
    else if (key=="damp") DAMP=val;
    else if (key=="htfrac") Htfrac=val;
    else if (key=="freq1") Freq1=val;
    else if (key=="dshaft") Dshaft=val;
    else if (key=="w0") w0=val;
    else RenewableComponent::set(param,val,unitType);
}

double WTDTA1::get(std::string_view param,units::unit unitType) const
{
    const auto key=gmlc::utilities::convertToLowerCase(std::string{param});
    if (key=="h") return H;
    if (key=="damp") return DAMP;
    if (key=="htfrac") return Htfrac;
    if (key=="freq1") return Freq1;
    if (key=="dshaft") return Dshaft;
    if (key=="w0") return w0;
    return RenewableComponent::get(param,unitType);
}

void WTDTA1::dynObjectInitializeA(CoreTime time0,std::uint32_t)
{
    if (!std::isfinite(H) || !std::isfinite(DAMP) || !std::isfinite(Htfrac) ||
        !std::isfinite(Freq1) || !std::isfinite(Dshaft) || !std::isfinite(w0) ||
        H<=0 || Htfrac<=0 || Htfrac>=1 || Freq1<=0 || Dshaft<0 ||
        DAMP<0 || w0<=0) {
        throw InvalidParameterValue("WTDTA1 requires positive two-mass inertia and speed");
    }
    auto& local=offsets.local().local;
    local.diffSize=3;
    local.jacSize=24;
    prevTime=time0;
}

void WTDTA1::dynObjectInitializeB(const IOdata& values,const IOdata&,
                                   IOdata& fieldSet)
{
    if (values.empty() || !std::isfinite(values[0])) {
        throw InvalidParameterValue("WTDTA1 requires initial electrical power");
    }
    initialPower=values[0];
    operatingSpeed=values.size()>2 && values[2]!=kNullVal?values[2]:w0;
    if (!std::isfinite(operatingSpeed) || operatingSpeed<=0)
        throw InvalidParameterValue("WTDTA1 initial speed reference must be positive");
    m_state[wg]=operatingSpeed;
    m_state[wt]=operatingSpeed;
    m_state[shaft]=initialPower/operatingSpeed;
    fieldSet={operatingSpeed,operatingSpeed};
}

std::array<double,3> WTDTA1::rates(const IOdata& values,const double state[]) const
{
    const double pe=values[0];
    const double pm=values.size()>1 && values[1]!=kNullVal?values[1]:initialPower;
    const double ht2=2*Htfrac*H,hg2=2*(1-Htfrac)*H;
    const double delta=state[wt]-state[wg];
    const double pd=Dshaft*delta;
    const double stiffness=ht2*hg2*0.5*Freq1*Freq1/H;
    return {(-pe/std::max(state[wg],0.01)+state[shaft]-
             DAMP*(state[wg]-operatingSpeed)+pd)/hg2,
            (pm/std::max(state[wt],0.01)-state[shaft]-pd)/ht2,
            stiffness*delta};
}

void WTDTA1::derivative(const IOdata& values,const StateData& stateData,
                        double deriv[],const SolverMode& sMode)
{
    if (!hasDifferential(sMode)) return;
    const auto loc=offsets.getLocations(stateData,deriv,sMode,this);
    const auto result=rates(values,loc.diffStateLoc);
    for (index_t i=0;i<3;++i) loc.destDiffLoc[i]=result[i];
}

void WTDTA1::residual(const IOdata& values,const StateData& stateData,
                      double resid[],const SolverMode& sMode)
{
    if (!hasDifferential(sMode)) return;
    const auto loc=offsets.getLocations(stateData,resid,sMode,this);
    derivative(values,stateData,resid,sMode);
    for (index_t i=0;i<3;++i) loc.destDiffLoc[i]-=loc.dstateLoc[i];
}

void WTDTA1::jacobianElements(const IOdata& values,const StateData& stateData,
                              MatrixData<double>& matrixData,const IOlocs& inputLocs,
                              const SolverMode& sMode)
{
    if (!hasDifferential(sMode)) return;
    const auto loc=offsets.getLocations(stateData,sMode,this);
    constexpr double step=1e-6;
    const auto diff=loc.diffOffset;
    for (index_t j=0;j<3;++j) {
        std::array<double,3> plus{loc.diffStateLoc[0],loc.diffStateLoc[1],
                                  loc.diffStateLoc[2]};
        auto minus=plus;
        plus[j]+=step; minus[j]-=step;
        const auto upper=rates(values,plus.data()),lower=rates(values,minus.data());
        for (index_t i=0;i<3;++i)
            matrixData.assign(diff+i,diff+j,(upper[i]-lower[i])/(2*step)-
                              (i==j?stateData.cj:0.0));
    }
    for (index_t j=0;j<2 && j<inputLocs.size();++j) {
        if (inputLocs[j]==kNullLocation || (j==1 && values[j]==kNullVal)) continue;
        auto plus=values,minus=values;
        plus[j]+=step;minus[j]-=step;
        const auto upper=rates(plus,loc.diffStateLoc);
        const auto lower=rates(minus,loc.diffStateLoc);
        for (index_t i=0;i<3;++i)
            matrixData.assign(diff+i,inputLocs[j],(upper[i]-lower[i])/(2*step));
    }
}

void WTDTA1::timestep(CoreTime time,const IOdata& values,const SolverMode&)
{
    const double dt=time-prevTime;
    if (dt<0) throw InvalidParameterValue("WTDTA1 timestep precedes current time");
    const auto rate=rates(values,m_state.data());
    for (index_t i=0;i<3;++i) m_state[i]+=dt*rate[i];
    prevTime=time;
}

stringVec WTDTA1::localStateNames() const {return {"wg","wt","Tshaft"};}

} // namespace griddyn
