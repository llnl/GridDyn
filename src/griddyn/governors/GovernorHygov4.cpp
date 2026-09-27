#include "GovernorHygov4.h"
#include "core/CoreExceptions.h"
#include "core/CoreObjectTemplates.hpp"
#include "utilities/MatrixData.hpp"
#include <algorithm>
#include <array>
#include <cmath>

namespace griddyn::governors {
GovernorHygov4::GovernorHygov4(const std::string& name): Governor(name)
{
    Pmax=1.0; Pmin=0.0;
    opFlags.set(IGNORE_DEADBAND); opFlags.set(IGNORE_FILTER); opFlags.set(IGNORE_THROTTLE);
}
CoreObject* GovernorHygov4::clone(CoreObject* obj) const
{
    auto* out=cloneBase<GovernorHygov4,Governor>(this,obj);
    if (out != nullptr) {
        out->Rperm=Rperm; out->Rtemp=Rtemp; out->UO=UO; out->UC=UC;
        out->Tp=Tp; out->Tg=Tg; out->Tr=Tr; out->Tw=Tw;
        out->At=At; out->Dturb=Dturb; out->Hdam=Hdam; out->qNL=qNL; out->paux=paux;
        out->referenceOffset=referenceOffset;
    }
    return out;
}
void GovernorHygov4::dynObjectInitializeA(CoreTime time0, std::uint32_t flags)
{
    setInitialLimitPolicy(flags);
    const std::array<double,15> pars{Rperm,Rtemp,UO,UC,Pmax,Pmin,Tp,Tg,Tr,Tw,At,Dturb,Hdam,qNL,paux};
    if (std::any_of(pars.begin(),pars.end(),[](double v){return !std::isfinite(v);}) ||
        Rperm <= 0.0 || Rtemp < 0.0 || UO < UC || Tp <= 0.0 || Tg <= 0.0 ||
        Tr <= 0.0 || Tw <= 0.0 || At <= 0.0 || Hdam <= 0.0 || Pmax < Pmin) {
        throw InvalidParameterValue("HYGOV4 parameters");
    }
    auto& local=offsets.local().local;
    local.algSize=1; local.diffSize=4; local.algRoots=0; local.diffRoots=0; local.jacSize=28;
    prevTime=time0;
}
void GovernorHygov4::dynObjectInitializeB(const IOdata&, const IOdata& desired, IOdata& fieldSet)
{
    if (desired.empty() || !std::isfinite(desired[0])) {
        throw InvalidParameterValue("HYGOV4 initial power");
    }
    const double flow=qNL+desired[0]/(At*Hdam);
    const double gate=flow/std::sqrt(Hdam);
    if (!std::isfinite(gate) || std::abs(gate)<1e-8 || gate<Pmin ||
        !adjustInitialUpperLimit(gate,"HYGOV4 initial gate")) {
        throw InvalidParameterValue("HYGOV4 initial gate outside limits");
    }
    const auto a=offsets.getAlgOffset(cLocalSolverMode);
    const auto d=offsets.getDiffOffset(cLocalSolverMode);
    m_state[a]=desired[0];
    m_state[d]=gate; m_state[d+1]=gate; m_state[d+2]=0.0; m_state[d+3]=flow;
    Pset=Rperm*gate;
    // The generator's pset input remains its dispatched mechanical power.
    // Preserve the ANDES pref=Rperm*gate equilibrium with a reference offset.
    referenceOffset=Pset-desired[0]-paux;
    fieldSet.resize(2); fieldSet[govpSetInLocation]=Pset-paux;
}
double GovernorHygov4::regularizedGate(double gate) const
{ return std::abs(gate)>=1e-8 ? gate : std::copysign(1e-8,gate); }
double GovernorHygov4::mechanicalPower(const IOdata& inputs, const double* x) const
{
    const double gate=regularizedGate(x[0]);
    const double h=(x[3]/gate)*(x[3]/gate);
    return At*h*(x[3]-qNL)-Dturb*x[0]*(inputs[govOmegaInLocation]-1.0);
}
double GovernorHygov4::gateRate(const double* x) const
{
    const double raw=x[2]/Tg;
    const double rate=std::clamp(raw,UC,UO);
    if ((x[0]>=Pmax && rate>0.0) || (x[0]<=Pmin && rate<0.0)) { return 0.0; }
    return rate;
}
void GovernorHygov4::derivative(const IOdata& inputs, const StateData& sd, double out[],
                                const SolverMode& mode)
{
    if (!hasDifferential(mode)) { return; }
    const auto loc=offsets.getLocations(sd,out,mode,this);
    const double* x=loc.diffStateLoc;
    const double gate=regularizedGate(x[0]);
    const double head=(x[3]/gate)*(x[3]/gate);
    loc.destDiffLoc[0]=gateRate(x);
    loc.destDiffLoc[1]=(x[0]-x[1])/Tr;
    loc.destDiffLoc[2]=(inputs[govpSetInLocation]+referenceOffset+paux-Rperm*x[0]
                        -Rtemp*(x[0]-x[1])-(inputs[govOmegaInLocation]-1.0)-x[2])/Tp;
    loc.destDiffLoc[3]=(Hdam-head)/Tw;
}
void GovernorHygov4::residual(const IOdata& inputs, const StateData& sd, double out[],
                              const SolverMode& mode)
{
    const auto loc=offsets.getLocations(sd,out,mode,this);
    if (hasAlgebraic(mode)) { loc.destLoc[0]=mechanicalPower(inputs,loc.diffStateLoc)-loc.algStateLoc[0]; }
    if (hasDifferential(mode)) {
        derivative(inputs,sd,out,mode);
        for (index_t i=0;i<4;++i) { loc.destDiffLoc[i]-=loc.dstateLoc[i]; }
    }
}
void GovernorHygov4::algebraicUpdate(const IOdata& inputs, const StateData& sd,
                                     double out[], const SolverMode& mode, double)
{
    if (!hasAlgebraic(mode)) { return; }
    const auto loc=offsets.getLocations(sd,out,mode,this);
    loc.destLoc[0]=mechanicalPower(inputs,loc.diffStateLoc);
}
void GovernorHygov4::jacobianElements(const IOdata& inputs, const StateData& sd,
                                      MatrixData<double>& mat, const IOlocs& cols,
                                      const SolverMode& mode)
{
    const auto loc=offsets.getLocations(sd,mode,this);
    const double* x=loc.diffStateLoc;
    const double gate=regularizedGate(x[0]);
    const double head=(x[3]/gate)*(x[3]/gate);
    const double dhdg=std::abs(x[0])>=1e-8 ? -2.0*head/gate : 0.0;
    const double dhdq=2.0*x[3]/(gate*gate);
    if (hasAlgebraic(mode)) {
        mat.assign(loc.algOffset,loc.algOffset,-1.0);
        if (!isAlgebraicOnly(mode)) {
            mat.assign(loc.algOffset,loc.diffOffset,At*dhdg*(x[3]-qNL)-Dturb*(inputs[govOmegaInLocation]-1.0));
            mat.assign(loc.algOffset,loc.diffOffset+3,At*(dhdq*(x[3]-qNL)+head));
        }
        mat.assignCheckCol(loc.algOffset,cols[govOmegaInLocation],-Dturb*x[0]);
    }
    if (!hasDifferential(mode)) { return; }
    const double raw=x[2]/Tg;
    const double rate=std::clamp(raw,UC,UO);
    const bool gateLimited=(x[0]>=Pmax && rate>0.0)||(x[0]<=Pmin && rate<0.0);
    mat.assign(loc.diffOffset,loc.diffOffset,-sd.cj);
    if (!gateLimited && raw>UC && raw<UO) {
        mat.assign(loc.diffOffset,loc.diffOffset+2,1.0/Tg);
    }
    mat.assign(loc.diffOffset+1,loc.diffOffset,1.0/Tr);
    mat.assign(loc.diffOffset+1,loc.diffOffset+1,-1.0/Tr-sd.cj);
    mat.assign(loc.diffOffset+2,loc.diffOffset,-(Rperm+Rtemp)/Tp);
    mat.assign(loc.diffOffset+2,loc.diffOffset+1,Rtemp/Tp);
    mat.assign(loc.diffOffset+2,loc.diffOffset+2,-1.0/Tp-sd.cj);
    mat.assignCheckCol(loc.diffOffset+2,cols[govOmegaInLocation],-1.0/Tp);
    mat.assignCheckCol(loc.diffOffset+2,cols[govpSetInLocation],1.0/Tp);
    mat.assign(loc.diffOffset+3,loc.diffOffset,-dhdg/Tw);
    mat.assign(loc.diffOffset+3,loc.diffOffset+3,-dhdq/Tw-sd.cj);
}
void GovernorHygov4::timestep(CoreTime time, const IOdata& inputs, const SolverMode&)
{
    derivative(inputs,emptyStateData,m_dstate_dt.data(),cLocalSolverMode);
    const double dt=time-prevTime;
    const auto d=offsets.getDiffOffset(cLocalSolverMode);
    for (index_t i=0;i<4;++i) { m_state[d+i]+=dt*m_dstate_dt[d+i]; }
    m_state[d]=std::clamp(m_state[d],static_cast<double>(Pmin),static_cast<double>(Pmax));
    m_state[offsets.getAlgOffset(cLocalSolverMode)]=mechanicalPower(inputs,m_state.data()+d);
    prevTime=time;
}
index_t GovernorHygov4::findIndex(std::string_view field, const SolverMode& mode) const
{
    if (field=="pm" || field=="pmech") { return offsets.getAlgOffset(mode); }
    if (field=="gate") { return offsets.getDiffOffset(mode); }
    if (field=="washout") { return offsets.getDiffOffset(mode)+1; }
    if (field=="pilot") { return offsets.getDiffOffset(mode)+2; }
    if (field=="flow") { return offsets.getDiffOffset(mode)+3; }
    return kInvalidLocation;
}
void GovernorHygov4::set(std::string_view param, double value, units::unit unitType)
{
    if (!std::isfinite(value)) { throw InvalidParameterValue("HYGOV4 parameter must be finite"); }
    if (param=="rperm") { Rperm=value; }
    else if (param=="rtemp") { Rtemp=value; }
    else if (param=="uo") { UO=value; }
    else if (param=="uc") { UC=value; }
    else if (param=="tp") { Tp=value; }
    else if (param=="tg") { Tg=value; }
    else if (param=="tr") { Tr=value; }
    else if (param=="tw") { Tw=value; }
    else if (param=="at") { At=value; }
    else if (param=="dturb") { Dturb=value; }
    else if (param=="hdam") { Hdam=value; }
    else if (param=="qnl") { qNL=value; }
    else if (param=="paux") { paux=value; }
    else { Governor::set(param,value,unitType); }
}
double GovernorHygov4::get(std::string_view param, units::unit unitType) const
{
    if (param=="rperm") { return Rperm; }
    if (param=="rtemp") { return Rtemp; }
    if (param=="uo") { return UO; }
    if (param=="uc") { return UC; }
    if (param=="tp") { return Tp; }
    if (param=="tg") { return Tg; }
    if (param=="tr") { return Tr; }
    if (param=="tw") { return Tw; }
    if (param=="at") { return At; }
    if (param=="dturb") { return Dturb; }
    if (param=="hdam") { return Hdam; }
    if (param=="qnl") { return qNL; }
    if (param=="paux") { return paux; }
    return Governor::get(param,unitType);
}
}  // namespace griddyn::governors
