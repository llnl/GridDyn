#pragma once

/*
 * Copyright (c) 2014-2026, Lawrence Livermore National Security
 * See the top-level NOTICE for additional details. All rights reserved.
 * SPDX-License-Identifier: BSD-3-Clause
 */
#include "pulseSource.h"
#include <string>

namespace griddyn::sources {
    /** A source generating a sinusoidal output
     */
    class sineSource: public pulseSource {
      public:
        static const char pulsed_flag =
            object_flag4;  //!< indicator that the source should be pulsed
      protected:
        model_parameter frequency = 0.0;  //!<[Hz] frequency of an oscillation
        model_parameter phase = 0.0;  //!<[rad]  the offset angle
        coreTime lastCycle = negTime;  ///!< time of the last cycle completion
        model_parameter Amp = 0.0;  //!< the amplitude of the pulse
        coreTime sinePeriod = maxTime;  //!< the period of the sinusoid
        model_parameter dfdt = 0.0;  ///!<[Hz/s] the rate of change of frequency
        model_parameter dAdt = 0.0;  //!< [1/s] the rate of change of amplitude

      public:
        sineSource(const std::string& objName = "sineSource_#", double startVal = 0.0);

        virtual coreObject* clone(coreObject* obj = nullptr) const override;
        virtual void set(const std::string& param, const std::string& val) override;
        virtual void set(const std::string& param,
                         double val,
                         units::unit unitType = units::defunit) override;

        virtual void pFlowObjectInitializeA(coreTime time0, std::uint32_t flags) override;

        virtual void updateOutput(coreTime time) override;
        virtual double computeOutput(coreTime time) const override;
    };
}  // namespace griddyn::sources
