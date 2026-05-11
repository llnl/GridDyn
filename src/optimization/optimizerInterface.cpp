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
static childClassFactory<basicOptimizer, optimizerInterface>
    basicFac(stringVec{"basic", "pricestack"});

optimizerInterface::optimizerInterface(std::string_view optName): mName(optName) {}

optimizerInterface::optimizerInterface(gridDynOptimization* gdo, const optimMode& oMode):
    mode(oMode), mGridDynOptimization(gdo)
{
}

void optimizerInterface::setOptimizationData(gridDynOptimization* gdo, const optimMode& oMode)
{
    mode = oMode;
    if (gdo != nullptr) {
        mGridDynOptimization = gdo;
    }
}

void optimizerInterface::initializeJacArray(count_t /*size*/) {}

double optimizerInterface::get(std::string_view /*param*/) const
{
    return kNullVal;
}

int optimizerInterface::check_flag(void* flagvalue,
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

basicOptimizer::basicOptimizer(std::string_view optName): optimizerInterface(optName) {}

basicOptimizer::basicOptimizer(gridDynOptimization* gdo, const optimMode& oMode):
    optimizerInterface(gdo, oMode)
{
}

int basicOptimizer::allocate(count_t size)
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

void basicOptimizer::dynObjectInitializeA(double /*t0*/)
{
    if (!mAllocated) {
        //  return (-2);
    }
    mInitialized = true;
    // return FUNCTION_EXECUTION_SUCCESS;
}

std::shared_ptr<optimizerInterface> makeOptimizer(gridDynOptimization* gdo, const optimMode& oMode)
{
    std::shared_ptr<optimizerInterface> of;
    switch (oMode.flowMode) {
        case flowModel_t::none:
        default:
            of = std::make_shared<basicOptimizer>(gdo, oMode);
            break;
        case flowModel_t::transport:
        case flowModel_t::dc:
        case flowModel_t::ac:
            break;
    }
    return of;
}

std::shared_ptr<optimizerInterface> makeOptimizer(std::string_view type)
{
    return coreClassFactory<optimizerInterface>::instance()->createObject(type);
}

}  // namespace griddyn
