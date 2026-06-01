/*
 * Copyright (c) 2014-2026, Lawrence Livermore National Security
 * See the top-level NOTICE for additional details. All rights reserved.
 * SPDX-License-Identifier: BSD-3-Clause
 */

#include "core/CoreExceptions.h"
#include "core/ObjectFactory.hpp"
#include "core/ObjectInterpreter.h"
#include "fileInput.h"
#include "gmlc/utilities/stringOps.h"
#include "gmlc/utilities/string_viewConversion.h"
#include "griddyn/GridArea.h"
#include "griddyn/GridBus.h"
#include "griddyn/Link.h"
#include "griddyn/Relay.h"
#include "readerHelper.h"
#include <cstdint>
#include <cstdlib>
#include <fstream>
#include <iostream>
#include <memory>
#include <string>
#include <vector>

namespace griddyn {
using gmlc::utilities::makeLowerCase;
using gmlc::utilities::numeric_conversion;
using gmlc::utilities::string_viewOps::split;
using gmlc::utilities::string_viewOps::trim;

namespace {
    enum class ModeState : std::uint8_t { READ_HEADER, READ_DATA };

    struct CsvSectionState {
        stringVec headers;
        std::vector<int> skipToken;
        std::vector<units::unit> units;
        std::string objectMode;
        int typeKey = -1;
        int refKey = -1;
    };

    bool processHeaderLine(const std::string& line,
                           const std::string& objectName,
                           const std::shared_ptr<CoreObjectFactory>& cof,
                           CsvSectionState& sectionState)
    {
        sectionState.headers = gmlc::utilities::stringOps::splitline(line);
        gmlc::utilities::stringOps::trim(sectionState.headers);
        sectionState.objectMode = sectionState.headers[0];
        makeLowerCase(sectionState.objectMode);
        if ((sectionState.objectMode == "branch") || (sectionState.objectMode == "line")) {
            sectionState.objectMode = "link";
        }

        if (!(cof->isValidObject(sectionState.objectMode))) {
            if (!objectName.empty()) {
                if (!(cof->isValidObject(objectName))) {
                    sectionState.objectMode = objectName;
                } else {
                    WARNPRINT(READER_WARN_IMPORTANT,
                              "Unrecognized object " << sectionState.objectMode
                                                     << " Unable to process CSV");
                    return false;
                }
            } else {
                WARNPRINT(READER_WARN_IMPORTANT,
                          "Unrecognized object " << sectionState.objectMode
                                                 << " Unable to process CSV");
                return false;
            }
        }

        sectionState.units =
            std::vector<units::unit>(sectionState.headers.size(), units::defunit);
        sectionState.skipToken.assign(sectionState.headers.size(), 0);
        sectionState.typeKey = -1;
        sectionState.refKey = -1;

        int headerIndex = 0;
        for (auto& headerToken : sectionState.headers) {
            gmlc::utilities::stringOps::trimString(headerToken);
            if (headerToken.empty()) {
                ++headerIndex;
                continue;
            }
            if (headerToken[0] == '#') {
                headerToken.clear();
                ++headerIndex;
                continue;
            }

            makeLowerCase(headerToken);
            if (headerToken == "type") {
                sectionState.typeKey = headerIndex;
            }
            if ((headerToken == "ref") || (headerToken == "reference")) {
                sectionState.refKey = headerIndex;
            }
            if (headerToken.back() == ')') {
                const auto unitStartPos = headerToken.find_first_of('(');
                if (unitStartPos != std::string::npos) {
                    std::string unitName =
                        headerToken.substr(unitStartPos + 1,
                                           headerToken.length() - 2 - unitStartPos);
                    sectionState.units[headerIndex] = units::unit_cast_from_string(unitName);
                    headerToken =
                        gmlc::utilities::stringOps::trim(headerToken.substr(0, unitStartPos));
                }
            }
            ++headerIndex;
        }

        if (sectionState.refKey > 0) {
            sectionState.skipToken[sectionState.refKey] = 4;
        }
        return true;
    }

    GridBus* findBusFromToken(CoreObject* parentObject, const std::string& busToken)
    {
        const auto val = numeric_conversion<double>(busToken, kBigNum);
        if (val < kHalfBigNum) {
            return static_cast<GridBus*>(
                parentObject->findByUserID("bus", static_cast<int>(val)));
        }
        return static_cast<GridBus*>(locateObject(busToken, parentObject));
    }

