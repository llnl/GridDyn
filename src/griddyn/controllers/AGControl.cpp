/*
 * Copyright (c) 2014-2026, Lawrence Livermore National Security
 * See the top-level NOTICE for additional details. All rights reserved.
 * SPDX-License-Identifier: BSD-3-Clause
 */

#include "AGControl.h"

#include "../Area.h"
#include "../Generator.h"
#include "../blocks/blockLibrary.h"
#include "core/coreExceptions.h"
#include "core/coreObjectTemplates.hpp"
#include "core/objectFactoryTemplates.hpp"
#include "scheduler.h"
#include <string>

namespace griddyn {
/*
class AGControl
{
public:
        char name[32];
        Area *Parent;

protected:
        double KI;
        double KP;
        double beta;
        double deadband;
        double alpha;

        double ACE;
        double fACE;
        double reg;
        double regUpAvailable;
        double regDownAvailable;

        pidBlock *pid;

        int schedCount;

        std::vector<scheduler *> schedList;
        double upRat;
        double downRat;

public:
        AGControl();

        ~AGControl();

        double initialize(coreTime time0,double freq0,double tiedev0);

        double updateP(coreTime time, double freq, double tiedev);
        double currentValue();

        double addGen(scheduler *sched);
        void set (const std::string &param, double val,unit unitType=defunit);
        void set (const std::string &param, double val,unit unitType=defunit){return set(param,&val,
unitType);};

        void regChange();
protected:
        void checkGen();
};
*/

static typeFactory<Block> bbof("agc", std::to_array<std::string_view>({"basic", "agc"}), "basic");

AGControl::~AGControl() = default;

AGControl::AGControl(const std::string& objName): GridSubModel(objName)
{
    pid = make_owningPtr<blocks::pidBlock>(KP, KI, 0, "pid");
    pid->setParent(this);
    filt1 = make_owningPtr<blocks::delayBlock>(Tf, "delay1");
    filt1->setParent(this);
    filt2 = make_owningPtr<blocks::delayBlock>(Tr, "delay2");
    filt2->setParent(this);
    db = make_owningPtr<blocks::deadbandBlock>(deadband, "deadband");
    db->setParent(this);
    db->set("rampband", 4);
}

CoreObject* AGControl::clone(CoreObject* obj) const
{
    auto nobj = cloneBase<AGControl, GridSubModel>(this, obj);
    if (nobj == nullptr) {
        return obj;
    }

    nobj->KI = KI;
    nobj->KP = KP;
    nobj->beta = beta;
    nobj->deadband = deadband;

    nobj->Tf = Tf;
    nobj->Tr = Tr;

    pid->clone(nobj->pid.get());
    filt1->clone(nobj->filt1.get());
    filt2->clone(nobj->filt2.get());
    db->clone(nobj->db.get());
    return nobj;
}

double AGControl::getOutput(const IOdata& /*inputs*/,
                            const stateData& /*sD*/,
                            const solverMode& /*sMode*/,
                            index_t /*outNum*/) const
{
    return reg;
}

double AGControl::getOutput(index_t /*outNum*/) const
{
    return reg;
}
void AGControl::dynObjectInitializeB(const IOdata& inputs,
                                     const IOdata& desiredOutput,
                                     IOdata& fieldSet)
{
    IOdata iSet(1);
    if (desiredOutput.empty()) {
        ACE = (inputs[1]) - 10.0 * beta * inputs[0];
    } else {
        ACE = desiredOutput[0];
    }
    filt1->dynInitializeB({0}, {ACE}, iSet);
    fACE = ACE;
    pid->dynInitializeB({0}, {fACE}, iSet);
    // freg=filt2->dynInitializeB(time0,reg);
    // freg=db->updateA(time0,freg);
    fieldSet[0] = pid->getOutput();
}

void AGControl::updateA(coreTime /*time*/) {}

void AGControl::timestep(coreTime time, const IOdata& inputs, const solverMode& /*sMode*/)
{
    prevTime = time;

    ACE = (inputs[1]) - 10.0 * beta * inputs[0];
    fACE = filt1->step(time, ACE);

    reg += pid->step(time, fACE - reg);

    reg = db->step(time, reg);

    freg = filt2->step(time, reg);

    for (index_t kk = 0; kk < schedCount; kk++) {
        if (freg >= 0.0) {
            if (freg > regUpAvailable) {
                schedList[kk]->setReg(regUpAvailable * upRat[kk]);
                reg = regUpAvailable;
            } else {
                schedList[kk]->setReg(freg * upRat[kk]);
            }
        } else {
            if (freg < -regDownAvailable) {
                schedList[kk]->setReg(-regDownAvailable * downRat[kk]);
                reg = -regDownAvailable;
            } else {
                schedList[kk]->setReg(freg * downRat[kk]);
            }
        }
    }
}

void AGControl::add(CoreObject* obj)
{
    if (dynamic_cast<schedulerReg*>(obj) != nullptr) {
        add(static_cast<schedulerReg*>(obj));
    } else {
        throw(unrecognizedObjectException(this));
    }
}

void AGControl::add(schedulerReg* sched)
{
    schedCount++;
    schedList.push_back(sched);
    // sched->AGClink(this);
    upRat.resize(schedCount);
    downRat.resize(schedCount);
    regChange();
}

void AGControl::remove(CoreObject* obj)
{
    for (index_t kk = 0; kk < schedCount; kk++) {
        if (isSameObject(schedList[kk], obj)) {
            schedList.erase(schedList.begin() + kk);
            schedCount--;
            upRat.resize(schedCount);
            downRat.resize(schedCount);
            regChange();
            break;
        }
    }
}

void AGControl::set(std::string_view param, std::string_view val)
{
    CoreObject::set(param, val);
}

void AGControl::set(std::string_view param, double val, units::unit unitType)
{
    if (param == "deadband") {
        deadband = val;
        db->set("deadband", deadband);
        db->set("rampband", 0.2 * deadband);
    } else if (param == "beta") {
        beta = val;
    } else if (param == "ki") {
        KI = val;
        pid->set("I", val);
    } else if (param == "kp") {
        KP = val;
        pid->set("P", val);
    } else if (param == "tf") {
        Tf = val;
        filt1->set("T1", Tf);
    } else if (param == "tr") {
        Tr = val;
        filt2->set("T1", Tr);
    } else {
        CoreObject::set(param, val, unitType);
    }
}

void AGControl::regChange()
{
    regUpAvailable = 0;
    regDownAvailable = 0;

    for (auto& sched : schedList) {
        regUpAvailable += sched->getRegUpAvailable();
        regDownAvailable += sched->getRegDownAvailable();
    }
    for (index_t kk = 0; kk < schedCount; kk++) {
        upRat[kk] = schedList[kk]->getRegUpAvailable() / regUpAvailable;
        downRat[kk] = schedList[kk]->getRegDownAvailable() / regDownAvailable;
    }
}

AGControl* newAGC(const std::string& type)
{
    AGControl* agc = nullptr;
    if ((type.empty()) || (type == "basic")) {
        agc = new AGControl();
    } else if (type == "battery") {
        // agc = new AGControlBattery();
    } else if (type == "battDR") {
        // agc= new AGCControlBattDR();
    }
    return agc;
}

}  // namespace griddyn

