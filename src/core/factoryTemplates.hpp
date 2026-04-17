/*
 * Copyright (c) 2014-2026, Lawrence Livermore National Security
 * See the top-level NOTICE for additional details. All rights reserved.
 * SPDX-License-Identifier: BSD-3-Clause
 */

#ifndef FACTORY_TEMPLATES_H_
#define FACTORY_TEMPLATES_H_
#pragma once

#include <map>
#include <memory>
#include <string>
#include <type_traits>
#include <vector>

namespace griddyn {
/**create a factory for a specific type of helper component*/
template<class parentClass>
class classFactory;

/** @brief factory for building types of various components that interact with GridDyn
 */
template<class parentClass>
class coreClassFactory {
    using fMap = std::map<std::string, classFactory<parentClass>*>;
    std::string m_defaultType;

  public:
    /** @brief get a shared pointer to the core object factory*/
    static std::shared_ptr<coreClassFactory> instance()
    {
        static std::shared_ptr<coreClassFactory> factory =
            std::shared_ptr<coreClassFactory>(new coreClassFactory());
        return factory;
    }

    /** @brief register a type factory with the coreObjectFactory
    @param[in] name the string identifier to the factory
    @param[in] tf the type factory to place in the map
    */
    void registerFactory(const std::string& name, classFactory<parentClass>* tf)
    {
        m_factoryMap[name] = tf;
    }
    /** @brief register a type factory with the coreObjectFactory
    gets the name to use in the mapping from the type factory itself
    @param[in] tf the type factory to place in the map
    */
    void registerFactory(classFactory<parentClass>* tf) { registerFactory(tf->name, tf); }
    void setDefault(const std::string& type)
    {
        if (type.empty()) {
            return;
        }
        auto mfind = m_factoryMap.find(type);
        if (mfind != m_factoryMap.end()) {
            m_defaultType = type;
        }
    }

    /** @brief get a listing of the factory names*/
    std::vector<std::string> getFactoryNames()
    {
        std::vector<std::string> tnames;
        tnames.reserve(m_factoryMap.size());
        for (auto tname : m_factoryMap) {
            tnames.push_back(tname.first);
        }
        return tnames;
    }

    /** @brief create an object from a given objectType and typeName
    @param[in] typeName  the specific type to create
    @return the created coreObject */
    std::unique_ptr<parentClass> createObject(const std::string& typeName)
    {
        auto mfind = m_factoryMap.find(typeName);
        if (mfind != m_factoryMap.end()) {
            return m_factoryMap[typeName]->makeObject();
        }
        if (!m_defaultType.empty()) {
            return m_factoryMap[m_defaultType]->makeObject();
        }
        return nullptr;
    }

    /** @brief create an object from the specific type with a name of objName
    @param[in] typeName  the specific type to create
    @param[in] objName  the name of the object to create
    @return the created coreObject */
    std::unique_ptr<parentClass> createObject(const std::string& typeName,
                                              const std::string& objName)
    {
        auto mfind = m_factoryMap.find(typeName);
        if (mfind != m_factoryMap.end()) {
            return m_factoryMap[typeName]->makeObject(objName);
        }
        if (!m_defaultType.empty()) {
            return m_factoryMap[m_defaultType]->makeObject(objName);
        }
        return nullptr;
    }

    /** @brief get a specific type factory
    @param[in] typeName the name of the typeFactory to get
    @return a shared pointer to a specific type Factory
    */
    classFactory<parentClass>* getFactory(const std::string& typeName)
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

    /** @brief check if a specific object category is valid*/
    bool isValidObject(const std::string& typeName)
    {
        auto mfind = m_factoryMap.find(typeName);
        return (mfind != m_factoryMap.end());
    }

  private:
    coreClassFactory() = default;
    fMap m_factoryMap;  //!< the main map from string to the classFactory
};

template<class parentClass>
class classFactory {
  public:
    std::string name;

    explicit classFactory(const std::string& keyName): name(keyName)
    {
        coreClassFactory<parentClass>::instance()->registerFactory(keyName, this);
    }
    explicit classFactory(const std::vector<std::string>& names): name(names[0])
    {
        auto cfac = coreClassFactory<parentClass>::instance();
        for (auto& nn : names) {
            cfac->registerFactory(nn, this);
        }
    }
    classFactory(const std::vector<std::string>& names, const std::string& defType): name(names[0])
    {
        auto cfac = coreClassFactory<parentClass>::instance();
        for (auto& nn : names) {
            cfac->registerFactory(nn, this);
        }
        cfac->setDefault(defType);
    }
    virtual ~classFactory() = default;
    virtual std::unique_ptr<parentClass> makeObject() { return std::make_unique<parentClass>(); }
    virtual std::unique_ptr<parentClass> makeObject(const std::string& newObjectName)
    {
        return std::make_unique<parentClass>(newObjectName);
    }
};

/** factory class for an inherited object*/
template<class childClass, class parentClass>
class childClassFactory: public classFactory<parentClass> {
    static_assert(std::is_base_of<parentClass, childClass>::value,
                  "factory classes must have parent child class relationship");

  public:
    explicit childClassFactory(const std::string& keyName): classFactory<parentClass>(keyName) {}
    explicit childClassFactory(const std::vector<std::string>& names):
        classFactory<parentClass>(names)
    {
    }
    explicit childClassFactory(const std::vector<std::string>& names, const std::string& defType):
        classFactory<parentClass>(names, defType)
    {
    }
    virtual std::unique_ptr<parentClass> makeObject() override
    {
        return std::make_unique<childClass>();
    }
    virtual std::unique_ptr<parentClass> makeObject(const std::string& newObjectName) override
    {
        return std::make_unique<childClass>(newObjectName);
    }

    std::unique_ptr<childClass> makeClassObject() { return std::make_unique<childClass>(); }
};

/** factory class for an inherited object with an argument*/
template<class childClass, class parentClass, class argType>
class childClassFactoryArg: public classFactory<parentClass> {
    static_assert(std::is_base_of<parentClass, childClass>::value,
                  "factory classes must have parent child class relationship");
    static_assert(!std::is_same<argType, std::string>::value, "arg type cannot be a std::string");

  private:
    argType argVal;

  public:
    childClassFactoryArg(const std::string& keyName, argType iArg):
        classFactory<parentClass>(keyName), argVal(iArg)
    {
    }
    childClassFactoryArg(const std::vector<std::string>& names, argType iArg):
        classFactory<parentClass>(names), argVal(iArg)
    {
    }
    childClassFactoryArg(const std::vector<std::string>& names,
                         const std::string& defType,
                         argType iArg): classFactory<parentClass>(names, defType), argVal(iArg)
    {
    }
    std::unique_ptr<parentClass> makeObject() override
    {
        return std::make_unique<childClass>(argVal);
    }
    std::unique_ptr<parentClass> makeObject(const std::string& newObjectName) override
    {
        return std::make_unique<childClass>(newObjectName, argVal);
    }
    std::unique_ptr<childClass> makeClassObject() { return std::make_unique<childClass>(argVal); }
};

}  // namespace griddyn
#endif
