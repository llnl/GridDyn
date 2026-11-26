/*
 * Copyright (c) 2014-2026, Lawrence Livermore National Security
 * See the top-level NOTICE for additional details. All rights reserved.
 * SPDX-License-Identifier: BSD-3-Clause
 */

#include "core/CoreExceptions.h"
#include "fileInput.h"
#include "readerHelper.h"
#include "utilities/zipUtilities.h"
#include <filesystem>
#include <string>

namespace griddyn {
void loadGdz(CoreObject* parentObject, const std::string& fileName, ReaderInfo& ReaderInformation)
{
    validateInputFile(fileName);
    const std::filesystem::path filePath(fileName);

    auto extractPath = std::filesystem::temp_directory_path() / filePath.stem();
    auto keyFile = extractPath / "simulation.xml";
    if (!std::filesystem::exists(keyFile)) {
        const auto unzipResult = utilities::unzip(fileName, extractPath.string());
        if (unzipResult != 0) {
            throw FileOperationError("unable to extract GDZ file '" + fileName + "'");
        }
    }

    if (!std::filesystem::exists(keyFile)) {
        throw FileOperationError("GDZ file '" + fileName + "' does not contain simulation.xml");
    }
    ReaderInformation.addDirectory(extractPath.string());
    auto resourcePath = extractPath / "resources";
    if (std::filesystem::exists(resourcePath)) {
        ReaderInformation.addDirectory(resourcePath.string());
    }
    loadFile(parentObject, keyFile.string(), &ReaderInformation, "xml");
}

}  // namespace griddyn
