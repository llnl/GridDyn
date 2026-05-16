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
class gridLinkOpt;
class gridLoadOpt;
class gridGenOpt;
class gridBus;

class gridBusOpt: public gridOptObject {
  public:
    enum BusFlags {

    };

  protected:
    std::vector<gridLoadOpt*> loadList;
    std::vector<gridLinkOpt*> linkList;
    std::vector<gridGenOpt*> genList;

    gridBus* bus = nullptr;

  public:
    gridBusOpt(const std::string& objName = "");
    gridBusOpt(coreObject* obj, const std::string& objName = "");
    ~gridBusOpt();

    coreObject* clone(coreObject* obj = nullptr) const override;
    // add components
    void add(coreObject* obj) override;
    void add(gridLoadOpt* ld);
    void add(gridGenOpt* gen);
    void add(gridLinkOpt* lnk);

    // remove components
    void remove(coreObject* obj) override;
    void remove(gridLoadOpt* ld);
    void remove(gridGenOpt* gen);
    void remove(gridLinkOpt* lnk);

    virtual void dynObjectInitializeA(std::uint32_t flags) override;
    virtual void loadSizes(const OptimizationMode& oMode) override;

    virtual void setValues(const OptimizationData& of, const OptimizationMode& oMode) override;
    // for saving the state
    virtual void guessState(double time, double val[], const OptimizationMode& oMode) override;
    virtual void getTols(double tols[], const OptimizationMode& oMode) override;
    virtual void getVariableType(double sdata[], const OptimizationMode& oMode) override;

    virtual void valueBounds(double time,
                             double upperLimit[],
                             double lowerLimit[],
                             const OptimizationMode& oMode) override;

    virtual void
        linearObj(const OptimizationData& of,
                  vectData<double>& linObj,
                  const OptimizationMode& oMode) override;
    virtual void quadraticObj(const OptimizationData& of,
                              vectData<double>& linObj,
                              vectData<double>& quadObj,
                              const OptimizationMode& oMode) override;

    virtual double objValue(const OptimizationData& of, const OptimizationMode& oMode) override;
    virtual void
        gradient(const OptimizationData& of, double deriv[], const OptimizationMode& oMode) override;
    virtual void jacobianElements(const OptimizationData& of,
                                  matrixData<double>& md,
                                  const OptimizationMode& oMode) override;
    virtual void getConstraints(const OptimizationData& of,
                                matrixData<double>& cons,
                                double upperLimit[],
                                double lowerLimit[],
                                const OptimizationMode& oMode) override;
    virtual void
        constraintValue(const OptimizationData& of,
                        double cVals[],
                        const OptimizationMode& oMode) override;
    virtual void constraintJacobianElements(const OptimizationData& of,
                                            matrixData<double>& md,
                                            const OptimizationMode& oMode) override;
    virtual void getObjName(stringVec& objNames,
                            const OptimizationMode& oMode,
                            const std::string& prefix = "") override;

    void disable() override;
    // parameter set functions
    virtual void
        setOffsets(const OptimizationOffsets& newOffsets, const OptimizationMode& oMode) override;
    virtual void
        setOffset(index_t offset,
                  index_t constraintOffset,
                  const OptimizationMode& oMode) override;

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
    gridLinkOpt* findLink(gridBus* bs) const;
    coreObject* find(std::string_view objName) const override;
    coreObject* getSubObject(std::string_view typeName, index_t num) const override;
    coreObject* findByUserID(std::string_view typeName, index_t searchID) const override;

    gridOptObject* getLink(index_t x) const override;
    gridOptObject* getLoad(index_t x = 0) const;
    gridOptObject* getGen(index_t x = 0) const;

    gridOptObject* getBus(index_t /*index*/) const override
    {
        return const_cast<gridBusOpt*>(this);
    }
    gridOptObject* getArea(index_t /*index*/) const override
    {
        return static_cast<gridOptObject*>(getParent());
    }

  protected:
};

// bool compareBus (gridBus *bus1, gridBus *bus2, bool cmpLink = false,bool printDiff = false);

gridBusOpt* getMatchingBusOpt(gridBusOpt* bus, const gridOptObject* src, gridOptObject* sec);

}  // namespace griddyn
