/*
 * Copyright (c) 2026, Lawrence Livermore National Security
 * See the top-level NOTICE for additional details. All rights reserved.
 * SPDX-License-Identifier: BSD-3-Clause
 */

#include "core/CoreExceptions.h"
#include "core/ModelParameter.hpp"
#include <algorithm>
#include <bitset>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <gtest/gtest.h>
#include <limits>
#include <stdexcept>
#include <string>
#include <string_view>
#include <variant>
#include <vector>

namespace griddyn {
namespace {
    // Match the public CoreObject::set/get defaults in these test models.
    // NOLINTBEGIN(google-default-arguments)
    class ParameterModel: public CoreObject {
      public:
        enum class Mode : std::uint8_t { FIRST, SECOND };
        double mGain = 1.0;
        double mDelay = 0.5;
        double mUnchecked = 0.0;
        double mHooked = 1.0;
        int mHookCalls = 0;
        int mCount = 2;
        float mCoefficient = 1.0F;
        Mode mMode = Mode::FIRST;
        CoreTime mDeadline = timeOneSecond;
        std::uint64_t mTicket = std::numeric_limits<std::uint64_t>::max();
        bool mSwitch = false;
        std::string mLabel = "original";
        CoreTime mRememberedTime = timeZero;
        std::vector<double> mSamples{1.0, 2.0};
        std::bitset<64> mFlags;
        std::bitset<64> mOptions;

        [[nodiscard]] bool objectDependentValidator(double value) const { return value > mGain; }

        void setHooked(double value)
        {
            if (value > 10.0) {
                throw InvalidParameterValue("hooked maximum is 10");
            }
            ++mHookCalls;
            mHooked = value;
        }

        [[nodiscard]] const ModelDescription& getModelDescription() const override
        {
            static const auto finiteValue = [](double value) { return std::isfinite(value); };
            static const auto description = ModelDescription::define<ParameterModel>(
                "ParameterModel",
                &CoreObject::getModelDescription(),
                {parameter({"gain", "k"}, &ParameterModel::mGain, units::one, "Gain", finiteValue),
                 parameter({"delay"},
                           &ParameterModel::mDelay,
                           units::second,
                           "Delay",
                           [](double value) {
                               return std::isfinite(value) && value >= 0.0 && value <= 2.0;
                           }),
                 parameter(
                     {"unchecked"}, &ParameterModel::mUnchecked, units::one, "Unrestricted value"),
                 parameter(
                     {"hooked"}, &ParameterModel::mHooked, units::one, "Class-managed setter"),
                 parameter({"count"}, &ParameterModel::mCount, units::one, "Integer setting"),
                 parameter(
                     {"coefficient"}, &ParameterModel::mCoefficient, units::one, "Float setting"),
                 parameter({"mode"},
                           &ParameterModel::mMode,
                           units::one,
                           "Enumeration",
                           [](Mode value) {
                               return value == Mode::FIRST || value == Mode::SECOND;
                           }),
                 parameter(
                     {"deadline"}, &ParameterModel::mDeadline, units::second, "Clock setting"),
                 parameter(
                     {"ticket"}, &ParameterModel::mTicket, units::one, "Exact unsigned setting"),
                 flag({"switch"}, &ParameterModel::mSwitch, "Boolean setting"),
                 parameter({"label"}, &ParameterModel::mLabel, units::defunit, "Text setting"),
                 localState("remembered_time",
                            &ParameterModel::mRememberedTime,
                            "Exact clock",
                            units::second),
                 localState("samples", &ParameterModel::mSamples, "Retained sample history"),
                 localState("flags", &ParameterModel::mFlags, "Retained history bits"),
                 flag({"packed"}, &ParameterModel::mOptions, 5, "Packed Boolean option"),
                 state("solver_quantity", units::V, "Externally managed solver variable")});
            return description;
        }

