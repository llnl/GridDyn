/*
 * Copyright (c) 2026, Lawrence Livermore National Security
 * See the top-level NOTICE for additional details. All rights reserved.
 * SPDX-License-Identifier: BSD-3-Clause
 */

#include "ModelDescription.h"

#include "CoreExceptions.h"
#include <algorithm>
#include <cmath>
#include <cstddef>
#include <stdexcept>
#include <string>
#include <string_view>
#include <type_traits>
#include <utility>
#include <variant>
#include <vector>

namespace griddyn {
namespace {
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

bool ModelField::matches(std::string_view name) const
{
    return std::find(mNames.begin(), mNames.end(), name) != mNames.end();
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
            const auto duplicate = std::find_if(declaredNames.begin(),
                                                declaredNames.end(),
                                                [fieldName](const auto& entry) {
                                                    return entry.first == fieldName;
                                                });
            const bool parameterAndState =
                duplicate != declaredNames.end() &&
                ((duplicate->second == ModelFieldKind::PARAMETER &&
                  field.kind() == ModelFieldKind::STATE) ||
                 (duplicate->second == ModelFieldKind::STATE &&
                  field.kind() == ModelFieldKind::PARAMETER));
            if (duplicate != declaredNames.end() && !parameterAndState) {
                throw std::invalid_argument("duplicate model field name: " +
                                            std::string(fieldName));
            }
            declaredNames.emplace_back(fieldName, field.kind());
            if (field.editable()) {
                if (field.kind() == ModelFieldKind::FLAG) {
                    mFlagNames.push_back(fieldName);
                } else if (field.valueType() == ModelFieldType::STRING) {
                    mStringNames.push_back(fieldName);
                } else {
                    mNumericNames.push_back(fieldName);
                }
            }
        }
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
    const auto field = std::find_if(mFields.begin(), mFields.end(), [name](const auto& candidate) {
        return candidate.matches(name);
    });
    return field == mFields.end() ? nullptr : &*field;
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
        const auto field = std::find_if(layer->mFields.begin(),
                                        layer->mFields.end(),
                                        [name, kind](const auto& candidate) {
                                            return candidate.kind() == kind && candidate.matches(name);
                                        });
        if (field != layer->mFields.end()) {
            return &*field;
        }
    }
    return nullptr;
}

bool ModelDescription::trySetLocal(CoreObject& object,
                                   std::string_view name,
                                   double value,
                                   units::unit unitType) const
{
    if (name.empty() || name.front() == '#') {
        return true;
    }
    if (!std::isfinite(value)) {
        throw InvalidParameterValue(name);
    }
    const auto* field = findLocal(name);
    if (field == nullptr) {
        return false;
    }
    if (field->kind() == ModelFieldKind::FLAG) {
        object.setFlag(name, value != 0.0);
        return true;
    }
    if (field->kind() != ModelFieldKind::PARAMETER) {
        return false;
    }
    if (field->valueType() == ModelFieldType::STRING) {
        throw InvalidParameterValue(name);
    }
    const auto converted = field->convertValue(value, unitType, field->unitType());
    field->mWrite(object, converted, name);
    return true;
}

bool ModelDescription::trySetLocal(CoreObject& object,
                                   std::string_view name,
                                   std::string_view value) const
{
    if (name.empty() || name.front() == '#') {
        return true;
    }
    const auto* field = findLocal(name);
    if (field == nullptr || !field->editable()) {
        return false;
    }
    if (field->valueType() != ModelFieldType::STRING) {
        throw InvalidParameterValue(name);
    }
    if (!field->mWrite) {
        return false;
    }
    field->mWrite(object, std::string(value), name);
    return true;
}

std::optional<double> ModelDescription::tryGetLocal(const CoreObject& object,
                                                    std::string_view name,
                                                    units::unit unitType) const
{
    const auto* field = findLocal(name);
    if (field == nullptr || field->kind() != ModelFieldKind::PARAMETER ||
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
                   std::string_view description)
{
    return {names, unitType, description, ModelFieldKind::COMMAND, ModelFieldType::REAL};
}

ModelField state(std::string_view name, units::unit unitType, std::string_view description)
{
    return {{name}, unitType, description, ModelFieldKind::STATE, ModelFieldType::REAL};
}

bool ModelDescription::trySetFlagLocal(CoreObject& object, std::string_view name, bool value) const
{
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

}  // namespace griddyn
