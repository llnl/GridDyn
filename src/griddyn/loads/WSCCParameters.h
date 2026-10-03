/*
 * Copyright (c) 2014-2026, Lawrence Livermore National Security
 * See the top-level NOTICE for additional details. All rights reserved.
 * SPDX-License-Identifier: BSD-3-Clause
 */

#pragma once

#include <array>
#include <cmath>

namespace griddyn {
/** Parameters for the PSLF/PowerWorld WSCC load characteristic.
 *
 * The DYD order is P1, Q1, P2, Q2, P3, Q3, P4, Q4, LPD, LQD, and optionally
 * VMIN. The first ten parameters are the documented WSCC characteristic; VMIN
 * is the final minimum-voltage field present in some DYD records.
 */
struct WSCCParameters {
    double p1 = 0.0;
    double q1 = 0.0;
    double p2 = 0.0;
    double q2 = 0.0;
    double p3 = 0.0;
    double q3 = 0.0;
    double p4 = 0.0;
    double q4 = 0.0;
    double lpd = 0.0;
    double lqd = 0.0;
    double vmin = 0.0;
};

enum class WSCCRepresentation { ZIP, FDEP, WSCC };

/** Scope of a WSCC characteristic in a PSLF DYD file, from broad to specific. */
enum class WSCCLoadScope { System, Area, Zone, Bus };

struct WSCCFDepSide {
    double scale = 0.0;
    double voltageExponent = 0.0;
    double frequencyExponent = 0.0;
};

/** Select the simplest existing load model that exactly represents a WSCC curve. */
inline WSCCRepresentation classifyWSCC(const WSCCParameters& parameters,
                                       WSCCFDepSide& pSide,
                                       WSCCFDepSide& qSide)
{
    constexpr double tolerance = 1e-12;
    const auto isZero = [tolerance](double value) { return std::abs(value) <= tolerance; };
    const bool extended = !isZero(parameters.p4) || !isZero(parameters.q4);
    if (isZero(parameters.vmin)) {
        const bool pHasVoltageCurve = extended ?
            (!isZero(parameters.p1) || !isZero(parameters.p2) ||
             !isZero(parameters.p3) || !isZero(parameters.p4)) :
            (!isZero(parameters.p1) || !isZero(parameters.p2) || !isZero(parameters.p3));
        const bool qHasVoltageCurve = extended ?
            (!isZero(parameters.q1) || !isZero(parameters.q2) ||
             !isZero(parameters.q3) || !isZero(parameters.q4)) :
            (!isZero(parameters.q1) || !isZero(parameters.q2) || !isZero(parameters.q3));
        const bool pFrequencyDependent = extended ?
            !isZero(parameters.p4 * parameters.lpd) :
            (!isZero(parameters.lpd) && pHasVoltageCurve);
        const bool qFrequencyDependent = extended ?
            !isZero(parameters.q4 * parameters.lqd) :
            (!isZero(parameters.lqd) && qHasVoltageCurve);
        if (!pFrequencyDependent && !qFrequencyDependent) {
            return WSCCRepresentation::ZIP;
        }

        const auto setFDepSide = [isZero, extended](double z,
                                                     double i,
                                                     double constant,
                                                     double p4,
                                                     double frequencyCoefficient,
                                                     WSCCFDepSide& side) {
            const double curveConstant = extended ? (constant + p4) : constant;
            const double frequencyDelta = extended ? (p4 * frequencyCoefficient) : 0.0;
            double scale = 0.0;
            double exponent = 0.0;
            double beta = 0.0;

            if (extended && !isZero(frequencyDelta)) {
                if (!isZero(z) || !isZero(i) || isZero(curveConstant) ||
                    !isZero((frequencyDelta / curveConstant) - 1.0)) {
                    return false;
                }
                scale = curveConstant;
                beta = 1.0;
            } else {
                int activeTerms = 0;
                if (!isZero(z)) {
                    ++activeTerms;
                    scale = z;
                    exponent = 2.0;
                }
                if (!isZero(i)) {
                    ++activeTerms;
                    scale = i;
                    exponent = 1.0;
                }
                if (!isZero(curveConstant)) {
                    ++activeTerms;
                    scale = curveConstant;
                    exponent = 0.0;
                }
                if (activeTerms > 1) {
                    return false;
                }
                if ((activeTerms > 0) && !extended && !isZero(frequencyCoefficient)) {
                    if (!isZero(frequencyCoefficient - 1.0)) {
                        return false;
                    }
                    beta = 1.0;
                }
            }
            side.scale = scale;
            side.voltageExponent = exponent;
            side.frequencyExponent = beta;
            return true;
        };

        const bool pFits = setFDepSide(parameters.p1,
                                       parameters.p2,
                                       parameters.p3,
                                       parameters.p4,
                                       parameters.lpd,
                                       pSide);
        const bool qFits = setFDepSide(parameters.q1,
                                       parameters.q2,
                                       parameters.q3,
                                       parameters.q4,
                                       parameters.lqd,
                                       qSide);
        if (pFits && qFits) {
            return WSCCRepresentation::FDEP;
        }
    }
    return WSCCRepresentation::WSCC;
}
}  // namespace griddyn
