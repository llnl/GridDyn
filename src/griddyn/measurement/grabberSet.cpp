/*
 * Copyright (c) 2014-2020, Lawrence Livermore National Security
 * See the top-level NOTICE for additional details. All rights reserved.
 * SPDX-License-Identifier: BSD-3-Clause
 */

#include "grabberSet.h"

#include "gridGrabbers.h"
#include "stateGrabber.h"
#include "utilities/valuePredictor.hpp"

namespace griddyn {
grabberSet::grabberSet(const std::string& fld, coreObject* obj, bool step_only)
{
    auto ggb = makeGrabbers(fld, obj);
    if (!ggb.empty()) {
        grab = std::move(ggb[0]);
    } else {
        grab = nullptr;  // TODO(phlpt): Consider a default grabber here.
    }
    if (!step_only) {
        auto ggbst = makeStateGrabbers(fld, obj);
        if (!ggbst.empty()) {
            stGrab = std::move(ggbst[0]);
        }
    }
}

grabberSet::grabberSet(index_t noffset, coreObject* obj)
{
    grab = createGrabber(noffset, obj);

    stGrab = std::make_shared<stateGrabber>(noffset, obj);
}

grabberSet::grabberSet(std::shared_ptr<gridGrabber> ggrab, std::shared_ptr<stateGrabber> stgrab):
    grab(std::move(ggrab)), stGrab(std::move(stgrab))
{
}

grabberSet::~grabberSet() = default;

std::unique_ptr<grabberSet> grabberSet::clone() const
{
    auto gset = std::make_unique<grabberSet>(grab->clone(), (stGrab) ? stGrab->clone() : nullptr);
    if (predictor) {
        gset->predictor = std::make_unique<utilities::valuePredictor<coreTime, double>>(*predictor);
    }
    return gset;
}

void grabberSet::cloneTo(grabberSet* ggb) const
{
    ggb->updateGrabbers(grab->clone(), (stGrab) ? stGrab->clone() : nullptr);
    if (predictor) {
        ggb->predictor = std::make_unique<utilities::valuePredictor<coreTime, double>>(*predictor);
    }
}

void grabberSet::updateGrabbers(std::shared_ptr<gridGrabber> ggrab,
                                std::shared_ptr<stateGrabber> stgrab)
{
    grab = std::move(ggrab);
    stGrab = std::move(stgrab);
}

void grabberSet::updateField(const std::string& fld)
{
    if (grab) {
        grab->updateField(fld);
    }

    if (stGrab) {
        stGrab->updateField(fld);
    }
}
/** actually go and get the data
 *@return the value produced by the grabber*/
double grabberSet::grabData()
{
    auto lastOutput = (grab) ? grab->grabData() :
                               ((stGrab) ? grabData(emptyStateData, cLocalSolverMode) : kNullVal);
    if (predictor) {
        predictor->update(lastOutput, grab->getTime());
    }
    return lastOutput;
}
/** @brief grab a vector of data
 *@param[out] data the vector to store the data in
 */
void grabberSet::grabData(std::vector<double>& data)
{
    grab->grabVectorData(data);
}
double grabberSet::grabData(const stateData& sD, const solverMode& sMode)
{
    if (stGrab) {
        return stGrab->grabData(sD, sMode);
    }
    if (predictor) {
        return predictor->predict(sD.time);
    }
    if (grab) {
        return grab->grabData();
    }
    return kNullVal;
}

void grabberSet::outputPartialDerivatives(const stateData& sD,
                                          matrixData<double>& md,
                                          const solverMode& sMode)
{
    if (stGrab) {
        stGrab->outputPartialDerivatives(sD, md, sMode);
    }
}
void grabberSet::getDesc(std::vector<std::string>& desc_list) const
{
    grab->getDesc(desc_list);
}
const std::string& grabberSet::getDesc() const
{
    return grab->getDesc();
}
std::string grabberSet::getDesc()
{
    return grab->getDesc();
}
void grabberSet::setDescription(const std::string& newDesc)
{
    grab->setDescription(newDesc);
}
void grabberSet::updateObject(coreObject* obj, object_update_mode mode)
{
    if (grab) {
        grab->updateObject(obj, mode);
    }
    if (stGrab) {
        stGrab->updateObject(obj, mode);
    }
}

void grabberSet::setGain(double newGain)
{
    if (grab) {
        grab->gain = newGain;
    }
    if (stGrab) {
        stGrab->gain = newGain;
    }
}

coreObject* grabberSet::getObject() const
{
    return grab->getObject();
}
void grabberSet::getObjects(std::vector<coreObject*>& objects) const
{
    if (grab) {
        grab->getObjects(objects);
    }
    if (stGrab) {
        stGrab->getObjects(objects);
    }
}

bool grabberSet::stateCapable() const
{
    if (stGrab) {
        return (stGrab->loaded);
    }
    return false;
}

bool grabberSet::hasJacobian() const
{
    if (stGrab) {
        if (stGrab->getJacobianMode() != jacobian_mode::none) {
            return true;
        }
    }
    return false;
}

}  // namespace griddyn
