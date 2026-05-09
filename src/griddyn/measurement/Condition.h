/*
 * Copyright (c) 2014-2026, Lawrence Livermore National Security
 * See the top-level NOTICE for additional details. All rights reserved.
 * SPDX-License-Identifier: BSD-3-Clause
 */

#pragma once

#include "../gridComponent.h"
#include "core/objectOperatorInterface.hpp"
#include <functional>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

namespace griddyn {
class grabberSet;
/** enumeration of the available comparison types*/
enum class comparison_type {
    eq,
    ne,
    gt,
    ge,
    le,
    lt,
    null,
};
/** generate a value of the comparison type enumeration from a string*/
comparison_type comparisonFromString(std::string_view compStr);
/** convert the comparison type to a string description*/
std::string to_string(comparison_type comp);

/**
 *condition class:  sets up a comparison operator for both states and regular grabbers
 **/
class Condition: public objectOperatorInterface {
  protected:
    double m_constant = 0.0;  //!< right hand side constant
    double m_margin = 0.0;  //!< the margin around the conditions
    double m_curr_margin = 0.0;  //!< the currently used margin
    std::shared_ptr<grabberSet> conditionLHS;  //!< grabber for left side condition
    std::shared_ptr<grabberSet> conditionRHS;  //!< grabber for right side condition
    bool m_constRHS = false;  //!< flag indicating use of a constant RHS
    bool use_margin = false;  //!< flag indicating margin use
  public:
    /** default constructor
    @param[in] valGrabber the grabber for the LHS of the equation
    */
    explicit Condition(std::shared_ptr<grabberSet> valGrabber = nullptr);
    /** destructor*/
    virtual ~Condition();
    /** clone the condition
    @return a new Condition object with identical to the original
    */
    virtual std::unique_ptr<Condition> clone() const;
    /** clone the condition to a given object
    @param[out] cond a Condition object to clone the information to
    */
    virtual void cloneTo(Condition* cond) const;
    /** load the grabbers for the Left hand side of the condition equation
    @param[in] valGrabber a gridGrabber
    */
    void setConditionLHS(std::shared_ptr<grabberSet> valGrabber);
    /** load the grabbers for the Right hand side of the condition equation
    @param[in] valGrabber a gridGrabber
    */
    void setConditionRHS(std::shared_ptr<grabberSet> valGrabber);
    /**run the comparison between condition sides from gridGrabbers
     * @return returns a difference between the condition and parameter designed to go negative if
     *the condition is met
     **/
    virtual double evalCondition();
    /**
     *run the comparison between for state grabbers
     * @return returns a difference between the condition and parameter designed to go negative if
     *the condition is met
     **/
    virtual double evalCondition(const stateData& stateDataValue, const solverMode& sMode);
    /**
    *get the value for which the comparison is made
    @param[in] side either 1 for left hand side or 2 for right hand side
    @param[in] sD the state data from which to get the values
    @param[in] sMode the solverMode related to the state data
    * @return returns the value for the comparison side=1 is left hand side, side=2 is the right
    hand side
    **/
    double getVal(int side, const stateData& stateDataValue, const solverMode& sMode) const;
    /**
     *get the value for which the comparison is made
     * @return returns the value for the comparison side=1 is left hand side, side=2 is the right
     *hand side
     **/
    double getVal(int side) const;

    virtual void updateObject(coreObject* obj,
                              object_update_mode mode = object_update_mode::direct) override;

    /** evaluation the condition based on object data
    @return true if the condition evaluates true
    */
    virtual bool checkCondition() const;
    /** evaluation the condition based on state data
    @param[in] sD the state data
    @param[in] sMode the solver mode related to the data
    @return true if the condition evaluates true
    */
    virtual bool checkCondition(const stateData& stateDataValue, const solverMode& sMode) const;
    /** set the comparison operator
    @param[in] comparison the comparison type*/
    void setComparison(comparison_type comparison);
    /** set the comparison operator
    @param[in] compStr the comparison type as a string*/
    void setComparison(std::string_view compStr);

