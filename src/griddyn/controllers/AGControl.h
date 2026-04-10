/*
 * Copyright (c) 2014-2020, Lawrence Livermore National Security
 * See the top-level NOTICE for additional details. All rights reserved.
 * SPDX-License-Identifier: BSD-3-Clause
 */

#ifndef ___W_GRIDDYN_GRIDDYN_SRC_GRIDDYN_CONTROLLERS_AGCONTROL_H_
#define ___W_GRIDDYN_GRIDDYN_SRC_GRIDDYN_CONTROLLERS_AGCONTROL_H_

#include "core/coreOwningPtr.hpp"
#include "griddyn/gridSubModel.h"
#include <memory>
#include <string>
#include <vector>

namespace griddyn {
class Area;
class schedulerReg;
class Generator;
class battery;
namespace blocks {
    class pidBlock;
    class delayBlock;
    class deadbandBlock;
}  // namespace blocks

class Communicator;
class AGControl: public gridSubModel {
  public:
    enum agcType {
        basicAGC,
        batteryAGC,
        battDR,
    };

  protected:
    double KI = 0.005;
    double KP = 1.0;
    double beta = 8.0;
    double deadband = 20;

    double Tf = 8.0;
    double Tr = 15;
    double ACE = 0;
    double fACE = 0;
    double freg = 0;
    double reg = 0;
    double regUpAvailable = 0;
    double regDownAvailable = 0;

    coreOwningPtr<blocks::pidBlock> pid;
    coreOwningPtr<blocks::delayBlock> filt1;
    coreOwningPtr<blocks::delayBlock> filt2;
    coreOwningPtr<blocks::deadbandBlock> db;

    count_t schedCount = 0;

    std::vector<schedulerReg*> schedList;
    std::vector<double> upRat;
    std::vector<double> downRat;
    std::shared_ptr<Communicator> comms;

  public:
    AGControl(const std::string& objName = "AGC_#");
    virtual coreObject* clone(coreObject* obj = nullptr) const override;
    virtual ~AGControl();

    virtual void dynObjectInitializeB(const IOdata& inputs,
                                      const IOdata& desiredOutput,
                                      IOdata& fieldSet) override;

    virtual void updateA(coreTime time) override;

    virtual void timestep(coreTime time, const IOdata& inputs, const solverMode& sMode) override;

    virtual double getOutput(const IOdata& inputs,
                             const stateData& sD,
                             const solverMode& sMode,
                             index_t num = 0) const override;

    virtual double getOutput(index_t /*num*/ = 0) const override;
    virtual void add(coreObject* obj) override;
    virtual void add(schedulerReg* sched);
    virtual void remove(coreObject* obj) override;
    virtual void set(const std::string& param, const std::string& val) override;
    virtual void
        set(const std::string& param, double val, units::unit unitType = units::defunit) override;

    double getACE() { return ACE; }
    double getfACE() { return fACE; }

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
        virtual coreObject *clone(coreObject *obj = nullptr, bool copyName = false) const;
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
#endif  // ___W_GRIDDYN_GRIDDYN_SRC_GRIDDYN_CONTROLLERS_AGCONTROL_H_
