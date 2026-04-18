/*
 * Copyright (c) 2014-2026, Lawrence Livermore National Security
 * See the top-level NOTICE for additional details. All rights reserved.
 * SPDX-License-Identifier: BSD-3-Clause
 */

#pragma once

// forward declarations
class readerElement;

class gridGrabberInfo;

#include "gridParameter.h"
#include <cstdint>
#include <list>
#include <map>
#include <memory>
#include <string>
#include <string_view>
#include <tuple>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>

namespace griddyn {
class collector;
class Event;
class coreObject;

/** @brief class containing some basic information for reading power system files*/
class basicReaderInfo {
  public:
    double base = 100.0;  //!< base power in MW
    double basefreq = 60.0;  //!< base frequency in Hz
  private:
    uint32_t flags = 0;  //!< any reader flags
  public:
    int version = 0;  //!< file version info
    std::string prefix;  //!< prefix to add to the names of imported objects

    /** @brief virtual destructor*/
    virtual ~basicReaderInfo() = default;
    void setFlag(int flagID);
    bool checkFlag(int flagID) const
    {
        return (flagID < 32) ? ((flags & (1 << flagID)) != 0) : false;
    }
    void setAllFlags(uint32_t newFlags) { flags = newFlags; }
    uint32_t getFlags() const { return flags; }
};

const basicReaderInfo defInfo = basicReaderInfo();

using ignoreListType = std::unordered_set<std::string>;
/** @brief a class defining information to help manage the inputs files
 */
class readerInfo: public basicReaderInfo {
  public:
    std::vector<std::shared_ptr<collector>> collectors;  //!< stores the active recorders
    std::vector<std::shared_ptr<Event>> events;  //!< store the captured events
    bool keepdefines = false;
    bool captureFiles = false;
    using scopeID = std::uint64_t;

  private:
    std::unordered_map<std::string, std::string> defines;  //!< storages for string definitions
    std::vector<std::string> directories;  //!< stores a list of folders for finding files
    std::vector<std::string> capturedFiles;  //!< store a list of all the files used
    std::unordered_map<std::string, std::string>
        objectTranslations;  //!< storage for object Translations
    std::unordered_map<std::string, std::string>
        objectTranslationsType;  //!< storage for the type associated with an object translation
    std::unordered_map<std::string, std::pair<coreObject*, std::vector<gridParameter>>>
        library;  //!< library objects
    std::unordered_map<std::string, std::string> lockDefines;  //!< locked definitions
    std::unordered_map<std::string, std::pair<std::shared_ptr<readerElement>, int>>
        customElements;  //!< custom objects
    scopeID currentScope = 0;  //!< identification of the current scope
    std::vector<std::tuple<scopeID, std::string, bool, std::string>>
        scopedDefinitions;  // container for the scoped definitions so they can be removed when the
                            // scope changes
    std::vector<scopeID> directoryScope;  //!< the scope of the listed directories
    ignoreListType parameterIgnoreStrings;  //!< parameters to ignore
    coreObject* keyObj = nullptr;  //!< placeholder for storage of a keyObject
  public:
    /** @brief default constructor*/
    readerInfo();
    /** @brief construct a reader info object from a basic readerInfo object
    @param[in] bri  a basic reader info object
    */
    explicit readerInfo(basicReaderInfo const& bri);
    /**@brief destructor*/
    ~readerInfo();
    /** @brief add an object to the library
    @param[in] obj  the object to add
    @param[in] pobjs a set of parameter objects to apply to newly created objects
    @return true if object was successfully added
    */
    bool addLibraryObject(coreObject* obj, std::vector<gridParameter>& pobjs);
    /** @brief  create an object based on a reference object in the library
    @param[in] objName  the name of the object in the library to find
    @param[in] mobj set to nullptr to create a new object, if set the library object is cloned onto
    the given object
    @return a pointer to the new object created/or cloned from the library object
    */
    coreObject* makeLibraryObject(std::string_view objName, coreObject* mobj);
    /** @brief get a pointer to a library object
    @param[in] objName  the name of the object in the library to locate
    @return nullptr if no object found or a pointer to the library object
    */
    coreObject* findLibraryObject(std::string_view objName) const;

    /** @brief  find a collector stored in the readerInfo either by name or by sink location
    *@param[in] name  the name of the collector to find
    @param[in] fileName  the sink name of the collector to find by file name
    @return a shared pointer to the collector or nullptr if not found
    */
    std::shared_ptr<collector> findCollector(std::string_view name, std::string_view fileName);
    /** get the keyObject*/
    coreObject* getKeyObject() { return keyObj; }
    /** set the keyObject*/
    void setKeyObject(coreObject* obj) { keyObj = obj; }
    /** @brief change the scope for definitions and translations
    @return the new scopeID for closing the scope later */
    scopeID newScope();

