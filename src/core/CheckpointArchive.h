/*
 * Copyright (c) 2026, Lawrence Livermore National Security
 * See the top-level NOTICE for additional details. All rights reserved.
 * SPDX-License-Identifier: BSD-3-Clause
 */

#pragma once

#include "CoreExceptions.h"
#include "coreDefinitions.hpp"
#include <algorithm>
#include <atomic>
#include <bitset>
#include <chrono>
#include <cmath>
#include <concepts>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <limits>
#include <nlohmann/json.hpp>
#include <optional>
#include <string>
#include <string_view>
#include <type_traits>
#include <typeindex>
#include <typeinfo>
#include <utility>
#include <vector>

namespace griddyn {
template<class Value>
concept CheckpointRealArray = requires(Value& value, std::size_t size) {
    { value.data() } -> std::same_as<double*>;
    value.size();
    value.resize(size);
    value.begin();
    value.end();
};

/** Field adapter shared by model checkpoint hooks. Real runtime arrays belong to
 * the common numerical payload; typed settings and exact clocks stay readable.
 * Loading prepares assignments without changing the model. The coordinator
 * commits them only after every component and payload has been read.
 */
class CheckpointArchive {
  public:
    using Json = nlohmann::json;
    using Assignments = std::vector<std::function<void()>>;

    CheckpointArchive(Json& object,
                      std::vector<double>& values,
                      bool saving,
                      bool parameters,
                      Assignments& stateAssignments,
                      Assignments& parameterAssignments):
        mObject(object), mValues(values), mSaving(saving), mParameters(parameters),
        mStateAssignments(stateAssignments), mParameterAssignments(parameterAssignments)
    {
    }

    template<class Model>
    void model(std::string_view name)
    {
        mClass = name;
        mType = typeid(Model);
        if (mSaving) {
            mObject["type"] = name;
        }
    }

    [[nodiscard]] bool supports(const CoreObject& object) const { return mType == typeid(object); }
    [[nodiscard]] std::string_view modelName() const { return mClass; }
    [[nodiscard]] bool saving() const { return mSaving; }
    [[nodiscard]] bool includesParameters() const { return mParameters; }

    void referenceContext(std::function<std::size_t(CoreObject*)> index,
                          std::function<CoreObject*(std::size_t)> object)
    {
        mObjectIndex = std::move(index);
        mObjectAt = std::move(object);
    }

    template<class Model>
    void reference(std::string_view name, Model*& value)
    {
        auto& entry = member("runtime", name);
        if (mSaving) {
            if (value == nullptr) {
                entry = nullptr;
            } else {
                entry = mObjectIndex(value);
            }
        } else {
            auto* object = entry.is_null() ? nullptr : mObjectAt(entry.get<std::size_t>());
            auto* model = dynamic_cast<Model*>(object);
            if (object != nullptr && model == nullptr) {
                throw FileOperationError("checkpoint object reference has an invalid type");
            }
            mStateAssignments.emplace_back([&value, model]() { value = model; });
        }
    }

    template<class Model>
    void references(std::string_view name, std::vector<Model*>& value)
    {
        auto& entry = member("runtime", name);
        if (mSaving) {
            entry = Json::array();
            for (auto* object : value) {
                if (object == nullptr) {
                    entry.push_back(nullptr);
                } else {
                    entry.push_back(mObjectIndex(object));
                }
            }
        } else {
            std::vector<Model*> copy;
            for (const auto& index : entry) {
                auto* object = index.is_null() ? nullptr : mObjectAt(index.get<std::size_t>());
                auto* model = dynamic_cast<Model*>(object);
                if (object != nullptr && model == nullptr) {
                    throw FileOperationError("checkpoint object reference has an invalid type");
                }
                copy.push_back(model);
            }
            mStateAssignments.emplace_back(
                [&value, copy = std::move(copy)]() mutable { value.swap(copy); });
        }
    }

    void require(bool supported, std::string_view feature) const
    {
        if (!supported) {
            throw FileOperationError("checkpoint " + mClass + " does not support " +
                                     std::string(feature));
        }
    }

    template<class Value>
    void state(std::string_view name, Value& value)
    {
        field("runtime", name, value, true, mStateAssignments);
    }

