/*
 * Copyright (c) 2014-2026, Lawrence Livermore National Security
 * See the top-level NOTICE for additional details. All rights reserved.
 * SPDX-License-Identifier: BSD-3-Clause
 */

#pragma once

#include "griddyn/griddyn-config.h"
#include "optHelperClasses.h"
#include "utilities/vectData.hpp"
#include <memory>
#include <string>
#include <string_view>
#include <vector>

namespace griddyn {
class gridDynOptimization;

class optimizerInterface {
  public:
    vectData<double> v1;
    // solver outputs
    std::vector<double> values;  //!< mask vector for which roots were found

    OptimizationMode mode;
    double rtol = 1e-6;  //!< solver relative solution tolerance
    bool sparse = false;
    bool constantJacobian = false;

  protected:
    std::string mName;
    void* mSolverMem = nullptr;
    bool mAllocated = false;
    bool mInitialized = false;  //!< flag indicating if these vectors have been initialized
    gridDynOptimization* mGridDynOptimization = nullptr;
    count_t mStateVectorSize = 0;

  public:
    optimizerInterface(std::string_view optName = "optim");

    optimizerInterface(gridDynOptimization* gdo, const OptimizationMode& oMode);
    virtual ~optimizerInterface() {}
    virtual double* val_data() { return values.data(); }

    virtual int allocate(count_t size)
    {
        values.resize(size);
        return 0;
    }
    virtual void dynObjectInitializeA(double /*t0*/) {}

    virtual double get(std::string_view param) const;
    virtual int solve(double /*tStop*/, double& /*tReturn*/) { return -101; }
    void initializeJacArray(count_t size);
    virtual void logSolverStats(int /*logLevel*/, bool /*iconly*/ = false) {}
    virtual void logErrorWeights(int /*logLevel*/) {}

    count_t getSize() const { return mStateVectorSize; }
    virtual void setOptimizationData(gridDynOptimization* gdo, const OptimizationMode& oMode);
    virtual int
        check_flag(void* flagvalue, std::string_view funcname, int opt, bool printError = true);
    bool isInitialized() const { return mInitialized; }
    void setName(std::string_view newName) { mName = std::string{newName}; }
    const std::string& getName() const { return mName; }
};

class basicOptimizer: public optimizerInterface {
  private:
  public:
    explicit basicOptimizer(std::string_view optName = "basic");

    basicOptimizer(gridDynOptimization* gdo, const OptimizationMode& oMode);

    int allocate(count_t size) override;
    void dynObjectInitializeA(double t0) override;
};

std::shared_ptr<optimizerInterface>
    makeOptimizer(gridDynOptimization* gdo, const OptimizationMode& oMode);

std::shared_ptr<optimizerInterface> makeOptimizer(std::string_view type);

}  // namespace griddyn
