/*
 * Copyright (c) 2014-2026, Lawrence Livermore National Security
 * See the top-level NOTICE for additional details. All rights reserved.
 * SPDX-License-Identifier: BSD-3-Clause
 */

#pragma once

#include "core/coreOwningPtr.hpp"
#include "runner/gridDynRunner.h"
#include <memory>
#include <string>
#include <vector>

namespace griddyn {
class readerInfo;

namespace fmi {
    class FmiCoordinator;

    // class gridDynSimulation;
    /** object to build an FMI object from a gridDyn simulation file*/
    class FmuBuilder: public GriddynRunner {
      private:
        std::string mFmuLocation;  //!< location to place the FMU
        std::vector<unsigned int> mValueReferences;  //!< the value references
        coreOwningPtr<FmiCoordinator>
            mCoordinator;  //!< coordinator to maintain organize everything
        std::unique_ptr<readerInfo> mReaderInfo;  //!< location of readerInfo
        std::string mExecutablePath;  //!< location of the executable making the fmu
        std::string mPlatform = "all";  //!< target platform for the fmu
        bool mKeepDirectory = false;
        /** private function for loading the subcomponents*/
        void loadComponents();
        void generateXML(const std::string& xmlfile);

      public:
        FmuBuilder();
        FmuBuilder(std::shared_ptr<gridDynSimulation> gds);
        virtual ~FmuBuilder();

      public:
        virtual std::shared_ptr<CLI::App>
            generateLocalCommandLineParser(readerInfo& readerInformation) override final;

        /** build the FMU at the given location
    @param[in] fmuLocation optional argument to specify the location to build the FMU*/
        void MakeFmu(const std::string& fmuLocation = "");
        const std::string& getOutputFile() const { return mFmuLocation; }

      private:
        void copySharedLibrary(const std::string& tempdir);
    };

}  // namespace fmi
}  // namespace griddyn
