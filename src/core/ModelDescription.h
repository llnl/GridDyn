/*
 * Copyright (c) 2026, Lawrence Livermore National Security
 * See the top-level NOTICE for additional details. All rights reserved.
 * SPDX-License-Identifier: BSD-3-Clause
 */

#pragma once

#include "CoreObject.h"
#include <bitset>
#include <cstdint>
#include <functional>
#include <initializer_list>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <typeinfo>
#include <variant>
#include <vector>

namespace griddyn {
class SolverMode;

/** Meaning of a declared member, independently of its storage type. */
enum class ModelFieldKind : std::uint8_t {
    PARAMETER,
    FLAG,
    STATE,
    LOCAL_STATE,
    COMMAND,
    INDEXED_COMMAND,
    INDEXED_STATE,
    STATE_REFERENCE
};

enum class ModelFieldType : std::uint8_t {
    REAL,
    INTEGER,
    UNSIGNED_INTEGER,
    BOOLEAN,
    STRING,
    TIME,
    REAL_VECTOR,
    FLAGS
};

/** Native values used by field snapshots. Clocks retain exact nanoseconds. */
using ModelFieldValue = std::variant<double,
                                     std::int64_t,
                                     std::uint64_t,
                                     bool,
                                     std::string,
                                     CoreTime,
                                     std::vector<double>,
                                     std::bitset<64>>;

enum class FieldSnapshotLevel : std::uint8_t { STATE, PARAMETERS };

/** Outcome of local setter dispatch. Commands still require class-specific handling. */
enum class SetResult : std::uint8_t {
    NO_MATCH = 0,
    PARAMETER = 1,
    FLAG = 2,
    COMMAND = 3,
    INDEXED_COMMAND = 4,
    IGNORED = 10
};

/** True when dispatch completed the operation, including ignored comment names. */
[[nodiscard]] constexpr bool isSetHandled(SetResult result)
{
    return result == SetResult::PARAMETER || result == SetResult::FLAG ||
        result == SetResult::IGNORED;
}

/** Metadata and typed access to one existing member; no member value is stored here.
 *
 * parameter() and flag() describe controls. state() identifies owned solver variables;
 * stateReference() identifies borrowed solver outputs. indexedState() names a state family;
 * localState() binds retained model memory. command() describes operations implemented by the
 * class. An exact Boolean command dispatches to setFlag(). Normal clients continue to use
 * CoreObject::set/get. The local dispatch helpers perform direct member access for model
 * implementations. Handle special cases in the class before calling them; they do not dispatch to
 * parent tables.
 */
class ModelField {
  public:
    using Assignment = std::function<void()>;
    /** Index in the object's local state vector; kInvalidLocation means inactive. */
    using StateIndex = std::function<index_t(const CoreObject&, index_t)>;
    using StateCount = std::function<count_t(const CoreObject&)>;
    using StateName = std::function<std::string(const CoreObject&, index_t)>;
    using SolverIndex = std::function<index_t(const CoreObject&, const SolverMode&)>;
    using Reader = std::function<ModelFieldValue(const CoreObject&)>;
    using Writer = std::function<void(CoreObject&, const ModelFieldValue&, std::string_view)>;
    using Restorer = std::function<Assignment(CoreObject&, const ModelFieldValue&)>;

    struct StateBinding {
        StateIndex mLocalIndex{};
        SolverIndex mSolverIndex{};
        StateCount mCount{};
        StateName mName{};
    };

    /** Low-level constructor used by the member-binding templates. */
    ModelField(std::initializer_list<std::string_view> names,
               units::unit unitType,
               std::string_view description,
               ModelFieldKind kind,
               ModelFieldType valueType,
               Reader reader = {},
               Writer writer = {},
               Restorer restorer = {});
    ModelField(std::initializer_list<std::string_view> names,
               units::unit unitType,
               std::string_view description,
               StateBinding binding,
               ModelFieldKind kind = ModelFieldKind::STATE);

