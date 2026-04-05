/*
 * Copyright (c) 2014-2020, Lawrence Livermore National Security
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

namespace griddyn {
using namespace units;
using namespace readerConfig;
using namespace gmlc::utilities::string_viewOps;
using namespace gmlc::utilities;

enum mode_state { read_header, read_data };

void loadCSV(coreObject* parentObject,
             const std::string& fileName,
             readerInfo& ri,
             const std::string& oname)
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
    std::vector<unit> units;
    std::string ObjectMode;
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
            headers = stringOps::splitline(line);
            stringOps::trim(headers);
            ObjectMode = headers[0];
            makeLowerCase(ObjectMode);
            // translate a few mode possibilities
            if ((ObjectMode == "branch") || (ObjectMode == "line")) {
                ObjectMode = "link";
            }
            if (!(cof->isValidObject(ObjectMode))) {
                if (!oname.empty()) {
                    if (!(cof->isValidObject(oname))) {
                        ObjectMode = oname;
                    } else {
                        WARNPRINT(READER_WARN_IMPORTANT,
                                  "Unrecognized object " << ObjectMode << " Unable to process CSV");
                        return;
                    }
                } else {
                    WARNPRINT(READER_WARN_IMPORTANT,
                              "Unrecognized object " << ObjectMode << " Unable to process CSV");
                    return;
                }
            }

            units = std::vector<unit>(headers.size(), defunit);
            skipToken.resize(headers.size(), 0);
            typekey = -1;
            int nn = 0;
            for (auto& tk : headers) {
                stringOps::trimString(tk);

                if (tk.empty()) {
                    continue;
                }
                if (tk[0] == '#') {
                    tk.clear();
                    continue;
                }
                makeLowerCase(tk);
                if (tk == "type") {
                    typekey = nn;
                }
                if ((tk == "ref") || (tk == "reference")) {
                    refkey = nn;
                }
                if (tk.back() == ')') {
                    auto p = tk.find_first_of('(');
                    if (p != std::string::npos) {
                        std::string uname = tk.substr(p + 1, tk.length() - 2 - p);
                        units[nn] = unit_cast_from_string(uname);
                        tk = stringOps::trim(tk.substr(0, p));
                    }
                }
                ++nn;
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
                obj = parentObject->findByUserID(ObjectMode, index);
            } else if (index == -2) {
                obj = locateObject(std::string{trim(lineTokens[0])}, parentObject);
            }
            if (refkey >= 0) {
                obj = ri.makeLibraryObject(ref, obj);
            }

            if (obj == nullptr) {
                obj = cof->createObject(ObjectMode, type);
                if (obj != nullptr) {
                    if (index > 0) {
                        obj->setUserID(index);
                    } else if (index == -2) {
                        obj->setName(std::string{lineTokens[0]});
                    }
                }
            }

            if (obj == nullptr) {
                std::cerr << "Line " << lineNumber << "::Unable to create object " << ObjectMode
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
                    if (ObjectMode == "bus") {
                        auto str = trim(lineTokens[kk]);
                        obj->set("type", std::string{str});
                    }
                } else if ((field == "name") || (field == "description")) {
                    auto str = std::string{trim(lineTokens[kk])};
                    str = ri.checkDefines(str);
                    obj->set(field, str);
                } else if ((field.compare(0, 2, "to") == 0) && (ObjectMode == "link")) {
                    auto str = std::string{trim(lineTokens[kk])};

                    str = ri.checkDefines(str);
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
                } else if ((field.compare(0, 4, "from") == 0) && (ObjectMode == "link")) {
                    auto str = ri.checkDefines(std::string{trim(lineTokens[kk])});
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
                } else if ((field == "bus") && ((ObjectMode == "load") || (ObjectMode == "gen"))) {
                    auto str = ri.checkDefines(std::string{lineTokens[kk]});
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
                           (ObjectMode == "relay")) {
                    auto str = ri.checkDefines(std::string{lineTokens[kk]});
                    auto obj2 = locateObject(str, parentObject);
                    if (obj2 != nullptr) {
                        if (field != "sink") {
                            (static_cast<Relay*>(obj))->setSource(obj2);
                        }
                        if (field != "source") {
                            (static_cast<Relay*>(obj))->setSink(obj2);
                        }
                    } else {
                        WARNPRINT(READER_WARN_ALL,
                                  "line " << lineNumber << ":: unable to locate object  " << str);
                    }
                } else if (field == "file") {
                    auto str = std::string{lineTokens[kk]};
                    ri.checkFileParam(str);
                    gridParameter po(field, str);

                    objectParameterSet(std::to_string(lineNumber), obj, po);
                } else if (field == "workdir") {
                    auto str = std::string{lineTokens[kk]};
                    ri.checkDirectoryParam(str);
                    gridParameter po(field, str);

                    objectParameterSet(std::to_string(lineNumber), obj, po);
                } else {
                    auto str = ri.checkDefines(std::string{trim(lineTokens[kk])});
                    auto val = numeric_conversion<double>(str, kBigNum);

                    if (val < kHalfBigNum) {
                        gridParameter po(field, val);
                        po.paramUnits = units[kk];
                        objectParameterSet(std::to_string(lineNumber), obj, po);
                    } else {
                        if (str.empty()) {
                            continue;
                        }
                        gridParameter po(field, str);
                        paramStringProcess(po, ri);
                        auto ret = objectParameterSet(std::to_string(lineNumber), obj, po);

                        if (ret != 0) {
                            skipToken[kk] += 1;
                        }
                    }
                }
            }
            if (obj->isRoot()) {
                if (!(ri.prefix.empty())) {
                    obj->setName(ri.prefix + '_' + obj->getName());
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
