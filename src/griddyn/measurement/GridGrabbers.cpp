/*
 * Copyright (c) 2014-2026, Lawrence Livermore National Security
 * See the top-level NOTICE for additional details. All rights reserved.
 * SPDX-License-Identifier: BSD-3-Clause
 */

#include "GridGrabbers.h"

#include "../Generator.h"
#include "../GridArea.h"
#include "../GridBus.h"
#include "../GridSubModel.h"
#include "../Link.h"
#include "../Load.h"
#include "../Relay.h"
#include "../relays/Sensor.h"
#include "../simulation/GridSimulation.h"
#include "GrabberInterpreter.hpp"
#include "ObjectGrabbers.h"
#include "core/CoreExceptions.h"
#include "gmlc/utilities/vectorOps.hpp"
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
GridGrabber::GridGrabber(std::string_view fld)
{
    GridGrabber::updateField(fld);
}
GridGrabber::GridGrabber(std::string_view fld, CoreObject* obj)
{
    GridGrabber::updateObject(obj);
    GridGrabber::updateField(fld);
}

std::unique_ptr<GridGrabber> GridGrabber::clone() const
{
    auto ggb = std::make_unique<GridGrabber>();
    GridGrabber::cloneTo(ggb.get());
    return ggb;
}

void GridGrabber::cloneTo(GridGrabber* ggb) const
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

static const std::map<std::string, std::function<double(CoreObject*)>> coreFunctions{
    {"nextupdatetime", [](CoreObject* obj) { return obj->getNextUpdateTime(); }},
    {"lastupdatetime", [](CoreObject* obj) { return obj->get("lastupdatetime"); }},
    {"currenttime", [](CoreObject* obj) { return obj->currentTime(); }},
    {"constant", [](CoreObject* /*obj*/) { return 0.0; }},
};

void GridGrabber::updateField(std::string_view fld)
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

const std::string& GridGrabber::getDesc() const
{
    if (mDescription.empty() && loaded) {
        makeDescription();
    }
    return mDescription;
}

void GridGrabber::getDesc(std::vector<std::string>& desc_list) const
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

double GridGrabber::grabData()
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

void GridGrabber::grabVectorData(std::vector<double>& vdata)
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

CoreTime GridGrabber::getTime() const
{
    if (mObject != nullptr) {
        return mObject->currentTime();
    }
    return negTime;
}

void GridGrabber::updateObject(CoreObject* obj, ObjectUpdateMode mode)
{
    if (obj != nullptr) {
        if (mode == ObjectUpdateMode::DIRECT) {
            mObject = obj;
        } else {
            mObject = findMatchingObject(mObject, obj);
            if (mObject == nullptr) {
                throw(ObjectUpdateFailException());
            }
        }
    } else {
        mObject = obj;
    }
    loaded = checkIfLoaded();
}

void GridGrabber::makeDescription() const
{
    if (!customDesc) {
        mDescription = (mObject != nullptr) ? (mObject->getName() + ':' + field) : field;

        if (outputUnits != defunit) {
            mDescription += '(' + to_string(outputUnits) + ')';
        }
    }
}

