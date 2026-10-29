/*
 * Copyright (c) 2014-2026, Lawrence Livermore National Security
 * See the top-level NOTICE for additional details. All rights reserved.
 * SPDX-License-Identifier: BSD-3-Clause
 */

#pragma once

#include "core/CoreOwningPtr.hpp"
#include "gridOptObjects.h"
#include <functional>
#include <map>
#include <memory>
#include <string>
#include <string_view>
#include <type_traits>
#include <vector>

namespace griddyn {
/** Base class for factories that construct GridOptObject implementations. */
class OptFactory {
  public:
    std::string name;  //!< registered optimization-object type name
    int mLevel = 0;  //!< priority used when multiple factories accept an object

    /** Creates a factory associated with one GridDyn component type.
     * @param[in] component component factory name (registration context)
     * @param[in] objName optimization-object type name
     * @param[in] level factory priority
     */
    OptFactory([[maybe_unused]] std::string_view component,
               std::string_view objName,
               int level = 0): name(objName), mLevel(level)
    {
    }

    /** Creates a factory associated with a set of GridDyn component types.
     * @param[in] component supported component factory names
     * @param[in] objName optimization-object type name
     * @param[in] level factory priority
     */
    OptFactory([[maybe_unused]] const stringVec& component,
               std::string_view objName,
               int level = 0): name(objName), mLevel(level)
    {
    }

    /** Constructs an optimization object attached to a GridDyn object.
     * @param[in] obj source object to associate with the new object
     * @return newly created optimization object
     */
    virtual GridOptObject* makeObject(CoreObject* obj) = 0;

    /** Constructs an unattached optimization object. */
    virtual GridOptObject* makeObject() = 0;

    /** Optionally preallocates the requested count for repeated construction
     * using the source object to find the model root.
     */
    virtual void prepObjects(count_t /*count*/, CoreObject* /*obj*/) {}

    /** Gets the number of preallocated objects still available. */
    virtual count_t remainingPrepped() const { return 0; }

    /** Checks whether this factory supports a source object. */
    virtual bool testObject(CoreObject*) { return true; }
};

/** Transparent-name map from optimization type names to factories. */
using optMap = std::map<std::string, OptFactory*, std::less<>>;

/** Factory registry for optimization objects belonging to one component type. */
class OptComponentFactory {
  public:
    std::string name;  //!< component factory name

    /** Creates an unnamed component factory. */
    OptComponentFactory() {}

    /** Creates a component factory with the given registered name.
     * @param[in] typeName component factory name
     */
    OptComponentFactory(std::string_view typeName);

    /** Destroys the registry containers without taking ownership of factories. */
    ~OptComponentFactory();

    /** Lists registered optimization-object type names. */
    stringVec getObjNames();

    /** Creates the best matching object for a GridDyn object. */
    GridOptObject* makeObject(CoreObject* obj);

    /** Creates an object of a named optimization type.
     * @param[in] objType registered optimization-object type name
     */
    GridOptObject* makeObject(std::string_view objType);

    /** Calls the registry's default-creation hook, which currently returns null. */
    GridOptObject* makeObject();

    /** Registers an optimization-object factory.
     * @param[in] optFac factory to register
     */
    void registerFactory(OptFactory* optFac);

    /** Checks whether an optimization-object type is registered.
     * @param[in] objName type name to check
     */
    bool isValidObject(std::string_view objName);

    /** Gets a registered factory by optimization-object type name.
     * @param[in] typeName registered type name
     * @return matching factory, or null when no factory is registered
     */
    OptFactory* getFactory(std::string_view typeName);

  protected:
    optMap mFactoryMap;  //!< factories indexed by optimization-object type
    std::vector<OptFactory*> mFactoryList;  //!< factories in registration order
};

/** Map of component factory names to their shared registries. */
using optfMap = std::map<std::string, std::shared_ptr<OptComponentFactory>, std::less<>>;

/** Global registry that creates optimization objects for GridDyn objects. */
class CoreOptObjectFactory {
  public:
    /** public destructor
     * Destructor must be public to work with shared_ptr
     */
    ~CoreOptObjectFactory() {}
    /** Gets the process-wide factory registry. */
    static std::shared_ptr<CoreOptObjectFactory> instance();

    /** Registers a named component factory registry.
     * @param[in] name component factory name
     * @param[in] componentFac registry to associate with the name
     */
    void registerFactory(std::string_view name, std::shared_ptr<OptComponentFactory> componentFac);

    /** Registers a component factory using its own name. */
    void registerFactory(std::shared_ptr<OptComponentFactory> componentFac);

    /** Lists registered component factory names. */
    stringVec getFactoryNames();

    /** Lists optimization-object types registered under a component factory.
     * @param[in] factoryName component factory name
     */
    stringVec getObjNames(std::string_view factoryName);

    /** Creates an object by component and optimization-object type names. */
    GridOptObject* createObject(std::string_view optType, std::string_view typeName);

    /** Creates an object by component type and source GridDyn object. */
    GridOptObject* createObject(std::string_view optType, CoreObject* obj);

    /** Selects a registered factory for the source object and creates an object. */
    GridOptObject* createObject(CoreObject* obj);

    /** Creates an object using the default component factory. */
    GridOptObject* createObject(std::string_view typeName);

    /** Gets or creates a component registry by name.
     * @param[in] factoryName component factory name
     * @return shared registry, creating an empty one when the name is unknown
     */
    std::shared_ptr<OptComponentFactory> getFactory(std::string_view factoryName);

