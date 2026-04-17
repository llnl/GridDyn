/*
 * Copyright (c) 2014-2026, Lawrence Livermore National Security
 * See the top-level NOTICE for additional details. All rights reserved.
 * SPDX-License-Identifier: BSD-3-Clause
 */

#pragma once

#include "../gridSubModel.h"
#include "gmlc/utilities/vectorOps.hpp"
#include <string>
#include <vector>

namespace griddyn {
class Area;
class schedulerRamp;

/** in development object to manage the dispatch of reserve generation
 */
class reserveDispatcher: public coreObject {
  public:
  protected:
    double thresholdStart = kBigNum;
    double thresholdStop = kBigNum;
    double currDispatch = 0.0;
    double reserveAvailable = 0.0;
    coreTime dispatchTime = negTime;
    coreTime dispatchInterval = 60.0 * 5.0;

    count_t schedCount = 0;
    std::vector<schedulerRamp*> schedList;
    std::vector<double> resAvailable;
    std::vector<double> reserveUsed;

  public:
    explicit reserveDispatcher(const std::string& objName = "reserveDispatch_#");
    virtual coreObject* clone(coreObject* obj = nullptr) const override;
    virtual ~reserveDispatcher();

    virtual double dynInitializeA(coreTime time0, double dispatchSet);

    void moveSchedulers(reserveDispatcher* rD);

    virtual double updateP(coreTime time, double pShort);
    virtual double testP(coreTime time, double pShort);
    double getOutput(index_t /*num*/ = 0) { return currDispatch; }

    virtual void add(schedulerRamp* sched);
    virtual void add(coreObject* obj) override;

    virtual void remove(schedulerRamp* sched);
    virtual void remove(coreObject* obj) override;

    virtual void set(const std::string& param, const std::string& val) override;
    virtual void
        set(const std::string& param, double val, units::unit unitType = units::defunit) override;

    double getAvailable() { return reserveAvailable; }

    virtual void schedChange();

  protected:
    virtual void checkGen();
    virtual void dispatch(double level);
};

}  // namespace griddyn
