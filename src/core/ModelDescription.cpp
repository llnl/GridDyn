/*
 * Copyright (c) 2026, Lawrence Livermore National Security
 * See the top-level NOTICE for additional details. All rights reserved.
 * SPDX-License-Identifier: BSD-3-Clause
 */

#include "ModelDescription.h"

#include "CoreExceptions.h"
#include <algorithm>
#include <charconv>
#include <cmath>
#include <cstddef>
#include <memory>
#include <stdexcept>
#include <string>
#include <string_view>
#include <system_error>
#include <type_traits>
#include <utility>
#include <variant>
#include <vector>

namespace griddyn {
namespace {
    bool indexed(ModelFieldKind kind)
    {
        return kind == ModelFieldKind::INDEXED_COMMAND || kind == ModelFieldKind::INDEXED_STATE;
    }

    std::optional<index_t> indexedElement(std::string_view name, std::string_view prefix)
    {
        if (!name.starts_with(prefix) || name.size() == prefix.size()) {
            return std::nullopt;
        }
        const auto suffix = name.substr(prefix.size());
        if (suffix.front() < '0' || suffix.front() > '9') {
            return std::nullopt;
        }
        index_t element = 0;
        const auto [end, error] =
            std::from_chars(suffix.data(), suffix.data() + suffix.size(), element);
        return error == std::errc{} && end == suffix.data() + suffix.size() ?
            std::optional<index_t>{element} :
            std::nullopt;
    }

    bool selected(const ModelField& field, FieldSnapshotLevel level)
    {
        return field.kind() == ModelFieldKind::FLAG ||
            field.kind() == ModelFieldKind::LOCAL_STATE ||
            (field.kind() == ModelFieldKind::PARAMETER && level == FieldSnapshotLevel::PARAMETERS);
    }

    struct SnapshotField {
        const ModelDescription* mOwner;
        const ModelField* mField;
    };

    void snapshotFields(const ModelDescription& description,
                        FieldSnapshotLevel level,
                        std::vector<SnapshotField>& fields)
    {
        if (level != FieldSnapshotLevel::STATE && level != FieldSnapshotLevel::PARAMETERS) {
            throw FileOperationError("unsupported field snapshot level");
        }
        std::vector<const ModelDescription*> hierarchy;
        for (const auto* layer = &description; layer != nullptr; layer = layer->parent()) {
            hierarchy.push_back(layer);
        }
        std::reverse(hierarchy.begin(), hierarchy.end());
        for (const auto* layer : hierarchy) {
            for (const auto& field : layer->fields()) {
                if (selected(field, level)) {
                    fields.push_back({.mOwner = layer, .mField = &field});
                }
            }
        }
    }