    [[nodiscard]] std::string_view name() const { return mNames.front(); }
    [[nodiscard]] std::span<const std::string> names() const { return mNames; }
    [[nodiscard]] std::string_view description() const { return mDescription; }
    [[nodiscard]] units::unit unitType() const { return mUnits; }
    [[nodiscard]] ModelFieldKind kind() const { return mKind; }
    [[nodiscard]] ModelFieldType valueType() const { return mValueType; }
    [[nodiscard]] bool hasUnits() const
    {
        return mKind != ModelFieldKind::FLAG && !units::is_default(mUnits);
    }
    [[nodiscard]] bool readable() const { return static_cast<bool>(mRead); }
    [[nodiscard]] bool editable() const
    {
        return mKind == ModelFieldKind::PARAMETER || mKind == ModelFieldKind::FLAG ||
            mKind == ModelFieldKind::COMMAND || mKind == ModelFieldKind::INDEXED_COMMAND;
    }
    [[nodiscard]] bool matches(std::string_view name) const;
    /** No value means metadata only; a bound inactive state returns kInvalidLocation. */
    [[nodiscard]] std::optional<index_t> stateIndex(const CoreObject& object,
                                                    index_t element = 0) const;
    [[nodiscard]] bool hasStateBinding() const
    {
        return mState && (mState->mLocalIndex || mState->mSolverIndex);
    }
    [[nodiscard]] bool isState() const
    {
        return mKind == ModelFieldKind::STATE || mKind == ModelFieldKind::INDEXED_STATE ||
            mKind == ModelFieldKind::STATE_REFERENCE;
    }
    [[nodiscard]] count_t stateCount(const CoreObject& object) const;
    [[nodiscard]] std::string stateName(const CoreObject& object, index_t element = 0) const;
    [[nodiscard]] std::optional<index_t> stateElement(const CoreObject& object,
                                                      std::string_view name) const;
    [[nodiscard]] std::optional<index_t> solverStateIndex(const CoreObject& object,
                                                          const SolverMode& mode) const;
    /** Read the existing member in its declared/native units. */
    [[nodiscard]] ModelFieldValue value(const CoreObject& object) const;

  private:
    friend class ModelDescription;
    friend class ModelFieldSnapshot;
    [[nodiscard]] double
        convertValue(double value, units::unit sourceUnit, units::unit targetUnit) const;
    std::vector<std::string> mNames;
    units::unit mUnits;
    std::string mDescription;
    ModelFieldKind mKind;
    ModelFieldType mValueType;
    Reader mRead;
    Writer mWrite;
    Restorer mRestore;
    std::shared_ptr<const StateBinding> mState;
};

/** Shared immutable table for one declaring class and its parent description. */
class ModelDescription {
  public:
    /** Construct a table with an exact dynamic-type check for snapshot capture. */
    template<class Model>
    static ModelDescription define(std::string_view name,
                                   const ModelDescription* parent,
                                   std::initializer_list<ModelField> fields)
    {
        return {name, parent, fields, typeid(Model)};
    }

    ModelDescription(const ModelDescription&) = delete;
    ModelDescription& operator=(const ModelDescription&) = delete;
    ModelDescription(ModelDescription&&) noexcept = default;
    ModelDescription& operator=(ModelDescription&&) noexcept = default;

