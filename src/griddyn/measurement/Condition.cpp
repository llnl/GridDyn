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
#include <unordered_map>
#include <utility>

namespace griddyn {

std::unique_ptr<Condition> make_condition(std::string_view condString, coreObject* rootObject)
{
    auto cString = gmlc::utilities::stringOps::xmlCharacterCodeReplace(std::string{condString});
    // size_t posA = condString.find_first_of("&|");
    // TODO(phlpt): Handle parenthesized and &| conditions.

    size_t pos = cString.find_first_of("><=!");
    if (pos == std::string::npos) {
        return nullptr;
    }

    char A = cString[pos];
    char B = cString[pos + 1];
    std::string BlockA = gmlc::utilities::stringOps::trim(cString.substr(0, pos - 1));
    std::string BlockB = (B == '=') ? cString.substr(pos + 2) : cString.substr(pos + 1);

    gmlc::utilities::stringOps::trimString(BlockB);
    auto gc = std::make_unique<Condition>();

    // get the state grabbers part

    gc->setConditionLHS(std::make_shared<grabberSet>(BlockA, rootObject));

    gc->setConditionRHS(std::make_shared<grabberSet>(BlockB, rootObject));

    std::string condstr;
    condstr.push_back(A);
    if (B == '=') {
        condstr.push_back(B);
    }

    gc->setComparison(comparisonFromString(condstr));

    return gc;
}

static const std::unordered_map<std::string, comparison_type> compStrMap{
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
    return mapFind(compStrMap, std::string{compStr}, comparison_type::null);
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
        case comparison_type::null:
            return "??";
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
        auto gc = std::make_unique<Condition>(std::move(gset));
        gc->setConditionRHS(level);

        gc->setComparison(comp);

        return gc;
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

void Condition::cloneTo(Condition* ngc) const
{
    ngc->m_constant = m_constant;
    ngc->m_margin = m_margin;
    ngc->m_constRHS = m_constRHS;
    ngc->m_curr_margin = m_curr_margin;
    ngc->use_margin = use_margin;

    if (conditionLHS) {
        if (ngc->conditionLHS) {
            conditionLHS->cloneTo(ngc->conditionLHS.get());
        } else {
            ngc->conditionLHS = conditionLHS->clone();
        }
    }

    if (!m_constRHS) {
        if (ngc->conditionRHS) {
            conditionRHS->cloneTo(ngc->conditionRHS.get());
        } else {
            ngc->conditionRHS = conditionRHS->clone();
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
    coreObject* keyObject = nullptr;
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
            throw(oe);
        }
    }
}

void Condition::setComparison(std::string_view compStr)
{
    setComparison(comparisonFromString(compStr));
}
void Condition::setComparison(comparison_type ct)
{
    comp = ct;
    switch (comp) {
        case comparison_type::gt:
        case comparison_type::ge:
            evalf = [](double p1, double p2, double margin) { return p2 - p1 - margin; };
            break;
        case comparison_type::lt:
        case comparison_type::le:
            evalf = [](double p1, double p2, double margin) { return p1 - p2 + margin; };
            break;
        case comparison_type::eq:
            evalf = [](double p1, double p2, double margin) { return std::abs(p1 - p2) - margin; };
            break;
        case comparison_type::ne:
            evalf = [](double p1, double p2, double margin) { return -std::abs(p1 - p2) + margin; };
            break;
        default:
            evalf = [](double, double, double) { return kNullVal; };
            break;
    }
}

double Condition::evalCondition()
{
    double v1 = conditionLHS->grabData();
    double v2 = (m_constRHS) ? m_constant : conditionRHS->grabData();

    return evalf(v1, v2, m_curr_margin);
}

double Condition::evalCondition(const stateData& sD, const solverMode& sMode)
{
    double v1 = conditionLHS->grabData(sD, sMode);
    double v2 = (m_constRHS) ? m_constant : conditionRHS->grabData(sD, sMode);
    return evalf(v1, v2, m_curr_margin);
}

double Condition::getVal(int side) const
{
    double v = (side == 2) ? ((m_constRHS) ? m_constant : conditionRHS->grabData()) :
                             conditionLHS->grabData();
    return v;
}

double Condition::getVal(int side, const stateData& sD, const solverMode& sMode) const
{
    double v = (side == 2) ? ((m_constRHS) ? m_constant : conditionRHS->grabData(sD, sMode)) :
                             conditionLHS->grabData(sD, sMode);
    return v;
}

inline bool isEqualityComparison(comparison_type comp)
{
    switch (comp) {
        case comparison_type::ge:
        case comparison_type::le:
        case comparison_type::eq:
            return true;
        default:
            return false;
    }
}

bool Condition::checkCondition() const
{
    double v1 = conditionLHS->grabData();
    double v2 = (m_constRHS) ? m_constant : conditionRHS->grabData();
    double ret = evalf(v1, v2, m_curr_margin);
    return (isEqualityComparison(comp)) ? (ret <= 0.0) : (ret < 0.0);
}

bool Condition::checkCondition(const stateData& sD, const solverMode& sMode) const
{
    double v1 = conditionLHS->grabData(sD, sMode);
    double v2 = (m_constRHS) ? m_constant : conditionRHS->grabData(sD, sMode);

    double ret = evalf(v1, v2, m_curr_margin);
    return (isEqualityComparison(comp)) ? (ret <= 0.0) : (ret < 0.0);
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
