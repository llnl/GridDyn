/*
 * Copyright (c) 2014-2020, Lawrence Livermore National Security
 * See the top-level NOTICE for additional details. All rights reserved.
 * SPDX-License-Identifier: BSD-3-Clause
 */

#include "elementReaderTemplates.hpp"
#include "formatInterpreters/tinyxml2ReaderElement.h"
#include "formatInterpreters/tinyxmlReaderElement.h"
#include "griddyn/gridDynSimulation.h"
#include "readElement.h"
#include "readElementFile.h"
#include "readerHelper.h"
#include <cstdio>
#include <sstream>
#include <utility>

#include <filesystem>

namespace griddyn {
using namespace readerConfig;

std::unique_ptr<gridDynSimulation>
    readSimXMLFile(const std::string& fileName, readerInfo* ri, xmlreader rtype)
{
    if (!std::filesystem::exists(fileName)) {
        return nullptr;
    }
    if (rtype == xmlreader::default_reader) {
        rtype = readerConfig::default_xml_reader;
    }
    switch (rtype) {
        case xmlreader::tinyxml:
        default:
            return std::unique_ptr<gridDynSimulation>(static_cast<gridDynSimulation*>(
                loadElementFile<tinyxmlReaderElement>(nullptr, fileName, ri)));
        case xmlreader::tinyxml2:
            return std::unique_ptr<gridDynSimulation>(static_cast<gridDynSimulation*>(
                loadElementFile<tinyxml2ReaderElement>(nullptr, fileName, ri)));
    }
}
}  // namespace griddyn
