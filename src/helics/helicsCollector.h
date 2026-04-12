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

#include "../griddyn/measurement/collector.h"
#include <memory>
#include <string>
#include <utility>
#include <vector>

namespace griddyn {
namespace helicsLib {
    class helicsCoordinator;

    class helicsCollector: public collector {
      private:
        class pubInformation {
          public:
            std::string colname;
            int32_t pubIndex = -1;
            bool vectorPub = false;
            pubInformation() = default;
            pubInformation(const std::string& cname, int32_t index, bool vectorPublish = false):
                colname(cname), pubIndex(index), vectorPub(vectorPublish)
            {
            }
        };
        enum class collectorPubType {
            as_individual = 0,
            as_vector = 1,
            as_string = 2,
        };
        collectorPubType pubType = collectorPubType::as_individual;
        std::vector<std::pair<std::string, std::string>> complexPairs;
        std::vector<std::string> cnames;
        std::vector<pubInformation> pubs;
        std::string pubName;
        int32_t mpubIndex = -1;
        helicsCoordinator* coord =
            nullptr;  //!< the coordinator for interaction with the helics interface
      public:
        helicsCollector(coreTime time0 = timeZero, coreTime period = timeOneSecond);
        explicit helicsCollector(const std::string& name);

        virtual std::unique_ptr<collector> clone() const override;
        virtual void cloneTo(collector* col) const override;
        virtual change_code trigger(coreTime time) override;

        void set(const std::string& param, double val) override;
        void set(const std::string& param, const std::string& val) override;

        virtual const std::string& getSinkName() const override;

      protected:
        void dataPointAdded(const collectorPoint& cp) override;

      private:
        /** function to find the fmi coordinator so we can connect to that*/
        void findCoordinator();
    };

}  // namespace helicsLib
}  // namespace griddyn
