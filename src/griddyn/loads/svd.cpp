/*
 * Copyright (c) 2014-2020, Lawrence Livermore National Security
 * See the top-level NOTICE for additional details. All rights reserved.
 * SPDX-License-Identifier: BSD-3-Clause
 */

#include "svd.h"

#include "core/coreObjectTemplates.hpp"
#include "core/objectFactoryTemplates.hpp"
#include "gmlc/utilities/stringConversion.h"
#include <cmath>
#include <string>
#include <vector>

namespace griddyn {
namespace loads {
    static typeFactory<svd> svdld("load",
                                  stringVec{"svd", "switched shunt", "switchedshunt", "ssd"});

    using gmlc::utilities::convertToLowerCase;
    using gmlc::utilities::numeric_conversion;
    using units::convert;
    using units::puMW;
    using units::puV;
    using units::unit;

    svd::svd(const std::string& objName): rampLoad(objName) {}
    svd::svd(double rP, double rQ, const std::string& objName): rampLoad(rP, rQ, objName)
    {
        opFlags.set(adjustable_Q);
    }

    svd::~svd() = default;
    coreObject* svd::clone(coreObject* obj) const
    {
        auto ld = cloneBase<svd, rampLoad>(this, obj);
        if (ld == nullptr) {
            return obj;
        }

        ld->Qmin = Qmin;
        ld->Qmax = Qmax;
        ld->Vmin = Vmin;
        ld->Vmax = Vmax;

        ld->Qlow = Qlow;
        ld->Qhigh = Qhigh;
        ld->currentStep = currentStep;
        ld->stepCount = stepCount;
        ld->Cblocks = Cblocks;
        ld->participation = participation;
        return ld;
    }

    void svd::setControlBus(gridBus* cBus)
    {
        if (cBus != nullptr) {
            controlBus = cBus;
        }
    }

    void svd::setLoad(double level, unit unitType)
    {
        double dlevel = convert(level, unitType, puMW, systemBasePower);
        int setLevel = checkSetting(dlevel);
        if (setLevel >= 0) {
            setYq(dlevel);
        }
    }

    void svd::setLoad(double Plevel, double Qlevel, unit unitType)
    {
        setup(convert(Plevel, unitType, puMW, systemBasePower));
        double dlevel = convert(Qlevel, unitType, puMW, systemBasePower);
        int setLevel = checkSetting(dlevel);
        if (setLevel >= 0) {
            setYq(dlevel);
        }
    }

    int svd::checkSetting(double level)
    {
        if (level == 0.0) {
            return 0;
        }
        if (opFlags[continuous_flag]) {
            return ((level >= Qlow) && (level <= Qhigh)) ? 1 : -1;
        }

        int setting = 0;
        double totalQ = Qlow;
        if (!opFlags[reverse_control_flag]) {
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
            if (opFlags[reverse_toggled_flag]) {
                opFlags.flip(reverse_control_flag);
                opFlags.reset(reverse_toggled_flag);
                LOG_WARNING("unable to match requested level");
            } else {
                opFlags.flip(reverse_control_flag);
                opFlags.set(reverse_toggled_flag);
                return checkSetting(level);
            }
        }
        return setting;
    }

