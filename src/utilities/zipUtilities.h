/*
 * Copyright (c) 2014-2026, Lawrence Livermore National Security
 * See the top-level NOTICE for additional details. All rights reserved.
 * SPDX-License-Identifier: BSD-3-Clause
 */

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
