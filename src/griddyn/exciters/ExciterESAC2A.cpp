/*
 * Copyright (c) 2014-2026, Lawrence Livermore National Security
 * See the top-level NOTICE for additional details. All rights reserved.
 * SPDX-License-Identifier: BSD-3-Clause
 */
#include "ExciterESAC2A.h"

#include "core/CoreExceptions.h"
#include "core/CoreObjectTemplates.hpp"
#include "core/ModelParameter.hpp"
#include "utilities/MatrixData.hpp"
#include <algorithm>
#include <cmath>
#include <string>
#include <vector>

namespace griddyn::exciters {
using detail::addSignals;
using detail::applyFunction;
using detail::clampSignal;
using detail::constantSignal;
using detail::divideSignals;
using detail::inputSignal;
using detail::integrationBlocked;
using detail::multiplySignals;
using detail::rectifierFactorSignal;
using detail::scaleSignal;
using detail::stateSignal;
using detail::subtractSignals;
namespace {
    constexpr int ceilingIterations = 60;
    constexpr int rectifierInverseIterations = 40;
    constexpr double initializationTolerance = 1e-7;
}  // namespace

const ModelDescription& ExciterESAC2A::getModelDescription() const
{
    static const auto description = ModelDescription::define<ExciterESAC2A>(
        "ExciterESAC2A",
        &Exciter::getModelDescription(),
        {parameter({"tr"}, &ExciterESAC2A::Tr, units::second, "Voltage measurement time constant"),
         parameter({"tb"}, &ExciterESAC2A::Tb, units::second, "Lead-lag denominator time constant"),
         parameter({"tc"}, &ExciterESAC2A::Tc, units::second, "Lead-lag numerator time constant"),
         parameter({"te"}, &ExciterESAC2A::Te, units::second, "Exciter time constant"),
         parameter({"vamax"}, &ExciterESAC2A::Vamax, units::one, "Vamax upper limit"),
         parameter({"vamin"}, &ExciterESAC2A::Vamin, units::one, "Vamin lower limit"),
         parameter({"kb"}, &ExciterESAC2A::Kb, units::one, "Kb gain or coefficient"),
         parameter({"vfemax"}, &ExciterESAC2A::Vfemax, units::one, "Vfemax upper limit"),
         parameter({"kh"}, &ExciterESAC2A::Kh, units::one, "Kh gain or coefficient"),
         parameter({"kf"}, &ExciterESAC2A::Kf, units::one, "Kf gain or coefficient"),
         parameter({"tf"}, &ExciterESAC2A::Tf, units::second, "Feedback time constant"),
         parameter({"kc"}, &ExciterESAC2A::Kc, units::one, "Kc gain or coefficient"),
         parameter({"kd"}, &ExciterESAC2A::Kd, units::one, "Kd gain or coefficient"),
         parameter({"ke"}, &ExciterESAC2A::Ke, units::one, "Ke gain or coefficient"),
         parameter({"e1"}, &ExciterESAC2A::E1, units::one, "E1 model setting"),
         parameter({"se1"}, &ExciterESAC2A::Se1, units::one, "Se1 model setting"),
         parameter({"e2"}, &ExciterESAC2A::E2, units::one, "E2 model setting"),
         parameter({"se2"}, &ExciterESAC2A::Se2, units::one, "Se2 model setting"),
         flag({"uel_enabled"}, &ExciterESAC2A::underSelectorEnabled, "underSelectorEnabled option"),
         flag({"oel_enabled"}, &ExciterESAC2A::overSelectorEnabled, "overSelectorEnabled option"),
         state("efd", units::one, "Field voltage"),
         state("vc", units::one, "Sensed terminal voltage"),
         state("xll", units::one, "Lead-lag state"),
         state("va", units::one, "Amplifier state"),
         state("ve", units::one, "Exciter voltage"),
         state("vf", units::one, "Feedback state")});
    return description;
}

void ExciterESAC2A::getParameterStrings(stringVec& pstr, ParamStringType pstype) const
{
    const auto& description = ExciterESAC2A::getModelDescription();
    getParamString<ExciterESAC2A, Exciter>(this,
                                           pstr,
                                           description.numericNames(),
                                           description.stringNames(),
                                           description.flagNames(),
                                           pstype);
}

ExciterESAC2A::ExciterESAC2A(const std::string& objName): Exciter(objName)
{
    Ka = 400.0;
    Ta = 0.02;
    Vrmax = 9.0;
    Vrmin = -5.43;
    m_inputSize = exciterInputCount;
}

CoreObject* ExciterESAC2A::clone(CoreObject* obj) const
{
    auto* copy = cloneBase<ExciterESAC2A, Exciter>(this, obj);
    if (copy == nullptr) {
        return obj;
    }
    for (const auto* const parameterName : {"tr",
                                            "tb",
                                            "tc",
                                            "te",
                                            "vamax",
                                            "vamin",
                                            "kb",
                                            "vfemax",
                                            "kh",
                                            "kf",
                                            "tf",
                                            "kc",
                                            "kd",
                                            "ke",
                                            "e1",
                                            "se1",
                                            "e2",
                                            "se2"}) {
        copy->set(parameterName, get(parameterName));
    }
    copy->saturation = saturation;
    copy->underSelectorEnabled = underSelectorEnabled;
    copy->overSelectorEnabled = overSelectorEnabled;
    return copy;
}

ExciterESAC2A::Layout ExciterESAC2A::layout() const
{
    Layout result;
    if (Tr > 0.0) {
        result.voltage = result.count++;
    }
    if (Tb > 0.0) {
        result.lead = result.count++;
    }
    if (Ta > 0.0) {
        result.regulator = result.count++;
    }
    result.exciter = result.count++;
    result.feedback = result.count++;
    return result;
}

void ExciterESAC2A::dynObjectInitializeA(CoreTime /*time0*/, std::uint32_t flags)
{
    setInitialLimitPolicy(flags);
    if (!std::isfinite(Vref)) {
        throw InvalidParameterValue("ESAC2A voltage reference must be finite");
    }
    for (const auto* const parameterName :
         {"tr",     "tb", "tc", "ka", "ta", "te", "vamax", "vamin", "kb",  "vrmax", "vrmin",
          "vfemax", "kh", "kf", "tf", "kc", "kd", "ke",    "e1",    "se1", "e2",    "se2"}) {
        if (!std::isfinite(get(parameterName))) {
            throw InvalidParameterValue("ESAC2A parameters must be finite");
        }
    }
    if (Tr < 0.0 || Tb < 0.0 || Tc < 0.0 || (Tb == 0.0 && Tc != 0.0) || Ta < 0.0 || Te <= 0.0 ||
        Tf <= 0.0 || Ka <= 0.0 || Kb <= 0.0 || Ke <= 0.0 || Kc < 0.0 || Kd < 0.0 || Kh < 0.0 ||
        Vfemax <= 0.0 || Vamax < Vamin || Vrmax < Vrmin || Se1 < 0.0 || Se2 < 0.0) {
        throw InvalidParameterValue("ESAC2A gains, time constants, or limits");
    }
    // OpenIPSL SE is disabled when SE1 is zero. Enabled SE is B*(E-A)^2/E
    // above A; the stored curve here is E*SE(E).
    if (Se1 == 0.0) {
        saturation.setType(utilities::Saturation::SaturationType::NONE);
    } else {
        if (E1 <= 0.0 || E2 <= E1 || Se2 <= 0.0 || E1 * Se1 >= E2 * Se2) {
            throw InvalidParameterValue("ESAC2A saturation points");
        }
        saturation.setType(utilities::Saturation::SaturationType::CUTOFF_QUADRATIC);
        saturation.setParam(E1, E1 * Se1, E2, E2 * Se2);
    }
    offsets.local().local.algSize = 1;
    offsets.local().local.diffSize = layout().count;
    offsets.local().local.algRoots = 4;
    offsets.local().local.jacSize =
        (maximumStates * (maximumStates + exciterInputCount)) + exciterInputCount + 1;
}

ExciterESAC2A::Signal ExciterESAC2A::rectified(const Signal& voltage, const Signal& current) const
{
    if (voltage.value <= 1e-12) {
        return constantSignal<maximumStates>(0.0);
    }
    const auto normalized = divideSignals(scaleSignal(current, Kc), voltage);
    return multiplySignals(voltage, rectifierFactorSignal(normalized));
}

ExciterESAC2A::Signal ExciterESAC2A::saturated(const Signal& voltage) const
{
    return applyFunction(voltage, saturation(voltage.value), saturation.deriv(voltage.value));
}

ExciterESAC2A::Signal ExciterESAC2A::ceiling(const Signal& current) const
{
    const double available = Vfemax - (Kd * current.value);
    if (available <= 0.0) {
        throw InvalidParameterValue("ESAC2A field-current ceiling must be positive");
    }
    // At the ceiling, EFD is itself the rectifier output at VEmax. Solve the
    // implicit OpenIPSL bound VEmax*(KE+SE(EFD(VEmax)))=VFEMAX-KD*Ifd.
    double low = 0.0;
    double high = available / Ke;
    const auto unloadedField = rectified(constantSignal<maximumStates>(high), current);
    if (saturation(unloadedField.value) == 0.0) {
        // Below the saturation cutoff the ceiling is explicit.
        return scaleSignal(subtractSignals(constantSignal<maximumStates>(Vfemax),
                                           scaleSignal(current, Kd)),
                           1.0 / Ke);
    }
    // Sixty bisections resolve the bound to double precision. The Jacobian
    // below differentiates the bound equation, not the bisection branches.
    for (int iteration = 0; iteration < ceilingIterations; ++iteration) {
        const double value = 0.5 * (low + high);
        const auto field = rectified(constantSignal<maximumStates>(value), current);
        const double saturationRatio =
            field.value > 1e-12 ? saturation(field.value) / field.value : 0.0;
        if (value * (Ke + saturationRatio) > available) {
            high = value;
        } else {
            low = value;
        }
    }
    const double value = 0.5 * (low + high);
    const auto field = rectified(stateSignal<maximumStates>(value, 0), current);
    const double saturationRatio =
        field.value > 1e-12 ? saturation(field.value) / field.value : 0.0;
    const double seDerivative =
        field.value > 1e-12 ? (saturation.deriv(field.value) - saturationRatio) / field.value : 0.0;
    const double slope = Ke + saturationRatio + (value * seDerivative * field.state[0]);
    Signal result = constantSignal<maximumStates>(value);
    for (index_t inputIndex = 0; inputIndex < exciterInputCount; ++inputIndex) {
        result.input[inputIndex] =
            ((-Kd * current.input[inputIndex]) - (value * seDerivative * field.input[inputIndex])) /
            slope;
    }
    return result;
}

ExciterESAC2A::Evaluation ExciterESAC2A::evaluate(const IOdata& inputs, const double state[]) const
{
    const auto loc = layout();
    const auto constant = [](double value) { return constantSignal<maximumStates>(value); };
    const auto stateAt = [state](index_t stateIndex) {
        return stateSignal<maximumStates>(state[stateIndex], stateIndex);
    };
    const auto inputAt = [&inputs](index_t inputIndex) {
        return inputSignal<maximumStates>(inputs, inputIndex);
    };
    const auto current = inputAt(exciterXadIfdInLocation);
    Evaluation result;
    result.ceiling = ceiling(current);
    const auto voltage = clampSignal(stateAt(loc.exciter), constant(0.0), result.ceiling);
    result.field = rectified(voltage, current);
    const auto feedback = addSignals(addSignals(scaleSignal(voltage, Ke), saturated(voltage)),
                                     scaleSignal(current, Kd));
    const auto measured = Tr > 0.0 ? stateAt(loc.voltage) : inputAt(exciterVoltageInLocation);
    auto error = subtractSignals(addSignals(constant(Vref + vBias - 1.0),
                                            addSignals(inputAt(exciterVsetInLocation),
                                                       inputAt(exciterVssInLocation))),
                                 measured);
    error = subtractSignals(error,
                            scaleSignal(subtractSignals(feedback, stateAt(loc.feedback)), Kf / Tf));
    const auto lead = Tb > 0.0 ?
        addSignals(stateAt(loc.lead),
                   scaleSignal(subtractSignals(error, stateAt(loc.lead)), Tc / Tb)) :
        error;
    const auto regulatorState = Ta > 0.0 ? stateAt(loc.regulator) : scaleSignal(lead, Ka);
    const auto regulator = clampSignal(regulatorState, Vamin, Vamax);
    result.regulatorDrive = subtractSignals(scaleSignal(lead, Ka), regulatorState);
    auto target = scaleSignal(subtractSignals(regulator, scaleSignal(feedback, Kh)), Kb);
    const auto under = inputAt(exciterVuelInLocation);
    const auto over = inputAt(exciterVoelInLocation);
    if (underSelectorEnabled && target.value < under.value) {
        target = under;
    }
    if (overSelectorEnabled && target.value > over.value) {
        target = over;
    }
    target = clampSignal(target, Vrmin, Vrmax);
    result.exciterDrive = subtractSignals(target, feedback);
    if (Tr > 0.0) {
        result.rates[loc.voltage] =
            scaleSignal(subtractSignals(inputAt(exciterVoltageInLocation), measured), 1.0 / Tr);
    }
    if (Tb > 0.0) {
        result.rates[loc.lead] = scaleSignal(subtractSignals(error, stateAt(loc.lead)), 1.0 / Tb);
    }
    if (Ta > 0.0) {
        result.rates[loc.regulator] = integrationBlocked<maximumStates>(state[loc.regulator],
                                                                        Vamin,
                                                                        Vamax,
                                                                        result.regulatorDrive) ?
            constant(0.0) :
            scaleSignal(result.regulatorDrive, 1.0 / Ta);
    }
    result.rates[loc.exciter] = integrationBlocked<maximumStates>(state[loc.exciter],
                                                                  0.0,
                                                                  result.ceiling.value,
                                                                  result.exciterDrive) ?
        constant(0.0) :
        scaleSignal(result.exciterDrive, 1.0 / Te);
    result.rates[loc.feedback] =
        scaleSignal(subtractSignals(feedback, stateAt(loc.feedback)), 1.0 / Tf);
    return result;
}

void ExciterESAC2A::dynObjectInitializeB(const IOdata& inputs,
                                         const IOdata& desiredOutput,
                                         IOdata& fieldSet)
{
    if (inputs.size() < exciterInputCount || desiredOutput.empty() ||
        !std::isfinite(desiredOutput[0]) || desiredOutput[0] < 0.0 ||
        !std::isfinite(inputs[exciterVoltageInLocation]) ||
        !std::isfinite(inputs[exciterVsetInLocation]) ||
        !std::isfinite(inputs[exciterVssInLocation]) ||
        (underSelectorEnabled && !std::isfinite(inputs[exciterVuelInLocation])) ||
        (overSelectorEnabled && !std::isfinite(inputs[exciterVoelInLocation])) ||
        !std::isfinite(inputs[exciterXadIfdInLocation]) ||
        std::abs(inputs[exciterXadIfdInLocation]) > 1e20) {
        throw InvalidParameterValue(
            "ESAC2A requires finite synchronous-machine inputs and nonnegative EFD");
    }
    const double field = desiredOutput[0];
    const auto current = inputSignal<maximumStates>(inputs, exciterXadIfdInLocation);
    double voltage = std::max(0.01, field + (Kc * std::max(0.0, current.value)));
    // Invert EFD=VE*FEX(KC*Ifd/VE) using the same piecewise analytic slope
    // as the solver callbacks. The final equilibrium check detects failure.
    for (int iteration = 0; iteration < rectifierInverseIterations; ++iteration) {
        const auto output = rectified(stateSignal<maximumStates>(voltage, 0), current);
        if (std::abs(output.value - field) < 1e-12) {
            break;
        }
        if (output.state[0] <= 1e-12) {
            voltage *= 2.0;
            continue;
        }
        voltage = std::max(0.0, voltage - ((output.value - field) / output.state[0]));
    }
    const double feedback = (Ke * voltage) + saturation(voltage) + (Kd * current.value);
    const double regulator = (Kh + (1.0 / Kb)) * feedback;
    if (regulator < Vamin ||
        !adjustInitialUpperLimit(regulator, Vamax, "ESAC2A initial regulator") ||
        feedback < Vrmin ||
        !adjustInitialUpperLimit(feedback, Vrmax, "ESAC2A initial field drive")) {
        throw InvalidParameterValue("ESAC2A initial regulator or field drive outside limits");
    }
    if (voltage > ceiling(current).value + 1e-8) {
        const double saturationRatio = field > 1e-12 ? saturation(field) / field : 0.0;
        if (!adjustInitialUpperLimit((voltage * (Ke + saturationRatio)) + (Kd * current.value),
                                     Vfemax,
                                     "ESAC2A initial field ceiling")) {
            throw InvalidParameterValue("ESAC2A initial field voltage outside ceiling");
        }
    }
    const auto loc = layout();
    auto* state = m_state.data() + 1;
    if (Tr > 0.0) {
        state[loc.voltage] = inputs[exciterVoltageInLocation];
    }
    if (Tb > 0.0) {
        state[loc.lead] = regulator / Ka;
    }
    if (Ta > 0.0) {
        state[loc.regulator] = regulator;
    }
    state[loc.exciter] = voltage;
    state[loc.feedback] = feedback;
    m_state[0] = field;
    vBias = inputs[exciterVoltageInLocation] + (regulator / Ka) - Vref -
        (inputs[exciterVsetInLocation] - 1.0) - inputs[exciterVssInLocation];
    fieldSet.resize(2);
    fieldSet[exciterVsetInLocation] = Vref;
    std::fill(m_dstate_dt.begin(), m_dstate_dt.end(), 0.0);
    const auto initial = evaluate(inputs, state);
    if (!std::isfinite(initial.field.value) ||
        std::abs(initial.field.value - field) > initializationTolerance ||
        std::any_of(initial.rates.begin(),
                    initial.rates.begin() + loc.count,
                    [](const Signal& rate) {
                        return !std::isfinite(rate.value) ||
                            std::abs(rate.value) > initializationTolerance;
                    })) {
        throw InvalidParameterValue("ESAC2A initial equations are inconsistent");
    }
}

void ExciterESAC2A::residual(const IOdata& inputs,
                             const StateData& stateData,
                             double resid[],
                             const SolverMode& sMode)
{
    const auto loc = offsets.getLocations(stateData, resid, sMode, this);
    const auto result = evaluate(inputs, loc.diffStateLoc);
    if (hasAlgebraic(sMode)) {
        loc.destLoc[0] = result.field.value - loc.algStateLoc[0];
    }
    if (hasDifferential(sMode)) {
        for (index_t differentialIndex = 0; differentialIndex < loc.diffSize; ++differentialIndex) {
            loc.destDiffLoc[differentialIndex] =
                result.rates[differentialIndex].value - loc.dstateLoc[differentialIndex];
        }
    }
}

void ExciterESAC2A::derivative(const IOdata& inputs,
                               const StateData& stateData,
                               double deriv[],
                               const SolverMode& sMode)
{
    if (!hasDifferential(sMode)) {
        return;
    }
    const auto loc = offsets.getLocations(stateData, deriv, sMode, this);
    const auto result = evaluate(inputs, loc.diffStateLoc);
    for (index_t differentialIndex = 0; differentialIndex < loc.diffSize; ++differentialIndex) {
        loc.destDiffLoc[differentialIndex] = result.rates[differentialIndex].value;
    }
}

void ExciterESAC2A::algebraicUpdate(const IOdata& inputs,
                                    const StateData& stateData,
                                    double update[],
                                    const SolverMode& sMode,
                                    double /*alpha*/)
{
    if (hasAlgebraic(sMode)) {
        const auto loc = offsets.getLocations(stateData, update, sMode, this);
        loc.destLoc[0] = evaluate(inputs, loc.diffStateLoc).field.value;
    }
}

void ExciterESAC2A::jacobianElements(const IOdata& inputs,
                                     const StateData& stateData,
                                     MatrixData<double>& matrixData,
                                     const IOlocs& inputLocs,
                                     const SolverMode& sMode)
{
    const auto loc = offsets.getLocations(stateData, sMode, this);
    const auto result = evaluate(inputs, loc.diffStateLoc);
    const auto add = [&](index_t row, const Signal& signal, bool includeStates) {
        if (includeStates) {
            for (index_t col = 0; col < loc.diffSize; ++col) {
                if (signal.state[col] != 0.0) {
                    matrixData.assign(row, loc.diffOffset + col, signal.state[col]);
                }
            }
        }
        for (index_t inputIndex = 0; inputIndex < exciterInputCount; ++inputIndex) {
            if (signal.input[inputIndex] != 0.0) {
                matrixData.assignCheckCol(row, inputLocs[inputIndex], signal.input[inputIndex]);
            }
        }
    };
    if (hasAlgebraic(sMode)) {
        matrixData.assign(loc.algOffset, loc.algOffset, -1.0);
        add(loc.algOffset, result.field, !isAlgebraicOnly(sMode));
    }
    if (hasDifferential(sMode)) {
        for (index_t row = 0; row < loc.diffSize; ++row) {
            auto rate = result.rates[row];
            // CVODE has cj=0. A held integrator still needs an explicit
            // diagonal entry so the sparse matrix retains its empty row.
            matrixData.assign(loc.diffOffset + row,
                              loc.diffOffset + row,
                              rate.state[row] - stateData.cj);
            rate.state[row] = 0.0;
            add(loc.diffOffset + row, rate, true);
        }
    }
}

bool ExciterESAC2A::projectLimits(const IOdata& inputs)
{
    const auto loc = layout();
    auto* state = m_state.data() + 1;
    const auto result = evaluate(inputs, state);
    const double oldVoltage = state[loc.exciter];
    state[loc.exciter] = std::clamp(oldVoltage, 0.0, result.ceiling.value);
    bool changed = oldVoltage != state[loc.exciter];
    if (Ta > 0.0) {
        const double oldRegulator = state[loc.regulator];
        state[loc.regulator] =
            std::clamp(oldRegulator, static_cast<double>(Vamin), static_cast<double>(Vamax));
        changed = changed || oldRegulator != state[loc.regulator];
    }
    return changed;
}

void ExciterESAC2A::timestep(CoreTime time, const IOdata& inputs, const SolverMode& /*sMode*/)
{
    derivative(inputs, emptyStateData, m_dstate_dt.data(), cLocalSolverMode);
    for (index_t stateIndex = 0; stateIndex < layout().count; ++stateIndex) {
        m_state[stateIndex + 1] += (time - prevTime) * m_dstate_dt[stateIndex + 1];
    }
    projectLimits(inputs);
    m_state[0] = evaluate(inputs, m_state.data() + 1).field.value;
    prevTime = time;
}

void ExciterESAC2A::rootTest(const IOdata& inputs,
                             const StateData& stateData,
                             double roots[],
                             const SolverMode& sMode)
{
    const auto loc = offsets.getLocations(stateData, sMode, this);
    const auto indices = layout();
    const auto result = evaluate(inputs, loc.diffStateLoc);
    const auto start = offsets.getRootOffset(sMode);
    const double voltage = loc.diffStateLoc[indices.exciter];
    roots[start] = result.ceiling.value - voltage + 1e-4;
    roots[start + 1] = voltage + 1e-4;
    roots[start + 2] = Ta > 0.0 ? Vamax - loc.diffStateLoc[indices.regulator] + 1e-4 : 1.0;
    roots[start + 3] = Ta > 0.0 ? loc.diffStateLoc[indices.regulator] - Vamin + 1e-4 : 1.0;
}

void ExciterESAC2A::rootTrigger(CoreTime /*time*/,
                                const IOdata& inputs,
                                const std::vector<int>& rootMask,
                                const SolverMode& sMode)
{
    const auto start = offsets.getRootOffset(sMode);
    for (index_t rootIndex = 0; rootIndex < 4; ++rootIndex) {
        if (rootMask[start + rootIndex] != 0) {
            projectLimits(inputs);
            alert(this, JAC_COUNT_CHANGE);
            break;
        }
    }
}

ChangeCode ExciterESAC2A::rootCheck(const IOdata& inputs,
                                    const StateData& /*stateData*/,
                                    const SolverMode& /*sMode*/,
                                    CheckLevel /*level*/)
{
    if (projectLimits(inputs)) {
        alert(this, JAC_COUNT_CHANGE);
        return ChangeCode::JACOBIAN_CHANGE;
    }
    return ChangeCode::NO_CHANGE;
}

stringVec ExciterESAC2A::localStateNames() const
{
    stringVec names{"efd"};
    if (Tr > 0.0) {
        names.emplace_back("vc");
    }
    if (Tb > 0.0) {
        names.emplace_back("xll");
    }
    if (Ta > 0.0) {
        names.emplace_back("va");
    }
    names.emplace_back("ve");
    names.emplace_back("vf");
    return names;
}

index_t ExciterESAC2A::findIndex(std::string_view field, const SolverMode& sMode) const
{
    if (field == "efd" || field == "field") {
        return offsets.getAlgOffset(sMode);
    }
    const auto loc = layout();
    index_t index = kNullLocation;
    if (field == "vc") {
        index = loc.voltage;
    } else if (field == "xll") {
        index = loc.lead;
    } else if (field == "va") {
        index = loc.regulator;
    } else if (field == "ve") {
        index = loc.exciter;
    } else if (field == "vf") {
        index = loc.feedback;
    }
    return index == kNullLocation ? kInvalidLocation : offsets.getDiffOffset(sMode) + index;
}

void ExciterESAC2A::set(std::string_view param, double val, units::unit unitType)
{
    if (isSetHandled(
            ExciterESAC2A::getModelDescription().trySetLocal(*this, param, val, unitType))) {
        return;
    }
    Exciter::set(param, val, unitType);
}

double ExciterESAC2A::get(std::string_view param, units::unit unitType) const
{
    if (const auto value =
            ExciterESAC2A::getModelDescription().tryGetLocal(*this, param, unitType)) {
        return *value;
    }
    return Exciter::get(param, unitType);
}

bool ExciterESAC2A::supportsLimiterSignal(ExciterLimiterSignal signal) const
{
    return signal == ExciterLimiterSignal::UNDER ? underSelectorEnabled : overSelectorEnabled;
}

void ExciterESAC2A::set(std::string_view param, std::string_view val)
{
    if (!isSetHandled(ExciterESAC2A::getModelDescription().trySetLocal(*this, param, val))) {
        Exciter::set(param, val);
    }
}

void ExciterESAC2A::setFlag(std::string_view param, bool value)
{
    if (!ExciterESAC2A::getModelDescription().trySetFlagLocal(*this, param, value)) {
        Exciter::setFlag(param, value);
    }
}

bool ExciterESAC2A::getFlag(std::string_view param) const
{
    if (const auto value = ExciterESAC2A::getModelDescription().tryGetFlagLocal(*this, param)) {
        return *value;
    }
    return Exciter::getFlag(param);
}

}  // namespace griddyn::exciters
