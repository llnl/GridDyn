/*
 * Copyright (c) 2014-2020, Lawrence Livermore National Security
 * See the top-level NOTICE for additional details. All rights reserved.
 * SPDX-License-Identifier: BSD-3-Clause
 */

#include "exeTestHelper.h"

#include <cstdlib>
#include <fstream>
#include <streambuf>

#include <filesystem>

int exeTestRunner::counter = 1;

exeTestRunner::exeTestRunner()
{
    ++counter;
    buildOutFile();
}

exeTestRunner::exeTestRunner(const std::string& baseLocation, const std::string& target)
{
    ++counter;
    buildOutFile();
    active = findFileLocation(baseLocation, target);
    if (!(system(nullptr))) {
        active = false;
    }
}

exeTestRunner::exeTestRunner(const std::string& baseLocation,
                             const std::string& baseLocation2,
                             const std::string& target)
{
    ++counter;
    buildOutFile();
    active = findFileLocation(baseLocation, target);
    if (!active) {
        active = findFileLocation(baseLocation2, target);
    }
    if (!(system(nullptr))) {
        active = false;
    }
}

void exeTestRunner::buildOutFile()
{
    auto pth = std::filesystem::temp_directory_path();
    pth /= ("exeText_" + std::to_string(counter) + ".out");
    outFile = pth.string();
}

bool exeTestRunner::findFileLocation(const std::string& baseLocation, const std::string& target)
{
    std::filesystem::path sourcePath(baseLocation);

    auto tryPath1 = sourcePath / target;
    if (std::filesystem::exists(tryPath1)) {
        exeString = tryPath1.string();
        return true;
    }

    auto tryPath2 = sourcePath / (target + ".exe");
    if (std::filesystem::exists(tryPath2)) {
        exeString = tryPath2.string();
        return true;
    }
#ifndef NDEBUG
    auto tryPathD1 = sourcePath / "Debug" / target;
    if (std::filesystem::exists(tryPathD1)) {
        exeString = tryPathD1.string();
        return true;
    }

    auto tryPathD2 = sourcePath / "Debug" / (target + ".exe");
    if (std::filesystem::exists(tryPathD2)) {
        exeString = tryPathD2.string();
        return true;
    }
#endif
    auto tryPathR1 = sourcePath / "Release" / target;
    if (std::filesystem::exists(tryPathR1)) {
        exeString = tryPathR1.string();
        return true;
    }

    auto tryPathR2 = sourcePath / "Release" / (target + ".exe");
    if (std::filesystem::exists(tryPathR2)) {
        exeString = tryPathR2.string();
        return true;
    }
    auto tryPathbin1 = sourcePath / "bin" / target;
    if (std::filesystem::exists(tryPathbin1)) {
        exeString = tryPathbin1.string();
        return true;
    }

    auto tryPathbin2 = sourcePath / "bin" / (target + ".exe");
    if (std::filesystem::exists(tryPathbin2)) {
        exeString = tryPathbin2.string();
        return true;
    }

    std::filesystem::path tryPatht1 = target;
    if (std::filesystem::exists(tryPatht1)) {
        exeString = tryPatht1.string();
        return true;
    }

    std::filesystem::path tryPatht2 = (target + ".exe");
    if (std::filesystem::exists(tryPatht2)) {
        exeString = tryPatht2.string();
        return true;
    }
    return false;
}

int exeTestRunner::run(const std::string& args) const
{
    if (!active) {
        return -101;
    }
    std::string rstr = exeString + " " + args;
    return system(rstr.c_str());
}

std::string exeTestRunner::runCaptureOutput(const std::string& args) const
{
    if (!active) {
        return "invalid executable";
    }
    std::string rstr = exeString + " " + args + " > " + outFile;
    // printf ("string %s\n", rstr.c_str ());
    auto out = system(rstr.c_str());

    // printf (" after system call string %s\n", rstr.c_str ());
    std::ifstream t(outFile);
    std::string str((std::istreambuf_iterator<char>(t)), std::istreambuf_iterator<char>());
    if (out != 0) {
        str.push_back('\n');
        str.append(exeString);
        str.append(" returned ");
        str.append(std::to_string(out));
        str.push_back('\n');
    }

    remove(outFile.c_str());
    return str;
}
