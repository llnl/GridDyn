/*
 * Copyright (c) 2014-2026, Lawrence Livermore National Security
 * See the top-level NOTICE for additional details. All rights reserved.
 * SPDX-License-Identifier: BSD-3-Clause
 */
#include "REPCA1.h"
#include "core/CoreExceptions.h"
#include "core/CoreObjectTemplates.hpp"
#include "gmlc/utilities/stringOps.h"
#include "utilities/MatrixData.hpp"
#include <algorithm>
#include <array>
#include <cmath>
#include <string>

namespace griddyn {
namespace {
    constexpr std::array<RenewablePort, 2> inputs{{
        {.signal = RenewableSignal::terminalVoltage, .ioIndex = 0},
        {.signal = RenewableSignal::reactivePower, .ioIndex = 1, .base = RenewableBase::machine},
    }};
    constexpr std::array<RenewablePort, 2> outputs{{
        {.signal = RenewableSignal::activeReferenceIncrement,
         .ioIndex = 0,
         .base = RenewableBase::machine},
        {.signal = RenewableSignal::reactiveReferenceIncrement,
         .ioIndex = 1,
         .base = RenewableBase::machine},
    }};
    constexpr index_t pext = 0, qext = 1, voltageFilter = 0, reactiveFilter = 1, integral = 2, lag = 3;
}  // namespace

REPCA1::REPCA1(const std::string& name): RenewableComponent(name)
{
    m_inputSize=2;
    m_outputSize=2;
}

CoreObject* REPCA1::clone(CoreObject* obj) const
{
    auto* out=cloneBase<REPCA1,RenewableComponent>(this,obj);
    if (out == nullptr) {
        return obj;
    }
    out->VCFlag=VCFlag; out->RefFlag=RefFlag; out->Fflag=Fflag; out->PLflag=PLflag;
    out->Tfltr=Tfltr; out->Kp=Kp; out->Ki=Ki; out->Tft=Tft; out->Tfv=Tfv;
    out->Vfrz=Vfrz; out->Rc=Rc; out->Xc=Xc; out->Kc=Kc;
    out->emax=emax; out->emin=emin; out->dbd1=dbd1; out->dbd2=dbd2;
    out->Qmax=Qmax; out->Qmin=Qmin; out->Kpg=Kpg; out->Kig=Kig; out->Tp=Tp;
    out->fdbd1=fdbd1; out->fdbd2=fdbd2; out->femax=femax; out->femin=femin;
    out->Pmax=Pmax; out->Pmin=Pmin; out->Tg=Tg; out->Ddn=Ddn; out->Dup=Dup;
    out->vReference=vReference; out->qReference=qReference;
    return out;
}

std::span<const RenewablePort> REPCA1::inputPorts() const { return inputs; }
std::span<const RenewablePort> REPCA1::outputPorts() const { return outputs; }

void REPCA1::set(std::string_view param,double val,units::unit unitType)
{
    const auto key=gmlc::utilities::convertToLowerCase(std::string{param});
    if (key=="vcflag" || key=="refflag" || key=="fflag" || key=="plflag") {
        if (val != 0.0 && val != 1.0) {
            throw InvalidParameterValue("REPCA1 flag must be zero or one");
        }
        const auto flag=static_cast<int>(val);
        if (key == "vcflag") {
            VCFlag = flag;
        } else if (key == "refflag") {
            RefFlag = flag;
        } else if (key == "fflag") {
            Fflag = flag;
        } else {
            PLflag = flag;
        }
    } else if (key == "tfltr") {
        Tfltr = val;
    } else if (key == "kp") {
        Kp = val;
    } else if (key == "ki") {
        Ki = val;
    } else if (key == "tft") {
        Tft = val;
    } else if (key == "tfv") {
        Tfv = val;
    } else if (key == "vfrz") {
        Vfrz = val;
    } else if (key == "rc") {
        Rc = val;
    } else if (key == "xc") {
        Xc = val;
    } else if (key == "kc") {
        Kc = val;
    } else if (key == "emax") {
        emax = val;
    } else if (key == "emin") {
        emin = val;
    } else if (key == "dbd1") {
        dbd1 = val;
    } else if (key == "dbd2") {
        dbd2 = val;
    } else if (key == "qmax") {
        Qmax = val;
    } else if (key == "qmin") {
        Qmin = val;
    } else if (key == "kpg") {
        Kpg = val;
    } else if (key == "kig") {
        Kig = val;
    } else if (key == "tp") {
        Tp = val;
    } else if (key == "fdbd1") {
        fdbd1 = val;
    } else if (key == "fdbd2") {
        fdbd2 = val;
    } else if (key == "femax") {
        femax = val;
    } else if (key == "femin") {
        femin = val;
    } else if (key == "pmax") {
        Pmax = val;
    } else if (key == "pmin") {
        Pmin = val;
    } else if (key == "tg") {
        Tg = val;
    } else if (key == "ddn") {
        Ddn = val;
    } else if (key == "dup") {
        Dup = val;
    } else if (key == "vref") {
        vReference = val;
    } else if (key == "qref") {
        qReference = val;
    } else {
        RenewableComponent::set(param, val, unitType);
    }
}

double REPCA1::get(std::string_view param,units::unit unitType) const
{
    const auto key=gmlc::utilities::convertToLowerCase(std::string{param});
    if (key == "vcflag") {
        return VCFlag;
    }
    if (key == "refflag") {
        return RefFlag;
    }
    if (key == "fflag") {
        return Fflag;
    }
    if (key == "kp") {
        return Kp;
    }
    if (key == "ki") {
        return Ki;
    }
    if (key == "tfltr") {
        return Tfltr;
    }
    if (key == "vref") {
        return vReference;
    }
    if (key == "qref") {
        return qReference;
    }
    if (key == "qmax") {
        return Qmax;
    }
    if (key == "qmin") {
        return Qmin;
    }
    return RenewableComponent::get(param,unitType);
}

void REPCA1::dynObjectInitializeA(CoreTime time0,std::uint32_t /*flags*/)
{
    const std::array<double,27> inputs{Tfltr,Kp,Ki,Tft,Tfv,Vfrz,Rc,Xc,Kc,
        emax,emin,dbd1,dbd2,Qmax,Qmin,Kpg,Kig,Tp,fdbd1,fdbd2,
        femax,femin,Pmax,Pmin,Tg,Ddn,Dup};
    if (std::any_of(inputs.begin(),inputs.end(),[](double stateValue){return !std::isfinite(stateValue);}) ||
        Tfltr<=0 || Tfv<=0 || Tft<0 || Kp<0 || Ki<0 || Qmax<Qmin ||
        emax<emin || dbd1>0 || dbd2<0 || (RefFlag!=0 && RefFlag!=1) ||
        (VCFlag!=0 && VCFlag!=1) || (PLflag!=0 && PLflag!=1) ||
        Fflag!=0 || Rc!=0 || Xc!=0 || Kc!=0) {
        throw InvalidParameterValue("REPCA1 unsupported remote/frequency mode or invalid parameters");
    }
    auto& local=offsets.local().local;
    local.algSize=2;
    local.diffSize=4;
    local.jacSize=48;
    prevTime=time0;
}

void REPCA1::dynObjectInitializeB(const IOdata& inputs,const IOdata& desiredOutput,
                                   IOdata& fieldSet)
{
    if (inputs.size()<2 || desiredOutput.size()<2 || !std::isfinite(inputs[0]) ||
        !std::isfinite(inputs[1]) || inputs[0]<=0) {
        throw InvalidParameterValue("REPCA1 initial terminal voltage or Q");
    }
    vReference=inputs[0];
    qReference=inputs[1];
    m_state[pext]=0; m_state[qext]=0;
    m_state[2+voltageFilter]=inputs[0]; m_state[2+reactiveFilter]=inputs[1];
    m_state[2+integral]=0; m_state[2+lag]=0;
    fieldSet={0.0,0.0};
}

double REPCA1::controllerError(double voltage,const double state[]) const
{
    if (voltage < Vfrz) {
        return 0.0;
    }
    const double raw=(RefFlag==1?vReference-state[voltageFilter]:qReference-state[reactiveFilter]);
    double deadband = 0.0;
    if (raw > dbd2) {
        deadband = raw - dbd2;
    } else if (raw < dbd1) {
        deadband = raw - dbd1;
    }
    return std::clamp(deadband,emin,emax);
}

double REPCA1::piOutput(double voltage,const double state[]) const
{
    return std::clamp((Kp * controllerError(voltage, state)) + (Ki * state[integral]), Qmin, Qmax);
}

double REPCA1::reactiveIncrement(double voltage,const double state[]) const
{
    const double ratio=Tft/Tfv;
    return (ratio * piOutput(voltage, state)) + ((1.0 - ratio) * state[lag]);
}

std::array<double,4> REPCA1::rates(const IOdata& inputs,const double state[]) const
{
    const double voltage = inputs[0];
    const double reactivePower = inputs[1];
    const double err=controllerError(voltage,state);
    const double raw = (Kp * err) + (Ki * state[integral]);
    const bool upper=raw>=Qmax && err>0;
    const bool lower=raw<=Qmin && err<0;
    return {(voltage-state[voltageFilter])/Tfltr,
            (reactivePower-state[reactiveFilter])/Tfltr,
            upper||lower?0.0:err,(piOutput(voltage,state)-state[lag])/Tfv};
}

void REPCA1::derivative(const IOdata& inputs,const StateData& stateData,
                        double deriv[],const SolverMode& sMode)
{
    if (!hasDifferential(sMode)) {
        return;
    }
    const auto loc=offsets.getLocations(stateData,deriv,sMode,this);
    const auto result=rates(inputs,loc.diffStateLoc);
    for (index_t index = 0; index < 4; ++index) {
        loc.destDiffLoc[index] = result[index];
    }
}

void REPCA1::residual(const IOdata& inputs,const StateData& stateData,
                      double resid[],const SolverMode& sMode)
{
    const auto loc=offsets.getLocations(stateData,resid,sMode,this);
    if (hasAlgebraic(sMode)) {
        loc.destLoc[pext]=-loc.algStateLoc[pext];
        loc.destLoc[qext]=reactiveIncrement(inputs[0],loc.diffStateLoc)-loc.algStateLoc[qext];
    }
    if (hasDifferential(sMode)) {
        derivative(inputs,stateData,resid,sMode);
        for (index_t index = 0; index < loc.diffSize; ++index) {
            loc.destDiffLoc[index] -= loc.dstateLoc[index];
        }
    }
}

void REPCA1::algebraicUpdate(const IOdata& inputs,const StateData& stateData,
                             double update[],const SolverMode& sMode,double /*alpha*/)
{
    if (!hasAlgebraic(sMode)) {
        return;
    }
    const auto loc=offsets.getLocations(stateData,update,sMode,this);
    loc.destLoc[pext]=0.0;
    loc.destLoc[qext]=reactiveIncrement(inputs[0],loc.diffStateLoc);
}

void REPCA1::jacobianElements(const IOdata& inputs,const StateData& stateData,
                              MatrixData<double>& matrixData,const IOlocs& inputLocs,
                              const SolverMode& sMode)
{
    const auto loc=offsets.getLocations(stateData,sMode,this);
    const auto alg = loc.algOffset;
    const auto diff = loc.diffOffset;
    const double* state=loc.diffStateLoc;
    constexpr double step=1e-6;
    if (hasAlgebraic(sMode)) {
        matrixData.assign(alg+pext,alg+pext,-1.0);
        matrixData.assign(alg+qext,alg+qext,-1.0);
    }
    if (!hasDifferential(sMode) && !hasAlgebraic(sMode)) {
        return;
    }
    if (!isAlgebraicOnly(sMode)) {
        for (index_t column=0;column<4;++column) {
            std::array<double,4> plus{state[0],state[1],state[2],state[3]};
            auto minus=plus;
            plus[column]+=step; minus[column]-=step;
            if (hasAlgebraic(sMode)) {
                matrixData.assign(alg + qext,
                                  diff + column,
                                  (reactiveIncrement(inputs[0], plus.data()) -
                                   reactiveIncrement(inputs[0], minus.data())) /
                                      (2 * step));
            }
            if (hasDifferential(sMode)) {
                const auto upper=rates(inputs,plus.data());
                const auto lower=rates(inputs,minus.data());
                for (index_t index = 0; index < 4; ++index) {
                    matrixData.assign(diff + index,
                                      diff + column,
                                      ((upper[index] - lower[index]) / (2 * step)) -
                                          (index == column ? stateData.cj : 0.0));
                }
            }
        }
    }
    for (std::size_t column = 0; column < 2 && column < inputLocs.size(); ++column) {
        if (inputLocs[column] == kNullLocation) {
            continue;
        }
        auto plus = inputs;
        auto minus = inputs;
        plus[column]+=step; minus[column]-=step;
        if (hasAlgebraic(sMode) && column == 0) {
            matrixData.assign(alg + qext,
                              inputLocs[column],
                              (reactiveIncrement(plus[0], state) -
                               reactiveIncrement(minus[0], state)) /
                                  (2 * step));
        }
        if (hasDifferential(sMode)) {
            const auto upper = rates(plus, state);
            const auto lower = rates(minus, state);
            for (index_t index = 0; index < 4; ++index) {
                matrixData.assign(diff + index, inputLocs[column], (upper[index] - lower[index]) / (2 * step));
            }
        }
    }
}

void REPCA1::timestep(CoreTime time,const IOdata& inputs,const SolverMode& /*sMode*/)
{
    const double deltaTime=time-prevTime;
    if (deltaTime < 0) {
        throw InvalidParameterValue("REPCA1 timestep precedes current time");
    }
    const auto rate=rates(inputs,m_state.data()+2);
    for (index_t index = 0; index < 4; ++index) {
        m_state[2 + index] += deltaTime * rate[index];
    }
    m_state[pext]=0.0;
    m_state[qext]=reactiveIncrement(inputs[0],m_state.data()+2);
    prevTime=time;
}

IOdata REPCA1::getOutputs(const IOdata& /*inputs*/,
                          const StateData& stateData,
                          const SolverMode& sMode) const
{
    const auto loc=offsets.getLocations(stateData,sMode,this);
    return {loc.algStateLoc[pext],loc.algStateLoc[qext]};
}

void REPCA1::outputPartialDerivatives(const IOdata& /*inputs*/,
                                      const StateData& /*stateDataValue*/,
                                      MatrixData<double>& matrixData,
                                      const SolverMode& sMode)
{
    const auto alg=offsets.getAlgOffset(sMode);
    matrixData.assign(pext,alg+pext,1.0);
    matrixData.assign(qext,alg+qext,1.0);
}

stringVec REPCA1::localStateNames() const
{
    return {"Pext","Qext","Vf","Qf","Qi","Qlag"};
}
}  // namespace griddyn
