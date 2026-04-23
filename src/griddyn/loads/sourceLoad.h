/*
 * Copyright (c) 2014-2026, Lawrence Livermore National Security
 * See the top-level NOTICE for additional details. All rights reserved.
 * SPDX-License-Identifier: BSD-3-Clause
 */

#pragma once

#include "zipLoad.h"
#include <array>
#include <string>
#include <vector>
namespace griddyn {
class Source;
namespace loads {
    /** @brief a load that uses sources to calculate the values for the each of the load parameters
eventually will replace most of the shaped loads*/
    class sourceLoad: public zipLoad {
      public:
        enum sourceLoc {
            p_source = 0,
            q_source = 1,
            yp_source = 2,
            yq_source = 3,
            ip_source = 4,
            iq_source = 5,
            r_source = 6,
            x_source = 7,
        };
        enum class sourceType {
            other,
            pulse,
            sine,
            random,
        };

      private:
        std::vector<Source*> sources;
        std::array<int, 8> sourceLink;  // source lookups for the values
        sourceType sType = sourceType::other;

      public:
        explicit sourceLoad(const std::string& objName = "sourceLoad_$");
        sourceLoad(sourceType type, const std::string& objName = "sourceLoad_$");
        virtual coreObject* clone(coreObject* obj = nullptr) const override;

        virtual void add(coreObject* obj) override;
        virtual void add(Source* src);
        virtual void remove(coreObject* obj) override;
        virtual void remove(Source* src);

        virtual void set(std::string_view param, std::string_view val) override;
        virtual void set(std::string_view param,
                         double val,
                         units::unit unitType = units::defunit) override;
        virtual void setFlag(std::string_view flag, bool val = true) override;

        virtual void pFlowObjectInitializeA(coreTime time0, std::uint32_t flags) override;
        virtual void dynObjectInitializeA(coreTime time0, std::uint32_t flags) override;

        virtual void updateLocalCache(const IOdata& inputs,
                                      const stateData& sD,
                                      const solverMode& sMode) override;

        virtual void setState(coreTime time,
                              const double state[],
                              const double dstate_dt[],
                              const solverMode& sMode) override;
        virtual void
            timestep(coreTime time, const IOdata& inputs, const solverMode& sMode) override;

        coreObject* find(std::string_view obj) const override;

      private:
        void getSourceLoads();
        Source* makeSource(sourceLoc loc);
        Source* findSource(const std::string& srcname);
        Source* findSource(const std::string& srcname) const;
    };
}  // namespace loads
}  // namespace griddyn
