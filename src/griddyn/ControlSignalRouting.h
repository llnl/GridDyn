/*
 * Copyright (c) 2014-2026, Lawrence Livermore National Security
 * See the top-level NOTICE for additional details. All rights reserved.
 * SPDX-License-Identifier: BSD-3-Clause
 */

#pragma once

#include "core/CoreExceptions.h"
#include "gridDynDefinitions.hpp"
#include "utilities/MatrixData.hpp"
#include <algorithm>
#include <functional>
#include <string>
#include <utility>
#include <vector>

namespace griddyn {
class StateData;
class SolverMode;

// Keep route-local Jacobian markers outside ordinary solver locations.
inline constexpr index_t kControlSignalPseudoLocationBase = kInvalidLocation / 2;

/** One contribution to the derivative of a routed controller signal. */
struct ControlSignalDerivative {
    index_t location;
    double value;
};

/** Current values and optional location/chain-rule data for one host evaluation. */
struct ControlSignalContext {
    const IOdata& hostInputs;
    const IOlocs* hostInputLocs;
    const StateData& stateData;
    const SolverMode& solverMode;
    const std::vector<std::vector<ControlSignalDerivative>>* hostInputDerivatives = nullptr;
};

/**
 * A host resolves source identity once, then supplies value and derivative
 * readers for that source. Readers must use the current StateData; storing a
 * previously sampled value would break implicit controller feedback.
 */
struct ControlSignalRoute {
    using ValueReader = std::function<double(const ControlSignalContext&)>;
    using DerivativeReader =
        std::function<void(const ControlSignalContext&, std::vector<ControlSignalDerivative>&)>;

    index_t inputIndex = 0;
    ValueReader value;
    DerivativeReader derivatives;
    double gain = 1.0;
    double offset = 0.0;
    std::string sourceName;
};

/**
 * Solver locations and sparse chain rules for the inputs of one submodel.
 * Simple unit-gain connections retain their real solver location. Computed or
 * scaled connections use local pseudo columns that are expanded by assign().
 */
struct ControlSignalInputLocations {
    IOlocs locations;
    std::vector<std::vector<ControlSignalDerivative>> derivatives;
    bool needsTranslation() const { return translated; }

    void appendExpanded(index_t column,
                        double value,
                        std::vector<ControlSignalDerivative>& terms) const
    {
        if (needsTranslation() && std::cmp_greater_equal(column, 0) &&
            column < kControlSignalPseudoLocationBase) {
            const auto inputIndex =
                static_cast<std::size_t>(kControlSignalPseudoLocationBase - 1 - column);
            if (inputIndex < derivatives.size() && !derivatives[inputIndex].empty() &&
                locations[inputIndex] == column) {
                for (const auto& term : derivatives[inputIndex]) {
                    terms.push_back({term.location, value * term.value});
                }
                return;
            }
        }
        terms.push_back({column, value});
    }

    void assign(MatrixData<double>& matrix, index_t row, index_t column, double value) const
    {
        if (needsTranslation() && std::cmp_greater_equal(column, 0) &&
            column < kControlSignalPseudoLocationBase) {
            const auto inputIndex =
                static_cast<std::size_t>(kControlSignalPseudoLocationBase - 1 - column);
            if (inputIndex < derivatives.size() && !derivatives[inputIndex].empty() &&
                locations[inputIndex] == column) {
                for (const auto& term : derivatives[inputIndex]) {
                    matrix.assign(row, term.location, value * term.value);
                }
                return;
            }
        }
        matrix.assign(row, column, value);
    }

  private:
    friend class ControlSignalRouting;
    std::vector<ControlSignalDerivative> scratch;
    bool translated = false;
};

/** Reusable, resolved routing and linear signal math for a submodel's inputs. */
class ControlSignalRouting {
  public:
    void clear()
    {
        routes.clear();
        routeInputCount = 0;
    }
    void add(ControlSignalRoute route)
    {
        if (std::cmp_less(route.inputIndex, 0) ||
            route.inputIndex >= kControlSignalPseudoLocationBase - 1) {
            throw InvalidParameterValue("controller signal input index");
        }
        if (std::any_of(routes.begin(), routes.end(), [&route](const auto& existing) {
                return existing.inputIndex == route.inputIndex;
            })) {
            throw InvalidParameterValue("duplicate controller signal input index");
        }
        routeInputCount = std::max(routeInputCount, static_cast<std::size_t>(route.inputIndex) + 1);
        routes.push_back(std::move(route));
    }
    bool empty() const { return routes.empty(); }
    const std::vector<ControlSignalRoute>& bindings() const { return routes; }

