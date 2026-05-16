/*
 * Copyright (c) 2014-2026, Lawrence Livermore National Security
 * See the top-level NOTICE for additional details. All rights reserved.
 * SPDX-License-Identifier: BSD-3-Clause
 */

#include "optimizerInterface.h"

#include "core/factoryTemplates.hpp"
#include "gridDynOpt.h"
#include <memory>
#include <string>
#include <string_view>

namespace griddyn {
static childClassFactory<BasicOptimizer, OptimizerInterface>
    basicFac(stringVec{"basic", "pricestack"});

OptimizerInterface::OptimizerInterface(std::string_view optName): mName(optName) {}

OptimizerInterface::OptimizerInterface(GridDynOptimization* gdo, const OptimizationMode& oMode):
    mode(oMode), mGridDynOptimization(gdo)
{
}

void OptimizerInterface::setOptimizationData(GridDynOptimization* gdo,
                                             const OptimizationMode& oMode)
{
    mode = oMode;
    if (gdo != nullptr) {
        mGridDynOptimization = gdo;
    }
}

void OptimizerInterface::initializeJacArray(count_t /*size*/) {}

double OptimizerInterface::get(std::string_view /*param*/) const
{
    return kNullVal;
}

int OptimizerInterface::check_flag(void* flagvalue,
                                   std::string_view funcname,
                                   int opt,
                                   bool printError)
{
    // Check if SUNDIALS function returned nullptr pointer - no memory allocated
    if (opt == 0 && flagvalue == nullptr) {
        if (printError) {
            mGridDynOptimization->log(mGridDynOptimization,
                                      print_level::error,
                                      std::string{funcname} + " failed - returned nullptr pointer");
        }
        return (1);
    }
    if (opt == 1) {
        // Check if flag < 0
        auto* errflag = reinterpret_cast<int*>(flagvalue);
        if (*errflag < 0) {
            if (printError) {
                mGridDynOptimization->log(mGridDynOptimization,
                                          print_level::error,
                                          std::string{funcname} +
                                              " failed with flag = " + std::to_string(*errflag));
            }
            return (1);
        }
    } else if (opt == 2 && flagvalue == nullptr) {
        // Check if function returned nullptr pointer - no memory allocated
        if (printError) {
            mGridDynOptimization->log(mGridDynOptimization,
                                      print_level::error,
                                      std::string{funcname} +
                                          " failed MEMORY_ERROR- returned nullptr pointer");
        }
        return (1);
    }
    return 0;
}

BasicOptimizer::BasicOptimizer(std::string_view optName): OptimizerInterface(optName) {}

BasicOptimizer::BasicOptimizer(GridDynOptimization* gdo, const OptimizationMode& oMode):
    OptimizerInterface(gdo, oMode)
{
}

int BasicOptimizer::allocate(count_t size)
{
    // load the vectors
    if (size == mStateVectorSize) {
        return FUNCTION_EXECUTION_SUCCESS;
    }

    mStateVectorSize = size;
    mInitialized = false;
    mAllocated = true;
    return FUNCTION_EXECUTION_SUCCESS;
}

void BasicOptimizer::dynObjectInitializeA(double /*t0*/)
{
    if (!mAllocated) {
        //  return (-2);
    }
    mInitialized = true;
    // return FUNCTION_EXECUTION_SUCCESS;
}

std::shared_ptr<OptimizerInterface> makeOptimizer(GridDynOptimization* gdo,
                                                  const OptimizationMode& oMode)
{
    std::shared_ptr<OptimizerInterface> of;
    switch (oMode.flowMode) {
        case FlowModel::NONE:
        default:
            of = std::make_shared<BasicOptimizer>(gdo, oMode);
            break;
        case FlowModel::TRANSPORT:
        case FlowModel::DC:
        case FlowModel::AC:
            break;
    }
    return of;
}

std::shared_ptr<OptimizerInterface> makeOptimizer(std::string_view type)
{
    return coreClassFactory<OptimizerInterface>::instance()->createObject(type);
}

}  // namespace griddyn