    [[nodiscard]] std::string_view name() const { return mName; }
    [[nodiscard]] const ModelDescription* parent() const { return mParent; }
    [[nodiscard]] std::span<const ModelField> fields() const { return mFields; }
    /** Effective state declarations, including inherited references. Derived names replace
     * ancestors. */
    [[nodiscard]] std::span<const ModelField* const> stateFields() const { return mStates; }
    /** Bound canonical state names in local vector order. Derived declarations override
     * inherited names and occupied slots. Inactive states are omitted; unnamed slots are empty.
     */
    [[nodiscard]] stringVec solverStateNames(const CoreObject& object) const;
    /** Resolve a declared state name or alias to its local vector index, without allocating.
     * No value means no bound declaration; kInvalidLocation means a declared inactive state.
     */
    [[nodiscard]] std::optional<index_t> stateIndex(const CoreObject& object,
                                                    std::string_view name) const;
    /** Resolve once, then translate local slots through the caller's active solver layout. */
    template<class LocalMapper>
    [[nodiscard]] std::optional<index_t> solverStateIndex(const CoreObject& object,
                                                          std::string_view name,
                                                          const SolverMode& mode,
                                                          LocalMapper mapLocal) const
    {
        index_t element = 0;
        const auto* field = findState(object, name, element);
        if (field == nullptr) {
            return std::nullopt;
        }
        if (const auto index = field->solverStateIndex(object, mode)) {
            return index;
        }
        const auto local = field->stateIndex(object, element);
        return local ? std::optional<index_t>{mapLocal(*local)} : std::nullopt;
    }
    [[nodiscard]] std::span<const std::string_view> numericNames() const { return mNumericNames; }
    [[nodiscard]] std::span<const std::string_view> stringNames() const { return mStringNames; }
    [[nodiscard]] std::span<const std::string_view> flagNames() const { return mFlagNames; }
    /** Enumerate declared setter names, including aliases, in child-to-parent order.
     * Local queries replace the vector; inherited queries append. ALL appends numeric
     * names, a '#' separator, then string names. Flags are requested separately.
     */
    void getParameterStrings(stringVec& names, ParamStringType type = ParamStringType::ALL) const;
    /** Search only this class, preserving the model's existing parent dispatch. */
    [[nodiscard]] const ModelField* findLocal(std::string_view name) const;
    /** Search this class and then its described ancestors. */
    [[nodiscard]] const ModelField* find(std::string_view name) const;
    /** Resolve a name that is shared by a parameter and solver state. */
    [[nodiscard]] const ModelField* find(std::string_view name, ModelFieldKind kind) const;
    /** Default numeric setter through this declaring table, retaining parent dispatch.
     * Derived assignments and flags are applied; only this table's commands are returned
     * for the declaring class to execute. Normalized/internal writes use trySetLocal.
     */
    [[nodiscard]] SetResult trySet(CoreObject& object,
                                   std::string_view name,
                                   double value,
                                   units::unit unitType = units::defunit) const;
    /** Default string setter with the same derived-to-declaring-table boundary.
     * String parameters are assigned; only this table's string commands are returned.
     * Flags and Boolean commands remain with the class's existing string handlers.
     */
    [[nodiscard]] SetResult
        trySet(CoreObject& object, std::string_view name, std::string_view value) const;
    /** Classify local dispatch; empty/comment names are ignored before checking the value.
     * Exact numeric Boolean commands dispatch through setFlag and return FLAG.
     * Other commands return COMMAND or INDEXED_COMMAND for the class to execute.
     */
    [[nodiscard]] SetResult trySetLocal(CoreObject& object,
                                        std::string_view name,
                                        double value,
                                        units::unit unitType = units::defunit) const;
    [[nodiscard]] SetResult
        trySetLocal(CoreObject& object, std::string_view name, std::string_view value) const;
    /** Default flag access through the object's derived tables down to this declaring table.
     * Commands and parent handlers remain the responsibility of the class's public methods.
     */
    bool trySetFlag(CoreObject& object, std::string_view name, bool value) const;
    [[nodiscard]] std::optional<bool> tryGetFlag(const CoreObject& object,
                                                 std::string_view name) const;
    bool trySetFlagLocal(CoreObject& object, std::string_view name, bool value) const;
    [[nodiscard]] std::optional<bool> tryGetFlagLocal(const CoreObject& object,
                                                      std::string_view name) const;
    /** Default numeric getter for a declaring class. Search the object's derived tables
     * through this table, leaving parent-specific getters to the caller's parent dispatch.
     * A standalone table not in that hierarchy retains local lookup behavior.
     */
    [[nodiscard]] std::optional<double> tryGet(const CoreObject& object,
                                               std::string_view name,
                                               units::unit unitType = units::defunit) const;
    [[nodiscard]] std::optional<double> tryGetLocal(const CoreObject& object,
                                                    std::string_view name,
                                                    units::unit unitType = units::defunit) const;
    [[nodiscard]] std::optional<std::string> tryGetStringLocal(const CoreObject& object,
                                                               std::string_view name) const;
    [[nodiscard]] bool describesExactType(const CoreObject& object) const
    {
        return typeid(object) == *mExactType;
    }

