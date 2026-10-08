/*
 * Copyright (c) 2014-2026, Lawrence Livermore National Security
 * See the top-level NOTICE for additional details. All rights reserved.
 * SPDX-License-Identifier: BSD-3-Clause
 */

#include "WECCMotor3.h"

#include "../GridBus.h"
#include "core/CoreExceptions.h"
#include "core/CoreObjectTemplates.hpp"
#include "core/ObjectFactoryTemplates.hpp"
#include <algorithm>
#include <array>
#include <cmath>
#include <string>
#include <utility>
#include <vector>

namespace griddyn::loads {
namespace {
    TypeFactory<WECCMotor3> gWeccMotor3Factory(
        "load",
        std::to_array<std::string_view>({"weccmotor3", "motorabc", "cmp_mo3_2"}));

    constexpr std::array<std::string_view, 18> numericParameters{"lfm",
                                                                 "ls",
                                                                 "lp",
                                                                 "lpp",
                                                                 "tpo",
                                                                 "tppo",
                                                                 "etrq",
                                                                 "tmo",
                                                                 "vtr1",
                                                                 "ttr1",
                                                                 "ftr1",
                                                                 "vrc1",
                                                                 "trc1",
                                                                 "vtr2",
                                                                 "ttr2",
                                                                 "ftr2",
                                                                 "vrc2",
                                                                 "trc2"};
    constexpr std::array<std::string_view, 0> stringParameters{};
    constexpr std::array<std::string_view, 0> flagParameters{};

