/*
 * Copyright (c) 2014-2026, Lawrence Livermore National Security
 * See the top-level NOTICE for additional details. All rights reserved.
 * SPDX-License-Identifier: BSD-3-Clause
 */

#include "gridGrabbers.h"

#include "../Area.h"
#include "../Generator.h"
#include "../Link.h"
#include "../Load.h"
#include "../Relay.h"
#include "../gridBus.h"
#include "../gridSubModel.h"
#include "../relays/sensor.h"
#include "../simulation/gridSimulation.h"
#include "core/coreExceptions.h"
#include "gmlc/utilities/vectorOps.hpp"
#include "grabberInterpreter.hpp"
#include "objectGrabbers.h"
#include "utilities/functionInterpreter.h"
#include <algorithm>
#include <cmath>
#include <map>
#include <memory>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace griddyn {
using units::convert;
using units::defunit;
gridGrabber::gridGrabber(std::string_view fld)
{
    gridGrabber::updateField(fld);
}
gridGrabber::gridGrabber(std::string_view fld, CoreObject* obj)
{
    gridGrabber::updateObject(obj);
    gridGrabber::updateField(fld);
}

std::unique_ptr<gridGrabber> gridGrabber::clone() const
{
    auto ggb = std::make_unique<gridGrabber>();
    gridGrabber::cloneTo(ggb.get());
    return ggb;
}

void gridGrabber::cloneTo(gridGrabber* ggb) const
{
    ggb->mDescription = mDescription;
    ggb->field = field;
    ggb->mGrabberFunction = mGrabberFunction;
    ggb->mVectorGrabberFunction = mVectorGrabberFunction;
    ggb->mVectorDescriptionFunction = mVectorDescriptionFunction;
    ggb->gain = gain;
    ggb->bias = bias;
    ggb->inputUnits = inputUnits;
    ggb->outputUnits = outputUnits;
    ggb->vectorGrab = vectorGrab;
    ggb->loaded = loaded;
    ggb->mObject = mObject;
}

// NOLINTNEXTLINE(bugprone-throwing-static-initialization)
static const std::map<std::string, std::function<double(CoreObject*)>> coreFunctions{
    {"nextupdatetime", [](CoreObject* obj) { return obj->getNextUpdateTime(); }},
    {"lastupdatetime", [](CoreObject* obj) { return obj->get("lastupdatetime"); }},
    {"currenttime", [](CoreObject* obj) { return obj->currentTime(); }},
    {"constant", [](CoreObject* /*obj*/) { return 0.0; }},
};

void gridGrabber::updateField(std::string_view fld)
{
    if (fld == "null")  // this is an escape hatch for the clone function
    {
        loaded = false;
        return;
    }
    field = fld;
    auto fnd = coreFunctions.find(field);
    if (fnd != coreFunctions.end()) {
        mGrabberFunction = fnd->second;
    }

    loaded = checkIfLoaded();
}

const std::string& gridGrabber::getDesc() const
{
    if (mDescription.empty() && loaded) {
        makeDescription();
    }
    return mDescription;
}

void gridGrabber::getDesc(std::vector<std::string>& desc_list) const
{
    if (vectorGrab) {
        mVectorDescriptionFunction(mObject, desc_list);
        for (auto& description : desc_list) {
            description += ':' + field;
        }
    } else {
        desc_list.resize(1);
        desc_list[0] = mDescription;
    }
}

double gridGrabber::grabData()
{
    if (!loaded) {
        return kNullVal;
    }
    double val;
    if (mGrabberFunction) {
        val = mGrabberFunction(mObject);
        if (outputUnits != defunit) {
            val = convert(val,
                          inputUnits,
                          outputUnits,
                          mObject->get("basepower"),
                          mObject->get("basevoltage"));
        }
    } else {
        val = mObject->get(field, outputUnits);
    }
    // val = val * gain + bias;
    val = std::fma(val, gain, bias);
    return val;
}

