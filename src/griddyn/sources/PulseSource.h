/*
 * Copyright (c) 2014-2026, Lawrence Livermore National Security
 * See the top-level NOTICE for additional details. All rights reserved.
 * SPDX-License-Identifier: BSD-3-Clause
 */

#pragma once

#include "../Source.h"
#include <string>

namespace griddyn::sources {
/** @brief describe a pulsing source*/
class PulseSource: public Source {
  public:
    static const char invertFlag = OBJECT_FLAG3;  //!< flag location indicating an inverse waveform
    /** enumeration of the different available pulse types*/
    enum class PulseType {
        SQUARE,
        TRIANGLE,
        GAUSSIAN,
        BIEXPONENTIAL,
        EXPONENTIAL,
        COSINE,
        FLATTOP,
        MONOCYCLE
    };
    PulseType ptype = PulseType::SQUARE;  //!< the type of the pulse
  protected:
    CoreTime period = maxTime;  //!<[s] pulse period
    model_parameter dutyCycle = 0.5;  //!<[%] pulse duty cycle
    model_parameter Amplitude = 0.0;  //!< pulse amplitude
    CoreTime cycleTime = maxTime;  //!<[s] the start time of the last cycle
    model_parameter baseValue;  //!< the base level of the output
    model_parameter shift = 0.0;  //!< storage for phase shift fraction (should be between 0 and 1)

  public:
    PulseSource(const std::string& objName = "pulseSource_#", double startVal = 0.0);

    virtual CoreObject* clone(CoreObject* obj = nullptr) const override;
    virtual void set(std::string_view param, std::string_view val) override;
    virtual void
        set(std::string_view param, double val, units::unit unitType = units::defunit) override;
    virtual void pFlowObjectInitializeA(CoreTime time0, std::uint32_t flags) override;

    virtual void updateOutput(CoreTime time) override;
    virtual double computeOutput(CoreTime time) const override;
    virtual double getDoutdt(const IOdata& inputs,
                             const StateData& stateData,
                             const SolverMode& sMode,
                             index_t num = 0) const override;

    virtual void setLevel(double val) override;
    /** function to calculate a value of the pulsing output
@param[in] timeDelta the time change from the last update
@return the output value
*/
    double pulseCalc(double timeDelta) const;
};

}  // namespace griddyn::sources
