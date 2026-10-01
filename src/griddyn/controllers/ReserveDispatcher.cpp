/*
 * Copyright (c) 2014-2026, Lawrence Livermore National Security
 * See the top-level NOTICE for additional details. All rights reserved.
 * SPDX-License-Identifier: BSD-3-Clause
 */

#include "ReserveDispatcher.h"

#include "../Generator.h"
#include "../GridArea.h"
#include "AGControl.h"
#include "Scheduler.h"
#include "core/CoreExceptions.h"
#include <algorithm>
#include <string>

namespace griddyn {
/*

class reserveDispatcher
{
public:
        std::string name;
        GridArea *Parent;
        bool enabled;

protected:
        double threshold;
        double dispatch
        double reserveAvailable;

        count_t schedCount;

        std::vector<scheduler *> schedList;
        std::vector<double> resAvailable;
        std::vector<double> reserveUsed;

public:
        reserveDispatcher();

        virtual ~reserveDispatcher();

        virtual double initialize(CoreTime time0,double dispatchSet);

        void setTime(CoreTime time);
        virtual double updateP(CoreTime time);
        virtual double testP(CoreTime time);
        double currentValue(){return dispatch;};

        virtual void addGen(scheduler *sched);
        virtual void removeSched(scheduler *sched);
        virtual void set (const std::string &param, double val,unit unitType=defunit);
        virtual void set (const std::string &param, double val,unit unitType=defunit){return
set(param,&val, unitType);};

        double getAvailable(){return sum(&resAvailable)-sum(&reserveUsed);};

        virtual void schedChange();
protected:
        virtual void checkGen();
};

*/
ReserveDispatcher::ReserveDispatcher(const std::string& objName): CoreObject(objName) {}

CoreObject* ReserveDispatcher::clone(CoreObject* obj) const
{
    ReserveDispatcher* nobj;
    if (obj == nullptr) {
        nobj = new ReserveDispatcher();
    } else {
        nobj = dynamic_cast<ReserveDispatcher*>(obj);
        if (nobj == nullptr) {
            // if we can't cast the pointer clone at the next lower level
            CoreObject::clone(obj);
            return obj;
        }
    }
    CoreObject::clone(nobj);
    nobj->thresholdStart = thresholdStart;
    nobj->thresholdStop = thresholdStop;
    nobj->dispatchInterval = dispatchInterval;  // 5 minutes
    return nobj;
}

ReserveDispatcher::~ReserveDispatcher()
{
    for (auto* sched : schedList) {
        if (sched != nullptr && sched->reserveDispatcher == this) {
            sched->reserveDispatcher = nullptr;
        }
    }
    schedList.clear();
    reserveUsed.clear();
    reserveAvailableByScheduler.clear();
    schedCount = 0;
}

void ReserveDispatcher::moveSchedulers(ReserveDispatcher* dispatcherToMove)
{
    if (dispatcherToMove == nullptr || dispatcherToMove == this) {
        return;
    }
    const auto movedSchedulers = dispatcherToMove->schedList;
    for (auto* sched : movedSchedulers) {
        add(sched);
    }
    checkGen();
}

double ReserveDispatcher::dynInitializeA(CoreTime time0, double dispatchSet)
{
    checkGen();
    const double targetDispatch = std::max(0.0, dispatchSet);
    if (targetDispatch != currentDispatch) {
        dispatch(targetDispatch);
        dispatchTime = time0;
    }
    prevTime = time0;
    return currentDispatch;
}

double ReserveDispatcher::updateP(CoreTime time, double pShort)
{
    if (currentDispatch > 0) {
        if (time > (dispatchTime + dispatchInterval)) {
            if (currentDispatch + pShort < thresholdStop) {
                dispatch(0);
                dispatchTime = time;
            } else {
                dispatch(currentDispatch + pShort);
                dispatchTime = time;
            }
        }
    } else {
        if (pShort > thresholdStart) {
            if ((time - dispatchTime) > dispatchInterval) {
                dispatch(pShort);
                dispatchTime = time;
            }
        }
    }
    return currentDispatch;
}

double ReserveDispatcher::testP(CoreTime time, double pShort)
{
    double output = 0;
    if (currentDispatch > 0) {
        if (time > (dispatchTime + dispatchInterval)) {
            if (currentDispatch + pShort > thresholdStop) {
                output = currentDispatch + pShort;
            }
        }
    } else {
        if (pShort > thresholdStart) {
            if ((time - dispatchTime) > dispatchInterval) {
                dispatch(pShort);
                dispatchTime = time;
            }
        }
    }
    return output;
}

void ReserveDispatcher::remove(SchedulerRamp* sched)
{
    const auto schedIter =
        std::find_if(schedList.begin(), schedList.end(), [sched](SchedulerRamp* candidate) {
            return isSameObject(candidate, sched);
        });
    if (schedIter != schedList.end()) {
        schedList.erase(schedIter);
        schedCount = static_cast<count_t>(schedList.size());
        reserveUsed.resize(schedCount);
        reserveAvailableByScheduler.resize(schedCount);
        if (sched != nullptr && sched->reserveDispatcher == this) {
            sched->reserveDispatcher = nullptr;
        }
        checkGen();
    } else if (sched != nullptr && sched->reserveDispatcher == this) {
        sched->reserveDispatcher = nullptr;
    }
}

void ReserveDispatcher::add(CoreObject* obj)
{
    if (dynamic_cast<SchedulerRamp*>(obj) != nullptr) {
        add(static_cast<SchedulerRamp*>(obj));
    } else {
        throw(UnrecognizedObjectException(this));
    }
}

void ReserveDispatcher::add(SchedulerRamp* sched)
{
    if (sched == nullptr) {
        return;
    }
    if (sched->reserveDispatcher != nullptr && sched->reserveDispatcher != this) {
        sched->reserveDispatcher->remove(sched);
    }
    const auto schedIter = std::find_if(
        schedList.begin(), schedList.end(), [sched](SchedulerRamp* candidate) {
            return isSameObject(candidate, sched);
        });
    if (schedIter == schedList.end()) {
        schedList.push_back(sched);
        schedCount = static_cast<count_t>(schedList.size());
        reserveUsed.resize(schedCount);
        reserveAvailableByScheduler.resize(schedCount);
    }
    sched->reserveDispatcher = this;
    checkGen();
}

void ReserveDispatcher::remove(CoreObject* obj)
{
    if (dynamic_cast<SchedulerRamp*>(obj) != nullptr) {
        remove(static_cast<SchedulerRamp*>(obj));
    }
}

void ReserveDispatcher::set(std::string_view param, std::string_view val)
{
    CoreObject::set(param, val);
}

void ReserveDispatcher::set(std::string_view param, double val, units::unit unitType)
{
    if ((param == "threshold") || (param == "thresholdstart")) {
        thresholdStart = val;
        if (thresholdStop > thresholdStart) {
            thresholdStop = thresholdStart / 2;
        }
    } else if (param == "thresholdstop") {
        thresholdStop = val;
    } else if ((param == "dispatchinterval") || (param == "interval")) {
        dispatchInterval = val;
    } else {
        CoreObject::set(param, val, unitType);
    }
}

void ReserveDispatcher::schedChange()
{
    checkGen();
}

void ReserveDispatcher::checkGen()
{
    reserveAvailable = 0;
    currentDispatch = 0.0;
    for (decltype(schedCount) schedIndex = 0; schedIndex < schedCount; ++schedIndex) {
        reserveAvailableByScheduler[schedIndex] = schedList[schedIndex]->getReserveAvailable();
        reserveAvailable += reserveAvailableByScheduler[schedIndex];
        reserveUsed[schedIndex] = schedList[schedIndex]->getReserveTarget();
        currentDispatch += reserveUsed[schedIndex];
    }
}

void ReserveDispatcher::dispatch(double level)
{
    double avail = 0.0;
    int ind = -1;
    // if the dispatch is too low
    while (currentDispatch < level) {
        avail = 0.0;
        ind = -1;
        for (decltype(schedCount) schedIndex = 0; schedIndex < schedCount; ++schedIndex) {
            auto tempAvail = reserveAvailableByScheduler[schedIndex] - reserveUsed[schedIndex];
            if (tempAvail > avail) {
                ind = schedIndex;
                avail = tempAvail;
            }
        }
        if (avail == 0) {
            break;
        }
        if (avail <= (level - currentDispatch)) {
            schedList[ind]->setReserveTarget(reserveUsed[ind] + avail);
        } else {
            auto tempAvail = level - currentDispatch;
            schedList[ind]->setReserveTarget(reserveUsed[ind] + tempAvail);
        }
    }

    // if the dispatch is too high
    while (currentDispatch > level) {
        avail = 0.0;
        ind = -1;
        for (decltype(schedCount) schedIndex = 0; schedIndex < schedCount; ++schedIndex) {
            auto tempAvail = reserveUsed[schedIndex];
            if (tempAvail > avail) {
                ind = schedIndex;
                avail = tempAvail;
            }
        }
        if (avail == 0) {
            break;
        }
        if (avail < (currentDispatch - level)) {
            schedList[ind]->setReserveTarget(0);
        } else {
            auto tempAvail = currentDispatch - level;
            schedList[ind]->setReserveTarget(reserveUsed[ind] - tempAvail);
        }
    }
}

}  // namespace griddyn