    CoreObject* findOrCreateObject(CoreObject* parentObject,
                                   const std::shared_ptr<CoreObjectFactory>& cof,
                                   const CsvSectionState& sectionState,
                                   ReaderInfo& readerInformation,
                                   const std::vector<std::string_view>& lineTokens)
    {
        const auto index = numeric_conversion<int>(lineTokens[0], -2);
        CoreObject* obj = nullptr;
        if (index >= 0) {
            obj = parentObject->findByUserID(sectionState.objectMode, index);
        } else if (index == -2) {
            obj = locateObject(std::string{trim(lineTokens[0])}, parentObject);
        }

        if (sectionState.refKey >= 0) {
            const std::string ref =
                std::string{trim(lineTokens[sectionState.refKey])};
            obj = readerInformation.makeLibraryObject(ref, obj);
        }

        if (obj != nullptr) {
            return obj;
        }

        const std::string type =
            (sectionState.typeKey >= 0) ? std::string{trim(lineTokens[sectionState.typeKey])} :
                                          std::string{};
        obj = cof->createObject(sectionState.objectMode, type);
        if (obj == nullptr) {
            return nullptr;
        }

        if (index > 0) {
            obj->setUserID(index);
        } else if (index == -2) {
            obj->setName(std::string{lineTokens[0]});
        }
        return obj;
    }

    void handleNamedField(CoreObject* obj,
                          std::string_view field,
                          std::string_view token,
                          ReaderInfo& readerInformation)
    {
        auto str = std::string{trim(token)};
        str = readerInformation.checkDefines(str);
        obj->set(field, str);
    }

    bool handleLinkBusField(CoreObject* parentObject,
                            CoreObject* obj,
                            const std::string& objectMode,
                            std::string_view field,
                            std::string_view token,
                            ReaderInfo& readerInformation,
                            int lineNumber)
    {
        const bool isToField = field.starts_with("to");
        const bool isFromField = field.starts_with("from");
        if ((!isToField && !isFromField) || (objectMode != "link")) {
            return false;
        }

        auto busToken = readerInformation.checkDefines(std::string{trim(token)});
        auto* bus = findBusFromToken(parentObject, busToken);
        if (bus != nullptr) {
            static_cast<Link*>(obj)->updateBus(bus, isToField ? 2 : 1);
        } else if (isFromField) {
            WARNPRINT(READER_WARN_ALL,
                      "line " << lineNumber << ":: unable to locate bus object  " << busToken);
        }
        return true;
    }

    bool handleParentBusField(CoreObject* parentObject,
                              CoreObject* obj,
                              const std::string& objectMode,
                              std::string_view field,
                              std::string_view token,
                              ReaderInfo& readerInformation,
                              int lineNumber)
    {
        if ((field != "bus") || ((objectMode != "load") && (objectMode != "gen"))) {
            return false;
        }

        const auto busToken = readerInformation.checkDefines(std::string{token});
        auto* bus = findBusFromToken(parentObject, busToken);
        if (bus != nullptr) {
            bus->add(obj);
        } else {
            WARNPRINT(READER_WARN_ALL,
                      "line " << lineNumber << ":: unable to locate bus object  " << busToken);
        }
        return true;
    }

    bool handleRelayField(CoreObject* parentObject,
                          CoreObject* obj,
                          const std::string& objectMode,
                          std::string_view field,
                          std::string_view token,
                          ReaderInfo& readerInformation,
                          int lineNumber)
    {
        if (((field != "target") && (field != "sink") && (field != "source")) ||
            (objectMode != "relay")) {
            return false;
        }

        const auto relatedName = readerInformation.checkDefines(std::string{token});
        auto* relatedObject = locateObject(relatedName, parentObject);
        if (relatedObject != nullptr) {
            if (field != "sink") {
                static_cast<Relay*>(obj)->setSource(relatedObject);
            }
            if (field != "source") {
                static_cast<Relay*>(obj)->setSink(relatedObject);
            }
        } else {
            WARNPRINT(READER_WARN_ALL,
                      "line " << lineNumber << ":: unable to locate object  " << relatedName);
        }
        return true;
    }

    bool handleFilePathField(CoreObject* obj,
                             std::string_view field,
                             std::string_view token,
                             ReaderInfo& readerInformation,
                             int lineNumber)
    {
        if ((field != "file") && (field != "workdir")) {
            return false;
        }

        auto pathValue = std::string{token};
        if (field == "file") {
            readerInformation.checkFileParam(pathValue);
        } else {
            readerInformation.checkDirectoryParam(pathValue);
        }

        GridParameter parameterObject(std::string{field}, pathValue);
        setObjectParameter(std::to_string(lineNumber), obj, parameterObject);
        return true;
    }

    void handleGenericField(CoreObject* obj,
                            std::string_view field,
                            std::string_view token,
                            units::unit unitType,
                            ReaderInfo& readerInformation,
                            int lineNumber,
                            int& skipTokenValue)
    {
        auto str = readerInformation.checkDefines(std::string{trim(token)});
        const auto val = numeric_conversion<double>(str, kBigNum);
        if (val < kHalfBigNum) {
            GridParameter parameterObject(std::string{field}, val);
            parameterObject.paramUnits = unitType;
            setObjectParameter(std::to_string(lineNumber), obj, parameterObject);
            return;
        }

        if (str.empty()) {
            return;
        }

        GridParameter parameterObject(std::string{field}, str);
        processParamString(parameterObject, readerInformation);
        const auto result = setObjectParameter(std::to_string(lineNumber), obj, parameterObject);
        if (result != 0) {
            skipTokenValue += 1;
        }
    }

