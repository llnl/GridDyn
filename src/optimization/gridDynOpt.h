/*
 * Copyright (c) 2014-2020, Lawrence Livermore National Security
 * See the top-level NOTICE for additional details. All rights reserved.
 * SPDX-License-Identifier: BSD-3-Clause
 */

#ifndef GRIDDYNOPT_H_
#define GRIDDYNOPT_H_
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
enum optimization_type_t { default_optimization, bidstack, water, DCOPF, ACOPF };

// additional flags the controlFlags bitset
enum gdopt_flags {

};

// for the status flags bitset

// extra local flags
enum gdopt_extra_flags {

};

class optimData;
class gridAreaOpt;
class gridOptObject;

class gridDynOptimization: public gridDynSimulation {
  public:
  protected:
    // storageSpace for SUNDIALS solverInterface
    std::vector<std::shared_ptr<optimizerInterface>> oData;
    gridAreaOpt* areaOpt = nullptr;
    std::string defaultOptMode;
    // ---------------solution mode-------------
    // total thread count

    // list of object to PreExecute
    int constraintCount = 0;
    optimization_type_t optimization_mode;

  public:
    gridDynOptimization(const std::string& simName = "gridDynOptSim_#");
    ~gridDynOptimization();
    coreObject* clone(coreObject* obj) const override;

    void setOptimizationMode(optimization_type_t omode)
    {
        if (omode != default_optimization) {
            optimization_mode = omode;
        }
    }
    virtual void set(const std::string& param, const std::string& val) override;
    virtual void
        set(const std::string& param, double val, units::unit unitType = units::defunit) override;

    virtual double get(const std::string& param,
                       units::unit unitType = units::defunit) const override;

    void setFlag(const std::string& flag, bool val = true) override;
    void setFlags(size_t param, int val);
    // void get(std::string param,int &val);
    // void get(std::string param, double &val);

    virtual coreObject* find(const std::string& objName) const override;
    virtual coreObject* getSubObject(const std::string& typeName, index_t num) const override;
    virtual coreObject* findByUserID(const std::string& typeName, index_t searchID) const override;
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
        return (oData[oMode.offsetIndex].get());
    }

    const optimizerInterface* getOptimizerData(const optimMode& oMode) const
    {
        return (oData[oMode.offsetIndex].get());
    }

    void setMaxJacSize(const optimMode& oMode, count_t ssize)
    {
        oData[oMode.offsetIndex]->initializeJacArray(ssize);
    }
    // dynamics protected
    // void dynInitializeObjects(double initTime, double absInitTime);

    void setupOptOffsets(const optimMode& oMode, int setupMode);
};

}  // namespace griddyn
#endif
