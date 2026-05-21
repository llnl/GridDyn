/*
 * Copyright (c) 2014-2026, Lawrence Livermore National Security
 * See the top-level NOTICE for additional details. All rights reserved.
 * SPDX-License-Identifier: BSD-3-Clause
 */

#pragma once

#include <exception>
#include <vector>

namespace griddyn {
class CoreObject;

/**enumeration of the update mode*/
enum class ObjectUpdateMode {
    DIRECT,  //!< just update with the given object
    direct = DIRECT,  // NOLINT(readability-identifier-naming)
    MATCH,  //!< use the object as a search for replacing the specified object
    match = MATCH,  // NOLINT(readability-identifier-naming)
};

/** exception class for use if the object failed to update*/
class ObjectUpdateFailException: public std::exception {
  public:
    virtual const char* what() const noexcept override { return "object update Fail"; }
};

/** exception class for use if an object operator interface failed to complete an add function
 * operation*/
class AddFailureException: public std::exception {
  public:
    virtual const char* what() const noexcept override { return "add operation failed"; }
};

/** @brief defining a very basic virtual interface for all objects which work with events and
 * triggers
 */
class ObjectOperatorInterface {
  public:
    /** virtual destructor*/
    virtual ~ObjectOperatorInterface() = default;
    /** basic function to update an object
    @param[in] obj the new object
    @param[in] mode the update mode can be either direct which means that the given object is the
    new target or match which means that the given object is the a new container object and the
    existing object should be matched to something in the container object and updated
    @throw ObjectUpdateFailException on update failure
    */
    virtual void updateObject(CoreObject* obj,
                              ObjectUpdateMode mode = ObjectUpdateMode::DIRECT) = 0;

    /** function to check whether the object can be updated
    @details used in cases where a throw might cause an inconsistent state for cases of a match
    ObjectUpdateMode
    @param[in] obj the new object
    @return true if the object update will succeed false otherwise
    */
    virtual bool checkValidUpdate(CoreObject* obj) const { return (obj != nullptr); }
    /** get an object that is used by the interface
    @return a pointer to the object
    */
    virtual CoreObject* getObject() const = 0;
    /** add the object contained in the operator to a vector of objects
    @param[out] objects the vector of objects to add any used objects to
    */
    virtual void getObjects(std::vector<CoreObject*>& objects) const = 0;
};

using object_update_mode = ObjectUpdateMode;  // NOLINT(readability-identifier-naming)
using objectUpdateFailException =
    ObjectUpdateFailException;  // NOLINT(readability-identifier-naming)
using addFailureException = AddFailureException;  // NOLINT(readability-identifier-naming)
using objectOperatorInterface = ObjectOperatorInterface;  // NOLINT(readability-identifier-naming)

}  // namespace griddyn
