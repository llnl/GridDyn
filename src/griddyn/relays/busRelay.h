/*
 * Copyright (c) 2014-2026, Lawrence Livermore National Security
 * See the top-level NOTICE for additional details. All rights reserved.
 * SPDX-License-Identifier: BSD-3-Clause
 */

#pragma once

#include "../Relay.h"
#include <string>
namespace griddyn::relays {
/** relay object for bus protection can isolate a bus based on voltage or frequency
with a controllable delay time operates on undervoltage and underfrequency
*/
class busRelay: public Relay {
  public:
    enum BusRelayFlags {
        NONDIRECTIONAL_FLAG = object_flag10,  //!< specify that the relay is non directional
    };

  protected:
    model_parameter mCutoutVoltage = 0.0;  //!<[puV] low voltage limit
    model_parameter mCutoutFrequency = 0.0;  //!<[puHz] trip on low frequency
    coreTime mVoltageDelay =
        timeZero;  //!< [s] period of time the voltage must be below limit to activate
    coreTime mFrequencyDelay =
        timeZero;  //!< [s] period of time the frequency must be below limit to activate
  public:
    explicit busRelay(const std::string& objName = "busrelay_$");
    virtual coreObject* clone(coreObject* obj) const override;
    virtual void setFlag(std::string_view flag, bool val = true) override;
    virtual void set(std::string_view param, std::string_view val) override;

    virtual void
        set(std::string_view param, double val, units::unit unitType = units::defunit) override;

    virtual void pFlowObjectInitializeA(coreTime time0, std::uint32_t flags) override;

  protected:
    virtual void actionTaken(index_t ActionNum,
                             index_t conditionNum,
                             change_code actionReturn,
                             coreTime actionTime) override;
};
}  // namespace griddyn::relays
