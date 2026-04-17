/*
 * Copyright (c) 2014-2026, Lawrence Livermore National Security
 * See the top-level NOTICE for additional details. All rights reserved.
 * SPDX-License-Identifier: BSD-3-Clause
 */

#pragma once

#include "../solvers/solverMode.hpp"
#include "core/coreExceptions.h"
#include "core/objectInterpreter.h"
#include "gridGrabbers.h"
#include <memory>
#include <string>
#include <utility>
#include <vector>

namespace griddyn {
class gridSubModel;
class gridComponent;
class gridBus;
class Load;
class Link;
class Generator;
class Area;
class Relay;
class gridSubModel;

using fobjectPair = std::pair<std::function<double(coreObject*)>, units::unit>;

fobjectPair getObjectFunction(const gridComponent* comp, const std::string& field);
fobjectPair getObjectFunction(const gridBus* bus, const std::string& field);
fobjectPair getObjectFunction(const Load* ld, const std::string& field);
fobjectPair getObjectFunction(const Link* lnk, const std::string& field);
fobjectPair getObjectFunction(const Generator* gen, const std::string& field);
fobjectPair getObjectFunction(const Area* area, const std::string& field);
fobjectPair getObjectFunction(const Relay* rel, const std::string& field);
fobjectPair getObjectFunction(const gridSubModel* sub, const std::string& field);

using fvecPair = std::pair<std::function<void(coreObject*, std::vector<double>&)>, units::unit>;

fvecPair getObjectVectorFunction(const gridComponent* comp, const std::string& field);

fvecPair getObjectVectorFunction(const Area* area, const std::string& field);

using descVecFunc = std::function<void(coreObject*, stringVec&)>;

descVecFunc getObjectVectorDescFunction(const gridComponent* comp, const std::string& field);
descVecFunc getObjectVectorDescFunction(const Area* area, const std::string& field);

const char objEmptyString[] = "";

template<class X>
class objectGrabber: public gridGrabber {
  protected:
    X* tobject;  //!< a class specific object pointer
  public:
    objectGrabber(const std::string& fld = objEmptyString, X* newObj = nullptr)
    {
        if (newObj) {
            updateObject(newObj);
        }
        if (!fld.empty()) {
            objectGrabber<X>::updateField(fld);
        }
    }
    std::unique_ptr<gridGrabber> clone() const override
    {
        std::unique_ptr<gridGrabber> ggb = std::make_unique<objectGrabber>();
        cloneTo(ggb.get());
        return ggb;
    }

    void cloneTo(gridGrabber* ggb) const override
    {
        gridGrabber::cloneTo(ggb);
        auto ngb = dynamic_cast<objectGrabber*>(ggb);
        if (ngb == nullptr) {
            return;
        }

        ngb->tobject = tobject;
    }

    void updateField(const std::string& fld) override
    {
        field = fld;
        auto fret = getObjectFunction(tobject, fld);
        if (fret.first) {
            fptr = fret.first;
            inputUnits = fret.second;
            loaded = checkIfLoaded();
            return;
        }
        auto fvecret = getObjectVectorFunction(tobject, fld);
        if (fvecret.first) {
            fptrV = fvecret.first;
            inputUnits = fvecret.second;
            vectorGrab = true;
            fptrN = getObjectVectorDescFunction(tobject, fld);
            loaded = checkIfLoaded();
            return;
        }
        gridGrabber::updateField(fld);
    }

    void updateObject(coreObject* obj,
                      object_update_mode mode = object_update_mode::direct) override
    {
        coreObject* newObject =
            (mode == object_update_mode::direct) ? obj : findMatchingObject(cobj, obj);
        if (dynamic_cast<X*>(newObject)) {
            tobject = static_cast<X*>(newObject);
            gridGrabber::updateObject(newObject);
        } else {
            throw(objectUpdateFailException());
        }
    }
};

template<class X>
class objectOffsetGrabber: public gridGrabber {
  protected:
    X* tobject;
    index_t offset = kInvalidLocation;

