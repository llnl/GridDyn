/*
 * Copyright (c) 2014-2026, Lawrence Livermore National Security
 * See the top-level NOTICE for additional details. All rights reserved.
 * SPDX-License-Identifier: BSD-3-Clause
 */

#include "Condition.h"

#include "gmlc/containers/mapOps.hpp"
#include "grabberInterpreter.hpp"
#include "grabberSet.h"
#include <memory>
#include <string>
#include <map>
#include <string_view>
#include <utility>

namespace griddyn {
namespace {
    static bool isEqualityComparison(comparison_type comparison)
    {
        switch (comparison) {
            case comparison_type::ge:
            case comparison_type::le:
            case comparison_type::eq:
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
static const std::map<std::string_view, comparison_type, std::less<std::string_view>> compStrMap{
    {">", comparison_type::gt},
    {"gt", comparison_type::gt},
    {">=", comparison_type::ge},
    {"ge", comparison_type::ge},
    {"<", comparison_type::lt},
    {"lt", comparison_type::lt},
    {"<=", comparison_type::le},
    {"le", comparison_type::le},
    {"=", comparison_type::eq},
    {"eq", comparison_type::eq},
    {"==", comparison_type::eq},
    {"!=", comparison_type::ne},
    {"ne", comparison_type::ne},
    {"~=", comparison_type::ne},
    {"<>", comparison_type::ne},
    {"===", comparison_type::eq},
    {"??", comparison_type::null},
};

comparison_type comparisonFromString(std::string_view compStr)
{
    const auto foundComparison = compStrMap.find(compStr);
    return (foundComparison != compStrMap.end()) ? foundComparison->second : comparison_type::null;
}

std::string to_string(comparison_type comp)
{
    switch (comp) {
        case comparison_type::gt:
            return ">";
        case comparison_type::ge:
            return ">=";
        case comparison_type::lt:
            return "<";
        case comparison_type::le:
            return "<=";
        case comparison_type::eq:
            return "==";
        case comparison_type::ne:
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
                                          comparison_type comp,
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

Condition::Condition(std::shared_ptr<grabberSet> valGrabber): conditionLHS(std::move(valGrabber)) {}
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
    cond->m_constRHS = m_constRHS;
    cond->m_curr_margin = m_curr_margin;
    cond->use_margin = use_margin;

    if (conditionLHS) {
        if (cond->conditionLHS) {
            conditionLHS->cloneTo(cond->conditionLHS.get());
        } else {
            cond->conditionLHS = conditionLHS->clone();
        }
    }

    if (!m_constRHS) {
        if (cond->conditionRHS) {
            conditionRHS->cloneTo(cond->conditionRHS.get());
        } else {
            cond->conditionRHS = conditionRHS->clone();
        }
    }
}

void Condition::setConditionLHS(std::shared_ptr<grabberSet> valGrabber)
{
    if (valGrabber) {
        conditionLHS = std::move(valGrabber);
    }
}

void Condition::setConditionRHS(std::shared_ptr<grabberSet> valGrabber)
{
    if (valGrabber) {
        conditionRHS = std::move(valGrabber);
        m_constRHS = false;
    }
}

void Condition::updateObject(coreObject* obj, object_update_mode mode)
{
    // Update object may throw an error if it does everything is fine
    // if it doesn't then B update may throw an error in which case we need to rollback A for
    // exception safety this would be very unusual to occur.
    const coreObject* keyObject = nullptr;
    if (conditionLHS) {
        keyObject = conditionLHS->getObject();
        conditionLHS->updateObject(obj, mode);
    }

    if (conditionRHS) {
        try {
            conditionRHS->updateObject(obj, mode);
        }
        catch (objectUpdateFailException& oe) {
            if ((conditionLHS) && (keyObject != nullptr)) {
                // now rollback A
                conditionLHS->updateObject(keyObject->getRoot(), object_update_mode::match);
            }
            throw oe;
        }
    }
}

void Condition::setComparison(std::string_view compStr)
{
    setComparison(comparisonFromString(compStr));
}
void Condition::setComparison(comparison_type comparison)
{
    comp = comparison;
    switch (comp) {
        case comparison_type::gt:
        case comparison_type::ge:
            evalf = [](double leftValue, double rightValue, double margin) {
                return rightValue - leftValue - margin;
            };
            break;
        case comparison_type::lt:
        case comparison_type::le:
            evalf = [](double leftValue, double rightValue, double margin) {
                return leftValue - rightValue + margin;
            };
            break;
        case comparison_type::eq:
            evalf = [](double leftValue, double rightValue, double margin) {
                return std::abs(leftValue - rightValue) - margin;
            };
            break;
        case comparison_type::ne:
            evalf = [](double leftValue, double rightValue, double margin) {
                return -std::abs(leftValue - rightValue) + margin;
            };
            break;
        default:
            evalf = [](double, double, double) { return kNullVal; };
            break;
    }
}

double Condition::evalCondition()
{
    const double leftValue = conditionLHS->grabData();
    const double rightValue = (m_constRHS) ? m_constant : conditionRHS->grabData();

    return evalf(leftValue, rightValue, m_curr_margin);
}

double Condition::evalCondition(const stateData& stateDataValue, const solverMode& sMode)
{
    const double leftValue = conditionLHS->grabData(stateDataValue, sMode);
    const double rightValue =
        (m_constRHS) ? m_constant : conditionRHS->grabData(stateDataValue, sMode);
    return evalf(leftValue, rightValue, m_curr_margin);
}

double Condition::getVal(int side) const
{
    if (side == 2) {
        return (m_constRHS) ? m_constant : conditionRHS->grabData();
    }
    return conditionLHS->grabData();
}

double Condition::getVal(int side, const stateData& stateDataValue, const solverMode& sMode) const
{
    if (side == 2) {
        return (m_constRHS) ? m_constant : conditionRHS->grabData(stateDataValue, sMode);
    }
    return conditionLHS->grabData(stateDataValue, sMode);
}

bool Condition::checkCondition() const
{
    const double leftValue = conditionLHS->grabData();
    const double rightValue = (m_constRHS) ? m_constant : conditionRHS->grabData();
    const double conditionValue = evalf(leftValue, rightValue, m_curr_margin);
    return (isEqualityComparison(comp)) ? (conditionValue <= 0.0) : (conditionValue < 0.0);
}

bool Condition::checkCondition(const stateData& stateDataValue, const solverMode& sMode) const
{
    const double leftValue = conditionLHS->grabData(stateDataValue, sMode);
    const double rightValue =
        (m_constRHS) ? m_constant : conditionRHS->grabData(stateDataValue, sMode);

    const double conditionValue = evalf(leftValue, rightValue, m_curr_margin);
    return (isEqualityComparison(comp)) ? (conditionValue <= 0.0) : (conditionValue < 0.0);
}

void Condition::setMargin(double val)
{
    m_margin = val;
    if (use_margin) {
        m_curr_margin = m_margin;
    }
}

coreObject* Condition::getObject() const
{
    if (conditionLHS) {
        return conditionLHS->getObject();
    }

    return nullptr;
}

void Condition::getObjects(std::vector<coreObject*>& objects) const
{
    if (conditionLHS) {
        conditionLHS->getObjects(objects);
    }

    if (!m_constRHS) {
        if (conditionRHS) {
            conditionRHS->getObjects(objects);
        }
    }
}

}  // namespace griddyn