  private:
    const ModelField*
        findState(const CoreObject& object, std::string_view name, index_t& element) const;
    /** Start a bounded lookup at the derived table, or this table for a standalone view. */
    [[nodiscard]] const ModelDescription* derivedDescription(const CoreObject& object) const;
    ModelDescription(std::string_view name,
                     const ModelDescription* parent,
                     std::initializer_list<ModelField> fields,
                     const std::type_info& exactType);
    std::string mName;
    const ModelDescription* mParent;
    std::vector<ModelField> mFields;
    std::vector<const ModelField*> mStates;
    std::vector<std::string_view> mNumericNames;
    std::vector<std::string_view> mStringNames;
    std::vector<std::string_view> mFlagNames;
    const std::type_info* mExactType;
};

/** One editable, native-unit field value, identified by declaring class and name. */
struct ModelFieldEntry {
    std::string mDeclaringClass;
    std::string mName;
    ModelFieldValue mValue;
};

/** Capture declared object fields; this is not a simulation/solver checkpoint.
 *
 * Restoration stages all types and array sizes before assigning any member. It
 * bypasses setter validators, unit conversion, notifications and physical checks.
 * Topology, external solver memory, event queues and reconstruction are outside
 * this object-field layer. Existing model structure must remain in place.
 */
class ModelFieldSnapshot {
  public:
    static ModelFieldSnapshot capture(const CoreObject& object,
                                      FieldSnapshotLevel level = FieldSnapshotLevel::PARAMETERS);
    void restore(CoreObject& object) const;
    std::string mModelType;
    FieldSnapshotLevel mLevel = FieldSnapshotLevel::PARAMETERS;
    std::vector<ModelFieldEntry> mFields;
};

/** Describe a compound/custom setter without inventing a stored member for it. */
ModelField command(std::initializer_list<std::string_view> names,
                   units::unit unitType,
                   std::string_view description,
                   ModelFieldType valueType = ModelFieldType::REAL);

/** A command family such as output0, output1, ...; exact names take precedence.
 * The suffix must contain decimal digits and fit index_t. The class checks its own bounds.
 */
ModelField indexedCommand(std::initializer_list<std::string_view> prefixes,
                          units::unit unitType,
                          std::string_view description,
                          ModelFieldType valueType = ModelFieldType::REAL);

/** Solver-managed variable; use GridComponent::findIndex with the active mode to resolve it.
 * Its values belong to the shared solver-state payload, not object-field snapshots.
 */
ModelField state(std::string_view name, units::unit unitType, std::string_view description = {});

/** Offset an optional local state index without overflowing its inactive sentinel. */
constexpr index_t stateSlot(index_t index, index_t offset)
{
    return index < kInvalidLocation && index >= 0 ? index + offset : kInvalidLocation;
}

/** Bind a fixed slot in the object's local vector (voltage, angle, algebraic, differential).
 * The binding supplies location metadata only; solver values are not object-field snapshots.
 */
ModelField state(std::initializer_list<std::string_view> names,
                 units::unit unitType,
                 index_t index,
                 std::string_view description = {});

}  // namespace griddyn