    void loadObjectFields(CoreObject* parentObject,
                          CoreObject* obj,
                          CsvSectionState& sectionState,
                          const std::vector<std::string_view>& lineTokens,
                          ReaderInfo& readerInformation,
                          int lineNumber)
    {
        for (size_t kk = 1; kk < lineTokens.size(); ++kk) {
            if (sectionState.skipToken[kk] > 2) {
                continue;
            }
            if (lineTokens[kk].empty()) {
                continue;
            }

            auto& skipTokenValue = sectionState.skipToken[kk];
            const auto& field = sectionState.headers[kk];
            if (field.empty()) {
                skipTokenValue = 4;
                continue;
            }

            if (field == "type") {
                if (sectionState.objectMode == "bus") {
                    obj->set("type", std::string{trim(lineTokens[kk])});
                }
                continue;
            }
            if ((field == "name") || (field == "description")) {
                handleNamedField(obj, field, lineTokens[kk], readerInformation);
                continue;
            }
            if (handleLinkBusField(parentObject,
                                   obj,
                                   sectionState.objectMode,
                                   field,
                                   lineTokens[kk],
                                   readerInformation,
                                   lineNumber) ||
                handleParentBusField(parentObject,
                                     obj,
                                     sectionState.objectMode,
                                     field,
                                     lineTokens[kk],
                                     readerInformation,
                                     lineNumber) ||
                handleRelayField(parentObject,
                                 obj,
                                 sectionState.objectMode,
                                 field,
                                 lineTokens[kk],
                                 readerInformation,
                                 lineNumber) ||
                handleFilePathField(
                    obj, field, lineTokens[kk], readerInformation, lineNumber)) {
                continue;
            }

            handleGenericField(obj,
                               field,
                               lineTokens[kk],
                               sectionState.units[kk],
                               readerInformation,
                               lineNumber,
                               skipTokenValue);
        }
    }

    bool finalizeRootObject(CoreObject* parentObject,
                            CoreObject* obj,
                            const ReaderInfo& readerInformation,
                            int lineNumber)
    {
        if (!(obj->isRoot())) {
            return true;
        }

        if (!(readerInformation.prefix.empty())) {
            obj->setName(readerInformation.prefix + '_' + obj->getName());
        }

        try {
            parentObject->add(obj);
        }
        catch (const CoreObjectException& uroe) {
            WARNPRINT(READER_WARN_ALL,
                      "line " << lineNumber << ":: " << uroe.what() << " " << uroe.who());
        }
        return true;
    }
}

void loadCsv(CoreObject* parentObject,
             const std::string& fileName,
             ReaderInfo& readerInformation,
             const std::string& objectName)
{
    auto cof = CoreObjectFactory::instance();
    std::ifstream file(fileName, std::ios::in);
    if (!(file.is_open())) {
        std::cerr << "Unable to open file " << fileName << '\n';
        return;
    }
    std::string line;  // line storage
    int lineNumber = 0;
    CsvSectionState sectionState;
    ModeState mState = ModeState::READ_HEADER;

    // loop over the sections
    while (std::getline(file, line)) {
        ++lineNumber;
        if (line.empty()) {
            continue;
        }
        if (line[0] == '#') {
            if ((line.size() > 1U) && (line[1] == '#'))  // new section
            {
                mState = ModeState::READ_HEADER;
            }
            continue;
        }
        if (mState == ModeState::READ_HEADER) {
            if (!processHeaderLine(line, objectName, cof, sectionState)) {
                return;
            }
            mState = ModeState::READ_DATA;
        } else {
            auto lineTokens = split(line);
            if (lineTokens.size() != sectionState.headers.size()) {
                std::cerr << "line " << std::to_string(lineNumber)
                          << " length does not match section header\n";
                return;
            }
            CoreObject* obj =
                findOrCreateObject(parentObject, cof, sectionState, readerInformation, lineTokens);
            if (obj == nullptr) {
                const std::string type = (sectionState.typeKey >= 0) ?
                    std::string{trim(lineTokens[sectionState.typeKey])} :
                    std::string{};
                std::cerr << "Line " << lineNumber << "::Unable to create object "
                          << sectionState.objectMode << " of Type " << type << '\n';
                return;
            }
            loadObjectFields(
                parentObject, obj, sectionState, lineTokens, readerInformation, lineNumber);
            finalizeRootObject(parentObject, obj, readerInformation, lineNumber);
        }
    }
}
}  // namespace griddyn
