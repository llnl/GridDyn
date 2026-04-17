#pragma once

/*
 * LLNS Copyright Start
 * Copyright (c) 2017, Lawrence Livermore National Security
 * This work was performed under the auspices of the U.S. Department
 * of Energy by Lawrence Livermore National Laboratory in part under
 * Contract W-7405-Eng-48 and in part under Contract DE-AC52-07NA27344.
 * Produced at the Lawrence Livermore National Laboratory.
 * All rights reserved.
 * For details, see the LICENSE file.
 * LLNS Copyright End
 */
#include "core/coreOwningPtr.hpp"
#include "runner/gridDynRunner.h"
#include <memory>

namespace griddyn {
class readerInfo;
namespace dimeLib {
    /** dimeRunner is the execution object for executing in coordination with the Dime co-simulation
environment it inherits from gridDynRunner and adds some extra features necessary for executing with
dime
*/
    class dimeRunner: public GriddynRunner {
      private:
        // coreOwningPtr<dimeCoordinator> coord_; //!< the coordinator object for managing object
        // that manage the HELICS coordination
      public:
        dimeRunner();
        dimeRunner(std::shared_ptr<gridDynSimulation> sim);
        ~dimeRunner();

      public:
        virtual std::shared_ptr<CLI::App> generateLocalCommandLineParser(readerInfo& ri) override;

        virtual coreTime Run(void) override;

        virtual coreTime Step(coreTime time) override;

        virtual void Finalize(void) override;
    };

}  // namespace dimeLib
}  // namespace griddyn