    /** @brief change the scope for definitions and translations
    @param[in] scopeToClose the scopeID for closing*/
    void closeScope(scopeID scopeToClose = 0);

    /** @brief  add a definition to the reader info object
      the definition will only be added or updated if there is no locked definition of the same name
    @param[in] def the string that has a translation
    @param[in] replacement  the string to replace def with
    */
    void addDefinition(const std::string& def, const std::string& replacement);

    /** @brief  add a definition to the reader info object
      the definition will only be added or updated if there is no locked definition of the same name
    @param[in] def the string that has a translation
    @param[in] replacement  the string to replace def with
    */
    void addLockedDefinition(const std::string& def, const std::string& replacement);

    /** @brief  add or replace a locked definition
      this function will replace any existing locked definition with a new one
    @param[in] def the string that has a translation
    @param[in] replacement  the string to replace def with
    */
    void replaceLockedDefinition(const std::string& def, const std::string& replacement);

    /** @brief  add a name translation
     name translations are one to one translations of object names for declaring new names for
    convenience
    @param[in] def the object name needing translation
    @param[in] component the underlying component name usually one with a defined reader
    */
    void addTranslate(const std::string& def, const std::string& component);

    /** @brief  add a name translation with a specific type
     name translations are one to one translations of object names for declaring new names for
    convenience and can include a specific type of the component along with it
    @param[in] def the object name needing translation
    @param[in] component the underlying component name usually one with a defined reader
    @param[in] type the type of the object to go along with the component
    */
    void
        addTranslate(const std::string& def, const std::string& component, const std::string& type);

    /** @brief  add a type specification associated with a component
     name translations are one to one translations of object names for declaring new names for
    convenience
    @param[in] def the object name needing translation
    @param[in] type the type associated with a given definition
    */
    void addTranslateType(const std::string& def, const std::string& type);

    /** @brief  add a custom object to the reader Info
     unlike library objects a pointer to the actual XML code is stored and reread to create an
    object allowing for creation of complex XML structures as a library
    @param[in] name the name of the custom object
    @param[in] element  a pointer to the xml element with the object creation information
      @param[in] nargs the number of arguments the custom element has
    */
    void addCustomElement(const std::string& name,
                          const std::shared_ptr<readerElement>& element,
                          int nargs);

    /** @brief  check if a string represents a custom object
    @param[in] name the name of the supposed custom object
    @return true if the string names a custom object
    */
    bool isCustomElement(const std::string& name) const;

    /** @brief  retrieve a custom object
    @param[in] name the name of the supposed custom object
    @return a pair with a shared pointer to the custom object and an integer representing the number
    or arguments
    */
    std::pair<std::shared_ptr<readerElement>, int> getCustomElement(const std::string& name) const;

    /** @brief check and translate a string for definitions
     does a direct translation for any strings also interprets and translates to a string anything
    contained within pairs of $
    @param[in] input  the input string to translate
    @return  the translated string
    */
    std::string checkDefines(const std::string& input);

    /** @brief check and translate an object name
     does a direct translation of any previously defined names to translate
    @param[in] input  the input string to translate
    @return  the translated object name
    */
    std::string objectNameTranslate(const std::string& input);

    /** @brief add directory*/
    void addDirectory(const std::string& directory);

    /** @brief check that a file parameter is valid
    @param[in,out] strVal  the file to check and output an updated file location
    @param[in] extra_find if the file cannot be found on the first check, ignore the path
    information and check again for just the fileName in known locations
    @return true if the file was changed false otherwise
    */
    bool checkFileParam(std::string& strVal, bool extra_find = false);

    /** @brief check if a directory is valid
    @param[in,out] strVal  the directory to check and output an updated location
    @return true if the file was changed false otherwise
    */
    bool checkDirectoryParam(std::string& strVal);
    /** @brief get a container of all the strings which should be interpreted as blocks instead of
     * parameters
     */
    const ignoreListType& getIgnoreList() const;

    /** @brief get the list of captured files
    @return a reference to the captured file list*/
    const std::vector<std::string>& getCapturedFiles() const { return capturedFiles; }

  private:
    /** @brief load some default defines such as %time, %date, and %datetime */
    void loadDefaultDefines();
};

}  // namespace griddyn
