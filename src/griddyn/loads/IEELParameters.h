/*
 * Copyright (c) 2014-2026, Lawrence Livermore National Security
 * See the top-level NOTICE for additional details. All rights reserved.
 * SPDX-License-Identifier: BSD-3-Clause
 */

#pragma once

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <utility>
#include <vector>

namespace griddyn {
/** Parameters for the PSS/E IEEL static load characteristic.
 *
 * The DYR order is A1-A8 followed by N1-N6:
 * P = P0 (A1 V^N1 + A2 V^N2 + A3 V^N3) (1 + A7 (f - 1))
 * Q = Q0 (A4 V^N4 + A5 V^N5 + A6 V^N6) (1 + A8 (f - 1))
 * PowerWorld's optional Pfs field is specific to component-based load composition and is not part
 * of the 14-value standalone PSS/E DYR record.
 */
struct IEELParameters {
    std::array<double, 8> coefficients{};  //!< A1 through A8
    std::array<double, 6> exponents{};  //!< N1 through N6
};

enum class IEELRepresentation { ZIP, FDEP, IEEL };

/** Select the simplest existing GridDyn load model that exactly represents an IEEL curve. */
inline IEELRepresentation classifyIEEL(const IEELParameters& parameters)
{
    constexpr double tolerance = 1e-12;
    using Term = std::pair<double, double>;
    const auto terms = [&parameters, tolerance](std::size_t start) {
        std::vector<Term> result;
        for (std::size_t index = 0; index < 3U; ++index) {
            const double coefficient = parameters.coefficients[start + index];
            const double exponent = parameters.exponents[start + index];
            if (std::abs(coefficient) <= tolerance) {
                continue;
            }
            auto existing =
                std::find_if(result.begin(), result.end(), [exponent, tolerance](const Term& term) {
                    return std::abs(term.second - exponent) <= tolerance;
                });
            if (existing == result.end()) {
                result.emplace_back(coefficient, exponent);
            } else {
                existing->first += coefficient;
            }
        }
        std::erase_if(result,
                      [tolerance](const Term& term) { return std::abs(term.first) <= tolerance; });
        return result;
    };
    const auto pTerms = terms(0U);
    const auto qTerms = terms(3U);
    const auto isZipExponent = [tolerance](double exponent) {
        return (std::abs(exponent) <= tolerance) || (std::abs(exponent - 1.0) <= tolerance) ||
            (std::abs(exponent - 2.0) <= tolerance);
    };
    const bool noFrequency = (std::abs(parameters.coefficients[6]) <= tolerance) &&
        (std::abs(parameters.coefficients[7]) <= tolerance);
    const bool zipExponents =
        std::all_of(pTerms.begin(),
                    pTerms.end(),
                    [&isZipExponent](const Term& term) { return isZipExponent(term.second); }) &&
        std::all_of(qTerms.begin(), qTerms.end(), [&isZipExponent](const Term& term) {
            return isZipExponent(term.second);
        });
    if (noFrequency && zipExponents) {
        return IEELRepresentation::ZIP;
    }

    const auto frequencyPowerFits = [tolerance](const std::vector<Term>& side, double a) {
        return side.empty() ||
            ((side.size() == 1U) &&
             ((std::abs(a) <= tolerance) || (std::abs(a - 1.0) <= tolerance)));
    };
    if ((pTerms.size() <= 1U) && (qTerms.size() <= 1U) &&
        frequencyPowerFits(pTerms, parameters.coefficients[6]) &&
        frequencyPowerFits(qTerms, parameters.coefficients[7])) {
        return IEELRepresentation::FDEP;
    }
    return IEELRepresentation::IEEL;
}
}  // namespace griddyn