    template<CheckpointRealArray Value>
    void fixedState(std::string_view name, Value& value)
    {
        if (!mSaving && member("runtime", name).at("count").get<std::size_t>() != value.size()) {
            throw FileOperationError("checkpoint component array layout does not match");
        }
        state(name, value);
    }

    template<std::size_t Bits>
    void flag(std::string_view name, std::bitset<Bits>& flags, std::size_t index)
    {
        auto& entry = member("runtime", name);
        if (mSaving) {
            entry = flags.test(index);
        } else {
            const bool value = entry.get<bool>();
            mStateAssignments.emplace_back([&flags, index, value]() { flags.set(index, value); });
        }
    }

    template<class Value>
    void parameter(std::string_view name, Value& value)
    {
        if (mParameters) {
            field("parameters", name, value, false, mParameterAssignments);
        }
    }

    void state(std::string_view name, std::atomic<CoreTime>& value)
    {
        auto copy = value.load();
        auto& entry = member("runtime", name);
        if (mSaving) {
            entry = copy.to_ns().count();
        } else {
            decode(entry, copy);
            mStateAssignments.emplace_back([&value, copy]() { value.store(copy); });
        }
    }

    void onRestore(std::function<void()> operation)
    {
        if (!mSaving) {
            mParameterAssignments.push_back(std::move(operation));
        }
    }

  private:
    Json& mObject;
    std::vector<double>& mValues;
    bool mSaving;
    bool mParameters;
    Assignments& mStateAssignments;
    Assignments& mParameterAssignments;
    std::string mClass;
    std::type_index mType{typeid(void)};
    std::function<std::size_t(CoreObject*)> mObjectIndex;
    std::function<CoreObject*(std::size_t)> mObjectAt;

    Json& member(std::string_view group, std::string_view name)
    {
        if (mSaving) {
            return mObject[group][mClass][name];
        }
        return mObject.at(group).at(mClass).at(name);
    }

    template<class Value>
    void field(std::string_view group,
               std::string_view name,
               Value& value,
               bool binary,
               Assignments& assignments)
    {
        auto& entry = member(group, name);
        if constexpr (std::is_same_v<Value, double> || CheckpointRealArray<Value>) {
            if (binary) {
                if (mSaving) {
                    const auto offset = mValues.size();
                    if constexpr (std::is_same_v<Value, double>) {
                        mValues.push_back(value);
                    } else {
                        mValues.insert(mValues.end(), value.begin(), value.end());
                    }
                    entry = {{"offset", offset}, {"count", mValues.size() - offset}};
                } else {
                    const auto offset = entry.at("offset").get<std::size_t>();
                    const auto count = entry.at("count").get<std::size_t>();
                    if (offset > mValues.size() || count > mValues.size() - offset) {
                        throw FileOperationError("invalid checkpoint runtime array range");
                    }
                    if constexpr (std::is_same_v<Value, double>) {
                        if (count != 1) {
                            throw FileOperationError("invalid checkpoint scalar size");
                        }
                        assignments.emplace_back(
                            [&value, copy = mValues[offset]]() { value = copy; });
                    } else {
                        Value copy;
                        copy.resize(count);
                        std::copy_n(mValues.begin() + offset, count, copy.begin());
                        assignments.emplace_back([&value, copy = std::move(copy)]() mutable {
                            using std::swap;
                            swap(value, copy);
                        });
                    }
                }
                return;
            }
        }
        if (mSaving) {
            encode(entry, value);
        } else {
            Value copy{};
            decode(entry, copy);
            assignments.emplace_back([&value, copy = std::move(copy)]() mutable {
                using std::swap;
                swap(value, copy);
            });
        }
    }

    template<class Value>
    static void encode(Json& entry, const Value& value)
    {
        if constexpr (std::is_same_v<Value, CoreTime>) {
            entry = value.to_ns().count();
        } else if constexpr (std::is_enum_v<Value>) {
            entry = static_cast<std::underlying_type_t<Value>>(value);
        } else if constexpr (std::is_floating_point_v<Value>) {
            if (std::isnan(value)) {
                entry = "nan";
            } else if (std::isinf(value)) {
                entry = value > 0 ? "+inf" : "-inf";
            } else {
                entry = value;
            }
        } else if constexpr (CheckpointRealArray<Value>) {
            entry = Json::array();
            for (const auto element : value) {
                Json encoded;
                encode(encoded, element);
                entry.push_back(std::move(encoded));
            }
        } else {
            entry = value;
        }
    }

