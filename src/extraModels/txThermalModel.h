/*
 * Copyright (c) 2014-2026, Lawrence Livermore National Security
 * See the top-level NOTICE for additional details. All rights reserved.
 * SPDX-License-Identifier: BSD-3-Clause
 */

#pragma once

#include "griddyn/relays/sensor.h"
#include <string>

namespace griddyn::extra {
/** @brief basic thermal model of a transformer
 */
class txThermalModel: public sensor {
  public:
    enum thermal_model_flags {
        auto_parameter_load = object_flag10,
        enable_parameter_updates = object_flag11,
        enable_alarms = object_flag12,
    };

  protected:
    double mOilTimeConstant = 1.25 * 3600.0;  //!<[s] oil rise time constant
    double mRatedHotSpotRise = 35.0;  //!<[C] hot spot rise temp at rated current over top oil
    double mRatedTopOilRise = 45.0;  //!<[C] Oil rise temp at rated current
    double mWindingTimeConstant = 5.0 * 60.0;  //!<[s] winding time constant
    double mLossRatio = 6.5;  //!< Loss Ratio
    double mOilExponent = 1.0;  //!< oil exponent
    double mWindingExponent = 1.0;  //!< winding exponent
    double mAmbientTemp = 20;  //!<[C] ambient temperature in C
    double mTempRateOfChange = 0.0;  //!<[C/s] rate of change of ambient temperature
    double mAlarmTemp1 = 0;  //!<[C] the lower alarm temp
    double mAlarmTemp2 = 0;  //!<[C] the level 2 alarm temp
    double mCutoutTemp = 0;  //!<[C] the temp at which the breakers are tripped
    double mAlarmDelay = 300;  //!<[s] delay time on the alarms and cutout;
  private:
    double mRating = 0.0;  //!< transformer rating
    double mRatedLoss = 0.0;  //!<  the losses at rated power
    double mThermalCapacity = 0.0;  //!< transformer thermal capacity
    double mRadiationConstant = 0.0;  //!< transformer radiation constant
  public:
    /** @brief constructor*/
    txThermalModel(const std::string& objName = "txThermal_$");
    virtual coreObject* clone(coreObject* obj = nullptr) const override;
    virtual void setFlag(std::string_view flag, bool val = true) override;
    virtual void set(std::string_view param, std::string_view val) override;

    virtual void
        set(std::string_view param, double val, units::unit unitType = units::defunit) override;
    using sensor::add;
    virtual void add(coreObject* obj) override final;
    virtual double get(std::string_view param,
                       units::unit unitType = units::defunit) const override;

    virtual void dynObjectInitializeA(coreTime time0, std::uint32_t flags) override;
    virtual void dynObjectInitializeB(const IOdata& inputs,
                                      const IOdata& desiredOutput,
                                      IOdata& fieldSet) override;

    virtual void timestep(coreTime time, const IOdata& inputs, const solverMode& sMode) override;
    virtual void updateA(coreTime time) override;
};

}  // namespace griddyn::extra
