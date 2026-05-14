/*
 * Copyright (c) 2014-2026, Lawrence Livermore National Security
 * See the top-level NOTICE for additional details. All rights reserved.
 * SPDX-License-Identifier: BSD-3-Clause
 */

#include "Condition.h"

#include "gmlc/containers/mapOps.hpp"
#include "grabberInterpreter.hpp"
#include "grabberSet.h"
#include <functional>
#include <map>
#include <memory>
#include <string>
#include <string_view>
#include <utility>

namespace griddyn {
namespace {
    bool isEqualityComparison(ComparisonType comparison)
    {
        switch (comparison) {
            case ComparisonType::GE:
            case ComparisonType::LE:
            case ComparisonType::EQ:
                return true;
            default:
                return false;
        }
    }
}  // namespace

std::unique_ptr<Condition> make_condition(std::string_view condString, coreObject* rootObject)
{
    auto cString = gmlc::utilities::stringOps::xmlCharacterCodeReplace(std::string{condString});
    // size_t posA = condString.find_first_of("&|");
    // TODO(phlpt): Handle parenthesized and &| conditions.

    const size_t pos = cString.find_first_of("><=!");
    if (pos == std::string::npos) {
        return nullptr;
    }

    const char comparisonChar = cString[pos];
    const char comparisonSuffix = cString[pos + 1];
    const std::string lhsBlock = gmlc::utilities::stringOps::trim(cString.substr(0, pos - 1));
    std::string rhsBlock =
        (comparisonSuffix == '=') ? cString.substr(pos + 2) : cString.substr(pos + 1);

    gmlc::utilities::stringOps::trimString(rhsBlock);
    auto condition = std::make_unique<Condition>();

    // get the state grabbers part

    condition->setConditionLHS(std::make_shared<grabberSet>(lhsBlock, rootObject));

    condition->setConditionRHS(std::make_shared<grabberSet>(rhsBlock, rootObject));

    std::string condstr;
    condstr.push_back(comparisonChar);
    if (comparisonSuffix == '=') {
        condstr.push_back(comparisonSuffix);
    }

    condition->setComparison(comparisonFromString(condstr));

    return condition;
}

// NOLINTNEXTLINE(bugprone-throwing-static-initialization)
static const std::map<std::string_view, ComparisonType, std::less<std::string_view>> compStrMap{
    {">", ComparisonType::GT},
    {"gt", ComparisonType::GT},
    {">=", ComparisonType::GE},
    {"ge", ComparisonType::GE},
    {"<", ComparisonType::LT},
    {"lt", ComparisonType::LT},
    {"<=", ComparisonType::LE},
    {"le", ComparisonType::LE},
    {"=", ComparisonType::EQ},
    {"eq", ComparisonType::EQ},
    {"==", ComparisonType::EQ},
    {"!=", ComparisonType::NE},
    {"ne", ComparisonType::NE},
    {"~=", ComparisonType::NE},
    {"<>", ComparisonType::NE},
    {"===", ComparisonType::EQ},
    {"??", ComparisonType::NULL_COMPARISON},
};

ComparisonType comparisonFromString(std::string_view compStr)
{
    const auto foundComparison = compStrMap.find(compStr);
    return (foundComparison != compStrMap.end()) ? foundComparison->second :
                                                   ComparisonType::NULL_COMPARISON;
}

std::string to_string(ComparisonType comp)
{
    switch (comp) {
        case ComparisonType::GT:
            return ">";
        case ComparisonType::GE:
            return ">=";
        case ComparisonType::LT:
            return "<";
        case ComparisonType::LE:
            return "<=";
        case ComparisonType::EQ:
            return "==";
        case ComparisonType::NE:
            return "!=";
        default:
            return "??";
    }
}

std::unique_ptr<Condition> make_condition(std::string_view field,
                                          std::string_view compare,
                                          double level,
                                          coreObject* rootObject)
{
    return make_condition(field, comparisonFromString(compare), level, rootObject);
    // get the state grabbers part
}

std::unique_ptr<Condition> make_condition(std::string_view field,
                                          ComparisonType comp,
                                          double level,
                                          coreObject* rootObject)
{
    try {
        auto gset = std::make_shared<grabberSet>(std::string{field}, rootObject);
        auto condition = std::make_unique<Condition>(std::move(gset));
        condition->setConditionRHS(level);

        condition->setComparison(comp);

        return condition;
    }
    catch (const std::invalid_argument& ia) {
        rootObject->log(rootObject, print_level::warning, ia.what());
        return nullptr;
    }
}

Condition::Condition(std::shared_ptr<grabberSet> valGrabber): mConditionLHS(std::move(valGrabber))
{
}
Condition::~Condition() = default;

std::unique_ptr<Condition> Condition::clone() const
{
    std::unique_ptr<Condition> ngcP = std::make_unique<Condition>();
    cloneTo(ngcP.get());
    return ngcP;
}

void Condition::cloneTo(Condition* cond) const
{
    cond->m_constant = m_constant;
    cond->m_margin = m_margin;
    cond->mConstRHS = mConstRHS;
    cond->mCurrentMargin = mCurrentMargin;
    cond->mUseMargin = mUseMargin;

    if (mConditionLHS) {
        if (cond->mConditionLHS) {
            mConditionLHS->cloneTo(cond->mConditionLHS.get());
        } else {
            cond->mConditionLHS = mConditionLHS->clone();
        }
    }

    if (!mConstRHS) {
        if (cond->mConditionRHS) {
            mConditionRHS->cloneTo(cond->mConditionRHS.get());
        } else {
            cond->mConditionRHS = mConditionRHS->clone();
        }
    }
}

void Condition::setConditionLHS(std::shared_ptr<grabberSet> valGrabber)
{
    if (valGrabber) {
        mConditionLHS = std::move(valGrabber);
    }
}

void Condition::setConditionRHS(std::shared_ptr<grabberSet> valGrabber)
{
    if (valGrabber) {
        mConditionRHS = std::move(valGrabber);
        mConstRHS = false;
    }
}

void Condition::updateObject(coreObject* obj, object_update_mode mode)
{
    // Update object may throw an error if it does everything is fine
    // if it doesn't then B update may throw an error in which case we need to rollback A for
    // exception safety this would be very unusual to occur.
    const coreObject* keyObject = nullptr;
    if (mConditionLHS) {
        keyObject = mConditionLHS->getObject();
        mConditionLHS->updateObject(obj, mode);
    }

    if (mConditionRHS) {
        try {
            mConditionRHS->updateObject(obj, mode);
        }
        catch (objectUpdateFailException& oe) {
            if ((mConditionLHS) && (keyObject != nullptr)) {
                // now rollback A
                mConditionLHS->updateObject(keyObject->getRoot(), object_update_mode::match);
            }
            throw oe;
        }
    }
}

void Condition::setComparison(std::string_view compStr)
{
    setComparison(comparisonFromString(compStr));
}
void Condition::setComparison(ComparisonType comparison)
{
    mComparison = comparison;
    switch (mComparison) {
        case ComparisonType::GT:
        case ComparisonType::GE:
            mEvalFunction = [](double leftValue, double rightValue, double margin) {
                return rightValue - leftValue - margin;
            };
            break;
        case ComparisonType::LT:
        case ComparisonType::LE:
            mEvalFunction = [](double leftValue, double rightValue, double margin) {
                return leftValue - rightValue + margin;
            };
            break;
        case ComparisonType::EQ:
            mEvalFunction = [](double leftValue, double rightValue, double margin) {
                return std::abs(leftValue - rightValue) - margin;
            };
            break;
        case ComparisonType::NE:
            mEvalFunction = [](double leftValue, double rightValue, double margin) {
                return -std::abs(leftValue - rightValue) + margin;
            };
            break;
        default:
            mEvalFunction = [](double, double, double) { return kNullVal; };
            break;
    }
}

double Condition::evalCondition()
{
    const double leftValue = mConditionLHS->grabData();
    const double rightValue = (mConstRHS) ? m_constant : mConditionRHS->grabData();

    return mEvalFunction(leftValue, rightValue, mCurrentMargin);
}

double Condition::evalCondition(const stateData& stateDataValue, const solverMode& sMode)
{
    const double leftValue = mConditionLHS->grabData(stateDataValue, sMode);
    const double rightValue =
        (mConstRHS) ? m_constant : mConditionRHS->grabData(stateDataValue, sMode);
    return mEvalFunction(leftValue, rightValue, mCurrentMargin);
}

double Condition::getVal(int side) const
{
    if (side == 2) {
        return (mConstRHS) ? m_constant : mConditionRHS->grabData();
    }
    return mConditionLHS->grabData();
}

double Condition::getVal(int side, const stateData& stateDataValue, const solverMode& sMode) const
{
    if (side == 2) {
        return (mConstRHS) ? m_constant : mConditionRHS->grabData(stateDataValue, sMode);
    }
    return mConditionLHS->grabData(stateDataValue, sMode);
}

bool Condition::checkCondition() const
{
    const double leftValue = mConditionLHS->grabData();
    const double rightValue = (mConstRHS) ? m_constant : mConditionRHS->grabData();
    const double conditionValue = mEvalFunction(leftValue, rightValue, mCurrentMargin);
    return (isEqualityComparison(mComparison)) ? (conditionValue <= 0.0) : (conditionValue < 0.0);
}

bool Condition::checkCondition(const stateData& stateDataValue, const solverMode& sMode) const
{
    const double leftValue = mConditionLHS->grabData(stateDataValue, sMode);
    const double rightValue =
        (mConstRHS) ? m_constant : mConditionRHS->grabData(stateDataValue, sMode);

    const double conditionValue = mEvalFunction(leftValue, rightValue, mCurrentMargin);
    return (isEqualityComparison(mComparison)) ? (conditionValue <= 0.0) : (conditionValue < 0.0);
}

void Condition::setMargin(double val)
{
    m_margin = val;
    if (mUseMargin) {
        mCurrentMargin = m_margin;
    }
}

coreObject* Condition::getObject() const
{
    if (mConditionLHS) {
        return mConditionLHS->getObject();
    }

    return nullptr;
}

void Condition::getObjects(std::vector<coreObject*>& objects) const
{
    if (mConditionLHS) {
        mConditionLHS->getObjects(objects);
    }

    if (!mConstRHS) {
        if (mConditionRHS) {
            mConditionRHS->getObjects(objects);
        }
    }
}

}  // namespace griddyn