void gridGrabber::grabVectorData(std::vector<double>& vdata)
{
    if ((loaded) && (vectorGrab)) {
        mVectorGrabberFunction(mObject, vdata);
        if (outputUnits != defunit) {
            auto localBasePower = mObject->get("basepower");
            auto localBaseVoltage = mObject->get("basevoltage");
            for (auto& value : vdata) {
                value = convert(value, inputUnits, outputUnits, localBasePower, localBaseVoltage);
            }
        }
    } else {
        vdata.resize(0);
    }
}

coreTime gridGrabber::getTime() const
{
    if (mObject != nullptr) {
        return mObject->currentTime();
    }
    return negTime;
}

void gridGrabber::updateObject(CoreObject* obj, object_update_mode mode)
{
    if (obj != nullptr) {
        if (mode == object_update_mode::direct) {
            mObject = obj;
        } else {
            mObject = findMatchingObject(mObject, obj);
            if (mObject == nullptr) {
                throw(objectUpdateFailException());
            }
        }
    } else {
        mObject = obj;
    }
    loaded = checkIfLoaded();
}

void gridGrabber::makeDescription() const
{
    if (!customDesc) {
        mDescription = (mObject != nullptr) ? (mObject->getName() + ':' + field) : field;

        if (outputUnits != defunit) {
            mDescription += '(' + to_string(outputUnits) + ')';
        }
    }
}

CoreObject* gridGrabber::getObject() const
{
    return mObject;
}
void gridGrabber::getObjects(std::vector<CoreObject*>& objects) const
{
    objects.push_back(getObject());
}
bool gridGrabber::checkIfLoaded()
{
    if (mObject != nullptr) {
        if ((mGrabberFunction) || (mVectorGrabberFunction)) {
            return true;
        }
        if (!field.empty()) {
            try {
                const double testval = mObject->get(field);
                if (testval != kNullVal) {
                    return true;
                }
            }
            catch (const unrecognizedParameter&) {
                return false;
            }
        } else {
            return false;
        }
    } else if (field == "constant") {
        return true;
    }
    return false;
}

std::unique_ptr<gridGrabber> createGrabber(std::string_view fld, CoreObject* obj)
{
    std::unique_ptr<gridGrabber> ggb = nullptr;

    auto* bus = dynamic_cast<gridBus*>(obj);
    if (bus != nullptr) {
        ggb = std::make_unique<objectGrabber<gridBus>>(fld, bus);
        return ggb;
    }

    auto* loadObject = dynamic_cast<Load*>(obj);
    if (loadObject != nullptr) {
        ggb = std::make_unique<objectOffsetGrabber<Load>>(fld, loadObject);
        return ggb;
    }

    auto* gen = dynamic_cast<Generator*>(obj);
    if (gen != nullptr) {
        ggb = std::make_unique<objectOffsetGrabber<Generator>>(fld, gen);
        return ggb;
    }

    auto* lnk = dynamic_cast<Link*>(obj);
    if (lnk != nullptr) {
        ggb = std::make_unique<objectGrabber<Link>>(fld, lnk);
        return ggb;
    }

    auto* area = dynamic_cast<Area*>(obj);
    if (area != nullptr) {
        ggb = std::make_unique<objectGrabber<Area>>(fld, area);
        return ggb;
    }

    auto* rel = dynamic_cast<Relay*>(obj);
    if (rel != nullptr) {
        ggb = std::make_unique<objectGrabber<Relay>>(fld, rel);
        return ggb;
    }

    auto* sub = dynamic_cast<gridSubModel*>(obj);
    if (sub != nullptr) {
        ggb = std::make_unique<objectOffsetGrabber<gridSubModel>>(fld, sub);
        return ggb;
    }
    return ggb;
}

