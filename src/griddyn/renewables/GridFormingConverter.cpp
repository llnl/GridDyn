/*
 * Copyright (c) 2014-2026, Lawrence Livermore National Security
 * See the top-level NOTICE for additional details. All rights reserved.
 * SPDX-License-Identifier: BSD-3-Clause
 */
#include "GridFormingConverter.h"

#include "core/CoreExceptions.h"
#include "core/CoreObjectTemplates.hpp"
#include "gmlc/utilities/stringOps.h"
#include "utilities/MatrixData.hpp"
#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <string>
#include <string_view>

namespace griddyn {
namespace {
    enum Parameter : index_t {
        fn,
        tc,
        kw,
        kv,
        inertia,
        damping,
        resistance,
        reactance,
        kpvd,
        kivd,
        kpvq,
        kivq,
        kpid,
        kiid,
        kpiq,
        kiiq,
        tid,
        tiq,
        wdrp,
        qdrp,
        tr,
        te,
        kpi,
        kii,
        kpv,
        kiv,
        pmax,
        pmin,
        kpplim,
        kiplim,
        qmax,
        qmin,
        kpqlim,
        kiqlim,
        tpm,
        dwmax,
        dwmin,
        mf,
        dd,
        count
    };
    constexpr std::array<std::string_view, count> names{
        {"fn",   "tc",   "kw",     "kv",     "m",    "d",     "ra",    "xs",   "kpvd",   "kivd",
         "kpvq", "kivq", "kpid",   "kiid",   "kpiq", "kiiq",  "tid",   "tiq",  "wdrp",   "qdrp",
         "tr",   "te",   "kpi",    "kii",    "kpv",  "kiv",   "pmax",  "pmin", "kpplim", "kiplim",
         "qmax", "qmin", "kpqlim", "kiqlim", "tpm",  "dwmax", "dwmin", "mf",   "dd"}};
    constexpr std::array<double, count> defaults{
        {60.0, .01,  0.0, 0.0,  10.0, 0.0,  0.0,  .2,   .5,   .02,  .5,    .02, .2,
         .01,  .2,   .01, .01,  .01,  .033, .045, .005, .005, .5,   20.0,  3.0, 10.0,
         1.0,  -1.0, 5.0, 30.0, 1.0,  -1.0, .1,   1.5,  .025, 75.0, -75.0, .15, .11}};
    constexpr std::array<RenewablePort, 2> terminals{{
        {.signal = RenewableSignal::terminalVoltage, .ioIndex = 0},
        {.signal = RenewableSignal::terminalAngle, .ioIndex = 1},
    }};
    constexpr std::array<RenewablePort, 3> pllTerminals{{
        {.signal = RenewableSignal::terminalVoltage, .ioIndex = 0},
        {.signal = RenewableSignal::terminalAngle, .ioIndex = 1},
        {.signal = RenewableSignal::frequencyDeviation, .ioIndex = 2},
    }};
    constexpr std::array<RenewablePort, 2> powers{{
        {.signal = RenewableSignal::electricalPower, .ioIndex = 0, .base = RenewableBase::machine},
        {.signal = RenewableSignal::reactivePower, .ioIndex = 1, .base = RenewableBase::machine},
    }};
    constexpr double pi = 3.14159265358979323846;
}  // namespace

GridFormingConverter::GridFormingConverter(Variant variantType, const std::string& name):
    TerminalElectricalModel(name), variant(variantType)
{
    std::copy(defaults.begin(), defaults.end(), parameters.begin());
    m_inputSize = variant == Variant::f2 ? 3 : 2;
}

GridFormingConverter::GridFormingConverter(const std::string& name):
    GridFormingConverter(Variant::cv1, name)
{
}

CoreObject* GridFormingConverter::clone(CoreObject* obj) const
{
    auto* out = cloneBase<GridFormingConverter, TerminalElectricalModel>(this, obj);
    if (out != nullptr) {
        out->variant = variant;
        out->parameters = parameters;
        out->pllName = pllName;
        out->initialActivePower = initialActivePower;
        out->initialReactivePower = initialReactivePower;
        out->initialVoltage = initialVoltage;
    }
    return out == nullptr ? obj : out;
}

std::span<const RenewablePort> GridFormingConverter::inputPorts() const
{
    return variant == Variant::f2 ? std::span<const RenewablePort>{pllTerminals} :
                                    std::span<const RenewablePort>{terminals};
}

std::span<const RenewablePort> GridFormingConverter::outputPorts() const
{
    return powers;
}

std::string_view GridFormingConverter::sourceName(RenewableSignal signal) const
{
    return signal == RenewableSignal::frequencyDeviation ? std::string_view{pllName} :
                                                           std::string_view{};
}

void GridFormingConverter::set(std::string_view param, double val, units::unit unitType)
{
    const auto key = gmlc::utilities::convertToLowerCase(std::string{param});
    for (index_t k = 0; k < count; ++k) {
        if (key == names[k] || (key == "rf" && k == resistance) ||
            (key == "xf" && k == reactance)) {
            if (!std::isfinite(val)) {
                throw InvalidParameterValue("grid-forming converter parameter must be finite");
            }
            parameters[k] = val;
            return;
        }
    }
    TerminalElectricalModel::set(param, val, unitType);
}

void GridFormingConverter::set(std::string_view param, std::string_view val)
{
    if (gmlc::utilities::convertToLowerCase(std::string{param}) == "pll") {
        pllName = std::string{val};
        return;
    }
    GridComponent::set(param, val);
}

double GridFormingConverter::get(std::string_view param, units::unit unitType) const
{
    const auto key = gmlc::utilities::convertToLowerCase(std::string{param});
    for (index_t k = 0; k < count; ++k) {
        if (key == names[k] || (key == "rf" && k == resistance) ||
            (key == "xf" && k == reactance)) {
            return parameters[k];
        }
    }
    return TerminalElectricalModel::get(param, unitType);
}

index_t GridFormingConverter::stateCount() const
{
    if (variant == Variant::cv1) {
        return 8;
    }
    if (variant == Variant::cv2) {
        return 6;
    }
    return variant == Variant::f1 ? 13 : 14;
}

void GridFormingConverter::dynObjectInitializeA(CoreTime time0, std::uint32_t /*flags*/)
{
    const bool cv = variant == Variant::cv1 || variant == Variant::cv2;
    const double z2 = (parameters[resistance] * parameters[resistance]) +
        (parameters[reactance] * parameters[reactance]);
    if (std::any_of(parameters.begin(),
                    parameters.begin() + count,
                    [](double v) { return !std::isfinite(v); }) ||
        parameters[fn] <= 0.0 || z2 <= 0.0 || (cv && parameters[inertia] <= 0.0) ||
        (variant == Variant::cv1 && parameters[tc] <= 0.0) ||
        (variant == Variant::cv2 && (parameters[tid] <= 0.0 || parameters[tiq] <= 0.0)) ||
        (!cv &&
         (parameters[tr] <= 0.0 || parameters[te] <= 0.0 || parameters[tpm] <= 0.0 ||
          parameters[wdrp] <= 0.0 || parameters[pmin] > parameters[pmax] ||
          parameters[qmin] > parameters[qmax] || parameters[dwmin] > parameters[dwmax])) ||
        (variant == Variant::f2 && (parameters[mf] <= 0.0 || pllName.empty()))) {
        throw InvalidParameterValue("grid-forming converter parameters or PLL binding");
    }
    auto& local = offsets.local().local;
    local.algSize = 2;
    local.diffSize = stateCount();
    local.jacSize = (stateCount() + 2) * (stateCount() + 5);
    prevTime = time0;
}

void GridFormingConverter::dynObjectInitializeB(const IOdata& inputs,
                                                const IOdata& desiredOutput,
                                                IOdata& fieldSet)
{
    if (inputs.size() < static_cast<std::size_t>(m_inputSize) || desiredOutput.size() < 2 ||
        !std::isfinite(inputs[0]) || inputs[0] <= 0.0 || !std::isfinite(inputs[1]) ||
        !std::isfinite(desiredOutput[0]) || !std::isfinite(desiredOutput[1])) {
        throw InvalidParameterValue("grid-forming converter terminal or initial P/Q");
    }
    if (variant == Variant::f2 && (!std::isfinite(inputs[2]) || inputs[2] == kNullVal)) {
        throw InvalidParameterValue("REGF2 requires a named PLL frequency deviation");
    }
    const double dCurrent = desiredOutput[0] / inputs[0];
    const double iq = -desiredOutput[1] / inputs[0];
    const double ud = inputs[0] + parameters[resistance] * dCurrent - parameters[reactance] * iq;
    const double uq = parameters[resistance] * iq + parameters[reactance] * dCurrent;
    m_state[0] = desiredOutput[0];
    m_state[1] = desiredOutput[1];
    initialActivePower = desiredOutput[0];
    initialReactivePower = desiredOutput[1];
    initialVoltage = inputs[0];
    if (variant == Variant::cv1 || variant == Variant::cv2) {
        m_state[2] = 0.0;  // dw
        m_state[3] = inputs[1];  // delta
        m_state[4] = dCurrent;  // outer d integral
        m_state[5] = iq;  // outer q integral
        if (variant == Variant::cv1) {
            m_state[6] = parameters[resistance] * dCurrent;
            m_state[7] = parameters[resistance] * iq;
            m_state[8] = ud;
            m_state[9] = uq;
        } else {
            m_state[6] = dCurrent;
            m_state[7] = iq;
        }
    } else {
        m_state[2] = inputs[1];  // delta
        m_state[3] = desiredOutput[0];  // Psen
        m_state[4] = desiredOutput[1];  // Qsen
        m_state[5] = desiredOutput[0];  // Psig
        m_state[6] = desiredOutput[1];  // Qsig
        m_state[7] = desiredOutput[0];  // PIplim integral
        m_state[8] = desiredOutput[1];  // PIqlim integral
        m_state[9] = dCurrent;  // outer d integral
        m_state[10] = iq;  // outer q integral
        m_state[11] = 0.0;  // inner d integral
        m_state[12] = 0.0;  // inner q integral
        m_state[13] = ud;
        m_state[14] = uq;
        if (variant == Variant::f2) {
            m_state[15] = 1.0;
        }
        if (variant == Variant::f3) {
            m_state[15] = inputs[0];
        }
    }
    fieldSet = {};
}

void GridFormingConverter::evaluate(const IOdata& inputs,
                                    const double* alg,
                                    const double* x,
                                    double* powerResidual,
                                    double* rates) const
{
    const bool cv = variant == Variant::cv1 || variant == Variant::cv2;
    const double v = inputs[0];
    const double angle = inputs[1];
    const double delta = x[cv ? 1 : 0];
    const double vd = v * std::cos(delta - angle);
    const double vq = -v * std::sin(delta - angle);
    const double r = parameters[resistance];
    const double react = parameters[reactance];
    const double z2 = r * r + react * react;
    const double ud = variant == Variant::cv2 ? 0.0 : x[cv ? 6 : 11];
    const double uq = variant == Variant::cv2 ? 0.0 : x[cv ? 7 : 12];
    const double dCurrent =
        variant == Variant::cv2 ? x[4] : (r * (ud - vd) + react * (uq - vq)) / z2;
    const double iq = variant == Variant::cv2 ? x[5] : (-react * (ud - vd) + r * (uq - vq)) / z2;
    powerResidual[0] = vd * dCurrent + vq * iq - alg[0];
    powerResidual[1] = -vd * iq + vq * dCurrent - alg[1];
    const double w0 = 2.0 * pi * parameters[fn];
    if (cv) {
        const double pref2 = initialActivePower - parameters[kw] * x[0];
        const double vref2 = initialVoltage + parameters[kv] * (initialReactivePower - alg[1]);
        const double ed = vd - vref2;
        const double eq = vq;
        const double idref = x[2] + parameters[kpvd] * ed;
        const double iqref = x[3] + parameters[kpvq] * eq;
        rates[0] = (pref2 - alg[0] - parameters[damping] * x[0]) / parameters[inertia];
        rates[1] = w0 * x[0];
        rates[2] = parameters[kivd] * ed;
        rates[3] = parameters[kivq] * eq;
        if (variant == Variant::cv2) {
            rates[4] = (idref - dCurrent) / parameters[tid];
            rates[5] = (iqref - iq) / parameters[tiq];
        } else {
            const double eid = dCurrent - idref;
            const double eiq = iq - iqref;
            rates[4] = parameters[kiid] * eid;
            rates[5] = parameters[kiiq] * eiq;
            const double udref = x[4] + parameters[kpid] * eid + vd - iqref * react;
            const double uqref = x[5] + parameters[kpiq] * eiq + vq + idref * react;
            rates[6] = (udref - ud) / parameters[tc];
            rates[7] = (uqref - uq) / parameters[tc];
        }
        return;
    }
    const double perr = x[3] - x[1];
    const double qerr = x[4] - x[2];
    const double plim = x[5] + parameters[kpplim] * perr;
    const double qlim = x[6] + parameters[kpqlim] * qerr;
    const double vref2 =
        variant == Variant::f3 ? x[13] : initialVoltage + parameters[qdrp] * (qlim - x[2]);
    double dw = w0 * parameters[wdrp] * (plim - x[1]);
    if (variant == Variant::f2) {
        dw = w0 * (x[13] - 1.0);
    }
    if (variant == Variant::f3) {
        dw /= vref2 * vref2;
    }
    rates[0] = std::clamp(dw, parameters[dwmin], parameters[dwmax]);
    rates[1] = (alg[0] - x[1]) / parameters[tr];
    rates[2] = (alg[1] - x[2]) / parameters[tr];
    const double pLimitRate = (x[1] - x[3]) / parameters[tpm];
    const double qLimitRate = (x[2] - x[4]) / parameters[tpm];
    rates[3] = ((x[3] >= parameters[pmax] && pLimitRate > 0.0) ||
                (x[3] <= parameters[pmin] && pLimitRate < 0.0)) ?
        0.0 :
        pLimitRate;
    rates[4] = ((x[4] >= parameters[qmax] && qLimitRate > 0.0) ||
                (x[4] <= parameters[qmin] && qLimitRate < 0.0)) ?
        0.0 :
        qLimitRate;
    rates[5] = parameters[kiplim] * perr;
    rates[6] = parameters[kiqlim] * qerr;
    const double ed = vref2 - vd;
    const double eq = -vq;
    const double idref = x[7] + parameters[kpv] * ed;
    const double iqref = x[8] + parameters[kpv] * eq;
    rates[7] = parameters[kiv] * ed;
    rates[8] = parameters[kiv] * eq;
    const double eid = idref - dCurrent;
    const double eiq = iqref - iq;
    rates[9] = parameters[kii] * eid;
    rates[10] = parameters[kii] * eiq;
    const double udref = x[9] + parameters[kpi] * eid + vd + r * dCurrent - react * iq;
    const double uqref = x[10] + parameters[kpi] * eiq + vq + r * iq + react * dCurrent;
    rates[11] = (udref - ud) / parameters[te];
    rates[12] = (uqref - uq) / parameters[te];
    if (variant == Variant::f2) {
        rates[13] = (inputs[2] * parameters[dd] * parameters[wdrp] + 1.0 +
                     parameters[wdrp] * (plim - x[1]) - x[13]) /
            (parameters[mf] * parameters[wdrp]);
    } else if (variant == Variant::f3) {
        const double qd = parameters[qdrp];
        const double denom = 100000000.0 - 2.0 * std::pow(100.0 - 100.0 * qd, 2.0) - 10000.0;
        const double kdvoc = parameters[wdrp] * 400000000.0 / (denom * denom);
        rates[13] = w0 *
            (parameters[wdrp] * (qlim - x[2]) / vref2 +
             vref2 * kdvoc * (initialVoltage + vref2) * (initialVoltage - vref2));
    }
}

void GridFormingConverter::derivative(const IOdata& inputs,
                                      const StateData& stateData,
                                      double deriv[],
                                      const SolverMode& sMode)
{
    if (!hasDifferential(sMode)) {
        return;
    }
    const auto loc = offsets.getLocations(stateData, deriv, sMode, this);
    std::array<double, 2> residual{};
    evaluate(inputs, loc.algStateLoc, loc.diffStateLoc, residual.data(), loc.destDiffLoc);
}

void GridFormingConverter::residual(const IOdata& inputs,
                                    const StateData& stateData,
                                    double resid[],
                                    const SolverMode& sMode)
{
    const auto loc = offsets.getLocations(stateData, resid, sMode, this);
    std::array<double, 14> rates{};
    std::array<double, 2> power{};
    evaluate(inputs, loc.algStateLoc, loc.diffStateLoc, power.data(), rates.data());
    if (hasAlgebraic(sMode)) {
        loc.destLoc[0] = power[0];
        loc.destLoc[1] = power[1];
    }
    if (hasDifferential(sMode)) {
        for (index_t k = 0; k < stateCount(); ++k) {
            loc.destDiffLoc[k] = rates[k] - loc.dstateLoc[k];
        }
    }
}

void GridFormingConverter::algebraicUpdate(const IOdata& inputs,
                                           const StateData& stateData,
                                           double update[],
                                           const SolverMode& sMode,
                                           double /*alpha*/)
{
    if (!hasAlgebraic(sMode)) {
        return;
    }
    const auto loc = offsets.getLocations(stateData, update, sMode, this);
    std::array<double, 14> rates{};
    std::array<double, 2> power{};
    evaluate(inputs, loc.algStateLoc, loc.diffStateLoc, power.data(), rates.data());
    loc.destLoc[0] = loc.algStateLoc[0] + power[0];
    loc.destLoc[1] = loc.algStateLoc[1] + power[1];
}

void GridFormingConverter::timestep(CoreTime time,
                                    const IOdata& inputs,
                                    const SolverMode& /*sMode*/)
{
    const double dt = time - prevTime;
    if (dt < 0.0) {
        throw InvalidParameterValue("grid-forming converter timestep precedes current time");
    }
    std::array<double, 14> rates{};
    std::array<double, 2> power{};
    evaluate(inputs, m_state.data(), m_state.data() + 2, power.data(), rates.data());
    for (index_t k = 0; k < stateCount(); ++k) {
        m_state[2 + k] += dt * rates[k];
    }
    evaluate(inputs, m_state.data(), m_state.data() + 2, power.data(), rates.data());
    m_state[0] += power[0];
    m_state[1] += power[1];
    prevTime = time;
}

void GridFormingConverter::jacobianElements(const IOdata& inputs,
                                            const StateData& stateData,
                                            MatrixData<double>& matrixData,
                                            const IOlocs& inputLocs,
                                            const SolverMode& sMode)
{
    const auto loc = offsets.getLocations(stateData, sMode, this);
    const index_t n = stateCount();
    std::array<double, 2> alg{{loc.algStateLoc[0], loc.algStateLoc[1]}};
    std::array<double, 14> state{};
    std::copy_n(loc.diffStateLoc, n, state.begin());
    std::array<double, 2> basePower{};
    std::array<double, 14> baseRates{};
    evaluate(inputs, alg.data(), state.data(), basePower.data(), baseRates.data());
    const auto addColumn = [&](index_t column,
                               const std::array<double, 2>& p,
                               const std::array<double, 14>& d,
                               double h) {
        if (hasAlgebraic(sMode)) {
            matrixData.assignCheckCol(loc.algOffset, column, (p[0] - basePower[0]) / h);
            matrixData.assignCheckCol(loc.algOffset + 1, column, (p[1] - basePower[1]) / h);
        }
        if (hasDifferential(sMode)) {
            for (index_t row = 0; row < n; ++row) {
                matrixData.assignCheckCol(loc.diffOffset + row,
                                          column,
                                          (d[row] - baseRates[row]) / h);
            }
        }
    };
    for (index_t k = 0; k < 2; ++k) {
        const double h = 1e-7 * std::max(1.0, std::abs(alg[k]));
        alg[k] += h;
        std::array<double, 2> p{};
        std::array<double, 14> d{};
        evaluate(inputs, alg.data(), state.data(), p.data(), d.data());
        addColumn(loc.algOffset + k, p, d, h);
        alg[k] -= h;
        if (hasAlgebraic(sMode)) {
            matrixData.assign(loc.algOffset + k, loc.algOffset + k, -1.0);
        }
    }
    for (index_t k = 0; k < n; ++k) {
        const double h = 1e-7 * std::max(1.0, std::abs(state[k]));
        state[k] += h;
        std::array<double, 2> p{};
        std::array<double, 14> d{};
        evaluate(inputs, alg.data(), state.data(), p.data(), d.data());
        addColumn(loc.diffOffset + k, p, d, h);
        state[k] -= h;
        if (hasDifferential(sMode)) {
            matrixData.assign(loc.diffOffset + k, loc.diffOffset + k, -stateData.cj);
        }
    }
    for (std::size_t k = 0; k < inputs.size(); ++k) {
        IOdata perturbed = inputs;
        const double h = 1e-7 * std::max(1.0, std::abs(inputs[k]));
        perturbed[k] += h;
        std::array<double, 2> p{};
        std::array<double, 14> d{};
        evaluate(perturbed, alg.data(), state.data(), p.data(), d.data());
        addColumn(inputLocs[k], p, d, h);
    }
}

IOdata GridFormingConverter::getOutputs(const IOdata& /*inputs*/,
                                        const StateData& stateData,
                                        const SolverMode& sMode) const
{
    const auto loc = offsets.getLocations(stateData, sMode, this);
    return {loc.algStateLoc[0], loc.algStateLoc[1]};
}

void GridFormingConverter::outputPartialDerivatives(const IOdata& /*inputs*/,
                                                    const StateData& /*stateData*/,
                                                    MatrixData<double>& matrixData,
                                                    const SolverMode& sMode)
{
    const auto alg = offsets.getAlgOffset(sMode);
    matrixData.assign(0, alg, 1.0);
    matrixData.assign(1, alg + 1, 1.0);
}

stringVec GridFormingConverter::localStateNames() const
{
    if (variant == Variant::cv1) {
        return {"Pe", "Qe", "dw", "delta", "PIvd", "PIvq", "PIId", "PIIq", "ud", "uq"};
    }
    if (variant == Variant::cv2) {
        return {"Pe", "Qe", "dw", "delta", "PIvd", "PIvq", "Id", "Iq"};
    }
    stringVec result{"Pe",
                     "Qe",
                     "delta",
                     "Psen",
                     "Qsen",
                     "Psig",
                     "Qsig",
                     "PIplim",
                     "PIqlim",
                     "PIvd",
                     "PIvq",
                     "PIId",
                     "PIIq",
                     "ud",
                     "uq"};
    if (variant == Variant::f2) {
        result.emplace_back("INTw");
    }
    if (variant == Variant::f3) {
        result.emplace_back("vref2");
    }
    return result;
}

#define GRID_FORMING_MODEL_IMPL(Model, kind)                                                       \
    Model::Model(const std::string& name): GridFormingConverter(Variant::kind, name) {}            \
    CoreObject* Model::clone(CoreObject* obj) const                                                \
    {                                                                                              \
        auto* out = cloneBase<Model, GridFormingConverter>(this, obj);                             \
        if (out != nullptr) {                                                                      \
            out->parameters = parameters;                                                          \
            out->pllName = pllName;                                                                \
            out->initialActivePower = initialActivePower;                                          \
            out->initialReactivePower = initialReactivePower;                                      \
            out->initialVoltage = initialVoltage;                                                  \
        }                                                                                          \
        return out == nullptr ? obj : out;                                                         \
    }
GRID_FORMING_MODEL_IMPL(REGCV1, cv1)
GRID_FORMING_MODEL_IMPL(REGCV2, cv2)
GRID_FORMING_MODEL_IMPL(REGF1, f1)
GRID_FORMING_MODEL_IMPL(REGF2, f2)
GRID_FORMING_MODEL_IMPL(REGF3, f3)
#undef GRID_FORMING_MODEL_IMPL

}  // namespace griddyn
