/*
 * Copyright (c) 2014-2026, Lawrence Livermore National Security
 * See the top-level NOTICE for additional details. All rights reserved.
 * SPDX-License-Identifier: BSD-3-Clause
 */

#include "ReaderInfo.h"
#include "core/CoreExceptions.h"
#include "core/CoreObject.h"
#include "core/coreDefinitions.hpp"
#include "fileInput.h"
#include "gmlc/utilities/stringConversion.h"
#include "gmlc/utilities/stringOps.h"
#include "gridDynReadDyrModels.h"
#include "griddyn/GridDynSimulation.h"
#include <algorithm>
#include <array>
#include <cstddef>
#include <fstream>
#include <map>
#include <string>
#include <string_view>

namespace griddyn {
namespace {
    struct UnsupportedDydModelSummary {
        std::size_t mCount = 0;
        std::size_t mFirstLine = 0;
        std::string mFirstBus;
        std::string mFirstMachine;
    };

    bool isDydDirectModel(std::string_view modelName)
    {
        static constexpr std::array directModels{"gencls", "genrou", "genroe", "gensae", "gensal",
                                                 "esdc1a", "esdc2a", "ieeet1", "ieeet3", "ieeex1",
                                                 "ac7b",   "ac8b",   "esst1a", "esst2a", "esst3a",
                                                 "esst4b", "expic1", "scrx",   "esac6a", "exst1",
                                                 "exac1",  "esac1a", "exac2",  "exac4",  "exdc2",
                                                 "tgov1",  "hygov",  "gast",   "ieeeg1", "ieesgo",
                                                 "ieeest", "sexs"};
        const auto normalized = gmlc::utilities::convertToLowerCase(modelName);
        return std::ranges::any_of(directModels, [normalized](const char* directModel) {
            return normalized == directModel;
        });
    }

    bool isDydIgnoredLoadModel(std::string_view modelName)
    {
        static constexpr std::array ignoredLoadModels{"alwscc", "blwscc", "wlwscc", "zlwscc"};
        return std::ranges::any_of(ignoredLoadModels, [modelName](const char* ignoredModel) {
            return modelName == ignoredModel;
        });
    }

    std::size_t dydPositionalPayloadLimit(std::string_view modelName)
    {
        // These PSLF records contain additional fields after the positional
        // PSS/E-compatible prefix. Keep the prefix consumed by the existing
        // DYR loaders and ignore the trailing PSLF-specific values.
        if (modelName == "gast") {
            return 9U;
        }
        if (modelName == "hygov") {
            return 12U;
        }
        if (modelName == "ieeest") {
            return 19U;
        }
        return 0U;
    }