    double numericField(const ModelFieldValue& value)
    {
        return std::visit(
            [](const auto& stored) -> double {
                using Value = std::decay_t<decltype(stored)>;
                if constexpr (std::is_arithmetic_v<Value> || std::is_same_v<Value, CoreTime>) {
                    return static_cast<double>(stored);
                } else {
                    throw InvalidParameterValue("field has no scalar numeric getter");
                }
            },
            value);
    }
}  // namespace

ModelField::ModelField(std::initializer_list<std::string_view> names,
                       units::unit unitType,
                       std::string_view description,
                       ModelFieldKind kind,
                       ModelFieldType valueType,
                       Reader reader,
                       Writer writer,
                       Restorer restorer):
    mNames(names.begin(), names.end()), mUnits(unitType), mDescription(description), mKind(kind),
    mValueType(valueType), mRead(std::move(reader)), mWrite(std::move(writer)),
    mRestore(std::move(restorer))
{
    if (mNames.empty() || std::any_of(mNames.begin(), mNames.end(), [](std::string_view name) {
            return name.empty();
        })) {
        throw std::invalid_argument("model fields need nonempty names");
    }
    if (selected(*this, FieldSnapshotLevel::PARAMETERS) && (!mRead || !mRestore)) {
        throw std::invalid_argument("captured model fields need storage bindings");
    }
    if ((kind == ModelFieldKind::PARAMETER || kind == ModelFieldKind::FLAG) && !mWrite) {
        throw std::invalid_argument("model controls need default member access");
    }
}

ModelField::ModelField(std::initializer_list<std::string_view> names,
                       units::unit unitType,
                       std::string_view description,
                       StateBinding binding,
                       ModelFieldKind kind):
    ModelField(names, unitType, description, kind, ModelFieldType::REAL)
{
    mState = std::make_shared<StateBinding>(std::move(binding));
}

std::optional<index_t> ModelField::stateIndex(const CoreObject& object, index_t element) const
{
    if (!mState || !mState->mLocalIndex) {
        return std::nullopt;
    }
    if (element < 0 || element >= stateCount(object)) {
        return kInvalidLocation;
    }
    return mState->mLocalIndex(object, element);
}

count_t ModelField::stateCount(const CoreObject& object) const
{
    return mState && mState->mCount ? mState->mCount(object) : 1;
}

std::string ModelField::stateName(const CoreObject& object, index_t element) const
{
    if (mState && mState->mName) {
        auto configured = mState->mName(object, element);
        if (!configured.empty()) {
            return configured;
        }
    }
    return mKind == ModelFieldKind::INDEXED_STATE ? std::string(name()) + std::to_string(element) :
                                                    std::string(name());
}

std::optional<index_t> ModelField::solverStateIndex(const CoreObject& object,
                                                    const SolverMode& mode) const
{
    return mState && mState->mSolverIndex ?
        std::optional<index_t>{mState->mSolverIndex(object, mode)} :
        std::nullopt;
}

std::optional<index_t> ModelField::stateElement(const CoreObject& object,
                                                std::string_view name) const
{
    if (!isState()) {
        return std::nullopt;
    }
    for (const auto& alias : names()) {
        if (mKind != ModelFieldKind::INDEXED_STATE) {
            if (name == alias) {
                return 0;
            }
            continue;
        }
        if (const auto element = indexedElement(name, alias)) {
            return element;
        }
    }
    if (mKind == ModelFieldKind::STATE && mState && mState->mName && name == stateName(object)) {
        return 0;
    }
    return std::nullopt;
}

const ModelField* ModelDescription::findState(const CoreObject& object,
                                              std::string_view name,
                                              index_t& element) const
{
    for (const auto* table = this; table != nullptr; table = table->parent()) {
        const ModelField* indexedMatch = nullptr;
        for (const auto& field : table->fields()) {
            if (const auto match = field.stateElement(object, name)) {
                if (field.kind() != ModelFieldKind::INDEXED_STATE) {
                    element = *match;
                    return table == this ? &field : find(field.name(), ModelFieldKind::STATE);
                }
                if (indexedMatch == nullptr) {
                    indexedMatch = &field;
                    element = *match;
                }
            }
        }
        if (indexedMatch != nullptr) {
            return table == this ? indexedMatch :
                                   find(indexedMatch->name(), ModelFieldKind::INDEXED_STATE);
        }
    }
    return nullptr;
}

std::optional<index_t> ModelDescription::stateIndex(const CoreObject& object,
                                                    std::string_view name) const
{
    index_t element = 0;
    const auto* field = findState(object, name, element);
    return field != nullptr ? field->stateIndex(object, element) : std::nullopt;
}

stringVec ModelDescription::solverStateNames(const CoreObject& object) const
{
    stringVec names;
    for (const auto* field : mStates) {
        for (index_t element = 0; element < field->stateCount(object); ++element) {
            const auto index = field->stateIndex(object, element);
            if (!index || std::cmp_less(*index, 0) || *index >= kInvalidLocation) {
                continue;
            }
            const auto slot = static_cast<std::size_t>(*index);
            if (names.size() <= slot) {
                names.resize(slot + 1);
            }
            if (names[slot].empty()) {
                names[slot] = field->stateName(object, element);
            }
        }
    }
    return names;
}

bool ModelField::matches(std::string_view name) const
{
    if (!indexed(mKind) || mKind == ModelFieldKind::INDEXED_STATE) {
        if (std::find(mNames.begin(), mNames.end(), name) != mNames.end()) {
            return true;
        }
        if (!indexed(mKind)) {
            return false;
        }
    }
    return std::any_of(mNames.begin(), mNames.end(), [name](std::string_view prefix) {
        return indexedElement(name, prefix).has_value();
    });
}

ModelFieldValue ModelField::value(const CoreObject& object) const
{
    if (!mRead) {
        throw UnrecognizedParameter(name());
    }
    return mRead(object);
}

double ModelField::convertValue(double value, units::unit sourceUnit, units::unit targetUnit) const
{
    if (units::is_error(sourceUnit) || units::is_error(targetUnit)) {
        throw InvalidParameterValue(std::string(name()) + ": invalid units");
    }
    if (sourceUnit == targetUnit || units::is_default(sourceUnit) ||
        units::is_default(targetUnit)) {
        return value;
    }
    if (std::isnan(units::convert(1.0, sourceUnit, targetUnit))) {
        throw InvalidParameterValue(std::string(name()) + ": incompatible units");
    }
    return units::convert(value, sourceUnit, targetUnit);
}

ModelDescription::ModelDescription(std::string_view name,
                                   const ModelDescription* parent,
                                   std::initializer_list<ModelField> fields,
                                   const std::type_info& exactType):
    mName(name), mParent(parent), mFields(fields), mExactType(&exactType)
{
    if (name.empty()) {
        throw std::invalid_argument("model descriptions need a class name");
    }
    for (const auto* ancestor = parent; ancestor != nullptr; ancestor = ancestor->parent()) {
        if (ancestor->name() == name) {
            throw std::invalid_argument("duplicate declaring class in a model description");
        }
    }
    std::vector<std::pair<std::string_view, ModelFieldKind>> declaredNames;
    for (const auto& field : mFields) {
        for (const auto& fieldName : field.names()) {
            for (const auto& [declaredName, declaredKind] : declaredNames) {
                if (declaredName != fieldName ||
                    (declaredKind == ModelFieldKind::INDEXED_COMMAND) !=
                        (field.kind() == ModelFieldKind::INDEXED_COMMAND)) {
                    continue;
                }
                const bool controlAndState = ((declaredKind == ModelFieldKind::PARAMETER ||
                                               declaredKind == ModelFieldKind::COMMAND ||
                                               declaredKind == ModelFieldKind::FLAG) &&
                                              field.isState()) ||
                    ((declaredKind == ModelFieldKind::STATE ||
                      declaredKind == ModelFieldKind::INDEXED_STATE ||
                      declaredKind == ModelFieldKind::STATE_REFERENCE) &&
                     field.editable());
                if (!controlAndState) {
                    throw std::invalid_argument("duplicate model field name: " +
                                                std::string(fieldName));
                }
            }
            declaredNames.emplace_back(fieldName, field.kind());
            if (field.editable()) {
                if (field.kind() == ModelFieldKind::FLAG ||
                    ((field.kind() == ModelFieldKind::COMMAND ||
                      field.kind() == ModelFieldKind::INDEXED_COMMAND) &&
                     field.valueType() == ModelFieldType::BOOLEAN)) {
                    mFlagNames.push_back(fieldName);
                } else if (field.valueType() == ModelFieldType::STRING) {
                    mStringNames.push_back(fieldName);
                } else {
                    mNumericNames.push_back(fieldName);
                }
            }
        }
    }
    for (const auto* table = this; table != nullptr; table = table->parent()) {
        for (const auto& field : table->fields()) {
            if (field.isState() && find(field.name(), ModelFieldKind::STATE) == &field) {
                mStates.push_back(&field);
            }
        }
    }
}

void ModelDescription::getParameterStrings(stringVec& names, ParamStringType type) const
{
    const auto appendNames = [this, &names](auto member) {
        for (const auto* description = this; description != nullptr;
             description = description->parent()) {
            const auto& declaredNames = description->*member;
            names.insert(names.end(), declaredNames.begin(), declaredNames.end());
        }
    };
    switch (type) {
        case ParamStringType::ALL:
            appendNames(&ModelDescription::mNumericNames);
            names.emplace_back("#");
            appendNames(&ModelDescription::mStringNames);
            break;
        case ParamStringType::LOCAL_NUM:
            names.assign(mNumericNames.begin(), mNumericNames.end());
            break;
        case ParamStringType::LOCAL_STR:
            names.assign(mStringNames.begin(), mStringNames.end());
            break;
        case ParamStringType::LOCAL_FLAGS:
            names.assign(mFlagNames.begin(), mFlagNames.end());
            break;
        case ParamStringType::NUMERIC:
            appendNames(&ModelDescription::mNumericNames);
            break;
        case ParamStringType::STR:
            appendNames(&ModelDescription::mStringNames);
            break;
        case ParamStringType::FLAGS:
            appendNames(&ModelDescription::mFlagNames);
            break;
    }
}

const ModelField* ModelDescription::findLocal(std::string_view name) const
{
    // A parameter can share a name with a solver state. Keep ordinary set/get
    // dispatch independent of the order of those two declarations.
    const auto parameterField =
        std::find_if(mFields.begin(), mFields.end(), [name](const auto& candidate) {
            return candidate.kind() == ModelFieldKind::PARAMETER && candidate.matches(name);
        });
    if (parameterField != mFields.end()) {
        return &*parameterField;
    }
    const ModelField* indexedMatch = nullptr;
    for (const auto& field : mFields) {
        if (field.matches(name)) {
            if (!indexed(field.kind())) {
                return &field;
            }
            if (indexedMatch == nullptr) {
                indexedMatch = &field;
            }
        }
    }
    return indexedMatch;
}

const ModelField* ModelDescription::find(std::string_view name) const
{
    for (const auto* layer = this; layer != nullptr; layer = layer->parent()) {
        if (const auto* field = layer->findLocal(name)) {
            return field;
        }
    }
    return nullptr;
}

const ModelField* ModelDescription::find(std::string_view name, ModelFieldKind kind) const
{
    for (const auto* layer = this; layer != nullptr; layer = layer->parent()) {
        const ModelField* indexedMatch = nullptr;
        for (const auto& field : layer->mFields) {
            if ((field.kind() == kind || (kind == ModelFieldKind::STATE && field.isState())) &&
                field.matches(name)) {
                if (!indexed(field.kind())) {
                    return &field;
                }
                if (indexedMatch == nullptr) {
                    indexedMatch = &field;
                }
            }
        }
        if (indexedMatch != nullptr) {
            return indexedMatch;
        }
    }
    return nullptr;
}

SetResult ModelDescription::trySet(CoreObject& object,
                                   std::string_view name,
                                   double value,
                                   units::unit unitType) const
{
    // Preserve the input gate before looking up even the derived description.
    if (isIgnoredParameter(name) || !std::isfinite(value)) {
        return trySetLocal(object, name, value, unitType);
    }
    for (const auto* description = derivedDescription(object); description != this;
         description = description->parent()) {
        const auto result = description->trySetLocal(object, name, value, unitType);
        if (isSetHandled(result)) {
            return result;
        }
    }
    return trySetLocal(object, name, value, unitType);
}

SetResult ModelDescription::trySet(CoreObject& object,
                                   std::string_view name,
                                   std::string_view value) const
{
    if (isIgnoredParameter(name)) {
        return SetResult::IGNORED;
    }
    for (const auto* description = derivedDescription(object);;
         description = description->parent()) {
        const auto* field = description->findLocal(name);
        // Leave textual flags and Boolean commands to the public string handlers.
        // Other controls retain trySetLocal's type checks, including numeric commands.
        if (field != nullptr && field->kind() != ModelFieldKind::FLAG &&
            (field->kind() != ModelFieldKind::COMMAND ||
             field->valueType() != ModelFieldType::BOOLEAN)) {
            const auto result = description->trySetLocal(object, name, value);
            if (isSetHandled(result) || description == this) {
                return result;
            }
        }
        if (description == this) {
            return SetResult::NO_MATCH;
        }
    }
}

SetResult ModelDescription::trySetLocal(CoreObject& object,
                                        std::string_view name,
                                        double value,
                                        units::unit unitType) const
{
    if (isIgnoredParameter(name)) {
        return SetResult::IGNORED;
    }
    if (!std::isfinite(value)) {
        throw InvalidParameterValue(name);
    }
    const auto* field = findLocal(name);
    if (field == nullptr) {
        return SetResult::NO_MATCH;
    }
    if (field->kind() == ModelFieldKind::INDEXED_COMMAND) {
        return SetResult::INDEXED_COMMAND;
    }
    if (field->kind() == ModelFieldKind::FLAG ||
        (field->kind() == ModelFieldKind::COMMAND &&
         field->valueType() == ModelFieldType::BOOLEAN)) {
        object.setFlag(name, value != 0.0);
        return SetResult::FLAG;
    }
    if (field->kind() == ModelFieldKind::COMMAND) {
        return SetResult::COMMAND;
    }
    if (field->kind() != ModelFieldKind::PARAMETER) {
        return SetResult::NO_MATCH;
    }
    if (field->valueType() == ModelFieldType::STRING) {
        throw InvalidParameterValue(name);
    }
    const auto converted = field->convertValue(value, unitType, field->unitType());
    field->mWrite(object, converted, name);
    return SetResult::PARAMETER;
}

SetResult ModelDescription::trySetLocal(CoreObject& object,
                                        std::string_view name,
                                        std::string_view value) const
{
    if (isIgnoredParameter(name)) {
        return SetResult::IGNORED;
    }
    const auto* field = findLocal(name);
    if (field == nullptr || !field->editable()) {
        return SetResult::NO_MATCH;
    }
    if (field->valueType() != ModelFieldType::STRING) {
        throw InvalidParameterValue(name);
    }
    if (field->kind() == ModelFieldKind::INDEXED_COMMAND) {
        return SetResult::INDEXED_COMMAND;
    }
    if (field->kind() == ModelFieldKind::COMMAND) {
        return SetResult::COMMAND;
    }
    field->mWrite(object, std::string(value), name);
    return SetResult::PARAMETER;
}

const ModelDescription* ModelDescription::derivedDescription(const CoreObject& object) const
{
    const auto* derived = &object.getModelDescription();
    for (const auto* description = derived; description != nullptr;
         description = description->parent()) {
        if (description == this) {
            return derived;
        }
    }
    return this;
}

std::optional<double> ModelDescription::tryGet(const CoreObject& object,
                                               std::string_view name,
                                               units::unit unitType) const
{
    for (const auto* description = derivedDescription(object); description != this;
         description = description->parent()) {
        if (const auto value = description->tryGetLocal(object, name, unitType)) {
            return value;
        }
    }
    return tryGetLocal(object, name, unitType);
}

std::optional<double> ModelDescription::tryGetLocal(const CoreObject& object,
                                                    std::string_view name,
                                                    units::unit unitType) const
{
    const auto* field = findLocal(name);
    if (field == nullptr) {
        return std::nullopt;
    }
    if (field->kind() == ModelFieldKind::FLAG) {
        return std::get<bool>(field->mRead(object)) ? 1.0 : 0.0;
    }
    if (field->kind() != ModelFieldKind::PARAMETER ||
        field->valueType() == ModelFieldType::STRING) {
        return std::nullopt;
    }
    return field->convertValue(numericField(field->mRead(object)), field->unitType(), unitType);
}

std::optional<std::string> ModelDescription::tryGetStringLocal(const CoreObject& object,
                                                               std::string_view name) const
{
    const auto* field = findLocal(name);
    if (field == nullptr || field->kind() != ModelFieldKind::PARAMETER ||
        field->valueType() != ModelFieldType::STRING) {
        return std::nullopt;
    }
    return std::get<std::string>(field->mRead(object));
}

ModelField command(std::initializer_list<std::string_view> names,
                   units::unit unitType,
                   std::string_view description,
                   ModelFieldType valueType)
{
    return {names, unitType, description, ModelFieldKind::COMMAND, valueType};
}

ModelField indexedCommand(std::initializer_list<std::string_view> prefixes,
                          units::unit unitType,
                          std::string_view description,
                          ModelFieldType valueType)
{
    return {prefixes, unitType, description, ModelFieldKind::INDEXED_COMMAND, valueType};
}

ModelField state(std::string_view name, units::unit unitType, std::string_view description)
{
    return {{name}, unitType, description, ModelFieldKind::STATE, ModelFieldType::REAL};
}

bool ModelDescription::trySetFlag(CoreObject& object, std::string_view name, bool value) const
{
    if (isIgnoredParameter(name)) {
        return true;
    }
    for (const auto* description = derivedDescription(object); description != this;
         description = description->parent()) {
        if (description->trySetFlagLocal(object, name, value)) {
            return true;
        }
    }
    return trySetFlagLocal(object, name, value);
}

std::optional<bool> ModelDescription::tryGetFlag(const CoreObject& object,
                                                 std::string_view name) const
{
    for (const auto* description = derivedDescription(object); description != this;
         description = description->parent()) {
        if (const auto value = description->tryGetFlagLocal(object, name)) {
            return value;
        }
    }
    return tryGetFlagLocal(object, name);
}

bool ModelDescription::trySetFlagLocal(CoreObject& object, std::string_view name, bool value) const
{
    if (isIgnoredParameter(name)) {
        return true;
    }
    const auto* field = findLocal(name);
    if (field == nullptr || field->kind() != ModelFieldKind::FLAG || !field->mWrite) {
        return false;
    }
    field->mWrite(object, value, name);
    return true;
}

std::optional<bool> ModelDescription::tryGetFlagLocal(const CoreObject& object,
                                                      std::string_view name) const
{
    const auto* field = findLocal(name);
    if (field == nullptr || field->kind() != ModelFieldKind::FLAG || !field->mWrite) {
        return std::nullopt;
    }
    return std::get<bool>(field->mRead(object));
}

ModelFieldSnapshot ModelFieldSnapshot::capture(const CoreObject& object, FieldSnapshotLevel level)
{
    const auto& description = object.getModelDescription();
    if (!description.describesExactType(object)) {
        throw FileOperationError("field snapshot requires a description of the exact model class");
    }
    ModelFieldSnapshot snapshot;
    snapshot.mModelType = description.name();
    snapshot.mLevel = level;
    std::vector<SnapshotField> fields;
    snapshotFields(description, level, fields);
    snapshot.mFields.reserve(fields.size());
    for (const auto& binding : fields) {
        snapshot.mFields.push_back({.mDeclaringClass = std::string(binding.mOwner->name()),
                                    .mName = std::string(binding.mField->name()),
                                    .mValue = binding.mField->mRead(object)});
    }
    return snapshot;
}

void ModelFieldSnapshot::restore(CoreObject& object) const
{
    const auto& description = object.getModelDescription();
    if (!description.describesExactType(object) || mModelType != description.name()) {
        throw FileOperationError("field snapshot model class does not match");
    }
    std::vector<SnapshotField> expected;
    snapshotFields(description, mLevel, expected);
    if (expected.size() != mFields.size()) {
        throw FileOperationError("field snapshot layout does not match");
    }
    std::vector<ModelField::Assignment> assignments;
    assignments.reserve(mFields.size());
    for (std::size_t ii = 0; ii < mFields.size(); ++ii) {
        if (mFields[ii].mDeclaringClass != expected[ii].mOwner->name() ||
            mFields[ii].mName != expected[ii].mField->name()) {
            throw FileOperationError("field snapshot layout does not match");
        }
        assignments.push_back(expected[ii].mField->mRestore(object, mFields[ii].mValue));
    }
    for (auto& assignment : assignments) {
        assignment();
    }
}

ModelField state(std::initializer_list<std::string_view> names,
                 units::unit unitType,
                 index_t index,
                 std::string_view description)
{
    return {names, unitType, description, {.mLocalIndex = [index](const CoreObject&, index_t) {
                return index;
            }}};
}

}  // namespace griddyn
