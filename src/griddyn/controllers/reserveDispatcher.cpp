/*
 * Copyright (c) 2014-2026, Lawrence Livermore National Security
 * See the top-level NOTICE for additional details. All rights reserved.
 * SPDX-License-Identifier: BSD-3-Clause
 */

#include "reserveDispatcher.h"

#include "../Area.h"
#include "../Generator.h"
#include "AGControl.h"
#include "core/coreExceptions.h"
#include "scheduler.h"
#include <algorithm>
#include <string>

namespace griddyn {
/*

class reserveDispatcher
{
public:
        std::string name;
        Area *Parent;
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

        virtual double initialize(coreTime time0,double dispatchSet);

        void setTime(coreTime time);
        virtual double updateP(coreTime time);
        virtual double testP(coreTime time);
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
reserveDispatcher::reserveDispatcher(const std::string& objName): coreObject(objName) {}

coreObject* reserveDispatcher::clone(coreObject* obj) const
{
    reserveDispatcher* nobj;
    if (obj == nullptr) {
        nobj = new reserveDispatcher();
    } else {
        nobj = dynamic_cast<reserveDispatcher*>(obj);
        if (nobj == nullptr) {
            // if we can't cast the pointer clone at the next lower level
            coreObject::clone(obj);
            return obj;
        }
    }
    coreObject::clone(nobj);
    nobj->thresholdStart = thresholdStart;
    nobj->thresholdStop = thresholdStop;
    nobj->dispatchInterval = dispatchInterval;  // 5 minutes
    return nobj;
}

reserveDispatcher::~reserveDispatcher()
{
    index_t kk;
    for (kk = 0; kk < schedCount; kk++) {
        // schedList[kk]->reserveDispatcherUnlink();
    }
}

void reserveDispatcher::moveSchedulers(reserveDispatcher* rD)
{
    index_t kk;
    schedList.resize(this->schedCount + rD->schedCount);
    reserveUsed.resize(this->schedCount + rD->schedCount);
    resAvailable.resize(this->schedCount + rD->schedCount);

    for (kk = 0; kk < rD->schedCount; kk++) {
        //    rD->schedList[kk]->reserveDispatcherUnlink();
        this->schedList[this->schedCount + kk] = rD->schedList[kk];
        //    rD->schedList[kk]->reserveDispatcherLink(this);
    }
    schedCount = static_cast<count_t>(schedList.size());
    checkGen();
}

double reserveDispatcher::dynInitializeA(coreTime time0, double dispatchSet)
{
    currDispatch = dispatchSet;
    if (dispatchSet > 0) {
        dispatch(dispatchSet);
        dispatchTime = time0;
    }
    prevTime = time0;
    return currDispatch;
}

double reserveDispatcher::updateP(coreTime time, double pShort)
{
    if (currDispatch > 0) {
        if (time > (dispatchTime + dispatchInterval)) {
            if (currDispatch + pShort < thresholdStop) {
                dispatch(0);
                dispatchTime = time;
            } else {
                dispatch(currDispatch + pShort);
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
    return currDispatch;
}

double reserveDispatcher::testP(coreTime time, double pShort)
{
    double output = 0;
    if (currDispatch > 0) {
        if (time > (dispatchTime + dispatchInterval)) {
            if (currDispatch + pShort > thresholdStop) {
                output = currDispatch + pShort;
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

void reserveDispatcher::remove(schedulerRamp* sched)
{
    const auto schedIter =
        std::find_if(schedList.begin(), schedList.end(), [sched](schedulerRamp* candidate) {
            return isSameObject(candidate, sched);
        });
    if (schedIter != schedList.end()) {
        schedList.erase(schedIter);
        schedCount = static_cast<count_t>(schedList.size());
        checkGen();
    }
}

void reserveDispatcher::add(coreObject* obj)
{
    if (dynamic_cast<schedulerRamp*>(obj) != nullptr) {
        add(static_cast<schedulerRamp*>(obj));
    } else {
        throw(unrecognizedObjectException(this));
    }
}

void reserveDispatcher::add(schedulerRamp* sched)
{
    schedList.push_back(sched);
    schedCount = static_cast<count_t>(schedList.size());
    reserveUsed.resize(schedCount);
    resAvailable.resize(schedCount);
    //    sched->reserveDispatcherLink(this);
    checkGen();
}

void reserveDispatcher::remove(coreObject* obj)
{
    if (dynamic_cast<schedulerRamp*>(obj) != nullptr) {
        remove(static_cast<schedulerRamp*>(obj));
    }
}

void reserveDispatcher::set(std::string_view param, std::string_view val)
{
    coreObject::set(param, val);
}

void reserveDispatcher::set(std::string_view param, double val, units::unit unitType)
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
        coreObject::set(param, val, unitType);
    }
}

void reserveDispatcher::schedChange()
{
    checkGen();
}

void reserveDispatcher::checkGen()
{
    reserveAvailable = 0;
    for (decltype(schedCount) kk = 0; kk < schedCount; kk++) {
        resAvailable[kk] = schedList[kk]->getReserveTarget();
        reserveAvailable += resAvailable[kk];

        reserveUsed[kk] = schedList[kk]->getReserveTarget();
    }
}

void reserveDispatcher::dispatch(double level)
{
    double avail = 0.0;
    int ind = -1;
    // if the dispatch is too low
    while (currDispatch < level) {
        for (decltype(schedCount) kk = 0; kk < schedCount; kk++) {
            auto tempAvail = resAvailable[kk] - reserveUsed[kk];
            if (tempAvail > avail) {
                ind = kk;
                avail = tempAvail;
            }
        }
        if (avail == 0) {
            break;
        }
        if (avail <= (level - currDispatch)) {
            schedList[ind]->setReserveTarget(reserveUsed[ind] + avail);
            reserveUsed[ind] = reserveUsed[ind] + avail;
            currDispatch += avail;
        } else {
            auto tempAvail = level - currDispatch;
            schedList[ind]->setReserveTarget(reserveUsed[ind] + tempAvail);
            reserveUsed[ind] = reserveUsed[ind] + tempAvail;
            currDispatch += tempAvail;
        }
    }

    // if the dispatch is too high
    while (currDispatch > level) {
        for (decltype(schedCount) kk = 0; kk < schedCount; kk++) {
            auto tempAvail = reserveUsed[kk];
            if (tempAvail > avail) {
                ind = kk;
                avail = tempAvail;
            }
        }
        if (avail == 0) {
            break;
        }
        if (avail < (currDispatch - level)) {
            schedList[ind]->setReserveTarget(0);
            reserveUsed[ind] = 0;
            currDispatch -= avail;
        } else {
            auto tempAvail = currDispatch - level;
            schedList[ind]->setReserveTarget(reserveUsed[ind] - tempAvail);
            reserveUsed[ind] = reserveUsed[ind] - tempAvail;
            currDispatch -= tempAvail;
        }
    }
}

}  // namespace griddyn