    void addUnsupportedModel(std::map<std::string, UnsupportedDydModelSummary>& unsupportedModels,
                             std::string_view modelName,
                             const stringVec& lineTokens,
                             std::size_t recordLineNumber)
    {
        auto& summary = unsupportedModels[std::string{modelName}];
        ++summary.mCount;
        if (summary.mFirstLine == 0U) {
            summary.mFirstLine = recordLineNumber;
            summary.mFirstBus = lineTokens.empty() ? "<missing>" : lineTokens[0];
            summary.mFirstMachine = (lineTokens.size() > 2U) ? lineTokens[2] : "<missing>";
        }
    }
}  // namespace

void loadDyd(CoreObject* parentObject,
             const std::string& fileName,
             const BasicReaderInfo& /*readerOptions*/)
{
    const auto* simulation = dynamic_cast<const GridDynSimulation*>(parentObject->getRoot());
    const bool disableStabilizers =
        (simulation != nullptr) && simulation->isFlagSet(DISABLE_STABILIZERS_FOR_DIAGNOSTICS);
    count_t zeroGainStabilizers = 0;
    std::ifstream file(fileName.c_str(), std::ios::in);
    std::string line;
    std::size_t lineNumber = 0;
    std::map<std::string, UnsupportedDydModelSummary> unsupportedModels;
    std::map<std::string, UnsupportedDydModelSummary> ignoredLoadModels;

    if (!file.is_open()) {
        parentObject->log(parentObject, PrintLevel::ERROR, "Unable to open file " + fileName);
        return;
    }

    while (std::getline(file, line)) {
        ++lineNumber;
        gmlc::utilities::stringOps::trimString(line);
        if (line.empty() || line.starts_with('#')) {
            continue;
        }

        // A PSLF DYD model record has a colon separating its identifying fields
        // from the parameter payload. Section headers and NETTING records do not.
        const auto colon = line.find(':');
        if (colon == std::string::npos) {
            continue;
        }
        const auto header = gmlc::utilities::stringOps::splitlineQuotes(
            line.substr(0, colon),
            " \t\n,",
            gmlc::utilities::stringOps::default_quote_chars,
            gmlc::utilities::stringOps::delimiter_compression::on);
        if (header.size() < 5U) {
            continue;
        }

        const auto modelName = gmlc::utilities::convertToLowerCase(
            gmlc::utilities::stringOps::removeQuotes(header[0]));
        const auto displayModelName = gmlc::utilities::convertToUpperCase(modelName);
        stringVec lineTokens{gmlc::utilities::stringOps::removeQuotes(header[1]),
                             "'" + displayModelName + "'",
                             gmlc::utilities::stringOps::removeQuotes(header[4])};
        if (isDydIgnoredLoadModel(modelName)) {
            addUnsupportedModel(ignoredLoadModels, displayModelName, lineTokens, lineNumber);
            continue;
        }
        const auto payload = gmlc::utilities::stringOps::splitlineQuotes(
            line.substr(colon + 1),
            " \t\n,",
            gmlc::utilities::stringOps::default_quote_chars,
            gmlc::utilities::stringOps::delimiter_compression::on);

        bool hasUnsupportedField = false;
        const auto payloadLimit = dydPositionalPayloadLimit(modelName);
        std::size_t positionalPayloadCount = 0;
        for (const auto& token : payload) {
            if (token == "#9") {
                continue;
            }
            // Generator DYD records carry the machine base as a named field;
            // EPC has already supplied that value to the generator object.
            if (((modelName == "genrou") || (modelName == "gensal")) && token.starts_with("mva=")) {
                continue;
            }
            // Named fields in other DYD schemas need an explicit conversion
            // table. Do not pass them through as positional DYR values.
            if (token.contains('=')) {
                hasUnsupportedField = true;
                break;
            }
            if ((payloadLimit != 0U) && (positionalPayloadCount >= payloadLimit)) {
                continue;
            }
            lineTokens.push_back(token);
            ++positionalPayloadCount;
        }

        if (hasUnsupportedField || !isDydDirectModel(modelName) ||
            !detail::loadDyrModelRecord(
                parentObject, lineTokens, disableStabilizers, zeroGainStabilizers)) {
            addUnsupportedModel(unsupportedModels, displayModelName, lineTokens, lineNumber);
        }
    }

    if (!unsupportedModels.empty()) {
        std::string message = fileName + ": unsupported DYD models:";
        for (const auto& [modelName, summary] : unsupportedModels) {
            message += "\n  " + modelName + ": " + std::to_string(summary.mCount) +
                " record(s); first at line " + std::to_string(summary.mFirstLine) + ", bus " +
                summary.mFirstBus + " machine " + summary.mFirstMachine;
        }
        throw InvalidParameterValue(message);
    }
    if (!ignoredLoadModels.empty()) {
        std::string message = fileName +
            ": ignored DYD load-characteristic models (using DYR-equivalent static loads):";
        for (const auto& [modelName, summary] : ignoredLoadModels) {
            message += "\n  " + modelName + ": " + std::to_string(summary.mCount) +
                " record(s); first at line " + std::to_string(summary.mFirstLine) + ", bus " +
                summary.mFirstBus + " machine " + summary.mFirstMachine;
        }
        parentObject->log(parentObject, PrintLevel::SUMMARY, message);
    }
    if (disableStabilizers) {
        parentObject->log(parentObject,
                          PrintLevel::SUMMARY,
                          "DYD diagnostic: set zero output gain on " +
                              std::to_string(zeroGainStabilizers) + " stabilizer model records");
    }
}
}  // namespace griddyn