std::unique_ptr<gridGrabber> createGrabber(int noffset, CoreObject* obj)
{
    std::unique_ptr<gridGrabber> ggb = nullptr;

    auto* gen = dynamic_cast<Generator*>(obj);
    if (gen != nullptr) {
        ggb = std::make_unique<objectOffsetGrabber<Generator>>(noffset, gen);
        return ggb;
    }
    auto* loadObject = dynamic_cast<Load*>(obj);
    if (loadObject != nullptr) {
        ggb = std::make_unique<objectOffsetGrabber<Load>>(noffset, loadObject);
        return ggb;
    }
    return ggb;
}

void customGrabber::setGrabberFunction(std::string_view fld,
                                       std::function<double(CoreObject*)> nfptr)
{
    mGrabberFunction = std::move(nfptr);
    loaded = true;
    vectorGrab = false;
    field = fld;
}

void customGrabber::setGrabberFunction(std::function<void(CoreObject*, std::vector<double>&)> nVptr)
{
    vectorGrab = true;
    mVectorGrabberFunction = std::move(nVptr);
    loaded = true;
}

bool customGrabber::checkIfLoaded()
{
    return ((mGrabberFunction) || (mVectorGrabberFunction));
}

functionGrabber::functionGrabber(std::shared_ptr<gridGrabber> ggb, std::string func):
    mBaseGrabber(std::move(ggb))
{
    mFunctionName = std::move(func);

    if (auto unaryFunctionPtr = get1ArgFunction(mFunctionName)) {
        mFunctionPtr = unaryFunctionPtr;
        mVectorFunctionPtr = nullptr;
        vectorGrab = mBaseGrabber->vectorGrab;
        if (mBaseGrabber->loaded) {
            loaded = true;
        }
    } else if (auto vectorFunctionPtr = getArrayFunction(mFunctionName)) {
        mFunctionPtr = nullptr;
        mVectorFunctionPtr = vectorFunctionPtr;
        vectorGrab = false;
        if (mBaseGrabber->loaded) {
            loaded = true;
        }
    }
}

void functionGrabber::updateField(std::string_view fld)
{
    mFunctionName = fld;

    if (auto unaryFunctionPtr = get1ArgFunction(mFunctionName)) {
        mFunctionPtr = unaryFunctionPtr;
        mVectorFunctionPtr = nullptr;
        vectorGrab = mBaseGrabber->vectorGrab;
    } else if (auto vectorFunctionPtr = getArrayFunction(mFunctionName)) {
        mFunctionPtr = nullptr;
        mVectorFunctionPtr = vectorFunctionPtr;
        vectorGrab = false;
    } else {
        mFunctionPtr = nullptr;
        mVectorFunctionPtr = nullptr;
    }
    loaded = checkIfLoaded();
}

void functionGrabber::getDesc(std::vector<std::string>& desc_list) const
{
    if (vectorGrab) {
        stringVec dA1;
        mBaseGrabber->getDesc(dA1);
        desc_list.resize(dA1.size());
        for (size_t kk = 0; kk < dA1.size(); ++kk) {
            desc_list[kk] = mFunctionName + '(' + dA1[kk] + ')';
        }
    } else {
        stringVec dA1;
        mBaseGrabber->getDesc(dA1);
        desc_list.resize(dA1.size());
        desc_list[0] = mFunctionName + '(' + dA1[0] + ')';
    }
}

std::unique_ptr<gridGrabber> functionGrabber::clone() const
{
    std::unique_ptr<gridGrabber> fgrab = std::make_unique<functionGrabber>();
    functionGrabber::cloneTo(fgrab.get());
    return fgrab;
}

void functionGrabber::cloneTo(gridGrabber* ggb) const
{
    gridGrabber::cloneTo(ggb);
    auto* fgb = dynamic_cast<functionGrabber*>(ggb);

    if (fgb == nullptr) {
        return;
    }
    fgb->mBaseGrabber = mBaseGrabber->clone();
    fgb->mFunctionName = mFunctionName;
    fgb->mFunctionPtr = mFunctionPtr;
    fgb->mVectorFunctionPtr = mVectorFunctionPtr;
}

