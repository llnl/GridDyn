/*
 * Copyright (c) 2014-2026, Lawrence Livermore National Security
 * See the top-level NOTICE for additional details. All rights reserved.
 * SPDX-License-Identifier: BSD-3-Clause
 */

#include "Svd.h"

#include "core/CoreObjectTemplates.hpp"
#include "core/ObjectFactoryTemplates.hpp"
#include "gmlc/utilities/stringConversion.h"
#include <cmath>
#include <string>
#include <utility>
#include <vector>

namespace griddyn::loads {
static TypeFactory<Svd>
    gSvdld("load",
           std::to_array<std::string_view>({"Svd", "switched shunt", "switchedshunt", "ssd"}));

using gmlc::utilities::convertToLowerCase;
using gmlc::utilities::numeric_conversion;
using units::convert;
using units::puMW;
using units::puV;
using units::unit;

Svd::Svd(const std::string& objName): RampLoad(objName) {}
Svd::Svd(double realPower, double reactivePower, const std::string& objName):
    RampLoad(realPower, reactivePower, objName)
{
    opFlags.set(ADJUSTABLE_Q);
}

Svd::~Svd() = default;
CoreObject* Svd::clone(CoreObject* obj) const
{
    auto* load = cloneBase<Svd, RampLoad>(this, obj);
    if (load == nullptr) {
        return obj;
    }

    load->Qmin = Qmin;
    load->Qmax = Qmax;
    load->Vmin = Vmin;
    load->Vmax = Vmax;

    load->Qlow = Qlow;
    load->Qhigh = Qhigh;
    load->currentStep = currentStep;
    load->stepCount = stepCount;
    load->Cblocks = Cblocks;
    load->participation = participation;
    return load;
}

void Svd::setControlBus(GridBus* cBus)
{
    if (cBus != nullptr) {
        controlBus = cBus;
    }
}

void Svd::setLoad(double level, unit unitType)
{
    const double dlevel = convert(level, unitType, puMW, systemBasePower);
    const int setLevel = checkSetting(dlevel);
    if (setLevel >= 0) {
        setYq(dlevel);
    }
}

void Svd::setLoad(double plevel, double qlevel, unit unitType)
{
    setup(convert(plevel, unitType, puMW, systemBasePower));
    const double dlevel = convert(qlevel, unitType, puMW, systemBasePower);
    const int setLevel = checkSetting(dlevel);
    if (setLevel >= 0) {
        setYq(dlevel);
    }
}

int Svd::checkSetting(double level)
{
    if (level == 0.0) {
        return 0;
    }
    if (opFlags[CONTINUOUS_FLAG]) {
        return ((level >= Qlow) && (level <= Qhigh)) ? 1 : -1;
    }

    while (true) {
        int setting = 0;
        double totalQ = Qlow;
        if (!opFlags[REVERSE_CONTROL_FLAG]) {
            auto block = Cblocks.begin();
            while (std::abs(totalQ) < std::abs(level)) {
                for (int kk = 0; kk < (*block).first; ++kk) {
                    totalQ += (*block).second;
                    ++setting;
                    if (std::abs(totalQ - level) < 0.00001) {
                        return setting;
                    }
                }
                ++block;
                if (block == Cblocks.end()) {
                    break;
                }
            }
        } else {
            auto block = Cblocks.rbegin();
            while (std::abs(totalQ) < std::abs(level)) {
                for (int kk = 0; kk < (*block).first; ++kk) {
                    totalQ += (*block).second;
                    ++setting;
                    if (std::abs(totalQ - level) < 0.00001) {
                        return setting;
                    }
                }
                ++block;
                if (block == Cblocks.rend()) {
                    break;
                }
            }
        }
        if (std::abs(totalQ) > std::abs(level)) {
            if (opFlags[REVERSE_TOGGLED_FLAG]) {
                opFlags.flip(REVERSE_CONTROL_FLAG);
                opFlags.reset(REVERSE_TOGGLED_FLAG);
                logging::warning(this, "unable to match requested level");
            } else {
                opFlags.flip(REVERSE_CONTROL_FLAG);
                opFlags.set(REVERSE_TOGGLED_FLAG);
                continue;
            }
        }
        return setting;
    }
}

void Svd::updateSetting(int step)
{
    if (step <= 0) {
        currentStep = checkSetting(step);
        setYq(Qlow);
    } else if (step >= stepCount) {
        currentStep = stepCount;
        setYq(Qhigh);
    } else {
        double qlevel = Qlow;
        if (opFlags[REVERSE_CONTROL_FLAG]) {
            auto block = Cblocks.begin();
            int scount = 0;

            while (step > scount + (*block).first) {
                scount += (*block).first;
                qlevel += (*block).second;
                ++block;
                if (block == Cblocks.end()) {
                    break;
                }
            }
            qlevel += (step - scount) * (*block).second;
        } else {
            auto block = Cblocks.rbegin();
            int scount = 0;
            while (step > scount + (*block).first) {
                scount += (*block).first;
                qlevel += (*block).second;
                ++block;
                if (block == Cblocks.rend()) {
                    break;
                }
            }
            qlevel += (step - scount) * (*block).second;
        }
        setYq(qlevel);
        currentStep = step;
    }
}

void Svd::pFlowObjectInitializeA(CoreTime time0, std::uint32_t flags)
{
    if (opFlags[CONTINUOUS_FLAG]) {
        if (!opFlags[LOCKED_FLAG]) {
            opFlags.set(HAS_PFLOW_STATES);
            opFlags.set(HAS_POWERFLOW_ADJUSTMENTS);
        }
    } else {
        if (!opFlags[LOCKED_FLAG]) {
            opFlags.set(HAS_POWERFLOW_ADJUSTMENTS);
        }
    }
    ZipLoad::pFlowObjectInitializeA(time0, flags);
}

void Svd::dynObjectInitializeA(CoreTime time0, std::uint32_t flags)
{
    ZipLoad::dynObjectInitializeA(time0, flags);
}

void Svd::dynObjectInitializeB(const IOdata& /*inputs*/,
                               const IOdata& /*desiredOutput*/,
                               IOdata& /*fieldSet*/)
{
}

void Svd::setState(CoreTime /*time*/,
                   const double /*state*/[],
                   const double /*dstate_dt*/[],
                   const SolverMode& /*sMode*/)
{
}

void Svd::guessState(CoreTime /*time*/,
                     double /*state*/[],
                     double /*dstate_dt*/[],
                     const SolverMode& /*sMode*/)
{
}

ChangeCode
    Svd::powerFlowAdjust(const IOdata& /*inputs */, std::uint32_t /*flags*/, CheckLevel /*level*/)
{
    return ChangeCode::NO_CHANGE;
}

void Svd::reset(ResetLevels /*level*/) {}
// for identifying which variables are algebraic vs differential
void Svd::getVariableType(double /*sdata*/[], const SolverMode& /*sMode*/) {}
void Svd::set(std::string_view param, std::string_view val)
{
    if ((param == "blocks") || (param == "block")) {
        auto bin = gmlc::utilities::stringOps::splitline(val);
        for (size_t kk = 0; kk < bin.size() - 1; ++kk) {
            auto cnt = numeric_conversion<int>(bin[kk], 0);
            const double bsize = numeric_conversion(bin[kk + 1], 0.0);
            if (cnt > 0) {
                addBlock(cnt, bsize);
            }
        }
    } else if (param == "mode") {
        const auto lowerValue = convertToLowerCase(val);
        if ((lowerValue == "manual") || (lowerValue == "locked")) {
            opFlags.set(LOCKED_FLAG);
        }
        if ((lowerValue == "cont") || (lowerValue == "continuous")) {
            opFlags.set(CONTINUOUS_FLAG, true);
            opFlags.reset(LOCKED_FLAG);
        } else if ((lowerValue == "stepped") || (lowerValue == "discrete")) {
            opFlags.reset(CONTINUOUS_FLAG);
            opFlags.reset(LOCKED_FLAG);
        }
    } else if (param == "control") {
        const auto lowerValue = convertToLowerCase(val);
        if (lowerValue == "reactive") {
            opFlags.set(REACTIVE_CONTROL_FLAG, true);
        }
    } else {
        ZipLoad::set(param, val);
    }
}
void Svd::set(std::string_view param, double val, unit unitType)
{
    if (param == "qlow") {
        Qlow = convert(val, unitType, puMW, systemBasePower, localBaseVoltage);
    } else if (param == "qhigh") {
        Qhigh = convert(val, unitType, puMW, systemBasePower, localBaseVoltage);
    } else if (param == "qmin") {
        Qmin = convert(val, unitType, puMW, systemBasePower, localBaseVoltage);
    } else if (param == "qmax") {
        Qmax = convert(val, unitType, puMW, systemBasePower, localBaseVoltage);
    } else if (param == "vmax") {
        Vmax = convert(val, unitType, puV, systemBasePower, localBaseVoltage);
    } else if (param == "vmin") {
        Vmin = convert(val, unitType, puV, systemBasePower, localBaseVoltage);
    } else if (param == "yq") {
        const double convertedValue =
            convert(val, unitType, puMW, systemBasePower, localBaseVoltage);
        setLoad(convertedValue);
    } else if (param == "step") {
        updateSetting(static_cast<int>(val));
    } else if (param == "participation") {
        participation = val;
    } else if (param == "block") {
        if (Cblocks.size() == 1) {
            if (Cblocks[0].second == 0) {
                Cblocks[0].second = convert(val, unitType, puMW, systemBasePower, localBaseVoltage);
                Qhigh = Qlow + (Cblocks[0].first * Cblocks[0].second);
                stepCount = Cblocks[0].first;
            } else {
                addBlock(1, val, unitType);
            }
        } else {
            addBlock(1, val, unitType);
        }
    } else if (param == "count") {
        if (Cblocks.size() < 2) {
            if (Cblocks.empty()) {
                addBlock(static_cast<int>(val), 0.0);
            } else {
                Cblocks[0].first = static_cast<int>(val);
                Qhigh = Qlow + (Cblocks[0].first * Cblocks[0].second);
                stepCount = Cblocks[0].first;
            }
        }
    } else if (param.starts_with("block") || param.starts_with("count")) {
    } else {
        ZipLoad::set(param, val, unitType);
    }
}

void Svd::addBlock(int steps, double qstep, units::unit unitType)
{
    const double convertedStep = units::convert(qstep, unitType, units::puMW, systemBasePower);
    Cblocks.emplace_back(steps, convertedStep);
    Qhigh += steps * convertedStep;
    stepCount += steps;
}

void Svd::residual(const IOdata& /*inputs*/,
                   const StateData& /*sD*/,
                   double /*resid*/[],
                   const SolverMode& /*sMode*/)
{
}

void Svd::derivative(const IOdata& /*inputs*/,
                     const StateData& /*sD*/,
                     double /*deriv*/[],
                     const SolverMode& /*sMode*/)
{
}

void Svd::outputPartialDerivatives(const IOdata& /*inputs*/,
                                   const StateData& /*sD*/,
                                   MatrixData<double>& /*md*/,
                                   const SolverMode& /*sMode*/)
{
}

void Svd::jacobianElements(const IOdata& /*inputs*/,
                           const StateData& /*sD*/,
                           MatrixData<double>& /*md*/,
                           const IOlocs& /*inputLocs*/,
                           const SolverMode& /*sMode*/)
{
}
void Svd::getStateName(stringVec& /*stNames*/,
                       const SolverMode& /*sMode*/,
                       const std::string& /*prefix*/) const
{
}

void Svd::timestep(CoreTime /*time*/, const IOdata& /*inputs*/, const SolverMode& /*sMode*/) {}
void Svd::rootTest(const IOdata& /*inputs*/,
                   const StateData& /*sD*/,
                   double /*roots*/[],
                   const SolverMode& /*sMode*/)
{
}

void Svd::rootTrigger(CoreTime /*time*/,
                      const IOdata& /*inputs*/,
                      const std::vector<int>& /*rootMask*/,
                      const SolverMode& /*sMode*/)
{
}

ChangeCode Svd::rootCheck(const IOdata& /*inputs*/,
                          const StateData& /*sD*/,
                          const SolverMode& /*sMode*/,
                          CheckLevel /*level*/)
{
    return ChangeCode::NO_CHANGE;
}
}  // namespace griddyn::loads
