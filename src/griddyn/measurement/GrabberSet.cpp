/*
 * Copyright (c) 2014-2026, Lawrence Livermore National Security
 * See the top-level NOTICE for additional details. All rights reserved.
 * SPDX-License-Identifier: BSD-3-Clause
 */

#include "GrabberSet.h"

#include "GridGrabbers.h"
#include "StateGrabber.h"
#include "utilities/valuePredictor.hpp"
#include <memory>
#include <string>
#include <string_view>
#include <utility>

namespace griddyn {
GrabberSet::GrabberSet(std::string_view fld, CoreObject* obj, bool step_only)
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

GrabberSet::GrabberSet(index_t noffset, CoreObject* obj)
{
    mGrabber = createGrabber(noffset, obj);

    mStateGrabber = std::make_shared<StateGrabber>(noffset, obj);
}

GrabberSet::GrabberSet(std::shared_ptr<GridGrabber> ggrab, std::shared_ptr<StateGrabber> stgrab):
    mGrabber(std::move(ggrab)), mStateGrabber(std::move(stgrab))
{
}

GrabberSet::~GrabberSet() = default;

std::unique_ptr<GrabberSet> GrabberSet::clone() const
{
    auto gset = std::make_unique<GrabberSet>(mGrabber->clone(),
                                             (mStateGrabber) ? mStateGrabber->clone() : nullptr);
    if (mPredictor) {
        gset->mPredictor =
            std::make_unique<utilities::valuePredictor<CoreTime, double>>(*mPredictor);
    }
    return gset;
}

void GrabberSet::cloneTo(GrabberSet* gset) const
{
    gset->updateGrabbers(mGrabber->clone(), (mStateGrabber) ? mStateGrabber->clone() : nullptr);
    if (mPredictor) {
        gset->mPredictor =
            std::make_unique<utilities::valuePredictor<CoreTime, double>>(*mPredictor);
    }
}

void GrabberSet::updateGrabbers(std::shared_ptr<GridGrabber> ggrab,
                                std::shared_ptr<StateGrabber> stgrab)
{
    mGrabber = std::move(ggrab);
    mStateGrabber = std::move(stgrab);
}

void GrabberSet::updateField(std::string_view fld)
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
double GrabberSet::grabData()
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
void GrabberSet::grabData(std::vector<double>& data)
{
    mGrabber->grabVectorData(data);
}
double GrabberSet::grabData(const StateData& stateDataValue, const SolverMode& sMode)
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

void GrabberSet::outputPartialDerivatives(const StateData& stateDataValue,
                                          MatrixData<double>& matrixDataValue,
                                          const SolverMode& sMode)
{
    if (mStateGrabber) {
        mStateGrabber->outputPartialDerivatives(stateDataValue, matrixDataValue, sMode);
    }
}
void GrabberSet::getDesc(std::vector<std::string>& desc_list) const
{
    mGrabber->getDesc(desc_list);
}
const std::string& GrabberSet::getDesc() const
{
    return mGrabber->getDesc();
}
std::string GrabberSet::getDesc()
{
    return mGrabber->getDesc();
}
void GrabberSet::setDescription(const std::string& newDesc)
{
    mGrabber->setDescription(newDesc);
}
void GrabberSet::updateObject(CoreObject* obj, ObjectUpdateMode mode)
{
    if (mGrabber) {
        mGrabber->updateObject(obj, mode);
    }
    if (mStateGrabber) {
        mStateGrabber->updateObject(obj, mode);
    }
}

void GrabberSet::setGain(double newGain)
{
    if (mGrabber) {
        mGrabber->gain = newGain;
    }
    if (mStateGrabber) {
        mStateGrabber->gain = newGain;
    }
}

CoreObject* GrabberSet::getObject() const
{
    return mGrabber->getObject();
}
void GrabberSet::getObjects(std::vector<CoreObject*>& objects) const
{
    if (mGrabber) {
        mGrabber->getObjects(objects);
    }
    if (mStateGrabber) {
        mStateGrabber->getObjects(objects);
    }
}

bool GrabberSet::stateCapable() const
{
    if (mStateGrabber) {
        return (mStateGrabber->loaded);
    }
    return false;
}

bool GrabberSet::hasJacobian() const
{
    if (mStateGrabber) {
        if (mStateGrabber->getJacobianMode() != JacobianMode::NONE) {
            return true;
        }
    }
    return false;
}

}  // namespace griddyn
