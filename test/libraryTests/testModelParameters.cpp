/*
 * Copyright (c) 2026, Lawrence Livermore National Security
 * See the top-level NOTICE for additional details. All rights reserved.
 * SPDX-License-Identifier: BSD-3-Clause
 */

#include "core/CoreExceptions.h"
#include "core/ModelParameter.hpp"
#include <algorithm>
#include <array>
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
        struct Settings {
            double delay = 0.5;
            int selector = 0;
            std::array<double, 2> coefficients{0.25, 0.75};
        };
        Settings mSettings;
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
            static const auto description = ModelDescription::define<ParameterModel>(
                "ParameterModel",
                &CoreObject::getModelDescription(),
                {parameter({"nested_delay", "nested_t"},
                           &ParameterModel::mSettings,
                           &Settings::delay,
                           units::second,
                           "Structured delay",
                           parameterValidators::positive),
                 parameter({"nested_selector"},
                           &ParameterModel::mSettings,
                           &Settings::selector,
                           units::one,
                           "Structured selector",
                           parameterValidators::binary),
                 parameter({"nested_coefficient"},
                           &ParameterModel::mSettings,
                           &Settings::coefficients,
                           1,
                           units::one,
                           "Structured coefficient",
                           parameterValidators::unitInterval),
                 parameter({"gain", "k"}, &ParameterModel::mGain, units::one, "Gain"),
                 parameter({"delay"},
                           &ParameterModel::mDelay,
                           units::second,
                           "Delay",
                           [](double value) { return value >= 0.0 && value <= 2.0; }),
                 parameter({"unchecked"},
                           &ParameterModel::mUnchecked,
                           units::one,
                           "Unbounded finite value"),
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

        void set(std::string_view fieldName,
                 double value,
                 units::unit unitType = units::defunit) override
        {
            if (fieldName == "hooked") {
                const auto converted = units::convert(value, unitType, units::one);
                if (!std::isfinite(converted)) {
                    throw InvalidParameterValue(fieldName);
                }
                setHooked(converted);
                return;
            }
            if (!ParameterModel::getModelDescription().trySetLocal(
                    *this, fieldName, value, unitType)) {
                CoreObject::set(fieldName, value, unitType);
            }
        }
        void set(std::string_view fieldName, std::string_view value) override
        {
            if (!ParameterModel::getModelDescription().trySetLocal(*this, fieldName, value)) {
                CoreObject::set(fieldName, value);
            }
        }
        [[nodiscard]] double get(std::string_view fieldName,
                                 units::unit unitType = units::defunit) const override
        {
            if (fieldName == "hooked") {
                return units::convert(mHooked, units::one, unitType);
            }
            const auto value =
                ParameterModel::getModelDescription().tryGetLocal(*this, fieldName, unitType);
            return value ? *value : CoreObject::get(fieldName, unitType);
        }
        void setFlag(std::string_view flagName, bool value) override
        {
            if (!ParameterModel::getModelDescription().trySetFlagLocal(*this, flagName, value)) {
                CoreObject::setFlag(flagName, value);
            }
        }
        [[nodiscard]] bool getFlag(std::string_view flagName) const override
        {
            const auto value =
                ParameterModel::getModelDescription().tryGetFlagLocal(*this, flagName);
            return value ? *value : CoreObject::getFlag(flagName);
        }
        [[nodiscard]] double calculated() const { return (mGain * mCount) + mSamples.back(); }
        [[nodiscard]] std::string getString(std::string_view fieldName) const override
        {
            const auto value =
                ParameterModel::getModelDescription().tryGetStringLocal(*this, fieldName);
            return value ? *value : CoreObject::getString(fieldName);
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
        void set(std::string_view fieldName,
                 double value,
                 units::unit unitType = units::defunit) override
        {
            if (!DerivedParameterModel::getModelDescription().trySetLocal(
                    *this, fieldName, value, unitType)) {
                ParameterModel::set(fieldName, value, unitType);
            }
        }
        [[nodiscard]] double get(std::string_view fieldName,
                                 units::unit unitType = units::defunit) const override
        {
            const auto value = DerivedParameterModel::getModelDescription().tryGetLocal(*this,
                                                                                        fieldName,
                                                                                        unitType);
            return value ? *value : ParameterModel::get(fieldName, unitType);
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
    for (const double invalid :
         {std::numeric_limits<double>::quiet_NaN(), std::numeric_limits<double>::infinity()}) {
        EXPECT_THROW(model.set("unchecked", invalid), InvalidParameterValue);
        EXPECT_THROW(model.set("coefficient", invalid), InvalidParameterValue);
    }
    EXPECT_DOUBLE_EQ(model.get("unchecked"), 0.0);
}

TEST(ModelParameterTests, AppliesNamedRangeValidatorsAfterUnitConversion)
{
    ParameterModel model;
    const auto description =
        ModelDescription::define<ParameterModel>("NamedValidators",
                                                 nullptr,
                                                 {parameter({"positive"},
                                                            &ParameterModel::mGain,
                                                            units::one,
                                                            "Strictly positive gain",
                                                            parameterValidators::positive),
                                                  parameter({"nonnegative"},
                                                            &ParameterModel::mDelay,
                                                            units::second,
                                                            "Nonnegative delay",
                                                            parameterValidators::nonnegative)});

    EXPECT_THROW(description.trySetLocal(model, "positive", 0.0), InvalidParameterValue);
    EXPECT_THROW(description.trySetLocal(model, "positive", -1.0), InvalidParameterValue);
    EXPECT_THROW(description.trySetLocal(model,
                                         "positive",
                                         std::numeric_limits<double>::infinity()),
                 InvalidParameterValue);
    EXPECT_DOUBLE_EQ(model.mGain, 1.0);
    EXPECT_TRUE(description.trySetLocal(model, "positive", 50.0, units::percent));
    EXPECT_NEAR(model.mGain, 0.5, 1e-7);

    EXPECT_THROW(description.trySetLocal(model, "nonnegative", -1.0, units::ms),
                 InvalidParameterValue);
    EXPECT_DOUBLE_EQ(model.mDelay, 0.5);
    EXPECT_TRUE(description.trySetLocal(model, "nonnegative", 0.0, units::ms));
    EXPECT_DOUBLE_EQ(model.mDelay, 0.0);
    EXPECT_THROW(description.trySetLocal(model,
                                         "nonnegative",
                                         std::numeric_limits<double>::infinity()),
                 InvalidParameterValue);
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

TEST(ModelParameterTests, ChecksNumericInputBeforeLookingUpAFieldExceptComments)
{
    ParameterModel model;
    const auto& description = model.getModelDescription();
    const auto nan = std::numeric_limits<double>::quiet_NaN();
    EXPECT_THROW(description.trySetLocal(model, "missing", nan), InvalidParameterValue);
    EXPECT_THROW(description.trySetLocal(model, "remembered_time", nan), InvalidParameterValue);
    EXPECT_THROW(description.trySetLocal(model, "switch", nan), InvalidParameterValue);
    EXPECT_FALSE(model.mSwitch);
    EXPECT_TRUE(description.trySetLocal(model, "", nan));
    EXPECT_TRUE(description.trySetLocal(model, "# ignored", nan));
    EXPECT_TRUE(description.trySetLocal(model, "", std::string_view{"ignored"}));
    EXPECT_TRUE(description.trySetLocal(model, "# ignored", std::string_view{"ignored"}));
    EXPECT_NO_THROW(model.set("", nan));
    EXPECT_NO_THROW(model.set("# ignored", nan));
    EXPECT_NO_THROW(model.set("", "ignored"));
    EXPECT_NO_THROW(model.set("# ignored", "ignored"));
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
    EXPECT_TRUE(model.getModelDescription().trySetLocal(model, "switch", 0.2));
    EXPECT_TRUE(model.mSwitch);
    EXPECT_DOUBLE_EQ(model.get("switch"), 1.0);
    model.set("switch", 0.0);
    EXPECT_FALSE(model.mSwitch);
    EXPECT_DOUBLE_EQ(model.get("switch"), 0.0);
    model.set("packed", -2.0);
    EXPECT_TRUE(model.getFlag("packed"));
    EXPECT_DOUBLE_EQ(model.get("packed"), 1.0);
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

TEST(ModelParameterTests, ResolvesParameterBeforeSameNamedStateRegardlessOfDeclarationOrder)
{
    auto description = ModelDescription::define<ParameterModel>(
        "SharedNameModel",
        nullptr,
        {state("gain", units::one),
         parameter({"gain"}, &ParameterModel::mGain, units::one, "Gain")});
    ParameterModel model;
    ASSERT_NE(description.find("gain", ModelFieldKind::STATE), nullptr);
    ASSERT_NE(description.findLocal("gain"), nullptr);
    EXPECT_EQ(description.findLocal("gain")->kind(), ModelFieldKind::PARAMETER);
    EXPECT_TRUE(description.trySetLocal(model, "gain", 2.5));
    const auto gain = description.tryGetLocal(model, "gain");
    ASSERT_TRUE(gain.has_value());
    EXPECT_DOUBLE_EQ(*gain, 2.5);
}

TEST(ModelParameterTests, StructuredParametersConvertValidateAndRestoreAtomically)
{
    ParameterModel model;
    model.set("nested_t", 250.0, units::ms);
    model.set("nested_coefficient", 50.0, units::percent);
    model.set("nested_selector", 1.0);
    EXPECT_NEAR(model.get("nested_delay", units::ms), 250.0, 1e-4);
    EXPECT_DOUBLE_EQ(model.mSettings.coefficients[0], 0.25);
    EXPECT_NEAR(model.mSettings.coefficients[1], 0.5, 1e-6);
    EXPECT_THROW(model.set("nested_selector", 0.5), InvalidParameterValue);
    EXPECT_THROW(model.set("nested_selector", 2.0), InvalidParameterValue);
    EXPECT_THROW(model.set("nested_coefficient", 1.1), InvalidParameterValue);
    EXPECT_THROW(model.set("nested_delay", 1.0, units::MW), InvalidParameterValue);

    const auto saved = ModelFieldSnapshot::capture(model);
    model.set("nested_delay", 1.5);
    model.set("nested_coefficient", 0.2);
    auto damaged = saved;
    entry(damaged, "ParameterModel", "nested_selector").mValue = std::string{"bad type"};
    EXPECT_THROW(damaged.restore(model), FileOperationError);
    EXPECT_DOUBLE_EQ(model.get("nested_delay"), 1.5);
    EXPECT_DOUBLE_EQ(model.get("nested_coefficient"), 0.2);
    saved.restore(model);
    EXPECT_NEAR(model.get("nested_delay"), 0.25, 1e-6);
    EXPECT_NEAR(model.get("nested_coefficient"), 0.5, 1e-6);
    EXPECT_DOUBLE_EQ(model.get("nested_selector"), 1.0);
}

TEST(ModelParameterTests, RejectsOutOfBoundsStructuredCoefficientDeclarations)
{
    EXPECT_THROW((parameter({"bad_index"},
                            &ParameterModel::mSettings,
                            &ParameterModel::Settings::coefficients,
                            2,
                            units::one,
                            "Invalid element")),
                 std::invalid_argument);
}

TEST(ModelParameterTests, DiscoversStringCommandsWithoutInterceptingTheirImplementation)
{
    auto description = ModelDescription::define<ParameterModel>(
        "StringCommandModel",
        nullptr,
        {command({"role"}, units::defunit, "Select model role", ModelFieldType::STRING)});
    ASSERT_EQ(description.stringNames().size(), 1U);
    EXPECT_EQ(description.stringNames().front(), "role");
    EXPECT_TRUE(description.numericNames().empty());
    ParameterModel model;
    EXPECT_FALSE(description.trySetLocal(model, "role", "under"));
    EXPECT_FALSE(description.trySetLocal(model, "role", 0.0));
}

TEST(ModelParameterTests, SetterResultsDistinguishAssignmentsCommandsAndIgnoredNames)
{
    static_assert(static_cast<int>(SetResult::IGNORED) == 10);
    ParameterModel model;
    const auto& description = model.getModelDescription();
    EXPECT_EQ(description.trySetLocalResult(model, "k", 250.0, units::percent),
              SetResult::PARAMETER);
    EXPECT_NEAR(model.mGain, 2.5, 1e-6);
    EXPECT_EQ(description.trySetLocalResult(model, "switch", -2.0), SetResult::FLAG);
    EXPECT_TRUE(model.mSwitch);
    EXPECT_EQ(description.trySetLocalResult(model, "label", "new label"), SetResult::PARAMETER);
    EXPECT_EQ(model.mLabel, "new label");
    EXPECT_EQ(description.trySetLocalResult(model, "missing", 1.0), SetResult::NO_MATCH);
    EXPECT_EQ(description.trySetLocalResult(model, "remembered_time", 1.0), SetResult::NO_MATCH);
    const auto nan = std::numeric_limits<double>::quiet_NaN();
    EXPECT_EQ(description.trySetLocalResult(model, "", nan), SetResult::IGNORED);
    EXPECT_EQ(description.trySetLocalResult(model, "# comment", nan), SetResult::IGNORED);
    EXPECT_EQ(description.trySetLocalResult(model, "# comment", "anything"), SetResult::IGNORED);
    EXPECT_THROW((void)description.trySetLocalResult(model, "missing", nan), InvalidParameterValue);

    const auto commands = ModelDescription::define<ParameterModel>(
        "Commands",
        nullptr,
        {command({"operation"}, units::one, "Class operation"),
         command({"role"}, units::defunit, "Role", ModelFieldType::STRING),
         command({"enabled"}, units::defunit, "Enable object", ModelFieldType::BOOLEAN)});
    EXPECT_EQ(commands.trySetLocalResult(model, "operation", 2.0), SetResult::COMMAND);
    EXPECT_EQ(commands.trySetLocalResult(model, "role", "under"), SetResult::COMMAND);
    EXPECT_FALSE(commands.trySetLocal(model, "operation", 2.0));
    EXPECT_FALSE(commands.trySetLocal(model, "role", "under"));
    EXPECT_EQ(commands.trySetLocalResult(model, "enabled", 0.0), SetResult::FLAG);
    EXPECT_FALSE(model.isEnabled());
    EXPECT_TRUE(commands.trySetLocal(model, "enabled", 1.0));
    EXPECT_TRUE(model.isEnabled());
    EXPECT_TRUE(isSetHandled(SetResult::PARAMETER));
    EXPECT_TRUE(isSetHandled(SetResult::FLAG));
    EXPECT_TRUE(isSetHandled(SetResult::IGNORED));
    EXPECT_FALSE(isSetHandled(SetResult::NO_MATCH));
    EXPECT_FALSE(isSetHandled(SetResult::COMMAND));
    EXPECT_FALSE(isSetHandled(SetResult::INDEXED_COMMAND));
}

TEST(ModelParameterTests, IndexedCommandsCheckCompleteSuffixesAndPreserveExactNames)
{
    const auto description = ModelDescription::define<ParameterModel>(
        "IndexedCommands",
        nullptr,
        {indexedCommand({"output", "route"}, units::defunit, "Output route"),
         indexedCommand({"input"}, units::defunit, "Input", ModelFieldType::STRING),
         command({"output"}, units::defunit, "All outputs", ModelFieldType::STRING),
         command({"output2"}, units::one, "Exact override"),
         parameter({"output3"}, &ParameterModel::mGain, units::one, "Exact parameter")});
    ParameterModel model;
    for (const auto* name : {"output0", "output001", "output12", "route4"}) {
        SCOPED_TRACE(name);
        const auto* field = description.findLocal(name);
        ASSERT_NE(field, nullptr);
        EXPECT_EQ(field->kind(), ModelFieldKind::INDEXED_COMMAND);
        EXPECT_TRUE(field->editable());
        EXPECT_EQ(description.find(name, ModelFieldKind::INDEXED_COMMAND), field);
        EXPECT_EQ(description.trySetLocalResult(model, name, 1.0), SetResult::INDEXED_COMMAND);
        EXPECT_FALSE(description.trySetLocal(model, name, 1.0));
    }
    const auto maximumIndex = "output" + std::to_string(std::numeric_limits<index_t>::max());
    EXPECT_EQ(description.trySetLocalResult(model, maximumIndex, 1.0), SetResult::INDEXED_COMMAND);
    EXPECT_EQ(description.trySetLocalResult(model, maximumIndex + "0", 1.0), SetResult::NO_MATCH);
    for (const auto* name : {"route", "output-1", "output+1", "output 1", "output1x", "xoutput1"}) {
        SCOPED_TRACE(name);
        EXPECT_EQ(description.findLocal(name), nullptr);
        EXPECT_EQ(description.trySetLocalResult(model, name, 1.0), SetResult::NO_MATCH);
    }
    EXPECT_EQ(description.trySetLocalResult(model, "input12", "voltage"),
              SetResult::INDEXED_COMMAND);
    EXPECT_FALSE(description.trySetLocal(model, "input12", "voltage"));
    EXPECT_EQ(description.findLocal("output")->kind(), ModelFieldKind::COMMAND);
    EXPECT_EQ(description.trySetLocalResult(model, "output", "input0"), SetResult::COMMAND);
    EXPECT_EQ(description.find("output2", ModelFieldKind::COMMAND)->name(), "output2");
    EXPECT_EQ(description.trySetLocalResult(model, "output2", 1.0), SetResult::COMMAND);
    EXPECT_EQ(description.trySetLocalResult(model, "output3", 2.0), SetResult::PARAMETER);
    EXPECT_DOUBLE_EQ(model.mGain, 2.0);
    EXPECT_THROW((void)description.trySetLocalResult(model,
                                                     "output999999999999999999999",
                                                     std::numeric_limits<double>::infinity()),
                 InvalidParameterValue);
}

TEST(ModelParameterTests, RejectsDuplicatePatternsAndRepeatedStateDeclarations)
{
    EXPECT_THROW((ModelDescription::define<ParameterModel>(
                     "DuplicatePatterns",
                     nullptr,
                     {indexedCommand({"output"}, units::one, "First"),
                      indexedCommand({"output"}, units::one, "Duplicate")})),
                 std::invalid_argument);
    EXPECT_THROW((ModelDescription::define<ParameterModel>(
                     "RepeatedState",
                     nullptr,
                     {parameter({"gain"}, &ParameterModel::mGain, units::one, "Gain"),
                      state("gain", units::one),
                      state("gain", units::one)})),
                 std::invalid_argument);
}

}  // namespace griddyn
