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
class gridBusOpt;
class Generator;

class gridGenOpt: public gridOptObject {
  public:
    enum OptGenFlags {
        PIECEWISE_LINEAR_COST = 1,
        LIMIT_OVERRIDE = 2,
    };

  protected:
    Generator* gen = nullptr;
    gridBusOpt* bus;
    double m_heatRate = -kBigNum;
    std::vector<double> Pcoeff;
    std::vector<double> Qcoeff;
    double m_penaltyCost = 0;
    double m_fuelCost = -1;
    double m_Pmax = kBigNum;
    double m_Pmin = kBigNum;
    double m_forecast = -kBigNum;
    double systemBasePower = 100.0;  //!< the base power of the generator
    double mBase = 100.0;  //!< the machine base of the generator
  public:
    gridGenOpt(const std::string& objName = "");
    gridGenOpt(coreObject* obj, const std::string& objName = "");

    virtual coreObject* clone(coreObject* obj = nullptr) const override;
    // add components

    virtual void add(coreObject* obj) override;
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

    // parameter set functions
    virtual void set(std::string_view param, std::string_view val) override;
    virtual void
        set(std::string_view param, double val, units::unit unitType = units::defunit) override;
    // parameter get functions
    virtual double get(std::string_view param,
                       units::unit unitType = units::defunit) const override;

    virtual void loadCostCoeff(std::vector<double> const& coeff, int mode);
    // find components

    virtual gridOptObject* getBus(index_t index) const override;
    virtual gridOptObject* getArea(index_t index) const override;

  protected:
};

}  // namespace griddyn
