/*
 * Copyright (c) 2014-2026, Lawrence Livermore National Security
 * See the top-level NOTICE for additional details. All rights reserved.
 * SPDX-License-Identifier: BSD-3-Clause
 */

#include "CoreExceptions.h"

#include <string>
namespace griddyn {
CoreObjectException::CoreObjectException(const CoreObject* obj)
{
    updateObject(obj);
}

std::string CoreObjectException::who() const
{
    return objectName;
}

std::string CoreObjectException::diagnostic() const
{
    return objectName.empty() ? what() : std::string(what()) + " [" + objectName + "]";
}

void CoreObjectException::updateObject(const CoreObject* obj)
{
    objectName = (obj != nullptr) ? fullObjectName(obj) : std::string{};
}
}  // namespace griddyn
