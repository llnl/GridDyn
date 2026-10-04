/*
 * Copyright (c) 2014-2026, Lawrence Livermore National Security
 * See the top-level NOTICE for additional details. All rights reserved.
 * SPDX-License-Identifier: BSD-3-Clause
 */

#include "LoadTemplateAdapters.h"

#include "../GridBus.h"
#include "../Load.h"
#include "FDepLoad.h"
#include "IEELLoad.h"
#include "WSCCLoad.h"
#include "ZipLoad.h"
#include "core/CoreExceptions.h"
#include <algorithm>
#include <cmath>
#include <cstddef>
#include <memory>
#include <string>
#include <typeinfo>
#include <utility>
#include <vector>

namespace griddyn::loads {
bool supportsCharacteristicReplacement(const GridLoad& load)
{
    // Inheritance alone is insufficient: Svd, RampLoad, AggregateLoad and other
    // controlled devices derive from ZipLoad but must retain their own behavior.
    if (typeid(load) == typeid(ZipLoad)) {
        return true;
    }
    if (typeid(load) == typeid(FDepLoad)) {
        const auto& fdep = static_cast<const FDepLoad&>(load);
        return (fdep.getFrequencyFilter() == nullptr) && (fdep.getFrequencyBus() == nullptr);
    }
    return (typeid(load) == typeid(IEELLoad)) || (typeid(load) == typeid(WSCCLoad));
}

namespace {
    struct IEELVoltageTerm {
        double mCoefficient;
        double mExponent;
    };

    std::vector<IEELVoltageTerm> getIEELVoltageTerms(const IEELParameters& parameters,
                                                      std::size_t firstTerm)
    {
        constexpr double tolerance = 1e-12;
        std::vector<IEELVoltageTerm> terms;
        for (std::size_t index = 0; index < 3U; ++index) {
            const double coefficient = parameters.coefficients[firstTerm + index];
            const double exponent = parameters.exponents[firstTerm + index];
            if (std::abs(coefficient) <= tolerance) {
                continue;
            }
            auto existing = std::find_if(terms.begin(), terms.end(), [exponent](const auto& term) {
                return std::abs(term.mExponent - exponent) <= tolerance;
            });
            if (existing == terms.end()) {
                terms.push_back(
                    IEELVoltageTerm{.mCoefficient = coefficient, .mExponent = exponent});
            } else {
                existing->mCoefficient += coefficient;
            }
        }
        std::erase_if(terms, [](const auto& term) {
            return std::abs(term.mCoefficient) <= tolerance;
        });
        return terms;
    }

    std::pair<double, double> getCharacteristicReferencePower(const GridLoad& load, double voltage)
    {
        const GridLoad* reference = &load;
        std::unique_ptr<GridLoad> connectedCopy;
        if (!load.isConnected()) {
            // Power getters return zero for a disconnected load. Evaluate a
            // connected copy so the replacement retains its stored demand.
            connectedCopy.reset(dynamic_cast<GridLoad*>(load.clone()));
            if (connectedCopy == nullptr) {
                throw InvalidParameterValue("cannot clone disconnected load '" + load.getName() +
                                            "' for characteristic assignment");
            }
            connectedCopy->reconnect();
            reference = connectedCopy.get();
        }
        return {reference->getRealPower(voltage), reference->getReactivePower(voltage)};
    }

    void validateIEELTarget(const GridLoad& load)
    {
        if (load.checkFlag(DYN_INITIALIZED)) {
            throw InvalidParameterValue("IEEL cannot be assigned after dynamic initialization");
        }
        if (!supportsCharacteristicReplacement(load)) {
            throw InvalidParameterValue("IEELAL cannot replace load '" + load.getName() +
                                        "' because its existing dynamic load model is not a ZIP, "
                                        "FDep, IEEL, or WSCC model");
        }
    }

    void validateWSCCTarget(const GridLoad& load)
    {
        if (load.checkFlag(DYN_INITIALIZED)) {
            throw InvalidParameterValue("WSCC load characteristics cannot be assigned after "
                                        "dynamic initialization");
        }
        if (!supportsCharacteristicReplacement(load)) {
            throw InvalidParameterValue("WSCC load characteristic cannot replace load '" +
                                        load.getName() +
                                        "' because its existing dynamic load model is not a ZIP, "
                                        "FDep, IEEL, or WSCC model");
        }
    }

