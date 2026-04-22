/*
 * Copyright (c) 2014-2026, Lawrence Livermore National Security
 * See the top-level NOTICE for additional details. All rights reserved.
 * SPDX-License-Identifier: BSD-3-Clause
 */

#pragma once

#include "coreObject.h"
#include <functional>
#include <map>
#include <memory>
#include <string>
#include <string_view>
#include <type_traits>
#include <vector>

namespace griddyn {
/** @brief class definitions for the object factories that can create the objects
 cFactory is a virtual base class for object Construction functions
*/
class objectFactory {
  public:
    std::string name;  //!< factory name

    /** @brief constructor
    @param[in] component  the name of the type of component this factory is a constructor for
    @param[in] typeName  the name of the specific type of object this factor builds
    */
    objectFactory(std::string_view component, std::string typeName);

    /** @brief constructor B
    @param[in] component  the name of component this factory is a constructor for
    @param[in] typeNames  the names of the specific types of objects this factor builds
    */
    objectFactory(std::string_view component, const stringVec& typeNames);

    /** @brief make and object   abstract function
    @return a pointer to a newly constructed object
    */
    virtual coreObject* makeObject() = 0;

    /** @brief make and object   abstract function
    @param[in] objectName  the name of the object to construct
    @return a pointer to a newly constructed object
    */

    virtual coreObject* makeObject(std::string_view objectName) = 0;
    /** @brief prepare to make a certain number of objects
    The parameters are intentionally unnamed in the interface because they are optional to use.
    */
    virtual void prepObjects(count_t /*count*/, coreObject* /*obj*/);
    /** @brief get the number of unused prepped objects
    @return the number of prepped objects
    */
    virtual count_t remainingPrepped() const;
    /** @brief destructor*/
    virtual ~objectFactory();
};
// component factory is a template class that inherits from cFactory to actually to the construction
// of a specific object

using cMap = std::map<std::string, objectFactory*, std::less<>>;

/** @brief a factory containing a mapping of specific object factories for a specific component
 */
class componentFactory {
  public:
    std::string name;  //!< name of the component
    componentFactory();
    explicit componentFactory(std::string component);
    ~componentFactory();
    stringVec getTypeNames();
    coreObject* makeObject(std::string_view type, std::string_view objectName);
    coreObject* makeObject(std::string_view type);
    coreObject* makeObject();
    void registerFactory(std::string_view typeName, objectFactory* oFac);
    void registerFactory(objectFactory* oFac);
    void setDefault(std::string_view type);
    bool isValidType(std::string_view typeName) const;
    objectFactory* getFactory(std::string_view typeName);

  protected:
    cMap m_factoryMap;
    std::string m_defaultType;
};

// create a high level object factory for the coreObject class
using fMap = std::map<std::string, std::shared_ptr<componentFactory>, std::less<>>;
/** @brief central location for building objects and storing factories for making all the gridDyn
 component core object Factory class  intended to be a singleton it contains a map from strings to
 typeFactories
*/
class coreObjectFactory {
  public:
    ~coreObjectFactory();

    /** @brief get a shared pointer to the core object factory*/
    static std::shared_ptr<coreObjectFactory> instance();

    /** @brief register a type factory with the coreObjectFactory
    @param[in] name the string identifier to the factory
    @param[in] componentFac the type factory to place in the map
    */
    void registerFactory(std::string_view name,
                         const std::shared_ptr<componentFactory>& componentFac);

    /** @brief register a type factory with the coreObjectFactory
    gets the name to use in the mapping from the type factory itself
    @param[in] componentFac the type factory to place in the map
    */
    void registerFactory(const std::shared_ptr<componentFactory>& componentFac);

    /** @brief get a listing of the factory names*/
    stringVec getFactoryNames();

    /** @brief get a listing of the type names available for a given factory*/
    stringVec getTypeNames(std::string_view component);

    /** @brief create the default object from a given component
    @param[in] component  the name of the category of objects
    @return the created coreObject */
    coreObject* createObject(std::string_view component);

    /** @brief create an object from a given objectType and typeName
    @param[in] component  the name of the category of objects
    @param[in] typeName  the specific type to create
    @return the created coreObject */
    coreObject* createObject(std::string_view component, std::string_view typeName);

    /** @brief create an object from a given objectType and typeName
    @param[in] component  the name of the category of objects
    @param[in] typeName  the specific type to create
    @param[in] objName  the name of the object to create
    @return the created coreObject */
    coreObject* createObject(std::string_view component,
                             std::string_view typeName,
                             std::string_view objName);

    /** @brief get a specific type factory
    @param[in] component the name of the typeFactory to get
    @return a shared pointer to a specific type Factory
    */
    std::shared_ptr<componentFactory> getFactory(std::string_view component);

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
                     coreObject* obj);

    /** @brief prepare a number of objects for use later so they can all be constructed at once of
    the default type for a given container
    @param[in] component the category of Object to create
    @param[in] numObjects  the number of objects to preallocate
    @param[in] obj the object to reference as the owner responsible for deleting the container
    */
    void prepObjects(std::string_view component, count_t numObjects, coreObject* obj);

  private:
    coreObjectFactory();

    fMap m_factoryMap;  //!< the main map from string to the typeFactory
};
}  // namespace griddyn
