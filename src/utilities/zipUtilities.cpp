/*
 * LLNS Copyright Start
 * Copyright (c) 2014-2018, Lawrence Livermore National Security
 * This work was performed under the auspices of the U.S. Department
 * of Energy by Lawrence Livermore National Laboratory in part under
 * Contract W-7405-Eng-48 and in part under Contract DE-AC52-07NA27344.
 * Produced at the Lawrence Livermore National Laboratory.
 * All rights reserved.
 * For details, see the L */

#include "zipUtilities.h"

#include <Minizip/miniunz.h>
#include <Minizip/minizip.h>
#include <filesystem>
#include <string>
#include <vector>

namespace utilities {
static const char* zipname = "minizip";
static const char* ziparg_overwrite = "-o";
static const char* ziparg_append = "-a";
static const char* ziparg2 = "-3";
static const char* ziparg3 = "-j";

int zip(const std::string& file, const std::vector<std::string>& filesToZip, zipMode mode)
{
#define NUMBER_FIXED_ARGS 5

    std::vector<char> fileV(file.c_str(),
                            file.c_str() + file.size() + 1u);  // 1u for /0 at end of string

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
                                  (mode == zipMode::overwrite) ? ziparg_overwrite : ziparg_append,
                                  ziparg2,
                                  ziparg3,
                                  fileV.data()};
    std::vector<std::vector<char>> filez(filesToZip.size());
    size_t argc = NUMBER_FIXED_ARGS + filesToZip.size();
    argv.resize(argc + 1, nullptr);

    /* need to copy over the arguments since theoretically minizip may modify the input arguments */
    for (size_t kk = 0; kk < filesToZip.size(); kk++) {
        filez[kk].assign(filesToZip[kk].c_str(),
                         filesToZip[kk].c_str() + filesToZip[kk].size() +
                             1u);  // 1u to copy the NULL at the end of the string
        argv[NUMBER_FIXED_ARGS + kk] = filez[kk].data();
    }
    /* minizip may change the current working directory */
    auto cpath = std::filesystem::current_path();
    /* Zip */
    int status = minizip(static_cast<int>(argc), argv.data());

    /* Reset the current directory */
    std::filesystem::current_path(cpath);

    return status;
}

void addToFileList(std::vector<std::filesystem::path>& files,
                   const std::filesystem::path& startpath)
{
    if (std::filesystem::is_directory(startpath)) {
        for (const auto& entry : std::filesystem::directory_iterator(startpath)) {
            if (std::filesystem::is_regular_file(entry)) {
                files.push_back(entry.path());
            } else if (std::filesystem::is_directory(entry)) {
                addToFileList(files, entry.path());
            }
        }
    }
}

int zipFolder(const std::string& file, const std::string& folderLoc, zipMode mode)
{
    std::filesystem::path dpath(folderLoc);
    if (!std::filesystem::is_directory(dpath)) {
        return -2;
    }
    /* we are changing the working directory */
    auto cpath = std::filesystem::current_path();

    std::filesystem::current_path(dpath);

    /** get all the files to add*/
    std::vector<std::filesystem::path> zfiles;

    addToFileList(zfiles, std::filesystem::current_path());

    for (auto& pth : zfiles) {
        pth = std::filesystem::relative(pth, dpath);
    }

    std::vector<char> fileV(file.c_str(),
                            file.c_str() + file.size() + 1u);  // 1u for /0 at end of string

    std::vector<const char*> argv{zipname,
                                  (mode == zipMode::overwrite) ? ziparg_overwrite : ziparg_append,
                                  ziparg2,
                                  fileV.data()};
    std::vector<std::vector<char>> filez(zfiles.size());
    size_t argc = 4 + zfiles.size();
    argv.resize(argc + 1, nullptr);

    /* need to copy over the arguments since theoretically minizip may modify the input arguments */
    for (size_t kk = 0; kk < zfiles.size(); kk++) {
        auto filestr = zfiles[kk].string();
        filez[kk].assign(filestr.c_str(),
                         filestr.c_str() + filestr.size() +
                             1u);  // 1u to copy the NULL at the end of the string
        argv[4 + kk] = filez[kk].data();
    }

    /* Zip */
    int status = minizip(static_cast<int>(argc), argv.data());

    /* Reset the current directory */
    std::filesystem::current_path(cpath);
    return status;
}

static const char* unzipname = "miniunz";
static const char* unziparg1 = "-x";
static const char* unziparg2 = "-o";
static const char* unziparg4 = "-d";

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

    std::vector<char> fileV(file.c_str(),
                            file.c_str() + file.size() + 1u);  // 1u for /0 at end of string
    std::vector<char> dirV(directory.c_str(), directory.c_str() + directory.size() + 1u);
    std::vector<const char*> argv{unzipname, unziparg1, unziparg2, fileV.data()};

    if (!directory.empty()) {
        argc = 6;
        argv.resize(6);
        argv[4] = unziparg4;
        argv[5] = dirV.data();

        if (!std::filesystem::exists(directory)) {
            std::filesystem::create_directories(directory);
            if (!std::filesystem::exists(directory)) {
                return (-3);
            }
        }
    }

    /* minunz may change the current working directory */
    auto cpath = std::filesystem::current_path();

    /* Unzip */
    int status = miniunz(argc, argv.data());

    /* Reset the current directory */
    std::filesystem::current_path(cpath);

    return status;
}

}  // namespace utilities
