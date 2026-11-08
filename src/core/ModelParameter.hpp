/*
 * Copyright (c) 2026, Lawrence Livermore National Security
 * See the top-level NOTICE for additional details. All rights reserved.
 * SPDX-License-Identifier: BSD-3-Clause
 */

#pragma once

#include "CoreExceptions.h"
#include "ModelDescription.h"
#include <array>
#include <bitset>
#include <cmath>
#include <concepts>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <initializer_list>
#include <limits>
#include <stdexcept>
#include <string>
#include <string_view>
#include <type_traits>
#include <utility>
#include <variant>
#include <vector>

namespace griddyn {
namespace detail {
    struct AcceptModelValue {
        template<class Value>
        bool operator()(const Value& /*value*/) const
        {
            return true;
        }
    };
    template<class Model>
    Model& fieldOwner(CoreObject& object)
    {
        auto* model = dynamic_cast<Model*>(&object);
        if (model == nullptr) {
            throw InvalidParameterValue("field belongs to a different model class");
        }
        return *model;
    }

    template<class Model>
    const Model& fieldOwner(const CoreObject& object)
    {
        const auto* model = dynamic_cast<const Model*>(&object);
        if (model == nullptr) {
            throw InvalidParameterValue("field belongs to a different model class");
        }
        return *model;
    }

    template<class Value>
    auto storedValue(const Value& value)
    {
        if constexpr (std::is_enum_v<Value>) {
            return storedValue(static_cast<std::underlying_type_t<Value>>(value));
        } else if constexpr (std::is_integral_v<Value> && std::is_signed_v<Value>) {
            return static_cast<std::int64_t>(value);
        } else if constexpr (std::is_integral_v<Value> && !std::is_same_v<Value, bool>) {
            return static_cast<std::uint64_t>(value);
        } else if constexpr (std::is_floating_point_v<Value>) {
            return static_cast<double>(value);
        } else {
            return value;
        }
    }

    template<class Value>
    Value restoreValue(const ModelFieldValue& value)
    {
        if constexpr (std::is_enum_v<Value>) {
            return static_cast<Value>(restoreValue<std::underlying_type_t<Value>>(value));
        } else {
            using Stored = decltype(storedValue(std::declval<Value>()));
            const auto* stored = std::get_if<Stored>(&value);
            if (stored == nullptr) {
                throw FileOperationError("snapshot field has an incompatible value type");
            }
            if constexpr (std::is_integral_v<Value> && !std::is_same_v<Value, bool>) {
                if (!std::in_range<Value>(*stored)) {
                    throw FileOperationError("snapshot integer is out of storage range");
                }
            }
            if constexpr (std::is_floating_point_v<Value>) {
                if (std::isfinite(*stored) &&
                    (*stored > std::numeric_limits<Value>::max() ||
                     *stored < std::numeric_limits<Value>::lowest())) {
                    throw FileOperationError("snapshot real value is out of storage range");
                }
            }
            return static_cast<Value>(*stored);
        }
    }

    template<class Value>
    Value numericValue(double value)
    {
        if constexpr (std::is_enum_v<Value>) {
            return static_cast<Value>(numericValue<std::underlying_type_t<Value>>(value));
        } else if constexpr (std::is_same_v<Value, CoreTime>) {
            if (!std::isfinite(value) || value <= static_cast<double>(negTime) ||
                value >= static_cast<double>(maxTime)) {
                throw InvalidParameterValue("time is outside the simulation clock range");
            }
            return CoreTime(value);
        } else if constexpr (std::is_same_v<Value, bool>) {
            if (value != 0.0 && value != 1.0) {
                throw InvalidParameterValue("boolean parameter requires zero or one");
            }
            return value != 0.0;
        } else if constexpr (std::is_integral_v<Value>) {
            // Use a strict upper bound for 64-bit integers: double rounds their
            // maximum values up to the first unrepresentable integer.
            constexpr auto digits = std::numeric_limits<Value>::digits;
            const auto upper = std::ldexp(1.0, digits);
            const auto lower = std::is_signed_v<Value> ? -upper : 0.0;
            if (!std::isfinite(value) || std::trunc(value) != value || value < lower ||
                value >= upper) {
                throw InvalidParameterValue("integer parameter is out of storage range");
            }
            return static_cast<Value>(value);
        } else if constexpr (std::is_floating_point_v<Value>) {
            if (!std::isfinite(value) || value > std::numeric_limits<Value>::max() ||
                value < std::numeric_limits<Value>::lowest()) {
                throw InvalidParameterValue("real parameter is out of storage range");
            }
            return static_cast<Value>(value);
        } else {
            static_assert(std::is_arithmetic_v<Value>, "field is not numeric");
        }
    }

