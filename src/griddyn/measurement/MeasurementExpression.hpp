/*
 * Copyright (c) 2014-2026, Lawrence Livermore National Security
 * See the top-level NOTICE for additional details. All rights reserved.
 * SPDX-License-Identifier: BSD-3-Clause
 */
#pragma once

/** @file
 * @brief Shared expression evaluation for object samples and solver-state samples.
 *
 * The string interpreters construct these nodes after resolving their operand
 * fields. Evaluation, scalar broadcasting, shape validation, descriptions, and
 * cloning share one implementation; only the sampling interface differs.
 */

#include "GridGrabbers.h"
#include "MeasurementMath.hpp"
#include "StateGrabber.h"
#include "gmlc/utilities/stringOps.h"
#include "utilities/MatrixDataScale.hpp"
#include <algorithm>
#include <array>
#include <memory>
#include <string>
#include <type_traits>
#include <utility>
#include <vector>

namespace griddyn {
/** @brief Adapt an expression node to one of the existing grabber interfaces.
 * @tparam Base GridGrabber for current object values, or StateGrabber for a
 *         supplied solver state.
 * @tparam Expression Derived evaluator implementing readScalar(), readVector(),
 *         describe(), and, for state access, writeJacobian().
 *
 * Uses a statically selected derived evaluator while preserving the public
 * virtual grabber API. The specializations contain interface forwarding only;
 * mathematical behavior belongs to MeasurementExpression.
 */
template<class Base, class Expression>
class MeasurementAccess;

/** @brief Forward ordinary grabber reads and labels to a shared evaluator.
 * @tparam Expression Derived evaluator used by the interface adapter.
 */
template<class Expression>
class MeasurementAccess<GridGrabber, Expression>: public GridGrabber {
  public:
    using GridGrabber::getDesc;
    /** @brief Read a scalar from current object values in the local solver mode. */
    double grabData() override
    {
        return static_cast<Expression*>(this)->readScalar(emptyStateData, cLocalSolverMode);
    }
    /** @brief Replace @p values with the current vector sample, or clear it if unavailable. */
    void grabVectorData(std::vector<double>& values) override
    {
        static_cast<Expression*>(this)->readVector(emptyStateData, cLocalSolverMode, values);
    }
    /** @brief Return the expression label maintained by the evaluator. */
    const std::string& getDesc() const override
    {
        return static_cast<const Expression*>(this)->describe();
    }
    /** @brief Return the first operand target's current time, or negTime without a target. */
    CoreTime getTime() const override
    {
        auto* object = this->getObject();
        return object != nullptr ? object->currentTime() : negTime;
    }
};

/** @brief Forward solver-state reads and Jacobian assembly to a shared evaluator.
 * @tparam Expression Derived evaluator used by the interface adapter.
 */
template<class Expression>
class MeasurementAccess<StateGrabber, Expression>: public StateGrabber {
  public:
    /** @brief Read a scalar using the supplied state and its solver layout. */
    double grabData(const StateData& data, const SolverMode& mode) override
    {
        return static_cast<Expression*>(this)->readScalar(data, mode);
    }
    /** @brief Replace the vector sample using the supplied state and solver layout. */
    void grabVectorData(const StateData& data,
                        const SolverMode& mode,
                        std::vector<double>& values) override
    {
        static_cast<Expression*>(this)->readVector(data, mode, values);
    }
    /** @brief Add a scalar expression's derivatives to row zero; ignore vector expressions. */
    void outputPartialDerivatives(const StateData& data,
                                  MatrixData<double>& matrix,
                                  const SolverMode& mode) override
    {
        if (!this->vectorGrab) {
            static_cast<Expression*>(this)->writeJacobian(data, matrix, mode, 0);
        }
    }
    /** @brief Add the selected vector element's derivatives to row zero. */
    void outputVectorPartialDerivatives(const StateData& data,
                                        MatrixData<double>& matrix,
                                        const SolverMode& mode,
                                        size_t element) override
    {
        static_cast<Expression*>(this)->writeJacobian(data, matrix, mode, element);
    }
};

/** @brief Own and evaluate one registered call over scalar or vector operands.
 * @tparam Base GridGrabber or StateGrabber, selecting the sampling interface.
 *
 * Handles zero- through three-argument scalar calls, elementwise vector calls,
 * one-vector reductions, and two-vector dot products. Scalars broadcast in any
 * argument position; vector operands must have equal lengths. Reductions return
 * a scalar, with kNullVal for empty inputs. A dot product requires two vectors.
 *
 * The parser resolves fields and folds pure constant subexpressions before
 * constructing nodes. This class also supports direct construction from owned
 * operand grabbers. Unsupported names/arity or unloaded operands leave loaded
 * false. State expressions reject random calls and expose Jacobians only when
 * every nonconstant operand supplies one. Operand units are converted by their
 * readers; the expression performs no dimensional analysis.
 *
 * State Jacobians combine local partials from MeasurementMath.hpp with operand
 * Jacobians. Scratch values and reduction weights are prepared per derivative
 * request so assembly does not reread a complete vector for each element.
 *
 * @par Reuse
 * Prefer makeGrabbers()/makeStateGrabbers() when accepting measurement strings
 * in another recorder, exporter, or controller. Direct construction is useful
 * when operands already exist as uniquely owned readers. Existing function/
 * operator wrapper APIs accept shared operands; replacing those wrappers must
 * preserve their sharing, mutation, and cloning contracts rather than simply
 * substituting this node's unique ownership.
 *
 * @note The node owns its operand grabbers, not their target objects. Targets
 *       must outlive reads and derivative requests. Scratch storage and labels
 *       are mutable: concurrent use of the same node requires external locking
 *       or independent clones. Retarget after topology changes to refresh any
 *       bound state-vector membership.
 * @see makeGrabbers, makeStateGrabbers, measurement::callPartial,
 *      measurement::reductionPartials
 */
template<class Base>
class MeasurementExpression final: public MeasurementAccess<Base, MeasurementExpression<Base>> {
    static constexpr bool STATE = std::is_same_v<Base, StateGrabber>;
    std::string mName;
    std::vector<std::unique_ptr<Base>> mArguments;
    std::vector<std::vector<double>> mValues;  //!< Operand samples reused during one evaluation.
    std::vector<double> mWeights;  //!< Reduction partials reused during Jacobian assembly.
    size_t mPreparedCount = 0;
    mutable std::string mLabel;
    function0_t mFunction0 = nullptr;
    function1_t mFunction1 = nullptr;
    function2_t mFunction2 = nullptr;
    function3_t mFunction3 = nullptr;
    vector_function1_t mReduction = nullptr;
    vector_function2_t mDot = nullptr;

