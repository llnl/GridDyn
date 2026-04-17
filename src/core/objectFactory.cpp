/*
 * Copyright (c) 2014-2026, Lawrence Livermore National Security
 * See the top-level NOTICE for additional details. All rights reserved.
 * SPDX-License-Identifier: BSD-3-Clause
 */

#include "objectFactory.hpp"

#include <memory>
#include <string>
#include <utility>
namespace griddyn {
objectFactory::objectFactory(const std::string& /*component*/, std::string typeName):
    name(std::move(typeName))
{
}

objectFactory::objectFactory(const std::string& /*component*/, const stringVec& typeNames):
    name(typeNames[0])
{
}

void objectFactory::prepObjects(count_t /*count*/, coreObject* /*obj*/) {}
count_t objectFactory::remainingPrepped() const
{
    return 0;
}
objectFactory::~objectFactory() = default;

componentFactory::componentFactory() = default;

componentFactory::componentFactory(std::string component): name(std::move(component)) {}
componentFactory::~componentFactory() = default;

void componentFactory::registerFactory(const std::string& typeName, objectFactory* oFac)
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

coreObject* componentFactory::makeObject()
{
    if (!m_defaultType.empty()) {
        coreObject* obj = m_factoryMap[m_defaultType]->makeObject();
        return obj;
    }
    return nullptr;
}

bool componentFactory::isValidType(const std::string& typeName) const
{
    auto mfind = m_factoryMap.find(typeName);
    return (mfind != m_factoryMap.end());
}

coreObject* componentFactory::makeObject(const std::string& type)
{
    auto mfind = m_factoryMap.find(type);
    if (mfind != m_factoryMap.end()) {
        coreObject* obj = m_factoryMap[type]->makeObject();
        return obj;
    }

    if (!m_defaultType.empty()) {
        coreObject* obj = m_factoryMap[m_defaultType]->makeObject();
        return obj;
    }

    return nullptr;
}

coreObject* componentFactory::makeObject(const std::string& type, const std::string& objectName)
{
    auto mfind = m_factoryMap.find(type);
    if (mfind != m_factoryMap.end()) {
        coreObject* obj = m_factoryMap[type]->makeObject(objectName);
        return obj;
    }

    if (!m_defaultType.empty()) {
        coreObject* obj = m_factoryMap[m_defaultType]->makeObject(objectName);
        return obj;
    }

    return nullptr;
}

void componentFactory::setDefault(const std::string& type)
{
    if (type.empty()) {
        m_defaultType = type;
    }
    auto mfind = m_factoryMap.find(type);
    if (mfind != m_factoryMap.end()) {
        m_defaultType = type;
    }
}

objectFactory* componentFactory::getFactory(const std::string& typeName)
{
    if (typeName.empty()) {
        return m_factoryMap[m_defaultType];
    }

    auto mfind = m_factoryMap.find(typeName);
    if (mfind != m_factoryMap.end()) {
        return m_factoryMap[typeName];
    }
    return nullptr;
}

// create a high level object factory for the coreObject class

std::shared_ptr<coreObjectFactory> coreObjectFactory::instance()
{
    // can't use make shared since constructor is private
    static std::shared_ptr<coreObjectFactory> factory =
        std::shared_ptr<coreObjectFactory>(new coreObjectFactory());  // NOLINT
    return factory;
}

void coreObjectFactory::registerFactory(const std::string& name,
                                        const std::shared_ptr<componentFactory>& tf)
{
    auto ret = m_factoryMap.emplace(name, tf);
    if (!ret.second) {
        ret.first->second = tf;
    }
}

void coreObjectFactory::registerFactory(const std::shared_ptr<componentFactory>& tf)
{
    auto ret = m_factoryMap.emplace(tf->name, tf);
    if (!ret.second) {
        ret.first->second = tf;
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

stringVec coreObjectFactory::getTypeNames(const std::string& component)
{
    auto mfind = m_factoryMap.find(component);
    if (mfind != m_factoryMap.end()) {
        return m_factoryMap[component]->getTypeNames();
    }
    return stringVec();
}

coreObject* coreObjectFactory::createObject(const std::string& component)
{
    auto mfind = m_factoryMap.find(component);
    if (mfind != m_factoryMap.end()) {
        coreObject* obj = m_factoryMap[component]->makeObject();
        return obj;
    }
    return nullptr;
}

coreObject* coreObjectFactory::createObject(const std::string& component,
                                            const std::string& typeName)
{
    auto mfind = m_factoryMap.find(component);
    if (mfind != m_factoryMap.end()) {
        coreObject* obj = m_factoryMap[component]->makeObject(typeName);
        return obj;
    }
    return nullptr;
}

coreObject* coreObjectFactory::createObject(const std::string& component,
                                            const std::string& typeName,
                                            const std::string& objName)
{
    auto mfind = m_factoryMap.find(component);
    if (mfind != m_factoryMap.end()) {
        coreObject* obj = m_factoryMap[component]->makeObject(typeName, objName);
        return obj;
    }
    return nullptr;
}

std::shared_ptr<componentFactory> coreObjectFactory::getFactory(const std::string& component)
{
    auto mfind = m_factoryMap.find(component);
    if (mfind != m_factoryMap.end()) {
        return (m_factoryMap[component]);
    }
    // make a new factory
    auto tf = std::make_shared<componentFactory>(component);
    m_factoryMap.emplace(component, tf);
    return tf;
}

bool coreObjectFactory::isValidObject(const std::string& component)
{
    auto mfind = m_factoryMap.find(component);
    return (mfind != m_factoryMap.end());
}

bool coreObjectFactory::isValidType(const std::string& component, const std::string& typeName)
{
    auto mfind = m_factoryMap.find(component);
    if (mfind != m_factoryMap.end()) {
        return mfind->second->isValidType(typeName);
    }
    return false;
}

void coreObjectFactory::prepObjects(const std::string& component,
                                    const std::string& typeName,
                                    count_t numObjects,
                                    coreObject* obj)
{
    auto mfind = m_factoryMap.find(component);
    if (mfind != m_factoryMap.end()) {
        auto* obfact = m_factoryMap[component]->getFactory(typeName);
        if (obfact != nullptr) {
            obfact->prepObjects(numObjects, obj);
        }
    }
}

void coreObjectFactory::prepObjects(const std::string& component,
                                    count_t numObjects,
                                    coreObject* obj)
{
    auto mfind = m_factoryMap.find(component);
    if (mfind != m_factoryMap.end()) {
        auto* obfact = m_factoryMap[component]->getFactory("");
        obfact->prepObjects(numObjects, obj);
    }
}

coreObjectFactory::coreObjectFactory() = default;
coreObjectFactory::~coreObjectFactory() = default;

}  // namespace griddyn
