/*
 * LLNS Copyright Start
 * Copyright (c) 2014-2018, Lawrence Livermore National Security
 * This work was performed under the auspices of the U.S. Department
 * of Energy by Lawrence Livermore National Laboratory in part under
 * Contract W-7405-Eng-48 and in part under Contract DE-AC52-07NA27344.
 * Produced at the Lawrence Livermore National Laboratory.
 * All rights reserved.
 * For details, see the LICENSE file.
 * LLNS Copyright End
 */
#pragma once
#include "../core/coreOwningPtr.hpp"
#include "../runner/gridDynRunner.h"
#include <memory>

namespace helics {
class Federate;
}

namespace griddyn {
class readerInfo;
namespace helicsLib {
    class helicsCoordinator;

    /** helicsRunner is the execution object for executing in coordination with the Helics
co-simulation environment it inherits from gridDynRunner and adds some extra features necessary for
executing with helics
*/
    class helicsRunner: public GriddynRunner {
      private:
        coreOwningPtr<helicsCoordinator> coord_;  //!< the coordinator object for managing object
                                                  //!< that manage the HELICS coordination
        std::shared_ptr<helics::Federate> fed_;  //!< pointer to the helics federate object
      public:
        helicsRunner();
        explicit helicsRunner(std::shared_ptr<gridDynSimulation> sim);
        ~helicsRunner();

      public:
        virtual std::shared_ptr<CLI::App> generateLocalCommandLineParser(readerInfo& ri) override;

        virtual void simInitialize() override;
        virtual coreTime Run() override;

        virtual coreTime Step(coreTime time) override;

        virtual void Finalize() override;
    };

}  // namespace helicsLib
}  // namespace griddyn
