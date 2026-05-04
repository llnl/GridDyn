/*
 * Copyright (c) 2014-2026, Lawrence Livermore National Security
 * See the top-level NOTICE for additional details. All rights reserved.
 * SPDX-License-Identifier: BSD-3-Clause
 */

#pragma once

/** @file
define some functions and operations for configuring file reader operations and loading files
*/
#include "../griddyn/gridDynDefinitions.hpp"
#include "readerInfo.h"
#include <memory>
#include <string>

namespace griddyn {
class Event;
class Recorder;

class coreObject;

class gridDynSimulation;

#define READER_VERBOSE_PRINT 3
#define READER_NORMAL_PRINT 2
#define READER_SUMMARY_PRINT 1
#define READER_NO_PRINT 0

#define READER_WARN_ALL 2
#define READER_WARN_IMPORTANT 1
#define READER_WARN_NONE 0
/** enumeration of the possible xml readers*/
enum class xmlreader {
    default_reader,
    tinyxml,
    tinyxml2,
};
/** namespace for configuring various options about filereaders such as xml or json*/
namespace readerConfig {
    extern int printMode;  //!< the print level
    extern int warnMode;  //!< the warning mode
    extern int warnCount;  //!< total count of the warnings
    /** set the printMode to a particular level the higher the level the more gets printed*/
    void setPrintMode(int level);
    /** set the warning mode to a specific level*/
    void setWarnMode(int level);
    /** set the printMode via a string
@details "none,summary,normal, verbose"*/
    void setPrintMode(const std::string& level);
    /** set the warning mode to a specific level via a string
@details "none,important,normal"*/
    void setWarnMode(const std::string& level);
    /** set the case matching mode
@details can be "exact,  capital, or any  capital checks a few possible matches for capitalization
*/
    void setDefaultMatchType(const std::string& matchType);
    /** @brief enumeration describing how the matching should be done
     */
    enum class match_type {
        strict_case_match,  //!< match only the cases given
        capital_case_match,  //!< match where the first letter can be either case, or all lower
                             //!< case, or all capitals
        any_case_match,  //!< match where any letter can be any case

    };

    constexpr double PI = 3.141592653589793;
    extern match_type defMatchType;  //!< control for how names are matches in the xm
}  // namespace readerConfig

enum readerflags {

};
/** @brief defined flags for the readerInfo*/
enum readerFlags {
    ignore_step_up_transformer = 1,  //!< ignore any step up transformer definitions
    assume_powerflow_only =
        4,  //!< specify that some object construction may assume it will never be used for dynamics
    no_generator_bus_voltage_reset =
        5,  //!< do not use generator specification to alter bus voltages

};

std::unique_ptr<gridDynSimulation> readSimXMLFile(const std::string& fileName,
                                                  readerInfo* readerInfoPtr = nullptr,
                                                  xmlreader rtype = xmlreader::default_reader);

void addflags(basicReaderInfo& bri, const std::string& flags);

void loadFile(std::unique_ptr<gridDynSimulation>& gds,
              const std::string& fileName,
              readerInfo* readerInf = nullptr,
              const std::string& ext = "");

void loadFile(coreObject* parentObject,
              const std::string& fileName,
              readerInfo* readerInf = nullptr,
              std::string ext = "");

void loadGDZ(coreObject* parentObject, const std::string& fileName, readerInfo& ri);

void loadCDF(coreObject* parentObject,
             const std::string& fileName,
             const basicReaderInfo& bri = defInfo);

void loadPSP(coreObject* parentObject,
             const std::string& fileName,
             const basicReaderInfo& bri = defInfo);
void loadPTI(coreObject* parentObject,
             const std::string& fileName,
             const basicReaderInfo& bri = defInfo);

void loadRAW(coreObject* parentObject,
             const std::string& fileName,
             const basicReaderInfo& bri = defInfo);

void loadDYR(coreObject* parentObject,
             const std::string& fileName,
             const basicReaderInfo& bri = defInfo);
void loadEPC(coreObject* parentObject,
             const std::string& fileName,
             const basicReaderInfo& bri = defInfo);

// wrapper function to detect m file format for matpower or PSAT
void loadMFile(coreObject* parentObject,
               const std::string& fileName,
               const basicReaderInfo& bri = defInfo);

void loadCSV(coreObject* parentObject,
             const std::string& fileName,
             readerInfo& ri,
             const std::string& oname = "");

/** function sets a parameter in an object
@param[in] label the name to be printed if there is a problem
@param[in] obj  the object to change the parameter of
@param[in] param a gridParameter definition
@return 0 if successful (-1) if the setting failed
*/
int objectParameterSet(const std::string& label, coreObject* obj, gridParameter& param) noexcept;

void addToParent(coreObject* objectToAdd, coreObject* parentObject);
/** @brief attempt to add to a parent object with renaming sequence*/
void addToParentRename(coreObject* objectToAdd, coreObject* parentObject);

}  // namespace griddyn
