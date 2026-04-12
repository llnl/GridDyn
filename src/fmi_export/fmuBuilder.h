/*
 * Copyright (c) 2014-2020, Lawrence Livermore National Security
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
    class fmiCoordinator;

    // class gridDynSimulation;
    /** object to build an FMI object from a gridDyn simulation file*/
    class fmuBuilder: public GriddynRunner {
      private:
        std::string fmuLoc;  //!< location to place the FMU
        std::vector<unsigned int> vrs;  //!< the value references
        coreOwningPtr<fmiCoordinator> coord_;  //!< coordinator to maintain organize everything
        std::unique_ptr<readerInfo> ri_;  //!< location of readerInfo
        std::string execPath;  //!< location of the executable making the fmu
        std::string platform = "all";  //!< target platform for the fmu
        bool keep_dir = false;
        /** private function for loading the subcomponents*/
        void loadComponents();
        void generateXML(const std::string& xmlfile);

      public:
        fmuBuilder();
        fmuBuilder(std::shared_ptr<gridDynSimulation> gds);
        virtual ~fmuBuilder();

      public:
        virtual std::shared_ptr<CLI::App>
            generateLocalCommandLineParser(readerInfo& ri) override final;

        /** build the FMU at the given location
    @param[in] fmuLocation optional argument to specify the location to build the FMU*/
        void MakeFmu(const std::string& fmuLocation = "");
        const std::string& getOutputFile() const { return fmuLoc; }

      private:
        void copySharedLibrary(const std::string& tempdir);
    };

}  // namespace fmi
}  // namespace griddyn
