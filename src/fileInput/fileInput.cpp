/*
 * Copyright (c) 2014-2026, Lawrence Livermore National Security
 * See the top-level NOTICE for additional details. All rights reserved.
 * SPDX-License-Identifier: BSD-3-Clause
 */

#include "fileInput.h"

#include "core/coreExceptions.h"
#include "formatInterpreters/XmlReaderElement.h"
#include "formatInterpreters/jsonReaderElement.h"
#include "formatInterpreters/yamlReaderElement.h"
#include "gmlc/utilities/stringOps.h"
#include "griddyn/gridDynSimulation.h"
#include "readElement.h"
#include "readElementFile.h"
#include <array>
#include <filesystem>
#include <functional>
#include <memory>
#include <string>
namespace griddyn {
namespace readerConfig {
    int printMode = READER_DEFAULT_PRINT;
    int warnMode = READER_WARN_ALL;
    int warnCount = 0;

    MatchType defMatchType = MatchType::CAPITAL_CASE_MATCH;

    void setPrintMode(int level)
    {
        printMode = level;
    }
#define READER_VERBOSE_PRINT 3
#define READER_NORMAL_PRINT 2
#define READER_SUMMARY_PRINT 1
#define READER_NO_PRINT 0

#define READER_WARN_ALL 2
#define READER_WARN_IMPORTANT 1
#define READER_WARN_NONE 0

    void setPrintMode(const std::string& level)
    {
        if ((level == "0") || (level == "none")) {
            printMode = READER_NO_PRINT;
        } else if ((level == "1") || (level == "summary")) {
            printMode = READER_SUMMARY_PRINT;
        } else if ((level == "2") || (level == "normal")) {
            printMode = READER_NORMAL_PRINT;
        } else if ((level == "3") || (level == "verbose")) {
            printMode = READER_VERBOSE_PRINT;
        } else {
            WARNPRINT(READER_WARN_IMPORTANT, "invalid printMode");
        }
    }

    void setWarnMode(int level)
    {
        warnMode = level;
    }

    void setWarnMode(const std::string& level)
    {
        if ((level == "0") || (level == "none")) {
            printMode = READER_WARN_NONE;
        } else if ((level == "1") || (level == "important")) {
            printMode = READER_WARN_IMPORTANT;
        } else if ((level == "2") || (level == "normal")) {
            printMode = READER_WARN_ALL;
        } else {
            WARNPRINT(READER_WARN_IMPORTANT, "invalid warning level specification");
        }
    }