    template<std::size_t Bits>
    static void encode(Json& entry, const std::bitset<Bits>& value)
    {
        entry = value.to_string();
    }

    template<class Value>
    static void encode(Json& entry, const std::vector<Value>& value)
    {
        entry = Json::array();
        for (const auto& element : value) {
            Json encoded;
            encode(encoded, element);
            entry.push_back(std::move(encoded));
        }
    }

    template<class Value>
    static void encode(Json& entry, const std::optional<Value>& value)
    {
        if (value) {
            encode(entry, *value);
        } else {
            entry = nullptr;
        }
    }

    template<class Value>
    static void decode(const Json& entry, Value& value)
    {
        if constexpr (std::is_same_v<Value, CoreTime>) {
            std::int64_t nanoseconds{};
            decode(entry, nanoseconds);
            value = CoreTime(std::chrono::nanoseconds(nanoseconds));
        } else if constexpr (std::is_enum_v<Value>) {
            std::underlying_type_t<Value> underlying{};
            decode(entry, underlying);
            value = static_cast<Value>(underlying);
        } else if constexpr (std::is_integral_v<Value> && !std::is_same_v<Value, bool>) {
            if (!entry.is_number_integer()) {
                throw FileOperationError("checkpoint integer field has an invalid type");
            }
            if (entry.is_number_unsigned()) {
                const auto number = entry.get<std::uint64_t>();
                if (number > static_cast<std::uint64_t>(std::numeric_limits<Value>::max())) {
                    throw FileOperationError("checkpoint integer field is out of range");
                }
                value = static_cast<Value>(number);
            } else {
                const auto number = entry.get<std::int64_t>();
                if constexpr (std::is_signed_v<Value>) {
                    if (number < static_cast<std::int64_t>(std::numeric_limits<Value>::lowest()) ||
                        number > static_cast<std::int64_t>(std::numeric_limits<Value>::max())) {
                        throw FileOperationError("checkpoint integer field is out of range");
                    }
                } else {
                    if (number < 0 ||
                        static_cast<std::uint64_t>(number) >
                            static_cast<std::uint64_t>(std::numeric_limits<Value>::max())) {
                        throw FileOperationError("checkpoint integer field is out of range");
                    }
                }
                value = static_cast<Value>(number);
            }
        } else if constexpr (std::is_floating_point_v<Value>) {
            if (entry.is_string()) {
                const auto text = entry.get<std::string>();
                if (text == "nan") {
                    value = std::numeric_limits<Value>::quiet_NaN();
                } else if (text == "+inf" || text == "-inf") {
                    value = std::numeric_limits<Value>::infinity() * (text == "+inf" ? 1 : -1);
                } else {
                    throw FileOperationError("invalid checkpoint real value");
                }
            } else {
                value = entry.get<Value>();
            }
        } else if constexpr (CheckpointRealArray<Value>) {
            if (!entry.is_array()) {
                throw FileOperationError("checkpoint vector field has an invalid type");
            }
            value.resize(entry.size());
            for (std::size_t index = 0; index < value.size(); ++index) {
                decode(entry.at(index), value[index]);
            }
        } else {
            value = entry.get<Value>();
        }
    }

    template<std::size_t Bits>
    static void decode(const Json& entry, std::bitset<Bits>& value)
    {
        const auto text = entry.get<std::string>();
        if (text.size() != Bits) {
            throw FileOperationError("invalid checkpoint flag width");
        }
        value = std::bitset<Bits>(text);
    }

    template<class Value>
    static void decode(const Json& entry, std::vector<Value>& value)
    {
        if (!entry.is_array()) {
            throw FileOperationError("checkpoint vector field has an invalid type");
        }
        value.reserve(entry.size());
        for (const auto& element : entry) {
            Value decoded{};
            decode(element, decoded);
            value.push_back(std::move(decoded));
        }
    }

    template<class Value>
    static void decode(const Json& entry, std::optional<Value>& value)
    {
        if (entry.is_null()) {
            value.reset();
        } else {
            Value copy{};
            decode(entry, copy);
            value = std::move(copy);
        }
    }
};
}  // namespace griddyn
