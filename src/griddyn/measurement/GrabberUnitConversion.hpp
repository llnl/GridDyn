/*
 * Copyright (c) 2014-2026, Lawrence Livermore National Security
 * See the top-level NOTICE for additional details. All rights reserved.
 * SPDX-License-Identifier: BSD-3-Clause
 */

#pragma once

/** @file
 * @brief Shared engineering-unit context for scalar, vector, and solver-state measurements.
 */

#include "core/CoreObject.h"
#include "units/units.hpp"
#include <cmath>

namespace griddyn {
/** @brief Convert measurement values using the target object's system bases.
 *
 * Captures base power and voltage once so a vector can reuse the same conversion
 * context for every element. Per-unit frequency uses the configured frequency
 * base, rather than the power base. Gain and bias belong to the caller.
 *
 * The object is queried during construction and is not retained. Reconstruct
 * this context after changing system bases. This helper does not infer units
 * for an expression or provide derivatives of nonlinear unit conversions.
 * @see GridGrabber, StateGrabber
 */
class GrabberUnitConversion {
    units::unit mInputUnits;
    units::unit mOutputUnits;
    double mBasePower = 100.0;
    double mBaseVoltage = 1.0;
    double mBaseFrequency = 60.0;
    bool mUseFrequencyBase;

  public:
    /** @brief Capture the conversion units and target's system bases.
     * @param input Native measurement units.
     * @param output Requested measurement units, or units::defunit for native values.
     * @param object Target supplying basepower, basevoltage, and basefrequency.
     *        May be null, in which case basepower defaults to 100, basevoltage
     *        to 1 (in the object's base-parameter conventions), and frequency to 60 Hz.
     * @note An absent or invalid frequency base retains the 60 Hz fallback.
     */
    GrabberUnitConversion(units::unit input, units::unit output, const CoreObject* object):
        mInputUnits(input), mOutputUnits(output),
        mUseFrequencyBase(input == units::puHz || output == units::puHz)
    {
        if (object != nullptr) {
            mBasePower = object->get("basepower");
            mBaseVoltage = object->get("basevoltage");
            if (mUseFrequencyBase) {
                const auto frequency = object->get("basefrequency", units::Hz);
                if (std::isfinite(frequency) && frequency > 0.0) {
                    mBaseFrequency = frequency;
                }
            }
        }
    }

    /** @brief Convert one value with the captured unit context.
     * @param value Value expressed in the input units.
     * @return Converted value, following the units library's domain/error behavior.
     * @note Callers normally skip conversion when output is units::defunit.
     */
    [[nodiscard]] double convert(double value) const
    {
        return mUseFrequencyBase ?
            units::convert(value, mInputUnits, mOutputUnits, mBaseFrequency) :
            units::convert(value, mInputUnits, mOutputUnits, mBasePower, mBaseVoltage);
    }

    /** @brief Return the Jacobian multiplier for an affine unit conversion.
     * @return convert(1) - convert(0), or one when native units are requested.
     *
     * Removes an affine offset so scalar and vector state readers apply the
     * same derivative scale. Multiply by the grabber gain separately; bias has
     * no derivative. This is not a pointwise derivative for logarithmic or
     * other nonlinear conversions; express those through measurement functions.
     */
    [[nodiscard]] double affineScale() const
    {
        return mOutputUnits == units::defunit ? 1.0 : convert(1.0) - convert(0.0);
    }
};
}  // namespace griddyn