CoreObject* GridGrabber::getObject() const
{
    return mObject;
}
void GridGrabber::getObjects(std::vector<CoreObject*>& objects) const
{
    objects.push_back(getObject());
}
bool GridGrabber::checkIfLoaded()
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
            catch (const UnrecognizedParameter&) {
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

std::unique_ptr<GridGrabber> createGrabber(std::string_view fld, CoreObject* obj)
{
    std::unique_ptr<GridGrabber> ggb = nullptr;

    auto* bus = dynamic_cast<GridBus*>(obj);
    if (bus != nullptr) {
        ggb = std::make_unique<ObjectGrabber<GridBus>>(fld, bus);
        return ggb;
    }

    auto* loadObject = dynamic_cast<GridLoad*>(obj);
    if (loadObject != nullptr) {
        ggb = std::make_unique<ObjectOffsetGrabber<GridLoad>>(fld, loadObject);
        return ggb;
    }

    auto* gen = dynamic_cast<Generator*>(obj);
    if (gen != nullptr) {
        ggb = std::make_unique<ObjectOffsetGrabber<Generator>>(fld, gen);
        return ggb;
    }

    auto* lnk = dynamic_cast<Link*>(obj);
    if (lnk != nullptr) {
        ggb = std::make_unique<ObjectGrabber<Link>>(fld, lnk);
        return ggb;
    }

    auto* area = dynamic_cast<GridArea*>(obj);
    if (area != nullptr) {
        ggb = std::make_unique<ObjectGrabber<GridArea>>(fld, area);
        return ggb;
    }

    auto* rel = dynamic_cast<Relay*>(obj);
    if (rel != nullptr) {
        ggb = std::make_unique<ObjectGrabber<Relay>>(fld, rel);
        return ggb;
    }

    auto* sub = dynamic_cast<GridSubModel*>(obj);
    if (sub != nullptr) {
        ggb = std::make_unique<ObjectOffsetGrabber<GridSubModel>>(fld, sub);
        return ggb;
    }
    return ggb;
}

std::unique_ptr<GridGrabber> createGrabber(int noffset, CoreObject* obj)
{
    std::unique_ptr<GridGrabber> ggb = nullptr;

    auto* gen = dynamic_cast<Generator*>(obj);
    if (gen != nullptr) {
        ggb = std::make_unique<ObjectOffsetGrabber<Generator>>(noffset, gen);
        return ggb;
    }
    auto* loadObject = dynamic_cast<GridLoad*>(obj);
    if (loadObject != nullptr) {
        ggb = std::make_unique<ObjectOffsetGrabber<GridLoad>>(noffset, loadObject);
        return ggb;
    }
    return ggb;
}

void CustomGrabber::setGrabberFunction(std::string_view fld,
                                       std::function<double(CoreObject*)> nfptr)
{
    mGrabberFunction = std::move(nfptr);
    loaded = true;
    vectorGrab = false;
    field = fld;
}

void CustomGrabber::setGrabberFunction(std::function<void(CoreObject*, std::vector<double>&)> nVptr)
{
    vectorGrab = true;
    mVectorGrabberFunction = std::move(nVptr);
    loaded = true;
}

bool CustomGrabber::checkIfLoaded()
{
    return ((mGrabberFunction) || (mVectorGrabberFunction));
}

FunctionGrabber::FunctionGrabber(std::shared_ptr<GridGrabber> ggb, std::string func):
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

void FunctionGrabber::updateField(std::string_view fld)
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

void FunctionGrabber::getDesc(std::vector<std::string>& desc_list) const
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

std::unique_ptr<GridGrabber> FunctionGrabber::clone() const
{
    std::unique_ptr<GridGrabber> fgrab = std::make_unique<FunctionGrabber>();
    FunctionGrabber::cloneTo(fgrab.get());
    return fgrab;
}

void FunctionGrabber::cloneTo(GridGrabber* ggb) const
{
    GridGrabber::cloneTo(ggb);
    auto* fgb = dynamic_cast<FunctionGrabber*>(ggb);

    if (fgb == nullptr) {
        return;
    }
    fgb->mBaseGrabber = mBaseGrabber->clone();
    fgb->mFunctionName = mFunctionName;
    fgb->mFunctionPtr = mFunctionPtr;
    fgb->mVectorFunctionPtr = mVectorFunctionPtr;
}

double FunctionGrabber::grabData()
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

void FunctionGrabber::grabVectorData(std::vector<double>& vdata)
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

CoreTime FunctionGrabber::getTime() const
{
    if (mBaseGrabber) {
        return mBaseGrabber->getTime();
    }
    return negTime;
}

void FunctionGrabber::updateObject(CoreObject* obj, ObjectUpdateMode mode)
{
    if (mBaseGrabber) {
        mBaseGrabber->updateObject(obj, mode);
    }
    loaded = checkIfLoaded();
}

bool FunctionGrabber::checkIfLoaded()
{
    return (mBaseGrabber->loaded);
}

CoreObject* FunctionGrabber::getObject() const
{
    if (mBaseGrabber) {
        return mBaseGrabber->getObject();
    }
    return nullptr;
}

void FunctionGrabber::getObjects(std::vector<CoreObject*>& objects) const
{
    if (mBaseGrabber) {
        mBaseGrabber->getObjects(objects);
    }
}

// operatorGrabber
OpGrabber::OpGrabber(std::shared_ptr<GridGrabber> ggb1,
                     std::shared_ptr<GridGrabber> ggb2,
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
        loaded = OpGrabber::checkIfLoaded();
    } else if (auto vectorFunctionPtr = get2ArrayFunction(mOperationName)) {
        mFunctionPtr = nullptr;
        mVectorFunctionPtr = vectorFunctionPtr;
        vectorGrab = false;
    }
    loaded = OpGrabber::checkIfLoaded();
}

