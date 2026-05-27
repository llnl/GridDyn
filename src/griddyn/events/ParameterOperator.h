/*
 * Copyright (c) 2014-2026, Lawrence Livermore National Security
 * See the top-level NOTICE for additional details. All rights reserved.
 * SPDX-License-Identifier: BSD-3-Clause
 */

#pragma once

// headers
// #include "GridDynSimulation.h"

#include "core/HelperObject.h"
#include "core/ObjectOperatorInterface.hpp"
#include "core/coreDefinitions.hpp"
#include <memory>
#include <string>
#include <vector>

namespace griddyn {
class GridComponent;
/** a class defining a parameter to change as part of a sequence or other operation
 */
class ParameterOperator: public HelperObject, public ObjectOperatorInterface {
  protected:
    std::string m_field;  //!< field to trigger
    GridComponent* comp = nullptr;  //!< the object to operator on
    index_t parameterIndex = kNullLocation;  //!< the parameter index to use if so inclined
  public:
    ParameterOperator();
    ParameterOperator(GridComponent* target, const std::string& field);

    virtual void setTarget(GridComponent* target, const std::string& field = "");

    virtual void updateObject(CoreObject* target,
                              ObjectUpdateMode mode = ObjectUpdateMode::DIRECT) override;
    virtual void setParameter(double val);
    virtual double getParameter() const;
    virtual CoreObject* getObject() const override;
    virtual void getObjects(std::vector<CoreObject*>& objects) const override;
    bool isDirect() const { return (parameterIndex != kNullLocation); }

  protected:
    void checkField();
};

/** construct a parameter operator object from a field and root object*/
std::unique_ptr<ParameterOperator> make_parameterOperator(std::string_view param,
                                                          GridComponent* rootObject);

/** helper class defining a set of parameters for various operations*/
class ParameterSet {
  private:
    std::vector<std::unique_ptr<ParameterOperator>> params;

  public:
    ParameterSet() = default;
    index_t add(const std::string& paramString, GridComponent* rootObject);
    ParameterOperator* operator[](index_t index);
};

}  // namespace griddyn
