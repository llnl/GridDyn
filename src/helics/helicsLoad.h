/*
 * Copyright (c) 2014-2026, Lawrence Livermore National Security
 * See the top-level NOTICE for additional details. All rights reserved.
 * SPDX-License-Identifier: BSD-3-Clause
 */

#pragma once

#include "../griddyn/loads/rampLoad.h"
#include "helicsSupport.h"
#include <string>
namespace griddyn::helicsLib {
class HelicsCoordinator;
enum class HelicsValueType : char;

/** class defining a load object that links with a helics source*/
class HelicsLoad: public loads::rampLoad {
  public:
    enum HelicsLoadFlags {
        USE_RAMP = object_flag8,
        PREDICTIVE_RAMP = object_flag9,
        INITIAL_QUERY = object_flag10,
    };

  protected:
    std::string voltageKey;  //!< the key to send voltage
    std::string loadKey;  //!< time series containing the load information
    int32_t voltageIndex = -1;  //!< index for sending the voltage data
    int32_t loadIndex = -1;  //!< index for getting the load data
    units::unit inputUnits = units::MW;
    helics::data_type loadType;
    helics::data_type voltageType;
    double scaleFactor = 1.0;  //!< scaling factor on the load
    HelicsCoordinator* coordinator_ =
        nullptr;  //!< the coordinator for interaction with the helics interface
  private:
    double prevP = 0;
    double prevQ = 0;

  public:
    explicit HelicsLoad(const std::string& objName = "helicsLoad_$");

    CoreObject* clone(CoreObject* obj = nullptr) const override;
    virtual void pFlowObjectInitializeA(coreTime time0, uint32_t flags) override;
    virtual void pFlowObjectInitializeB() override;

    virtual void updateA(coreTime time) override;
    virtual coreTime updateB() override;
    virtual void timestep(coreTime ttime, const IOdata& inputs, const solverMode& sMode) override;
    virtual void setFlag(const std::string& param, bool val = true) override;
    virtual void set(const std::string& param, const std::string& val) override;
    virtual void
        set(const std::string& param, double val, units::unit unitType = units::defunit) override;

  private:
    void setSubscription();
};

}  // namespace griddyn::helicsLib

