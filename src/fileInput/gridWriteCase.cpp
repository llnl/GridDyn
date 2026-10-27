/*
 * Copyright (c) 2014-2026, Lawrence Livermore National Security
 * See the top-level NOTICE for additional details. All rights reserved.
 * SPDX-License-Identifier: BSD-3-Clause
 */

#include "fileInput.h"

#include <algorithm>
#include <cctype>
#include <filesystem>
#include <iostream>

namespace griddyn {
namespace {
    enum class NetworkCaseFormat { UNSUPPORTED, MATPOWER, PYPOWER, PSSE_RAW };

    NetworkCaseFormat caseFormatFromFileName(const std::string& fileName)
    {
        auto extension = std::filesystem::path(fileName).extension().string();
        std::transform(extension.begin(), extension.end(), extension.begin(), [](unsigned char ch) {
            return static_cast<char>(std::tolower(ch));
        });

        if (extension == ".m" || extension == ".matpower") {
            return NetworkCaseFormat::MATPOWER;
        }
        if (extension == ".py" || extension == ".pypower") {
            return NetworkCaseFormat::PYPOWER;
        }
        if (extension == ".raw" || extension == ".psse" || extension == ".pti") {
            return NetworkCaseFormat::PSSE_RAW;
        }
        return NetworkCaseFormat::UNSUPPORTED;
    }
}  // namespace

bool isNetworkCaseOutputFile(const std::string& fileName)
{
    return caseFormatFromFileName(fileName) != NetworkCaseFormat::UNSUPPORTED;
}

bool saveNetworkCaseByExtension(const CoreObject* parentObject,
                                const std::string& fileName,
                                stringVec* warnings)
{
    const auto warning = [warnings](const std::string& message) {
        if (warnings != nullptr) {
            warnings->clear();
            warnings->push_back(message);
        } else {
            std::cerr << message << '\n';
        }
    };

    switch (caseFormatFromFileName(fileName)) {
        case NetworkCaseFormat::MATPOWER:
            return saveMatPowerCase(parentObject, fileName, warnings);
        case NetworkCaseFormat::PYPOWER:
            return savePyPowerCase(parentObject, fileName, warnings);
        case NetworkCaseFormat::PSSE_RAW:
            return saveRawCase(parentObject, fileName, warnings);
        case NetworkCaseFormat::UNSUPPORTED:
            if (warnings != nullptr) {
                warnings->clear();
            }
            warning("unsupported network case output extension for '" + fileName + "'");
            return false;
    }
    return false;
}
}  // namespace griddyn