    template<std::size_t N>
    bool solveLinear(std::array<std::array<double, N + 1>, N>& equations,
                     std::array<double, N>& solution)
    {
        for (std::size_t column = 0; column < solution.size(); ++column) {
            auto pivot = column;
            for (std::size_t row = column + 1; row < solution.size(); ++row) {
                if (std::abs(equations[row][column]) > std::abs(equations[pivot][column])) {
                    pivot = row;
                }
            }
            if (std::abs(equations[pivot][column]) < 1e-14) {
                return false;
            }
            std::swap(equations[column], equations[pivot]);
            const double diagonal = equations[column][column];
            for (std::size_t entry = column; entry <= N; ++entry) {
                equations[column][entry] /= diagonal;
            }
            for (std::size_t row = 0; row < solution.size(); ++row) {
                if (row == column) {
                    continue;
                }
                const double factor = equations[row][column];
                for (std::size_t entry = column; entry <= N; ++entry) {
                    equations[row][entry] -= factor * equations[column][entry];
                }
            }
        }
        for (std::size_t row = 0; row < solution.size(); ++row) {
            solution[row] = equations[row][N];
        }
        return true;
    }
}  // namespace

WECCMotor3::WECCMotor3(const std::string& objName): CIM6(objName)
{
    for (std::size_t stage = 0; stage < MotorProtectionGroups::stageCount; ++stage) {
        configureProtectionStage(stage);
    }
}

CoreObject* WECCMotor3::clone(CoreObject* obj) const
{
    auto* copied = cloneBase<WECCMotor3, CIM6>(this, obj);
    if (copied != nullptr) {
        copied->loadFactor = loadFactor;
        copied->torqueExponent = torqueExponent;
        copied->torqueNominal = torqueNominal;
        copied->torqueNominalSet = torqueNominalSet;
        copied->explicitMotorBase = explicitMotorBase;
        copied->synchronousReactance = synchronousReactance;
        copied->transientReactance = transientReactance;
        copied->subtransientReactance = subtransientReactance;
        copied->transientTimeConstant = transientTimeConstant;
        copied->subtransientTimeConstant = subtransientTimeConstant;
        copied->protectionParameters = protectionParameters;
        copied->protectionGroups = protectionGroups;
    }
    return (copied == nullptr) ? obj : copied;
}

void WECCMotor3::getParameterStrings(stringVec& pstr, ParamStringType pstype) const
{
    getParamString<WECCMotor3, CIM6>(
        this, pstr, numericParameters, stringParameters, flagParameters, pstype);
}

void WECCMotor3::set(std::string_view param, std::string_view val)
{
    CIM6::set(param, val);
}

void WECCMotor3::set(std::string_view param, double val, units::unit unitType)
{
    if ((param == "lfm") || (param == "LFm")) {
        if (!std::isfinite(val) || (val <= 0.0)) {
            throw InvalidParameterValue("WECC motor LFm must be positive");
        }
        loadFactor = val;
    } else if ((param == "ls") || (param == "Ls")) {
        if (!std::isfinite(val) || (val <= 0.0)) {
            throw InvalidParameterValue("WECC motor Ls must be positive");
        }
        synchronousReactance = val;
    } else if ((param == "lp") || (param == "Lp")) {
        if (!std::isfinite(val) || (val <= 0.0)) {
            throw InvalidParameterValue("WECC motor Lp must be positive");
        }
        transientReactance = val;
    } else if ((param == "lpp") || (param == "Lpp")) {
        if (!std::isfinite(val) || (val <= 0.0)) {
            throw InvalidParameterValue("WECC motor Lpp must be positive");
        }
        subtransientReactance = val;
    } else if ((param == "tpo") || (param == "Tpo")) {
        if (!std::isfinite(val) || (val <= 0.0)) {
            throw InvalidParameterValue("WECC motor Tpo must be positive");
        }
        transientTimeConstant = val;
    } else if ((param == "tppo") || (param == "Tppo")) {
        if (!std::isfinite(val) || (val < 0.0)) {
            throw InvalidParameterValue("WECC motor Tppo must be non-negative");
        }
        subtransientTimeConstant = val;
    } else if ((param == "etrq") || (param == "Etrq")) {
        if (!std::isfinite(val) || (val < 0.0)) {
            throw InvalidParameterValue("WECC motor Etrq must be non-negative");
        }
        torqueExponent = val;
    } else if ((param == "tmo") || (param == "Tmo")) {
        if (!std::isfinite(val) || (val < 0.0)) {
            throw InvalidParameterValue("WECC motor Tmo must be non-negative");
        }
        torqueNominal = val;
        torqueNominalSet = true;
    } else if ((param == "base") || (param == "mbase") || (param == "rating")) {
        explicitMotorBase = true;
        CIM6::set(param, val, unitType);
    } else if (param == "p") {
        CIM6::set(param, val, unitType);
        GridLoad::set(param, val, unitType);
    } else if (param == "vtr1" || param == "ttr1" || param == "ftr1" || param == "vrc1" ||
               param == "trc1") {
        setProtectionParameter(0, param, val);
    } else if (param == "vtr2" || param == "ttr2" || param == "ftr2" || param == "vrc2" ||
               param == "trc2") {
        setProtectionParameter(1, param, val);
    } else if ((param == "rs") || (param == "ra") || (param == "Rs")) {
        if (!std::isfinite(val) || (val < 0.0)) {
            throw InvalidParameterValue("WECC motor stator resistance must be non-negative");
        }
        CIM6::set("rs", val, unitType);
    } else {
        CIM6::set(param, val, unitType);
    }
}

double WECCMotor3::get(std::string_view param, units::unit unitType) const
{
    if (param == "lfm" || param == "LFm") {
        return loadFactor;
    }
    if (param == "ls" || param == "Ls") {
        return synchronousReactance;
    }
    if (param == "lp" || param == "Lp") {
        return transientReactance;
    }
    if (param == "lpp" || param == "Lpp") {
        return subtransientReactance;
    }
    if (param == "tpo" || param == "Tpo") {
        return transientTimeConstant;
    }
    if (param == "tppo" || param == "Tppo") {
        return subtransientTimeConstant;
    }
    if (param == "etrq" || param == "Etrq") {
        return torqueExponent;
    }
    if (param == "tmo" || param == "Tmo") {
        return torqueNominal;
    }
    for (std::size_t stage = 0; stage < MotorProtectionGroups::stageCount; ++stage) {
        const auto& values = protectionParameters[stage];
        const auto suffix = (stage == 0) ? '1' : '2';
        if (param == std::string("vtr") + suffix) {
            return values.tripVoltage;
        }
        if (param == std::string("ttr") + suffix) {
            return values.tripDelay;
        }
        if (param == std::string("ftr") + suffix) {
            return values.tripFraction;
        }
        if (param == std::string("vrc") + suffix) {
            return values.reconnectVoltage;
        }
        if (param == std::string("trc") + suffix) {
            return values.reconnectDelay;
        }
    }
    return CIM6::get(param, unitType);
}

void WECCMotor3::setProtectionParameter(std::size_t stage, std::string_view parameter, double value)
{
    auto& settings = protectionParameters[stage];
    if (parameter.starts_with("vtr")) {
        settings.tripVoltage = value;
    } else if (parameter.starts_with("ttr")) {
        settings.tripDelay = value;
    } else if (parameter.starts_with("ftr")) {
        settings.tripFraction = value;
    } else if (parameter.starts_with("vrc")) {
        settings.reconnectVoltage = value;
    } else if (parameter.starts_with("trc")) {
        settings.reconnectDelay = value;
    }
    configureProtectionStage(stage);
}

void WECCMotor3::configureProtectionStage(std::size_t stage)
{
    const auto& settings = protectionParameters[stage];
    protectionGroups.setStage(stage,
                              settings.tripVoltage,
                              settings.tripDelay,
                              settings.tripFraction,
                              settings.reconnectVoltage,
                              settings.reconnectDelay);
}

void WECCMotor3::pFlowObjectInitializeA(CoreTime time0, std::uint32_t flags)
{
    const double allocatedPower = getP();
    if (!std::isfinite(allocatedPower) || (allocatedPower <= 0.0)) {
        throw InvalidParameterValue("WECC motor requires positive allocated active power");
    }
    Pmot = allocatedPower;
    if (!explicitMotorBase) {
        mBase = allocatedPower * systemBasePower / loadFactor;
    }
    if (!std::isfinite(mBase) || (mBase <= 0.0)) {
        throw InvalidParameterValue("WECC motor requires a positive MVA base");
    }
    directX0 = synchronousReactance;
    directXp = transientReactance;
    const bool singleCage =
        (subtransientTimeConstant == 0.0) || (subtransientReactance == transientReactance);
    directXpp = singleCage ? transientReactance : subtransientReactance;
    directT0p = transientTimeConstant;
    directT0pp = singleCage ? 1.0e-7 : subtransientTimeConstant;
    useDirectMachineParameters = true;
    CIM6::pFlowObjectInitializeA(time0, flags);
}

void WECCMotor3::converge()
{
    const double voltage = bus->getVoltage();
    const double angle = bus->getAngle();
    const double voltageReal = -voltage * Vcontrol * std::sin(angle);
    const double voltageImaginary = voltage * Vcontrol * std::cos(angle);
    const double targetPower = getP() / scale;
    // For a fixed slip the six electrical variables form a linear circuit.
    // The motor Q follows from that circuit; it cannot be assigned from the
    // composite load's original aggregate power factor.
    auto electricalState = [&](double slip, std::array<double, 6>& state) {
        const double frequencySlip = systemBaseFrequency * slip;
        double sat = 0.0;
        for (int iteration = 0; iteration < 20; ++iteration) {
            std::array<std::array<double, 7>, 6> equations{{
                {{xpp, r, 0.0, 0.0, 0.0, 1.0, voltageImaginary}},
                {{r, -xpp, 0.0, 0.0, 1.0, 0.0, voltageReal}},
                {{0.0, x0 - xp, 1.0, -T0p * frequencySlip, 0.0, -sat, 0.0}},
                {{-(x0 - xp), 0.0, T0p * frequencySlip, 1.0, sat, 0.0, 0.0}},
                {{0.0, -(xp - xpp), 1.0, T0pp * frequencySlip, -1.0, -T0pp * frequencySlip, 0.0}},
                {{xp - xpp, 0.0, -T0pp * frequencySlip, 1.0, T0pp * frequencySlip, -1.0, 0.0}},
            }};
            if (!solveLinear(equations, state)) {
                return false;
            }
            const double updated = saturationFactor(state[4], state[5]);
            if (!std::isfinite(updated)) {
                return false;
            }
            if (std::abs(updated - sat) < 1e-11) {
                return true;
            }
            sat = updated;
        }
        return false;
    };
    std::array<double, 6> state{};
    double lower = 1e-8;
    if (!electricalState(lower, state)) {
        throw InvalidParameterValue("WECC motor electrical initialization failed");
    }
    double lowerError = (voltageReal * state[0]) + (voltageImaginary * state[1]) - targetPower;
    double upper = lower;
    bool bracketed = false;
    for (int step = 0; step < 140; ++step) {
        upper = std::min(1.0, std::max(upper * 1.3, upper + 1e-5));
        if (!electricalState(upper, state)) {
            break;
        }
        const double upperError =
            (voltageReal * state[0]) + (voltageImaginary * state[1]) - targetPower;
        if ((lowerError <= 0.0) && (upperError >= 0.0)) {
            bracketed = true;
            break;
        }
        lower = upper;
        lowerError = upperError;
        if (upper >= 1.0) {
            break;
        }
    }
    if (!bracketed) {
        throw InvalidParameterValue("WECC motor cannot attain the allocated active power");
    }
    for (int iteration = 0; iteration < 60; ++iteration) {
        const double middle = 0.5 * (lower + upper);
        if (!electricalState(middle, state)) {
            throw InvalidParameterValue("WECC motor electrical initialization failed");
        }
        const double error = (voltageReal * state[0]) + (voltageImaginary * state[1]) - targetPower;
        if (std::abs(error) < 1e-12) {
            lower = upper = middle;
            break;
        }
        if (error < 0.0) {
            lower = middle;
        } else {
            upper = middle;
        }
    }
    const double slip = 0.5 * (lower + upper);
    if (!electricalState(slip, state)) {
        throw InvalidParameterValue("WECC motor electrical initialization failed");
    }
    m_state[0] = state[0];
    m_state[1] = state[1];
    m_state[2] = slip;
    m_state[3] = state[2];
    m_state[4] = state[3];
    m_state[5] = state[4];
    m_state[6] = state[5];

    if (!torqueNominalSet) {
        const double speed = std::max(1e-6, 1.0 - slip);
        const double electricalTorque = (state[4] * state[0]) + (state[5] * state[1]);
        torqueNominal = std::max(0.0, electricalTorque) / std::pow(speed, torqueExponent);
    }
}

void WECCMotor3::dynObjectInitializeA(CoreTime time0, std::uint32_t flags)
{
    CIM6::dynObjectInitializeA(time0, flags);
    protectionGroups.initialize(time0, bus->getVoltage());
}

double WECCMotor3::motorOutputFraction() const
{
    return protectionGroups.onlineFraction();
}

double WECCMotor3::mechPower(double slip) const
{
    const double speed = std::max(0.0, 1.0 - slip);
    return torqueNominal * std::pow(speed, torqueExponent);
}

double WECCMotor3::dmechds(double slip) const
{
    const double speed = std::max(0.0, 1.0 - slip);
    if ((torqueExponent == 0.0) || (speed == 0.0)) {
        return 0.0;
    }
    return -torqueNominal * torqueExponent * std::pow(speed, torqueExponent - 1.0);
}

double WECCMotor3::mechanicalTorqueAtSpeed(double speed) const
{
    return torqueNominal * std::pow(std::max(0.0, speed), torqueExponent);
}

std::pair<count_t, count_t> WECCMotor3::LocalRootCount(const SolverMode& sMode) const
{
    auto rootCounts = CIM6::LocalRootCount(sMode);
    rootCounts.second += static_cast<count_t>(MotorProtectionGroups::rootCount);
    return rootCounts;
}

void WECCMotor3::rootTest(const IOdata& inputs,
                          const StateData& stateData,
                          double roots[],
                          const SolverMode& sMode)
{
    CIM6::rootTest(inputs, stateData, roots, sMode);
    const auto baseRoots = CIM6::LocalRootCount(sMode);
    const auto protectionRootOffset =
        offsets.getRootOffset(sMode) + baseRoots.first + baseRoots.second;
    protectionGroups.rootTest(stateData.time,
                              inputs[VOLTAGE_IN_LOCATION],
                              roots + protectionRootOffset);
}

void WECCMotor3::rootTrigger(CoreTime time,
                             const IOdata& inputs,
                             const std::vector<int>& rootMask,
                             const SolverMode& sMode)
{
    CIM6::rootTrigger(time, inputs, rootMask, sMode);
    const auto baseRoots = CIM6::LocalRootCount(sMode);
    const auto protectionRootOffset =
        offsets.getRootOffset(sMode) + baseRoots.first + baseRoots.second;
    for (std::size_t root = 0; root < MotorProtectionGroups::rootCount; ++root) {
        if ((protectionRootOffset + root < rootMask.size()) &&
            (rootMask[protectionRootOffset + root] != 0)) {
            if (protectionGroups.rootTrigger(root, time, inputs[VOLTAGE_IN_LOCATION])) {
                alert(this, POTENTIAL_FAULT_CHANGE);
            }
        }
    }
}

}  // namespace griddyn::loads