    /** @brief Resolve name/arity and recompute loaded, result shape, and Jacobian mode.
     * @throws InvalidParameterValue If constant clamp bounds are invalid.
     */
    void refresh()
    {
        this->field = mName;
        mFunction0 = mArguments.empty() ? get0ArgFunction(mName) : nullptr;
        mFunction1 = mArguments.size() == 1 ? get1ArgFunction(mName) : nullptr;
        mFunction2 = mArguments.size() == 2 ? get2ArgFunction(mName) : nullptr;
        mFunction3 = mArguments.size() == 3 ? get3ArgFunction(mName) : nullptr;
        mReduction =
            mArguments.size() == 1 && mFunction1 == nullptr ? getArrayFunction(mName) : nullptr;
        mDot = mArguments.size() == 2 && mFunction2 == nullptr ? get2ArrayFunction(mName) : nullptr;
        this->loaded = mFunction0 || mFunction1 || mFunction2 || mFunction3 || mReduction || mDot;
        this->vectorGrab = false;
        for (const auto& argument : mArguments) {
            this->loaded = this->loaded && argument && argument->loaded;
            this->vectorGrab = this->vectorGrab || (argument && argument->vectorGrab);
        }
        if (mName == "clamp" && mFunction3 != nullptr && mArguments[1] && mArguments[2] &&
            mArguments[1]->field == "constant" && mArguments[2]->field == "constant" &&
            !(mArguments[1]->bias <= mArguments[2]->bias)) {
            throw InvalidParameterValue("clamp requires ordered, non-NaN bounds");
        }
        if (mDot) {
            this->loaded = this->loaded && mArguments[0]->vectorGrab && mArguments[1]->vectorGrab;
        }
        this->vectorGrab = this->vectorGrab && !mReduction && !mDot;
        mValues.resize(mArguments.size());
        if constexpr (STATE) {
            this->loaded = this->loaded && isPureFunction(mName);
            this->jacMode = JacobianMode::NONE;
            bool variable = false;
            bool derivatives = this->loaded;
            for (const auto& argument : mArguments) {
                if (argument && argument->field != "constant") {
                    variable = true;
                    derivatives = derivatives && argument->getJacobianMode() != JacobianMode::NONE;
                }
            }
            if (variable && derivatives) {
                this->jacMode = JacobianMode::COMPUTED;
            }
        }
    }

