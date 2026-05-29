/*
 * Copyright (c) 2014-2026, Lawrence Livermore National Security
 * See the top-level NOTICE for additional details. All rights reserved.
 * SPDX-License-Identifier: BSD-3-Clause
 */

#pragma once

#include "core/CoreOwningPtr.hpp"
#include "griddyn/GridSubModel.h"
#include <memory>
#include <string>
#include <vector>

namespace griddyn {
class GridArea;
class SchedulerReg;
class Generator;
class battery;
namespace blocks {
    class PidBlock;
    class DelayBlock;
    class DeadbandBlock;
}  // namespace blocks

class Communicator;
class AGControl: public GridSubModel {
  public:
    enum AGCType {
        BASIC_AGC,
        BATTERY_AGC,
        BATT_DR,
    };

  protected:
    double ki = 0.005;
    double kp = 1.0;
    double beta = 8.0;
    double deadband = 20;

    double tf = 8.0;
    double tr = 15;
    double ace = 0;
    double filteredAce = 0;
    double freg = 0;
    double reg = 0;
    double regUpAvailable = 0;
    double regDownAvailable = 0;

    CoreOwningPtr<blocks::PidBlock> pid;
    CoreOwningPtr<blocks::DelayBlock> filt1;
    CoreOwningPtr<blocks::DelayBlock> filt2;
    CoreOwningPtr<blocks::DeadbandBlock> db;

    count_t schedCount = 0;

    std::vector<SchedulerReg*> schedList;
    std::vector<double> upRat;
    std::vector<double> downRat;
    std::shared_ptr<Communicator> comms;

  public:
    AGControl(const std::string& objName = "AGC_#");
    virtual CoreObject* clone(CoreObject* obj = nullptr) const override;
    virtual ~AGControl();

    virtual void dynObjectInitializeB(const IOdata& inputs,
                                      const IOdata& desiredOutput,
                                      IOdata& fieldSet) override;

    virtual void updateA(coreTime time) override;

    virtual void timestep(coreTime time, const IOdata& inputs, const SolverMode& sMode) override;

    virtual double getOutput(const IOdata& inputs,
                             const StateData& sD,
                             const SolverMode& sMode,
                             index_t num = 0) const override;

    virtual double getOutput(index_t /*num*/ = 0) const override;
    virtual void add(CoreObject* obj) override;
    virtual void add(SchedulerReg* sched);
    virtual void remove(CoreObject* obj) override;
    virtual void set(std::string_view param, std::string_view val) override;
    virtual void
        set(std::string_view param, double val, units::unit unitType = units::defunit) override;

    double getACE() { return ace; }
    double getfACE() { return filteredAce; }

    virtual void regChange();
};

/*
class AGControlBattery:public AGControl
{
public:

protected:
        std::vector <battery *> batList;
        std::vector<int> isBat;
        std::vector<double> batUpRat;
        std::vector<double> batDownRat;
        std::vector<double> genSched;
        size_t batCount;
        double batUpMax;
        double batDownMax;
        double batRolloff;

        double convReg;
        double batReg;
public:
        AGControlBattery();
        virtual CoreObject *clone(CoreObject *obj = nullptr, bool copyName = false) const;
        virtual ~AGControlBattery();

        virtual double dynObjectInitializeA (coreTime time0,double freq0,double tiedev0);

        virtual double updateA(coreTime time, double freq, double tiedev);

        virtual void addGen(schedulerReg *sched);
        virtual void removeSched(schedulerReg *sched);
        virtual void set (const std::string &param, std::string val);
        virtual void set (const std::string &param, double val, units::unit unitType =
units::defunit);

        virtual void regChange();
protected:
};

*/
}  // namespace griddyn
