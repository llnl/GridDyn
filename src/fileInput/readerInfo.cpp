/*
 * Copyright (c) 2014-2026, Lawrence Livermore National Security
 * See the top-level NOTICE for additional details. All rights reserved.
 * SPDX-License-Identifier: BSD-3-Clause
 */

#include "readerInfo.h"

#include "core/coreObject.h"
#include "fileInput.h"
#include "formatInterpreters/readerElement.h"
#include "griddyn/measurement/collector.h"
#include "readerHelper.h"
#include <chrono>
#include <cmath>
#include <ctime>
#include <filesystem>
#include <iomanip>
#include <iostream>
#include <memory>
#include <sstream>
#include <string>
#include <string_view>
#include <utility>
#include <vector>
namespace griddyn {

namespace {
    std::tm makeUtcTm(std::time_t timeValue)
    {
        std::tm timeInfo{};
#ifdef _WIN32
        gmtime_s(&timeInfo, &timeValue);
#else
        gmtime_r(&timeValue, &timeInfo);
#endif
        return timeInfo;
    }

    std::tm makeLocalTm(std::time_t timeValue)
    {
        std::tm timeInfo{};
#ifdef _WIN32
        localtime_s(&timeInfo, &timeValue);
#else
        localtime_r(&timeValue, &timeInfo);
#endif
        return timeInfo;
    }

