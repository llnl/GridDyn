/*
 * Copyright (c) 2014-2026, Lawrence Livermore National Security
 * See the top-level NOTICE for additional details. All rights reserved.
 * SPDX-License-Identifier: BSD-3-Clause
 */

#include "grabberSet.h"

#include "gridGrabbers.h"
#include "stateGrabber.h"
#include "utilities/valuePredictor.hpp"
#include <memory>
#include <string>
#include <string_view>
#include <utility>

namespace griddyn {
grabberSet::grabberSet(std::string_view fld, coreObject* obj, bool step_only)
{
    auto ggb = makeGrabbers(fld, obj);
    if (!ggb.empty()) {
        mGrabber = std::move(ggb[0]);
    } else {
        mGrabber = nullptr;  // TODO(phlpt): Consider a default grabber here.
    }
    if (!step_only) {
        auto ggbst = makeStateGrabbers(fld, obj);
        if (!ggbst.empty()) {
            mStateGrabber = std::move(ggbst[0]);
        }
    }
}

grabberSet::grabberSet(index_t noffset, coreObject* obj)
{
    mGrabber = createGrabber(noffset, obj);

    mStateGrabber = std::make_shared<stateGrabber>(noffset, obj);
}

grabberSet::grabberSet(std::shared_ptr<gridGrabber> ggrab, std::shared_ptr<stateGrabber> stgrab):
    mGrabber(std::move(ggrab)), mStateGrabber(std::move(stgrab))
{
}

grabberSet::~grabberSet() = default;

std::unique_ptr<grabberSet> grabberSet::clone() const
{
    auto gset = std::make_unique<grabberSet>(mGrabber->clone(),
                                             (mStateGrabber) ? mStateGrabber->clone() : nullptr);
    if (mPredictor) {
        gset->mPredictor =
            std::make_unique<utilities::valuePredictor<coreTime, double>>(*mPredictor);
    }
    return gset;
}

void grabberSet::cloneTo(grabberSet* gset) const
{
    gset->updateGrabbers(mGrabber->clone(), (mStateGrabber) ? mStateGrabber->clone() : nullptr);
    if (mPredictor) {
        gset->mPredictor =
            std::make_unique<utilities::valuePredictor<coreTime, double>>(*mPredictor);
    }
}

void grabberSet::updateGrabbers(std::shared_ptr<gridGrabber> ggrab,
                                std::shared_ptr<stateGrabber> stgrab)
{
    mGrabber = std::move(ggrab);
    mStateGrabber = std::move(stgrab);
}

void grabberSet::updateField(std::string_view fld)
{
    if (mGrabber) {
        mGrabber->updateField(fld);
    }

    if (mStateGrabber) {
        mStateGrabber->updateField(fld);
    }
}
/** actually go and get the data
 *@return the value produced by the grabber*/
double grabberSet::grabData()
{
    double lastOutput = kNullVal;
    if (mGrabber) {
        lastOutput = mGrabber->grabData();
    } else if (mStateGrabber) {
        lastOutput = grabData(emptyStateData, cLocalSolverMode);
    }
    if (mPredictor) {
        mPredictor->update(lastOutput, mGrabber->getTime());
    }
    return lastOutput;
}
/** @brief grab a vector of data
 *@param[out] data the vector to store the data in
 */
void grabberSet::grabData(std::vector<double>& data)
{
    mGrabber->grabVectorData(data);
}
double grabberSet::grabData(const stateData& stateDataValue, const solverMode& sMode)
{
    if (mStateGrabber) {
        return mStateGrabber->grabData(stateDataValue, sMode);
    }
    if (mPredictor) {
        return mPredictor->predict(stateDataValue.time);
    }
    if (mGrabber) {
        return mGrabber->grabData();
    }
    return kNullVal;
}

void grabberSet::outputPartialDerivatives(const stateData& stateDataValue,
                                          matrixData<double>& matrixDataValue,
                                          const solverMode& sMode)
{
    if (mStateGrabber) {
        mStateGrabber->outputPartialDerivatives(stateDataValue, matrixDataValue, sMode);
    }
}
void grabberSet::getDesc(std::vector<std::string>& desc_list) const
{
    mGrabber->getDesc(desc_list);
}
const std::string& grabberSet::getDesc() const
{
    return mGrabber->getDesc();
}
std::string grabberSet::getDesc()
{
    return mGrabber->getDesc();
}
void grabberSet::setDescription(const std::string& newDesc)
{
    mGrabber->setDescription(newDesc);
}
void grabberSet::updateObject(coreObject* obj, object_update_mode mode)
{
    if (mGrabber) {
        mGrabber->updateObject(obj, mode);
    }
    if (mStateGrabber) {
        mStateGrabber->updateObject(obj, mode);
    }
}

void grabberSet::setGain(double newGain)
{
    if (mGrabber) {
        mGrabber->gain = newGain;
    }
    if (mStateGrabber) {
        mStateGrabber->gain = newGain;
    }
}

coreObject* grabberSet::getObject() const
{
    return mGrabber->getObject();
}
void grabberSet::getObjects(std::vector<coreObject*>& objects) const
{
    if (mGrabber) {
        mGrabber->getObjects(objects);
    }
    if (mStateGrabber) {
        mStateGrabber->getObjects(objects);
    }
}

bool grabberSet::stateCapable() const
{
    if (mStateGrabber) {
        return (mStateGrabber->loaded);
    }
    return false;
}

bool grabberSet::hasJacobian() const
{
    if (mStateGrabber) {
        if (mStateGrabber->getJacobianMode() != jacobian_mode::none) {
            return true;
        }
    }
    return false;
}

}  // namespace griddyn
