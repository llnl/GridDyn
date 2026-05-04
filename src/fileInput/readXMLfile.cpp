/*
 * Copyright (c) 2014-2026, Lawrence Livermore National Security
 * See the top-level NOTICE for additional details. All rights reserved.
 * SPDX-License-Identifier: BSD-3-Clause
 */

#include "elementReaderTemplates.hpp"
#include "formatInterpreters/XmlReaderElement.h"
#include "griddyn/gridDynSimulation.h"
#include "readElement.h"
#include "readElementFile.h"
#include "readerHelper.h"
#include <cstdio>
#include <filesystem>
#include <memory>
#include <sstream>
#include <string>
#include <utility>

namespace griddyn {
std::unique_ptr<gridDynSimulation>
    readSimXMLFile(const std::string& fileName, readerInfo* readerInfoPtr, xmlreader rtype)
{
    if (!std::filesystem::exists(fileName)) {
        return nullptr;
    }
    return std::unique_ptr<gridDynSimulation>(static_cast<gridDynSimulation*>(
        loadElementFile<XmlReaderElement>(nullptr, fileName, readerInfoPtr)));
}
}  // namespace griddyn
