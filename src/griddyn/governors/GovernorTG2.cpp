#include "GovernorTG2.h"

#include "core/CoreExceptions.h"
#include "core/CoreObjectTemplates.hpp"
#include "utilities/MatrixData.hpp"
#include <algorithm>
#include <cmath>

namespace griddyn::governors {
GovernorTG2::GovernorTG2(const std::string& name): Governor(name)
{
    K = 20.0;
    T1 = 0.2;
    T2 = 10.0;
    Pmax = 999.0;
    Pmin = 0.0;
    opFlags.set(IGNORE_DEADBAND);
    opFlags.set(IGNORE_FILTER);
    opFlags.set(IGNORE_THROTTLE);
}
CoreObject* GovernorTG2::clone(CoreObject* obj) const
{
    auto* out = cloneBase<GovernorTG2, Governor>(this, obj);
    if (out != nullptr) {
        out->dbL = dbL;
        out->dbU = dbU;
        out->dbC = dbC;
        out->deadbandEnabled = deadbandEnabled;
        out->hardLimitEnabled = hardLimitEnabled;
    }
    return out;
}
void GovernorTG2::dynObjectInitializeA(CoreTime time0, std::uint32_t flags)
{
    setInitialLimitPolicy(flags);
    if (!std::isfinite(K) || K <= 0.0 || !std::isfinite(T1) || T1 < 0.0 || !std::isfinite(T2) ||
        T2 <= 0.0 || !std::isfinite(Pmax) || !std::isfinite(Pmin) || Pmax < Pmin ||
        !std::isfinite(dbL) || !std::isfinite(dbU) || !std::isfinite(dbC) || dbL > dbU) {
        throw InvalidParameterValue("TG2 parameters");
    }
    auto& local = offsets.local().local;
    local.algSize = 1;
    local.diffSize = 1;
    local.algRoots = 0;
    local.diffRoots = 0;
    local.jacSize = 8;
    prevTime = time0;
}
void GovernorTG2::dynObjectInitializeB(const IOdata&, const IOdata& desired, IOdata& fieldSet)
{
    if (desired.empty() || !std::isfinite(desired[0]) ||
        (hardLimitEnabled &&
         (desired[0] < Pmin || !adjustInitialUpperLimit(desired[0], "TG2 initial power")))) {
        throw InvalidParameterValue("TG2 initial output");
    }
    Pset = desired[0];
    m_state[offsets.getAlgOffset(cLocalSolverMode)] = Pset;
    m_state[offsets.getDiffOffset(cLocalSolverMode)] = 0.0;
    fieldSet.resize(2);
    fieldSet[govpSetInLocation] = Pset;
}
double GovernorTG2::speedInput(const IOdata& inputs) const
{
    const double wd = 1.0 - inputs[govOmegaInLocation];
    if (!deadbandEnabled) {
        return wd;
    }
    // ANDES DeadBandRT currently has no working return-direction flags:
    // outside the band it passes the speed deviation, inside it gives zero.
    if (wd < dbL || wd > dbU) {
        return wd;
    }
    return 0.0;
}
double GovernorTG2::speedSlope(const IOdata& inputs) const
{
    const double wd = 1.0 - inputs[govOmegaInLocation];
    return (!deadbandEnabled || wd < dbL || wd > dbU) ? -1.0 : 0.0;
}
double GovernorTG2::output(const IOdata& inputs, double state) const
{
    const double raw =
        inputs[govpSetInLocation] + state + (T1 / T2) * (K * speedInput(inputs) - state);
    return hardLimitEnabled ?
        std::clamp(raw, static_cast<double>(Pmin), static_cast<double>(Pmax)) :
        raw;
}
double GovernorTG2::outputSlope(const IOdata& inputs, double state) const
{
    const double raw =
        inputs[govpSetInLocation] + state + (T1 / T2) * (K * speedInput(inputs) - state);
    return (!hardLimitEnabled || (raw > Pmin && raw < Pmax)) ? 1.0 : 0.0;
}
void GovernorTG2::derivative(const IOdata& inputs,
                             const StateData& sd,
                             double out[],
                             const SolverMode& mode)
{
    if (!hasDifferential(mode)) {
        return;
    }
    const auto loc = offsets.getLocations(sd, out, mode, this);
    loc.destDiffLoc[0] = (K * speedInput(inputs) - loc.diffStateLoc[0]) / T2;
}
void GovernorTG2::residual(const IOdata& inputs,
                           const StateData& sd,
                           double out[],
                           const SolverMode& mode)
{
    const auto loc = offsets.getLocations(sd, out, mode, this);
    if (hasAlgebraic(mode)) {
        loc.destLoc[0] = output(inputs, loc.diffStateLoc[0]) - loc.algStateLoc[0];
    }
    if (hasDifferential(mode)) {
        derivative(inputs, sd, out, mode);
        loc.destDiffLoc[0] -= loc.dstateLoc[0];
    }
}
void GovernorTG2::algebraicUpdate(const IOdata& inputs,
                                  const StateData& sd,
                                  double out[],
                                  const SolverMode& mode,
                                  double)
{
    if (!hasAlgebraic(mode)) {
        return;
    }
    const auto loc = offsets.getLocations(sd, out, mode, this);
    loc.destLoc[0] = output(inputs, loc.diffStateLoc[0]);
}
void GovernorTG2::jacobianElements(const IOdata& inputs,
                                   const StateData& sd,
                                   MatrixData<double>& mat,
                                   const IOlocs& cols,
                                   const SolverMode& mode)
{
    const auto loc = offsets.getLocations(sd, mode, this);
    const double active = outputSlope(inputs, loc.diffStateLoc[0]);
    if (hasAlgebraic(mode)) {
        mat.assign(loc.algOffset, loc.algOffset, -1.0);
        if (!isAlgebraicOnly(mode)) {
            mat.assign(loc.algOffset, loc.diffOffset, active * (1.0 - T1 / T2));
        }
        mat.assignCheckCol(loc.algOffset,
                           cols[govOmegaInLocation],
                           active * (T1 / T2) * K * speedSlope(inputs));
        mat.assignCheckCol(loc.algOffset, cols[govpSetInLocation], active);
    }
    if (hasDifferential(mode)) {
        mat.assign(loc.diffOffset, loc.diffOffset, -1.0 / T2 - sd.cj);
        mat.assignCheckCol(loc.diffOffset, cols[govOmegaInLocation], K * speedSlope(inputs) / T2);
    }
}
void GovernorTG2::timestep(CoreTime time, const IOdata& inputs, const SolverMode&)
{
    derivative(inputs, emptyStateData, m_dstate_dt.data(), cLocalSolverMode);
    const auto d = offsets.getDiffOffset(cLocalSolverMode);
    m_state[d] += (time - prevTime) * m_dstate_dt[d];
    m_state[offsets.getAlgOffset(cLocalSolverMode)] = output(inputs, m_state[d]);
    prevTime = time;
}
index_t GovernorTG2::findIndex(std::string_view field, const SolverMode& mode) const
{
    if (field == "pm" || field == "pmech") {
        return offsets.getAlgOffset(mode);
    }
    if (field == "ll" || field == "leadlag") {
        return offsets.getDiffOffset(mode);
    }
    return kInvalidLocation;
}
void GovernorTG2::set(std::string_view param, double value, units::unit unitType)
{
    if (!std::isfinite(value)) {
        throw InvalidParameterValue("TG2 parameter must be finite");
    }
    if (param == "dbl") {
        dbL = value;
    } else if (param == "dbu") {
        dbU = value;
    } else if (param == "dbc") {
        dbC = value;
    } else if (param == "deadbandenabled" || param == "deadband") {
        deadbandEnabled = (value != 0.0);
    } else if (param == "hardlimit") {
        hardLimitEnabled = (value != 0.0);
    } else {
        Governor::set(param, value, unitType);
    }
}
double GovernorTG2::get(std::string_view param, units::unit unitType) const
{
    if (param == "dbl") {
        return dbL;
    }
    if (param == "dbu") {
        return dbU;
    }
    if (param == "dbc") {
        return dbC;
    }
    if (param == "deadbandenabled" || param == "deadband") {
        return deadbandEnabled ? 1.0 : 0.0;
    }
    if (param == "hardlimit") {
        return hardLimitEnabled ? 1.0 : 0.0;
    }
    return Governor::get(param, unitType);
}
}  // namespace griddyn::governors
