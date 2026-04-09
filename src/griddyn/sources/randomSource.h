/*
 * Copyright (c) 2014-2020, Lawrence Livermore National Security
 * See the top-level NOTICE for additional details. All rights reserved.
 * SPDX-License-Identifier: BSD-3-Clause
 */

#ifndef ___W_GRIDDYN_GRIDDYN_SRC_GRIDDYN_SOURCES_RANDOMSOURCE_H_
#define ___W_GRIDDYN_GRIDDYN_SRC_GRIDDYN_SOURCES_RANDOMSOURCE_H_

#include "rampSource.h"
#include <memory>
#include <string>
namespace utilities {
class gridRandom;
}

namespace griddyn {
namespace sources {
    /** @brief a source generating a random output*/
    class randomSource: public rampSource {
      public:
        /** random source flags*/
        enum random_source_flags {
            interpolate_flag = object_flag5,  //!< indicator that the output should be interpolated
            proportional_flag = object_flag6,  //!< indicator that the random change is proportional
                                               //!< to the current value
            repeated_flag =
                object_flag7,  //!< indicator that the random generation should be repeated
            triggered_flag =
                object_flag8,  //!< indicator that the random generation has been triggered

        };

      protected:
        model_parameter param1_t = 0.0;  //!< parameter 1 for time distribution
        model_parameter param2_t = 100;  //!< parameter 2 for time distribution
        model_parameter param1_L = 0.0;  //!< parameter 1 for level distribution
        model_parameter param2_L = 0.0;  //!< parameter 2 for level distribution
        model_parameter zbias =
            0.0;  //!< a factor describing the preference of changes to trend toward zero mean
        model_parameter offset = 0.0;  //!< the current bias in the value
        coreTime keyTime = 0.0;  //!< the next time change
        std::string timeDistribution = "constant";  //!< string representing the time Distribution
                                                    //!< random number generation type
        std::string valDistribution = "constant";  //!< string representing the value Distribution
                                                   //!< random number generation type
        std::unique_ptr<utilities::gridRandom>
            timeGenerator;  //!< random number generator for the time
        std::unique_ptr<utilities::gridRandom>
            valGenerator;  //!< random number generator for the value

      public:
        randomSource(const std::string& objName = "randomsource_#", double startVal = 0.0);
        ~randomSource();  // included so the definition of gridRandom doesn't have to be
        virtual coreObject* clone(coreObject* obj = nullptr) const override;

        virtual void pFlowObjectInitializeA(coreTime time0, std::uint32_t flags) override;
        virtual void
            timestep(coreTime time, const IOdata& inputs, const solverMode& sMode) override;

        /** check if the random number generation has been triggered*/
        bool isTriggered() { return opFlags[triggered_flag]; }
        virtual void reset(reset_levels level = reset_levels::minimal) override;

        virtual void set(const std::string& param, const std::string& val) override;
        virtual void set(const std::string& param,
                         double val,
                         units::unit unitType = units::defunit) override;
        virtual void updateA(coreTime time) override;

        void setFlag(const std::string& flag, bool val = true) override;

        virtual void updateOutput(coreTime time) override;

      private:
        /** generate the next step in the random process this source represents*/
        void nextStep(coreTime triggerTime);
        /** generate a random time for the next update*/
        coreTime ntime();
        /** generate a new random value*/
        double nval();
        void timeParamUpdate();
        void valParamUpdate();
        /** compute a bias shift in the random generation*/
        double computeBiasAdjust();
    };
}  // namespace sources
}  // namespace griddyn

#endif  // ___W_GRIDDYN_GRIDDYN_SRC_GRIDDYN_SOURCES_RANDOMSOURCE_H_
