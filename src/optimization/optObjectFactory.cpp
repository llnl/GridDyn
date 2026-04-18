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
    auto ret = m_factoryMap.insert(std::pair<std::string, optFactory*>(optFac->name, optFac));
    if (!ret.second) {
        ret.first->second = optFac;
    }
    m_factoryList.push_back(optFac);
}

stringVec optComponentFactory::getObjNames()
{
    stringVec tnames;
    tnames.reserve(m_factoryMap.size());
    for (auto tname : m_factoryMap) {
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
    auto mfind = m_factoryMap.find(objName);
    return (mfind != m_factoryMap.end());
}

optFactory* optComponentFactory::getFactory(std::string_view typeName)
{
    auto mfind = m_factoryMap.find(typeName);
    if (mfind != m_factoryMap.end()) {
        return mfind->second;
    }
    return nullptr;
}

gridOptObject* optComponentFactory::makeObject(std::string_view objType)
{
    auto mfind = m_factoryMap.find(objType);
    if (mfind != m_factoryMap.end()) {
        return mfind->second->makeObject();
    }
    return nullptr;
}

gridOptObject* optComponentFactory::makeObject(coreObject* obj)
{
    gridOptObject* oo;

    int mxLevel = -1;
    auto ofm = m_factoryList[0];
    for (auto& of : m_factoryList) {
        if (of->m_level > mxLevel) {
            if (of->testObject(obj)) {
                ofm = of;
                mxLevel = of->m_level;
            }
        }
    }

    if (mxLevel >= 0) {
        oo = ofm->makeObject(obj);
        return oo;
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
    m_factoryMap[std::string{name}] = std::move(componentFac);
}

void coreOptObjectFactory::registerFactory(std::shared_ptr<optComponentFactory> componentFac)
{
    registerFactory(componentFac->name, std::move(componentFac));
}

stringVec coreOptObjectFactory::getFactoryNames()
{
    stringVec factoryNames;
    factoryNames.reserve(m_factoryMap.size());
    for (auto factoryName : m_factoryMap) {
        factoryNames.push_back(factoryName.first);
    }
    return factoryNames;
}

std::vector<std::string> coreOptObjectFactory::getObjNames(std::string_view factoryName)
{
    auto mfind = m_factoryMap.find(factoryName);
    if (mfind != m_factoryMap.end()) {
        return mfind->second->getObjNames();
    }
    return {};
}

gridOptObject* coreOptObjectFactory::createObject(std::string_view optType,
                                                  std::string_view typeName)
{
    auto mfind = m_factoryMap.find(optType);
    if (mfind != m_factoryMap.end()) {
        return mfind->second->makeObject(typeName);
    }
    return nullptr;
}

gridOptObject* coreOptObjectFactory::createObject(std::string_view optType, coreObject* obj)
{
    auto mfind = m_factoryMap.find(optType);
    if (mfind != m_factoryMap.end()) {
        return mfind->second->makeObject(obj);
    }
    return nullptr;
}

gridOptObject* coreOptObjectFactory::createObject(coreObject* obj)
{
    if (m_defaultType.empty()) {
        return nullptr;
    }
    auto mfind = m_factoryMap.find(m_defaultType);
    if (mfind != m_factoryMap.end()) {
        return mfind->second->makeObject(obj);
    }
    return nullptr;
}

gridOptObject* coreOptObjectFactory::createObject(std::string_view typeName)
{
    if (m_defaultType.empty()) {
        return nullptr;
    }
    auto mfind = m_factoryMap.find(m_defaultType);
    if (mfind != m_factoryMap.end()) {
        return mfind->second->makeObject(typeName);
    }
    return nullptr;
}

std::shared_ptr<optComponentFactory> coreOptObjectFactory::getFactory(std::string_view factoryName)
{
    auto mfind = m_factoryMap.find(factoryName);
    if (mfind != m_factoryMap.end()) {
        return mfind->second;
    }
    // make a new factory
    auto componentFac = std::make_shared<optComponentFactory>();
    componentFac->name = std::string{factoryName};
    m_factoryMap.insert(
        std::pair<std::string, std::shared_ptr<optComponentFactory>>(std::string{factoryName},
                                                                     componentFac));
    return componentFac;
}

bool coreOptObjectFactory::isValidType(std::string_view optType)
{
    auto mfind = m_factoryMap.find(optType);
    return (mfind != m_factoryMap.end());
}

bool coreOptObjectFactory::isValidObject(std::string_view optType, std::string_view objName)
{
    auto mfind = m_factoryMap.find(optType);
    if (mfind != m_factoryMap.end()) {
        return mfind->second->isValidObject(objName);
    }
    return false;
}

void coreOptObjectFactory::setDefaultType(std::string_view defType)
{
    auto mfind = m_factoryMap.find(defType);
    if (mfind != m_factoryMap.end()) {
        m_defaultType = std::string{defType};
    }
}

void coreOptObjectFactory::prepObjects(std::string_view optType,
                                       std::string_view typeName,
                                       count_t numObjects,
                                       coreObject* baseObj)
{
    auto mfind = m_factoryMap.find(optType);
    if (mfind != m_factoryMap.end()) {
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
    if (m_defaultType.empty()) {
        return;
    }

    auto* obfact = m_factoryMap[m_defaultType]->getFactory(typeName);
    if (obfact != nullptr) {
        obfact->prepObjects(numObjects, baseObj);
    }
}

}  // namespace griddyn
