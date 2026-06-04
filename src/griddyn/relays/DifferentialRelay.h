/*
 * Copyright (c) 2014-2026, Lawrence Livermore National Security
 * See the top-level NOTICE for additional details. All rights reserved.
 * SPDX-License-Identifier: BSD-3-Clause
 */

#pragma once

#include "../Relay.h"
#include "../comms/CommMessage.h"
#include <memory>
#include <string>

namespace griddyn::relays {
/** relay implementing differential relay protection scheme
 */
class DifferentialRelay: public Relay {
  public:
    enum DifferentialRelayFlags {
        RELATIVE_DIFFERENTIAL_FLAG = OBJECT_FLAG10,
        LINK_MODE = OBJECT_FLAG11,
        BUS_MODE = OBJECT_FLAG12,
    };

  protected:
    double m_resetMargin = 0.01;  //!< the reset margin for clearing a fault
    double mDelayTime = 0.08;  //!<[s] the delay time from first onset to trigger action
    double mMaxDifferential = 0.2;  //!< the maximum allowable differential
    double mMinLevel =
        0.01;  //!< the minimum absolute level to trigger for relative differential mode
  public:
    explicit DifferentialRelay(const std::string& objName = "diffRelay_$");
    virtual CoreObject* clone(CoreObject* obj = nullptr) const override;
    virtual void setFlag(std::string_view flag, bool val = true) override;
    virtual bool getFlag(std::string_view flag) const override;

    virtual void set(std::string_view param, std::string_view val) override;

    virtual void
        set(std::string_view param, double val, units::unit unitType = units::defunit) override;
    virtual void getParameterStrings(stringVec& pstr, ParamStringType pstype) const override;
    virtual void pFlowObjectInitializeA(CoreTime time0, std::uint32_t flags) override;

    virtual void receiveMessage(std::uint64_t sourceID,
                                std::shared_ptr<CommMessage> message) override;

  protected:
    virtual void actionTaken(index_t actionNum,
                             index_t conditionNum,
                             ChangeCode actionReturn,
                             CoreTime actionTime) override;
    virtual void conditionTriggered(index_t conditionNum, CoreTime triggerTime) override;
    virtual void conditionCleared(index_t conditionNum, CoreTime triggerTime) override;
};

}  // namespace griddyn::relays
