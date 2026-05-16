/*
 * Copyright (c) 2014-2026, Lawrence Livermore National Security
 * See the top-level NOTICE for additional details. All rights reserved.
 * SPDX-License-Identifier: BSD-3-Clause
 */

#pragma once

// headers
#include "../gridOptObjects.h"
#include <string>
// forward classes

namespace griddyn {
class GridLoadOpt;
class GridGenOpt;
class GridBusOpt;

class Link;

class GridLinkOpt: public GridOptObject {
  public:
    enum BusFlags {

    };

  protected:
    GridBusOpt* B1;
    GridBusOpt* B2;

    Link* link = nullptr;
    double rampUpLimit;
    double rampDownLimit;

  public:
    GridLinkOpt(const std::string& objName = "");
    GridLinkOpt(coreObject* obj, const std::string& objName = "");
    ~GridLinkOpt();

    virtual coreObject* clone(coreObject* obj = nullptr) const override;
    // add components
    virtual void add(coreObject* obj) override;

    // remove components
    virtual void remove(coreObject* obj) override;

    virtual void dynObjectInitializeA(std::uint32_t flags) override;
    virtual void loadSizes(const OptimizationMode& oMode) override;

    virtual void setValues(const OptimizationData& of, const OptimizationMode& oMode) override;
    // for saving the state
    virtual void guessState(double time, double val[], const OptimizationMode& oMode) override;
    virtual void getTols(double tols[], const OptimizationMode& oMode) override;
    virtual void getVariableType(double sdata[], const OptimizationMode& oMode) override;

    virtual void valueBounds(double time,
                             double upLimit[],
                             double lowerLimit[],
                             const OptimizationMode& oMode) override;

    virtual void linearObj(const OptimizationData& of,
                           vectData<double>& linObj,
                           const OptimizationMode& oMode) override;
    virtual void quadraticObj(const OptimizationData& of,
                              vectData<double>& linObj,
                              vectData<double>& quadObj,
                              const OptimizationMode& oMode) override;

    virtual double objValue(const OptimizationData& of, const OptimizationMode& oMode) override;
    virtual void gradient(const OptimizationData& of,
                          double deriv[],
                          const OptimizationMode& oMode) override;
    virtual void jacobianElements(const OptimizationData& of,
                                  matrixData<double>& md,
                                  const OptimizationMode& oMode) override;
    virtual void getConstraints(const OptimizationData& of,
                                matrixData<double>& cons,
                                double upperLimit[],
                                double lowerLimit[],
                                const OptimizationMode& oMode) override;
    virtual void constraintValue(const OptimizationData& of,
                                 double cVals[],
                                 const OptimizationMode& oMode) override;
    virtual void constraintJacobianElements(const OptimizationData& of,
                                            matrixData<double>& md,
                                            const OptimizationMode& oMode) override;
    virtual void getObjName(stringVec& objNames,
                            const OptimizationMode& oMode,
                            const std::string& prefix = "") override;

    virtual void disable() override;
    // parameter set functions

    virtual void setOffsets(const OptimizationOffsets& newOffsets,
                            const OptimizationMode& oMode) override;

    virtual void set(std::string_view param, std::string_view val) override;
    virtual void
        set(std::string_view param, double val, units::unit unitType = units::defunit) override;
    // parameter get functions
    virtual double get(std::string_view param,
                       units::unit unitType = units::defunit) const override;

    // find components
    virtual coreObject* find(std::string_view objName) const override;
    virtual coreObject* getSubObject(std::string_view typeName, index_t num) const override;
    virtual coreObject* findByUserID(std::string_view typeName, index_t searchID) const override;

    virtual GridOptObject* getBus(index_t index) const override;
    virtual GridOptObject* getArea(index_t index) const override;

  protected:
};

}  // namespace griddyn
