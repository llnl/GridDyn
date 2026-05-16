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
OptComponentFactory::OptComponentFactory(std::string_view typeName): name(typeName) {}

OptComponentFactory::~OptComponentFactory() = default;

void OptComponentFactory::registerFactory(OptFactory* optFac)
{
    auto ret = mFactoryMap.insert(std::pair<std::string, OptFactory*>(optFac->name, optFac));
    if (!ret.second) {
        ret.first->second = optFac;
    }
    mFactoryList.push_back(optFac);
}

stringVec OptComponentFactory::getObjNames()
{
    stringVec tnames;
    tnames.reserve(mFactoryMap.size());
    for (auto tname : mFactoryMap) {
        tnames.push_back(tname.first);
    }
    return tnames;
}

GridOptObject* OptComponentFactory::makeObject()
{
    return nullptr;
}

bool OptComponentFactory::isValidObject(std::string_view objName)
{
    auto mfind = mFactoryMap.find(objName);
    return (mfind != mFactoryMap.end());
}

OptFactory* OptComponentFactory::getFactory(std::string_view typeName)
{
    auto mfind = mFactoryMap.find(typeName);
    if (mfind != mFactoryMap.end()) {
        return mfind->second;
    }
    return nullptr;
}

GridOptObject* OptComponentFactory::makeObject(std::string_view objType)
{
    auto mfind = mFactoryMap.find(objType);
    if (mfind != mFactoryMap.end()) {
        return mfind->second->makeObject();
    }
    return nullptr;
}

GridOptObject* OptComponentFactory::makeObject(coreObject* obj)
{
    GridOptObject* optObject = nullptr;

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
std::shared_ptr<CoreOptObjectFactory> CoreOptObjectFactory::instance()
{
    static const std::shared_ptr<CoreOptObjectFactory> factory =
        std::shared_ptr<CoreOptObjectFactory>(new CoreOptObjectFactory());
    return factory;
}

void CoreOptObjectFactory::registerFactory(std::string_view name,
                                           std::shared_ptr<OptComponentFactory> componentFac)
{
    mFactoryMap[std::string{name}] = std::move(componentFac);
}

void CoreOptObjectFactory::registerFactory(std::shared_ptr<OptComponentFactory> componentFac)
{
    if (componentFac != nullptr) {
        const std::string componentName = componentFac->name;
        registerFactory(componentName, std::move(componentFac));
    }
}

stringVec CoreOptObjectFactory::getFactoryNames()
{
    stringVec factoryNames;
    factoryNames.reserve(mFactoryMap.size());
    for (auto factoryName : mFactoryMap) {
        factoryNames.push_back(factoryName.first);
    }
    return factoryNames;
}

std::vector<std::string> CoreOptObjectFactory::getObjNames(std::string_view factoryName)
{
    auto mfind = mFactoryMap.find(factoryName);
    if (mfind != mFactoryMap.end()) {
        return mfind->second->getObjNames();
    }
    return {};
}

GridOptObject* CoreOptObjectFactory::createObject(std::string_view optType,
                                                  std::string_view typeName)
{
    auto mfind = mFactoryMap.find(optType);
    if (mfind != mFactoryMap.end()) {
        return mfind->second->makeObject(typeName);
    }
    return nullptr;
}

GridOptObject* CoreOptObjectFactory::createObject(std::string_view optType, coreObject* obj)
{
    auto mfind = mFactoryMap.find(optType);
    if (mfind != mFactoryMap.end()) {
        return mfind->second->makeObject(obj);
    }
    return nullptr;
}

GridOptObject* CoreOptObjectFactory::createObject(coreObject* obj)
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

GridOptObject* CoreOptObjectFactory::createObject(std::string_view typeName)
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

std::shared_ptr<OptComponentFactory> CoreOptObjectFactory::getFactory(std::string_view factoryName)
{
    auto mfind = mFactoryMap.find(factoryName);
    if (mfind != mFactoryMap.end()) {
        return mfind->second;
    }
    // make a new factory
    auto componentFac = std::make_shared<OptComponentFactory>();
    componentFac->name = std::string{factoryName};
    mFactoryMap.insert(
        std::pair<std::string, std::shared_ptr<OptComponentFactory>>(std::string{factoryName},
                                                                     componentFac));
    return componentFac;
}

bool CoreOptObjectFactory::isValidType(std::string_view optType)
{
    auto mfind = mFactoryMap.find(optType);
    return (mfind != mFactoryMap.end());
}

bool CoreOptObjectFactory::isValidObject(std::string_view optType, std::string_view objName)
{
    auto mfind = mFactoryMap.find(optType);
    if (mfind != mFactoryMap.end()) {
        return mfind->second->isValidObject(objName);
    }
    return false;
}

void CoreOptObjectFactory::setDefaultType(std::string_view defType)
{
    auto mfind = mFactoryMap.find(defType);
    if (mfind != mFactoryMap.end()) {
        mDefaultType = std::string{defType};
    }
}

void CoreOptObjectFactory::prepObjects(std::string_view optType,
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

void CoreOptObjectFactory::prepObjects(std::string_view typeName,
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
