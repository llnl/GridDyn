/*
 * Copyright (c) 2014-2026, Lawrence Livermore National Security
 * See the top-level NOTICE for additional details. All rights reserved.
 * SPDX-License-Identifier: BSD-3-Clause
 */

// test case for workQueue

#include "../gtestHelper.h"
#include "utilities/zipUtilities.h"
#include <filesystem>
#include <fstream>
#include <gtest/gtest.h>
#include <string>
#include <string_view>
#include <vector>

static constexpr std::string_view zipTestDirectory{GRIDDYN_TEST_DIRECTORY "/zip_tests/"};

static std::string makeZipTestPath(std::string_view fileName)
{
    return std::string{zipTestDirectory} + std::string{fileName};
}

TEST(ZipUtilitiesTests, Unzip)
{
    std::string file = makeZipTestPath("Rectifier.fmu");
    std::string directory = makeZipTestPath("Rectifier");
    int status = utilities::unzip(file, directory);
    EXPECT_EQ(status, 0);
    ASSERT_TRUE(std::filesystem::exists(directory));
    std::filesystem::remove_all(directory);
}

TEST(ZipUtilitiesTests, ZipRoundTrip)
{
    // make two files with very simple text
    int fileSize1 = 1000000;
    std::vector<char> zeroData(fileSize1, '0');
    std::string fileZeros = makeZipTestPath("zeros.txt");
    std::ofstream outZeros(fileZeros);
    outZeros.write(zeroData.data(), fileSize1);
    outZeros.close();
    int fileSize2 = 981421;
    std::vector<char> oneData(fileSize2, '1');
    std::string fileOnes = makeZipTestPath("ones.txt");
    std::ofstream outOnes(fileOnes);
    outOnes.write(oneData.data(), fileSize2);
    outOnes.close();
    // zip them up into a zip file
    auto zipfile = makeZipTestPath("data.zip");
    auto status = utilities::zip(zipfile, std::vector<std::string>{fileZeros, fileOnes});
    EXPECT_EQ(status, 0);
    ASSERT_TRUE(std::filesystem::exists(zipfile));

    // get the sizes of the original files
    auto filesize1 = std::filesystem::file_size(fileZeros);
    auto filesize2 = std::filesystem::file_size(fileOnes);

    auto zipsize = std::filesystem::file_size(zipfile);
    // make sure we compressed a lot
    EXPECT_LT(zipsize, (filesize1 + filesize2) / 40);

    // remove the files
    std::filesystem::remove(fileZeros);
    std::filesystem::remove(fileOnes);
    // extract them and recheck sizes
    status = utilities::unzip(zipfile, std::string{zipTestDirectory});
    EXPECT_EQ(status, 0);
    ASSERT_TRUE(std::filesystem::exists(fileZeros));
    ASSERT_TRUE(std::filesystem::exists(fileOnes));

    auto filesize1b = std::filesystem::file_size(fileZeros);
    auto filesize2b = std::filesystem::file_size(fileOnes);

    EXPECT_EQ(filesize1, filesize1b);
    EXPECT_EQ(filesize2, filesize2b);
    // remove all the files
    std::filesystem::remove(fileZeros);
    std::filesystem::remove(fileOnes);
    std::filesystem::remove(zipfile);
}
