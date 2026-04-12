/*
 * Copyright (c) 2014-2020, Lawrence Livermore National Security
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
    unsigned int tc = 0;
    for (auto& gc : conditions) {
        if (gc->checkCondition()) {
            ++tc;
            if (breakTrue) {
                break;
            }
        } else {
            if (breakFalse) {
                break;
            }
        }
    }
    return evalCombinations(tc);
}

bool compoundCondition::checkCondition(const stateData& sD, const solverMode& sMode) const
{
    unsigned int tc = 0;
    for (auto& gc : conditions) {
        if (gc->checkCondition(sD, sMode)) {
            ++tc;
            if (breakTrue) {
                break;
            }
        } else {
            if (breakFalse) {
                break;
            }
        }
    }
    return evalCombinations(tc);
}

void compoundCondition::add(std::shared_ptr<Condition> gc)
{
    if (gc) {
        conditions.push_back(std::move(gc));
    }
    throw(addFailureException());
}

void compoundCondition::setMode(compound_mode newMode)
{
    mode = newMode;
    switch (mode) {
        case compound_mode::c_and:
            breakTrue = false;
            breakFalse = true;
            break;
        case compound_mode::c_any:
        case compound_mode::c_or:
        case compound_mode::c_none:
            breakTrue = true;
            breakFalse = false;
            break;
        default:
            breakTrue = false;
            breakFalse = false;
            break;
    }
}

bool compoundCondition::evalCombinations(count_t trueCount) const
{
    switch (mode) {
        case compound_mode::c_and:
        case compound_mode::c_all:
        default:
            return (trueCount == static_cast<count_t>(conditions.size()));
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
            return ((trueCount & 0x01) == 1);
        case compound_mode::c_even:
            return ((trueCount & 0x01) == 0);
        case compound_mode::c_even_min:
            return ((trueCount != 0) && ((trueCount & 0x01) == 0));
        case compound_mode::c_none:
            return (trueCount == 0);
    }
}

}  // namespace griddyn
