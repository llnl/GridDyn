/*
 * Copyright (c) 2014-2026, Lawrence Livermore National Security
 * See the top-level NOTICE for additional details. All rights reserved.
 * SPDX-License-Identifier: BSD-3-Clause
 */

#pragma once

#include "../gridSubModel.h"
#include <string>
#include <vector>

namespace griddyn {
class Area;
class scheduler;

class dispatcher: public coreObject {
  public:
  protected:
    double totalDispatch;
    double capacity;
    double period;
    double dispatchTime;
    unsigned int schedCount;

    std::vector<scheduler*> schedList;

  public:
    dispatcher(const std::string& objName = "dispatcher_#");

    virtual ~dispatcher();
    virtual coreObject* clone(coreObject* obj = nullptr) const override;
    void moveSchedulers(dispatcher* dis);
    virtual double initialize(coreTime time0, double dispatch);

    virtual double updateP(coreTime time, double required, double targetTime);
    virtual double testP(coreTime time, double required, double targetTime);
    double currentValue() { return totalDispatch; }

    virtual void add(coreObject* obj) override;
    virtual void add(scheduler* sched);
    virtual void remove(coreObject* obj) override;
    virtual void remove(scheduler* sched);

    virtual void set(std::string_view param, std::string_view val) override;
    virtual void
        set(std::string_view param, double val, units::unit unitType = units::defunit) override;

    virtual void checkGen();

  protected:
    virtual void dispatch(double level);
};

}  // namespace griddyn