  public:
    objectOffsetGrabber(const std::string& fld = objEmptyString, X* newObj = nullptr)
    {
        if (newObj) {
            updateObject(newObj);
        }
        if (!fld.empty()) {
            objectOffsetGrabber<X>::updateField(fld);
        }
    }
    objectOffsetGrabber(index_t newOffset, X* newObj = nullptr)
    {
        if (newObj) {
            updateObject(newObj);
        }

        updateOffset(newOffset);
    }

    std::unique_ptr<gridGrabber> clone() const override
    {
        std::unique_ptr<gridGrabber> ggb = std::make_unique<objectOffsetGrabber>();
        objectOffsetGrabber::cloneTo(ggb.get());
        return ggb;
    }

    void cloneTo(gridGrabber* ggb) const override
    {
        gridGrabber::cloneTo(ggb);
        auto ngb = dynamic_cast<objectOffsetGrabber*>(ggb);
        if (ngb == nullptr) {
            return;
        }
        ngb->offset = offset;
        ngb->tobject = tobject;
    }

    void updateField(const std::string& fld) override
    {
        field = fld;
        auto fret = getObjectFunction(tobject, fld);
        if (fret.first) {
            fptr = fret.first;
            inputUnits = fret.second;
            loaded = gridGrabber::checkIfLoaded();
            return;
        }
        auto fvecret = getObjectVectorFunction(tobject, fld);
        if (fvecret.first) {
            fptrV = fvecret.first;
            inputUnits = fvecret.second;
            vectorGrab = true;
            fptrN = getObjectVectorDescFunction(tobject, fld);
            loaded = gridGrabber::checkIfLoaded();
            return;
        }
        offset = tobject->findIndex(fld, cLocalSolverMode);

        if (offset == kInvalidLocation) {
            gridGrabber::updateField(fld);
        } else {
            loaded = true;
            makeDescription();
            inputUnits = units::defunit;
        }
    }

    void updateObject(coreObject* obj,
                      object_update_mode mode = object_update_mode::direct) override
    {
        coreObject* newObject =
            (mode == object_update_mode::direct) ? obj : findMatchingObject(cobj, obj);
        if (dynamic_cast<X*>(newObject)) {
            tobject = static_cast<X*>(newObject);
            if (offset == kInvalidLocation) {
                gridGrabber::updateObject(newObject);
            } else {
                offset = tobject->findIndex(field, cLocalSolverMode);

                if (offset == kInvalidLocation) {
                    gridGrabber::updateField(field);
                } else {
                    loaded = true;
                    makeDescription();
                    inputUnits = units::defunit;
                }
            }
        } else {
            throw(objectUpdateFailException());
        }
    }

    void updateOffset(index_t nOffset)
    {
        offset = nOffset;
        if (tobject) {
            if (offset < tobject->stateSize(cLocalSolverMode)) {
                loaded = true;
                if (!customDesc) {
                    desc = tobject->getName() + ':' + std::to_string(nOffset);
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
            if (offset != kInvalidLocation) {
                if (offset == kNullLocation) {
                    offset = tobject->findIndex(field, cLocalSolverMode);
                }
                if (offset != kNullLocation) {
                    val = tobject->getState(offset);
                } else {
                    val = kNullVal;
                }
                val = val * gain + bias;
            } else {
                val = gridGrabber::grabData();
            }
        }
        return val;
    }

    void makeDescription() const override
    {
        if (!customDesc) {
            if ((loaded) && (field.empty())) {
                desc = tobject->getName() + ':' + std::to_string(offset);
            } else {
                gridGrabber::makeDescription();
            }
        }
    }

    bool checkIfLoaded() override
    {
        // check for the offset, otherwise just use the regular check
        if (offset != kInvalidLocation) {
            return (cobj != nullptr);
        }
        return gridGrabber::checkIfLoaded();
    }
};

}  // namespace griddyn