    /** @brief Sample each operand and check vector lengths before element access.
     * @return Common vector length, or one for all-scalar operands.
     * @throws InvalidParameterValue If vector operand lengths differ.
     */
    size_t prepare(const StateData& data, const SolverMode& mode)
    {
        size_t count = 1;
        bool vector = false;
        for (size_t index = 0; index < mArguments.size(); ++index) {
            auto& argument = mArguments[index];
            auto& values = mValues[index];
            if (argument->vectorGrab) {
                if constexpr (STATE) {
                    argument->grabVectorData(data, mode, values);
                } else {
                    argument->grabVectorData(values);
                }
                if (vector && count != values.size()) {
                    throw InvalidParameterValue("grabber vector lengths do not match");
                }
                count = values.size();
                vector = true;
            } else {
                if constexpr (STATE) {
                    values.assign(1, argument->grabData(data, mode));
                } else {
                    values.assign(1, argument->grabData());
                }
            }
        }
        return count;
    }

    /** @brief Read a prepared operand, broadcasting a scalar from its sole value.
     * @pre Indices are valid for the operand samples populated by prepare().
     */
    double argumentValue(size_t argument, size_t element) const
    {
        return mValues[argument][mArguments[argument]->vectorGrab ? element : 0];
    }

    /** @brief Evaluate one prepared output element, before this node's gain and bias.
     * @pre The node is loaded and prepare() has populated its operands.
     * @throws InvalidParameterValue If evaluated clamp bounds are invalid.
     */
    double evaluate(size_t element) const
    {
        if (mFunction0) {
            return mFunction0();
        }
        if (mFunction1) {
            return mFunction1(argumentValue(0, element));
        }
        if (mFunction2) {
            return mFunction2(argumentValue(0, element), argumentValue(1, element));
        }
        if (mFunction3) {
            return measurement::ternaryValue(mName,
                                             mFunction3,
                                             argumentValue(0, element),
                                             argumentValue(1, element),
                                             argumentValue(2, element));
        }
        if (mReduction) {
            return mValues[0].empty() ? kNullVal : mReduction(mValues[0]);
        }
        return mValues[0].empty() ? kNullVal : mDot(mValues[0], mValues[1]);
    }

    /** @brief Prepare samples and reduction weights throughout the expression subtree.
     *
     * Keeps this setup outside the per-element derivative loops; scratch values
     * are valid only for the current state/mode and derivative request.
     */
    // NOLINTNEXTLINE(misc-no-recursion)
    void prepareDerivatives(const StateData& data, const SolverMode& mode)
    {
        mPreparedCount = prepare(data, mode);
        if (mReduction) {
            measurement::reductionPartials(mName, mValues[0], mWeights);
        }
        for (const auto& argument : mArguments) {
            if (auto* expression = dynamic_cast<MeasurementExpression*>(argument.get())) {
                expression->prepareDerivatives(data, mode);
            }
        }
    }

    /** @brief Apply the chain rule to prepared values and add derivatives to row zero.
     * @param data State used when preparing this subtree.
     * @param matrix Destination accepting additive, possibly repeated coordinates.
     * @param mode Solver layout associated with @p data.
     * @param element Output element, or zero for a scalar/reduction.
     * @pre prepareDerivatives() has been called for this request.
     */
    // NOLINTNEXTLINE(misc-no-recursion)
    void differentiatePrepared(const StateData& data,
                               MatrixData<double>& matrix,
                               const SolverMode& mode,
                               size_t element)
    {
        if constexpr (STATE) {
            if (!this->loaded || this->jacMode == JacobianMode::NONE) {
                return;
            }
            if (mPreparedCount == 0 || element >= mPreparedCount) {
                return;
            }
            if (mReduction || mDot) {
                for (size_t argument = 0; argument < mArguments.size(); ++argument) {
                    for (size_t index = 0; index < mValues[argument].size(); ++index) {
                        const double partial =
                            mDot ? mValues[1 - argument][index] : mWeights[index];
                        if (partial != 0.0) {
                            MatrixDataScale<double> scaled(matrix, this->gain * partial);
                            emitDerivative(argument, data, scaled, mode, index);
                        }
                    }
                }
            } else {
                std::array<double, 3> values{};
                for (size_t argument = 0; argument < mArguments.size(); ++argument) {
                    values[argument] = argumentValue(argument, element);
                }
                if (mFunction3) {
                    static_cast<void>(evaluate(element));
                }  // validates clamp bounds
                for (size_t argument = 0; argument < mArguments.size(); ++argument) {
                    if (mArguments[argument]->getJacobianMode() == JacobianMode::NONE) {
                        continue;
                    }
                    const double partial = measurement::callPartial(
                        mName, std::span<const double>(values.data(), mArguments.size()), argument);
                    if (partial != 0.0) {
                        MatrixDataScale<double> scaled(matrix, this->gain * partial);
                        emitDerivative(argument,
                                       data,
                                       scaled,
                                       mode,
                                       mArguments[argument]->vectorGrab ? element : 0);
                    }
                }
            }
        }
    }

