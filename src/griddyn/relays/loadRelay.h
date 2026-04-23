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
class loadRelay: public Relay {
  public:
    enum loadrelay_flags {
        nondirectional_flag = object_flag10,
    };

  protected:
    double cutoutVoltage = 0.0;  //!<[puV] low voltage trigger for load
    double cutoutFrequency = 0.0;  //!<[puHz] low frequency trigger for load
    coreTime voltageDelay = timeZero;  //!<[s]  the delay on the voltage trip
    coreTime frequencyDelay = timeZero;  //!<[s] the delay on the frequency tripping
    coreTime offTime = maxTime;  //!<[s] the time before the load comes back on line if the trip
                                 //!< cause has been corrected
  public:
    explicit loadRelay(const std::string& objName = "loadRelay_$");
    virtual coreObject* clone(coreObject* obj = nullptr) const override;
    virtual void setFlag(std::string_view flag, bool val = true) override;
    virtual void set(std::string_view param, std::string_view val) override;

    virtual void
        set(std::string_view param, double val, units::unit unitType = units::defunit) override;

    virtual void dynObjectInitializeA(coreTime time0, std::uint32_t flags) override;

  protected:
    virtual void actionTaken(index_t ActionNum,
                             index_t conditionNum,
                             change_code actionReturn,
                             coreTime actionTime) override;
    virtual void conditionTriggered(index_t conditionNum, coreTime triggerTime) override;
    virtual void conditionCleared(index_t conditionNum, coreTime triggerTime) override;
};
}  // namespace griddyn::relays
