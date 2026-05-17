/*
 * Copyright (c) 2014-2026, Lawrence Livermore National Security
 * See the top-level NOTICE for additional details. All rights reserved.
 * SPDX-License-Identifier: BSD-3-Clause
 */

#pragma once

#include "../Relay.h"
#include <memory>
#include <string>
#include <vector>

namespace griddyn::relays {
/** class building off of Relay to define a zonal relays
the number of zones is arbitrary and it works by checking the impedances of the associated link and
comparing to specific thresholds. This zonal relays runs off a single impedance number
*/
class zonalRelay: public Relay {
  public:
    enum ZonalRelayFlags {
        NONDIRECTIONAL_FLAG = object_flag10,
    };

  protected:
    count_t mZoneCount = 2;  //!< the number of zones for the relay
    index_t m_terminal = 1;  //!< the side of the line to connect 1=from side 2=to side, 3+ for
                             //!< multiterminal devices
    double mResetMargin = 0.01;  //!<! the reset margin for clearing a fault
    std::vector<double> mZoneLevels;  //!< the level of impedance to trigger
    std::vector<coreTime> mZoneDelays;  //!< the delay upon which to act for the relay
    count_t mConditionLevel = kInvalidCount;  //!< the level of condition that has been triggered
    int mAutoName = -1;  //!< storage for indicator of the type of autoname to use
  public:
    explicit zonalRelay(const std::string& objName = "zonalRelay_$");
    virtual CoreObject* clone(CoreObject* obj = nullptr) const override;
    virtual void setFlag(std::string_view flag, bool val = true) override;
    virtual void set(std::string_view param, std::string_view val) override;

    virtual void
        set(std::string_view param, double val, units::unit unitType = units::defunit) override;

    virtual double get(std::string_view param,
                       units::unit unitType = units::defunit) const override;
    virtual void dynObjectInitializeA(coreTime time0, std::uint32_t flags) override;

  protected:
    virtual void actionTaken(index_t ActionNum,
                             index_t conditionNum,
                             change_code actionReturn,
                             coreTime actionTime) override;
    virtual void conditionTriggered(index_t conditionNum, coreTime triggerTime) override;
    virtual void conditionCleared(index_t conditionNum, coreTime triggerTime) override;
    virtual void receiveMessage(std::uint64_t sourceID,
                                std::shared_ptr<commMessage> message) override;
    /** function to automatically generate the comm system names
@param[in] code  a code value representing the method of generating the name
@return the generated name
*/
    std::string generateAutoName(int code);

    virtual std::string generateCommName() override;
};
}  // namespace griddyn::relays

