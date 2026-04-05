/*
 * Copyright (c) 2014-2020, Lawrence Livermore National Security
 * See the top-level NOTICE for additional details. All rights reserved.
 * SPDX-License-Identifier: BSD-3-Clause
 */

#ifndef GD_OBJECT_FACTORY_TEMPLATES_H_
#define GD_OBJECT_FACTORY_TEMPLATES_H_
#pragma once

#include "core/coreExceptions.h"
#include "core/coreOwningPtr.hpp"
#include "objectFactory.hpp"

namespace griddyn {
/** @brief template class for object ownership*/
template<class Ntype>
class gridObjectHolder: public coreObject {
    static_assert(std::is_base_of<coreObject, Ntype>::value,
                  "holder object must have coreObject as base");

  private:
    std::vector<Ntype>
        objArray;  // vector of objects, this should not resize or change once created
    count_t next = 0;
    count_t objCount = 0;

  public:
    explicit gridObjectHolder(count_t objs): coreObject("holder_#"), objArray(objs), objCount(objs)
    {
        for (auto& so : objArray) {  // we want to add an owning reference since these objects are
                                     // held internally and should not be deleted
            // elsewhere
            so.addOwningReference();
        }
    }
    ~gridObjectHolder()
    {
        for (auto& so : objArray) {
            if (so.getParent()) {
                so.getParent()->remove(&so);
            }
        }
    }
    Ntype* getNext()
    {
        Ntype* obj = nullptr;
        if (next < objCount) {
            obj = &(objArray[next]);
            ++next;
        }
        return obj;
    }

    count_t remaining() const { return objCount - next; }
    coreObject* getRoot() const
    {
        if (getParent()->getID() > 0) {
            return getParent();
        }
        // loop down from the most recent used to get the most recently established root object
        // can't use index_t here since it might be unsigned
        for (auto pp = static_cast<int>(next - 1); pp >= 0; --pp) {
            if (objArray[pp].getParent()) {
                return objArray[pp].getParent()->getRoot();
            }
        }

        return nullptr;
    }
};
/** @brief template class for creating an object block of prepared objects
@details a class which contains an object holder and gives them out as requested*/
template<class Ntype>
class objectPrepper {
    static_assert(std::is_base_of<coreObject, Ntype>::value,
                  "factory class must have coreObject as base");

  private:
    coreOwningPtr<gridObjectHolder<Ntype>> obptr;
    count_t targetprepped = 0;
    bool useBlock;

  public:
    objectPrepper(count_t objCount, coreObject* example) { prepObjects(objCount, example); }
    void prepObjects(count_t objCount, coreObject* example)
    {
        auto root = example->getRoot();
        useBlock = true;
        if ((obptr) && (root != nullptr)) {
            if (obptr->getParent() != root) {
                root->add(obptr.get());
            }
        }
        if (remaining() < objCount) {
            if ((obptr) && (obptr->remaining() > 0)) {
                targetprepped = objCount - obptr->remaining();
            } else {
                obptr = make_owningPtr<gridObjectHolder<Ntype>>(targetprepped);
                if (root != nullptr) {
                    if (!obptr) {
                        root->log(root, print_level::warning, "unable to create container object");
                        useBlock = false;
                    } else {
                        root->add(obptr.get());
                    }
                } else {
                    useBlock = false;
                }
            }
        }
    }
    Ntype* getNewObject(const std::string& objName = "")
    {
        Ntype* ret = nullptr;
        if (useBlock) {
            ret = obptr->getNext();
            if (ret == nullptr) {  // means the block was used up
                if (targetprepped > 0)  // if we are scheduled for more make a new objectHolder
                {
                    auto root = obptr->getRoot();
                    if (root != nullptr) {
                        if (!isSameObject(obptr->getParent(), root)) {
                            root->add(obptr.get());
                        }
                    }
                    obptr = make_owningPtr<gridObjectHolder<Ntype>>(targetprepped);
                    if (!obptr) {
                        if (root != nullptr) {
                            root->log(root,
                                      print_level::warning,
                                      "unable to create container object");
                        }
                        useBlock = false;
                    } else {
                        if (root != nullptr) {
                            root->add(obptr.get());
                        }
                        ret = obptr->getNext();
                        useBlock = true;
                    }
                    targetprepped = 0;
                } else {
                    useBlock = false;
                    obptr = nullptr;
                }
            }
            if (ret) {
                if (!objName.empty()) {
                    ret->setName(objName);
                }
            }
        }
        if (ret == nullptr)  // if we fail on all counts just make a new object
        {
            ret = (objName.empty()) ? (new Ntype()) : (new Ntype(objName));
        }
        return ret;
    }
    count_t remaining() { return (obptr) ? (obptr->remaining() + targetprepped) : 0; }
    coreObject* getHolder() const { return obptr.get(); }
};

/** @brief template class for object construction */
template<class Ntype>
class typeFactory: public objectFactory {
    static_assert(std::is_base_of<coreObject, Ntype>::value,
                  "factory class must have coreObject as base");

