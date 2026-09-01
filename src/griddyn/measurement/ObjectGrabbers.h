/*
 * Copyright (c) 2014-2026, Lawrence Livermore National Security
 * See the top-level NOTICE for additional details. All rights reserved.
 * SPDX-License-Identifier: BSD-3-Clause
 */

#pragma once

#include "../solvers/SolverMode.hpp"
#include "GridGrabbers.h"
#include "core/CoreExceptions.h"
#include "core/ObjectInterpreter.h"
#include <memory>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace griddyn {
class GridSubModel;
class GridComponent;
class GridBus;
class GridLoad;
class Link;
class Generator;
class GridArea;
class Relay;
class GridSubModel;

using fobjectPair = std::pair<std::function<double(CoreObject*)>, units::unit>;

fobjectPair getObjectFunction(const GridComponent* comp, std::string_view field);
fobjectPair getObjectFunction(const GridBus* bus, std::string_view field);
fobjectPair getObjectFunction(const GridLoad* loadObject, std::string_view field);
fobjectPair getObjectFunction(const Link* lnk, std::string_view field);
fobjectPair getObjectFunction(const Generator* gen, std::string_view field);
fobjectPair getObjectFunction(const GridArea* area, std::string_view field);
fobjectPair getObjectFunction(const Relay* rel, std::string_view field);
fobjectPair getObjectFunction(const GridSubModel* sub, std::string_view field);

using fvecPair = std::pair<std::function<void(CoreObject*, std::vector<double>&)>, units::unit>;

fvecPair getObjectVectorFunction(const GridComponent* comp, std::string_view field);

fvecPair getObjectVectorFunction(const GridArea* area, std::string_view field);

using descVecFunc = std::function<void(CoreObject*, stringVec&)>;

descVecFunc getObjectVectorDescFunction(const GridComponent* comp, std::string_view field);
descVecFunc getObjectVectorDescFunction(const GridArea* area, std::string_view field);

const char objEmptyString[] = "";

template<class X>
class ObjectGrabber: public GridGrabber {
  protected:
    X* mTypedObject = nullptr;  //!< a class specific object pointer
  public:
    ObjectGrabber(std::string_view fld = objEmptyString, X* newObj = nullptr)
    {
        if (newObj) {
            updateObject(newObj);
        }
        if (!fld.empty()) {
            ObjectGrabber<X>::updateField(fld);
        }
    }
    std::unique_ptr<GridGrabber> clone() const override
    {
        std::unique_ptr<GridGrabber> ggb = std::make_unique<ObjectGrabber>();
        cloneTo(ggb.get());
        return ggb;
    }

    void cloneTo(GridGrabber* ggb) const override
    {
        GridGrabber::cloneTo(ggb);
        auto ngb = dynamic_cast<ObjectGrabber*>(ggb);
        if (ngb == nullptr) {
            return;
        }

        ngb->mTypedObject = mTypedObject;
    }

    void updateField(std::string_view fld) override
    {
        field = fld;
        auto fret = getObjectFunction(mTypedObject, fld);
        if (fret.first) {
            mGrabberFunction = fret.first;
            inputUnits = fret.second;
            loaded = checkIfLoaded();
            return;
        }
        auto fvecret = getObjectVectorFunction(mTypedObject, fld);
        if (fvecret.first) {
            mVectorGrabberFunction = fvecret.first;
            inputUnits = fvecret.second;
            vectorGrab = true;
            mVectorDescriptionFunction = getObjectVectorDescFunction(mTypedObject, fld);
            loaded = checkIfLoaded();
            return;
        }
        GridGrabber::updateField(fld);
    }

    void updateObject(CoreObject* obj, ObjectUpdateMode mode = ObjectUpdateMode::DIRECT) override
    {
        CoreObject* newObject =
            (mode == ObjectUpdateMode::DIRECT) ? obj : findMatchingObject(mObject, obj);
        if (dynamic_cast<X*>(newObject)) {
            mTypedObject = static_cast<X*>(newObject);
            GridGrabber::updateObject(newObject);
        } else {
            throw(ObjectUpdateFailException());
        }
    }
};

template<class X>
class ObjectOffsetGrabber: public GridGrabber {
  protected:
    X* mTypedObject = nullptr;
    index_t mOffset = kInvalidLocation;