    void svd::updateSetting(int step)
    {
        if (step <= 0) {
            currentStep = checkSetting(step);
            setYq(Qlow);
        } else if (step >= stepCount) {
            currentStep = stepCount;
            setYq(Qhigh);
        } else {
            double qlevel = Qlow;
            if (opFlags[reverse_control_flag]) {
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

    void svd::pFlowObjectInitializeA(coreTime time0, std::uint32_t flags)
    {
        if (opFlags[continuous_flag]) {
            if (!opFlags[locked_flag]) {
                opFlags.set(has_pflow_states);
                opFlags.set(has_powerflow_adjustments);
            }
        } else {
            if (!opFlags[locked_flag]) {
                opFlags.set(has_powerflow_adjustments);
            }
        }
        return zipLoad::pFlowObjectInitializeA(time0, flags);
    }

    void svd::dynObjectInitializeA(coreTime time0, std::uint32_t flags)
    {
        return zipLoad::dynObjectInitializeA(time0, flags);
    }

    void svd::dynObjectInitializeB(const IOdata& /*inputs*/,
                                   const IOdata& /*desiredOutput*/,
                                   IOdata& /*fieldSet*/)
    {
    }

    void svd::setState(coreTime /*time*/,
                       const double /*state*/[],
                       const double /*dstate_dt*/[],
                       const solverMode& /*sMode*/)
    {
    }

    void svd::guessState(coreTime /*time*/,
                         double /*state*/[],
                         double /*dstate_dt*/[],
                         const solverMode& /*sMode*/)
    {
    }

    change_code svd::powerFlowAdjust(const IOdata& /*inputs */,
                                     std::uint32_t /*flags*/,
                                     check_level_t /*level*/)
    {
        return change_code::no_change;
    }

    void svd::reset(reset_levels /*level*/) {}
    // for identifying which variables are algebraic vs differential
    void svd::getVariableType(double /*sdata*/[], const solverMode& /*sMode*/) {}
    void svd::set(const std::string& param, const std::string& val)
    {
        if ((param == "blocks") || (param == "block")) {
            auto bin = gmlc::utilities::stringOps::splitline(val);
            for (size_t kk = 0; kk < bin.size() - 1; ++kk) {
                auto cnt = numeric_conversion<int>(bin[kk], 0);
                double bsize = numeric_conversion(bin[kk + 1], 0.0);
                if (cnt > 0) {
                    addBlock(cnt, bsize);
                }
            }
        } else if (param == "mode") {
            auto v2 = convertToLowerCase(val);
            if ((v2 == "manual") || (v2 == "locked")) {
                opFlags.set(locked_flag);
            }
            if ((v2 == "cont") || (v2 == "continuous")) {
                opFlags.set(continuous_flag, true);
                opFlags.reset(locked_flag);
            } else if ((v2 == "stepped") || (v2 == "discrete")) {
                opFlags.reset(continuous_flag);
                opFlags.reset(locked_flag);
            }
        } else if (param == "control") {
            auto v2 = convertToLowerCase(val);
            if (v2 == "reactive") {
                opFlags.set(reactive_control_flag, true);
            }
        } else {
            zipLoad::set(param, val);
        }
    }
    void svd::set(const std::string& param, double val, unit unitType)
    {
        if (param == "qlow") {
            Qlow = convert(val, unitType, puMW, systemBasePower, localBaseVoltage);
        } else if (param == "qhigh") {
            Qhigh = convert(val, unitType, puMW, systemBasePower, localBaseVoltage);
        } else if (param == "qmin") {
            Qmin = convert(val, unitType, puMW, systemBasePower, localBaseVoltage);
        }
        if (param == "qmax") {
            Qmax = convert(val, unitType, puMW, systemBasePower, localBaseVoltage);
        } else if (param == "vmax") {
            Vmax = convert(val, unitType, puV, systemBasePower, localBaseVoltage);
        } else if (param == "vmin") {
            Vmin = convert(val, unitType, puV, systemBasePower, localBaseVoltage);
        } else if (param == "yq") {
            double temp = convert(val, unitType, puMW, systemBasePower, localBaseVoltage);
            setLoad(temp);
        } else if (param == "step") {
            updateSetting(static_cast<int>(val));
        } else if (param == "participation") {
            participation = val;
        } else if (param == "block") {
            if (Cblocks.size() == 1) {
                if (Cblocks[0].second == 0) {
                    Cblocks[0].second =
                        convert(val, unitType, puMW, systemBasePower, localBaseVoltage);
                    Qhigh = Qlow + Cblocks[0].first * Cblocks[0].second;
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
                    addBlock(static_cast<int>(val), 0.0f);
                } else {
                    Cblocks[0].first = static_cast<int>(val);
                    Qhigh = Qlow + Cblocks[0].first * Cblocks[0].second;
                    stepCount = Cblocks[0].first;
                }
            }
        } else if (param.substr(0, 5) == "block") {
        } else if (param.substr(0, 5) == "count") {
        } else {
            zipLoad::set(param, val, unitType);
        }
    }

    void svd::addBlock(int steps, double Qstep, units::unit unitType)
    {
        Qstep = units::convert(Qstep, unitType, units::puMW, systemBasePower);
        Cblocks.emplace_back(steps, Qstep);
        Qhigh += steps * Qstep;
        stepCount += steps;
    }

    void svd::residual(const IOdata& /*inputs*/,
                       const stateData& /*sD*/,
                       double /*resid*/[],
                       const solverMode& /*sMode*/)
    {
    }

    void svd::derivative(const IOdata& /*inputs*/,
                         const stateData& /*sD*/,
                         double /*deriv*/[],
                         const solverMode& /*sMode*/)
    {
    }

    void svd::outputPartialDerivatives(const IOdata& /*inputs*/,
                                       const stateData& /*sD*/,
                                       matrixData<double>& /*md*/,
                                       const solverMode& /*sMode*/)
    {
    }

    void svd::jacobianElements(const IOdata& /*inputs*/,
                               const stateData& /*sD*/,
                               matrixData<double>& /*md*/,
                               const IOlocs& /*inputLocs*/,
                               const solverMode& /*sMode*/)
    {
    }
    void svd::getStateName(stringVec& /*stNames*/,
                           const solverMode& /*sMode*/,
                           const std::string& /*prefix*/) const
    {
    }

    void svd::timestep(coreTime /*time*/, const IOdata& /*inputs*/, const solverMode& /*sMode*/) {}
    void svd::rootTest(const IOdata& /*inputs*/,
                       const stateData& /*sD*/,
                       double /*roots*/[],
                       const solverMode& /*sMode*/)
    {
    }

    void svd::rootTrigger(coreTime /*time*/,
                          const IOdata& /*inputs*/,
                          const std::vector<int>& /*rootMask*/,
                          const solverMode& /*sMode*/)
    {
    }

    change_code svd::rootCheck(const IOdata& /*inputs*/,
                               const stateData& /*sD*/,
                               const solverMode& /*sMode*/,
                               check_level_t /*level*/)
    {
        return change_code::no_change;
    }
}  // namespace loads
}  // namespace griddyn