    /** Bind a model input to one slot in a host's current signal frame. */
    void addInput(index_t inputIndex,
                  index_t sourceIndex,
                  std::string sourceName,
                  double gain = 1.0,
                  double offset = 0.0)
    {
        if (std::cmp_less(sourceIndex, 0)) {
            throw InvalidParameterValue("controller signal source index");
        }
        ControlSignalRoute route;
        route.inputIndex = inputIndex;
        route.sourceName = std::move(sourceName);
        route.gain = gain;
        route.offset = offset;
        route.value = [sourceIndex](const ControlSignalContext& context) {
            return std::cmp_less(sourceIndex, context.hostInputs.size()) ?
                context.hostInputs[sourceIndex] :
                kNullVal;
        };
        route.derivatives =
            [sourceIndex](const ControlSignalContext& context,
                          std::vector<ControlSignalDerivative>& terms) {
                bool hasSparseTerms = false;
                if (context.hostInputDerivatives != nullptr &&
                    std::cmp_less(sourceIndex, context.hostInputDerivatives->size())) {
                    const auto& sourceTerms = (*context.hostInputDerivatives)[sourceIndex];
                    hasSparseTerms = !sourceTerms.empty();
                    terms.insert(terms.end(), sourceTerms.begin(), sourceTerms.end());
                }
                if (!hasSparseTerms && context.hostInputLocs != nullptr &&
                    std::cmp_less(sourceIndex, context.hostInputLocs->size())) {
                    terms.push_back({(*context.hostInputLocs)[sourceIndex], 1.0});
                }
            };
        add(std::move(route));
    }

    IOdata values(const ControlSignalContext& context) const
    {
        IOdata result(routeInputCount, kNullVal);
        writeValues(context, result);
        return result;
    }

    /** Update only the bound inputs in a host-owned input buffer. */
    void writeValues(const ControlSignalContext& context, IOdata& result) const
    {
        if (result.size() < routeInputCount) {
            result.resize(routeInputCount, kNullVal);
        }
        for (const auto& route : routes) {
            result[route.inputIndex] = kNullVal;
            if (route.value) {
                const double value = route.value(context);
                if (value != kNullVal) {
                    result[route.inputIndex] = route.gain * value + route.offset;
                }
            }
        }
    }

    ControlSignalInputLocations inputLocations(const ControlSignalContext& context) const
    {
        ControlSignalInputLocations result;
        writeInputLocations(context, result);
        return result;
    }

    /** Rebuild mode-dependent locations while reusing a host-owned map. */
    void writeInputLocations(const ControlSignalContext& context,
                             ControlSignalInputLocations& result) const
    {
        result.locations.assign(routeInputCount, kNullLocation);
        if (result.derivatives.size() < routeInputCount) {
            result.derivatives.resize(routeInputCount);
        }
        for (auto& terms : result.derivatives) {
            terms.clear();
        }
        result.translated = false;
        auto& terms = result.scratch;
        for (const auto& route : routes) {
            if (!route.derivatives) {
                continue;
            }
            terms.clear();
            route.derivatives(context, terms);
            for (auto& term : terms) {
                term.value *= route.gain;
            }
            terms.erase(std::remove_if(terms.begin(),
                                       terms.end(),
                                       [](const auto& term) {
                                           return term.location == kNullLocation ||
                                               term.location == kInvalidLocation ||
                                               std::cmp_less(term.location, 0) || term.value == 0.0;
                                       }),
                        terms.end());
            if (terms.size() == 1 && terms.front().value == 1.0) {
                result.locations[route.inputIndex] = terms.front().location;
            } else if (!terms.empty()) {
                result.translated = true;
                result.derivatives[route.inputIndex].assign(terms.begin(), terms.end());
                result.locations[route.inputIndex] =
                    kControlSignalPseudoLocationBase - 1 - route.inputIndex;
            }
        }
    }

  private:
    std::vector<ControlSignalRoute> routes;
    std::size_t routeInputCount = 0;
};

}  // namespace griddyn