  public:
    ObjectOffsetGrabber(std::string_view fld = objEmptyString, X* newObj = nullptr)
    {
        if (newObj) {
            updateObject(newObj);
        }
        if (!fld.empty()) {
            ObjectOffsetGrabber<X>::updateField(fld);
        }
    }
    ObjectOffsetGrabber(index_t newOffset, X* newObj = nullptr)
    {
        if (newObj) {
            updateObject(newObj);
        }

        updateOffset(newOffset);
    }

    std::unique_ptr<GridGrabber> clone() const override
    {
        std::unique_ptr<GridGrabber> ggb = std::make_unique<ObjectOffsetGrabber>();
        ObjectOffsetGrabber::cloneTo(ggb.get());
        return ggb;
    }

    void cloneTo(GridGrabber* ggb) const override
    {
        GridGrabber::cloneTo(ggb);
        auto ngb = dynamic_cast<ObjectOffsetGrabber*>(ggb);
        if (ngb == nullptr) {
            return;
        }
        ngb->mOffset = mOffset;
        ngb->mTypedObject = mTypedObject;
    }

    void updateField(std::string_view fld) override
    {
        field = fld;
        const std::string fldString{fld};
        auto fret = getObjectFunction(mTypedObject, fldString);
        if (fret.first) {
            mGrabberFunction = fret.first;
            inputUnits = fret.second;
            loaded = GridGrabber::checkIfLoaded();
            return;
        }
        auto fvecret = getObjectVectorFunction(mTypedObject, fldString);
        if (fvecret.first) {
            mVectorGrabberFunction = fvecret.first;
            inputUnits = fvecret.second;
            vectorGrab = true;
            mVectorDescriptionFunction = getObjectVectorDescFunction(mTypedObject, fldString);
            loaded = GridGrabber::checkIfLoaded();
            return;
        }
        mOffset = mTypedObject->findIndex(fldString, cLocalSolverMode);

        if (mOffset == kInvalidLocation) {
            GridGrabber::updateField(fld);
        } else {
            loaded = true;
            makeDescription();
            inputUnits = units::defunit;
        }
    }

    void updateObject(CoreObject* obj, ObjectUpdateMode mode = ObjectUpdateMode::DIRECT) override
    {
        CoreObject* newObject =
            (mode == ObjectUpdateMode::DIRECT) ? obj : findMatchingObject(mObject, obj);
        if (dynamic_cast<X*>(newObject)) {
            mTypedObject = static_cast<X*>(newObject);
            if (mOffset == kInvalidLocation) {
                GridGrabber::updateObject(newObject);
            } else {
                mOffset = mTypedObject->findIndex(field, cLocalSolverMode);

                if (mOffset == kInvalidLocation) {
                    GridGrabber::updateField(field);
                } else {
                    loaded = true;
                    makeDescription();
                    inputUnits = units::defunit;
                }
            }
        } else {
            throw(ObjectUpdateFailException());
        }
    }

    void updateOffset(index_t nOffset)
    {
        mOffset = nOffset;
        if (mTypedObject) {
            if (mOffset < mTypedObject->stateSize(cLocalSolverMode)) {
                loaded = true;
                if (!customDesc) {
                    mDescription = mTypedObject->getName() + ':' + std::to_string(nOffset);
                }

                return;
            }
        }
        loaded = false;
    }

    double grabData() override
    {
        double val = kNullVal;
        if (loaded) {
            if (mOffset != kInvalidLocation) {
                if (mOffset == kNullLocation) {
                    mOffset = mTypedObject->findIndex(field, cLocalSolverMode);
                }
                if (mOffset != kNullLocation) {
                    val = mTypedObject->getState(mOffset);
                } else {
                    val = kNullVal;
                }
                val = val * gain + bias;
            } else {
                val = GridGrabber::grabData();
            }
        }
        return val;
    }

    void makeDescription() const override
    {
        if (!customDesc) {
            if ((loaded) && (field.empty())) {
                mDescription = mTypedObject->getName() + ':' + std::to_string(mOffset);
            } else {
                GridGrabber::makeDescription();
            }
        }
    }

    bool checkIfLoaded() override
    {
        // check for the offset, otherwise just use the regular check
        if (mOffset != kInvalidLocation) {
            return (mObject != nullptr);
        }
        return GridGrabber::checkIfLoaded();
    }
};

}  // namespace griddyn
