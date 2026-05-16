/*
 * Copyright (c) 2014-2026, Lawrence Livermore National Security
 * See the top-level NOTICE for additional details. All rights reserved.
 * SPDX-License-Identifier: BSD-3-Clause
 */

#pragma once

// headers
#include "../gridOptObjects.h"
#include <string>
#include <vector>
// forward classes
namespace griddyn {
class GridLinkOpt;
class GridLoadOpt;
class GridGenOpt;
class gridBus;

class GridBusOpt: public GridOptObject {
  public:
    enum BusFlags {

    };

  protected:
    std::vector<GridLoadOpt*> loadList;
    std::vector<GridLinkOpt*> linkList;
    std::vector<GridGenOpt*> genList;

    gridBus* bus = nullptr;

  public:
    GridBusOpt(const std::string& objName = "");
    GridBusOpt(coreObject* obj, const std::string& objName = "");
    ~GridBusOpt();

    coreObject* clone(coreObject* obj = nullptr) const override;
    // add components
    void add(coreObject* obj) override;
    void add(GridLoadOpt* loadObject);
    void add(GridGenOpt* gen);
    void add(GridLinkOpt* lnk);

    // remove components
    void remove(coreObject* obj) override;
    void remove(GridLoadOpt* loadObject);
    void remove(GridGenOpt* gen);
    void remove(GridLinkOpt* lnk);

    virtual void dynObjectInitializeA(std::uint32_t flags) override;
    virtual void loadSizes(const OptimizationMode& oMode) override;

    virtual void setValues(const OptimizationData& optimizationData,
                           const OptimizationMode& oMode) override;
    // for saving the state
    virtual void guessState(double time, double val[], const OptimizationMode& oMode) override;
    virtual void getTols(double tols[], const OptimizationMode& oMode) override;
    virtual void getVariableType(double sdata[], const OptimizationMode& oMode) override;

    virtual void valueBounds(double time,
                             double upperLimit[],
                             double lowerLimit[],
                             const OptimizationMode& oMode) override;

    virtual void linearObj(const OptimizationData& optimizationData,
                           vectData<double>& linObj,
                           const OptimizationMode& oMode) override;
    virtual void quadraticObj(const OptimizationData& optimizationData,
                              vectData<double>& linObj,
                              vectData<double>& quadObj,
                              const OptimizationMode& oMode) override;

    virtual double objValue(const OptimizationData& optimizationData,
                            const OptimizationMode& oMode) override;
    virtual void gradient(const OptimizationData& optimizationData,
                          double deriv[],
                          const OptimizationMode& oMode) override;
    virtual void jacobianElements(const OptimizationData& optimizationData,
                                  matrixData<double>& matrixDataRef,
                                  const OptimizationMode& oMode) override;
    virtual void getConstraints(const OptimizationData& optimizationData,
                                matrixData<double>& cons,
                                double upperLimit[],
                                double lowerLimit[],
                                const OptimizationMode& oMode) override;
    virtual void constraintValue(const OptimizationData& optimizationData,
                                 double cVals[],
                                 const OptimizationMode& oMode) override;
    virtual void constraintJacobianElements(const OptimizationData& optimizationData,
                                            matrixData<double>& matrixDataRef,
                                            const OptimizationMode& oMode) override;
    virtual void getObjName(stringVec& objNames,
                            const OptimizationMode& oMode,
                            const std::string& prefix = "") override;

    void disable() override;
    // parameter set functions
    virtual void setOffsets(const OptimizationOffsets& newOffsets,
                            const OptimizationMode& oMode) override;
    virtual void
        setOffset(index_t offset, index_t constraintOffset, const OptimizationMode& oMode) override;

    void setAll(const std::string& type,
                const std::string& param,
                double val,
                units::unit unitType = units::defunit);
    void set(std::string_view param, std::string_view val) override;
    void set(std::string_view param, double val, units::unit unitType = units::defunit) override;
    // parameter get functions
    virtual double get(std::string_view param,
                       units::unit unitType = units::defunit) const override;

    // void alert (coreObject *object, int code);

    // find components
    GridLinkOpt* findLink(gridBus* bs) const;
    coreObject* find(std::string_view objName) const override;
    coreObject* getSubObject(std::string_view typeName, index_t num) const override;
    coreObject* findByUserID(std::string_view typeName, index_t searchID) const override;

    GridOptObject* getLink(index_t index) const override;
    GridOptObject* getLoad(index_t index = 0) const;
    GridOptObject* getGen(index_t index = 0) const;

    GridOptObject* getBus(index_t /*index*/) const override
    {
        return const_cast<GridBusOpt*>(this);
    }
    GridOptObject* getArea(index_t /*index*/) const override
    {
        return static_cast<GridOptObject*>(getParent());
    }

  protected:
};

// bool compareBus (gridBus *bus1, gridBus *bus2, bool cmpLink = false,bool printDiff = false);

GridBusOpt* getMatchingBusOpt(GridBusOpt* bus, const GridOptObject* src, GridOptObject* sec);

}  // namespace griddyn
