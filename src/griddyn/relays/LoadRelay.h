/*
 * Copyright (c) 2014-2026, Lawrence Livermore National Security
 * See the top-level NOTICE for additional details. All rights reserved.
 * SPDX-License-Identifier: BSD-3-Clause
 */

#pragma once

#include "../Relay.h"
#include <string>

namespace griddyn::relays {
/** class implementing a protective relay for load objects
the protective systems include underfrequency, undervoltage, and a return time so the load
automatically recovers
*/
class LoadRelay: public Relay {
  public:
    enum LoadRelayFlags {
        NONDIRECTIONAL_FLAG = OBJECT_FLAG10,
    };

  protected:
    double mCutoutVoltage = 0.0;  //!<[puV] low voltage trigger for load
    double mCutoutFrequency = 0.0;  //!<[puHz] low frequency trigger for load
    CoreTime mVoltageDelay = timeZero;  //!<[s]  the delay on the voltage trip
    CoreTime mFrequencyDelay = timeZero;  //!<[s] the delay on the frequency tripping
    CoreTime mOffTime = maxTime;  //!<[s] the time before the load comes back on line if the trip
                                  //!< cause has been corrected
  public:
    explicit LoadRelay(const std::string& objName = "loadRelay_$");
    virtual CoreObject* clone(CoreObject* obj = nullptr) const override;
    virtual void setFlag(std::string_view flag, bool val = true) override;
    virtual void set(std::string_view param, std::string_view val) override;

    virtual void
        set(std::string_view param, double val, units::unit unitType = units::defunit) override;

    virtual void dynObjectInitializeA(CoreTime time0, std::uint32_t flags) override;

  protected:
    virtual void actionTaken(index_t actionNum,
                             index_t conditionNum,
                             ChangeCode actionReturn,
                             CoreTime actionTime) override;
    virtual void conditionTriggered(index_t conditionNum, CoreTime triggerTime) override;
    virtual void conditionCleared(index_t conditionNum, CoreTime triggerTime) override;
};
}  // namespace griddyn::relays