    void setDefaultMatchType(const std::string& matchType)
    {
        if ((matchType == "exact") || (matchType == "strict")) {
            defMatchType = MatchType::STRICT_CASE_MATCH;
        } else if ((matchType == "capital") || (matchType == "caps")) {
            defMatchType = MatchType::CAPITAL_CASE_MATCH;
        } else if ((matchType == "any") || (matchType == "all")) {
            defMatchType = MatchType::ANY_CASE_MATCH;
        }
    }

}  // namespace readerConfig

int setObjectParameter(const std::string& label, CoreObject* obj, gridParameter& param) noexcept
{
    try {
        if (param.stringType) {
            LEVELPRINT(READER_VERBOSE_PRINT,
                       label << ":setting " << obj->getName() << ' ' << param.field << " to "
                             << param.strVal);
            obj->set(param.field, param.strVal);
        } else {
            LEVELPRINT(READER_VERBOSE_PRINT,
                       label << ":setting " << obj->getName() << ' ' << param.field << " to "
                             << param.value);
            obj->set(param.field, param.value, param.paramUnits);
        }
        return 0;
    }
    catch (const unrecognizedParameter&) {
        WARNPRINT(READER_WARN_ALL, "unrecognized " << label << "  parameter " << param.field);
    }
    catch (const invalidParameterValue&) {
        if (param.stringType) {
            WARNPRINT(READER_WARN_ALL,
                      "value for parameter " << param.field << " (" << param.strVal
                                             << ") is invalid");
        } else {
            WARNPRINT(READER_WARN_ALL,
                      "value for parameter " << param.field << " (" << param.value
                                             << ") is invalid");
        }
    }
    catch (...) {
        WARNPRINT(READER_WARN_ALL, "unknown error when setting " << param.field);
    }
    return (-1);
}

static constexpr std::array<std::pair<std::string_view, int>, 4> flagStringMap{{
    {"ignore_step_up_transformers", IGNORE_STEP_UP_TRANSFORMER},
    {"powerflow_only", ASSUME_POWERFLOW_ONLY},
    {"no_generator_bus_reset", NO_GENERATOR_BUS_VOLTAGE_RESET},
    {"no_generator_bus_voltage_reset", NO_GENERATOR_BUS_VOLTAGE_RESET},
}};

void addFlags(basicReaderInfo& bri, const std::string& flags)
{
    auto flagStrings = gmlc::utilities::stringOps::splitline(flags);
    gmlc::utilities::stringOps::trim(flagStrings);
    for (auto& flag : flagStrings) {
        const auto loweredFlag = gmlc::utilities::convertToLowerCase(flag);
        for (const auto& [name, value] : flagStringMap) {
            if (name == loweredFlag) {
                bri.setFlag(value);
                break;
            }
        }
    }
}

void loadFile(std::unique_ptr<gridDynSimulation>& gds,
              const std::string& fileName,
              readerInfo* readerInf,
              const std::string& ext)
{
    loadFile(gds.get(), fileName, readerInf, ext);
}

std::unique_ptr<gridDynSimulation> readSimXMLFile(const std::string& fileName,
                                                  readerInfo* readerInfoPtr)
{
    if (!std::filesystem::exists(fileName)) {
        return nullptr;
    }
    return std::unique_ptr<gridDynSimulation>(static_cast<gridDynSimulation*>(
        loadElementFile<XmlReaderElement>(nullptr, fileName, readerInfoPtr)));
}

void loadFile(CoreObject* parentObject,
              const std::string& fileName,
              readerInfo* readerInf,
              std::string ext)
{
    if (ext.empty()) {
        const std::filesystem::path sourcePath(fileName);
        ext = gmlc::utilities::convertToLowerCase(sourcePath.extension().string());
        if (ext[0] == '.') {
            ext.erase(0, 1);
        }
    }

    const std::unique_ptr<readerInfo> uniqueReaderInfo =
        (readerInf != nullptr) ? nullptr : std::make_unique<readerInfo>();
    if (readerInf == nullptr) {
        readerInf = uniqueReaderInfo.get();
    }

    // get rid of the . on the extension if it has one

    if (ext == "xml") {
        loadElementFile<XmlReaderElement>(parentObject, fileName, readerInf);
    } else if (ext == "csv") {
        loadCsv(parentObject, fileName, *readerInf);
    } else if (ext == "raw" || ext == "psse" || ext == "pss/e" || ext == "pti") {
        loadRaw(parentObject, fileName, *readerInf);
    } else if (ext == "dyr") {
        loadDyr(parentObject, fileName, *readerInf);
    } else if ((ext == "cdf") || (ext == "txt")) {
        loadCdf(parentObject, fileName, *readerInf);
    } else if (ext == "uct") {
    } else if (ext == "m" || ext == "matlab") {
        loadMatlabFile(parentObject, fileName, *readerInf);
    } else if (ext == "psp") {
        loadPsp(parentObject, fileName, *readerInf);
    } else if (ext == "epc") {
        loadEpc(parentObject, fileName, *readerInf);
    } else if (ext == "json") {
        loadElementFile<jsonReaderElement>(parentObject, fileName, readerInf);
    } else if ((ext == "yaml") || (ext == "yml")) {
        loadElementFile<yamlReaderElement>(parentObject, fileName, readerInf);
    } else if (ext == "gdz") {  // gridDyn Zipped file
        loadGdz(parentObject, fileName, *readerInf);
    }
}

void addToParent(CoreObject* objectToAdd, CoreObject* parentObject)
{
    try {
        parentObject->add(objectToAdd);
    }
    catch (const unrecognizedObjectException&) {
        WARNPRINT(READER_WARN_IMPORTANT,
                  "Object " << objectToAdd->getName() << " not recognized by "
                            << parentObject->getName());
    }
    catch (const objectAddFailure&) {
        WARNPRINT(READER_WARN_IMPORTANT,
                  "Failure to add " << objectToAdd->getName() << " to " << parentObject->getName());
    }
}

// if multiple object with the same name may have been added (parallel transmission lines,
// generators, etc) sequence through the count to find one that hasn't been used then rename the
// object and add it.
void addToParentWithRename(CoreObject* objectToAdd, CoreObject* parentObject)
{
    const std::string bname = objectToAdd->getName();
    int cnt = 2;
    auto* fndObject = parentObject->find(bname + '-' + std::to_string(cnt));
    while (fndObject != nullptr) {
        ++cnt;
        fndObject = parentObject->find(bname + '-' + std::to_string(cnt));
    }
    objectToAdd->setName(bname + '-' + std::to_string(cnt));
    addToParent(objectToAdd, parentObject);
}

}  // namespace griddyn