  private:
    std::unique_ptr<objectPrepper<Ntype>> preparedObjects;

  public:
    typeFactory(const std::string& component, const std::string& typeName):
        objectFactory(component, typeName)
    {
        auto tF = coreObjectFactory::instance()->getFactory(component);
        tF->registerFactory(typeName, this);
    }

    typeFactory(const std::string& component, const stringVec& typeNames):
        objectFactory(component, typeNames)
    {
        auto tF = coreObjectFactory::instance()->getFactory(component);
        for (auto tname : typeNames) {
            tF->registerFactory(tname, this);
        }
    }
    typeFactory(const std::string& component,
                const stringVec& typeNames,
                const std::string& defType): objectFactory(component, typeNames)
    {
        auto tF = coreObjectFactory::instance()->getFactory(component);
        for (auto tname : typeNames) {
            tF->registerFactory(tname, this);
        }
        tF->setDefault(defType);
    }

    coreObject* makeObject() override
    {
        coreObject* ret = makeTypeObject();
        return ret;
    }

    coreObject* makeObject(const std::string& objName) override
    {
        coreObject* ret = makeTypeObject(objName);
        return ret;
    }

    virtual Ntype* makeTypeObject(const std::string& objName = "")
    {
        if (preparedObjects) {
            return preparedObjects->getNewObject(objName);
        }
        if (!objName.empty()) {
            return new Ntype(objName);
        }
        return new Ntype();
    }

    virtual void prepObjects(count_t objectCount, coreObject* obj) override
    {
        if (!preparedObjects) {
            preparedObjects = std::make_unique<objectPrepper<Ntype>>(objectCount, obj);
        } else {
            preparedObjects->prepObjects(objectCount, obj);
        }
    }
    virtual count_t remainingPrepped() const override
    {
        return (preparedObjects) ? preparedObjects->remaining() : 0;
    }

    virtual coreObject* getHolder() const
    {
        return (preparedObjects) ? preparedObjects->getHolder() : nullptr;
    }
};

/** @brief template class for inherited object factories to cascade correctly*/
template<class Ntype, class Btype>
class childTypeFactory: public typeFactory<Btype> {
    static_assert(std::is_base_of<coreObject, Btype>::value,
                  "factory class must have coreObject as base");
    static_assert(std::is_base_of<Btype, Ntype>::value,
                  "factory class types must have parent child relationship");

  private:
    std::unique_ptr<objectPrepper<Ntype>> preparedObjects;

  public:
    childTypeFactory(const std::string& component, const std::string& typeName):
        typeFactory<Btype>(component, typeName)
    {
    }

    childTypeFactory(const std::string& component, const stringVec& typeNames):
        typeFactory<Btype>(component, typeNames)
    {
    }
    childTypeFactory(const std::string& component,
                     const stringVec& typeNames,
                     const std::string& defType): typeFactory<Btype>(component, typeNames, defType)
    {
    }
    coreObject* makeObject() override
    {
        coreObject* ret = makeTypeObject();

        return ret;
    }

