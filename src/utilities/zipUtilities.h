/*
 * LLNS Copyright Start
 * Copyright (c) 2014-2018, Lawrence Livermore National Security
 * This work was performed under the auspices of the U.S. Department
 * of Energy by Lawrence Livermore National Laboratory in part under
 * Contract W-7405-Eng-48 and in part under Contract DE-AC52-07NA27344.
 * Produced at the Lawrence Livermore National Laboratory.
 * All rights reserved.
 * For details, see the L */

#pragma once

#include <string>
#include <vector>

namespace utilities {
/** enumeration describing whether the zip functions should overwrite or append*/
enum class zipMode {
    overwrite,  //!< any existing file should be erased
    append  //!< an existing file should be added to
};
/** zip a set of files into a zip file
@param[in] file the name of the file to zip the specified files into
@param[in] filesToZip  a list of files to zip
@param[in] mode  (optional) set to zipMode::overwrite to overwrite any existing file zipMode::append
to append, defaults to overwrite
@return 0 on success an error code otherwise
*/
int zip(const std::string& file,
        const std::vector<std::string>& filesToZip,
        zipMode mode = zipMode::overwrite);
/** zip a folder into a zip file
@param[in] file the name of the file to zip the specified files into
@param[in] folderLoc the folder to zip
@param[in] mode  (optional) set to zipMode::overwrite to overwrite any existing file zipMode::append
to append, defaults to overwrite
@return 0 on success an error code otherwise
*/
int zipFolder(const std::string& file,
              const std::string& folderLoc,
              zipMode mode = zipMode::overwrite);

/** unzip a file into the specified location
@param[in] file the name of the file to unzip
@param[in] directory the location to unzip the file relative to
@return 0 on success an error code otherwise
*/
int unzip(const std::string& file, const std::string& directory = "");
}  // namespace utilities