    /** @brief Add one operand's derivative through its expression or field interface. */
    // NOLINTNEXTLINE(misc-no-recursion)
    void emitDerivative(size_t argument,
                        const StateData& data,
                        MatrixData<double>& matrix,
                        const SolverMode& mode,
                        size_t element)
    {
        if constexpr (STATE) {
            if (auto* expression =
                    dynamic_cast<MeasurementExpression*>(mArguments[argument].get())) {
                expression->differentiatePrepared(data, matrix, mode, element);
            } else {
                mArguments[argument]->outputVectorPartialDerivatives(data, matrix, mode, element);
            }
        }
    }

    /** @brief Prepare and assemble one derivative request; unavailable Jacobians add nothing. */
    void differentiate(const StateData& data,
                       MatrixData<double>& matrix,
                       const SolverMode& mode,
                       size_t element)
    {
        if constexpr (STATE) {
            if (!this->loaded || this->jacMode == JacobianMode::NONE) {
                return;
            }
            prepareDerivatives(data, mode);
            differentiatePrepared(data, matrix, mode, element);
        }
    }

  public:
    using MeasurementAccess<Base, MeasurementExpression<Base>>::getDesc;
    /** @brief Construct a call, taking ownership of all operands.
     * @param arguments Operands in call order; empty for a zero-argument call.
     * @param name Registered operation name, normalized to lower case.
     * @throws InvalidParameterValue If constant clamp bounds are invalid.
     */
    MeasurementExpression(std::vector<std::unique_ptr<Base>> arguments, std::string_view name):
        mName(gmlc::utilities::convertToLowerCase(name)), mArguments(std::move(arguments))
    {
        refresh();
    }

    /** @brief Construct a unary call or one-vector reduction, taking ownership of its operand. */
    MeasurementExpression(std::unique_ptr<Base> argument, std::string_view name):
        mName(gmlc::utilities::convertToLowerCase(std::string{name}))
    {
        mArguments.push_back(std::move(argument));
        refresh();
    }

    /** @brief Construct a binary call or dot product, taking ownership of both operands. */
    MeasurementExpression(std::unique_ptr<Base> first,
                          std::unique_ptr<Base> second,
                          std::string_view name):
        mName(gmlc::utilities::convertToLowerCase(std::string{name}))
    {
        mArguments.push_back(std::move(first));
        mArguments.push_back(std::move(second));
        refresh();
    }

    /** @brief Deep-copy the operand tree and configuration, retaining target associations. */
    std::unique_ptr<Base> clone() const override
    {
        auto result =
            std::make_unique<MeasurementExpression>(std::vector<std::unique_ptr<Base>>{}, mName);
        cloneTo(result.get());
        return result;
    }

    /** @brief Copy base configuration and, for a matching expression type, replace its operands.
     * @param destination Non-null destination; self-copy is a no-op.
     * @note A destination of another grabber type receives only base configuration.
     */
    void cloneTo(Base* destination) const override
    {
        if (destination == this) {
            return;
        }
        Base::cloneTo(destination);
        if (auto* expression = dynamic_cast<MeasurementExpression*>(destination)) {
            expression->mName = mName;
            expression->mArguments.clear();
            for (const auto& argument : mArguments) {
                expression->mArguments.push_back(argument ? argument->clone() : nullptr);
            }
            expression->refresh();
        }
    }

    /** @brief Read one scalar/reduction sample and apply this node's gain and bias.
     * @param data Solver state; ignored by the object-grabber specialization.
     * @param mode Solver layout; ignored by the object-grabber specialization.
     * @return Sample, or kNullVal for an unloaded node, vector result, or empty reduction.
     * @throws InvalidParameterValue If operand shapes or evaluated clamp bounds are invalid.
     */
    double readScalar(const StateData& data, const SolverMode& mode)
    {
        if (!this->loaded || this->vectorGrab) {
            return kNullVal;
        }
        prepare(data, mode);
        const double value = evaluate(0);
        return value == kNullVal ? value : std::fma(value, this->gain, this->bias);
    }