        void
            set(std::string_view name, double value, units::unit unitType = units::defunit) override
        {
            if (name == "hooked") {
                const auto converted = units::convert(value, unitType, units::one);
                if (!std::isfinite(converted)) {
                    throw InvalidParameterValue(name);
                }
                setHooked(converted);
                return;
            }
            if (!ParameterModel::getModelDescription().trySetLocal(*this, name, value, unitType)) {
                CoreObject::set(name, value, unitType);
            }
        }
        void set(std::string_view name, std::string_view value) override
        {
            if (!ParameterModel::getModelDescription().trySetLocal(*this, name, value)) {
                CoreObject::set(name, value);
            }
        }
        [[nodiscard]] double get(std::string_view name,
                                 units::unit unitType = units::defunit) const override
        {
            if (name == "hooked") {
                return units::convert(mHooked, units::one, unitType);
            }
            const auto value =
                ParameterModel::getModelDescription().tryGetLocal(*this, name, unitType);
            return value ? *value : CoreObject::get(name, unitType);
        }
        void setFlag(std::string_view name, bool value) override
        {
            if (!ParameterModel::getModelDescription().trySetFlagLocal(*this, name, value)) {
                CoreObject::setFlag(name, value);
            }
        }
        [[nodiscard]] bool getFlag(std::string_view name) const override
        {
            const auto value = ParameterModel::getModelDescription().tryGetFlagLocal(*this, name);
            return value ? *value : CoreObject::getFlag(name);
        }
        [[nodiscard]] double calculated() const { return (mGain * mCount) + mSamples.back(); }
        [[nodiscard]] std::string getString(std::string_view name) const override
        {
            const auto value = ParameterModel::getModelDescription().tryGetStringLocal(*this, name);
            return value ? *value : CoreObject::getString(name);
        }
    };

    class DerivedParameterModel: public ParameterModel {
      public:
        using ParameterModel::set;
        double mOwnGain = 5.0;
        [[nodiscard]] const ModelDescription& getModelDescription() const override
        {
            static const auto description = ModelDescription::define<DerivedParameterModel>(
                "DerivedParameterModel",
                &ParameterModel::getModelDescription(),
                {parameter({"gain"}, &DerivedParameterModel::mOwnGain, units::one, "Child gain")});
            return description;
        }
        void
            set(std::string_view name, double value, units::unit unitType = units::defunit) override
        {
            if (!DerivedParameterModel::getModelDescription().trySetLocal(
                    *this, name, value, unitType)) {
                ParameterModel::set(name, value, unitType);
            }
        }
        [[nodiscard]] double get(std::string_view name,
                                 units::unit unitType = units::defunit) const override
        {
            const auto value =
                DerivedParameterModel::getModelDescription().tryGetLocal(*this, name, unitType);
            return value ? *value : ParameterModel::get(name, unitType);
        }
    };

    class UndescribedParameterModel: public ParameterModel {};
    // NOLINTEND(google-default-arguments)

    template<class Model>
    concept AcceptsObjectValidator = requires {
        parameter({"gain"},
                  &Model::mGain,
                  units::one,
                  "Invalid validator",
                  &Model::objectDependentValidator);
    };
    static_assert(!AcceptsObjectValidator<ParameterModel>);

