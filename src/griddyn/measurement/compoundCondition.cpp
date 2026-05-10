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
enum class compound_mode
{
    c_and, c_or, c_any, c_xor, c_one_of, c_two_of, c_three_of
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

void compoundCondition::setMode(compound_mode newMode)
{
    mMode = newMode;
    switch (mMode) {
        case compound_mode::c_and:
            mBreakTrue = false;
            mBreakFalse = true;
            break;
        case compound_mode::c_any:
        case compound_mode::c_or:
        case compound_mode::c_none:
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
        case compound_mode::c_and:
        case compound_mode::c_all:
        default:
            return std::cmp_equal(trueCount, mConditions.size());
        case compound_mode::c_any:
        case compound_mode::c_or:
            return (trueCount > 0);
        case compound_mode::c_one_of:
            return (trueCount == 1);
        case compound_mode::c_two_of:
            return (trueCount == 2);
        case compound_mode::c_three_of:
            return (trueCount == 3);
        case compound_mode::c_two_or_more:
            return (trueCount >= 2);
        case compound_mode::c_three_or_more:
            return (trueCount >= 3);
        case compound_mode::c_xor:
        case compound_mode::c_odd:
            return ((trueCount % count_t{2}) == count_t{1});
        case compound_mode::c_even:
            return ((trueCount % count_t{2}) == count_t{0});
        case compound_mode::c_even_min:
            return ((trueCount != count_t{0}) && ((trueCount % count_t{2}) == count_t{0}));
        case compound_mode::c_none:
            return (trueCount == count_t{0});
    }
}

}  // namespace griddyn
