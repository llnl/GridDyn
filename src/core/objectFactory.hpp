/*
 * Copyright (c) 2014-2020, Lawrence Livermore National Security
 * See the top-level NOTICE for additional details. All rights reserved.
 * SPDX-License-Identifier: BSD-3-Clause
 */

#ifndef GD_OBJECT_FACTORY_H_
#define GD_OBJECT_FACTORY_H_

#include "coreObject.h"
#include <map>
#include <memory>
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
    objectFactory(const std::string& component, std::string typeName);

    /** @brief constructor B
    @param[in] component  the name of component this factory is a constructor for
    @param[in] typeNames  the names of the specific types of objects this factor builds
    */
    objectFactory(const std::string& component, const stringVec& typeNames);

    /** @brief make and object   abstract function
    @return a pointer to a newly constructed object
    */
    virtual coreObject* makeObject() = 0;

    /** @brief make and object   abstract function
    @param[in] objectName  the name of the object to construct
    @return a pointer to a newly constructed object
    */

    virtual coreObject* makeObject(const std::string& objectName) = 0;
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

using cMap = std::map<std::string, objectFactory*>;

/** @brief a factory containing a mapping of specific object factories for a specific component
 */
class componentFactory {
  public:
    std::string name;  //!< name of the component
    componentFactory();
    explicit componentFactory(std::string component);
    ~componentFactory();
    stringVec getTypeNames();
    coreObject* makeObject(const std::string& type, const std::string& objectName);
    coreObject* makeObject(const std::string& type);
    coreObject* makeObject();
    void registerFactory(const std::string& typeName, objectFactory* oFac);
    void registerFactory(objectFactory* oFac);
    void setDefault(const std::string& type);
    bool isValidType(const std::string& typeName) const;
    objectFactory* getFactory(const std::string& typeName);

  protected:
    cMap m_factoryMap;
    std::string m_defaultType;
};

// create a high level object factory for the coreObject class
using fMap = std::map<std::string, std::shared_ptr<componentFactory>>;
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
    @param[in] tf the type factory to place in the map
    */
    void registerFactory(const std::string& name, const std::shared_ptr<componentFactory>& tf);

    /** @brief register a type factory with the coreObjectFactory
    gets the name to use in the mapping from the type factory itself
    @param[in] tf the type factory to place in the map
    */
    void registerFactory(const std::shared_ptr<componentFactory>& tf);

    /** @brief get a listing of the factory names*/
    stringVec getFactoryNames();

    /** @brief get a listing of the type names available for a given factory*/
    stringVec getTypeNames(const std::string& component);

    /** @brief create the default object from a given component
    @param[in] component  the name of the category of objects
    @return the created coreObject */
    coreObject* createObject(const std::string& component);

    /** @brief create an object from a given objectType and typeName
    @param[in] component  the name of the category of objects
    @param[in] typeName  the specific type to create
    @return the created coreObject */
    coreObject* createObject(const std::string& component, const std::string& typeName);

    /** @brief create an object from a given objectType and typeName
    @param[in] component  the name of the category of objects
    @param[in] typeName  the specific type to create
    @param[in] objName  the name of the object to create
    @return the created coreObject */
    coreObject* createObject(const std::string& component,
                             const std::string& typeName,
                             const std::string& objName);

    /** @brief get a specific type factory
    @param[in] component the name of the typeFactory to get
    @return a shared pointer to a specific type Factory
    */
    std::shared_ptr<componentFactory> getFactory(const std::string& component);

    /** @brief check if a specific object category is valid*/
    bool isValidObject(const std::string& component);

    /** @brief check if a specific component name is valid for a specific category of objects*/
    bool isValidType(const std::string& component, const std::string& typeName);

    /** @brief prepare a number of objects for use later so they can all be constructed at once
    @param[in] component the category of Object to create
    @param[in] typeName the specific type of object in reference
    @param[in] numObjects  the number of objects to preallocate
    @param[in] obj the object to reference as the owner responsible for deleting the container
    */
    void prepObjects(const std::string& component,
                     const std::string& typeName,
                     count_t numObjects,
                     coreObject* obj);

    /** @brief prepare a number of objects for use later so they can all be constructed at once of
    the default type for a given container
    @param[in] component the category of Object to create
    @param[in] numObjects  the number of objects to preallocate
    @param[in] obj the object to reference as the owner responsible for deleting the container
    */
    void prepObjects(const std::string& component, count_t numObjects, coreObject* obj);

  private:
    coreObjectFactory();

    fMap m_factoryMap;  //!< the main map from string to the typeFactory
};
}  // namespace griddyn
#endif
