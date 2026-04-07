/*
 * Copyright (c) 2014-2020, Lawrence Livermore National Security
 * See the top-level NOTICE for additional details. All rights reserved.
 * SPDX-License-Identifier: BSD-3-Clause
 */

#pragma once

#include "coreObject.h"
#include <exception>
#include <stdexcept>

namespace griddyn {
/** exception class for use in griddyn*/
class coreObjectException: public std::exception {
  protected:
    const coreObject* throwingObject;  ///< the object that threw the exception
  public:
    explicit coreObjectException(const coreObject* obj) noexcept;
    virtual const char* what() const noexcept override { return "core object exception"; }
    /** return the full name of the object that threw the exception*/
    std::string who() const noexcept;
    /** change the object for use with a cascading object*/
    void updateObject(const coreObject* newobj) { throwingObject = newobj; }
};

/** exception for use when an object is added to another object but the object
is not of a type that can be added
*/
class unrecognizedObjectException: public coreObjectException {
  public:
    explicit unrecognizedObjectException(coreObject* obj) noexcept: coreObjectException(obj) {}
    virtual const char* what() const noexcept override { return "unrecognized object"; }
};

class objectAddFailure: public coreObjectException {
  public:
    explicit objectAddFailure(coreObject* obj) noexcept: coreObjectException(obj) {}
    virtual const char* what() const noexcept override { return "failure to add object"; }
};

class objectRemoveFailure: public coreObjectException {
  public:
    explicit objectRemoveFailure(coreObject* obj) noexcept: coreObjectException(obj) {}
    virtual const char* what() const noexcept override { return "failure to remove object"; }
};

class unrecognizedParameter: public std::invalid_argument {
  public:
    unrecognizedParameter() noexcept: std::invalid_argument("unrecognized parameter") {}
    unrecognizedParameter(const std::string& param):
        std::invalid_argument(std::string("unrecognized Parameter:") + param)
    {
    }
};

class invalidParameterValue: public std::invalid_argument {
  public:
    invalidParameterValue() noexcept: std::invalid_argument("invalid parameter entry") {}
    invalidParameterValue(const std::string& param):
        std::invalid_argument(std::string("invalid parameter value for ") + param)
    {
    }
};

class executionFailure: public coreObjectException {
  private:
    std::string message;

  public:
    explicit executionFailure(const coreObject* obj, const std::string& error_message):
        coreObjectException(obj), message(error_message)
    {
    }
    virtual const char* what() const noexcept override { return message.c_str(); }
};
class cloneFailure: public coreObjectException {
  public:
    explicit cloneFailure(const coreObject* obj) noexcept: coreObjectException(obj) {}
    virtual const char* what() const noexcept override { return "clone failure"; }
};

class fileOperationError: public std::exception {
  private:
    std::string message;

  public:
    explicit fileOperationError(const std::string& error_message = "file operation error"):
        message(error_message)
    {
    }
    virtual const char* what() const noexcept override { return message.c_str(); }
};
class invalidFileName: public fileOperationError {
  public:
    explicit invalidFileName(const std::string& error_message = "file name is invalid"):
        fileOperationError(error_message)
    {
    }
};

}  // namespace griddyn