    template<class Model, class Value>
    ModelField bindModelMember(std::initializer_list<std::string_view> names,
                               Value Model::* member,
                               units::unit unitType,
                               std::string_view description,
                               ModelFieldKind kind,
                               ModelField::Writer writer = {})
    {
        static_assert(std::is_nothrow_swappable_v<Value>,
                      "snapshot commit requires nonthrowing member assignment");
        const auto valueType =
            static_cast<ModelFieldType>(ModelFieldValue(storedValue(Value{})).index());
        ModelField::Reader read = [member](const CoreObject& object) -> ModelFieldValue {
            return storedValue(fieldOwner<Model>(object).*member);
        };
        ModelField::Restorer restore =
            [member, kind](CoreObject& object,
                           const ModelFieldValue& value) -> ModelField::Assignment {
            auto& model = fieldOwner<Model>(object);
            auto copy = restoreValue<Value>(value);
            if (kind == ModelFieldKind::LOCAL_STATE) {
                if constexpr (std::is_same_v<Value, std::vector<double>>) {
                    if (copy.size() != (model.*member).size()) {
                        throw FileOperationError("snapshot state array layout does not match");
                    }
                }
            }
            return [&model, member, copy = std::move(copy)]() mutable {
                using std::swap;
                swap(model.*member, copy);
            };
        };
        return {names,
                unitType,
                description,
                kind,
                valueType,
                std::move(read),
                std::move(writer),
                std::move(restore)};
    }

    template<class Model, class Value, class Validator>
    ModelField bindParameter(std::initializer_list<std::string_view> names,
                             Value Model::* member,
                             units::unit unitType,
                             std::string_view description,
                             ModelFieldKind kind,
                             Validator validator)
    {
        static_assert(std::is_arithmetic_v<Value> || std::is_enum_v<Value> ||
                          std::is_same_v<Value, CoreTime> || std::is_same_v<Value, std::string>,
                      "automatic parameters require scalar or string storage");
        ModelField::Writer write = [member, validator](CoreObject& object,
                                                       const ModelFieldValue& input,
                                                       std::string_view name) {
            Value value;
            try {
                if constexpr (std::is_same_v<Value, bool>) {
                    value = std::get<bool>(input);
                } else if constexpr (std::is_same_v<Value, std::string>) {
                    value = std::get<std::string>(input);
                } else {
                    value = numericValue<Value>(std::get<double>(input));
                }
            }
            catch (const InvalidParameterValue&) {
                throw InvalidParameterValue(name);
            }
            if (!std::invoke(validator, std::as_const(value))) {
                throw InvalidParameterValue(name);
            }
            fieldOwner<Model>(object).*member = std::move(value);
        };
        return bindModelMember(names, member, unitType, description, kind, std::move(write));
    }
    template<bool Inverted = false, class Model, std::size_t Size>
    ModelField bindModelBit(std::initializer_list<std::string_view> names,
                            std::bitset<Size> Model::* member,
                            std::size_t bit,
                            std::string_view description,
                            ModelFieldKind kind)
    {
        if (bit >= Size) {
            throw std::invalid_argument("flag bit is outside its storage");
        }
        ModelField::Reader read = [member, bit](const CoreObject& object) -> ModelFieldValue {
            return static_cast<bool>((fieldOwner<Model>(object).*member)[bit]) != Inverted;
        };
        ModelField::Restorer restore =
            [member, bit](CoreObject& object,
                          const ModelFieldValue& value) -> ModelField::Assignment {
            auto& bits = fieldOwner<Model>(object).*member;
            const auto copy = restoreValue<bool>(value) != Inverted;
            return [&bits, bit, copy]() noexcept { bits[bit] = copy; };
        };
        ModelField::Writer write;
        if (kind == ModelFieldKind::FLAG) {
            write = [member, bit](CoreObject& object,
                                  const ModelFieldValue& value,
                                  std::string_view /*name*/) {
                (fieldOwner<Model>(object).*member)[bit] = std::get<bool>(value) != Inverted;
            };
        }
        return {names,
                units::defunit,
                description,
                kind,
                ModelFieldType::BOOLEAN,
                std::move(read),
                std::move(write),
                std::move(restore)};
    }

