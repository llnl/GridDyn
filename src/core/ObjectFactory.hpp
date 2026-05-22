/*
 * Copyright (c) 2014-2026, Lawrence Livermore National Security
 * See the top-level NOTICE for additional details. All rights reserved.
 * SPDX-License-Identifier: BSD-3-Clause
 */

#pragma once

#include "CoreObject.h"
#include <functional>
#include <map>
#include <memory>
#include <span>
#include <string>
#include <string_view>
#include <type_traits>
#include <vector>

namespace griddyn {
/** @brief class definitions for the object factories that can create the objects
 ObjectFactory is a virtual base class for object construction functions
*/
class ObjectFactory {
  public:
    std::string name;  //!< factory name

    /** @brief constructor
    @param[in] component  the name of the type of component this factory is a constructor for
    @param[in] typeName  the name of the specific type of object this factor builds
    */
    ObjectFactory(std::string_view component, std::string_view typeName);

    /** @brief constructor B
    @param[in] component  the name of component this factory is a constructor for
    @param[in] typeNames  the names of the specific types of objects this factor builds
    */
    ObjectFactory(std::string_view component, std::span<const std::string_view> typeNames);
    ObjectFactory(std::string_view component, const stringVec& typeNames);

    /** @brief make and object   abstract function
    @return a pointer to a newly constructed object
    */
    virtual CoreObject* makeObject() = 0;

    /** @brief make and object   abstract function
    @param[in] objectName  the name of the object to construct
    @return a pointer to a newly constructed object
    */

    virtual CoreObject* makeObject(std::string_view objectName) = 0;
    /** @brief prepare to make a certain number of objects
    The parameters are intentionally unnamed in the interface because they are optional to use.
    */
    virtual void prepObjects(count_t /*count*/, CoreObject* /*obj*/);
    /** @brief get the number of unused prepped objects
    @return the number of prepped objects
    */
    virtual count_t remainingPrepped() const;
    /** @brief destructor*/
    virtual ~ObjectFactory();
};
// component factory is a template class that inherits from cFactory to actually to the construction
// of a specific object

using cMap = std::map<std::string, ObjectFactory*, std::less<>>;

/** @brief a factory containing a mapping of specific object factories for a specific component
 */
class ComponentFactory {
  public:
    std::string name;  //!< name of the component
    ComponentFactory();
    explicit ComponentFactory(std::string component);
    ~ComponentFactory();
    stringVec getTypeNames();
    CoreObject* makeObject(std::string_view type, std::string_view objectName);
    CoreObject* makeObject(std::string_view type);
    CoreObject* makeObject();
    void registerFactory(std::string_view typeName, ObjectFactory* oFac);
    void registerFactory(ObjectFactory* oFac);
    void setDefault(std::string_view type);
    bool isValidType(std::string_view typeName) const;
    ObjectFactory* getFactory(std::string_view typeName);

  protected:
    cMap m_factoryMap;
    std::string mDefaultType;
};

// create a high level object factory for the CoreObject class
using fMap = std::map<std::string, std::shared_ptr<ComponentFactory>, std::less<>>;
/** @brief central location for building objects and storing factories for making all the gridDyn
 component core object Factory class  intended to be a singleton it contains a map from strings to
 typeFactories
*/
class CoreObjectFactory {
  public:
    ~CoreObjectFactory();

    /** @brief get a shared pointer to the core object factory*/
    static std::shared_ptr<CoreObjectFactory> instance();

    /** @brief register a type factory with the CoreObjectFactory
    @param[in] name the string identifier to the factory
    @param[in] componentFac the type factory to place in the map
    */
    void registerFactory(std::string_view name,
                         const std::shared_ptr<ComponentFactory>& componentFac);

    /** @brief register a type factory with the CoreObjectFactory
    gets the name to use in the mapping from the type factory itself
    @param[in] componentFac the type factory to place in the map
    */
    void registerFactory(const std::shared_ptr<ComponentFactory>& componentFac);

    /** @brief get a listing of the factory names*/
    stringVec getFactoryNames();

    /** @brief get a listing of the type names available for a given factory*/
    stringVec getTypeNames(std::string_view component);

    /** @brief create the default object from a given component
    @param[in] component  the name of the category of objects
    @return the created CoreObject */
    CoreObject* createObject(std::string_view component);

    /** @brief create an object from a given objectType and typeName
    @param[in] component  the name of the category of objects
    @param[in] typeName  the specific type to create
    @return the created CoreObject */
    CoreObject* createObject(std::string_view component, std::string_view typeName);

    /** @brief create an object from a given objectType and typeName
    @param[in] component  the name of the category of objects
    @param[in] typeName  the specific type to create
    @param[in] objName  the name of the object to create
    @return the created CoreObject */
    CoreObject* createObject(std::string_view component,
                             std::string_view typeName,
                             std::string_view objName);

    /** @brief get a specific type factory
    @param[in] component the name of the TypeFactory to get
    @return a shared pointer to a specific type Factory
    */
    std::shared_ptr<ComponentFactory> getFactory(std::string_view component);

    /** @brief check if a specific object category is valid*/
    bool isValidObject(std::string_view component);

    /** @brief check if a specific component name is valid for a specific category of objects*/
    bool isValidType(std::string_view component, std::string_view typeName);

    /** @brief prepare a number of objects for use later so they can all be constructed at once
    @param[in] component the category of Object to create
    @param[in] typeName the specific type of object in reference
    @param[in] numObjects  the number of objects to preallocate
    @param[in] obj the object to reference as the owner responsible for deleting the container
    */
    void prepObjects(std::string_view component,
                     std::string_view typeName,
                     count_t numObjects,
                     CoreObject* obj);

    /** @brief prepare a number of objects for use later so they can all be constructed at once of
    the default type for a given container
    @param[in] component the category of Object to create
    @param[in] numObjects  the number of objects to preallocate
    @param[in] obj the object to reference as the owner responsible for deleting the container
    */
    void prepObjects(std::string_view component, count_t numObjects, CoreObject* obj);

  private:
    CoreObjectFactory();

    fMap m_factoryMap;  //!< the main map from string to the TypeFactory
};

using objectFactory = ObjectFactory;  // NOLINT(readability-identifier-naming)
using componentFactory = ComponentFactory;  // NOLINT(readability-identifier-naming)
using coreObjectFactory = CoreObjectFactory;  // NOLINT(readability-identifier-naming)
}  // namespace griddyn
