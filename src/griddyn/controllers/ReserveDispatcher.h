/*
 * Copyright (c) 2014-2026, Lawrence Livermore National Security
 * See the top-level NOTICE for additional details. All rights reserved.
 * SPDX-License-Identifier: BSD-3-Clause
 */

#pragma once

#include "../GridSubModel.h"
#include "gmlc/utilities/vectorOps.hpp"
#include <string>
#include <vector>

namespace griddyn {
class GridArea;
class SchedulerRamp;

/** in development object to manage the dispatch of reserve generation
 */
class ReserveDispatcher: public CoreObject {
  public:
  protected:
    double thresholdStart = kBigNum;
    double thresholdStop = kBigNum;
    double currentDispatch = 0.0;
    double reserveAvailable = 0.0;
    CoreTime dispatchTime = negTime;
    CoreTime dispatchInterval = 60.0 * 5.0;

    count_t schedCount = 0;
    std::vector<SchedulerRamp*> schedList;
    std::vector<double> reserveAvailableByScheduler;
    std::vector<double> reserveUsed;

  public:
    explicit ReserveDispatcher(const std::string& objName = "reserveDispatch_#");
    virtual CoreObject* clone(CoreObject* obj = nullptr) const override;
    virtual ~ReserveDispatcher();

    virtual double dynInitializeA(CoreTime time0, double dispatchSet);

    void moveSchedulers(ReserveDispatcher* dispatcherToMove);

    virtual double updateP(CoreTime time, double pShort);
    virtual double testP(CoreTime time, double pShort);
    double getOutput(index_t /*num*/ = 0) { return currentDispatch; }

    virtual void add(SchedulerRamp* sched);
    virtual void add(CoreObject* obj) override;

    virtual void remove(SchedulerRamp* sched);
    virtual void remove(CoreObject* obj) override;

    virtual void set(std::string_view param, std::string_view val) override;
    virtual void
        set(std::string_view param, double val, units::unit unitType = units::defunit) override;

    double getAvailable() { return reserveAvailable; }

    virtual void schedChange();

  protected:
    virtual void checkGen();
    virtual void dispatch(double level);
};
}  // namespace griddyn
