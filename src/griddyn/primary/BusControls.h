/*
 * Copyright (c) 2014-2026, Lawrence Livermore National Security
 * See the top-level NOTICE for additional details. All rights reserved.
 * SPDX-License-Identifier: BSD-3-Clause
 */

#ifndef BUSCONTROLS_H_
#define BUSCONTROLS_H_
#pragma once

#include "../gridDynDefinitions.hpp"
#include <vector>

namespace griddyn {

class gridSecondary;
class acBus;
class gridBus;
class Link;
class gridComponent;

/** @brief a set of  controls for a bus that manages controllable generators and loads
 provides autogen functionality and manages controlled generators to help with the transition from
power flow to dynamic calculations also manages the direct connected buses and buses tied together
by perfect links
*/
class BusControls {
  public:
    acBus* controlledBus;  //!< the bus that is being controlled

    double Qmin = -kBigNum;  //!< [pu]    reactive power minimum
    double Qmax = kBigNum;  //!< [pu]    reactive power maximum
    double Pmin = -kBigNum;  //!< [pu]    real power maximum
    double Pmax = kBigNum;  //!< [pu]    real power maximum
    double autogenP = kBigNum;  //!< use an automatic generator to local match P load
    double autogenQ = kBigNum;  //!< use an automatic generator to locally match Q load
    double autogenDelay = 0.0;  //!< time constant for automatic generation
    double autogenPact = 0;  //!< use an automatic generator to local match P load
    double autogenQact = 0;  //!< use an automatic generator to locally match Q load

    // for managing voltage control objects
    std::vector<gridSecondary*> vControlObjects;  //!< object which control the voltage of the bus
    std::vector<Link*> proxyVControlObject;  //!< object which act as an interface for remote
                                             //!< objects acting on a bus
    std::vector<Link*>
        vControlLinks;  //!< set of Link which themselves act as controllable objects;
    std::vector<double>
        vcfrac;  //!< the fraction of control power which should be allocated to a specific object

    std::vector<double> vclinkFrac;  //!< the fraction of control power which should be allocated to
                                     //!< a specific controllable link

    // for managing p control objects
    std::vector<gridSecondary*> pControlObjects;  //!< object which control the angle of a bus
    std::vector<Link*> proxyPControlObject;  //!< object which act as an interface for remote
                                             //!< objects acting on a bus
    std::vector<Link*>
        pControlLinks;  //!< set of Link which themselves act as controllable power objects;
    std::vector<double>
        pcfrac;  //!< the fraction of control power which should be allocated to a specific object
    std::vector<double> pclinkFrac;  //!< the fraction of control power which should be allocated to
                                     //!< a specific controllable link

    // for coordinating node-breaker models and directly connected buses
    std::vector<acBus*> slaveBusses;  //!< buses which are slaved to this bus
    gridBus* masterBus = nullptr;  //!< if the bus is a slave this is the master
    gridBus* directBus = nullptr;  //!< if the bus is direct connected this is the master

  public:
    /** @brief const*/
    explicit BusControls(acBus* busToControl);

    bool hasVoltageAdjustments(id_type_t sid) const;
    bool hasPowerAdjustments(id_type_t sid) const;

    double getAdjustableCapacityUp(coreTime time) const;
    double getAdjustableCapacityDown(coreTime time) const;

    void addPowerControlObject(gridComponent* comp, bool update);
    void addVoltageControlObject(gridComponent* comp, bool update);

    void removePowerControlObject(id_type_t oid, bool update);
    void removeVoltageControlObject(id_type_t oid, bool update);

    /** @brief  update the values used in voltage control*/
    void updateVoltageControls();
    /** @brief  update the values used in power control*/
    void updatePowerControls();

    bool checkIdenticalControls();

    void mergeBus(acBus* mbus);
    void unmergeBus(acBus* mbus);
    void checkMerge();
};

}  // namespace griddyn
#endif
