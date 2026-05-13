/*
 * Copyright (c) 2014-2026, Lawrence Livermore National Security
 * See the top-level NOTICE for additional details. All rights reserved.
 * SPDX-License-Identifier: BSD-3-Clause
 */

#include "core/coreExceptions.h"
#include "core/objectFactory.hpp"
#include "core/objectInterpreter.h"
#include "fileInput.h"
#include "gmlc/utilities/stringOps.h"
#include "gmlc/utilities/string_viewConversion.h"
#include "griddyn/Area.h"
#include "griddyn/Link.h"
#include "griddyn/Relay.h"
#include "griddyn/gridBus.h"
#include "readerHelper.h"
#include <cstdlib>
#include <fstream>
#include <iostream>
#include <string>
#include <vector>

namespace griddyn {
using gmlc::utilities::makeLowerCase;
using gmlc::utilities::numeric_conversion;
using gmlc::utilities::string_viewOps::split;
using gmlc::utilities::string_viewOps::trim;

enum mode_state { read_header, read_data };

void loadCSV(coreObject* parentObject,
             const std::string& fileName,
             readerInfo& readerInformation,
             const std::string& objectName)
{
    auto cof = coreObjectFactory::instance();
    std::ifstream file(fileName, std::ios::in);
    if (!(file.is_open())) {
        std::cerr << "Unable to open file " << fileName << '\n';
        return;
    }
    std::string line;  // line storage
    int lineNumber = 0;
    stringVec headers;
    std::vector<int> skipToken;
    std::vector<units::unit> units;
    std::string objectMode;
    int typekey = -1;
    int refkey = -1;
    std::string field;
    mode_state mState = read_header;

    // loop over the sections
    while (std::getline(file, line)) {
        ++lineNumber;
        if (line[0] == '#') {
            if (line[1] == '#')  // new section
            {
                mState = read_header;
            }
            continue;
        }
        if (mState == read_header) {
            headers = gmlc::utilities::stringOps::splitline(line);
            gmlc::utilities::stringOps::trim(headers);
            objectMode = headers[0];
            makeLowerCase(objectMode);
            // translate a few mode possibilities
            if ((objectMode == "branch") || (objectMode == "line")) {
                objectMode = "link";
            }
            if (!(cof->isValidObject(objectMode))) {
                if (!objectName.empty()) {
                    if (!(cof->isValidObject(objectName))) {
                        objectMode = objectName;
                    } else {
                        WARNPRINT(READER_WARN_IMPORTANT,
                                  "Unrecognized object " << objectMode
                                                         << " Unable to process CSV");
                        return;
                    }
                } else {
                    WARNPRINT(READER_WARN_IMPORTANT,
                              "Unrecognized object " << objectMode << " Unable to process CSV");
                    return;
                }
            }

            units = std::vector<units::unit>(headers.size(), units::defunit);
            skipToken.resize(headers.size(), 0);
            typekey = -1;
            int headerIndex = 0;
            for (auto& headerToken : headers) {
                gmlc::utilities::stringOps::trimString(headerToken);

                if (headerToken.empty()) {
                    continue;
                }
                if (headerToken[0] == '#') {
                    headerToken.clear();
                    continue;
                }
                makeLowerCase(headerToken);
                if (headerToken == "type") {
                    typekey = headerIndex;
                }
                if ((headerToken == "ref") || (headerToken == "reference")) {
                    refkey = headerIndex;
                }
                if (headerToken.back() == ')') {
                    auto unitStartPos = headerToken.find_first_of('(');
                    if (unitStartPos != std::string::npos) {
                        std::string unitName = headerToken.substr(
                            unitStartPos + 1, headerToken.length() - 2 - unitStartPos);
                        units[headerIndex] = units::unit_cast_from_string(unitName);
                        headerToken = gmlc::utilities::stringOps::trim(
                            headerToken.substr(0, unitStartPos));
                    }
                }
                ++headerIndex;
            }

            mState = read_data;
            // skip the reference
            if (refkey > 0) {
                skipToken[refkey] = 4;
            }
        } else {
            auto lineTokens = split(line);
            if (lineTokens.size() != headers.size()) {
                std::cerr << "line " << std::to_string(lineNumber)
                          << " length does not match section header" << std::endl;
                return;
            }
            // check the identifier
            std::string ref = (refkey >= 0) ? std::string{trim(lineTokens[refkey])} : std::string{};
            std::string type =
                (typekey >= 0) ? std::string{trim(lineTokens[typekey])} : std::string{};
            // find or create the object
            auto index = numeric_conversion<int>(lineTokens[0], -2);
            coreObject* obj = nullptr;
            if (index >= 0) {
                obj = parentObject->findByUserID(objectMode, index);
            } else if (index == -2) {
                obj = locateObject(std::string{trim(lineTokens[0])}, parentObject);
            }
            if (refkey >= 0) {
                obj = readerInformation.makeLibraryObject(ref, obj);
            }

            if (obj == nullptr) {
                obj = cof->createObject(objectMode, type);
                if (obj != nullptr) {
                    if (index > 0) {
                        obj->setUserID(index);
                    } else if (index == -2) {
                        obj->setName(std::string{lineTokens[0]});
                    }
                }
            }

            if (obj == nullptr) {
                std::cerr << "Line " << lineNumber << "::Unable to create object " << objectMode
                          << " of Type " << type << '\n';
                return;
            }
            //

            for (size_t kk = 1; kk < lineTokens.size(); kk++) {
                // check if we just skip this one
                if (skipToken[kk] > 2) {
                    continue;
                }
                // make sure there is something in the field
                if (lineTokens[kk].empty()) {
                    continue;
                }

                field = headers[kk];
                if (field.empty()) {
                    skipToken[kk] = 4;
                }
                if (field == "type") {
                    if (objectMode == "bus") {
                        auto str = trim(lineTokens[kk]);
                        obj->set("type", std::string{str});
                    }
                } else if ((field == "name") || (field == "description")) {
                    auto str = std::string{trim(lineTokens[kk])};
                    str = readerInformation.checkDefines(str);
                    obj->set(field, str);
                } else if ((field.compare(0, 2, "to") == 0) && (objectMode == "link")) {
                    auto str = std::string{trim(lineTokens[kk])};

                    str = readerInformation.checkDefines(str);
                    auto val = numeric_conversion<double>(str, kBigNum);
                    gridBus* bus = nullptr;
                    if (val < kHalfBigNum) {
                        bus = static_cast<gridBus*>(
                            parentObject->findByUserID("bus", static_cast<int>(val)));
                    } else {
                        bus = static_cast<gridBus*>(locateObject(str, parentObject));
                    }
                    if (bus != nullptr) {
                        static_cast<Link*>(obj)->updateBus(bus, 2);
                    }
                } else if ((field.compare(0, 4, "from") == 0) && (objectMode == "link")) {
                    auto str =
                        readerInformation.checkDefines(std::string{trim(lineTokens[kk])});
                    auto val = numeric_conversion<double>(str, kBigNum);
                    gridBus* bus = nullptr;
                    if (val < kHalfBigNum) {
                        bus = static_cast<gridBus*>(
                            parentObject->findByUserID("bus", static_cast<int>(val)));
                    } else {
                        bus = static_cast<gridBus*>(locateObject(str, parentObject));
                    }
                    if (bus != nullptr) {
                        static_cast<Link*>(obj)->updateBus(bus, 1);
                    } else {
                        WARNPRINT(READER_WARN_ALL,
                                  "line " << lineNumber << ":: unable to locate bus object  "
                                          << str);
                    }
                } else if ((field == "bus") &&
                           ((objectMode == "load") || (objectMode == "gen"))) {
                    auto str = readerInformation.checkDefines(std::string{lineTokens[kk]});
                    auto val = numeric_conversion<double>(str, kBigNum);
                    gridBus* bus = nullptr;
                    if (val < kHalfBigNum) {
                        bus = static_cast<gridBus*>(
                            parentObject->findByUserID("bus", static_cast<int>(val)));
                    } else {
                        bus = static_cast<gridBus*>(locateObject(str, parentObject));
                    }
                    if (bus != nullptr) {
                        bus->add(obj);
                    } else {
                        WARNPRINT(READER_WARN_ALL,
                                  "line " << lineNumber << ":: unable to locate bus object  "
                                          << str);
                    }
                } else if (((field == "target") || (field == "sink") || (field == "source")) &&
                           (objectMode == "relay")) {
                    auto str = readerInformation.checkDefines(std::string{lineTokens[kk]});
                    auto relatedObject = locateObject(str, parentObject);
                    if (relatedObject != nullptr) {
                        if (field != "sink") {
                            (static_cast<Relay*>(obj))->setSource(relatedObject);
                        }
                        if (field != "source") {
                            (static_cast<Relay*>(obj))->setSink(relatedObject);
                        }
                    } else {
                        WARNPRINT(READER_WARN_ALL,
                                  "line " << lineNumber << ":: unable to locate object  " << str);
                    }
                } else if (field == "file") {
                    auto str = std::string{lineTokens[kk]};
                    readerInformation.checkFileParam(str);
                    gridParameter parameterObject(field, str);

                    objectParameterSet(std::to_string(lineNumber), obj, parameterObject);
                } else if (field == "workdir") {
                    auto str = std::string{lineTokens[kk]};
                    readerInformation.checkDirectoryParam(str);
                    gridParameter parameterObject(field, str);

                    objectParameterSet(std::to_string(lineNumber), obj, parameterObject);
                } else {
                    auto str = readerInformation.checkDefines(std::string{trim(lineTokens[kk])});
                    auto val = numeric_conversion<double>(str, kBigNum);

                    if (val < kHalfBigNum) {
                        gridParameter parameterObject(field, val);
                        parameterObject.paramUnits = units[kk];
                        objectParameterSet(std::to_string(lineNumber), obj, parameterObject);
                    } else {
                        if (str.empty()) {
                            continue;
                        }
                        gridParameter parameterObject(field, str);
                        paramStringProcess(parameterObject, readerInformation);
                        auto result =
                            objectParameterSet(std::to_string(lineNumber), obj, parameterObject);

                        if (result != 0) {
                            skipToken[kk] += 1;
                        }
                    }
                }
            }
            if (obj->isRoot()) {
                if (!(readerInformation.prefix.empty())) {
                    obj->setName(readerInformation.prefix + '_' + obj->getName());
                }
                try {
                    parentObject->add(obj);
                }
                catch (const coreObjectException& uroe) {
                    WARNPRINT(READER_WARN_ALL,
                              "line " << lineNumber << ":: " << uroe.what() << " " << uroe.who());
                }
            }
        }
    }
}

}  // namespace griddyn
