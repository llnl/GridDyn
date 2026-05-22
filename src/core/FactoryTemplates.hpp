/*
 * Copyright (c) 2014-2026, Lawrence Livermore National Security
 * See the top-level NOTICE for additional details. All rights reserved.
 * SPDX-License-Identifier: BSD-3-Clause
 */

#pragma once

#include <functional>
#include <map>
#include <memory>
#include <string>
#include <string_view>
#include <type_traits>
#include <vector>

namespace griddyn {
/**create a factory for a specific type of helper component*/
template<class parentClass>
class ClassFactory;

/** @brief factory for building types of various components that interact with GridDyn
 */
template<class parentClass>
class CoreClassFactory {
    using fMap = std::map<std::string, ClassFactory<parentClass>*, std::less<>>;
    std::string mDefaultType;

  public:
    /** @brief get a shared pointer to the core object factory*/
    static std::shared_ptr<CoreClassFactory> instance()
    {
        static std::shared_ptr<CoreClassFactory> factory =
            std::shared_ptr<CoreClassFactory>(new CoreClassFactory());
        return factory;
    }

    /** @brief register a type factory with the CoreClassFactory
    @param[in] name the string identifier to the factory
    @param[in] tf the type factory to place in the map
    */
    void registerFactory(std::string_view name, ClassFactory<parentClass>* tf)
    {
        auto ret = m_factoryMap.emplace(name, tf);
        if (!ret.second) {
            ret.first->second = tf;
        }
    }
    /** @brief register a type factory with the CoreClassFactory
    gets the name to use in the mapping from the type factory itself
    @param[in] tf the type factory to place in the map
    */
    void registerFactory(ClassFactory<parentClass>* tf) { registerFactory(tf->name, tf); }
    void setDefault(std::string_view type)
    {
        if (type.empty()) {
            return;
        }
        auto mfind = m_factoryMap.find(type);
        if (mfind != m_factoryMap.end()) {
            mDefaultType = mfind->first;
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
    @return the created CoreObject */
    std::unique_ptr<parentClass> createObject(std::string_view typeName)
    {
        auto mfind = m_factoryMap.find(typeName);
        if (mfind != m_factoryMap.end()) {
            return mfind->second->makeObject();
        }
        if (!mDefaultType.empty()) {
            return m_factoryMap[mDefaultType]->makeObject();
        }
        return nullptr;
    }

    /** @brief create an object from the specific type with a name of objName
    @param[in] typeName  the specific type to create
    @param[in] objName  the name of the object to create
    @return the created CoreObject */
    std::unique_ptr<parentClass> createObject(std::string_view typeName, std::string_view objName)
    {
        auto mfind = m_factoryMap.find(typeName);
        if (mfind != m_factoryMap.end()) {
            return mfind->second->makeObject(objName);
        }
        if (!mDefaultType.empty()) {
            return m_factoryMap[mDefaultType]->makeObject(objName);
        }
        return nullptr;
    }

    /** @brief get a specific type factory
    @param[in] typeName the name of the TypeFactory to get
    @return a shared pointer to a specific type Factory
    */
    ClassFactory<parentClass>* getFactory(std::string_view typeName)
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

    /** @brief check if a specific object category is valid*/
    bool isValidObject(std::string_view typeName)
    {
        auto mfind = m_factoryMap.find(typeName);
        return (mfind != m_factoryMap.end());
    }

  private:
    CoreClassFactory() = default;
    fMap m_factoryMap;  //!< the main map from string to the ClassFactory
};

template<class parentClass>
class ClassFactory {
  public:
    std::string name;

    explicit ClassFactory(const std::string& keyName): name(keyName)
    {
        CoreClassFactory<parentClass>::instance()->registerFactory(keyName, this);
    }
    explicit ClassFactory(const std::vector<std::string>& names): name(names[0])
    {
        auto cfac = CoreClassFactory<parentClass>::instance();
        for (auto& nn : names) {
            cfac->registerFactory(nn, this);
        }
    }
    ClassFactory(const std::vector<std::string>& names, const std::string& defType): name(names[0])
    {
        auto cfac = CoreClassFactory<parentClass>::instance();
        for (auto& nn : names) {
            cfac->registerFactory(nn, this);
        }
        cfac->setDefault(defType);
    }
    virtual ~ClassFactory() = default;
    virtual std::unique_ptr<parentClass> makeObject() { return std::make_unique<parentClass>(); }
    virtual std::unique_ptr<parentClass> makeObject(std::string_view newObjectName)
    {
        return std::make_unique<parentClass>(std::string{newObjectName});
    }
};

/** factory class for an inherited object*/
template<class childClass, class parentClass>
class ChildClassFactory: public ClassFactory<parentClass> {
    static_assert(std::is_base_of<parentClass, childClass>::value,
                  "factory classes must have parent child class relationship");

  public:
    explicit ChildClassFactory(const std::string& keyName): ClassFactory<parentClass>(keyName) {}
    explicit ChildClassFactory(const std::vector<std::string>& names):
        ClassFactory<parentClass>(names)
    {
    }
    explicit ChildClassFactory(const std::vector<std::string>& names, const std::string& defType):
        ClassFactory<parentClass>(names, defType)
    {
    }
    virtual std::unique_ptr<parentClass> makeObject() override
    {
        return std::make_unique<childClass>();
    }
    virtual std::unique_ptr<parentClass> makeObject(std::string_view newObjectName) override
    {
        return std::make_unique<childClass>(std::string{newObjectName});
    }

    std::unique_ptr<childClass> makeClassObject() { return std::make_unique<childClass>(); }
};

/** factory class for an inherited object with an argument*/
template<class childClass, class parentClass, class argType>
class ChildClassFactoryArg: public ClassFactory<parentClass> {
    static_assert(std::is_base_of<parentClass, childClass>::value,
                  "factory classes must have parent child class relationship");
    static_assert(!std::is_same<argType, std::string>::value, "arg type cannot be a std::string");

  private:
    argType argVal;

  public:
    ChildClassFactoryArg(const std::string& keyName, argType iArg):
        ClassFactory<parentClass>(keyName), argVal(iArg)
    {
    }
    ChildClassFactoryArg(const std::vector<std::string>& names, argType iArg):
        ClassFactory<parentClass>(names), argVal(iArg)
    {
    }
    ChildClassFactoryArg(const std::vector<std::string>& names,
                         const std::string& defType,
                         argType iArg): ClassFactory<parentClass>(names, defType), argVal(iArg)
    {
    }
    std::unique_ptr<parentClass> makeObject() override
    {
        return std::make_unique<childClass>(argVal);
    }
    std::unique_ptr<parentClass> makeObject(std::string_view newObjectName) override
    {
        return std::make_unique<childClass>(std::string{newObjectName}, argVal);
    }
    std::unique_ptr<childClass> makeClassObject() { return std::make_unique<childClass>(argVal); }
};

template<class parentClass>
using classFactory = ClassFactory<parentClass>;  // NOLINT(readability-identifier-naming)

template<class parentClass>
using coreClassFactory = CoreClassFactory<parentClass>;  // NOLINT(readability-identifier-naming)

template<class childClass, class parentClass>
using childClassFactory =
    ChildClassFactory<childClass, parentClass>;  // NOLINT(readability-identifier-naming)

template<class childClass, class parentClass, class argType>
using childClassFactoryArg =
    ChildClassFactoryArg<childClass, parentClass, argType>;  // NOLINT(readability-identifier-naming)

}  // namespace griddyn
