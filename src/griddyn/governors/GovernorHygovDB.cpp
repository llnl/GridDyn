#include "GovernorHygovDB.h"
#include "core/CoreExceptions.h"
#include "core/CoreObjectTemplates.hpp"
#include "utilities/MatrixData.hpp"
#include <algorithm>
#include <cmath>

namespace griddyn::governors {
GovernorHygovDB::GovernorHygovDB(const std::string& name): GovernorHygov(name)
{
    K=20.0; temporaryDroop=1.0; Pmax=1.0; Pmin=0.0; VELM=0.3;
    Tf=0.05; Tr=1.0; Tg=0.05; Tw=1.0; At=1.0; Dturb=0.0; qNL=0.1;
}

CoreObject* GovernorHygovDB::clone(CoreObject* obj) const
{
    auto* result = cloneBase<GovernorHygovDB, GovernorHygov>(this, obj);
    if (result != nullptr) {
        result->dbL = dbL;
        result->dbU = dbU;
    }
    return result;
}

void GovernorHygovDB::dynObjectInitializeA(CoreTime time0, std::uint32_t flags)
{
    if (!std::isfinite(dbL) || !std::isfinite(dbU) || dbL > dbU) {
        throw InvalidParameterValue("HYGOVDB deadband limits");
    }
    GovernorHygov::dynObjectInitializeA(time0, flags);
    offsets.local().local.algRoots = 0;
    offsets.local().local.jacSize = 20;
}

void GovernorHygovDB::set(std::string_view param, double value, units::unit unitType)
{
    if (param == "dbl") {
        dbL = value;
    } else if (param == "dbu") {
        dbU = value;
    } else {
        GovernorHygov::set(param, value, unitType);
    }
}

double GovernorHygovDB::get(std::string_view param, units::unit unitType) const
{
    if (param == "dbl") { return dbL; }
    if (param == "dbu") { return dbU; }
    return GovernorHygov::get(param, unitType);
}

double GovernorHygovDB::governorSpeedDeviation(const IOdata& inputs) const
{
    const double wd = inputs[govOmegaInLocation] - 1.0;
    if (wd < dbL) { return wd - dbL; }
    if (wd > dbU) { return wd - dbU; }
    return 0.0;
}

double GovernorHygovDB::governorSpeedSlope(const IOdata& inputs) const
{
    const double wd = inputs[govOmegaInLocation] - 1.0;
    return (wd < dbL || wd > dbU) ? 1.0 : 0.0;
}

double GovernorHygovDB::gateRate(const double* x) const
{
    const double rate=std::clamp(x[0],-static_cast<double>(VELM),static_cast<double>(VELM));
    if ((x[1]>=Pmax && rate>0.0) || (x[1]<=Pmin && rate<0.0)) { return 0.0; }
    return rate;
}

double GovernorHygovDB::mechanicalPower(const IOdata& inputs, const double* x) const
{
    const double gate=std::abs(x[2])>=1e-8 ? x[2] : std::copysign(1e-8,x[2]);
    const double head=(x[3]/gate)*(x[3]/gate);
    return At*head*(x[3]-qNL)-Dturb*x[2]*(inputs[govOmegaInLocation]-1.0);
}

void GovernorHygovDB::derivative(const IOdata& inputs, const StateData& stateData,
                                 double deriv[], const SolverMode& mode)
{
    if (!hasDifferential(mode)) { return; }
    const auto loc=offsets.getLocations(stateData,deriv,mode,this);
    const double* x=loc.diffStateLoc;
    const double dg=x[0]/temporaryDroop+x[1];
    const double gate=std::abs(x[2])>=1e-8 ? x[2] : std::copysign(1e-8,x[2]);
    const double head=(x[3]/gate)*(x[3]/gate);
    loc.destDiffLoc[0]=(Pset-governorSpeedDeviation(inputs)-dg/K-x[0])/Tf;
    loc.destDiffLoc[1]=gateRate(x);
    loc.destDiffLoc[2]=(dg-x[2])/Tg;
    loc.destDiffLoc[3]=(h0-head)/Tw;
}

void GovernorHygovDB::residual(const IOdata& inputs, const StateData& stateData,
                               double resid[], const SolverMode& mode)
{
    const auto loc=offsets.getLocations(stateData,resid,mode,this);
    if (hasAlgebraic(mode)) {
        loc.destLoc[0]=mechanicalPower(inputs,loc.diffStateLoc)-loc.algStateLoc[0];
    }
    if (hasDifferential(mode)) {
        derivative(inputs,stateData,resid,mode);
        for (index_t i=0;i<4;++i) { loc.destDiffLoc[i]-=loc.dstateLoc[i]; }
    }
}

void GovernorHygovDB::algebraicUpdate(const IOdata& inputs, const StateData& stateData,
                                      double update[], const SolverMode& mode, double)
{
    if (!hasAlgebraic(mode)) { return; }
    const auto loc=offsets.getLocations(stateData,update,mode,this);
    loc.destLoc[0]=mechanicalPower(inputs,loc.diffStateLoc);
}

void GovernorHygovDB::jacobianElements(const IOdata& inputs, const StateData& stateData,
                                       MatrixData<double>& mat, const IOlocs& cols,
                                       const SolverMode& mode)
{
    const auto loc=offsets.getLocations(stateData,mode,this);
    const double* x=loc.diffStateLoc;
    const double gate=std::abs(x[2])>=1e-8 ? x[2] : std::copysign(1e-8,x[2]);
    const double head=(x[3]/gate)*(x[3]/gate);
    const double dhdg=std::abs(x[2])>=1e-8 ? -2.0*head/gate : 0.0;
    const double dhdq=2.0*x[3]/(gate*gate);
    if (hasAlgebraic(mode)) {
        mat.assign(loc.algOffset,loc.algOffset,-1.0);
        if (!isAlgebraicOnly(mode)) {
            mat.assign(loc.algOffset,loc.diffOffset+2,
                       At*dhdg*(x[3]-qNL)-Dturb*(inputs[govOmegaInLocation]-1.0));
            mat.assign(loc.algOffset,loc.diffOffset+3,
                       At*(dhdq*(x[3]-qNL)+head));
        }
        mat.assignCheckCol(loc.algOffset,cols[govOmegaInLocation],-Dturb*x[2]);
    }
    if (!hasDifferential(mode)) { return; }
    mat.assign(loc.diffOffset,loc.diffOffset,
               -(1.0+1.0/(K*temporaryDroop))/Tf-stateData.cj);
    mat.assign(loc.diffOffset,loc.diffOffset+1,-1.0/(K*Tf));
    mat.assignCheckCol(loc.diffOffset,cols[govOmegaInLocation],
                       -governorSpeedSlope(inputs)/Tf);
    const double raw=x[0];
    const double rate=std::clamp(raw,-static_cast<double>(VELM),static_cast<double>(VELM));
    const bool positionLimited=(x[1]>=Pmax && rate>0.0)||(x[1]<=Pmin && rate<0.0);
    if (!positionLimited && raw>-VELM && raw<VELM) {
        mat.assign(loc.diffOffset+1,loc.diffOffset,1.0);
    }
    mat.assign(loc.diffOffset+1,loc.diffOffset+1,-stateData.cj);
    mat.assign(loc.diffOffset+2,loc.diffOffset,1.0/(temporaryDroop*Tg));
    mat.assign(loc.diffOffset+2,loc.diffOffset+1,1.0/Tg);
    mat.assign(loc.diffOffset+2,loc.diffOffset+2,-1.0/Tg-stateData.cj);
    mat.assign(loc.diffOffset+3,loc.diffOffset+2,-dhdg/Tw);
    mat.assign(loc.diffOffset+3,loc.diffOffset+3,-dhdq/Tw-stateData.cj);
}

void GovernorHygovDB::timestep(CoreTime time, const IOdata& inputs, const SolverMode&)
{
    derivative(inputs,emptyStateData,m_dstate_dt.data(),cLocalSolverMode);
    const auto d=offsets.getDiffOffset(cLocalSolverMode);
    const double dt=time-prevTime;
    for (index_t i=0;i<4;++i) { m_state[d+i]+=dt*m_dstate_dt[d+i]; }
    m_state[d+1]=std::clamp(m_state[d+1],static_cast<double>(Pmin),static_cast<double>(Pmax));
    m_state[offsets.getAlgOffset(cLocalSolverMode)]=mechanicalPower(inputs,m_state.data()+d);
    prevTime=time;
}

void GovernorHygovDB::rootTest(const IOdata&, const StateData&, double[], const SolverMode&) {}
void GovernorHygovDB::rootTrigger(CoreTime, const IOdata&, const std::vector<int>&,
                                  const SolverMode&) {}
ChangeCode GovernorHygovDB::rootCheck(const IOdata&, const StateData&, const SolverMode&,
                                      CheckLevel)
{ return ChangeCode::NO_CHANGE; }
}  // namespace griddyn::governors
