/*
 * Copyright (c) 2014-2026, Lawrence Livermore National Security
 * See the top-level NOTICE for additional details. All rights reserved.
 * SPDX-License-Identifier: BSD-3-Clause
 */

#include "Condition.h"
#include "grabberInterpreter.hpp"
#include <utility>

namespace griddyn {
/*
enum class CompoundMode
{
    AND, OR, ANY, XOR, ONE_OF, TWO_OF, THREE_OF
};
*/
double compoundCondition::evalCondition()
{
    return 0.0;
}
double compoundCondition::evalCondition(const stateData& /*sD*/, const solverMode& /*sMode*/)
{
    return 0.0;
}
bool compoundCondition::checkCondition() const
{
    unsigned int trueConditionCount = 0;
    for (const auto& condition : mConditions) {
        if (condition->checkCondition()) {
            ++trueConditionCount;
            if (mBreakTrue) {
                break;
            }
        } else {
            if (mBreakFalse) {
                break;
            }
        }
    }
    return evalCombinations(static_cast<count_t>(trueConditionCount));
}

bool compoundCondition::checkCondition(const stateData& stateDataValue,
                                       const solverMode& sMode) const
{
    unsigned int trueConditionCount = 0;
    for (const auto& condition : mConditions) {
        if (condition->checkCondition(stateDataValue, sMode)) {
            ++trueConditionCount;
            if (mBreakTrue) {
                break;
            }
        } else {
            if (mBreakFalse) {
                break;
            }
        }
    }
    return evalCombinations(static_cast<count_t>(trueConditionCount));
}

void compoundCondition::add(std::shared_ptr<Condition> condition)
{
    if (condition) {
        mConditions.push_back(std::move(condition));
        return;
    }
    throw(addFailureException());
}

void compoundCondition::setMode(CompoundMode newMode)
{
    mMode = newMode;
    switch (mMode) {
        case CompoundMode::AND:
            mBreakTrue = false;
            mBreakFalse = true;
            break;
        case CompoundMode::ANY:
        case CompoundMode::OR:
        case CompoundMode::NONE:
            mBreakTrue = true;
            mBreakFalse = false;
            break;
        default:
            mBreakTrue = false;
            mBreakFalse = false;
            break;
    }
}

bool compoundCondition::evalCombinations(count_t trueCount) const
{
    switch (mMode) {
        case CompoundMode::AND:
        case CompoundMode::ALL:
        default:
            return std::cmp_equal(trueCount, mConditions.size());
        case CompoundMode::ANY:
        case CompoundMode::OR:
            return (trueCount > 0);
        case CompoundMode::ONE_OF:
            return (trueCount == 1);
        case CompoundMode::TWO_OF:
            return (trueCount == 2);
        case CompoundMode::THREE_OF:
            return (trueCount == 3);
        case CompoundMode::TWO_OR_MORE:
            return (trueCount >= 2);
        case CompoundMode::THREE_OR_MORE:
            return (trueCount >= 3);
        case CompoundMode::XOR:
        case CompoundMode::ODD:
            return ((trueCount % count_t{2}) == count_t{1});
        case CompoundMode::EVEN:
            return ((trueCount % count_t{2}) == count_t{0});
        case CompoundMode::EVEN_MIN:
            return ((trueCount != count_t{0}) && ((trueCount % count_t{2}) == count_t{0}));
        case CompoundMode::NONE:
            return (trueCount == count_t{0});
    }
}

}  // namespace griddyn
