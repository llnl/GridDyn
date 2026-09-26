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
#include <string>

namespace griddyn {
namespace {
    constexpr std::array<RenewablePort, 3> inputs{{
        {.signal = RenewableSignal::electricalPower, .ioIndex = 0, .base = RenewableBase::machine},
        {.signal = RenewableSignal::mechanicalPower,
         .ioIndex = 1,
         .base = RenewableBase::machine,
         .required = false},
        {.signal = RenewableSignal::speedReference,
         .ioIndex = 2,
         .base = RenewableBase::none,
         .required = false},
    }};
    constexpr std::array<RenewablePort, 2> outputs{{
        {.signal = RenewableSignal::generatorSpeed, .ioIndex = 0},
        {.signal = RenewableSignal::turbineSpeed, .ioIndex = 1},
    }};
    constexpr index_t windGenerator = 0, windTurbine = 1, shaft = 2;
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
    if (key == "h") {
        H = val;
    } else if (key == "damp") {
        DAMP = val;
    } else if (key == "htfrac") {
        Htfrac = val;
    } else if (key == "freq1") {
        Freq1 = val;
    } else if (key == "dshaft") {
        Dshaft = val;
    } else if (key == "w0") {
        w0 = val;
    } else {
        RenewableComponent::set(param, val, unitType);
    }
}

double WTDTA1::get(std::string_view param,units::unit unitType) const
{
    const auto key=gmlc::utilities::convertToLowerCase(std::string{param});
    if (key == "h") {
        return H;
    }
    if (key == "damp") {
        return DAMP;
    }
    if (key == "htfrac") {
        return Htfrac;
    }
    if (key == "freq1") {
        return Freq1;
    }
    if (key == "dshaft") {
        return Dshaft;
    }
    if (key == "w0") {
        return w0;
    }
    return RenewableComponent::get(param,unitType);
}

void WTDTA1::dynObjectInitializeA(CoreTime time0, std::uint32_t /*flags*/)
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

void WTDTA1::dynObjectInitializeB(const IOdata& inputs,
                                  const IOdata& /*desiredOutput*/,
                                  IOdata& fieldSet)
{
    if (inputs.empty() || !std::isfinite(inputs[0])) {
        throw InvalidParameterValue("WTDTA1 requires initial electrical power");
    }
    initialPower=inputs[0];
    operatingSpeed=inputs.size()>2 && inputs[2]!=kNullVal?inputs[2]:w0;
    if (!std::isfinite(operatingSpeed) || operatingSpeed <= 0) {
        throw InvalidParameterValue("WTDTA1 initial speed reference must be positive");
    }
    m_state[windGenerator]=operatingSpeed;
    m_state[windTurbine]=operatingSpeed;
    m_state[shaft]=initialPower/operatingSpeed;
    fieldSet={operatingSpeed,operatingSpeed};
}

std::array<double,3> WTDTA1::rates(const IOdata& inputs,const double state[]) const
{
    const double electricalPower=inputs[0];
    const double mechanicalPower=inputs.size()>1 && inputs[1]!=kNullVal?inputs[1]:initialPower;
    const double ht2 = 2 * Htfrac * H;
    const double hg2 = 2 * (1 - Htfrac) * H;
    const double delta=state[windTurbine]-state[windGenerator];
    const double powerDifference=Dshaft*delta;
    const double stiffness=ht2*hg2*0.5*Freq1*Freq1/H;
    return {(-electricalPower/std::max(state[windGenerator],0.01)+state[shaft]-
             DAMP*(state[windGenerator]-operatingSpeed)+powerDifference)/hg2,
            (mechanicalPower/std::max(state[windTurbine],0.01)-state[shaft]-powerDifference)/ht2,
            stiffness*delta};
}

void WTDTA1::derivative(const IOdata& inputs,const StateData& stateData,
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

void WTDTA1::residual(const IOdata& inputs,const StateData& stateData,
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

void WTDTA1::jacobianElements(const IOdata& inputs,const StateData& stateData,
                              MatrixData<double>& matrixData,const IOlocs& inputLocs,
                              const SolverMode& sMode)
{
    if (!hasDifferential(sMode)) {
        return;
    }
    const auto loc=offsets.getLocations(stateData,sMode,this);
    constexpr double step=1e-6;
    const auto diff=loc.diffOffset;
    for (index_t column=0;column<3;++column) {
        std::array<double,3> plus{loc.diffStateLoc[0],loc.diffStateLoc[1],
                                  loc.diffStateLoc[2]};
        auto minus=plus;
        plus[column]+=step; minus[column]-=step;
        const auto upper = rates(inputs, plus.data());
        const auto lower = rates(inputs, minus.data());
        for (index_t index = 0; index < 3; ++index) {
            matrixData.assign(diff + index,
                              diff + column,
                              ((upper[index] - lower[index]) / (2 * step)) - (index == column ? stateData.cj : 0.0));
        }
    }
    for (index_t column=0;column<2 && column<inputLocs.size();++column) {
        if (inputLocs[column] == kNullLocation || (column == 1 && inputs[column] == kNullVal)) {
            continue;
        }
        auto plus = inputs;
        auto minus = inputs;
        plus[column]+=step;minus[column]-=step;
        const auto upper=rates(plus,loc.diffStateLoc);
        const auto lower=rates(minus,loc.diffStateLoc);
        for (index_t index = 0; index < 3; ++index) {
            matrixData.assign(diff + index, inputLocs[column], (upper[index] - lower[index]) / (2 * step));
        }
    }
}

void WTDTA1::timestep(CoreTime time, const IOdata& inputs, const SolverMode& /*sMode*/)
{
    const double deltaTime=time-prevTime;
    if (deltaTime < 0) {
        throw InvalidParameterValue("WTDTA1 timestep precedes current time");
    }
    const auto rate=rates(inputs,m_state.data());
    for (index_t index = 0; index < 3; ++index) {
        m_state[index] += deltaTime * rate[index];
    }
    prevTime=time;
}

stringVec WTDTA1::localStateNames() const {return {"wg","wt","Tshaft"};}

} // namespace griddyn
