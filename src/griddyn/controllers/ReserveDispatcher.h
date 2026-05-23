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
class schedulerRamp;

/** in development object to manage the dispatch of reserve generation
 */
class ReserveDispatcher: public CoreObject {
  public:
  protected:
    double thresholdStart = kBigNum;
    double thresholdStop = kBigNum;
    double currentDispatch = 0.0;
    double reserveAvailable = 0.0;
    coreTime dispatchTime = negTime;
    coreTime dispatchInterval = 60.0 * 5.0;

    count_t schedCount = 0;
    std::vector<schedulerRamp*> schedList;
    std::vector<double> reserveAvailableByScheduler;
    std::vector<double> reserveUsed;

  public:
    explicit ReserveDispatcher(const std::string& objName = "reserveDispatch_#");
    virtual CoreObject* clone(CoreObject* obj = nullptr) const override;
    virtual ~ReserveDispatcher();

    virtual double dynInitializeA(coreTime time0, double dispatchSet);

    void moveSchedulers(ReserveDispatcher* dispatcherToMove);

    virtual double updateP(coreTime time, double pShort);
    virtual double testP(coreTime time, double pShort);
    double getOutput(index_t /*num*/ = 0) { return currentDispatch; }

    virtual void add(schedulerRamp* sched);
    virtual void add(CoreObject* obj) override;

    virtual void remove(schedulerRamp* sched);
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
using reserveDispatcher = ReserveDispatcher;  // NOLINT(readability-identifier-naming)

}  // namespace griddyn
