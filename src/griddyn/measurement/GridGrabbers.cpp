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
#include "GrabberUnitConversion.hpp"
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
    ggb->customDesc = customDesc;
    ggb->clonable = clonable;
    ggb->mObject = mObject;
}

static const std::map<std::string, std::function<double(CoreObject*)>> CORE_FUNCTIONS{
    {"nextupdatetime", [](CoreObject* obj) { return obj->getNextUpdateTime(); }},
    {"lastupdatetime", [](CoreObject* obj) { return obj->get("lastupdatetime"); }},
    {"currenttime", [](CoreObject* obj) { return obj->currentTime(); }},
    {"constant", [](CoreObject* /*obj*/) { return 0.0; }},
};

void GridGrabber::updateField(std::string_view fld)
{
    if (!prepareField(fld)) {
        return;
    }

    loaded = checkIfLoaded();
}

bool GridGrabber::prepareField(std::string_view fld)
{
    mGrabberFunction = nullptr;
    mVectorGrabberFunction = nullptr;
    mVectorDescriptionFunction = nullptr;
    vectorGrab = false;
    inputUnits = defunit;
    if (!customDesc) {
        mDescription.clear();
    }
    if (fld == "null")  // this is an escape hatch for the clone function
    {
        loaded = false;
        return false;
    }
    field = fld;
    auto fnd = CORE_FUNCTIONS.find(field);
    if (fnd != CORE_FUNCTIONS.end()) {
        mGrabberFunction = fnd->second;
    }
    return true;
}

const std::string& GridGrabber::getDesc() const
{
    if (!customDesc && loaded) {
        makeDescription();
    }
    return mDescription;
}

void GridGrabber::getDesc(std::vector<std::string>& descriptions) const
{
    if (vectorGrab) {
        if (mVectorDescriptionFunction && !customDesc) {
            mVectorDescriptionFunction(mObject, descriptions);
            for (auto& description : descriptions) {
                description += ':' + field;
            }
        } else {
            std::vector<double> values;
            if (mVectorGrabberFunction) {
                mVectorGrabberFunction(mObject, values);
            }
            descriptions.resize(values.size());
            for (size_t index = 0; index < values.size(); ++index) {
                descriptions[index] = getDesc() + '[' + std::to_string(index) + ']';
            }
        }
    } else {
        descriptions.assign(1, getDesc());
    }
}