    double getWSCCCharacteristic(const WSCCParameters& parameters,
                                 bool reactive,
                                 double voltage,
                                 double frequency)
    {
        const double first = reactive ? parameters.q1 : parameters.p1;
        const double second = reactive ? parameters.q2 : parameters.p2;
        const double third = reactive ? parameters.q3 : parameters.p3;
        const double fourth = reactive ? parameters.q4 : parameters.p4;
        const double frequencyCoefficient = reactive ? parameters.lqd : parameters.lpd;
        const bool extended = (parameters.p4 != 0.0) || (parameters.q4 != 0.0);
        const double polynomial = ((first * voltage) * voltage) + (second * voltage) + third;
        const double frequencyFactor = 1.0 + (frequencyCoefficient * (frequency - 1.0));
        const double characteristic =
            extended ? polynomial + (fourth * frequencyFactor) : polynomial * frequencyFactor;
        if ((parameters.vmin > 0.0) && (voltage < parameters.vmin)) {
            const double voltageRatio = voltage / parameters.vmin;
            const double minimumPolynomial =
                ((first * parameters.vmin) * parameters.vmin) + (second * parameters.vmin) + third;
            const double minimumCharacteristic = extended ?
                minimumPolynomial + (fourth * frequencyFactor) :
                minimumPolynomial * frequencyFactor;
            return minimumCharacteristic * voltageRatio * voltageRatio;
        }
        return characteristic;
    }
}  // namespace

LoadTemplateFactory makeIEELALLoadTemplate(IEELParameters parameters)
{
    return [parameters](const GridLoad& load) {
        validateIEELTarget(load);
        const auto representation = classifyIEEL(parameters);
        const auto [baseP, baseQ] = getCharacteristicReferencePower(load, 1.0);
        std::unique_ptr<GridLoad> replacement;
        if (representation == IEELRepresentation::ZIP) {
            replacement = std::make_unique<ZipLoad>(load.getName());
        } else if (representation == IEELRepresentation::FDEP) {
            replacement = std::make_unique<FDepLoad>(load.getName());
        } else {
            replacement = std::make_unique<IEELLoad>(load.getName());
        }

        load.GridLoad::clone(replacement.get());
        replacement->setLoad(baseP, baseQ, units::puMW);
        replacement->setFlag("usepowerfactor", false);

        if (representation == IEELRepresentation::ZIP) {
            auto* zipLoad = static_cast<ZipLoad*>(replacement.get());
            double pConstant = 0.0;
            double pCurrent = 0.0;
            double pImpedance = 0.0;
            double qConstant = 0.0;
            double qCurrent = 0.0;
            double qImpedance = 0.0;
            for (const auto& term : getIEELVoltageTerms(parameters, 0U)) {
                if (std::abs(term.mExponent) <= 1e-12) {
                    pConstant += baseP * term.mCoefficient;
                } else if (std::abs(term.mExponent - 1.0) <= 1e-12) {
                    pCurrent += baseP * term.mCoefficient;
                } else {
                    pImpedance += baseP * term.mCoefficient;
                }
            }
            for (const auto& term : getIEELVoltageTerms(parameters, 3U)) {
                if (std::abs(term.mExponent) <= 1e-12) {
                    qConstant += baseQ * term.mCoefficient;
                } else if (std::abs(term.mExponent - 1.0) <= 1e-12) {
                    qCurrent += baseQ * term.mCoefficient;
                } else {
                    qImpedance += baseQ * term.mCoefficient;
                }
            }
            zipLoad->set("p", pConstant, units::puMW);
            zipLoad->set("ip", pCurrent, units::puA);
            zipLoad->set("yp", pImpedance, units::puMW);
            zipLoad->set("q", qConstant, units::puMW);
            zipLoad->set("iq", qCurrent, units::puA);
            zipLoad->set("yq", qImpedance, units::puMW);
            zipLoad->setFlag("no_pqvoltage_limit", true);
        } else if (representation == IEELRepresentation::FDEP) {
            auto* fdepLoad = static_cast<FDepLoad*>(replacement.get());
            const auto setSide =
                [&parameters](FDepLoad* target, std::size_t firstTerm, bool reactive) {
                const auto terms = getIEELVoltageTerms(parameters, firstTerm);
                const char* alpha = reactive ? "alphaq" : "alphap";
                const char* scale = reactive ? "q_scale" : "p_scale";
                const char* beta = reactive ? "betaq" : "betap";
                const std::size_t frequencyIndex = reactive ? 7U : 6U;
                target->set(alpha, terms.empty() ? 0.0 : terms.front().mExponent);
                target->set(scale, terms.empty() ? 0.0 : terms.front().mCoefficient);
                const double frequencyCoefficient = parameters.coefficients[frequencyIndex];
                    target->set(beta, (std::abs(frequencyCoefficient - 1.0) <= 1e-12) ? 1.0 : 0.0);
            };
            setSide(fdepLoad, 0U, false);
            setSide(fdepLoad, 3U, true);
        } else {
            static_cast<IEELLoad*>(replacement.get())->setIEELParameters(parameters);
        }
        return replacement;
    };
}

LoadTemplateFactory makeWSCCLoadTemplate(WSCCParameters parameters)
{
    if (!std::isfinite(parameters.p1) || !std::isfinite(parameters.q1) ||
        !std::isfinite(parameters.p2) || !std::isfinite(parameters.q2) ||
        !std::isfinite(parameters.p3) || !std::isfinite(parameters.q3) ||
        !std::isfinite(parameters.p4) || !std::isfinite(parameters.q4) ||
        !std::isfinite(parameters.lpd) || !std::isfinite(parameters.lqd) ||
        !std::isfinite(parameters.vmin) || (parameters.vmin < 0.0)) {
        throw InvalidParameterValue("WSCC parameters must be finite and VMIN must be nonnegative");
    }
    return [parameters](const GridLoad& load) {
        validateWSCCTarget(load);
        WSCCFDepSide pSide;
        WSCCFDepSide qSide;
        const auto representation = classifyWSCC(parameters, pSide, qSide);
        const auto* bus = load.getBus();
        const double initialVoltage = (bus == nullptr) ? 1.0 : bus->getVoltage();
        const double initialFrequency = (bus == nullptr) ? 1.0 : bus->getFreq();
        const double pFactor =
            getWSCCCharacteristic(parameters, false, initialVoltage, initialFrequency);
        const double qFactor =
            getWSCCCharacteristic(parameters, true, initialVoltage, initialFrequency);
        const auto [initialP, initialQ] = getCharacteristicReferencePower(load, initialVoltage);
        if (((pFactor == 0.0) && (initialP != 0.0)) || ((qFactor == 0.0) && (initialQ != 0.0))) {
            throw InvalidParameterValue(
                "WSCC characteristic is zero at the initial operating point "
                                        "for load '" +
                                        load.getName() + "'");
        }
        const double baseP = (pFactor == 0.0) ? 0.0 : initialP / pFactor;
        const double baseQ = (qFactor == 0.0) ? 0.0 : initialQ / qFactor;
        std::unique_ptr<GridLoad> replacement;
        if (representation == WSCCRepresentation::ZIP) {
            replacement = std::make_unique<ZipLoad>(load.getName());
        } else if (representation == WSCCRepresentation::FDEP) {
            replacement = std::make_unique<FDepLoad>(load.getName());
        } else {
            replacement = std::make_unique<WSCCLoad>(load.getName());
        }

        load.GridLoad::clone(replacement.get());
        replacement->setLoad(baseP, baseQ, units::puMW);
        replacement->setFlag("usepowerfactor", false);

        if (representation == WSCCRepresentation::ZIP) {
            auto* zipLoad = static_cast<ZipLoad*>(replacement.get());
            const bool extended =
                (std::abs(parameters.p4) > 1e-12) || (std::abs(parameters.q4) > 1e-12);
            const double pConstant = baseP * (parameters.p3 + (extended ? parameters.p4 : 0.0));
            const double pCurrent = baseP * parameters.p2;
            const double pImpedance = baseP * parameters.p1;
            const double qConstant = baseQ * (parameters.q3 + (extended ? parameters.q4 : 0.0));
            const double qCurrent = baseQ * parameters.q2;
            const double qImpedance = baseQ * parameters.q1;
            zipLoad->set("p", pConstant, units::puMW);
            zipLoad->set("ip", pCurrent, units::puA);
            zipLoad->set("yp", pImpedance, units::puMW);
            zipLoad->set("q", qConstant, units::puMW);
            zipLoad->set("iq", qCurrent, units::puA);
            zipLoad->set("yq", qImpedance, units::puMW);
            zipLoad->setFlag("no_pqvoltage_limit", true);
        } else if (representation == WSCCRepresentation::FDEP) {
            auto* fdepLoad = static_cast<FDepLoad*>(replacement.get());
            fdepLoad->set("alphap", pSide.voltageExponent);
            fdepLoad->set("p_scale", pSide.scale);
            fdepLoad->set("betap", pSide.frequencyExponent);
            fdepLoad->set("alphaq", qSide.voltageExponent);
            fdepLoad->set("q_scale", qSide.scale);
            fdepLoad->set("betaq", qSide.frequencyExponent);
        } else {
            static_cast<WSCCLoad*>(replacement.get())->setWSCCParameters(parameters);
        }
        return replacement;
    };
}
}  // namespace griddyn::loads
