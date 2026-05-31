/*
 * Copyright (c) 2014-2026, Lawrence Livermore National Security
 * See the top-level NOTICE for additional details. All rights reserved.
 * SPDX-License-Identifier: BSD-3-Clause
 */

#pragma once

#include "../GridSubModel.h"
#include <string>
#include <vector>

namespace griddyn {
class GridArea;
class Scheduler;

class Dispatcher: public CoreObject {
  public:
  protected:
    double totalDispatch = 0.0;
    double capacity = 0.0;
    double period = 0.0;
    double dispatchTime = 0.0;
    unsigned int schedCount = 0;

    std::vector<Scheduler*> schedList;

  public:
    Dispatcher(const std::string& objName = "dispatcher_#");

    virtual ~Dispatcher();
    virtual CoreObject* clone(CoreObject* obj = nullptr) const override;
    void moveSchedulers(Dispatcher* dis);
    virtual double initialize(CoreTime time0, double dispatch);

    virtual double updateP(CoreTime time, double required, double targetTime);
    virtual double testP(CoreTime time, double required, double targetTime);
    double currentValue() { return totalDispatch; }

    virtual void add(CoreObject* obj) override;
    virtual void add(Scheduler* sched);
    virtual void remove(CoreObject* obj) override;
    virtual void remove(Scheduler* sched);

    virtual void set(std::string_view param, std::string_view val) override;
    virtual void
        set(std::string_view param, double val, units::unit unitType = units::defunit) override;

    virtual void checkGen();

  protected:
    virtual void dispatch(double level);
};
}  // namespace griddyn
