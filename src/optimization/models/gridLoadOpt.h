/*
 * Copyright (c) 2014-2020, Lawrence Livermore National Security
 * See the top-level NOTICE for additional details. All rights reserved.
 * SPDX-License-Identifier: BSD-3-Clause
 */

#ifndef ___W_GRIDDYN_GRIDDYN_SRC_OPTIMIZATION_MODELS_GRIDLOADOPT_H_
#define ___W_GRIDDYN_GRIDDYN_SRC_OPTIMIZATION_MODELS_GRIDLOADOPT_H_

// headers
#include "../gridOptObjects.h"
#include <string>
// forward classes

namespace griddyn {
class gridBusOpt;
class zipLoad;

class gridLoadOpt: public gridOptObject {
  public:
  protected:
    gridBusOpt* bus = nullptr;
    zipLoad* load = nullptr;

  public:
    gridLoadOpt(const std::string& objName = "");
    gridLoadOpt(coreObject* obj, const std::string& objName = "");

    ~gridLoadOpt();

    virtual coreObject* clone(coreObject* obj = nullptr) const override;

    virtual void add(coreObject* obj) override;
    virtual count_t objSize(const optimMode& oMode);
    virtual count_t contObjSize(const optimMode& oMode);
    virtual count_t intObjSize(const optimMode& oMode);
    virtual count_t constraintSize(const optimMode& oMode);

    virtual void dynObjectInitializeA(std::uint32_t flags) override;

    virtual void setValues(const optimData& of, const optimMode& oMode) override;
    // for saving the state
    virtual void guessState(double time, double val[], const optimMode& oMode) override;
    virtual void getTols(double tols[], const optimMode& oMode) override;
    virtual void getVariableType(double sdata[], const optimMode& oMode) override;

    virtual void valueBounds(double time,
                             double upLimit[],
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

    virtual void set(const std::string& param, const std::string& val) override;
    virtual void
        set(const std::string& param, double val, units::unit unitType = units::defunit) override;
    // parameter get functions
    virtual double get(const std::string& param,
                       units::unit unitType = units::defunit) const override;

    virtual gridOptObject* getBus(index_t index) const override;
    virtual gridOptObject* getArea(index_t index) const override;

  protected:
};

}  // namespace griddyn
#endif  // ___W_GRIDDYN_GRIDDYN_SRC_OPTIMIZATION_MODELS_GRIDLOADOPT_H_
