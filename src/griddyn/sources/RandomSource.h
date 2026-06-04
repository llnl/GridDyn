/*
 * Copyright (c) 2014-2026, Lawrence Livermore National Security
 * See the top-level NOTICE for additional details. All rights reserved.
 * SPDX-License-Identifier: BSD-3-Clause
 */

#pragma once

#include "RampSource.h"
#include <memory>
#include <string>
namespace utilities {
class GridRandom;
}

namespace griddyn::sources {
/** @brief a source generating a random output*/
class RandomSource: public RampSource {
  public:
    /** random source flags*/
    enum RandomSourceFlags {
        INTERPOLATE_FLAG = OBJECT_FLAG5,  //!< indicator that the output should be interpolated
        PROPORTIONAL_FLAG = OBJECT_FLAG6,  //!< indicator that the random change is proportional
                                           //!< to the current value
        REPEATED_FLAG = OBJECT_FLAG7,  //!< indicator that the random generation should be repeated
        TRIGGERED_FLAG = OBJECT_FLAG8,  //!< indicator that the random generation has been triggered

    };

  protected:
    model_parameter param1_t = 0.0;  //!< parameter 1 for time distribution
    model_parameter param2_t = 100;  //!< parameter 2 for time distribution
    model_parameter param1_L = 0.0;  //!< parameter 1 for level distribution
    model_parameter param2_L = 0.0;  //!< parameter 2 for level distribution
    model_parameter zbias =
        0.0;  //!< a factor describing the preference of changes to trend toward zero mean
    model_parameter offset = 0.0;  //!< the current bias in the value
    CoreTime keyTime = 0.0;  //!< the next time change
    std::string timeDistribution = "constant";  //!< string representing the time Distribution
                                                //!< random number generation type
    std::string valDistribution = "constant";  //!< string representing the value Distribution
                                               //!< random number generation type
    std::unique_ptr<utilities::GridRandom> timeGenerator;  //!< random number generator for the time
    std::unique_ptr<utilities::GridRandom> valGenerator;  //!< random number generator for the value

  public:
    RandomSource(const std::string& objName = "randomsource_#", double startVal = 0.0);
    ~RandomSource();  // included so the definition of GridRandom doesn't have to be
    virtual CoreObject* clone(CoreObject* obj = nullptr) const override;

    virtual void pFlowObjectInitializeA(CoreTime time0, std::uint32_t flags) override;
    virtual void timestep(CoreTime time, const IOdata& inputs, const SolverMode& sMode) override;

    /** check if the random number generation has been triggered*/
    bool isTriggered() { return opFlags[TRIGGERED_FLAG]; }
    virtual void reset(ResetLevels level = ResetLevels::MINIMAL) override;

    virtual void set(std::string_view param, std::string_view val) override;
    virtual void
        set(std::string_view param, double val, units::unit unitType = units::defunit) override;
    virtual void updateA(CoreTime time) override;

    void setFlag(std::string_view flag, bool val = true) override;

    virtual void updateOutput(CoreTime time) override;

  private:
    /** generate the next step in the random process this source represents*/
    void nextStep(CoreTime triggerTime);
    /** generate a random time for the next update*/
    CoreTime ntime();
    /** generate a new random value*/
    double nval();
    void timeParamUpdate();
    void valParamUpdate();
    /** compute a bias shift in the random generation*/
    double computeBiasAdjust();
};
}  // namespace griddyn::sources
