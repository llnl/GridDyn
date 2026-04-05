/*
 * Copyright (c) 2014-2020, Lawrence Livermore National Security
 * See the top-level NOTICE for additional details. All rights reserved.
 * SPDX-License-Identifier: BSD-3-Clause
 */

#include "fileInput.h"
#include "utilities/zipUtilities.h"

#include <filesystem>

namespace griddyn {
using namespace std::filesystem;

void loadGDZ(coreObject* parentObject, const std::string& fileName, readerInfo& ri)
{
    path fpath(fileName);
    if (!exists(fpath)) {
        return;
    }

    auto extractPath = temp_directory_path() / fpath.stem();
    auto keyFile = extractPath / "simulation.xml";
    if (!exists(keyFile)) {
        auto ret = utilities::unzip(fileName, extractPath.string());
        if (ret != 0) {
            return;
        }
    }

    if (!exists(keyFile)) {
        return;
    }
    ri.addDirectory(extractPath.string());
    auto resourcePath = extractPath / "resources";
    if (exists(resourcePath)) {
        ri.addDirectory(resourcePath.string());
    }
    loadFile(parentObject, keyFile.string(), &ri, "xml");
}

}  // namespace griddyn