    template<class Duration>
    std::int64_t fractionalMicros(const Duration& timePointDuration)
    {
        return std::chrono::duration_cast<std::chrono::microseconds>(timePointDuration %
                                                                     std::chrono::seconds(1))
            .count();
    }
}  // namespace

void basicReaderInfo::setFlag(uint32_t flagID)
{
    if (flagID < 32) {
        flags |= (std::uint32_t{1} << flagID);
    }
}

readerInfo::readerInfo()
{
    loadDefaultDefines();
}
readerInfo::readerInfo(basicReaderInfo const& bri): basicReaderInfo(bri) {}
readerInfo::~readerInfo()
{
    for (auto& libObj : library) {
        delete libObj.second.first;
    }
}

readerInfo::scopeID readerInfo::newScope()
{
    if (keepdefines) {
        return 0;
    }
    ++currentScope;
    return currentScope;
}

void readerInfo::closeScope(scopeID scopeToClose)
{
    if (scopeToClose == 0) {
        if (currentScope > 0) {
            --currentScope;
        }
    } else {
        currentScope = scopeToClose - 1;
    }
    if (!scopedDefinitions.empty()) {
        while (std::get<0>(scopedDefinitions.back()) > currentScope) {
            if (std::get<2>(scopedDefinitions.back())) {
                defines[std::get<1>(scopedDefinitions.back())] =
                    std::get<3>(scopedDefinitions.back());
            } else {
                defines.erase(std::get<1>(scopedDefinitions.back()));
            }
            scopedDefinitions.pop_back();
            if (scopedDefinitions.empty()) {
                break;
            }
        }
    }
    // now deal with scoped directory definitions
    if (!directoryScope.empty()) {
        while (directoryScope.back() > currentScope) {
            directories.pop_back();
            directoryScope.pop_back();
            if (directoryScope.empty()) {
                break;
            }
        }
    }
}

void readerInfo::addDefinition(const std::string& def, const std::string& replacement)
{
    auto search = lockDefines.find(def);
    if (search == lockDefines.end()) {
        if (currentScope > 0) {
            auto previousDefinition = defines.find(def);
            if (previousDefinition != defines.end()) {
                scopedDefinitions.emplace_back(
                    currentScope, def, true, previousDefinition->second);
            } else {
                scopedDefinitions.emplace_back(currentScope, def, false, "");
            }
        }
        defines[def] = replacement;
    }
}

void readerInfo::addTranslate(const std::string& def, const std::string& component)
{
    objectTranslations[def] = component;
    parameterIgnoreStrings.insert(def);
}

void readerInfo::addTranslate(const std::string& def,
                              const std::string& component,
                              const std::string& type)
{
    objectTranslations[def] = component;
    objectTranslationsType[def] = type;
    parameterIgnoreStrings.insert(def);
}

void readerInfo::addTranslateType(const std::string& def, const std::string& type)
{
    objectTranslationsType[def] = type;
}

void readerInfo::addLockedDefinition(const std::string& def, const std::string& replacement)
{
    auto search = lockDefines.find(def);
    if (search == lockDefines.end()) {
        defines[def] = replacement;
        lockDefines[def] = replacement;
    }
}

void readerInfo::replaceLockedDefinition(const std::string& def, const std::string& replacement)
{
    defines[def] = replacement;
    lockDefines[def] = replacement;
}

std::string readerInfo::checkDefines(const std::string& input)
{
    std::string out = input;
    int replacementCount = 0;
    bool rep(true);
    while (rep) {
        rep = false;
        ++replacementCount;
        if (replacementCount > 15) {
            WARNPRINT(READER_WARN_IMPORTANT,
                      "probable definition recursion in " << input << "currently " << out);
            continue;
        }
        auto search = defines.find(out);
        if (search != defines.end()) {
            out = search->second;
            rep = true;
            continue;  // continue outer loop
        }
        auto pos1 = out.find_first_of('$');
        while (pos1 != std::string::npos) {
            auto pos2 = out.find_first_of('$', pos1 + 1);
            if (pos2 != std::string::npos) {
                auto temp = out.substr(pos1 + 1, pos2 - pos1 - 1);
                search = defines.find(temp);
                if (search != defines.end()) {
                    // out = out.substr (0, pos1) + search->second + out.substr (pos2 + 1);
                    out.replace(pos1, pos2 - pos1 + 1, search->second);
                    rep = true;
                    break;  // break out of inner loop
                }
                // try a recursive interpretation of the string block to convert a numeric result
                // back into a string
                const double val = interpretString(temp, *this);
                if (!std::isnan(val)) {
                    if (std::abs(trunc(val) - val) < 1e-9) {
                        // out = out.substr (0, pos1) + std::to_string (static_cast<int> (val)) +
                        // out.substr (pos2 + 1);
                        out.replace(pos1, pos2 - pos1 + 1, std::to_string(static_cast<int>(val)));
                    } else {
                        // out = out.substr (0, pos1) + std::to_string (val) + out.substr (pos2 +
                        // 1);
                        auto str = std::to_string(val);
                        while (str.back() == '0')  // remove trailing zeros
                        {
                            str.pop_back();
                        }
                        out.replace(pos1, pos2 - pos1 + 1, str);
                    }
                    rep = true;
                    break;  // break out of inner loop
                }
                pos1 = pos2;
            } else {
                break;
            }
        }
    }
    return out;
}

std::string readerInfo::objectNameTranslate(const std::string& input)
{
    std::string out = input;
    int replacementCount = 0;
    bool rep(true);
    while (rep) {
        ++replacementCount;
        if (replacementCount > 15) {
            WARNPRINT(READER_WARN_IMPORTANT,
                      "probable Translation recursion in " << input << "currently " << out);
            rep = false;
        }
        auto search = objectTranslations.find(out);
        if (search != objectTranslations.end()) {
            out = search->second;
            continue;
        }
        rep = false;
    }
    return out;
}

bool readerInfo::addLibraryObject(coreObject* obj, std::vector<gridParameter>& pobjs)
{
    auto retval = library.find(obj->getName());
    if (retval == library.end()) {
        library[obj->getName()] = std::make_pair(obj, pobjs);
        obj->setName(obj->getName() + "_$");  // make sure all cloned object have a unique name
        return true;
    }
    return false;
}

coreObject* readerInfo::findLibraryObject(std::string_view objName) const
{
    auto retval = library.find(std::string{objName});
    if (retval != library.end()) {
        return retval->second.first;
    }
    return nullptr;
}

const char libraryLabel[] = "library";
coreObject* readerInfo::makeLibraryObject(std::string_view objName, coreObject* mobj)
{
    auto objloc = library.find(std::string{objName});
    if (objloc == library.end()) {
        WARNPRINT(READER_WARN_ALL, "unknown reference object " << objName);
        return mobj;
    }

    coreObject* obj = objloc->second.first->clone(mobj);
    for (auto& paramObj : objloc->second.second) {
        paramStringProcess(paramObj, *this);
        objectParameterSet(libraryLabel, obj, paramObj);
    }
    obj->updateName();
    return obj;
}

void readerInfo::loadDefaultDefines()
{
    std::ostringstream ss1;
    std::ostringstream ss2;
    std::ostringstream ss3;
    const auto now = std::chrono::system_clock::now();
    const auto nowTime = std::chrono::system_clock::to_time_t(now);
    const auto utcTime = makeUtcTm(nowTime);
    const auto localTime = makeLocalTm(nowTime);

    ss1.imbue(std::cout.getloc());
    ss1 << std::put_time(&utcTime, "%Y%m%d");

    addDefinition("%date", ss1.str());

    ss2.imbue(std::cout.getloc());
    ss2 << std::put_time(&utcTime, "%Y%m%dT%H%M%S") << '.' << std::setw(6) << std::setfill('0')
        << fractionalMicros(now.time_since_epoch());

    addDefinition("%datetime", ss2.str());

    ss3.imbue(std::cout.getloc());
    ss3 << std::put_time(&localTime, "%H%M%S") << '.' << std::setw(6) << std::setfill('0')
        << fractionalMicros(now.time_since_epoch());

    addDefinition("%time", ss3.str());
    addDefinition("inf", std::to_string(kBigNum));
}

void readerInfo::addDirectory(const std::string& directory)
{
    directories.push_back(directory);
    if (currentScope > 0) {
        directoryScope.push_back(currentScope);
    }
}

std::shared_ptr<collector> readerInfo::findCollector(std::string_view name,
                                                     std::string_view fileName)
{
    for (auto& col : collectors) {
        if ((name.empty()) || (col->getName() == name)) {
            if ((fileName.empty()) || (col->getSinkName().empty()) ||
                (col->getSinkName() == fileName)) {
                return col;
            }
        }
    }
    return nullptr;
}

bool readerInfo::checkFileParam(std::string& strVal, bool extra_find)
{
    if (strVal.back() == '_')  // escape hatch to skip the file checking
    {
        strVal.pop_back();
        return false;
    }
    strVal = checkDefines(strVal);
    std::filesystem::path sourcePath(strVal);
    bool foundPath = false;
    if (sourcePath.has_relative_path()) {
        // check the most recently added directories first
        // NOLINTNEXTLINE(modernize-loop-convert)
        for (auto checkDirectory = directories.rbegin(); checkDirectory != directories.rend();
             ++checkDirectory) {
            auto qpath = std::filesystem::path(*checkDirectory);
            auto tempPath = (qpath.has_root_path()) ?
                qpath / sourcePath :
                std::filesystem::current_path() / qpath / sourcePath;

            if (std::filesystem::exists(tempPath)) {
                sourcePath = tempPath;
                foundPath = true;
                break;
            }
        }
        if (!foundPath && extra_find) {
            // check the most recently added directories first
            // NOLINTNEXTLINE(modernize-loop-convert)
            for (auto checkDirectory = directories.rbegin(); checkDirectory != directories.rend();
                 ++checkDirectory) {
                auto qpath = std::filesystem::path(*checkDirectory);
                auto tempPath = (qpath.has_root_path()) ?
                    qpath / sourcePath.filename() :
                    std::filesystem::current_path() / qpath / sourcePath.filename();

                if (std::filesystem::exists(tempPath)) {
                    sourcePath = tempPath;
                    WARNPRINT(READER_WARN_ALL,
                              "file location path change " << strVal << " mapped to "
                                                           << sourcePath.string());
                    foundPath = true;
                    break;
                }
            }
        }
        if (!foundPath) {
            if (std::filesystem::exists(sourcePath)) {
                foundPath = true;
            }
        }
        strVal = sourcePath.string();
    } else {
        if (std::filesystem::exists(sourcePath)) {
            foundPath = true;
        }
    }
    // if for some reason we need to capture the files
    if (captureFiles) {
        if (foundPath) {
            capturedFiles.push_back(strVal);
        }
    }
    return foundPath;
}

bool readerInfo::checkDirectoryParam(std::string& strVal)
{
    strVal = checkDefines(strVal);
    std::filesystem::path sourcePath(strVal);
    bool foundDirectory = false;
    if (sourcePath.has_relative_path()) {
        // NOLINTNEXTLINE(modernize-loop-convert)
        for (auto checkDirectory = directories.rbegin(); checkDirectory != directories.rend();
             ++checkDirectory) {
            auto qpath = std::filesystem::path(*checkDirectory);
            auto tempPath = (qpath.has_root_path()) ?
                qpath / sourcePath :
                std::filesystem::current_path() / qpath / sourcePath;

            if (std::filesystem::exists(tempPath)) {
                sourcePath = tempPath;
                foundDirectory = true;
                break;
            }
        }

        strVal = sourcePath.string();
    }

    return foundDirectory;
}

// a reader info thing that requires element class information
void readerInfo::addCustomElement(const std::string& name,
                                  const std::shared_ptr<readerElement>& element,
                                  int nargs)
{
    customElements[name] = std::make_pair(element->clone(), nargs);
    parameterIgnoreStrings.insert(name);
}

bool readerInfo::isCustomElement(const std::string& name) const
{
    auto retval = customElements.find(name);
    return (retval != customElements.end());
}

std::pair<std::shared_ptr<readerElement>, int>
    readerInfo::getCustomElement(const std::string& name) const
{
    auto retval = customElements.find(name);
    return retval->second;
}

const ignoreListType& readerInfo::getIgnoreList() const
{
    return parameterIgnoreStrings;
}
}  // namespace griddyn
