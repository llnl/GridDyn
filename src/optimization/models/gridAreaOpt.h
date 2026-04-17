#pragma once

/*
 * Copyright (c) 2014-2026, Lawrence Livermore National Security
 * See the top-level NOTICE for additional details. All rights reserved.
 * SPDX-License-Identifier: BSD-3-Clause
 */

// headers

#include "../gridOptObjects.h"
#include "core/coreObjectList.h"
#include <string>
#include <vector>
// forward classes

namespace griddyn {
class gridBusOpt;
class gridLinkOpt;
class gridRelayOpt;

class Area;

class gridAreaOpt: public gridOptObject {
  public:
  protected:
    std::vector<gridAreaOpt*> areaList;
    std::vector<gridBusOpt*> busList;
    std::vector<gridLinkOpt*> linkList;
    std::vector<gridRelayOpt*> relayList;

    std::vector<gridOptObject*> objectList;
    Area* area = nullptr;

    coreObjectList optObList;  // a search index for object names
  public:
    gridAreaOpt(const std::string& objName = "");
    gridAreaOpt(coreObject* obj, const std::string& objName = "");
    ~gridAreaOpt();

    virtual coreObject* clone(coreObject* obj = nullptr) const override;
    // add components

    void add(coreObject* obj) override;
    void add(gridAreaOpt* area);
    void add(gridBusOpt* bus);
    void add(gridLinkOpt* lnk);
    void add(gridRelayOpt* relay);

    // remove components
    void remove(coreObject* obj) override;
    void remove(gridAreaOpt* area);
    void remove(gridBusOpt* bus);
    void remove(gridLinkOpt* lnk);
    void remove(gridRelayOpt* relay);

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

    virtual void disable() override;
    // parameter set functions
    virtual void setOffsets(const optimOffsets& newOffsets, const optimMode& oMode) override;
    virtual void
        setOffset(index_t offset, index_t constraintOffset, const optimMode& oMode) override;

    virtual void setAll(const std::string& type,
                        const std::string& param,
                        double val,
                        units::unit unitType = units::defunit);
    virtual void set(const std::string& param, const std::string& val) override;
    virtual void
        set(const std::string& param, double val, units::unit unitType = units::defunit) override;
    // parameter get functions
    virtual double get(const std::string& param,
                       units::unit unitType = units::defunit) const override;

    virtual bool isMember(coreObject* obj) const;
    // find components

    virtual coreObject* find(const std::string& objName) const override;
    virtual coreObject* getSubObject(const std::string& typeName, index_t num) const override;
    virtual coreObject* findByUserID(const std::string& typeName, index_t searchID) const override;

    virtual gridOptObject* getBus(index_t index) const override;
    virtual gridOptObject* getArea(index_t index) const override;
    virtual gridOptObject* getRelay(index_t index) const override;
    virtual gridOptObject* getLink(index_t index) const override;

  protected:
};

gridAreaOpt* getMatchingArea(gridAreaOpt* area, gridOptObject* src, gridOptObject* sec);

}  // namespace griddyn
