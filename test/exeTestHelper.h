/*
 * Copyright (c) 2014-2020, Lawrence Livermore National Security
 * See the top-level NOTICE for additional details. All rights reserved.
 * SPDX-License-Identifier: BSD-3-Clause
 */

#pragma once

#include <string>
/** class designed to execute a run test of gridDynMain*/
class exeTestRunner {
  private:
    std::string exeString;
    bool active;
    static int counter;
    std::string outFile;

  public:
    exeTestRunner();
    exeTestRunner(const std::string& baseLocation, const std::string& target);
    exeTestRunner(const std::string& baseLocation,
                  const std::string& baseLocation2,
                  const std::string& target);
    bool findFileLocation(const std::string& baseLocation, const std::string& target);
    bool isActive() const { return active; }

    int run(const std::string& args) const;

    std::string runCaptureOutput(const std::string& args) const;
    const std::string& getExeString() const { return exeString; }

  private:
    void buildOutFile();
};
