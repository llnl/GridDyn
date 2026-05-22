/*
 * Copyright (c) 2014-2026, Lawrence Livermore National Security
 * See the top-level NOTICE for additional details. All rights reserved.
 * SPDX-License-Identifier: BSD-3-Clause
 */

#include "ObjectFactory.hpp"

#include <memory>
#include <string>
#include <string_view>
#include <utility>
namespace griddyn {
ObjectFactory::ObjectFactory(std::string_view /*component*/, std::string_view typeName):
    name(typeName)
{
}

ObjectFactory::ObjectFactory(std::string_view /*component*/,
                             std::span<const std::string_view> typeNames): name(typeNames[0])
{
}

ObjectFactory::ObjectFactory(std::string_view /*component*/, const stringVec& typeNames):
    name(typeNames[0])
{
}

void ObjectFactory::prepObjects(count_t /*count*/, CoreObject* /*obj*/) {}
count_t ObjectFactory::remainingPrepped() const
{
    return 0;
}
ObjectFactory::~ObjectFactory() = default;

ComponentFactory::ComponentFactory() = default;

ComponentFactory::ComponentFactory(std::string component): name(std::move(component)) {}
ComponentFactory::~ComponentFactory() = default;

void ComponentFactory::registerFactory(std::string_view typeName, ObjectFactory* oFac)
{
    auto ret = m_factoryMap.emplace(typeName, oFac);
    if (!ret.second) {
        ret.first->second = oFac;
    }
}

void ComponentFactory::registerFactory(ObjectFactory* oFac)
{
    registerFactory(oFac->name, oFac);
}
stringVec ComponentFactory::getTypeNames()
{
    stringVec tnames;
    tnames.reserve(m_factoryMap.size());
    for (const auto& tname : m_factoryMap) {
        tnames.push_back(tname.first);
    }
    return tnames;
}

CoreObject* ComponentFactory::makeObject()
{
    if (!mDefaultType.empty()) {
        CoreObject* obj = m_factoryMap[mDefaultType]->makeObject();
        return obj;
    }
    return nullptr;
}

bool ComponentFactory::isValidType(std::string_view typeName) const
{
    auto mfind = m_factoryMap.find(typeName);
    return (mfind != m_factoryMap.end());
}

CoreObject* ComponentFactory::makeObject(std::string_view type)
{
    auto mfind = m_factoryMap.find(type);
    if (mfind != m_factoryMap.end()) {
        CoreObject* obj = mfind->second->makeObject();
        return obj;
    }

    if (!mDefaultType.empty()) {
        CoreObject* obj = m_factoryMap[mDefaultType]->makeObject();
        return obj;
    }

    return nullptr;
}

CoreObject* ComponentFactory::makeObject(std::string_view type, std::string_view objectName)
{
    auto mfind = m_factoryMap.find(type);
    if (mfind != m_factoryMap.end()) {
        CoreObject* obj = mfind->second->makeObject(objectName);
        return obj;
    }

    if (!mDefaultType.empty()) {
        CoreObject* obj = m_factoryMap[mDefaultType]->makeObject(objectName);
        return obj;
    }

    return nullptr;
}

void ComponentFactory::setDefault(std::string_view type)
{
    if (type.empty()) {
        mDefaultType.clear();
        return;
    }
    auto mfind = m_factoryMap.find(type);
    if (mfind != m_factoryMap.end()) {
        mDefaultType = mfind->first;
    }
}

ObjectFactory* ComponentFactory::getFactory(std::string_view typeName)
{
    if (typeName.empty()) {
        return m_factoryMap[mDefaultType];
    }

    auto mfind = m_factoryMap.find(typeName);
    if (mfind != m_factoryMap.end()) {
        return mfind->second;
    }
    return nullptr;
}

// create a high level object factory for the CoreObject class

std::shared_ptr<CoreObjectFactory> CoreObjectFactory::instance()
{
    // can't use make shared since constructor is private
    static const std::shared_ptr<CoreObjectFactory> factory =
        std::shared_ptr<CoreObjectFactory>(new CoreObjectFactory());  // NOLINT
    return factory;
}

void CoreObjectFactory::registerFactory(std::string_view name,
                                        const std::shared_ptr<ComponentFactory>& componentFac)
{
    auto ret = m_factoryMap.emplace(name, componentFac);
    if (!ret.second) {
        ret.first->second = componentFac;
    }
}

void CoreObjectFactory::registerFactory(const std::shared_ptr<ComponentFactory>& componentFac)
{
    auto ret = m_factoryMap.emplace(componentFac->name, componentFac);
    if (!ret.second) {
        ret.first->second = componentFac;
    }
}

stringVec CoreObjectFactory::getFactoryNames()
{
    stringVec factoryNames;
    factoryNames.reserve(m_factoryMap.size());
    for (const auto& factoryName : m_factoryMap) {
        factoryNames.push_back(factoryName.first);
    }
    return factoryNames;
}

stringVec CoreObjectFactory::getTypeNames(std::string_view component)
{
    auto mfind = m_factoryMap.find(component);
    if (mfind != m_factoryMap.end()) {
        return mfind->second->getTypeNames();
    }
    return {};
}

CoreObject* CoreObjectFactory::createObject(std::string_view component)
{
    auto mfind = m_factoryMap.find(component);
    if (mfind != m_factoryMap.end()) {
        CoreObject* obj = mfind->second->makeObject();
        return obj;
    }
    return nullptr;
}

CoreObject* CoreObjectFactory::createObject(std::string_view component, std::string_view typeName)
{
    auto mfind = m_factoryMap.find(component);
    if (mfind != m_factoryMap.end()) {
        CoreObject* obj = mfind->second->makeObject(typeName);
        return obj;
    }
    return nullptr;
}

CoreObject* CoreObjectFactory::createObject(std::string_view component,
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

std::shared_ptr<ComponentFactory> CoreObjectFactory::getFactory(std::string_view component)
{
    auto mfind = m_factoryMap.find(component);
    if (mfind != m_factoryMap.end()) {
        return mfind->second;
    }
    // make a new factory
    auto componentFac = std::make_shared<ComponentFactory>(std::string{component});
    m_factoryMap.emplace(component, componentFac);
    return componentFac;
}

bool CoreObjectFactory::isValidObject(std::string_view component)
{
    auto mfind = m_factoryMap.find(component);
    return (mfind != m_factoryMap.end());
}

bool CoreObjectFactory::isValidType(std::string_view component, std::string_view typeName)
{
    auto mfind = m_factoryMap.find(component);
    if (mfind != m_factoryMap.end()) {
        return mfind->second->isValidType(typeName);
    }
    return false;
}

void CoreObjectFactory::prepObjects(std::string_view component,
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

void CoreObjectFactory::prepObjects(std::string_view component, count_t numObjects, CoreObject* obj)
{
    auto mfind = m_factoryMap.find(component);
    if (mfind != m_factoryMap.end()) {
        auto* obfact = mfind->second->getFactory({});
        obfact->prepObjects(numObjects, obj);
    }
}

CoreObjectFactory::CoreObjectFactory() = default;
CoreObjectFactory::~CoreObjectFactory() = default;

}  // namespace griddyn
