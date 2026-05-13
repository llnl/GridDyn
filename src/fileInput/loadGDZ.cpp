/*
 * Copyright (c) 2014-2026, Lawrence Livermore National Security
 * See the top-level NOTICE for additional details. All rights reserved.
 * SPDX-License-Identifier: BSD-3-Clause
 */

#include "fileInput.h"
#include "utilities/zipUtilities.h"
#include <filesystem>
#include <string>

namespace griddyn {
void loadGDZ(coreObject* parentObject,
             const std::string& fileName,
             readerInfo& readerInformation)
{
    std::filesystem::path filePath(fileName);
    if (!std::filesystem::exists(filePath)) {
        return;
    }

    auto extractPath = std::filesystem::temp_directory_path() / filePath.stem();
    auto keyFile = extractPath / "simulation.xml";
    if (!std::filesystem::exists(keyFile)) {
        const auto unzipResult = utilities::unzip(fileName, extractPath.string());
        if (unzipResult != 0) {
            return;
        }
    }

    if (!std::filesystem::exists(keyFile)) {
        return;
    }
    readerInformation.addDirectory(extractPath.string());
    auto resourcePath = extractPath / "resources";
    if (std::filesystem::exists(resourcePath)) {
        readerInformation.addDirectory(resourcePath.string());
    }
    loadFile(parentObject, keyFile.string(), &readerInformation, "xml");
}

}  // namespace griddyn
