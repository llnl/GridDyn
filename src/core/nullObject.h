/*
 * Copyright (c) 2014-2026, Lawrence Livermore National Security
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
class nullObject final: public CoreObject {
  public:
    /** @brief default constructor
    takes a code used as the oid for special id codes below 100
    */
    explicit nullObject(std::uint64_t nullCode = 500) noexcept;
    /** @brief nullObject constructor which takes a string*/
    explicit nullObject(std::string_view objName);

    virtual CoreObject* clone(CoreObject* obj = nullptr) const override;
    /**
     * alert just sinks all alerts and does nothing
     */
    virtual void alert(CoreObject* object, int code) override;

    /**
     * log just absorbs all log messages and does nothing
     */
    virtual void log(CoreObject* object, print_level level, const std::string& message) override;
    virtual bool shouldLog(print_level level) const override;

    /** return a nullptr*/
    virtual CoreObject* find(std::string_view object) const override;

    /**
    returns a nullptr
    */
    virtual CoreObject* findByUserID(std::string_view typeName, index_t searchID) const override;

    /** @brief set the parent
    @details nullObjects do not allow the parents to be set*/
    virtual void setParent(CoreObject* parentObj) override;
};
}  // namespace griddyn
