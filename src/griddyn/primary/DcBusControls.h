/*
 * Copyright (c) 2014-2026, Lawrence Livermore National Security
 * See the top-level NOTICE for additional details. All rights reserved.
 * SPDX-License-Identifier: BSD-3-Clause
 */

#pragma once

#include "../gridDynDefinitions.hpp"
#include <vector>

namespace griddyn {

class gridSecondary;
class dcBus;
class gridBus;
class Link;
class gridComponent;

/** @brief a set of  controls for a bus that manages controllable generators and loads for a dc bus
provides autogen functionality and manages controlled generators to help with the transition from
power flow to dynamic calculations also manages the direct connected buses and buses tied together
by perfect links
*/
class DcBusControls {
  public:
    dcBus* controlledBus;  //!< the bus that is being controlled

    double Pmin = -kBigNum;  //!< [pu]    real power maximum
    double Pmax = kBigNum;  //!< [pu]    real power maximum
    double autogenP = kBigNum;  //!< use an automatic generator to local match P load
    double autogenDelay = 0.0;  //!< time constant for automatic generation
    double autogenPact = 0;  //!< use an automatic generator to local match P load

    // for managing voltage control objects
    std::vector<gridSecondary*> controlObjects;  //!< object which control the voltage of the bus
    std::vector<Link*> proxyControlObject;  //!< object which act as an interface for remote objects
                                            //!< acting on a bus
    std::vector<Link*> controlLinks;  //!< set of Link which themselves act as controllable objects;
    std::vector<double>
        cfrac;  //!< the fraction of control power which should be allocated to a specific object

    std::vector<double> clinkFrac;  //!< the fraction of control power which should be allocated to
                                    //!< a specific controllable link

    // for coordinating node-breaker models and directly connected buses
    std::vector<dcBus*> slaveBusses;  //!< buses which are slaved to this bus
    gridBus* masterBus = nullptr;  //!< if the bus is a slave this is the master
    gridBus* directBus = nullptr;  //!< if the bus is direct connected this is the master

  public:
    /** @brief const*/
    explicit DcBusControls(dcBus* busToControl);
    bool hasAdjustments() const;
    bool hasAdjustments(id_type_t sid) const;

    double getAdjustableCapacityUp(coreTime time) const;
    double getAdjustableCapacityDown(coreTime time) const;

    void addControlObject(gridComponent* comp, bool update);

    void removeControlObject(id_type_t oid, bool update);

    /** @brief  update the values used in voltage control*/
    void updateControls();
    /** @brief  update the values used in power control*/

    void mergeBus(dcBus* mbus);
    void unmergeBus(dcBus* mbus);
    void checkMerge();
};

}  // namespace griddyn
