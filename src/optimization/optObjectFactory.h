/*
 * Copyright (c) 2014-2026, Lawrence Livermore National Security
 * See the top-level NOTICE for additional details. All rights reserved.
 * SPDX-License-Identifier: BSD-3-Clause
 */

#pragma once

#include "gridOptObjects.h"
#include <functional>
#include <map>
#include <memory>
#include <string>
#include <string_view>
#include <type_traits>
#include <vector>

namespace griddyn {
// class definitions for the object factories that can create the objects
// cFactory is a virtual base class for object Construction functions
class OptFactory {
  public:
    std::string name;
    int mLevel = 0;
    OptFactory([[maybe_unused]] std::string_view component,
               std::string_view objName,
               int level = 0): name(objName), mLevel(level)
    {
    }
    OptFactory([[maybe_unused]] const stringVec& component,
               std::string_view objName,
               int level = 0): name(objName), mLevel(level)
    {
    }
    virtual GridOptObject* makeObject(coreObject* obj) = 0;
    virtual GridOptObject* makeObject() = 0;
    virtual void prepObjects(count_t /*count*/, coreObject* /*obj*/) {}
    virtual count_t remainingPrepped() const { return 0; }
    virtual bool testObject(coreObject*) { return true; }
};

using optMap = std::map<std::string, OptFactory*, std::less<>>;

class OptComponentFactory {
  public:
    std::string name;
    OptComponentFactory() {}
    OptComponentFactory(std::string_view typeName);
    ~OptComponentFactory();
    stringVec getObjNames();
    GridOptObject* makeObject(coreObject* obj);
    GridOptObject* makeObject(std::string_view objType);
    GridOptObject* makeObject();
    void registerFactory(OptFactory* optFac);
    bool isValidObject(std::string_view objName);
    OptFactory* getFactory(std::string_view typeName);

  protected:
    optMap mFactoryMap;
    std::vector<OptFactory*> mFactoryList;
};

// create a high level object factory for the coreObject class
using optfMap = std::map<std::string, std::shared_ptr<OptComponentFactory>, std::less<>>;

class CoreOptObjectFactory {
  public:
    /** public destructor
     * Destructor must be public to work with shared_ptr
     */
    ~CoreOptObjectFactory() {}
    static std::shared_ptr<CoreOptObjectFactory> instance();
    void registerFactory(std::string_view name, std::shared_ptr<OptComponentFactory> componentFac);
    void registerFactory(std::shared_ptr<OptComponentFactory> componentFac);
    stringVec getFactoryNames();
    stringVec getObjNames(std::string_view factoryName);
    GridOptObject* createObject(std::string_view optType, std::string_view typeName);
    GridOptObject* createObject(std::string_view optType, coreObject* obj);
    GridOptObject* createObject(coreObject* obj);
    GridOptObject* createObject(std::string_view typeName);
    std::shared_ptr<OptComponentFactory> getFactory(std::string_view factoryName);
    bool isValidType(std::string_view obComponent);
    bool isValidObject(std::string_view optType, std::string_view objName);
    void setDefaultType(std::string_view defType);
    void prepObjects(std::string_view optType,
                     std::string_view typeName,
                     count_t numObjects,
                     coreObject* baseObj);
    void prepObjects(std::string_view typeName, count_t numObjects, coreObject* baseObj);

  private:
    CoreOptObjectFactory() {}

    optfMap mFactoryMap;
    std::string mDefaultType;
};

/*** template class for opt object ownership*/
template<class Ntype, class gdType>
class GridOptObjectHolder: public coreObject {
    static_assert(std::is_base_of<GridOptObject, Ntype>::value,
                  "opt class must have a base class of GridOptObject");
    static_assert(std::is_base_of<coreObject, gdType>::value,
                  "gridDyn class must have base class type of coreObject");

  private:
    std::vector<Ntype> objArray;
    count_t mNext = 0;
    count_t mObjectCount = 0;

  public:
    GridOptObjectHolder(count_t objs): objArray(objs), mObjectCount(objs)
    {
        for (auto& so : objArray) {
            so.addOwningReference();
        }
    }
    Ntype* getNext()
    {
        Ntype* obj = nullptr;
        if (mNext < mObjectCount) {
            obj = &(objArray[mNext]);
            ++mNext;
        }
        return obj;
    }

    count_t remaining() const { return mObjectCount - mNext; }
};

// opt factory is a template class that inherits from cFactory to actually to the construction of a
// specific object
template<class Ntype, class gdType>
class OptObjectFactory: public OptFactory {
    static_assert(std::is_base_of<GridOptObject, Ntype>::value,
                  "opt class must have a base class of GridOptObject");
    static_assert(std::is_base_of<coreObject, gdType>::value,
                  "gridDyn class must have base class type of coreObject");

  private:
    bool mUseBlock = false;
    GridOptObjectHolder<Ntype, gdType>* mObjectHolder = nullptr;

  public:
    OptObjectFactory(std::string_view component,
                     std::string_view objName,
                     int level = 0,
                     bool makeDefault = false): OptFactory(component, objName, level)
    {
        auto coof = CoreOptObjectFactory::instance();
        auto fac = coof->getFactory(component);
        fac->registerFactory(this);
        if (makeDefault) {
            coof->setDefaultType(component);
        }
    }

    OptObjectFactory(const stringVec& components,
                     std::string_view objName,
                     int level = 0,
                     bool makeDefault = false): OptFactory(components[0], objName, level)
    {
        auto coof = CoreOptObjectFactory::instance();
        for (auto& tname : components) {
            coof->getFactory(tname)->registerFactory(this);
        }
        if (makeDefault) {
            coof->setDefaultType(components[0]);
        }
    }

    bool testObject(coreObject* obj) override
    {
        if (dynamic_cast<gdType*>(obj)) {
            return true;
        } else {
            return false;
        }
    }

    GridOptObject* makeObject(coreObject* obj) override
    {
        GridOptObject* ret = nullptr;
        if (mUseBlock) {
            ret = mObjectHolder->getNext();
            if (ret == nullptr) {  // means the block was used up
                mUseBlock = false;
                mObjectHolder = nullptr;
            } else {
                ret->add(obj);
            }
        }
        if (ret == nullptr) {
            ret = new Ntype(obj);
        }
        return ret;
    }

    GridOptObject* makeObject() override
    {
        GridOptObject* ret = nullptr;
        if (mUseBlock) {
            ret = mObjectHolder->getNext();
            if (ret == nullptr) {  // means the block was used up
                mUseBlock = false;
                mObjectHolder = nullptr;
            }
        }
        if (ret == nullptr) {
            ret = new Ntype();
        }
        return ret;
    }

    Ntype* makeTypeObject(coreObject* obj)
    {
        Ntype* ret = nullptr;
        if (mUseBlock) {
            ret = mObjectHolder->getNext();
            if (ret == nullptr) {  // means the block was used up
                mUseBlock = false;
                mObjectHolder = nullptr;
            } else {
                ret->add(obj);
            }
        }
        if (ret == nullptr) {
            ret = new Ntype(obj);
        }
        return ret;
    }

    virtual void prepObjects(count_t count, coreObject* obj) override
    {
        auto root = obj->getRoot();
        mObjectHolder = new GridOptObjectHolder<Ntype, gdType>(count);
        root->add(mObjectHolder);
        mUseBlock = true;
    }
    virtual count_t remainingPrepped() const override
    {
        return (mObjectHolder) ? (mObjectHolder->remaining()) : 0;
    }
};

}  // namespace griddyn