    /** Checks whether a component factory type is registered. */
    bool isValidType(std::string_view obComponent);

    /** Checks whether an object type is registered for a component. */
    bool isValidObject(std::string_view optType, std::string_view objName);

    /** Sets the component factory used by default creation overloads.
     * @param[in] defType registered component factory name
     */
    void setDefaultType(std::string_view defType);

    /** Preallocates objects for a named component and object type.
     * @param[in] optType component factory name
     * @param[in] typeName optimization-object type name
     * @param[in] numObjects number of objects to prepare
     * @param[in] baseObj source object whose root owns the prepared block
     */
    void prepObjects(std::string_view optType,
                     std::string_view typeName,
                     count_t numObjects,
                     CoreObject* baseObj);

    /** Preallocates objects using the default component factory.
     * @param[in] typeName optimization-object type name
     * @param[in] numObjects number of objects to prepare
     * @param[in] baseObj source object whose root owns the prepared block
     */
    void prepObjects(std::string_view typeName, count_t numObjects, CoreObject* baseObj);

  private:
    CoreOptObjectFactory() {}

    optfMap mFactoryMap;
    std::string mDefaultType;
};

/** Owns a block of preconstructed optimization objects for one model type.
 * The holder is attached to the GridDyn root during preallocation so the
 * objects' lifetime follows the model tree.
 */
template<class Ntype, class gdType>
class GridOptObjectHolder: public CoreObject {
    static_assert(std::is_base_of<GridOptObject, Ntype>::value,
                  "opt class must have a base class of GridOptObject");
    static_assert(std::is_base_of<CoreObject, gdType>::value,
                  "gridDyn class must have base class type of CoreObject");

  private:
    std::vector<Ntype> objArray;
    count_t mNext = 0;
    count_t mObjectCount = 0;

  public:
    /** Constructs a holder containing the requested number of objects.
     * @param[in] objs number of optimization objects to construct
     */
    GridOptObjectHolder(count_t objs): objArray(objs), mObjectCount(objs)
    {
        for (auto& so : objArray) {
            so.addOwningReference();
        }
    }
    /** Gets the next object, or null when the block is exhausted. */
    Ntype* getNext()
    {
        Ntype* obj = nullptr;
        if (mNext < mObjectCount) {
            obj = &(objArray[mNext]);
            ++mNext;
        }
        return obj;
    }

    /** Gets the number of objects not yet handed out. */
    count_t remaining() const { return mObjectCount - mNext; }
};

/** Factory for a specific GridOptObject type, optionally constructed in blocks.
 * @tparam Ntype optimization-object implementation to construct
 * @tparam gdType GridDyn object type accepted by testObject
 */
template<class Ntype, class gdType>
class OptObjectFactory: public OptFactory {
    static_assert(std::is_base_of<GridOptObject, Ntype>::value,
                  "opt class must have a base class of GridOptObject");
    static_assert(std::is_base_of<CoreObject, gdType>::value,
                  "gridDyn class must have base class type of CoreObject");

  private:
    bool mUseBlock = false;
    CoreOwningPtr<GridOptObjectHolder<Ntype, gdType>> mObjectHolder;

    void attachHolderToRoot(CoreObject* root)
    {
        if ((root != nullptr) && mObjectHolder && (mObjectHolder->getParent() != root)) {
            root->add(mObjectHolder.get());
        }
    }

  public:
    /** Registers this factory for one GridDyn component type.
     * @param[in] component component factory name
     * @param[in] objName optimization-object type name
     * @param[in] level factory priority
     * @param[in] makeDefault whether to make this component type the default
     */
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

    /** Registers this factory for several GridDyn component types.
     * @param[in] components component factory names
     * @param[in] objName optimization-object type name
     * @param[in] level factory priority
     * @param[in] makeDefault whether to make the first component type default
     */
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

    /** Checks whether the source object has the configured GridDyn type. */
    bool testObject(CoreObject* obj) override
    {
        if (dynamic_cast<gdType*>(obj)) {
            return true;
        } else {
            return false;
        }
    }

    /** Creates an object and associates it with a source GridDyn object. */
    GridOptObject* makeObject(CoreObject* obj) override
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

    /** Creates an unattached object, using a prepared block when available. */
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

    /** Creates a typed object and associates it with a source object. */
    Ntype* makeTypeObject(CoreObject* obj)
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

    /** Prepares reusable objects under the source model root.
     * @param[in] count number of objects to prepare
     * @param[in] obj source object whose root owns the prepared block
     */
    virtual void prepObjects(count_t count, CoreObject* obj) override
    {
        if ((count == 0) || (obj == nullptr)) {
            mUseBlock = false;
            return;
        }

        auto root = obj->getRoot();
        if (root == nullptr) {
            mUseBlock = false;
            mObjectHolder = nullptr;
            return;
        }

        if (mObjectHolder && (mObjectHolder->remaining() >= count)) {
            attachHolderToRoot(root);
            mUseBlock = true;
            return;
        }

        mObjectHolder = makeOwningPtr<GridOptObjectHolder<Ntype, gdType>>(count);
        attachHolderToRoot(root);
        mUseBlock = true;
    }

    /** Gets the number of prepared objects still available. */
    virtual count_t remainingPrepped() const override
    {
        return (mObjectHolder) ? (mObjectHolder->remaining()) : 0;
    }
};

}  // namespace griddyn