double GridGrabber::grabData()
{
    if (!loaded || vectorGrab) {
        return kNullVal;
    }
    double val;
    if (mGrabberFunction) {
        val = mGrabberFunction(mObject);
        if (outputUnits != defunit) {
            val = GrabberUnitConversion(inputUnits, outputUnits, mObject).convert(val);
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
    vdata.clear();
    if ((loaded) && (vectorGrab)) {
        mVectorGrabberFunction(mObject, vdata);
        scaleVector(vdata);
    } else {
        vdata.resize(0);
    }
}

void GridGrabber::scaleVector(std::vector<double>& values) const
{
    if (outputUnits != defunit) {
        const GrabberUnitConversion conversion(inputUnits, outputUnits, mObject);
        for (auto& value : values) {
            value = conversion.convert(value);
        }
    }
    for (auto& value : values) {
        value = std::fma(value, gain, bias);
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
    if (!customDesc) {
        mDescription.clear();
    }
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
    return std::make_unique<GridGrabber>(fld, obj);
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
    if (auto* component = dynamic_cast<GridComponent*>(obj)) {
        return std::make_unique<ObjectOffsetGrabber<GridComponent>>(noffset, component);
    }
    return ggb;
}

void CustomGrabber::setGrabberFunction(std::string_view fld,
                                       std::function<double(CoreObject*)> nfptr)
{
    mGrabberFunction = std::move(nfptr);
    mVectorGrabberFunction = nullptr;
    loaded = checkIfLoaded();
    vectorGrab = false;
    field = fld;
}

void CustomGrabber::setGrabberFunction(std::function<void(CoreObject*, std::vector<double>&)> nVptr)
{
    vectorGrab = true;
    mGrabberFunction = nullptr;
    mVectorGrabberFunction = std::move(nVptr);
    loaded = checkIfLoaded();
}

std::unique_ptr<GridGrabber> CustomGrabber::clone() const
{
    auto copy = std::make_unique<CustomGrabber>();
    cloneTo(copy.get());
    return copy;
}

bool CustomGrabber::checkIfLoaded()
{
    return ((mGrabberFunction) || (mVectorGrabberFunction));
}

FunctionGrabber::FunctionGrabber(std::shared_ptr<GridGrabber> ggb, std::string_view func):
    mBaseGrabber(std::move(ggb))
{
    updateField(func);
}

void FunctionGrabber::updateField(std::string_view fld)
{
    mFunctionName = fld;

    if (auto unaryFunctionPtr = get1ArgFunction(mFunctionName)) {
        mFunctionPtr = unaryFunctionPtr;
        mVectorFunctionPtr = nullptr;
        vectorGrab = mBaseGrabber && mBaseGrabber->vectorGrab;
    } else if (auto vectorFunctionPtr = getArrayFunction(mFunctionName)) {
        mFunctionPtr = nullptr;
        mVectorFunctionPtr = vectorFunctionPtr;
        vectorGrab = false;
    } else {
        mFunctionPtr = nullptr;
        mVectorFunctionPtr = nullptr;
        vectorGrab = false;
    }
    if (!customDesc) {
        mDescription.clear();
    }
    loaded = checkIfLoaded();
}

void FunctionGrabber::getDesc(std::vector<std::string>& descriptions) const
{
    if (vectorGrab && mBaseGrabber) {
        stringVec dA1;
        mBaseGrabber->getDesc(dA1);
        descriptions.resize(dA1.size());
        for (size_t kk = 0; kk < dA1.size(); ++kk) {
            descriptions[kk] = customDesc ? getDesc() + '[' + std::to_string(kk) + ']' :
                                            mFunctionName + '(' + dA1[kk] + ')';
        }
    } else {
        descriptions.assign(1, getDesc());
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
    if (ggb == this) {
        return;
    }
    GridGrabber::cloneTo(ggb);
    auto* fgb = dynamic_cast<FunctionGrabber*>(ggb);

    if (fgb == nullptr) {
        return;
    }
    fgb->mBaseGrabber = mBaseGrabber ? mBaseGrabber->clone() : nullptr;
    fgb->mFunctionName = mFunctionName;
    fgb->mFunctionPtr = mFunctionPtr;
    fgb->mVectorFunctionPtr = mVectorFunctionPtr;
}

double FunctionGrabber::grabData()
{
    if (!loaded || vectorGrab) {
        return kNullVal;
    }
    double val;
    if (mVectorFunctionPtr != nullptr) {
        if (mBaseGrabber->vectorGrab) {
            mBaseGrabber->grabVectorData(mTempArray);
        } else {
            mTempArray.assign(1, mBaseGrabber->grabData());
        }
        if (mTempArray.empty()) {
            return kNullVal;
        }
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
    if (!loaded || !vectorGrab) {
        vdata.clear();
        return;
    }
    mBaseGrabber->grabVectorData(vdata);
    for (auto& value : vdata) {
        value = std::fma(mFunctionPtr(value), gain, bias);
    }
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
    updateField(std::string{mFunctionName});
}

bool FunctionGrabber::checkIfLoaded()
{
    return mBaseGrabber && mBaseGrabber->loaded &&
        (mFunctionPtr != nullptr || mVectorFunctionPtr != nullptr);
}

void FunctionGrabber::makeDescription() const
{
    if (!customDesc && mBaseGrabber) {
        mDescription = mFunctionName + '(' + mBaseGrabber->getDesc() + ')';
    }
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
    updateField(mOperationName);
}

void OpGrabber::updateField(std::string_view fld)
{
    mOperationName = fld;

    if (auto binaryFunctionPtr = get2ArgFunction(mOperationName)) {
        mFunctionPtr = binaryFunctionPtr;
        mVectorFunctionPtr = nullptr;
        vectorGrab = (mBaseGrabber1 && mBaseGrabber1->vectorGrab) ||
            (mBaseGrabber2 && mBaseGrabber2->vectorGrab);
        loaded = OpGrabber::checkIfLoaded();
    } else if (auto vectorFunctionPtr = get2ArrayFunction(mOperationName)) {
        mFunctionPtr = nullptr;
        mVectorFunctionPtr = vectorFunctionPtr;
        vectorGrab = false;
        loaded = OpGrabber::checkIfLoaded();
    } else {
        mFunctionPtr = nullptr;
        mVectorFunctionPtr = nullptr;
        vectorGrab = false;
        loaded = false;
    }
    if (!customDesc) {
        mDescription.clear();
    }
}

bool OpGrabber::checkIfLoaded()
{
    return mBaseGrabber1 && mBaseGrabber1->loaded && mBaseGrabber2 && mBaseGrabber2->loaded &&
        (mFunctionPtr != nullptr ||
         (mVectorFunctionPtr != nullptr && mBaseGrabber1->vectorGrab && mBaseGrabber2->vectorGrab));
}

void OpGrabber::getDesc(stringVec& descriptions) const
{
    if (!loaded || !mBaseGrabber1 || !mBaseGrabber2) {
        descriptions.clear();
        return;
    }
    if (vectorGrab) {
        stringVec dA1;
        stringVec dA2;
        mBaseGrabber1->getDesc(dA1);
        mBaseGrabber2->getDesc(dA2);
        const auto count = mBaseGrabber1->vectorGrab ? dA1.size() : dA2.size();
        if (mBaseGrabber1->vectorGrab && mBaseGrabber2->vectorGrab && dA1.size() != dA2.size()) {
            throw InvalidParameterValue("grabber vector lengths do not match");
        }
        descriptions.resize(count);
        for (size_t kk = 0; kk < count; ++kk) {
            descriptions[kk] = customDesc ? getDesc() + '[' + std::to_string(kk) + ']' :
                                            '(' + dA1[mBaseGrabber1->vectorGrab ? kk : 0] +
                    mOperationName + dA2[mBaseGrabber2->vectorGrab ? kk : 0] + ')';
        }
    } else {
        descriptions.assign(1, getDesc());
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
    if (ggb == this) {
        return;
    }
    GridGrabber::cloneTo(ggb);
    auto* ogb = dynamic_cast<OpGrabber*>(ggb);

    if (ogb == nullptr) {
        return;
    }
    ogb->mBaseGrabber1 = mBaseGrabber1 ? mBaseGrabber1->clone() : nullptr;
    ogb->mBaseGrabber2 = mBaseGrabber2 ? mBaseGrabber2->clone() : nullptr;
    ogb->mOperationName = mOperationName;
    ogb->mFunctionPtr = mFunctionPtr;
    ogb->mVectorFunctionPtr = mVectorFunctionPtr;
}

double OpGrabber::grabData()
{
    if (!loaded || vectorGrab) {
        return kNullVal;
    }
    double val;
    if (mVectorFunctionPtr != nullptr) {
        mBaseGrabber1->grabVectorData(mTempArray1);
        mBaseGrabber2->grabVectorData(mTempArray2);
        if (mTempArray1.size() != mTempArray2.size()) {
            throw InvalidParameterValue("grabber vector lengths do not match");
        }
        if (mTempArray1.empty()) {
            return kNullVal;
        }
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
    vdata.clear();
    if (!loaded || !vectorGrab) {
        return;
    }
    if (mBaseGrabber1->vectorGrab) {
        mBaseGrabber1->grabVectorData(mTempArray1);
    }
    if (mBaseGrabber2->vectorGrab) {
        mBaseGrabber2->grabVectorData(mTempArray2);
    }
    if (mBaseGrabber1->vectorGrab && mBaseGrabber2->vectorGrab &&
        mTempArray1.size() != mTempArray2.size()) {
        throw InvalidParameterValue("grabber vector lengths do not match");
    }
    const auto count = mBaseGrabber1->vectorGrab ? mTempArray1.size() : mTempArray2.size();
    const double scalar1 = mBaseGrabber1->vectorGrab ? 0.0 : mBaseGrabber1->grabData();
    const double scalar2 = mBaseGrabber2->vectorGrab ? 0.0 : mBaseGrabber2->grabData();
    vdata.resize(count);
    for (size_t index = 0; index < count; ++index) {
        vdata[index] =
            std::fma(mFunctionPtr(mBaseGrabber1->vectorGrab ? mTempArray1[index] : scalar1,
                                  mBaseGrabber2->vectorGrab ? mTempArray2[index] : scalar2),
                     gain,
                     bias);
    }
}

void OpGrabber::makeDescription() const
{
    if (!customDesc && mBaseGrabber1 && mBaseGrabber2) {
        mDescription =
            '(' + mBaseGrabber1->getDesc() + mOperationName + mBaseGrabber2->getDesc() + ')';
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
    updateField(std::string{mOperationName});
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
    updateField(std::string{mOperationName});
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
