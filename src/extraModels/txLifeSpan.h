/*
 * Copyright (c) 2014-2026, Lawrence Livermore National Security
 * See the top-level NOTICE for additional details. All rights reserved.
 * SPDX-License-Identifier: BSD-3-Clause
 */

#pragma once

#include "griddyn/relays/sensor.h"
#include <string>

namespace griddyn::extra {
/** @brief class modeling a transformer lifespan based on thermal effects
 */
class txLifeSpan: public sensor {
  public:
    enum lifespan_model_flags {
        useIECmethod = object_flag11,
        no_disconnect = object_flag12,  //!< flag indicating that the object should create a short
                                        //!< circuit instead of disconnecting when life reaches 0
    };

  protected:
    double mInitialLife = 150000.0;  //!< initial life in hours
    double mAgingConstant = 14594.0;  //!< aging constant default value of 14594 based on
                                      //!< research 15000 is another commonly used value
    double mBaseTemp = 110;  //!< the temperature base for the lifespan equations
    double mAgingFactor =
        1.0;  //!<  factor for accelerated or decelerated aging based on insulation properties

  private:
    double mAgingAccelerationFactor = 0.0;

  public:
    txLifeSpan(const std::string& objName = "txlifeSpan_$");
    CoreObject* clone(CoreObject* obj = nullptr) const override;
    virtual void setFlag(std::string_view flag, bool val = true) override;
    virtual void set(std::string_view param, std::string_view val) override;

    virtual void
        set(std::string_view param, double val, units::unit unitType = units::defunit) override;
    using sensor::add;
    virtual void add(CoreObject* obj) override final;
    virtual double get(std::string_view param,
                       units::unit unitType = units::defunit) const override;

    virtual void dynObjectInitializeA(coreTime time0, std::uint32_t flags) override;
    virtual void dynObjectInitializeB(const IOdata& inputs,
                                      const IOdata& desiredOutput,
                                      IOdata& fieldSet) override;

    virtual void timestep(coreTime time, const IOdata& inputs, const solverMode& sMode) override;
    virtual void updateA(coreTime time) override;

    void actionTaken(index_t actionNumber,
                     index_t conditionNum,
                     change_code actionReturn,
                     coreTime /*actionTime*/) override;
};

}  // namespace griddyn::extra