double functionGrabber::grabData()
{
    double val;
    if (mBaseGrabber->vectorGrab) {
        mBaseGrabber->grabVectorData(mTempArray);
        val = mVectorFunctionPtr(mTempArray);
    } else {
        const double temp = mBaseGrabber->grabData();
        val = mFunctionPtr(temp);
    }

    val = std::fma(val, gain, bias);
    return val;
}

void functionGrabber::grabVectorData(std::vector<double>& vdata)
{
    if (mBaseGrabber->vectorGrab) {
        mBaseGrabber->grabVectorData(mTempArray);
        vdata.resize(mTempArray.size());
    } else {
        mTempArray.assign(1, mBaseGrabber->grabData());
        vdata.resize(1);
    }
    std::transform(mTempArray.begin(), mTempArray.end(), vdata.begin(), mFunctionPtr);
}

coreTime functionGrabber::getTime() const
{
    if (mBaseGrabber) {
        return mBaseGrabber->getTime();
    }
    return negTime;
}

void functionGrabber::updateObject(CoreObject* obj, object_update_mode mode)
{
    if (mBaseGrabber) {
        mBaseGrabber->updateObject(obj, mode);
    }
    loaded = checkIfLoaded();
}

bool functionGrabber::checkIfLoaded()
{
    return (mBaseGrabber->loaded);
}

CoreObject* functionGrabber::getObject() const
{
    if (mBaseGrabber) {
        return mBaseGrabber->getObject();
    }
    return nullptr;
}

void functionGrabber::getObjects(std::vector<CoreObject*>& objects) const
{
    if (mBaseGrabber) {
        mBaseGrabber->getObjects(objects);
    }
}

// operatorGrabber
opGrabber::opGrabber(std::shared_ptr<gridGrabber> ggb1,
                     std::shared_ptr<gridGrabber> ggb2,
                     std::string operationName): mOperationName(std::move(operationName))
{
    if (ggb1) {
        mBaseGrabber1 = std::move(ggb1);
    }
    if (ggb2) {
        mBaseGrabber2 = std::move(ggb2);
    }
    if (auto binaryFunctionPtr = get2ArgFunction(mOperationName)) {
        mFunctionPtr = binaryFunctionPtr;
        mVectorFunctionPtr = nullptr;
        vectorGrab = (mBaseGrabber1) ? mBaseGrabber1->vectorGrab : false;
        loaded = opGrabber::checkIfLoaded();
    } else if (auto vectorFunctionPtr = get2ArrayFunction(mOperationName)) {
        mFunctionPtr = nullptr;
        mVectorFunctionPtr = vectorFunctionPtr;
        vectorGrab = false;
    }
    loaded = opGrabber::checkIfLoaded();
}

void opGrabber::updateField(std::string_view fld)
{
    mOperationName = fld;

    if (auto binaryFunctionPtr = get2ArgFunction(mOperationName)) {
        mFunctionPtr = binaryFunctionPtr;
        mVectorFunctionPtr = nullptr;
        vectorGrab = (mBaseGrabber1) ? mBaseGrabber1->vectorGrab : false;
        loaded = opGrabber::checkIfLoaded();
    } else if (auto vectorFunctionPtr = get2ArrayFunction(mOperationName)) {
        mFunctionPtr = nullptr;
        mVectorFunctionPtr = vectorFunctionPtr;
        vectorGrab = false;
        loaded = opGrabber::checkIfLoaded();
    } else {
        mFunctionPtr = nullptr;
        mVectorFunctionPtr = nullptr;
        loaded = false;
    }
}

bool opGrabber::checkIfLoaded()
{
    return (((mBaseGrabber1) && (mBaseGrabber1->loaded)) &&
            ((mBaseGrabber2) && (mBaseGrabber2->loaded)));
}