    template<class Model, class Accessor, class Validator>
    ModelField bindNumericParameter(std::initializer_list<std::string_view> names,
                                    Accessor access,
                                    units::unit unitType,
                                    std::string_view description,
                                    Validator validator)
    {
        using Value = std::remove_cvref_t<decltype(access(std::declval<Model&>()))>;
        static_assert((std::is_arithmetic_v<Value> && !std::is_same_v<Value, bool>) ||
                      std::is_same_v<Value, CoreTime>);
        ModelField::Reader read = [access](const CoreObject& object) -> ModelFieldValue {
            return storedValue(access(fieldOwner<Model>(object)));
        };
        ModelField::Writer write = [access, validator](CoreObject& object,
                                                       const ModelFieldValue& input,
                                                       std::string_view name) {
            Value value;
            try {
                value = numericValue<Value>(std::get<double>(input));
            }
            catch (const InvalidParameterValue&) {
                throw InvalidParameterValue(name);
            }
            if (!std::invoke(validator, std::as_const(value))) {
                throw InvalidParameterValue(name);
            }
            access(fieldOwner<Model>(object)) = value;
        };
        ModelField::Restorer restore =
            [access](CoreObject& object, const ModelFieldValue& input) -> ModelField::Assignment {
            auto& value = access(fieldOwner<Model>(object));
            const auto copy = restoreValue<Value>(input);
            return [&value, copy]() noexcept { value = copy; };
        };
        return {names,
                unitType,
                description,
                ModelFieldKind::PARAMETER,
                static_cast<ModelFieldType>(ModelFieldValue(storedValue(Value{})).index()),
                std::move(read),
                std::move(write),
                std::move(restore)};
    }

}  // namespace detail

namespace parameterValidators {
    inline constexpr auto positive = [](const auto& value) { return value > 0; };
    inline constexpr auto nonnegative = [](const auto& value) { return value >= 0; };
    inline constexpr auto nonpositive = [](const auto& value) { return value <= 0; };
    inline constexpr auto unitInterval = [](const auto& value) { return value >= 0 && value <= 1; };
    inline constexpr auto binary = [](const auto& value) { return value == 0 || value == 1; };
}  // namespace parameterValidators

/** A numeric member kept in a model's parameter structure. */
template<class Model, class Storage, class Value, class Validator = detail::AcceptModelValue>
    requires((std::is_arithmetic_v<Value> && !std::is_same_v<Value, bool>) ||
             std::is_same_v<Value, CoreTime>) &&
    std::predicate<Validator, const Value&>
ModelField parameter(std::initializer_list<std::string_view> names,
                     Storage Model::* storage,
                     Value Storage::* member,
                     units::unit unitType,
                     std::string_view description,
                     Validator validator = {})
{
    return detail::bindNumericParameter<Model>(
        names,
        [storage, member](auto& model) -> auto& { return (model.*storage).*member; },
        unitType,
        description,
        validator);
}

/** One coefficient in an array kept in a model's parameter structure. */
template<class Model,
         class Storage,
         class Value,
         std::size_t Size,
         class Validator = detail::AcceptModelValue>
    requires std::is_arithmetic_v<Value> &&
    (!std::is_same_v<Value, bool>) && std::predicate<Validator, const Value&>
ModelField parameter(std::initializer_list<std::string_view> names,
                     Storage Model::* storage,
                     std::array<Value, Size> Storage::* member,
                     std::size_t index,
                     units::unit unitType,
                     std::string_view description,
                     Validator validator = {})
{
    if (index >= Size) {
        throw std::invalid_argument("parameter index is outside its storage");
    }
    return detail::bindNumericParameter<Model>(
        names,
        [storage, member, index](auto& model) -> auto& {
            return ((model.*storage).*member)[index];
        },
        unitType,
        description,
        validator);
}

/** Numeric/string setting with ordinary conversion and optional value-only validation. */
template<class Model, class Value, class Validator = detail::AcceptModelValue>
    requires(!std::is_same_v<Value, bool>) && std::predicate<Validator, const Value&>
ModelField parameter(std::initializer_list<std::string_view> names,
                     Value Model::* member,
                     units::unit unitType,
                     std::string_view description,
                     Validator validator = {})
{
    return detail::bindParameter(
        names, member, unitType, description, ModelFieldKind::PARAMETER, validator);
}

/** One named scalar in fixed-size model parameter storage. */
template<class Model, class Value, std::size_t Size, class Validator = detail::AcceptModelValue>
    requires std::is_arithmetic_v<Value> &&
    (!std::is_same_v<Value, bool>) && std::predicate<Validator, const Value&>
ModelField parameter(std::initializer_list<std::string_view> names,
                     std::array<Value, Size> Model::* member,
                     std::size_t index,
                     units::unit unitType,
                     std::string_view description,
                     Validator validator = {})
{
    if (index >= Size) {
        throw std::invalid_argument("parameter index is outside its storage");
    }
    return detail::bindNumericParameter<Model>(
        names,
        [member, index](auto& model) -> auto& { return (model.*member)[index]; },
        unitType,
        description,
        validator);
}

/** Boolean control, without numerical units. */
template<class Model>
ModelField flag(std::initializer_list<std::string_view> names,
                bool Model::* member,
                std::string_view description)
{
    return detail::bindParameter(names,
                                 member,
                                 units::defunit,
                                 description,
                                 ModelFieldKind::FLAG,
                                 detail::AcceptModelValue{});
}

/** One named control in packed storage. Raw restore changes only the declared bit. */
template<class Model, std::size_t Size>
ModelField flag(std::initializer_list<std::string_view> names,
                std::bitset<Size> Model::* member,
                std::size_t bit,
                std::string_view description)
{
    return detail::bindModelBit(names, member, bit, description, ModelFieldKind::FLAG);
}

/** Boolean control whose logical value is the inverse of a packed storage bit. */
template<class Model, std::size_t Size>
ModelField inverseFlag(std::initializer_list<std::string_view> names,
                       std::bitset<Size> Model::* member,
                       std::size_t bit,
                       std::string_view description)
{
    return detail::bindModelBit<true>(names, member, bit, description, ModelFieldKind::FLAG);
}

/** Retained model-owned memory which cannot be reconstructed from restored inputs. */
template<class Model, class Value>
ModelField localState(std::string_view name,
                      Value Model::* member,
                      std::string_view description = {},
                      units::unit unitType = units::defunit)
{
    return detail::bindModelMember(
        {name}, member, unitType, description, ModelFieldKind::LOCAL_STATE);
}

/** One retained history bit; it is not exposed through ordinary flag setters. */
template<class Model, std::size_t Size>
ModelField localState(std::string_view name,
                      std::bitset<Size> Model::* member,
                      std::size_t bit,
                      std::string_view description)
{
    return detail::bindModelBit({name}, member, bit, description, ModelFieldKind::LOCAL_STATE);
}

}  // namespace griddyn
