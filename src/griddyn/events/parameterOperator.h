/*
 * Copyright (c) 2014-2026, Lawrence Livermore National Security
 * See the top-level NOTICE for additional details. All rights reserved.
 * SPDX-License-Identifier: BSD-3-Clause
 */

#pragma once

// headers
// #include "gridDynSimulation.h"

#include "core/coreDefinitions.hpp"
#include "core/helperObject.h"
#include "core/objectOperatorInterface.hpp"
#include <memory>
#include <string>
#include <vector>

namespace griddyn {
class gridComponent;
/** a class defining a parameter to change as part of a sequence or other operation
 */
class parameterOperator: public helperObject, public objectOperatorInterface {
  protected:
    std::string m_field;  //!< field to trigger
    gridComponent* comp = nullptr;  //!< the object to operator on
    index_t parameterIndex = kNullLocation;  //!< the parameter index to use if so inclined
  public:
    parameterOperator();
    parameterOperator(gridComponent* target, const std::string& field);

    virtual void setTarget(gridComponent* target, const std::string& field = "");

    virtual void updateObject(CoreObject* target,
                              object_update_mode mode = object_update_mode::direct) override;
    virtual void setParameter(double val);
    virtual double getParameter() const;
    virtual CoreObject* getObject() const override;
    virtual void getObjects(std::vector<CoreObject*>& objects) const override;
    bool isDirect() const { return (parameterIndex != kNullLocation); }

  protected:
    void checkField();
};

/** construct a parameter operator object from a field and root object*/
std::unique_ptr<parameterOperator> make_parameterOperator(std::string_view param,
                                                          gridComponent* rootObject);

/** helper class defining a set of parameters for various operations*/
class parameterSet {
  private:
    std::vector<std::unique_ptr<parameterOperator>> params;

  public:
    parameterSet() = default;
    index_t add(const std::string& paramString, gridComponent* rootObject);
    parameterOperator* operator[](index_t index);
};

}  // namespace griddyn

