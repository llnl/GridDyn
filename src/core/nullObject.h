/*
 * Copyright (c) 2014-2020, Lawrence Livermore National Security
 * See the top-level NOTICE for additional details. All rights reserved.
 * SPDX-License-Identifier: BSD-3-Clause
 */

#pragma once

#include "coreObject.h"
#include <string>

namespace griddyn {
/** @brief      a nullObject class
pretty much does nothing it absorbs logs and alerts (its parent is itself)
**/
class nullObject final: public coreObject {
  public:
    /** @brief default constructor
    takes a code used as the oid for special id codes below 100
    */
    explicit nullObject(std::uint64_t nullCode = 500) noexcept;
    /** @brief nullObject constructor which takes a string*/
    explicit nullObject(const std::string& objName);

    virtual coreObject* clone(coreObject* obj = nullptr) const override;
    /**
     * alert just sinks all alerts and does nothing
     */
    virtual void alert(coreObject* object, int code) override;

    /**
     * log just absorbs all log messages and does nothing
     */
    virtual void log(coreObject* object, print_level level, const std::string& message) override;

    /** return a nullptr*/
    virtual coreObject* find(const std::string& object) const override;

    /**
    returns a nullptr
    */
    virtual coreObject* findByUserID(const std::string& typeName, index_t searchID) const override;

    /** @brief set the parent
    @details nullObjects do not allow the parents to be set*/
    virtual void setParent(coreObject* parentObj) override;
};
}  // namespace griddyn