void opGrabber::getDesc(stringVec& desc_list) const
{
    if (vectorGrab) {
        stringVec dA1;
        stringVec dA2;
        mBaseGrabber1->getDesc(dA1);
        mBaseGrabber2->getDesc(dA2);
        desc_list.resize(dA1.size());
        for (size_t kk = 0; kk < dA1.size(); ++kk) {
            desc_list[kk] = dA1[kk] + mOperationName + dA2[kk];
        }
    } else {
        stringVec dA1;
        stringVec dA2;
        mBaseGrabber1->getDesc(dA1);
        mBaseGrabber2->getDesc(dA2);
        desc_list.resize(dA1.size());
        desc_list[0] = dA1[0] + mOperationName + dA2[0];
    }
}

std::unique_ptr<gridGrabber> opGrabber::clone() const
{
    std::unique_ptr<gridGrabber> ograb = std::make_unique<opGrabber>();
    opGrabber::cloneTo(ograb.get());
    return ograb;
}

void opGrabber::cloneTo(gridGrabber* ggb) const
{
    gridGrabber::cloneTo(ggb);
    auto* ogb = dynamic_cast<opGrabber*>(ggb);

    if (ogb == nullptr) {
        return;
    }
    if (mBaseGrabber1) {
        ogb->mBaseGrabber1 = mBaseGrabber1->clone();
    }
    if (mBaseGrabber2) {
        ogb->mBaseGrabber2 = mBaseGrabber2->clone();
    }
    ogb->mOperationName = mOperationName;
    ogb->mFunctionPtr = mFunctionPtr;
    ogb->mVectorFunctionPtr = mVectorFunctionPtr;
}

double opGrabber::grabData()
{
    double val;
    if (mBaseGrabber1->vectorGrab) {
        mBaseGrabber1->grabVectorData(mTempArray1);
        mBaseGrabber2->grabVectorData(mTempArray2);
        val = mVectorFunctionPtr(mTempArray1, mTempArray2);
    } else {
        const double grabberValue1 = mBaseGrabber1->grabData();
        const double grabberValue2 = mBaseGrabber2->grabData();
        val = mFunctionPtr(grabberValue1, grabberValue2);
    }
    val = std::fma(val, gain, bias);
    return val;
}

void opGrabber::grabVectorData(std::vector<double>& vdata)
{
    if (mBaseGrabber1->vectorGrab) {
        vdata.resize(mTempArray1.size());
        mBaseGrabber1->grabVectorData(mTempArray1);
        mBaseGrabber2->grabVectorData(mTempArray2);
        std::transform(mTempArray1.begin(),
                       mTempArray1.end(),
                       mTempArray2.begin(),
                       vdata.begin(),
                       mFunctionPtr);
    }
}

void opGrabber::updateObject(CoreObject* obj, object_update_mode mode)
{
    if (mBaseGrabber1) {
        mBaseGrabber1->updateObject(obj, mode);
    }
    if (mBaseGrabber2) {
        mBaseGrabber2->updateObject(obj, mode);
    }
}

void opGrabber::updateObject(CoreObject* obj, int num)
{
    if (num == 1) {
        if (mBaseGrabber1) {
            mBaseGrabber1->updateObject(obj);
        }
    } else if (num == 2) {
        if (mBaseGrabber2) {
            mBaseGrabber2->updateObject(obj);
        }
    }
}

coreTime opGrabber::getTime() const
{
    if (mBaseGrabber1) {
        return mBaseGrabber1->getTime();
    }
    if (mBaseGrabber2) {
        return mBaseGrabber2->getTime();
    }

    return negTime;
}
CoreObject* opGrabber::getObject() const
{
    if (mBaseGrabber1) {
        return mBaseGrabber1->getObject();
    }
    return nullptr;
}

void opGrabber::getObjects(std::vector<CoreObject*>& objects) const
{
    if (mBaseGrabber1) {
        mBaseGrabber1->getObjects(objects);
    }
    if (mBaseGrabber2) {
        mBaseGrabber2->getObjects(objects);
    }
}

}  // namespace griddyn

