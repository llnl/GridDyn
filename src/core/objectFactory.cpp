/*
 * Copyright (c) 2014-2026, Lawrence Livermore National Security
 * See the top-level NOTICE for additional details. All rights reserved.
 * SPDX-License-Identifier: BSD-3-Clause
 */

#include "objectFactory.hpp"

#include <memory>
#include <string>
#include <string_view>
#include <utility>
namespace griddyn {
objectFactory::objectFactory(std::string_view /*component*/, std::string_view typeName):
    name(typeName)
{
}

objectFactory::objectFactory(std::string_view /*component*/,
                             std::span<const std::string_view> typeNames): name(typeNames[0])
{
}

objectFactory::objectFactory(std::string_view /*component*/, const stringVec& typeNames):
    name(typeNames[0])
{
}

void objectFactory::prepObjects(count_t /*count*/, CoreObject* /*obj*/) {}
count_t objectFactory::remainingPrepped() const
{
    return 0;
}
objectFactory::~objectFactory() = default;

componentFactory::componentFactory() = default;

componentFactory::componentFactory(std::string component): name(std::move(component)) {}
componentFactory::~componentFactory() = default;

void componentFactory::registerFactory(std::string_view typeName, objectFactory* oFac)
{
    auto ret = m_factoryMap.emplace(typeName, oFac);
    if (!ret.second) {
        ret.first->second = oFac;
    }
}

void componentFactory::registerFactory(objectFactory* oFac)
{
    registerFactory(oFac->name, oFac);
}
stringVec componentFactory::getTypeNames()
{
    stringVec tnames;
    tnames.reserve(m_factoryMap.size());
    for (const auto& tname : m_factoryMap) {
        tnames.push_back(tname.first);
    }
    return tnames;
}

CoreObject* componentFactory::makeObject()
{
    if (!m_defaultType.empty()) {
        CoreObject* obj = m_factoryMap[m_defaultType]->makeObject();
        return obj;
    }
    return nullptr;
}

bool componentFactory::isValidType(std::string_view typeName) const
{
    auto mfind = m_factoryMap.find(typeName);
    return (mfind != m_factoryMap.end());
}

CoreObject* componentFactory::makeObject(std::string_view type)
{
    auto mfind = m_factoryMap.find(type);
    if (mfind != m_factoryMap.end()) {
        CoreObject* obj = mfind->second->makeObject();
        return obj;
    }

    if (!m_defaultType.empty()) {
        CoreObject* obj = m_factoryMap[m_defaultType]->makeObject();
        return obj;
    }

    return nullptr;
}

CoreObject* componentFactory::makeObject(std::string_view type, std::string_view objectName)
{
    auto mfind = m_factoryMap.find(type);
    if (mfind != m_factoryMap.end()) {
        CoreObject* obj = mfind->second->makeObject(objectName);
        return obj;
    }

    if (!m_defaultType.empty()) {
        CoreObject* obj = m_factoryMap[m_defaultType]->makeObject(objectName);
        return obj;
    }

    return nullptr;
}

void componentFactory::setDefault(std::string_view type)
{
    if (type.empty()) {
        m_defaultType.clear();
        return;
    }
    auto mfind = m_factoryMap.find(type);
    if (mfind != m_factoryMap.end()) {
        m_defaultType = mfind->first;
    }
}

objectFactory* componentFactory::getFactory(std::string_view typeName)
{
    if (typeName.empty()) {
        return m_factoryMap[m_defaultType];
    }

    auto mfind = m_factoryMap.find(typeName);
    if (mfind != m_factoryMap.end()) {
        return mfind->second;
    }
    return nullptr;
}

// create a high level object factory for the CoreObject class

std::shared_ptr<coreObjectFactory> coreObjectFactory::instance()
{
    // can't use make shared since constructor is private
    static const std::shared_ptr<coreObjectFactory> factory =
        std::shared_ptr<coreObjectFactory>(new coreObjectFactory());  // NOLINT
    return factory;
}

void coreObjectFactory::registerFactory(std::string_view name,
                                        const std::shared_ptr<componentFactory>& componentFac)
{
    auto ret = m_factoryMap.emplace(name, componentFac);
    if (!ret.second) {
        ret.first->second = componentFac;
    }
}

void coreObjectFactory::registerFactory(const std::shared_ptr<componentFactory>& componentFac)
{
    auto ret = m_factoryMap.emplace(componentFac->name, componentFac);
    if (!ret.second) {
        ret.first->second = componentFac;
    }
}

stringVec coreObjectFactory::getFactoryNames()
{
    stringVec factoryNames;
    factoryNames.reserve(m_factoryMap.size());
    for (const auto& factoryName : m_factoryMap) {
        factoryNames.push_back(factoryName.first);
    }
    return factoryNames;
}

stringVec coreObjectFactory::getTypeNames(std::string_view component)
{
    auto mfind = m_factoryMap.find(component);
    if (mfind != m_factoryMap.end()) {
        return mfind->second->getTypeNames();
    }
    return {};
}

CoreObject* coreObjectFactory::createObject(std::string_view component)
{
    auto mfind = m_factoryMap.find(component);
    if (mfind != m_factoryMap.end()) {
        CoreObject* obj = mfind->second->makeObject();
        return obj;
    }
    return nullptr;
}

CoreObject* coreObjectFactory::createObject(std::string_view component, std::string_view typeName)
{
    auto mfind = m_factoryMap.find(component);
    if (mfind != m_factoryMap.end()) {
        CoreObject* obj = mfind->second->makeObject(typeName);
        return obj;
    }
    return nullptr;
}

CoreObject* coreObjectFactory::createObject(std::string_view component,
                                            std::string_view typeName,
                                            std::string_view objName)
{
    auto mfind = m_factoryMap.find(component);
    if (mfind != m_factoryMap.end()) {
        CoreObject* obj = mfind->second->makeObject(typeName, objName);
        return obj;
    }
    return nullptr;
}

std::shared_ptr<componentFactory> coreObjectFactory::getFactory(std::string_view component)
{
    auto mfind = m_factoryMap.find(component);
    if (mfind != m_factoryMap.end()) {
        return mfind->second;
    }
    // make a new factory
    auto componentFac = std::make_shared<componentFactory>(std::string{component});
    m_factoryMap.emplace(component, componentFac);
    return componentFac;
}

bool coreObjectFactory::isValidObject(std::string_view component)
{
    auto mfind = m_factoryMap.find(component);
    return (mfind != m_factoryMap.end());
}

bool coreObjectFactory::isValidType(std::string_view component, std::string_view typeName)
{
    auto mfind = m_factoryMap.find(component);
    if (mfind != m_factoryMap.end()) {
        return mfind->second->isValidType(typeName);
    }
    return false;
}

void coreObjectFactory::prepObjects(std::string_view component,
                                    std::string_view typeName,
                                    count_t numObjects,
                                    CoreObject* obj)
{
    auto mfind = m_factoryMap.find(component);
    if (mfind != m_factoryMap.end()) {
        auto* obfact = mfind->second->getFactory(typeName);
        if (obfact != nullptr) {
            obfact->prepObjects(numObjects, obj);
        }
    }
}

void coreObjectFactory::prepObjects(std::string_view component, count_t numObjects, CoreObject* obj)
{
    auto mfind = m_factoryMap.find(component);
    if (mfind != m_factoryMap.end()) {
        auto* obfact = mfind->second->getFactory({});
        obfact->prepObjects(numObjects, obj);
    }
}

coreObjectFactory::coreObjectFactory() = default;
coreObjectFactory::~coreObjectFactory() = default;

}  // namespace griddyn
