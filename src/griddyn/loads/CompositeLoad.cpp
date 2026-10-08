/*
 * Copyright (c) 2014-2026, Lawrence Livermore National Security
 * See the top-level NOTICE for additional details. All rights reserved.
 * SPDX-License-Identifier: BSD-3-Clause
 */

#include "CompositeLoad.h"

#include "ZipLoad.h"
#include "core/CoreExceptions.h"
#include "core/CoreObjectTemplates.hpp"
#include "core/ObjectFactoryTemplates.hpp"
#include <algorithm>
#include <array>
#include <charconv>
#include <cmath>
#include <cstddef>
#include <string>
#include <string_view>
#include <system_error>
#include <vector>

namespace griddyn::loads {
namespace {
    bool parseIndexedParameter(std::string_view param, std::string_view prefix, index_t& index)
    {
        if (!param.starts_with(prefix)) {
            return false;
        }
        const auto digits = param.substr(prefix.size());
        if (digits.empty()) {
            return false;
        }
        unsigned int parsedIndex = 0;
        const auto [end, error] =
            std::from_chars(digits.data(), digits.data() + digits.size(), parsedIndex);
        if ((error != std::errc{}) || (end != digits.data() + digits.size()) ||
            (parsedIndex == 0U)) {
            return false;
        }
        index = static_cast<index_t>(parsedIndex - 1U);
        return true;
    }
}  // namespace

CompositeLoad::CompositeLoad(const std::string& objName): GridLoad(objName) {}

CoreObject* CompositeLoad::clone(CoreObject* obj) const
{
    auto* cloned = cloneBase<CompositeLoad, GridLoad>(this, obj);
    if (cloned != nullptr) {
        cloned->componentFractions = componentFractions;
        cloned->componentReactiveBases = componentReactiveBases;
        cloned->zipReferenceTerms = zipReferenceTerms;
    }
    return cloned;
}

std::vector<GridLoad*> CompositeLoad::getComponents() const
{
    std::vector<GridLoad*> components;
    components.reserve(getSubObjects().size());
    for (auto* subObject : getSubObjects()) {
        auto* load = dynamic_cast<GridLoad*>(subObject);
        if (load != nullptr) {
            components.push_back(load);
        }
    }
    return components;
}

count_t CompositeLoad::componentCount() const
{
    return static_cast<count_t>(getComponents().size());
}

GridLoad* CompositeLoad::component(index_t index) const
{
    const auto components = getComponents();
    return (index >= 0 && static_cast<std::size_t>(index) < components.size()) ?
        components[static_cast<std::size_t>(index)] :
        nullptr;
}

void CompositeLoad::add(CoreObject* obj)
{
    auto* load = dynamic_cast<GridLoad*>(obj);
    if (load == nullptr) {
        throw(UnrecognizedObjectException(this));
    }

    const auto currentComponents = getComponents();
    if (std::find(currentComponents.begin(), currentComponents.end(), load) !=
        currentComponents.end()) {
        return;
    }

    addSubObject(load);
    const auto componentIndex = currentComponents.size();
    if (componentFractions.size() <= componentIndex) {
        componentFractions.resize(componentIndex + 1U, -1.0);
    }
    if (componentReactiveBases.size() <= componentIndex) {
        componentReactiveBases.resize(componentIndex + 1U);
    }
    if (zipReferenceTerms.size() <= componentIndex) {
        zipReferenceTerms.resize(componentIndex + 1U);
    }
}

void CompositeLoad::remove(CoreObject* obj)
{
    auto* load = dynamic_cast<GridLoad*>(obj);
    if (load == nullptr) {
        GridLoad::remove(obj);
        return;
    }

    const auto currentComponents = getComponents();
    const auto found = std::find(currentComponents.begin(), currentComponents.end(), load);
    if (found != currentComponents.end()) {
        const auto componentIndex = static_cast<std::size_t>(found - currentComponents.begin());
        if (componentIndex < componentFractions.size()) {
            componentFractions.erase(componentFractions.begin() +
                                     static_cast<std::ptrdiff_t>(componentIndex));
        }
        if (componentIndex < componentReactiveBases.size()) {
            componentReactiveBases.erase(componentReactiveBases.begin() +
                                         static_cast<std::ptrdiff_t>(componentIndex));
        }
        if (componentIndex < zipReferenceTerms.size()) {
            zipReferenceTerms.erase(zipReferenceTerms.begin() +
                                    static_cast<std::ptrdiff_t>(componentIndex));
        }
    }
    GridLoad::remove(obj);
}

void CompositeLoad::getParameterStrings(stringVec& pstr, ParamStringType pstype) const
{
    static constexpr auto numericStrings =
        std::array<std::string_view, 2>{"fraction", "componentfraction"};
    static constexpr std::array<std::string_view, 1> stringStrings{"component"};
    static constexpr std::array<std::string_view, 0> flagStrings{};
    getParamString<CompositeLoad, GridLoad>(
        this, pstr, numericStrings, stringStrings, flagStrings, pstype);
}

void CompositeLoad::set(std::string_view param, std::string_view val)
{
    index_t componentIndex = 0;
    if (parseIndexedParameter(param, "component", componentIndex)) {
        auto factory = CoreObjectFactory::instance()->getFactory("load");
        if ((factory == nullptr) || !factory->isValidType(val)) {
            throw(InvalidParameterValue(std::string{val}));
        }
        CoreObject* const newObject = factory->makeObject(val);
        auto* load = dynamic_cast<GridLoad*>(newObject);
        if (load == nullptr) {
            delete newObject;
            throw(InvalidParameterValue(std::string{val}));
        }
        if (componentIndex != componentCount()) {
            delete load;
            throw(InvalidParameterValue(std::string{param}));
        }
        add(load);
        return;
    }
    GridLoad::set(param, val);
}

void CompositeLoad::setComponentFraction(index_t index, double fraction)
{
    if (index < 0 || !std::isfinite(fraction) || (fraction < 0.0) || (fraction > 1.0)) {
        throw(InvalidParameterValue("composite component fraction must be in [0, 1]"));
    }
    if (opFlags[POWERFLOW_INITIALIZED]) {
        throw(InvalidParameterValue(
            "composite component fractions must be set before power-flow initialization"));
    }
    const auto componentIndex = static_cast<std::size_t>(index);
    if (componentFractions.size() <= componentIndex) {
        componentFractions.resize(componentIndex + 1U, -1.0);
    }
    componentFractions[componentIndex] = fraction;
}

void CompositeLoad::setComponentReactiveBase(index_t index, double reactivePower)
{
    if ((index < 0) || (static_cast<std::size_t>(index) >= getComponents().size()) ||
        !std::isfinite(reactivePower) || opFlags[POWERFLOW_INITIALIZED]) {
        throw InvalidParameterValue("invalid composite component reactive base");
    }
    componentReactiveBases.resize(
        std::max(componentReactiveBases.size(), static_cast<std::size_t>(index) + 1U));
    componentReactiveBases[static_cast<std::size_t>(index)] = reactivePower;
}

void CompositeLoad::set(std::string_view param, double val, units::unit unitType)
{
    index_t componentIndex = 0;
    if (parseIndexedParameter(param, "componentfraction", componentIndex) ||
        parseIndexedParameter(param, "fraction", componentIndex)) {
        setComponentFraction(componentIndex, val);
        return;
    }
    GridLoad::set(param, val, unitType);
}

double CompositeLoad::get(std::string_view param, units::unit unitType) const
{
    index_t componentIndex = 0;
    if (parseIndexedParameter(param, "componentfraction", componentIndex) ||
        parseIndexedParameter(param, "fraction", componentIndex)) {
        return (componentIndex >= 0 &&
                static_cast<std::size_t>(componentIndex) < componentFractions.size()) ?
            componentFractions[static_cast<std::size_t>(componentIndex)] :
            kNullVal;
    }
    return GridLoad::get(param, unitType);
}

std::vector<double> CompositeLoad::resolveFractions() const
{
    const auto components = getComponents();
    std::vector<double> fractions(components.size(), -1.0);
    for (std::size_t index = components.size(); index < componentFractions.size(); ++index) {
        if (componentFractions[index] >= 0.0) {
            throw(InvalidParameterValue("composite fraction specified for a missing component"));
        }
    }
    if (components.empty()) {
        return fractions;
    }

    double specifiedTotal = 0.0;
    count_t unspecifiedCount = 0;
    for (std::size_t index = 0; index < components.size(); ++index) {
        if ((index < componentFractions.size()) && (componentFractions[index] >= 0.0)) {
            fractions[index] = componentFractions[index];
            specifiedTotal += fractions[index];
        } else {
            ++unspecifiedCount;
        }
    }
    constexpr double fractionTolerance = 1e-9;
    if (specifiedTotal > 1.0 + fractionTolerance) {
        throw(InvalidParameterValue("composite component fractions sum to more than one"));
    }
    if (unspecifiedCount == 0) {
        if (std::abs(specifiedTotal - 1.0) > fractionTolerance) {
            throw(InvalidParameterValue("fully specified composite fractions must sum to one"));
        }
        return fractions;
    }

    const double remainingFraction = std::max(0.0, 1.0 - specifiedTotal);
    const double defaultFraction = remainingFraction / static_cast<double>(unspecifiedCount);
    for (auto& fraction : fractions) {
        if (fraction < 0.0) {
            fraction = defaultFraction;
        }
    }
    return fractions;
}

void CompositeLoad::allocateComponentPowers()
{
    const auto components = getComponents();
    const auto fractions = resolveFractions();
    if (zipReferenceTerms.size() < components.size()) {
        zipReferenceTerms.resize(components.size());
    }
    for (std::size_t index = 0; index < components.size(); ++index) {
        auto* zipLoad = dynamic_cast<ZipLoad*>(components[index]);
        auto& zipTerms = zipReferenceTerms[index];
        if (!zipTerms.captured) {
            if (zipLoad != nullptr) {
                zipTerms.ip = zipLoad->get("ip");
                zipTerms.iq = zipLoad->get("iq");
                zipTerms.yp = zipLoad->get("yp");
                zipTerms.yq = zipLoad->get("yq");
            }
            zipTerms.captured = true;
        }
        // Allocate electrical bases without invoking model-specific p setters.
        // WECCMotor3 transfers its allocated P to Pmot during initialization;
        // its operating-point Q is solved by the motor circuit.
        double reactivePower = Q * fractions[index];
        if (index < componentReactiveBases.size()) {
            const auto& reactiveBase = componentReactiveBases[index];
            if (reactiveBase.has_value()) {
                reactivePower = reactiveBase.value();
            }
        }
        components[index]->setLoad(P * fractions[index], reactivePower);
        if (zipLoad != nullptr) {
            zipLoad->set("ip", zipTerms.ip * fractions[index]);
            zipLoad->set("iq", zipTerms.iq * fractions[index]);
            zipLoad->set("yp", zipTerms.yp * fractions[index]);
            zipLoad->set("yq", zipTerms.yq * fractions[index]);
        }
    }
}

void CompositeLoad::pFlowObjectInitializeA(CoreTime time0, std::uint32_t flags)
{
    allocateComponentPowers();
    GridLoad::pFlowObjectInitializeA(time0, flags);
}

double CompositeLoad::getRealPower(const IOdata& inputs,
                                   const StateData& stateData,
                                   const SolverMode& sMode) const
{
    const auto components = getComponents();
    if (components.empty()) {
        return GridLoad::getRealPower(inputs, stateData, sMode);
    }
    if (!isConnected()) {
        return 0.0;
    }
    double power = 0.0;
    for (const auto* load : components) {
        if (load->isConnected()) {
            power += load->getRealPower(inputs, stateData, sMode);
        }
    }
    return power;
}

double CompositeLoad::getReactivePower(const IOdata& inputs,
                                       const StateData& stateData,
                                       const SolverMode& sMode) const
{
    const auto components = getComponents();
    if (components.empty()) {
        return GridLoad::getReactivePower(inputs, stateData, sMode);
    }
    if (!isConnected()) {
        return 0.0;
    }
    double power = 0.0;
    for (const auto* load : components) {
        if (load->isConnected()) {
            power += load->getReactivePower(inputs, stateData, sMode);
        }
    }
    return power;
}

double CompositeLoad::getRealPower(double voltage) const
{
    const auto components = getComponents();
    if (components.empty()) {
        return GridLoad::getRealPower(voltage);
    }
    if (!isConnected()) {
        return 0.0;
    }
    double power = 0.0;
    for (const auto* load : components) {
        if (load->isConnected()) {
            power += load->getRealPower(voltage);
        }
    }
    return power;
}

double CompositeLoad::getReactivePower(double voltage) const
{
    const auto components = getComponents();
    if (components.empty()) {
        return GridLoad::getReactivePower(voltage);
    }
    if (!isConnected()) {
        return 0.0;
    }
    double power = 0.0;
    for (const auto* load : components) {
        if (load->isConnected()) {
            power += load->getReactivePower(voltage);
        }
    }
    return power;
}

double CompositeLoad::getRealPower() const
{
    const auto components = getComponents();
    if (components.empty()) {
        return GridLoad::getRealPower();
    }
    if (!isConnected()) {
        return 0.0;
    }
    double power = 0.0;
    for (const auto* load : components) {
        if (load->isConnected()) {
            power += load->getRealPower();
        }
    }
    return power;
}

double CompositeLoad::getReactivePower() const
{
    const auto components = getComponents();
    if (components.empty()) {
        return GridLoad::getReactivePower();
    }
    if (!isConnected()) {
        return 0.0;
    }
    double power = 0.0;
    for (const auto* load : components) {
        if (load->isConnected()) {
            power += load->getReactivePower();
        }
    }
    return power;
}

void CompositeLoad::outputPartialDerivatives(const IOdata& inputs,
                                             const StateData& stateData,
                                             MatrixData<double>& matrixData,
                                             const SolverMode& sMode)
{
    for (auto* load : getComponents()) {
        if (load->stateSize(sMode) > 0) {
            load->outputPartialDerivatives(inputs, stateData, matrixData, sMode);
        }
    }
}

void CompositeLoad::ioPartialDerivatives(const IOdata& inputs,
                                         const StateData& stateData,
                                         MatrixData<double>& matrixData,
                                         const IOlocs& inputLocs,
                                         const SolverMode& sMode)
{
    for (auto* load : getComponents()) {
        load->ioPartialDerivatives(inputs, stateData, matrixData, inputLocs, sMode);
    }
}

count_t CompositeLoad::outputDependencyCount(index_t outputNum, const SolverMode& sMode) const
{
    count_t dependencyCount = 0;
    for (const auto* load : getComponents()) {
        dependencyCount += load->outputDependencyCount(outputNum, sMode);
    }
    return dependencyCount;
}
}  // namespace griddyn::loads
