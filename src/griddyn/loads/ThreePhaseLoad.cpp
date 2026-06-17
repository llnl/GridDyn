/*
 * Copyright (c) 2014-2026, Lawrence Livermore National Security
 * See the top-level NOTICE for additional details. All rights reserved.
 * SPDX-License-Identifier: BSD-3-Clause
 */

#include "ThreePhaseLoad.h"

#include "../GridBus.h"
#include "core/CoreObjectTemplates.hpp"
#include "utilities/MatrixData.hpp"
#include "utilities/ThreePhaseFunctions.h"
#include <array>
#include <cmath>
#include <complex>
#include <iostream>
#include <string>
#include <vector>

namespace griddyn::loads {
using units::convert;
using units::puA;
using units::puMW;
using units::puV;
using units::rad;
using units::unit;

ThreePhaseLoad::ThreePhaseLoad(const std::string& objName): GridLoad(objName) {}
ThreePhaseLoad::ThreePhaseLoad(double realPower, double reactivePower, const std::string& objName):
    GridLoad(realPower, reactivePower, objName)
{
    Pa = Pb = Pc = realPower / 3.0;
    Qa = Qb = Qc = reactivePower / 3.0;
}

void ThreePhaseLoad::pFlowObjectInitializeA(CoreTime time0, std::uint32_t flags)
{
    if (bus->checkFlag(THREE_PHASE_ONLY)) {
        opFlags[THREE_PHASE_INPUT] = true;
        opFlags[THREE_PHASE_OUTPUT] = true;
    }

    GridLoad::pFlowObjectInitializeA(time0, flags);
}

CoreObject* ThreePhaseLoad::clone(CoreObject* obj) const
{
    auto* nobj = cloneBase<ThreePhaseLoad, GridLoad>(this, obj);
    if (nobj == nullptr) {
        return obj;
    }
    nobj->Pa = Pa;
    nobj->Pb = Pb;
    nobj->Pc = Pc;
    nobj->Qa = Qa;
    nobj->Qb = Qb;
    nobj->Qc = Qc;

    return nobj;
}

void ThreePhaseLoad::setLoad(double level, unit unitType)
{
    setP(convert(level, unitType, puMW, systemBasePower));
    Pa = Pb = Pc = getP() / 3.0;
}

void ThreePhaseLoad::setLoad(double plevel, double qlevel, unit unitType)
{
    setP(convert(plevel, unitType, puMW, systemBasePower));
    setQ(convert(qlevel, unitType, puMW, systemBasePower));
    Pa = Pb = Pc = getP() / 3.0;
    Qa = Qb = Qc = getQ() / 3.0;
}

static constexpr auto locNumStrings =
    std::array<std::string_view, 6>{"pa", "pb", "pc", "qa", "qb", "qc"};

static constexpr std::array<std::string_view, 0> locStrStrings{};

static constexpr auto flagStrings = std::array<std::string_view, 4>{
    "use_abs_angle", "ignore_phase", "three_phase_inputs", "three_phase_outputs"};

void ThreePhaseLoad::getParameterStrings(stringVec& pstr, ParamStringType pstype) const
{
    getParamString<ThreePhaseLoad, GridLoad>(
        this, pstr, locNumStrings, locStrStrings, flagStrings, pstype);
}

void ThreePhaseLoad::setFlag(std::string_view flag, bool val)
{
    if ((flag == "ignore_phase") || (flag == "ignorevoltagephase")) {
        opFlags.set(USE_ABS_ANGLE, !val);
    } else if (flag == "use_abs_angle") {
        opFlags.set(USE_ABS_ANGLE, val);
    } else if ((flag == "three_phase_inputs") || (flag == "three_phase_input")) {
        opFlags.set(THREE_PHASE_INPUT, val);
        m_inputSize = val ? 7 : 3;
    } else if ((flag == "three_phase_outputs") || (flag == "three_phase_output")) {
        opFlags.set(THREE_PHASE_OUTPUT, val);
        m_outputSize = val ? 6 : 3;
    } else {
        GridLoad::setFlag(flag, val);
    }
}

// set properties
void ThreePhaseLoad::set(std::string_view param, std::string_view val)
{
    if (!param.empty() && param[0] != '#') {
        GridLoad::set(param, val);
    }
}

double ThreePhaseLoad::getBaseAngle() const
{
    return (opFlags[USE_ABS_ANGLE]) ? bus->getAngle() : 0.0;
}

double ThreePhaseLoad::get(std::string_view param, unit unitType) const
{
    if (param.length() == 2) {
        switch (param[0]) {
            case 'p':
                return convert(phaseSelector(param[1], Pa, Pb, Pc, kNullVal),
                               puMW,
                               unitType,
                               systemBasePower);
            case 'q':
                return convert(phaseSelector(param[1], Qa, Qb, Qc, kNullVal),
                               puMW,
                               unitType,
                               systemBasePower);
            case 'v': {
                return convert(bus->getVoltage(),
                               puV,
                               unitType,
                               systemBasePower * 1000000.0,
                               localBaseVoltage);
            }
            case 'a': {
                const double angle = getBaseAngle();
                const double phaseAngle = phaseSelector(param[1],
                                                        angle,
                                                        angle + (2.0 * kPI / 3.0),
                                                        angle + (4.0 * kPI / 3.0),
                                                        kNullVal);
                return convert(phaseAngle, rad, unitType, systemBasePower, localBaseVoltage);
            }
            default:
                break;
        }
    } else if (param.length() == 3) {
        if (param.starts_with("vi"))  // get the real part of the voltage
        {
            auto voltageComplex = std::polar(bus->getVoltage(), getBaseAngle());
            voltageComplex =
                voltageComplex * phaseSelector(param[2], alpha0, alpha, alpha2, alpha0);
            return convert(voltageComplex.real(),
                           puV,
                           unitType,
                           systemBasePower * 1000000.0,
                           localBaseVoltage);
        }
        if (param.starts_with("vj"))  // get the reactive part of the voltage
        {
            auto voltageComplex = std::polar(bus->getVoltage(), getBaseAngle());
            voltageComplex =
                voltageComplex * phaseSelector(param[2], alpha0, alpha, alpha2, alpha0);
            return convert(voltageComplex.imag(),
                           puV,
                           unitType,
                           systemBasePower * 1000000.0,
                           localBaseVoltage);
        }
    } else if (param.starts_with("imag")) {
        switch (param[4]) {
            case 'a': {
                auto voltageA = std::polar(bus->getVoltage(), getBaseAngle());
                auto powerA = std::complex<double>(Pa, Qa);
                auto currentA = powerA / voltageA;
                return convert(std::abs(currentA) / multiplier,
                               puA,
                               unitType,
                               systemBasePower * 1000000.0,
                               localBaseVoltage);
            }
            case 'b': {
                auto voltageB = std::polar(bus->getVoltage(), getBaseAngle()) * alpha;
                auto powerB = std::complex<double>(Pb, Qb);
                auto currentB = powerB / voltageB;

                return convert(std::abs(currentB) / multiplier,
                               puA,
                               unitType,
                               systemBasePower * 1000000.0,
                               localBaseVoltage);
            }
            case 'c': {
                auto voltageC = std::polar(bus->getVoltage(), getBaseAngle()) * alpha2;
                auto powerC = std::complex<double>(Pc, Qc);
                auto currentC = powerC / voltageC;

                return convert(std::abs(currentC) / multiplier,
                               puA,
                               unitType,
                               systemBasePower * 1000000.0,
                               localBaseVoltage);
            }
            default:
                break;
        }
    } else if (param.starts_with("iangle")) {
        switch (param[6]) {
            case 'a': {
                auto voltageA = std::polar(bus->getVoltage(), getBaseAngle());
                auto powerA = std::complex<double>(Pa, Qa);
                auto currentA = powerA / voltageA;
                return convert(std::arg(currentA), rad, unitType);
            }
            case 'b': {
                auto voltageB = std::polar(bus->getVoltage(), getBaseAngle()) * alpha;
                auto powerB = std::complex<double>(Pb, Qb);
                auto currentB = powerB / voltageB;
                return convert(std::arg(currentB), rad, unitType);
            }
            case 'c': {
                auto voltageC = std::polar(bus->getVoltage(), getBaseAngle()) * alpha2;
                auto powerC = std::complex<double>(Pc, Qc);
                auto currentC = powerC / voltageC;
                return convert(std::arg(currentC), rad, unitType);
            }
            default:
                break;
        }
    } else if (param == "multiplier") {
        return multiplier;
    }
    return GridLoad::get(param, unitType);
}

void ThreePhaseLoad::set(std::string_view param, double val, unit unitType)
{
    if (param.length() == 2) {
        switch (param[0]) {
            case 'p':
                switch (param[1]) {
                    case 'a':
                        setPa(convert(
                            val * multiplier, unitType, puMW, systemBasePower, localBaseVoltage));
                        break;
                    case 'b':
                        setPb(convert(
                            val * multiplier, unitType, puMW, systemBasePower, localBaseVoltage));
                        break;
                    case 'c':
                        setPc(convert(
                            val * multiplier, unitType, puMW, systemBasePower, localBaseVoltage));
                        break;
                    default:
                        break;
                }
                break;
            case 'q':
                switch (param[1]) {
                    case 'a':
                        setQa(convert(
                            val * multiplier, unitType, puMW, systemBasePower, localBaseVoltage));
                        break;
                    case 'b':
                        setQb(convert(
                            val * multiplier, unitType, puMW, systemBasePower, localBaseVoltage));
                        break;
                    case 'c':
                        setQc(convert(
                            val * multiplier, unitType, puMW, systemBasePower, localBaseVoltage));
                        break;
                    default:
                        break;
                }
                break;
            default:
                GridLoad::set(param, val, unitType);
        }
        return;
    }
    if (param.starts_with("imag")) {
        switch (param[4]) {
            case 'a': {
                auto voltageA = std::polar(bus->getVoltage(), getBaseAngle());
                auto powerA = std::complex<double>(Pa, Qa);
                auto currentA = powerA / voltageA;

                auto newCurrentA = std::polar(
                    convert(val, unitType, puA, systemBasePower * 1000000.0, localBaseVoltage) *
                        multiplier,
                    std::arg(currentA));
                auto newPower = newCurrentA * voltageA;
                setPa(newPower.real());
                setQa(newPower.imag());
            }

            break;
            case 'b': {
                auto voltageB = std::polar(bus->getVoltage(), getBaseAngle()) * alpha;
                auto powerB = std::complex<double>(Pb, Qb);
                auto currentB = powerB / voltageB;

                auto newCurrentB = std::polar(
                    convert(val, unitType, puA, systemBasePower * 1000000.0, localBaseVoltage) *
                        multiplier,
                    std::arg(currentB));
                auto newPower = newCurrentB * voltageB;
                setPb(newPower.real());
                setQb(newPower.imag());
            } break;
            case 'c': {
                auto voltageC = std::polar(bus->getVoltage(), getBaseAngle()) * alpha2;
                auto powerC = std::complex<double>(Pc, Qc);
                auto currentC = powerC / voltageC;

                auto newCurrentC = std::polar(
                    convert(val, unitType, puA, systemBasePower * 1000000.0, localBaseVoltage) *
                        multiplier,
                    std::arg(currentC));
                auto newPower = newCurrentC * voltageC;
                setPc(newPower.real());
                setQc(newPower.imag());
            } break;
            default:
                break;
        }
    } else if (param.starts_with("iangle")) {
        switch (param[6]) {
            case 'a': {
                auto voltageA = std::polar(bus->getVoltage(), getBaseAngle());
                auto powerA = std::complex<double>(Pa, Qa);
                auto currentA = powerA / voltageA;
                auto newCurrentA = std::polar(std::abs(currentA), convert(val, unitType, rad));
                auto newPower = newCurrentA * voltageA;
                setPa(newPower.real());
                setQa(newPower.imag());
            } break;
            case 'b': {
                auto voltageB = std::polar(bus->getVoltage(), getBaseAngle()) * alpha;
                auto powerB = std::complex<double>(Pb, Qb);
                auto currentB = powerB / voltageB;
                auto newCurrentB = std::polar(std::abs(currentB), convert(val, unitType, rad));
                auto newPower = newCurrentB * voltageB;
                setPb(newPower.real());
                setQb(newPower.imag());
            } break;
            case 'c': {
                auto voltageC = std::polar(bus->getVoltage(), getBaseAngle()) * alpha2;
                auto powerC = std::complex<double>(Pc, Qc);
                auto currentC = powerC / voltageC;
                auto newCurrentC = std::polar(std::abs(currentC), convert(val, unitType, rad));
                auto newPower = newCurrentC * voltageC;
                setPc(newPower.real());
                setQc(newPower.imag());
            } break;
            default:
                break;
        }
    } else if (param == "multiplier") {
        multiplier = val;
    } else {
        GridLoad::set(param, val, unitType);
    }
}

IOdata ThreePhaseLoad::getRealPower3Phase(const IOdata& /*inputs*/,
                                          const StateData& /*sD*/,
                                          const SolverMode& /*sMode*/,
                                          PhaseType type) const
{
    return getRealPower3Phase(type);
}
IOdata ThreePhaseLoad::getReactivePower3Phase(const IOdata& /*inputs*/,
                                              const StateData& /*sD*/,
                                              const SolverMode& /*sMode*/,
                                              PhaseType type) const
{
    return getReactivePower3Phase(type);
}
/** get the 3 phase real output power that based on the given voltage
@return the real power consumed by the load*/
IOdata ThreePhaseLoad::getRealPower3Phase(const IOdata& /*V*/, PhaseType type) const
{
    return getRealPower3Phase(type);
}
/** get the 3 phase reactive output power that based on the given voltage
@return the reactive power consumed by the load*/
IOdata ThreePhaseLoad::getReactivePower3Phase(const IOdata& /*V*/, PhaseType type) const
{
    return getReactivePower3Phase(type);
}
IOdata ThreePhaseLoad::getRealPower3Phase(PhaseType type) const
{
    switch (type) {
        case PhaseType::ABC:
        default:
            return {Pa, Pb, Pc};
        case PhaseType::PNZ:
            return abcToPnzR<IOdata>({Pa, Pb, Pc}, {Qa, Qb, Qc});
    }
}
IOdata ThreePhaseLoad::getReactivePower3Phase(PhaseType type) const
{
    switch (type) {
        case PhaseType::ABC:
        default:
            return {Qa, Qb, Qc};
        case PhaseType::PNZ:
            return abcToPnzI<IOdata>({Pa, Pb, Pc}, {Qa, Qb, Qc});
    }
}

void ThreePhaseLoad::setPa(double val)
{
    Pa = val;
    setP(Pa + Pb + Pc);
}
void ThreePhaseLoad::setPb(double val)
{
    Pb = val;
    setP(Pa + Pb + Pc);
}
void ThreePhaseLoad::setPc(double val)
{
    Pc = val;
    setP(Pa + Pb + Pc);
}
void ThreePhaseLoad::setQa(double val)
{
    Qa = val;
    setQ(Qa + Qb + Qc);
}
void ThreePhaseLoad::setQb(double val)
{
    Qb = val;
    setQ(Qa + Qb + Qc);
}
void ThreePhaseLoad::setQc(double val)
{
    Qc = val;
    setQ(Qa + Qb + Qc);
}

static const std::vector<stringVec> INPUT_NAMES_STR3PHASE{
    {"voltage_a", "v_a", "volt_a", "vmag_a"},
    {"angle_a", "vangle_a", "angle_a", "ang_a", "vang_a"},
    {"voltage_b", "v_b", "volt_b", "vmag_b"},
    {"angle_b", "vangle_b", "angle_b", "ang_b", "vang_b"},
    {"voltage_c", "v_c", "volt_c", "vmag_c"},
    {"angle_c", "vangle_c", "angle_c", "ang_c", "vang_c"},
    {"frequency", "freq", "f", "omega"},
};

const std::vector<stringVec>& ThreePhaseLoad::inputNames() const
{
    if (opFlags[THREE_PHASE_INPUT]) {
        return INPUT_NAMES_STR3PHASE;
    }
    return GridLoad::inputNames();
}

static const std::vector<stringVec> OUTPUT_NAMES_STR3PHASE{
    {"p_a", "power_a", "realpower_a", "real_a"},
    {"q_a", "reactive_a", "reactivepower_a"},
    {"p_b", "power_b", "realpower_b", "real_b"},
    {"q_b", "reactive_b", "reactivepower_b"},
    {"p_c", "power_c", "realpower_c", "real_c"},
    {"q_c", "reactive_c", "reactivepower_c"},
};

const std::vector<stringVec>& ThreePhaseLoad::outputNames() const
{
    if (opFlags[THREE_PHASE_OUTPUT]) {
        return OUTPUT_NAMES_STR3PHASE;
    }
    return GridLoad::outputNames();
}

}  // namespace griddyn::loads