    /** set the level of the right side of the condition operate as a constant value
    @param[in] val the threshold
    */
    void setConditionRHS(double val)
    {
        m_constRHS = true;
        m_constant = val;
    }
    /** set the margin level for equality conditions
    @param[in] val the new margin
    */
    virtual void setMargin(double val);
    /** change the margin setting functionality
    @param[in] margin_on  a boolean true if the condition operator should use margins, false
    otherwise
    */
    virtual void useMargin(bool margin_on)
    {
        m_curr_margin = (margin_on) ? (m_margin) : 0.0;
        use_margin = margin_on;
    }

    virtual coreObject* getObject() const override;

    virtual void getObjects(std::vector<coreObject*>& objects) const override;

  private:
    std::function<double(double A, double B, double margin)>
        evalf;  //!< the function evaluation used in the condition
    comparison_type comp = comparison_type::gt;  //!< the condition operator
};

/** make a condition object
 * function to create a condition object from a string describing the condition
 * @param[in] condString a condition string  like bus1:Voltage>bus2::voltage
 * @param[in] rootObject the root object to find the other objects
 */
std::unique_ptr<Condition> make_condition(std::string_view condString, coreObject* rootObject);
/** make a condition object
* function to create a condition object from a field a comparison type and threshold
* @param field the field to get from the rootObject
@param compare a string of the comparison function
@param level the threshold level for the comparson
* @param[in] rootObject the root object to find the other objects
*/
std::unique_ptr<Condition> make_condition(std::string_view field,
                                          std::string_view compare,
                                          double level,
                                          coreObject* rootObject);
/** make a condition object
* function to create a condition object from a field a comparison type and threshold
* @param field the field to get from the rootObject
@param comp a value in the comparison enumeration
@param level the threshold level for the comparson
* @param[in] rootObject the root object to find the other objects
*/
std::unique_ptr<Condition> make_condition(std::string_view field,
                                          comparison_type comp,
                                          double level,
                                          coreObject* rootObject);

/** evaluate a compound condition consisting of multiple individual conditions
 */
class compoundCondition: public Condition {
  public:
    /** enumeration of the possible compounding modes*/
    enum class compound_mode {
        c_and,  //!< all conditions are true
        c_all,  //!< same as c_and  all conditions are true
        c_or,  //!< any of the conditions are true
        c_any,  //!< same as c_or, any of the conditions are true
        c_xor,  //!< an odd number of the conditions are true
        c_one_of,  //!< exactly one of the conditions are true
        c_two_of,  //!< exactly two of the conditions are true
        c_three_of,  //!< exactly three of the conditions are true
        c_none,  //!< no conditions are true
        c_two_or_more,  //!< 2 or more of the conditions are true
        c_three_or_more,  //!< 3 or more of the conditions are true
        c_even,  //!< an even number of conditions is true (0 is an even number)
        c_even_min,  //!< an even number of conditions are true with at least two being true
        c_odd,  //!< an odd number of conditions is true, same as xor
    };

  private:
    bool breakFalse = true;  //!< indicator that the checking should stop if a false is encountered
    bool breakTrue = false;  //!< indicator that the checking should stop if a true is encountered
    std::vector<std::shared_ptr<Condition>> conditions;  //!< vector of pointers to the conditions
    compound_mode mode = compound_mode::c_and;  //!< the compounding mode to use
  public:
    compoundCondition() = default;

    virtual double evalCondition() override;
    virtual double evalCondition(const stateData& stateDataValue,
                                 const solverMode& sMode) override;
    virtual bool checkCondition() const override;
    virtual bool checkCondition(const stateData& stateDataValue,
                                const solverMode& sMode) const override;
    /** add a condition to the set of conditions to evaluate*/
    void add(std::shared_ptr<Condition> gc);
    /** set the compounding mode
     */
    void setMode(compound_mode newMode);

  private:
    /** evaluate the compounding based on the number of true values encountered*/
    bool evalCombinations(count_t trueCount) const;
};

}  // namespace griddyn
