/*
 * Copyright (c) 2014-2026, Lawrence Livermore National Security
 * See the top-level NOTICE for additional details. All rights reserved.
 * SPDX-License-Identifier: BSD-3-Clause
 */

#include "zipUtilities.h"

#include <Minizip/miniunz.h>
#include <Minizip/minizip.h>
#include <filesystem>
#include <string>
#include <vector>

namespace utilities {
static constexpr char zipname[] = "minizip";
static constexpr char zipargOverwrite[] = "-o";
static constexpr char zipargAppend[] = "-a";
static constexpr char zipargCompression[] = "-3";
static constexpr char zipargJunkPath[] = "-j";

int zip(const std::string& file, const std::vector<std::string>& filesToZip, zipMode mode)
{
#define NUMBER_FIXED_ARGS 5

    std::vector<char> fileBuffer(file.c_str(),
                                 file.c_str() + file.size() + 1U);  // 1U for /0 at end of string

    /* Input arguments to the corresponding minizip main() function call */
    /*
    Usage : minizip [-o] [-a] [-0 to -9] [-p password] [-j] file.zip [files_to_add]

    -o  Overwrite existing file.zip
    -a  Append to existing file.zip
    -0  Store only
    -1  Compress faster
    -9  Compress better

    -j  exclude path. store only the file name.
    */
    std::vector<const char*> argv{zipname,
                                  (mode == zipMode::overwrite) ? zipargOverwrite : zipargAppend,
                                  zipargCompression,
                                  zipargJunkPath,
                                  fileBuffer.data()};
    std::vector<std::vector<char>> fileBuffers(filesToZip.size());
    const size_t argc = NUMBER_FIXED_ARGS + filesToZip.size();
    argv.resize(argc + 1, nullptr);

    /* need to copy over the arguments since theoretically minizip may modify the input arguments */
    for (size_t kk = 0; kk < filesToZip.size(); kk++) {
        fileBuffers[kk].assign(filesToZip[kk].c_str(),
                               filesToZip[kk].c_str() + filesToZip[kk].size() +
                                   1U);  // 1U to copy the NULL at the end of the string
        argv[NUMBER_FIXED_ARGS + kk] = fileBuffers[kk].data();
    }
    /* minizip may change the current working directory */
    auto currentPath = std::filesystem::current_path();
    /* Zip */
    const int status = minizip(static_cast<int>(argc), argv.data());

    /* Reset the current directory */
    std::filesystem::current_path(currentPath);

    return status;
}

static void addToFileList(std::vector<std::filesystem::path>& files,
                          const std::filesystem::path& startpath)
{
    if (!std::filesystem::is_directory(startpath)) {
        return;
    }

    std::vector<std::filesystem::path> directories{startpath};
    while (!directories.empty()) {
        const auto currentPath = directories.back();
        directories.pop_back();
        for (const auto& entry : std::filesystem::directory_iterator(currentPath)) {
            if (std::filesystem::is_regular_file(entry)) {
                files.push_back(entry.path());
            } else if (std::filesystem::is_directory(entry)) {
                directories.push_back(entry.path());
            }
        }
    }
}

int zipFolder(const std::string& file, const std::string& folderLoc, zipMode mode)
{
    const std::filesystem::path directoryPath(folderLoc);
    if (!std::filesystem::is_directory(directoryPath)) {
        return -2;
    }
    /* we are changing the working directory */
    auto currentPath = std::filesystem::current_path();

    std::filesystem::current_path(directoryPath);

    /** get all the files to add*/
    std::vector<std::filesystem::path> zippedFiles;

    addToFileList(zippedFiles, std::filesystem::current_path());

    for (auto& path : zippedFiles) {
        path = std::filesystem::relative(path, directoryPath);
    }

    std::vector<char> fileBuffer(file.c_str(),
                                 file.c_str() + file.size() + 1U);  // 1U for /0 at end of string

    std::vector<const char*> argv{zipname,
                                  (mode == zipMode::overwrite) ? zipargOverwrite : zipargAppend,
                                  zipargCompression,
                                  fileBuffer.data()};
    std::vector<std::vector<char>> fileBuffers(zippedFiles.size());
    const size_t argc = 4 + zippedFiles.size();
    argv.resize(argc + 1, nullptr);

    /* need to copy over the arguments since theoretically minizip may modify the input arguments */
    for (size_t kk = 0; kk < zippedFiles.size(); kk++) {
        auto fileString = zippedFiles[kk].string();
        fileBuffers[kk].assign(fileString.c_str(),
                               fileString.c_str() + fileString.size() +
                                   1U);  // 1U to copy the NULL at the end of the string
        argv[4 + kk] = fileBuffers[kk].data();
    }

    /* Zip */
    const int status = minizip(static_cast<int>(argc), argv.data());

    /* Reset the current directory */
    std::filesystem::current_path(currentPath);
    return status;
}

static constexpr char unzipname[] = "miniunz";
static constexpr char unzipargExtract[] = "-x";
static constexpr char unzipargOverwrite[] = "-o";
static constexpr char unzipargDirectory[] = "-d";

int unzip(const std::string& file, const std::string& directory)
{
    /*
    Usage : miniunz [-e] [-x] [-v] [-l] [-o] [-p password] file.zip [file_to_extr.] [-d extractdir]
    -e  Extract without pathname (junk paths)
    -x  Extract with pathname
    -v  list files
    -l  list files
    -d  directory to extract into
    -o  overwrite files without prompting
    -p  extract encrypted file using password
    */

    int argc = 4;

    std::vector<char> fileBuffer(file.c_str(),
                                 file.c_str() + file.size() + 1U);  // 1U for /0 at end of string
    std::vector<char> directoryBuffer(directory.c_str(), directory.c_str() + directory.size() + 1U);
    std::vector<const char*> argv{unzipname, unzipargExtract, unzipargOverwrite, fileBuffer.data()};

    if (!directory.empty()) {
        argc = 6;
        argv.resize(6);
        argv[4] = unzipargDirectory;
        argv[5] = directoryBuffer.data();

        if (!std::filesystem::exists(directory)) {
            std::filesystem::create_directories(directory);
            if (!std::filesystem::exists(directory)) {
                return (-3);
            }
        }
    }

    /* minunz may change the current working directory */
    auto currentPath = std::filesystem::current_path();

    /* Unzip */
    const int status = miniunz(argc, argv.data());

    /* Reset the current directory */
    std::filesystem::current_path(currentPath);

    return status;
}

}  // namespace utilities
