/*
 * Copyright (c) 2026, Lawrence Livermore National Security
 * See the top-level NOTICE for additional details. All rights reserved.
 * SPDX-License-Identifier: BSD-3-Clause
 */

#include "MotorLoadDQ.h"

#include "../GridBus.h"
#include "core/CoreExceptions.h"
#include "core/CoreObjectTemplates.hpp"
#include "core/ModelDescription.h"
#include "core/ModelParameter.hpp"
#include "utilities/MatrixData.hpp"
#include <algorithm>
#include <array>
#include <cmath>
#include <complex>
#include <string>

namespace griddyn::loads {

MotorLoadDQ::MotorLoadDQ(const std::string& objName): GridLoad(objName)
{
    opFlags.set(HAS_DYN_STATES);
    m_state.resize(5, 0.0);
    m_dstate_dt.resize(5, 0.0);
}

CoreObject* MotorLoadDQ::clone(CoreObject* obj) const
{
    auto* copied = cloneBase<MotorLoadDQ, GridLoad>(this, obj);
    if (copied != nullptr) {
        copied->mRs = mRs;
        copied->mRr = mRr;
        copied->mXls = mXls;
        copied->mXlr = mXlr;
        copied->mXm = mXm;
        copied->mInertia = mInertia;
        copied->mRating = mRating;
        copied->mLoadFactor = mLoadFactor;
        copied->mScale = mScale;
        copied->mTorqueConstant = mTorqueConstant;
        copied->mTorqueLinear = mTorqueLinear;
        copied->mTorqueQuadratic = mTorqueQuadratic;
        copied->mDamping = mDamping;
        copied->mLoadTorque = mLoadTorque;
        copied->mInitialSpeed = mInitialSpeed;
        copied->mTorqueSpecified = mTorqueSpecified;
        copied->mStarting = mStarting;
        copied->m_state = m_state;
        copied->m_dstate_dt = m_dstate_dt;
    }
    return copied == nullptr ? obj : copied;
}

namespace {
    inline constexpr auto positiveValue = [](const double& value) {
        return std::isfinite(value) && value > 0.0;
    };
    inline constexpr auto nonnegativeValue = [](const double& value) {
        return std::isfinite(value) && value >= 0.0;
    };
    inline constexpr auto loadFactorValue = [](const double& value) {
        return std::isfinite(value) && value > 0.0 && value <= 1.0;
    };
    inline constexpr auto initialSpeedValue = [](const double& value) {
        return std::isfinite(value) && value >= 0.0 && value < 1.0;
    };
}  // namespace

const ModelDescription& MotorLoadDQ::getModelDescription() const
{
    const auto motorState =
        [](std::string_view stateName, index_t slot, std::string_view description) {
            return ModelField({stateName},
                              units::one,
                              description,
                              {.mLocalIndex = [slot](const CoreObject&, index_t) { return slot; },
                               .mSolverIndex =
                                   [slot](const CoreObject& object, const SolverMode& mode) {
                                       if (isDynamic(mode) && !hasDifferential(mode)) {
                                           return kInvalidLocation;
                                       }
                                       const auto& layout =
                                           static_cast<const MotorLoadDQ&>(object).getOffsets(mode);
                                       const auto baseIndex =
                                           isDynamic(mode) ? layout.diffOffset : layout.algOffset;
                                       return baseIndex == kNullLocation ?
                                           kNullLocation :
                                           stateSlot(baseIndex, slot);
                                   }});
        };
    static const auto description = ModelDescription::define<MotorLoadDQ>(
        "MotorLoadDQ",
        &GridLoad::getModelDescription(),
        {parameter({"rs"}, &MotorLoadDQ::mRs, units::one, "Stator resistance", positiveValue),
         parameter({"rr"}, &MotorLoadDQ::mRr, units::one, "Rotor resistance", positiveValue),
         parameter(
             {"xls"}, &MotorLoadDQ::mXls, units::one, "Stator leakage reactance", positiveValue),
         parameter(
             {"xlr"}, &MotorLoadDQ::mXlr, units::one, "Rotor leakage reactance", positiveValue),
         parameter({"xm"}, &MotorLoadDQ::mXm, units::one, "Magnetizing reactance", positiveValue),
         parameter({"h"}, &MotorLoadDQ::mInertia, units::second, "Inertia constant", positiveValue),
         parameter({"rating"},
                   &MotorLoadDQ::mRating,
                   units::MW,
                   "Motor rating; zero uses load_factor",
                   nonnegativeValue),
         parameter({"load_factor"},
                   &MotorLoadDQ::mLoadFactor,
                   units::one,
                   "Load factor in (0, 1]",
                   loadFactorValue),
         parameter({"torque_constant"},
                   &MotorLoadDQ::mTorqueConstant,
                   units::one,
                   "Constant torque coefficient",
                   nonnegativeValue),
         parameter({"torque_linear"},
                   &MotorLoadDQ::mTorqueLinear,
                   units::one,
                   "Linear torque coefficient",
                   nonnegativeValue),
         parameter({"torque_quadratic"},
                   &MotorLoadDQ::mTorqueQuadratic,
                   units::one,
                   "Quadratic torque coefficient",
                   nonnegativeValue),
         parameter({"damping"},
                   &MotorLoadDQ::mDamping,
                   units::one,
                   "Speed-proportional damping",
                   nonnegativeValue),
         parameter({"initial_speed"},
                   &MotorLoadDQ::mInitialSpeed,
                   units::one,
                   "Initial speed in [0, 1)",
                   initialSpeedValue),
         command({"initialization"},
                 units::defunit,
                 "Select running or starting initialization",
                 ModelFieldType::STRING),
         // Setting load_torque also sets the explicit torque latch.
         command({"load_torque"}, units::one, "Load torque at rated speed"),
         localState("loadTorque", &MotorLoadDQ::mLoadTorque, "Applied load torque", units::one),
         localState("torqueSpecified",
                    &MotorLoadDQ::mTorqueSpecified,
                    "Explicit load torque latch"),
         localState(
             "scale", &MotorLoadDQ::mScale, "Initialized motor-to-system base ratio", units::one),
         motorState("psi_sd", 0, "Stator direct-axis flux"),
         motorState("psi_sq", 1, "Stator quadrature-axis flux"),
         motorState("psi_rd", 2, "Rotor direct-axis flux"),
         motorState("psi_rq", 3, "Rotor quadrature-axis flux"),
         motorState("speed", 4, "Rotor speed relative to synchronous speed")});
    return description;
}

void MotorLoadDQ::set(std::string_view param, std::string_view val)
{
    if (param == "initialization") {
        if (val != "running" && val != "starting") {
            throw InvalidParameterValue("motor_dq initialization must be running or starting");
        }
        mStarting = val == "starting";
    } else {
        GridLoad::set(param, val);
    }
}

void MotorLoadDQ::set(std::string_view param, double val, units::unit unitType)
{
    if (param == "rs" || param == "rr" || param == "xls" || param == "xlr" || param == "xm" ||
        param == "h") {
        if (!std::isfinite(val) || val <= 0.0) {
            throw InvalidParameterValue(std::string{"motor_dq "} + std::string{param} +
                                        " must be positive and finite");
        }
        if (param == "rs") {
            mRs = val;
        }
        if (param == "rr") {
            mRr = val;
        }
        if (param == "xls") {
            mXls = val;
        }
        if (param == "xlr") {
            mXlr = val;
        }
        if (param == "xm") {
            mXm = val;
        }
        if (param == "h") {
            mInertia = val;
        }
    } else if (param == "rating") {
        const double rating =
            units::convert(val, unitType, units::MW, systemBasePower, localBaseVoltage);
        if (!std::isfinite(rating) || rating <= 0.0) {
            throw InvalidParameterValue("motor_dq rating must be positive and finite");
        }
        mRating = rating;
    } else if (param == "load_factor") {
        if (!std::isfinite(val) || val <= 0.0 || val > 1.0) {
            throw InvalidParameterValue("motor_dq load_factor must be in (0, 1]");
        }
        mLoadFactor = val;
    } else if (param == "torque_constant" || param == "torque_linear" ||
               param == "torque_quadratic" || param == "damping" || param == "load_torque") {
        if (!std::isfinite(val) || val < 0.0) {
            throw InvalidParameterValue(
                "motor_dq torque coefficients must be nonnegative and finite");
        }
        if (param == "torque_constant") {
            mTorqueConstant = val;
        }
        if (param == "torque_linear") {
            mTorqueLinear = val;
        }
        if (param == "torque_quadratic") {
            mTorqueQuadratic = val;
        }
        if (param == "damping") {
            mDamping = val;
        }
        if (param == "load_torque") {
            mLoadTorque = val;
            mTorqueSpecified = true;
        }
    } else if (param == "initial_speed") {
        if (!std::isfinite(val) || val < 0.0 || val >= 1.0) {
            throw InvalidParameterValue("motor_dq initial_speed must be in [0, 1)");
        }
        mInitialSpeed = val;
    } else {
        GridLoad::set(param, val, unitType);
    }
}

double MotorLoadDQ::get(std::string_view param, units::unit unitType) const
{
    if (param == "rs") {
        return mRs;
    }
    if (param == "rr") {
        return mRr;
    }
    if (param == "xls") {
        return mXls;
    }
    if (param == "xlr") {
        return mXlr;
    }
    if (param == "xm") {
        return mXm;
    }
    if (param == "h") {
        return mInertia;
    }
    if (param == "rating") {
        return units::convert(mRating, units::MW, unitType, systemBasePower, localBaseVoltage);
    }
    if (param == "load_factor") {
        return mLoadFactor;
    }
    if (param == "torque_constant") {
        return mTorqueConstant;
    }
    if (param == "torque_linear") {
        return mTorqueLinear;
    }
    if (param == "torque_quadratic") {
        return mTorqueQuadratic;
    }
    if (param == "damping") {
        return mDamping;
    }
    if (param == "load_torque") {
        return mLoadTorque;
    }
    if (param == "initial_speed") {
        return mInitialSpeed;
    }
    if (param == "speed") {
        return m_state[4];
    }
    if (param == "slip") {
        return 1.0 - m_state[4];
    }
    if (param == "scale") {
        return mScale;
    }
    return GridLoad::get(param, unitType);
}

void MotorLoadDQ::validateParameters() const
{
    if (!(systemBasePower > 0.0) || !(systemBaseFrequency > 0.0) ||
        !std::isfinite(systemBasePower) || !std::isfinite(systemBaseFrequency)) {
        throw InvalidParameterValue("motor_dq requires positive system power and frequency bases");
    }
    if (mTorqueConstant + mTorqueLinear + mTorqueQuadratic <= 0.0) {
        throw InvalidParameterValue("motor_dq torque curve must have a positive coefficient");
    }
    const double determinant = (mXls * mXlr) + (mXm * (mXls + mXlr));
    if (!std::isfinite(determinant) || determinant <= 0.0) {
        throw InvalidParameterValue("motor_dq inductance matrix must be nonsingular and finite");
    }
}

std::array<double, 4> MotorLoadDQ::currents(const double* state) const
{
    const double determinant = (mXls * mXlr) + (mXm * (mXls + mXlr));
    const double statorCoefficient = (mXlr + mXm) / determinant;
    const double mutualCoefficient = -mXm / determinant;
    const double rotorCoefficient = (mXls + mXm) / determinant;
    return {(statorCoefficient * state[0]) + (mutualCoefficient * state[2]),
            (statorCoefficient * state[1]) + (mutualCoefficient * state[3]),
            (mutualCoefficient * state[0]) + (rotorCoefficient * state[2]),
            (mutualCoefficient * state[1]) + (rotorCoefficient * state[3])};
}

double MotorLoadDQ::torqueShape(double speed) const
{
    return mTorqueConstant + (speed * (mTorqueLinear + (speed * mTorqueQuadratic)));
}

double MotorLoadDQ::electricalTorque(const double* state) const
{
    const auto current = currents(state);
    return (state[0] * current[1]) - (state[1] * current[0]);
}

MotorLoadDQ::MotorState MotorLoadDQ::rates(const IOdata& inputs, const double* state) const
{
    const auto current = currents(state);
    const double voltage = inputs[VOLTAGE_IN_LOCATION];
    const double angle = inputs[ANGLE_IN_LOCATION];
    const double slip = 1.0 - state[4];
    return {systemBaseFrequency * ((voltage * std::cos(angle)) - (mRs * current[0]) + state[1]),
            systemBaseFrequency * ((voltage * std::sin(angle)) - (mRs * current[1]) - state[0]),
            systemBaseFrequency * ((-mRr * current[2]) + (slip * state[3])),
            systemBaseFrequency * ((-mRr * current[3]) - (slip * state[2])),
            (electricalTorque(state) - (mLoadTorque * torqueShape(state[4])) -
             (mDamping * state[4])) /
                (2.0 * mInertia)};
}

MotorLoadDQ::MotorState
    MotorLoadDQ::electricalEquilibrium(double voltage, double angle, double slip) const
{
    // Independent steady-state single-cage equivalent circuit: stator series
    // impedance followed by the parallel magnetizing and rotor branches.
    using Complex = std::complex<double>;
    const Complex magnetizing{0.0, mXm};
    const Complex rotor{mRr, slip * mXlr};
    const Complex parallel = magnetizing * rotor / (rotor + slip * magnetizing);
    const Complex terminal = std::polar(voltage, angle);
    const Complex statorCurrent = terminal / (Complex{mRs, mXls} + parallel);
    const Complex airGapVoltage = terminal - Complex{mRs, mXls} * statorCurrent;
    const Complex rotorCurrent = -airGapVoltage * slip / rotor;
    const Complex statorFlux = (mXls + mXm) * statorCurrent + mXm * rotorCurrent;
    const Complex rotorFlux = mXm * statorCurrent + (mXlr + mXm) * rotorCurrent;
    return {statorFlux.real(), statorFlux.imag(), rotorFlux.real(), rotorFlux.imag(), 1.0 - slip};
}

void MotorLoadDQ::initializeRunningPoint(double voltage, double angle)
{
    const double targetPower = getP() / mScale;
    auto errorAtSlip = [&](double slip) {
        const auto state = electricalEquilibrium(voltage, angle, slip);
        if (mTorqueSpecified) {
            return electricalTorque(state.data()) - (mLoadTorque * torqueShape(state[4])) -
                (mDamping * state[4]);
        }
        const auto current = currents(state.data());
        return (voltage * ((std::cos(angle) * current[0]) + (std::sin(angle) * current[1]))) -
            targetPower;
    };
    double lower = 0.0;
    double lowerError = errorAtSlip(lower);
    double upper = 1e-8;
    bool bracketed = false;
    for (int step = 0; step < 180; ++step) {
        const double upperError = errorAtSlip(upper);
        if (std::isfinite(lowerError) && std::isfinite(upperError) && lowerError <= 0.0 &&
            upperError >= 0.0) {
            bracketed = true;
            break;
        }
        lower = upper;
        lowerError = upperError;
        if (upper >= 1.0) {
            break;
        }
        upper = std::min(1.0, (upper * 1.15) + 1e-7);
    }
    if (!bracketed) {
        throw InvalidParameterValue("motor_dq cannot attain a loaded running operating point");
    }
    for (int iteration = 0; iteration < 70; ++iteration) {
        const double middle = 0.5 * (lower + upper);
        if (errorAtSlip(middle) < 0.0) {
            lower = middle;
        } else {
            upper = middle;
        }
    }
    const auto state = electricalEquilibrium(voltage, angle, 0.5 * (lower + upper));
    std::copy(state.begin(), state.end(), m_state.begin());
    if (!mTorqueSpecified) {
        mLoadTorque =
            (electricalTorque(state.data()) - (mDamping * state[4])) / torqueShape(state[4]);
        if (!std::isfinite(mLoadTorque) || mLoadTorque < 0.0) {
            throw InvalidParameterValue(
                "motor_dq allocated power cannot supply the specified damping");
        }
    }
}

void MotorLoadDQ::pFlowObjectInitializeA(CoreTime time0, std::uint32_t flags)
{
    validateParameters();
    GridLoad::pFlowObjectInitializeA(time0, flags);
    if (mRating == 0.0) {
        if (!std::isfinite(getP()) || getP() <= 0.0) {
            throw InvalidParameterValue(
                "motor_dq needs positive allocated P or an explicit rating");
        }
        mScale = getP() / mLoadFactor;
    } else {
        mScale = mRating / systemBasePower;
    }
    if (!std::isfinite(mScale) || mScale <= 0.0 || bus == nullptr ||
        !std::isfinite(bus->getVoltage()) || bus->getVoltage() <= 0.0 ||
        !std::isfinite(bus->getAngle())) {
        throw InvalidParameterValue(
            "motor_dq requires a finite positive rating and terminal voltage");
    }
    m_state.resize(5);
    m_dstate_dt.assign(5, 0.0);
    if (mStarting) {
        const auto state =
            electricalEquilibrium(bus->getVoltage(), bus->getAngle(), 1.0 - mInitialSpeed);
        std::copy(state.begin(), state.end(), m_state.begin());
    } else {
        initializeRunningPoint(bus->getVoltage(), bus->getAngle());
    }
    loadStateSizes(cLocalSolverMode);
    setOffset(0, cLocalSolverMode);
}

void MotorLoadDQ::dynObjectInitializeA(CoreTime time0, std::uint32_t flags)
{
    validateParameters();
    GridLoad::dynObjectInitializeA(time0, flags);
}

void MotorLoadDQ::dynObjectInitializeB(const IOdata& inputs,
                                       const IOdata& /*desiredOutput*/,
                                       IOdata& /*fieldSet*/)
{
    if (mStarting) {
        std::fill(m_state.begin(), m_state.end(), 0.0);
        m_state[4] = mInitialSpeed;
    }
    const auto initialRates = rates(inputs, m_state.data());
    std::copy(initialRates.begin(), initialRates.end(), m_dstate_dt.begin());
}

StateSizes MotorLoadDQ::localStateSizes(const SolverMode& sMode) const
{
    StateSizes sizes;
    if (!isDynamic(sMode)) {
        sizes.algSize = 5;
    } else if (hasDifferential(sMode)) {
        sizes.diffSize = 5;
    }
    return sizes;
}

count_t MotorLoadDQ::localJacobianCount(const SolverMode& sMode) const
{
    if (isDynamic(sMode) && !hasDifferential(sMode)) {
        return 0;
    }
    return !isDynamic(sMode) && mStarting ? 19 : 23;
}

void MotorLoadDQ::derivative(const IOdata& inputs,
                             const StateData& stateData,
                             double deriv[],
                             const SolverMode& sMode)
{
    if (!hasDifferential(sMode)) {
        return;
    }
    const auto loc = offsets.getLocations(stateData, deriv, sMode, this);
    const auto stateRates = rates(inputs, loc.diffStateLoc);
    std::copy(stateRates.begin(), stateRates.end(), loc.destDiffLoc);
}

void MotorLoadDQ::residual(const IOdata& inputs,
                           const StateData& stateData,
                           double resid[],
                           const SolverMode& sMode)
{
    if (isDynamic(sMode) && !hasDifferential(sMode)) {
        return;
    }
    const auto loc = offsets.getLocations(stateData, resid, sMode, this);
    const double* state = isDynamic(sMode) ? loc.diffStateLoc : loc.algStateLoc;
    double* destination = isDynamic(sMode) ? loc.destDiffLoc : loc.destLoc;
    const auto stateRates = rates(inputs, state);
    for (std::size_t index = 0; index < stateRates.size(); ++index) {
        destination[index] = stateRates[index] - (isDynamic(sMode) ? loc.dstateLoc[index] : 0.0);
    }
    if (!isDynamic(sMode) && mStarting) {
        destination[4] = state[4] - mInitialSpeed;
    }
}

void MotorLoadDQ::jacobianElements(const IOdata& inputs,
                                   const StateData& stateData,
                                   MatrixData<double>& matrixData,
                                   const IOlocs& inputLocs,
                                   const SolverMode& sMode)
{
    if (isDynamic(sMode) && !hasDifferential(sMode)) {
        return;
    }
    const auto loc = offsets.getLocations(stateData, sMode, this);
    const double* state = isDynamic(sMode) ? loc.diffStateLoc : loc.algStateLoc;
    const auto offset = isDynamic(sMode) ? loc.diffOffset : loc.algOffset;
    const double determinant = (mXls * mXlr) + (mXm * (mXls + mXlr));
    const double statorCoefficient = (mXlr + mXm) / determinant;
    const double mutualCoefficient = -mXm / determinant;
    const double rotorCoefficient = (mXls + mXm) / determinant;
    const double frequency = systemBaseFrequency;
    const double slip = 1.0 - state[4];
    const double solverCoefficient = isDynamic(sMode) ? stateData.cj : 0.0;
    matrixData.assign(offset, offset, (-frequency * mRs * statorCoefficient) - solverCoefficient);
    matrixData.assign(offset, offset + 1, frequency);
    matrixData.assign(offset, offset + 2, -frequency * mRs * mutualCoefficient);
    matrixData.assign(offset + 1, offset, -frequency);
    matrixData.assign(offset + 1,
                      offset + 1,
                      (-frequency * mRs * statorCoefficient) - solverCoefficient);
    matrixData.assign(offset + 1, offset + 3, -frequency * mRs * mutualCoefficient);
    matrixData.assign(offset + 2, offset, -frequency * mRr * mutualCoefficient);
    matrixData.assign(offset + 2,
                      offset + 2,
                      (-frequency * mRr * rotorCoefficient) - solverCoefficient);
    matrixData.assign(offset + 2, offset + 3, frequency * slip);
    matrixData.assign(offset + 2, offset + 4, -frequency * state[3]);
    matrixData.assign(offset + 3, offset + 1, -frequency * mRr * mutualCoefficient);
    matrixData.assign(offset + 3, offset + 2, -frequency * slip);
    matrixData.assign(offset + 3,
                      offset + 3,
                      (-frequency * mRr * rotorCoefficient) - solverCoefficient);
    matrixData.assign(offset + 3, offset + 4, frequency * state[2]);
    if (!isDynamic(sMode) && mStarting) {
        matrixData.assign(offset + 4, offset + 4, 1.0);
    } else {
        const double torqueCoefficient = mutualCoefficient / (2.0 * mInertia);
        matrixData.assign(offset + 4, offset, torqueCoefficient * state[3]);
        matrixData.assign(offset + 4, offset + 1, -torqueCoefficient * state[2]);
        matrixData.assign(offset + 4, offset + 2, -torqueCoefficient * state[1]);
        matrixData.assign(offset + 4, offset + 3, torqueCoefficient * state[0]);
        const double torqueSlope = mTorqueLinear + (2.0 * mTorqueQuadratic * state[4]);
        const double speedPartial = -((mLoadTorque * torqueSlope) + mDamping) / (2.0 * mInertia);
        matrixData.assign(offset + 4, offset + 4, speedPartial - solverCoefficient);
    }
    const double voltage = inputs[VOLTAGE_IN_LOCATION];
    const double angle = inputs[ANGLE_IN_LOCATION];
    matrixData.assignCheckCol(offset, inputLocs[VOLTAGE_IN_LOCATION], frequency * std::cos(angle));
    matrixData.assignCheckCol(offset + 1,
                              inputLocs[VOLTAGE_IN_LOCATION],
                              frequency * std::sin(angle));
    matrixData.assignCheckCol(offset,
                              inputLocs[ANGLE_IN_LOCATION],
                              -frequency * voltage * std::sin(angle));
    matrixData.assignCheckCol(offset + 1,
                              inputLocs[ANGLE_IN_LOCATION],
                              frequency * voltage * std::cos(angle));
}

void MotorLoadDQ::outputPartialDerivatives(const IOdata& inputs,
                                           const StateData& stateData,
                                           MatrixData<double>& matrixData,
                                           const SolverMode& sMode)
{
    if (isDynamic(sMode) && !hasDifferential(sMode)) {
        return;
    }
    const auto loc = offsets.getLocations(stateData, sMode, this);
    const auto offset = isDynamic(sMode) ? loc.diffOffset : loc.algOffset;
    const double determinant = (mXls * mXlr) + (mXm * (mXls + mXlr));
    const std::array<double, 2> coefficients{(mXlr + mXm) / determinant, -mXm / determinant};
    const double voltageD =
        inputs[VOLTAGE_IN_LOCATION] * std::cos(inputs[ANGLE_IN_LOCATION]) * mScale;
    const double voltageQ =
        inputs[VOLTAGE_IN_LOCATION] * std::sin(inputs[ANGLE_IN_LOCATION]) * mScale;
    for (std::size_t index = 0; index < coefficients.size(); ++index) {
        const auto column = offset + static_cast<index_t>(2 * index);
        matrixData.assign(POUT_LOCATION, column, voltageD * coefficients[index]);
        matrixData.assign(POUT_LOCATION, column + 1, voltageQ * coefficients[index]);
        matrixData.assign(QOUT_LOCATION, column, voltageQ * coefficients[index]);
        matrixData.assign(QOUT_LOCATION, column + 1, -voltageD * coefficients[index]);
    }
}

void MotorLoadDQ::ioPartialDerivatives(const IOdata& inputs,
                                       const StateData& stateData,
                                       MatrixData<double>& matrixData,
                                       const IOlocs& inputLocs,
                                       const SolverMode& sMode)
{
    const auto loc = offsets.getLocations(stateData, sMode, this);
    const auto current = currents(isDynamic(sMode) ? loc.diffStateLoc : loc.algStateLoc);
    const double angle = inputs[ANGLE_IN_LOCATION];
    const double active =
        mScale * ((std::cos(angle) * current[0]) + (std::sin(angle) * current[1]));
    const double reactive =
        mScale * ((std::sin(angle) * current[0]) - (std::cos(angle) * current[1]));
    matrixData.assignCheckCol(POUT_LOCATION, inputLocs[VOLTAGE_IN_LOCATION], active);
    matrixData.assignCheckCol(QOUT_LOCATION, inputLocs[VOLTAGE_IN_LOCATION], reactive);
    matrixData.assignCheckCol(POUT_LOCATION, inputLocs[ANGLE_IN_LOCATION], -inputs[0] * reactive);
    matrixData.assignCheckCol(QOUT_LOCATION, inputLocs[ANGLE_IN_LOCATION], inputs[0] * active);
}

count_t MotorLoadDQ::outputDependencyCount(index_t /*num*/, const SolverMode& sMode) const
{
    return isDynamic(sMode) && !hasDifferential(sMode) ? 0 : 4;
}

double MotorLoadDQ::getRealPower(const IOdata& inputs,
                                 const StateData& stateData,
                                 const SolverMode& sMode) const
{
    const auto loc = offsets.getLocations(stateData, sMode, this);
    const auto current = currents(isDynamic(sMode) ? loc.diffStateLoc : loc.algStateLoc);
    return inputs[0] * mScale *
        ((std::cos(inputs[1]) * current[0]) + (std::sin(inputs[1]) * current[1]));
}

double MotorLoadDQ::getReactivePower(const IOdata& inputs,
                                     const StateData& stateData,
                                     const SolverMode& sMode) const
{
    const auto loc = offsets.getLocations(stateData, sMode, this);
    const auto current = currents(isDynamic(sMode) ? loc.diffStateLoc : loc.algStateLoc);
    return inputs[0] * mScale *
        ((std::sin(inputs[1]) * current[0]) - (std::cos(inputs[1]) * current[1]));
}

double MotorLoadDQ::getRealPower(double voltage) const
{
    return getRealPower({voltage, bus == nullptr ? 0.0 : bus->getAngle()},
                        emptyStateData,
                        cLocalSolverMode);
}

double MotorLoadDQ::getReactivePower(double voltage) const
{
    return getReactivePower({voltage, bus == nullptr ? 0.0 : bus->getAngle()},
                            emptyStateData,
                            cLocalSolverMode);
}

double MotorLoadDQ::getRealPower() const
{
    return getRealPower(bus == nullptr ? 1.0 : bus->getVoltage());
}

double MotorLoadDQ::getReactivePower() const
{
    return getReactivePower(bus == nullptr ? 1.0 : bus->getVoltage());
}

}  // namespace griddyn::loads
