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
    /** Capture the object's name for later diagnostics. The object may be null. */
    explicit CoreObjectException(const CoreObject* obj);
    /** Return the base exception message. */
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
    /** Report that this object type cannot be added to the destination. */
    explicit UnrecognizedObjectException(const CoreObject* obj): CoreObjectException(obj) {}
    /** Identify an unsupported object type in the diagnostic. */
    virtual const char* what() const noexcept override { return "unrecognized object"; }
};

/** Failure to attach a component to its destination. */
class ObjectAddFailure: public CoreObjectException {
  public:
    /** Report an addition failure with the associated object context. */
    explicit ObjectAddFailure(const CoreObject* obj): CoreObjectException(obj) {}
    /** Identify an attachment failure in the diagnostic. */
    virtual const char* what() const noexcept override { return "failure to add object"; }
};

/** Failure to detach a component. */
class ObjectRemoveFailure: public CoreObjectException {
  public:
    /** Report a removal failure with the associated object context. */
    explicit ObjectRemoveFailure(const CoreObject* obj): CoreObjectException(obj) {}
    /** Identify a detachment failure in the diagnostic. */
    virtual const char* what() const noexcept override { return "failure to remove object"; }
};

/** Unknown parameter, flag or command name. */
class UnrecognizedParameter: public std::invalid_argument {
  public:
    /** Use the default message when a name is unavailable. */
    UnrecognizedParameter(): UnrecognizedParameter(std::string_view{}) {}
    /** Include the rejected name in the diagnostic when supplied. */
    explicit UnrecognizedParameter(std::string_view param):
        std::invalid_argument(param.empty() ? "unrecognized parameter" :
                                              "unrecognized parameter '" + std::string(param) + "'")
    {
    }
};

/** Invalid value or syntax for a recognized setting or operation. */
class InvalidParameterValue: public std::invalid_argument {
  public:
    /** Use the default message when no value detail is available. */
    InvalidParameterValue(): InvalidParameterValue(std::string_view{}) {}
    /** Include the rejected value or validation detail in the message. */
    explicit InvalidParameterValue(std::string_view detail):
        std::invalid_argument(detail.empty() ? "invalid parameter value" :
                                               "invalid parameter value: " + std::string(detail))
    {
    }
    /** Prefix an existing invalid-value diagnostic with caller context. */
    InvalidParameterValue(std::string_view context, const InvalidParameterValue& cause):
        std::invalid_argument(std::string(context) + ": " + cause.what())
    {
    }
};

/** Operation failure with a diagnostic supplied by the caller. */
class ExecutionFailure: public CoreObjectException {
  private:
    std::string message;

  public:
    /** Preserve the object context and the supplied execution diagnostic. */
    explicit ExecutionFailure(const CoreObject* obj, std::string_view error_message):
        CoreObjectException(obj), message(error_message)
    {
    }
    /** Return the execution diagnostic. */
    virtual const char* what() const noexcept override { return message.c_str(); }
};
/** Failure to create or populate an object clone. */
class CloneFailure: public CoreObjectException {
  public:
    /** Report a cloning failure with the associated object context. */
    explicit CloneFailure(const CoreObject* obj): CoreObjectException(obj) {}
    /** Identify a clone failure in the diagnostic. */
    virtual const char* what() const noexcept override { return "clone failure"; }
};

/** Failure to open, read, validate or write a file. */
class FileOperationError: public std::exception {
  private:
    std::string message;

  public:
    /** Store the file-operation diagnostic. */
    explicit FileOperationError(std::string_view error_message = "file operation error"):
        message(error_message)
    {
    }
    /** Return the stored file-operation diagnostic. */
    virtual const char* what() const noexcept override { return message.c_str(); }
};
/** File operation rejected because its path is invalid. */
class InvalidFileName: public FileOperationError {
  public:
    /** Set the filename diagnostic, using the standard message by default. */
    explicit InvalidFileName(std::string_view error_message = "file name is invalid"):
        FileOperationError(error_message)
    {
    }
};

}  // namespace griddyn
