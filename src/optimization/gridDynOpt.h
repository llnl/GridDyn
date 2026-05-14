/*
 * Copyright (c) 2014-2026, Lawrence Livermore National Security
 * See the top-level NOTICE for additional details. All rights reserved.
 * SPDX-License-Identifier: BSD-3-Clause
 */

#pragma once

// debug
// #define DEBUG_IDA

// header files
#include "griddyn/gridDynSimulation.h"
#include "optimizerInterface.h"
// libraries
#include <array>
#include <bitset>
#include <cstdio>
#include <fstream>
#include <list>
#include <string>
#include <vector>

namespace griddyn {
// definitions for defining solution mode
enum OptimizationType { DEFAULT_OPTIMIZATION, BIDSTACK, WATER, DCOPF, ACOPF };

// additional flags the controlFlags bitset
enum GridDynOptFlags {

};

// for the status flags bitset

// extra local flags
enum GridDynOptExtraFlags {

};

class optimData;
class gridAreaOpt;
class gridOptObject;

class gridDynOptimization: public gridDynSimulation {
  public:
  protected:
    // storageSpace for SUNDIALS solverInterface
    std::vector<std::shared_ptr<optimizerInterface>> mOptimizerData;
    gridAreaOpt* mAreaOpt = nullptr;
    std::string mDefaultOptMode;
    // ---------------solution mode-------------
    // total thread count

    // list of object to PreExecute
    int mConstraintCount = 0;
    OptimizationType mOptimizationMode;

  public:
    gridDynOptimization(const std::string& simName = "gridDynOptSim_#");
    ~gridDynOptimization();
    coreObject* clone(coreObject* obj) const override;

    void setOptimizationMode(OptimizationType omode)
    {
        if (omode != DEFAULT_OPTIMIZATION) {
            mOptimizationMode = omode;
        }
    }
    virtual void set(std::string_view param, std::string_view val) override;
    virtual void
        set(std::string_view param, double val, units::unit unitType = units::defunit) override;

    virtual double get(std::string_view param,
                       units::unit unitType = units::defunit) const override;

    void setFlag(std::string_view flag, bool val = true) override;
    void setFlags(size_t param, int val);
    // void get(std::string param,int &val);
    // void get(std::string param, double &val);

    virtual coreObject* find(std::string_view objName) const override;
    virtual coreObject* getSubObject(std::string_view typeName, index_t num) const override;
    virtual coreObject* findByUserID(std::string_view typeName, index_t searchID) const override;
    /** find the economic data for a corresponding grid core object
    @input coreObject the object for which to find the corresponding econ Data
    */
    virtual gridOptObject* getOptData(coreObject* obj = nullptr);
    virtual gridOptObject* makeOptObjectPath(coreObject* obj);

  protected:
    optimizerInterface* updateOptimizer(const optimMode& oMode);

    // SGS this was unused?
    // void updateOffsets (const optimMode &oMode);

    // void pFlowJacobian(const double state[]);
    optimizerInterface* getOptimizerData(const optimMode& oMode)
    {
        return (mOptimizerData[oMode.offsetIndex].get());
    }

    const optimizerInterface* getOptimizerData(const optimMode& oMode) const
    {
        return (mOptimizerData[oMode.offsetIndex].get());
    }

    void setMaxJacSize(const optimMode& oMode, count_t ssize)
    {
        mOptimizerData[oMode.offsetIndex]->initializeJacArray(ssize);
    }
    // dynamics protected
    // void dynInitializeObjects(double initTime, double absInitTime);

    void setupOptOffsets(const optimMode& oMode, int setupMode);
};

}  // namespace griddyn
