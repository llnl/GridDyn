/*
 * Copyright (c) 2014-2020, Lawrence Livermore National Security
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
    enum bus_flags {

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
    virtual void loadSizes(const optimMode& oMode) override;

    virtual void setValues(const optimData& of, const optimMode& oMode) override;
    // for saving the state
    virtual void guessState(double time, double val[], const optimMode& oMode) override;
    virtual void getTols(double tols[], const optimMode& oMode) override;
    virtual void getVariableType(double sdata[], const optimMode& oMode) override;

    virtual void valueBounds(double time,
                             double upperLimit[],
                             double lowerLimit[],
                             const optimMode& oMode) override;

    virtual void
        linearObj(const optimData& of, vectData<double>& linObj, const optimMode& oMode) override;
    virtual void quadraticObj(const optimData& of,
                              vectData<double>& linObj,
                              vectData<double>& quadObj,
                              const optimMode& oMode) override;

    virtual double objValue(const optimData& of, const optimMode& oMode) override;
    virtual void gradient(const optimData& of, double deriv[], const optimMode& oMode) override;
    virtual void jacobianElements(const optimData& of,
                                  matrixData<double>& md,
                                  const optimMode& oMode) override;
    virtual void getConstraints(const optimData& of,
                                matrixData<double>& cons,
                                double upperLimit[],
                                double lowerLimit[],
                                const optimMode& oMode) override;
    virtual void
        constraintValue(const optimData& of, double cVals[], const optimMode& oMode) override;
    virtual void constraintJacobianElements(const optimData& of,
                                            matrixData<double>& md,
                                            const optimMode& oMode) override;
    virtual void getObjName(stringVec& objNames,
                            const optimMode& oMode,
                            const std::string& prefix = "") override;

    void disable() override;
    // parameter set functions
    virtual void setOffsets(const optimOffsets& newOffsets, const optimMode& oMode) override;
    virtual void
        setOffset(index_t offset, index_t constraintOffset, const optimMode& oMode) override;

    void setAll(const std::string& type,
                const std::string& param,
                double val,
                units::unit unitType = units::defunit);
    void set(const std::string& param, const std::string& val) override;
    void set(const std::string& param, double val, units::unit unitType = units::defunit) override;
    // parameter get functions
    virtual double get(const std::string& param,
                       units::unit unitType = units::defunit) const override;

    // void alert (coreObject *object, int code);

    // find components
    gridLinkOpt* findLink(gridBus* bs) const;
    coreObject* find(const std::string& objName) const override;
    coreObject* getSubObject(const std::string& typeName, index_t num) const override;
    coreObject* findByUserID(const std::string& typeName, index_t searchID) const override;

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
