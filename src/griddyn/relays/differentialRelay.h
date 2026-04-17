/*
 * Copyright (c) 2014-2026, Lawrence Livermore National Security
 * See the top-level NOTICE for additional details. All rights reserved.
 * SPDX-License-Identifier: BSD-3-Clause
 */

#pragma once

#include "../Relay.h"
#include "../comms/commMessage.h"
#include <memory>
#include <string>

namespace griddyn::relays {
/** relay implementing differential relay protection scheme
 */
class differentialRelay: public Relay {
  public:
    enum differentialrelay_flags {
        relative_differential_flag = object_flag10,
        link_mode = object_flag11,
        bus_mode = object_flag12,
    };

  protected:
    double m_resetMargin = 0.01;  //!< the reset margin for clearing a fault
    double m_delayTime = 0.08;  //!<[s] the delay time from first onset to trigger action
    double m_max_differential = 0.2;  //!< the maximum allowable differential
    double m_minLevel =
        0.01;  //!< the minimum absolute level to trigger for relative differential mode
  public:
    explicit differentialRelay(const std::string& objName = "diffRelay_$");
    virtual coreObject* clone(coreObject* obj = nullptr) const override;
    virtual void setFlag(const std::string& flag, bool val = true) override;
    virtual bool getFlag(const std::string& flag) const override;

    virtual void set(const std::string& param, const std::string& val) override;

    virtual void
        set(const std::string& param, double val, units::unit unitType = units::defunit) override;
    virtual void getParameterStrings(stringVec& pstr, paramStringType pstype) const override;
    virtual void pFlowObjectInitializeA(coreTime time0, std::uint32_t flags) override;

    virtual void receiveMessage(std::uint64_t sourceID,
                                std::shared_ptr<commMessage> message) override;

  protected:
    virtual void actionTaken(index_t ActionNum,
                             index_t conditionNum,
                             change_code actionReturn,
                             coreTime actionTime) override;
    virtual void conditionTriggered(index_t conditionNum, coreTime triggerTime) override;
    virtual void conditionCleared(index_t conditionNum, coreTime triggerTime) override;
};

}  // namespace griddyn::relays