void OpGrabber::updateField(std::string_view fld)
{
    mOperationName = fld;

    if (auto binaryFunctionPtr = get2ArgFunction(mOperationName)) {
        mFunctionPtr = binaryFunctionPtr;
        mVectorFunctionPtr = nullptr;
        vectorGrab = (mBaseGrabber1) ? mBaseGrabber1->vectorGrab : false;
        loaded = OpGrabber::checkIfLoaded();
    } else if (auto vectorFunctionPtr = get2ArrayFunction(mOperationName)) {
        mFunctionPtr = nullptr;
        mVectorFunctionPtr = vectorFunctionPtr;
        vectorGrab = false;
        loaded = OpGrabber::checkIfLoaded();
    } else {
        mFunctionPtr = nullptr;
        mVectorFunctionPtr = nullptr;
        loaded = false;
    }
}

bool OpGrabber::checkIfLoaded()
{
    return (((mBaseGrabber1) && (mBaseGrabber1->loaded)) &&
            ((mBaseGrabber2) && (mBaseGrabber2->loaded)));
}

void OpGrabber::getDesc(stringVec& desc_list) const
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

std::unique_ptr<GridGrabber> OpGrabber::clone() const
{
    std::unique_ptr<GridGrabber> ograb = std::make_unique<OpGrabber>();
    OpGrabber::cloneTo(ograb.get());
    return ograb;
}

void OpGrabber::cloneTo(GridGrabber* ggb) const
{
    GridGrabber::cloneTo(ggb);
    auto* ogb = dynamic_cast<OpGrabber*>(ggb);

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

double OpGrabber::grabData()
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

void OpGrabber::grabVectorData(std::vector<double>& vdata)
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

void OpGrabber::updateObject(CoreObject* obj, ObjectUpdateMode mode)
{
    if (mBaseGrabber1) {
        mBaseGrabber1->updateObject(obj, mode);
    }
    if (mBaseGrabber2) {
        mBaseGrabber2->updateObject(obj, mode);
    }
}

void OpGrabber::updateObject(CoreObject* obj, int num)
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

CoreTime OpGrabber::getTime() const
{
    if (mBaseGrabber1) {
        return mBaseGrabber1->getTime();
    }
    if (mBaseGrabber2) {
        return mBaseGrabber2->getTime();
    }

    return negTime;
}
CoreObject* OpGrabber::getObject() const
{
    if (mBaseGrabber1) {
        return mBaseGrabber1->getObject();
    }
    return nullptr;
}

void OpGrabber::getObjects(std::vector<CoreObject*>& objects) const
{
    if (mBaseGrabber1) {
        mBaseGrabber1->getObjects(objects);
    }
    if (mBaseGrabber2) {
        mBaseGrabber2->getObjects(objects);
    }
}

}  // namespace griddyn