    /** @brief Replace a vector sample, applying this node's gain and bias per element.
     * @param data Solver state; ignored by the object-grabber specialization.
     * @param mode Solver layout; ignored by the object-grabber specialization.
     * @param[out] values Cleared first; remains empty for unloaded or scalar nodes.
     * @throws InvalidParameterValue If operand shapes or evaluated clamp bounds are invalid.
     */
    void readVector(const StateData& data, const SolverMode& mode, std::vector<double>& values)
    {
        values.clear();
        if (!this->loaded || !this->vectorGrab) {
            return;
        }
        const auto count = prepare(data, mode);
        values.resize(count);
        for (size_t index = 0; index < count; ++index) {
            values[index] = std::fma(evaluate(index), this->gain, this->bias);
        }
    }

    /** @brief Add one output element's derivatives, including gain, to matrix row zero.
     * @param data State to sample when assembling the partials.
     * @param matrix Additive destination; existing entries are not cleared.
     * @param mode Solver layout associated with @p data.
     * @param element Output element, or zero for a scalar/reduction.
     * @note Object nodes and unavailable Jacobians add nothing. Sparse output may
     *       contain repeated coordinates; compact it before indexed inspection.
     * @throws InvalidParameterValue If operand shapes or evaluated clamp bounds are invalid.
     */
    void writeJacobian(const StateData& data,
                       MatrixData<double>& matrix,
                       const SolverMode& mode,
                       size_t element)
    {
        differentiate(data, matrix, mode, element);
    }

    /** @brief Retarget all operands and refresh shape, membership, and derivative availability. */
    void updateObject(CoreObject* object, ObjectUpdateMode mode = ObjectUpdateMode::DIRECT) override
    {
        for (const auto& argument : mArguments) {
            if (argument) {
                argument->updateObject(object, mode);
            }
        }
        refresh();
    }

    /** @brief Change the call name while retaining its operands, then resolve the new operation. */
    void updateField(std::string_view name) override
    {
        mName = gmlc::utilities::convertToLowerCase(std::string{name});
        refresh();
    }

    /** @brief Return the first non-null operand target, or null for target-free calls. */
    CoreObject* getObject() const override
    {
        for (const auto& argument : mArguments) {
            if (!argument) {
                continue;
            }
            if (auto* object = argument->getObject()) {
                return object;
            }
        }
        return nullptr;
    }

    /** @brief Append all operand targets without clearing or deduplicating @p objects. */
    void getObjects(std::vector<CoreObject*>& objects) const override
    {
        for (const auto& argument : mArguments) {
            if (argument) {
                argument->getObjects(objects);
            }
        }
    }

    /** @brief Rebuild the scalar expression label, preserving an object-side custom label.
     * @return Internally stored label, or an empty label for an unloaded node.
     *         A later description request may replace its contents.
     */
    const std::string& describe() const
    {
        if constexpr (!STATE) {
            if (this->customDesc) {
                return Base::getDesc();
            }
        }
        if (!this->loaded) {
            mLabel.clear();
            return mLabel;
        }
        mLabel = mName + '(';
        for (size_t index = 0; index < mArguments.size(); ++index) {
            if (index > 0) {
                mLabel += ',';
            }
            stringVec names;
            mArguments[index]->getDesc(names);
            mLabel += names.size() == 1 ? names.front() : mArguments[index]->field;
        }
        mLabel += ')';
        return mLabel;
    }

    /** @brief Replace descriptions with one label per output column.
     *
     * Scalar/reduction calls have one label. Vector calls combine corresponding
     * operand labels; object-side custom labels use numeric element suffixes.
     * Unloaded nodes return no labels.
     * @throws InvalidParameterValue If vector operand description counts differ.
     */
    void getDesc(stringVec& descriptions) const override
    {
        descriptions.clear();
        if (!this->loaded) {
            return;
        }
        if (!this->vectorGrab) {
            descriptions.push_back(describe());
            return;
        }
        std::vector<stringVec> names(mArguments.size());
        size_t count = 0;
        bool vector = false;
        for (size_t argument = 0; argument < mArguments.size(); ++argument) {
            mArguments[argument]->getDesc(names[argument]);
            if (mArguments[argument]->vectorGrab) {
                if (vector && count != names[argument].size()) {
                    throw InvalidParameterValue("grabber vector lengths do not match");
                }
                count = names[argument].size();
                vector = true;
            }
        }
        for (size_t index = 0; index < count; ++index) {
            if constexpr (!STATE) {
                if (this->customDesc) {
                    descriptions.push_back(describe() + '[' + std::to_string(index) + ']');
                    continue;
                }
            }
            std::string label = mName + '(';
            for (size_t argument = 0; argument < mArguments.size(); ++argument) {
                if (argument > 0) {
                    label += ',';
                }
                label += names[argument][mArguments[argument]->vectorGrab ? index : 0];
            }
            descriptions.push_back(label + ')');
        }
    }
};
}  // namespace griddyn
