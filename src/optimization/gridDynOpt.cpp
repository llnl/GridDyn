/*
 * Copyright (c) 2014-2026, Lawrence Livermore National Security
 * See the top-level NOTICE for additional details. All rights reserved.
 * SPDX-License-Identifier: BSD-3-Clause
 */

#include "gridDynOpt.h"

#include "core/CoreExceptions.h"
#include "core/CoreObjectTemplates.hpp"
#include "core/ObjectFactoryTemplates.hpp"
#include "gmlc/utilities/stringOps.h"
#include "gridOptObjects.h"
#include "models/gridAreaOpt.h"
#include "models/gridBusOpt.h"
#include "optObjectFactory.h"
// system headers

#include <cmath>
#include <cstdio>
#include <fstream>
#include <iostream>
#include <map>
#include <memory>
#include <string>

namespace griddyn {
static TypeFactory<GridDynOptimization>
    gFo("simulation", std::to_array<std::string_view>({"optimization", "optim"}));

namespace {
    // The optimizer hierarchy mirrors the recursive GridDyn object hierarchy.
    // NOLINTNEXTLINE(misc-no-recursion)
    GridOptObject* findOptimizationObjectBySource(GridOptObject* root,
                                                  const CoreObject* sourceObject)
    {
        if ((root == nullptr) || (sourceObject == nullptr)) {
            return nullptr;
        }
        if (root->sourceObject() == sourceObject) {
            return root;
        }
        if (auto* area = dynamic_cast<GridAreaOpt*>(root); area != nullptr) {
            for (index_t index = 0;; ++index) {
                auto* found = findOptimizationObjectBySource(area->getArea(index), sourceObject);
                if (found != nullptr) {
                    return found;
                }
                if (area->getArea(index) == nullptr) {
                    break;
                }
            }
            for (index_t index = 0;; ++index) {
                auto* found = findOptimizationObjectBySource(area->getBus(index), sourceObject);
                if (found != nullptr) {
                    return found;
                }
                if (area->getBus(index) == nullptr) {
                    break;
                }
            }
            for (index_t index = 0;; ++index) {
                auto* found = findOptimizationObjectBySource(area->getLink(index), sourceObject);
                if (found != nullptr) {
                    return found;
                }
                if (area->getLink(index) == nullptr) {
                    break;
                }
            }
            for (index_t index = 0;; ++index) {
                auto* found = findOptimizationObjectBySource(area->getRelay(index), sourceObject);
                if (found != nullptr) {
                    return found;
                }
                if (area->getRelay(index) == nullptr) {
                    break;
                }
            }
        } else if (auto* bus = dynamic_cast<GridBusOpt*>(root); bus != nullptr) {
            for (index_t index = 0;; ++index) {
                auto* found = findOptimizationObjectBySource(bus->getGen(index), sourceObject);
                if (found != nullptr) {
                    return found;
                }
                if (bus->getGen(index) == nullptr) {
                    break;
                }
            }
            for (index_t index = 0;; ++index) {
                auto* found = findOptimizationObjectBySource(bus->getLoad(index), sourceObject);
                if (found != nullptr) {
                    return found;
                }
                if (bus->getLoad(index) == nullptr) {
                    break;
                }
            }
        }
        return nullptr;
    }
}  // namespace

GridDynOptimization::GridDynOptimization(const std::string& simName):
    GridDynSimulation(simName), mOptimizationMode(DEFAULT_OPTIMIZATION)
{
    // defaults
    mGridAreaOpt = new GridAreaOpt(this);
}

GridDynOptimization::~GridDynOptimization()
{
    delete mGridAreaOpt;
}

CoreObject* GridDynOptimization::clone(CoreObject* obj) const
{
    auto* sim = cloneBase<GridDynOptimization, GridDynSimulation>(this, obj);
    if (sim == nullptr) {
        return obj;
    }

    return sim;
}

void GridDynOptimization::initializeOptimizationModel(const OptimizationMode& oMode,
                                                      int setupMode,
                                                      std::uint32_t flags)
{
    // Keep the optimizer lifecycle parallel to GridDyn power-flow setup: first
    // traverse the physical hierarchy to construct and bind component models,
    // then perform a second pass to assign the assembled numerical layout.
    mGridAreaOpt->dynInitializeA(flags);
    mGridAreaOpt->loadSizes(oMode);
    setupOptOffsets(oMode, setupMode);
}

void GridDynOptimization::setupOptOffsets(const OptimizationMode& oMode, int setupMode)
{
    if (setupMode == 0) {
        // Flat mixed layout.  This mirrors the simulation setOffset() pass:
        // children allocate first, each object appends its local entries, and
        // all objective variables live in one zero-based block.
        mGridAreaOpt->setOffset(0, 0, oMode);
        return;
    }

    const auto& rootOffsets = mGridAreaOpt->offsets.getOffsets(oMode);
    const auto& rootSizes = rootOffsets.total;
    OptimizationOffsets baseOffset;
    if (setupMode == 1) {
        // Grouped physical/economic layout.  Angle, voltage, real-generation,
        // reactive-generation, generic-continuous, and integer categories each
        // receive a contiguous block when present.  This is the default OPF
        // layout because it keeps variable classes easy to inspect without
        // changing the single source of truth: sizes still come from the bound
        // optimization objects.
        baseOffset.constraintOffset = 0;
        index_t nextOffset = 0;
        if (rootSizes.aSize > 0) {
            baseOffset.aOffset = nextOffset;
            nextOffset += rootSizes.aSize;
        }
        if (rootSizes.vSize > 0) {
            baseOffset.vOffset = nextOffset;
            nextOffset += rootSizes.vSize;
        }
        if (rootSizes.genSize > 0) {
            baseOffset.gOffset = nextOffset;
            nextOffset += rootSizes.genSize;
        }
        if (rootSizes.qSize > 0) {
            baseOffset.qOffset = nextOffset;
            nextOffset += rootSizes.qSize;
        }
        baseOffset.contOffset = nextOffset;
        nextOffset += rootSizes.contSize;
        if (rootSizes.intSize > 0) {
            baseOffset.intOffset = nextOffset;
        }
    } else if (setupMode == 2) {
        // Solver-oriented layout: all continuous variables share one zero-based
        // block, followed by the integer block when one exists.
        baseOffset.constraintOffset = 0;
        baseOffset.contOffset = 0;
        if (rootSizes.intSize > 0) {
            baseOffset.intOffset = mGridAreaOpt->contObjSize(oMode);
        }
    }

    // call the area setOffset function to distribute the offsets
    mGridAreaOpt->setOffsets(baseOffset, oMode);
}

// --------------- set properties ---------------
void GridDynOptimization::set(std::string_view param, std::string_view val)
{
    if (param == "flags") {
        auto flagTokens = gmlc::utilities::stringOps::splitline(val);
        gmlc::utilities::stringOps::trim(flagTokens);
        for (auto& flagString : flagTokens) {
            setFlag(flagString, true);
        }
    } else if ((param == "defaultoptmode") || (param == "defaultopt")) {
        auto optFactory = CoreOptObjectFactory::instance();
        if (optFactory->isValidType(val)) {
            mDefaultOptMode = val;
            optFactory->setDefaultType(val);
        }
    } else if ((param == "optimizer") || (param == "optimizer_type") ||
               (param == "optimizersolver")) {
        if (makeOptimizer(val) != nullptr) {
            mDefaultOptimizerType = std::string{val};
        } else {
            logging::warning(this, "unknown optimizer type {}", val);
        }
    } else if (param == "optimization_mode") {
        /*default_solution,
    dcflow_only, powerflow_only, iterated_powerflow, contingency_powerflow,
    steppedP, steppedPQ, dynamic, dyanmic_contingency,*/
        auto temp = gmlc::utilities::convertToLowerCase(val);
        if ((temp == "dcopf") || (temp == "opf")) {
            mOptimizationMode = DCOPF;
        } else if ((temp == "acopf") || (temp == "ac")) {
            mOptimizationMode = ACOPF;
        } else if (temp == "bidstack") {
            mOptimizationMode = BIDSTACK;
        } else {
            logging::warning(this, "unknown optimization mode {}", temp);
        }
    } else {
        GridDynSimulation::set(param, val);
    }
}

void GridDynOptimization::setFlag(std::string_view flag, bool val)
{
    // int nval = static_cast<int> (val);
    /*
    constraints_disabled = 1,
    sparse_solver = 2,
    threads_enabled = 3,
    ignore_voltage_limits = 4,
    power_adjust_enabled = 5,
    dcFlow_initialization = 6,*/
    if (!flag.empty()) {
        GridDynSimulation::setFlag(flag, val);
    }
}

void GridDynOptimization::setFlags(size_t param, int val)
{
    if (param > 32) {
        throw(UnrecognizedParameter("flag" + std::to_string(param)));
    }

    controlFlags.set(param, (val > 0));
}

void GridDynOptimization::set(std::string_view param, double val, units::unit unitType)
{
    if (param == "optimtol") {
        tols.rtol = val;
    } else {
        // out = setFlags (param, val);
        try {
            GridDynSimulation::set(param, val, unitType);
        }
        catch (const UnrecognizedParameter&) {
            setFlag(param, (val > 0.1));
        }
    }
}

double GridDynOptimization::get(std::string_view param, units::unit unitType) const
{
    if (param == "voltagetolerance") {
        return tols.voltageTolerance;
    }
    if (param == "angletolerance") {
        return tols.angleTolerance;
    }
    return GridDynSimulation::get(param, unitType);
}

CoreObject* GridDynOptimization::find(std::string_view objName) const
{
    if (objName == "optroot") {
        return mGridAreaOpt;
    }
    if (objName.starts_with("opt")) {
        return mGridAreaOpt->find(objName.substr(3));
    }
    return GridDynSimulation::find(objName);
}

CoreObject* GridDynOptimization::getSubObject(std::string_view typeName, index_t num) const
{
    if (typeName.starts_with("opt")) {
        return mGridAreaOpt->getSubObject(typeName.substr(3), num);
    }
    return GridDynSimulation::getSubObject(typeName, num);
}
CoreObject* GridDynOptimization::findByUserID(std::string_view typeName, index_t searchID) const
{
    if (typeName.starts_with("opt")) {
        return mGridAreaOpt->findByUserID(typeName.substr(3), searchID);
    }
    return GridDynSimulation::findByUserID(typeName, searchID);
}

GridOptObject* GridDynOptimization::getOptimizationObject(CoreObject* obj)
{
    if (obj != nullptr) {
        return findOptimizationObjectBySource(mGridAreaOpt, obj);
    }
    return mGridAreaOpt;
}

// NOLINTNEXTLINE(misc-no-recursion)
GridOptObject* GridDynOptimization::makeOptimizationObjectPath(CoreObject* obj)
{
    GridOptObject* optObject = getOptimizationObject(obj);
    if (optObject != nullptr) {
        return optObject;
    }
    if (!(obj->isRoot())) {
        auto* parentOptObject = makeOptimizationObjectPath(obj->getParent());
        optObject = CoreOptObjectFactory::instance()->createObject(obj);
        parentOptObject->add(optObject);
        return optObject;
    }
    return nullptr;
}

std::shared_ptr<OptimizerInterface>
    GridDynOptimization::getOptimizerInterface(const OptimizationMode& oMode)
{
    if (!isValidIndex(oMode.offsetIndex, mOptimizerData) ||
        (mOptimizerData[oMode.offsetIndex] == nullptr)) {
        updateOptimizer(oMode);
    }
    return mOptimizerData[oMode.offsetIndex];
}

std::shared_ptr<const OptimizerInterface>
    GridDynOptimization::getOptimizerInterface(const OptimizationMode& oMode) const
{
    if (!isValidIndex(oMode.offsetIndex, mOptimizerData)) {
        return nullptr;
    }
    return mOptimizerData[oMode.offsetIndex];
}

OptimizerInterface* GridDynOptimization::updateOptimizer(const OptimizationMode& oMode)
{
    if (!isValidIndex(oMode.offsetIndex, mOptimizerData)) {
        mOptimizerData.resize(oMode.offsetIndex + 1);
    }
    mOptimizerData[oMode.offsetIndex] = makeOptimizer(this, oMode, mDefaultOptimizerType);
    OptimizerInterface* optimizer = mOptimizerData[oMode.offsetIndex].get();
    if (optimizer != nullptr) {
        optimizer->allocate(mGridAreaOpt->objSize(oMode), mGridAreaOpt->constraintSize(oMode));
    }

    return optimizer;
}

}  // namespace griddyn