    coreObject* makeObject(const std::string& objName) override
    {
        coreObject* ret = makeTypeObject(objName);

        return ret;
    }

    Btype* makeTypeObject(
        const std::string& objName = std::string()) override  // done this way to make sure calling
    // makeTypeObject on the parent works in the
    // correct polymorphic call
    {
        return makeDirectObject(objName);
    }

    Ntype* makeDirectObject(
        const std::string& objName = std::string())  // done this way to make sure calling
    // makeTypeObject on the parent works in the
    // correct polymorphic call
    {
        if (preparedObjects) {
            return preparedObjects->getNewObject(objName);
        }
        if (!objName.empty()) {
            return new Ntype(objName);
        }
        return new Ntype();
    }

    virtual void prepObjects(count_t objectCount, coreObject* obj) override
    {
        if (!preparedObjects) {
            preparedObjects = std::make_unique<objectPrepper<Ntype>>(objectCount, obj);
        } else {
            preparedObjects->prepObjects(objectCount, obj);
        }
    }
    virtual count_t remainingPrepped() const override
    {
        return (preparedObjects) ? preparedObjects->remaining() : 0;
    }

    virtual coreObject* getHolder() const override
    {
        return (preparedObjects) ? preparedObjects->getHolder() : nullptr;
    }
};

template<class Ntype, class argType>
class typeFactoryArg: public objectFactory {
    static_assert(std::is_base_of<coreObject, Ntype>::value,
                  "factory class must have coreObject as base");
    static_assert(!std::is_same<argType, std::string>::value, "arg type cannot be a std::string");

  public:
    argType arg;
    typeFactoryArg(const std::string& component, const std::string& typeName, const argType iArg):
        objectFactory(component, typeName), arg(iArg)
    {
        auto tF = coreObjectFactory::instance()->getFactory(component);
        tF->registerFactory(typeName, this);
    }

    typeFactoryArg(const std::string& component, const stringVec& typeNames, const argType iArg):
        objectFactory(component, typeNames), arg(iArg)
    {
        auto tF = coreObjectFactory::instance()->getFactory(component);
        for (auto tname : typeNames) {
            tF->registerFactory(tname, this);
        }
    }

    coreObject* makeObject() override { return static_cast<coreObject*>(new Ntype(arg)); }
    coreObject* makeObject(const std::string& objName) override
    {
        return static_cast<coreObject*>(new Ntype(arg, objName));
    }

    Ntype* makeTypeObject(const std::string& objName = "")
    {
        return (objName.empty()) ? (new Ntype(arg)) : (new Ntype(arg, objName));
    }
};

/**
* @brief template for cloneBase using an object Factory to make new objects instead of new
* @param[in] object of class A to be cloned
* @param[in] pointer of an object to clone to or a null pointer if a new object needs to be created
@param[in] factory making the new object
* @return pointer to the cloned object
*/
template<class A, class B>
A* cloneBaseFactory(const A* bobj, coreObject* obj, objectFactory* cfact)
{
    static_assert(std::is_base_of<B, A>::value,
                  "classes A and B must have parent child relationship");
    static_assert(std::is_base_of<coreObject, B>::value,
                  "classes must be inherited from coreObject");
    static_assert(std::is_base_of<coreObject, A>::value,
                  "classes must be inherited from coreObject");
    A* nobj;
    if (obj == nullptr) {
        auto cobj = cfact->makeObject(bobj->getName());
        nobj = dynamic_cast<A*>(cobj);
        if (nobj == nullptr) {
            // well this is just confusing what to do here
            delete cobj;
            throw(cloneFailure(bobj));
        }
    } else {
        nobj = dynamic_cast<A*>(obj);
        if (nobj == nullptr) {
            // if we can't cast the pointer clone at the next lower level
            bobj->B::clone(obj);
            return nullptr;
        }
    }
    bobj->B::clone(nobj);
    return nobj;
}

}  // namespace griddyn
#endif