    ModelFieldEntry&
        entry(ModelFieldSnapshot& snapshot, std::string_view declaringClass, std::string_view name)
    {
        const auto field = std::find_if(snapshot.mFields.begin(),
                                        snapshot.mFields.end(),
                                        [declaringClass, name](const auto& candidate) {
                                            return candidate.mDeclaringClass == declaringClass &&
                                                candidate.mName == name;
                                        });
        if (field == snapshot.mFields.end()) {
            throw std::runtime_error("missing test snapshot field");
        }
        return *field;
    }
}  // namespace

TEST(ModelParameterTests, SharesMetadataWithoutSharingMemberValues)
{
    ParameterModel first;
    ParameterModel second;
    const auto& description = first.getModelDescription();
    EXPECT_EQ(&description, &second.getModelDescription());
    const auto* gain = description.find("gain");
    ASSERT_NE(gain, nullptr);
    EXPECT_EQ(gain, description.find("k"));
    EXPECT_EQ(gain->description(), "Gain");
    EXPECT_EQ(gain->valueType(), ModelFieldType::REAL);
    EXPECT_EQ(gain->unitType(), units::one);
    first.set("gain", 3.0);
    EXPECT_DOUBLE_EQ(std::get<double>(gain->value(first)), 3.0);
    EXPECT_DOUBLE_EQ(std::get<double>(gain->value(second)), 1.0);
}

TEST(ModelParameterTests, ConvertsBeforeValidationAndConvertsGetResults)
{
    ParameterModel model;
    model.set("delay", 1500.0, units::ms);
    EXPECT_NEAR(model.get("delay"), 1.5, 1e-6);
    EXPECT_DOUBLE_EQ(model.get("delay", units::ms), 1500.0);
    model.set("k", 250.0, units::percent);
    EXPECT_NEAR(model.get("gain"), 2.5, 1e-6);
    EXPECT_DOUBLE_EQ(model.get("k", units::percent), 250.0);
    EXPECT_THROW(model.set("delay", 3000.0, units::ms), InvalidParameterValue);
    EXPECT_NEAR(model.get("delay"), 1.5, 1e-6);
}

TEST(ModelParameterTests, DistinguishesControlsSolverStateAndRetainedLocalMemory)
{
    ParameterModel model;
    const auto& description = model.getModelDescription();
    EXPECT_EQ(description.find("gain")->kind(), ModelFieldKind::PARAMETER);
    EXPECT_EQ(description.find("switch")->kind(), ModelFieldKind::FLAG);
    EXPECT_EQ(description.find("remembered_time")->kind(), ModelFieldKind::LOCAL_STATE);
    const auto* solverState = description.find("solver_quantity");
    ASSERT_NE(solverState, nullptr);
    EXPECT_EQ(solverState->kind(), ModelFieldKind::STATE);
    EXPECT_FALSE(solverState->editable());
    EXPECT_FALSE(solverState->readable());
    EXPECT_THROW((void)solverState->value(model), UnrecognizedParameter);
    EXPECT_FALSE(description.find("switch")->hasUnits());
    EXPECT_TRUE(description.find("gain")->hasUnits());
    EXPECT_EQ(std::find(description.numericNames().begin(),
                        description.numericNames().end(),
                        "switch"),
              description.numericNames().end());
    EXPECT_NE(std::find(description.flagNames().begin(), description.flagNames().end(), "switch"),
              description.flagNames().end());
    EXPECT_FALSE(description.trySetFlagLocal(model, "gain", true));
    EXPECT_FALSE(description.tryGetFlagLocal(model, "gain"));
    EXPECT_FALSE(description.trySetLocal(model, "remembered_time", 2.0));

    const auto saved = ModelFieldSnapshot::capture(model);
    EXPECT_EQ(std::find_if(saved.mFields.begin(),
                           saved.mFields.end(),
                           [](const auto& item) { return item.mName == "solver_quantity"; }),
              saved.mFields.end());
}

TEST(ModelParameterTests, CapturesOnlyTheDeclaredPackedFlagAndPreservesUnrelatedBits)
{
    ParameterModel model;
    model.setFlag("packed", true);
    EXPECT_TRUE(model.getFlag("packed"));
    const auto saved = ModelFieldSnapshot::capture(model, FieldSnapshotLevel::STATE);
    model.setFlag("packed", false);
    model.mOptions.set(63);
    saved.restore(model);
    EXPECT_TRUE(model.getFlag("packed"));
    EXPECT_TRUE(model.mOptions[63]);
    EXPECT_THROW(flag({"invalid"}, &ParameterModel::mOptions, 64, "Invalid bit"),
                 std::invalid_argument);
}

TEST(ModelParameterTests, RecomputesDerivedValuesFromRestoredParametersFlagsAndHistory)
{
    ParameterModel model;
    model.setFlag("switch", true);
    const auto original = model.calculated();
    const auto saved = ModelFieldSnapshot::capture(model);
    model.set("gain", 4.0);
    model.set("count", 5.0);
    model.mSamples = {8.0, 9.0};
    model.setFlag("switch", false);
    EXPECT_NE(model.calculated(), original);
    EXPECT_EQ(model.getModelDescription().find("calculated"), nullptr);
    saved.restore(model);
    EXPECT_DOUBLE_EQ(model.calculated(), original);
    EXPECT_TRUE(model.getFlag("switch"));
}

TEST(ModelParameterTests, DescribesCoreStatusAsFlagsAndSchedulingAsParametersOrLocalMemory)
{
    CoreObject object;
    const auto& description = object.getModelDescription();
    EXPECT_EQ(description.find("enabled")->kind(), ModelFieldKind::FLAG);
    EXPECT_TRUE(description.find("enabled")->editable());
    EXPECT_EQ(description.find("period")->kind(), ModelFieldKind::PARAMETER);
    EXPECT_EQ(description.find("prevTime")->kind(), ModelFieldKind::LOCAL_STATE);
    EXPECT_EQ(description.find("nextUpdateTime")->kind(), ModelFieldKind::LOCAL_STATE);
    EXPECT_FALSE(description.find("nextUpdateTime")->editable());
    EXPECT_EQ(description.find("extra_bool"), nullptr);
    EXPECT_EQ(description.find("extra_bool2"), nullptr);
    object.set("updatedelay", 250.0, units::ms);
    EXPECT_NEAR(object.get("updatedelay"), 0.25, 1e-6);
    EXPECT_EQ(description.find("updatedelay")->kind(), ModelFieldKind::PARAMETER);
    EXPECT_THROW(object.set("updatedelay", 1.0, units::MW), InvalidParameterValue);
}

TEST(ModelParameterTests, RejectsBadValuesAndUnitsBeforeAssignmentOrCustomSetter)
{
    ParameterModel model;
    for (const double invalid :
         {std::numeric_limits<double>::quiet_NaN(), std::numeric_limits<double>::infinity()}) {
        EXPECT_THROW(model.set("gain", invalid), InvalidParameterValue);
        EXPECT_THROW(model.set("hooked", invalid), InvalidParameterValue);
    }
    EXPECT_EQ(model.mHookCalls, 0);
    EXPECT_DOUBLE_EQ(model.get("gain"), 1.0);
    EXPECT_THROW(model.set("gain", 2.0, units::second), InvalidParameterValue);
    EXPECT_THROW((void)model.get("gain", units::MW), InvalidParameterValue);
    EXPECT_DOUBLE_EQ(model.get("gain"), 1.0);
    model.set("hooked", 200.0, units::percent);
    EXPECT_EQ(model.mHookCalls, 1);
    EXPECT_NEAR(model.mHooked, 2.0, 1e-6);
    model.set("unchecked", std::numeric_limits<double>::quiet_NaN());
    EXPECT_TRUE(std::isnan(model.get("unchecked")));
}

TEST(ModelParameterTests, PreservesBaseFallbackAndDifferentUnknownValueErrors)
{
    ParameterModel model;
    model.set("name", "renamed");
    EXPECT_EQ(model.getString("name"), "renamed");
    model.set("period", 250.0, units::ms);
    EXPECT_NEAR(model.get("period"), 0.25, 1e-6);
    EXPECT_THROW(model.set("missing", 1.0), UnrecognizedParameter);
    EXPECT_THROW(model.set("gain", "wrong type"), InvalidParameterValue);
    EXPECT_THROW(model.set("label", 1.0), InvalidParameterValue);
    EXPECT_EQ(model.get("missing"), kNullVal);
    EXPECT_FALSE(model.getModelDescription().trySetLocal(model, "missing", 2.0));
    EXPECT_FALSE(model.getModelDescription().trySetLocal(model, "remembered_time", 2.0));
    EXPECT_EQ(model.mRememberedTime, timeZero);
}

TEST(ModelParameterTests, HandlesIntegerBooleanAndStringStorageWithoutNarrowing)
{
    ParameterModel model;
    model.set("count", 12.0);
    EXPECT_EQ(model.mCount, 12);
    for (const double invalid :
         {12.5, std::ldexp(1.0, 31), std::numeric_limits<double>::infinity()}) {
        EXPECT_THROW(model.set("count", invalid), InvalidParameterValue);
    }
    EXPECT_EQ(model.mCount, 12);
    EXPECT_THROW(model.set("ticket", std::ldexp(1.0, 64)), InvalidParameterValue);
    EXPECT_THROW(model.set("ticket", -1.0), InvalidParameterValue);
    EXPECT_EQ(model.mTicket, std::numeric_limits<std::uint64_t>::max());
    model.setFlag("switch", true);
    EXPECT_TRUE(model.mSwitch);
    EXPECT_TRUE(model.getFlag("switch"));
    EXPECT_FALSE(model.getModelDescription().trySetLocal(model, "switch", 0.2));
    model.set("label", "updated");
    EXPECT_EQ(model.getString("label"), "updated");
    model.set("mode", 1.0);
    EXPECT_EQ(model.mMode, ParameterModel::Mode::SECOND);
    EXPECT_THROW(model.set("mode", 2.0), InvalidParameterValue);
    EXPECT_THROW(model.set("mode", 256.0), InvalidParameterValue);
    EXPECT_THROW(model.set("coefficient", 1e100), InvalidParameterValue);
    EXPECT_EQ(model.mCoefficient, 1.0F);
    model.set("deadline", 2.0);
    EXPECT_EQ(model.mDeadline, CoreTime(2.0));
    EXPECT_THROW(model.set("deadline", std::numeric_limits<double>::infinity()),
                 InvalidParameterValue);
}

TEST(ModelParameterTests, PreservesCustomSetterErrorDetails)
{
    ParameterModel model;
    try {
        model.set("hooked", 11.0);
        FAIL() << "custom setter should reject the value";
    }
    catch (const InvalidParameterValue& error) {
        EXPECT_NE(std::string(error.what()).find("maximum is 10"), std::string::npos);
    }
    EXPECT_DOUBLE_EQ(model.mHooked, 1.0);
    EXPECT_EQ(model.mHookCalls, 0);
}

TEST(ModelParameterTests, RunsSpecialClassHandlingBeforeDefaultMemberDispatch)
{
    ParameterModel model;
    const auto& description = model.getModelDescription();
    const auto* hooked = description.find("hooked");
    ASSERT_NE(hooked, nullptr);
    EXPECT_TRUE(hooked->editable());
    EXPECT_TRUE(hooked->readable());
    EXPECT_EQ(model.mHookCalls, 0);
    EXPECT_DOUBLE_EQ(model.mHooked, 1.0);

    model.set("hooked", 200.0, units::percent);
    EXPECT_EQ(model.mHookCalls, 1);
    EXPECT_NEAR(model.get("hooked"), 2.0, 1e-6);
    EXPECT_DOUBLE_EQ(std::get<double>(hooked->value(model)), model.mHooked);
}

TEST(ModelParameterTests, ResolvesChildOverridesAndParentAliasesThroughExistingSetGet)
{
    DerivedParameterModel model;
    model.set("gain", 4.0);
    model.set("k", 3.0);
    model.set("delay", 0.75);
    EXPECT_DOUBLE_EQ(model.get("gain"), 4.0);
    EXPECT_DOUBLE_EQ(model.get("k"), 3.0);
    EXPECT_DOUBLE_EQ(model.get("delay"), 0.75);
    EXPECT_EQ(model.getModelDescription().find("gain"),
              model.getModelDescription().findLocal("gain"));
    EXPECT_EQ(model.getModelDescription().find("k"),
              model.getModelDescription().parent()->findLocal("k"));
    const auto saved = ModelFieldSnapshot::capture(model);
    model.set("gain", 8.0);
    model.set("k", 9.0);
    saved.restore(model);
    EXPECT_DOUBLE_EQ(model.get("gain"), 4.0);
    EXPECT_DOUBLE_EQ(model.get("k"), 3.0);
}

TEST(ModelParameterTests, SnapshotRestoresParametersTogetherWithExactRuntimeState)
{
    ParameterModel model;
    constexpr std::int64_t exactTime = 9007199254740993LL;
    model.mRememberedTime = CoreTime(std::chrono::nanoseconds(exactTime));
    model.mFlags.set(63);
    model.set("gain", 2.0);
    const auto saved = ModelFieldSnapshot::capture(model);
    model.set("gain", 5.0);
    model.mTicket = 7;
    model.mRememberedTime = timeOneSecond;
    model.mSamples = {9.0, 8.0};
    model.mFlags.reset();
    model.set("label", "changed");
    model.disable();
    saved.restore(model);
    EXPECT_DOUBLE_EQ(model.get("gain"), 2.0);
    EXPECT_EQ(model.mTicket, std::numeric_limits<std::uint64_t>::max());
    EXPECT_EQ(model.mRememberedTime.to_ns().count(), exactTime);
    EXPECT_EQ(model.mSamples, (std::vector<double>{1.0, 2.0}));
    EXPECT_TRUE(model.mFlags.test(63));
    EXPECT_TRUE(model.isEnabled());
    EXPECT_EQ(model.getString("label"), "original");
    saved.restore(model);
    EXPECT_DOUBLE_EQ(model.get("gain"), 2.0);
}

TEST(ModelParameterTests, StateOnlyRestoreRetainsCurrentConfiguration)
{
    ParameterModel model;
    model.mRememberedTime = 2.0;
    const auto saved = ModelFieldSnapshot::capture(model, FieldSnapshotLevel::STATE);
    model.set("gain", 4.0);
    model.set("label", "keep");
    model.mRememberedTime = 5.0;
    model.mSamples = {6.0, 7.0};
    saved.restore(model);
    EXPECT_DOUBLE_EQ(model.get("gain"), 4.0);
    EXPECT_EQ(model.getString("label"), "keep");
    EXPECT_EQ(model.mRememberedTime, CoreTime(2.0));
    EXPECT_EQ(model.mSamples, (std::vector<double>{1.0, 2.0}));
}

TEST(ModelParameterTests, RestoreBypassesValidatorsAndOrdinarySetterSideEffects)
{
    ParameterModel model;
    auto saved = ModelFieldSnapshot::capture(model);
    entry(saved, "ParameterModel", "gain").mValue = std::numeric_limits<double>::quiet_NaN();
    entry(saved, "ParameterModel", "hooked").mValue = std::numeric_limits<double>::infinity();
    model.set("hooked", 3.0);
    saved.restore(model);
    EXPECT_TRUE(std::isnan(model.get("gain")));
    EXPECT_TRUE(std::isinf(model.mHooked));
    EXPECT_EQ(model.mHookCalls, 1);
}

TEST(ModelParameterTests, RejectsMalformedSnapshotsBeforeAnyMemberChanges)
{
    ParameterModel model;
    auto saved = ModelFieldSnapshot::capture(model);
    saved.mFields.back().mValue = std::string("wrong type");
    model.set("gain", 8.0);
    model.mRememberedTime = 3.0;
    EXPECT_THROW(saved.restore(model), FileOperationError);
    EXPECT_DOUBLE_EQ(model.get("gain"), 8.0);
    EXPECT_EQ(model.mRememberedTime, CoreTime(3.0));

    saved = ModelFieldSnapshot::capture(model);
    entry(saved, "ParameterModel", "count").mValue = std::numeric_limits<std::int64_t>::max();
    model.set("gain", 9.0);
    EXPECT_THROW(saved.restore(model), FileOperationError);
    EXPECT_DOUBLE_EQ(model.get("gain"), 9.0);
    EXPECT_EQ(model.mCount, 2);

    saved = ModelFieldSnapshot::capture(model);
    entry(saved, "ParameterModel", "coefficient").mValue = 1e100;
    model.set("gain", 10.0);
    EXPECT_THROW(saved.restore(model), FileOperationError);
    EXPECT_DOUBLE_EQ(model.get("gain"), 10.0);

    saved = ModelFieldSnapshot::capture(model);
    saved.mFields.back().mName = "unknown";
    EXPECT_THROW(saved.restore(model), FileOperationError);
    saved.mFields.pop_back();
    EXPECT_THROW(saved.restore(model), FileOperationError);
}

TEST(ModelParameterTests, RejectsChangedArrayLayoutsAndUnsupportedModelTypes)
{
    ParameterModel model;
    const auto saved = ModelFieldSnapshot::capture(model);
    model.mSamples.resize(3);
    model.set("gain", 8.0);
    EXPECT_THROW(saved.restore(model), FileOperationError);
    EXPECT_DOUBLE_EQ(model.get("gain"), 8.0);
    DerivedParameterModel other;
    EXPECT_THROW(saved.restore(other), FileOperationError);
    UndescribedParameterModel undescribed;
    EXPECT_THROW(ModelFieldSnapshot::capture(undescribed), FileOperationError);
    CoreObject wrongOwner;
    EXPECT_THROW((void)model.getModelDescription().find("gain")->value(wrongOwner),
                 InvalidParameterValue);
}

TEST(ModelParameterTests, RejectsDuplicateAliasesButOwnsDescriptionStrings)
{
    std::string fieldName = "temporary";
    std::string fieldDescription = "Owned description";
    const auto description = ModelDescription::define<ParameterModel>(
        "TemporaryModel",
        nullptr,
        {parameter({fieldName}, &ParameterModel::mGain, units::one, fieldDescription)});
    fieldName.assign(200, 'x');
    fieldDescription.assign(200, 'y');
    ASSERT_NE(description.find("temporary"), nullptr);
    EXPECT_EQ(description.find("temporary")->description(), "Owned description");
    EXPECT_THROW((ModelDescription::define<ParameterModel>(
                     "Invalid",
                     nullptr,
                     {parameter({"gain", "k"}, &ParameterModel::mGain, units::one, "First"),
                      parameter({"k"}, &ParameterModel::mDelay, units::second, "Duplicate")})),
                 std::invalid_argument);
}

}  // namespace griddyn
