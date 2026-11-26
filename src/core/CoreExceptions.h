/*
 * Copyright (c) 2014-2026, Lawrence Livermore National Security
 * See the top-level NOTICE for additional details. All rights reserved.
 * SPDX-License-Identifier: BSD-3-Clause
 */

#pragma once

#include "CoreObject.h"
#include <exception>
#include <stdexcept>
#include <string>
#include <string_view>

namespace griddyn {
/** exception class for use in griddyn*/
class CoreObjectException: public std::exception {
  private:
    std::string objectName;

  public:
    explicit CoreObjectException(const CoreObject* obj);
    virtual const char* what() const noexcept override { return "core object exception"; }
    /** Return the object name captured when the exception was constructed or updated. */
    std::string who() const;
    /** Return the exception message with its object context, when available. */
    std::string diagnostic() const;
    /** change the object for use with a cascading object*/
    void updateObject(const CoreObject* newobj);
};

/** exception for use when an object is added to another object but the object
is not of a type that can be added
*/
class UnrecognizedObjectException: public CoreObjectException {
  public:
    explicit UnrecognizedObjectException(const CoreObject* obj): CoreObjectException(obj) {}
    virtual const char* what() const noexcept override { return "unrecognized object"; }
};

class ObjectAddFailure: public CoreObjectException {
  public:
    explicit ObjectAddFailure(const CoreObject* obj): CoreObjectException(obj) {}
    virtual const char* what() const noexcept override { return "failure to add object"; }
};

class ObjectRemoveFailure: public CoreObjectException {
  public:
    explicit ObjectRemoveFailure(const CoreObject* obj): CoreObjectException(obj) {}
    virtual const char* what() const noexcept override { return "failure to remove object"; }
};

class UnrecognizedParameter: public std::invalid_argument {
  public:
UnrecognizedParameter(): UnrecognizedParameter(std::string_view{}) {}
    explicit UnrecognizedParameter(std::string_view param):
        std::invalid_argument(param.empty() ? "unrecognized parameter" :
                                              "unrecognized parameter '" + std::string(param) + "'")
    {
    }
};

class InvalidParameterValue: public std::invalid_argument {
  public:
    InvalidParameterValue(): InvalidParameterValue(std::string_view{}) {}
    explicit InvalidParameterValue(std::string_view detail):
        std::invalid_argument(detail.empty() ? "invalid parameter value" :
                                              "invalid parameter value: " + std::string(detail))
    {
    }
    InvalidParameterValue(std::string_view context, const InvalidParameterValue& cause):
        std::invalid_argument(std::string(context) + ": " + cause.what())
    {
    }
};

class ExecutionFailure: public CoreObjectException {
  private:
    std::string message;

  public:
    explicit ExecutionFailure(const CoreObject* obj, std::string_view error_message):
        CoreObjectException(obj), message(error_message)
    {
    }
    virtual const char* what() const noexcept override { return message.c_str(); }
};
class CloneFailure: public CoreObjectException {
  public:
    explicit CloneFailure(const CoreObject* obj): CoreObjectException(obj) {}
    virtual const char* what() const noexcept override { return "clone failure"; }
};

class FileOperationError: public std::exception {
  private:
    std::string message;

  public:
    explicit FileOperationError(std::string_view error_message = "file operation error"):
        message(error_message)
    {
    }
    virtual const char* what() const noexcept override { return message.c_str(); }
};
class InvalidFileName: public FileOperationError {
  public:
    explicit InvalidFileName(std::string_view error_message = "file name is invalid"):
        FileOperationError(error_message)
    {
    }
};

}  // namespace griddyn
