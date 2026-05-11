/*
 * Copyright (c) 2014-2026, Lawrence Livermore National Security
 * See the top-level NOTICE for additional details. All rights reserved.
 * SPDX-License-Identifier: BSD-3-Clause
 */

#include "optObjectFactory.h"

#include <memory>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace griddyn {
optComponentFactory::optComponentFactory(std::string_view typeName): name(typeName) {}

optComponentFactory::~optComponentFactory() = default;

void optComponentFactory::registerFactory(optFactory* optFac)
{
    auto ret = mFactoryMap.insert(std::pair<std::string, optFactory*>(optFac->name, optFac));
    if (!ret.second) {
        ret.first->second = optFac;
    }
    mFactoryList.push_back(optFac);
}

stringVec optComponentFactory::getObjNames()
{
    stringVec tnames;
    tnames.reserve(mFactoryMap.size());
    for (auto tname : mFactoryMap) {
        tnames.push_back(tname.first);
    }
    return tnames;
}

gridOptObject* optComponentFactory::makeObject()
{
    return nullptr;
}

bool optComponentFactory::isValidObject(std::string_view objName)
{
    auto mfind = mFactoryMap.find(objName);
    return (mfind != mFactoryMap.end());
}

optFactory* optComponentFactory::getFactory(std::string_view typeName)
{
    auto mfind = mFactoryMap.find(typeName);
    if (mfind != mFactoryMap.end()) {
        return mfind->second;
    }
    return nullptr;
}

gridOptObject* optComponentFactory::makeObject(std::string_view objType)
{
    auto mfind = mFactoryMap.find(objType);
    if (mfind != mFactoryMap.end()) {
        return mfind->second->makeObject();
    }
    return nullptr;
}

gridOptObject* optComponentFactory::makeObject(coreObject* obj)
{
    gridOptObject* optObject = nullptr;

    int mxLevel = -1;
    auto ofm = mFactoryList[0];
    for (auto& of : mFactoryList) {
        if (of->mLevel > mxLevel) {
            if (of->testObject(obj)) {
                ofm = of;
                mxLevel = of->mLevel;
            }
        }
    }

    if (mxLevel >= 0) {
        optObject = ofm->makeObject(obj);
        return optObject;
    }
    return nullptr;
}

// create a high level object factory for the coreObject class
std::shared_ptr<coreOptObjectFactory> coreOptObjectFactory::instance()
{
    static const std::shared_ptr<coreOptObjectFactory> factory =
        std::shared_ptr<coreOptObjectFactory>(new coreOptObjectFactory());
    return factory;
}

void coreOptObjectFactory::registerFactory(std::string_view name,
                                           std::shared_ptr<optComponentFactory> componentFac)
{
    mFactoryMap[std::string{name}] = std::move(componentFac);
}

void coreOptObjectFactory::registerFactory(std::shared_ptr<optComponentFactory> componentFac)
{
    if (componentFac != nullptr) {
        const std::string componentName = componentFac->name;
        registerFactory(componentName, std::move(componentFac));
    }
}

stringVec coreOptObjectFactory::getFactoryNames()
{
    stringVec factoryNames;
    factoryNames.reserve(mFactoryMap.size());
    for (auto factoryName : mFactoryMap) {
        factoryNames.push_back(factoryName.first);
    }
    return factoryNames;
}

std::vector<std::string> coreOptObjectFactory::getObjNames(std::string_view factoryName)
{
    auto mfind = mFactoryMap.find(factoryName);
    if (mfind != mFactoryMap.end()) {
        return mfind->second->getObjNames();
    }
    return {};
}

gridOptObject* coreOptObjectFactory::createObject(std::string_view optType,
                                                  std::string_view typeName)
{
    auto mfind = mFactoryMap.find(optType);
    if (mfind != mFactoryMap.end()) {
        return mfind->second->makeObject(typeName);
    }
    return nullptr;
}

gridOptObject* coreOptObjectFactory::createObject(std::string_view optType, coreObject* obj)
{
    auto mfind = mFactoryMap.find(optType);
    if (mfind != mFactoryMap.end()) {
        return mfind->second->makeObject(obj);
    }
    return nullptr;
}

gridOptObject* coreOptObjectFactory::createObject(coreObject* obj)
{
    if (mDefaultType.empty()) {
        return nullptr;
    }
    auto mfind = mFactoryMap.find(mDefaultType);
    if (mfind != mFactoryMap.end()) {
        return mfind->second->makeObject(obj);
    }
    return nullptr;
}

gridOptObject* coreOptObjectFactory::createObject(std::string_view typeName)
{
    if (mDefaultType.empty()) {
        return nullptr;
    }
    auto mfind = mFactoryMap.find(mDefaultType);
    if (mfind != mFactoryMap.end()) {
        return mfind->second->makeObject(typeName);
    }
    return nullptr;
}

std::shared_ptr<optComponentFactory> coreOptObjectFactory::getFactory(std::string_view factoryName)
{
    auto mfind = mFactoryMap.find(factoryName);
    if (mfind != mFactoryMap.end()) {
        return mfind->second;
    }
    // make a new factory
    auto componentFac = std::make_shared<optComponentFactory>();
    componentFac->name = std::string{factoryName};
    mFactoryMap.insert(
        std::pair<std::string, std::shared_ptr<optComponentFactory>>(std::string{factoryName},
                                                                     componentFac));
    return componentFac;
}

bool coreOptObjectFactory::isValidType(std::string_view optType)
{
    auto mfind = mFactoryMap.find(optType);
    return (mfind != mFactoryMap.end());
}

bool coreOptObjectFactory::isValidObject(std::string_view optType, std::string_view objName)
{
    auto mfind = mFactoryMap.find(optType);
    if (mfind != mFactoryMap.end()) {
        return mfind->second->isValidObject(objName);
    }
    return false;
}

void coreOptObjectFactory::setDefaultType(std::string_view defType)
{
    auto mfind = mFactoryMap.find(defType);
    if (mfind != mFactoryMap.end()) {
        mDefaultType = std::string{defType};
    }
}

void coreOptObjectFactory::prepObjects(std::string_view optType,
                                       std::string_view typeName,
                                       count_t numObjects,
                                       coreObject* baseObj)
{
    auto mfind = mFactoryMap.find(optType);
    if (mfind != mFactoryMap.end()) {
        auto* obfact = mfind->second->getFactory(typeName);
        if (obfact != nullptr) {
            obfact->prepObjects(numObjects, baseObj);
        }
    }
}

void coreOptObjectFactory::prepObjects(std::string_view typeName,
                                       count_t numObjects,
                                       coreObject* baseObj)
{
    if (mDefaultType.empty()) {
        return;
    }

    auto* obfact = mFactoryMap[mDefaultType]->getFactory(typeName);
    if (obfact != nullptr) {
        obfact->prepObjects(numObjects, baseObj);
    }
}

}  // namespace griddyn
