/*
 * Copyright (c) 2014-2026, Lawrence Livermore National Security
 * See the top-level NOTICE for additional details. All rights reserved.
 * SPDX-License-Identifier: BSD-3-Clause
 */
#pragma once

#include "Sensor.h"
#include <cstdint>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace griddyn {
class GridBus;
class Link;

/** Continuous bus measurement whose states belong to the containing area. */
class BusMeasurementSensor: public Sensor {
  public:
    explicit BusMeasurementSensor(const std::string& name);
    CoreObject* clone(CoreObject* obj = nullptr) const override;
    void dynObjectInitializeA(CoreTime time0, std::uint32_t flags) override;
    void dynObjectInitializeB(const IOdata& inputs,
                              const IOdata& desiredOutput,
                              IOdata& fieldSet) override;
    double getOutput(const IOdata& inputs,
                     const StateData& stateData,
                     const SolverMode& sMode,
                     index_t outNum = 0) const override;
    double getOutput(index_t outNum = 0) const override;
    index_t getOutputLoc(const SolverMode& sMode, index_t outNum) const override;
    void outputPartialDerivatives(const IOdata& inputs,
                                  const StateData& stateData,
                                  MatrixData<double>& matrixData,
                                  const SolverMode& sMode) override;
    stringVec localStateNames() const override;
    GridBus* sourceBus() const { return bus(); }

  protected:
    GridBus* bus() const;
    double angle(const StateData& stateData, const SolverMode& sMode) const;
    double voltage(const StateData& stateData, const SolverMode& sMode) const;
    void defineStates(count_t alg, count_t diff, stringVec names);
    virtual index_t outputState(index_t outNum) const = 0;
    stringVec stateNames;
};

/** ANDES PLL1/PLL2 equations; each instance has an independent bus source and state. */
class PLLSensor: public BusMeasurementSensor {
  public:
    explicit PLLSensor(const std::string& name = "PLL_#", bool voltagePhase = false);
    CoreObject* clone(CoreObject* obj = nullptr) const override;
    using BusMeasurementSensor::set;
    void set(std::string_view param, double value, units::unit unitType = units::defunit) override;
    double get(std::string_view param, units::unit unitType = units::defunit) const override;
    void dynObjectInitializeA(CoreTime time0, std::uint32_t flags) override;
    void dynObjectInitializeB(const IOdata& inputs,
                              const IOdata& desiredOutput,
                              IOdata& fieldSet) override;
    void residual(const IOdata& inputs,
                  const StateData& stateData,
                  double resid[],
                  const SolverMode& sMode) override;
    void derivative(const IOdata& inputs,
                    const StateData& stateData,
                    double deriv[],
                    const SolverMode& sMode) override;
    void algebraicUpdate(const IOdata& inputs,
                         const StateData& stateData,
                         double update[],
                         const SolverMode& sMode,
                         double alpha) override;
    void jacobianElements(const IOdata& inputs,
                          const StateData& stateData,
                          MatrixData<double>& matrixData,
                          const IOlocs& inputLocs,
                          const SolverMode& sMode) override;
    void timestep(CoreTime time, const IOdata& inputs, const SolverMode& sMode) override;

  protected:
    index_t outputState(index_t outNum) const override;
    bool phaseDetector = false;
    double Kp = 0.1, Ki = 0.1, Tf = 0.05, Tp = 0.05, fn = 60.0;
};

class PLL1Sensor final: public PLLSensor {
  public:
    explicit PLL1Sensor(const std::string& objName = "PLL1_#"): PLLSensor(objName, false) {}
    CoreObject* clone(CoreObject* obj = nullptr) const override;
};

class PLL2Sensor final: public PLLSensor {
  public:
    explicit PLL2Sensor(const std::string& objName = "PLL2_#"): PLLSensor(objName, true) {}
    CoreObject* clone(CoreObject* obj = nullptr) const override;
};

/** Bus-angle frequency deviation and ROCOF with the BUSROCOF filter equations. */
class BusROCOFSensor final: public BusMeasurementSensor {
  public:
    explicit BusROCOFSensor(const std::string& name = "BUSROCOF_#");
    CoreObject* clone(CoreObject* obj = nullptr) const override;
    using BusMeasurementSensor::set;
    void set(std::string_view param, double value, units::unit unitType = units::defunit) override;
    double get(std::string_view param, units::unit unitType = units::defunit) const override;
    void dynObjectInitializeA(CoreTime time0, std::uint32_t flags) override;
    void dynObjectInitializeB(const IOdata& inputs,
                              const IOdata& desiredOutput,
                              IOdata& fieldSet) override;
    void residual(const IOdata& inputs,
                  const StateData& stateData,
                  double resid[],
                  const SolverMode& sMode) override;
    void derivative(const IOdata& inputs,
                    const StateData& stateData,
                    double deriv[],
                    const SolverMode& sMode) override;
    void algebraicUpdate(const IOdata& inputs,
                         const StateData& stateData,
                         double update[],
                         const SolverMode& sMode,
                         double alpha) override;
    void jacobianElements(const IOdata& inputs,
                          const StateData& stateData,
                          MatrixData<double>& matrixData,
                          const IOlocs& inputLocs,
                          const SolverMode& sMode) override;
    void timestep(CoreTime time, const IOdata& inputs, const SolverMode& sMode) override;

  protected:
    index_t outputState(index_t outNum) const override;

  private:
    double Tf = 0.02, Tw = 0.1, Tr = 0.1, fn = 60.0;
    double initialAngle = 0.0, lastAngle = 0.0;
    double deviation(const double diff[]) const;
};

/** Algebraic network frequency divider on a closed electrical area. */
class FreqDivSensor final: public BusMeasurementSensor {
  public:
    explicit FreqDivSensor(const std::string& name = "FreqDiv_#");
    CoreObject* clone(CoreObject* obj = nullptr) const override;
    void dynObjectInitializeA(CoreTime time0, std::uint32_t flags) override;
    void dynObjectInitializeB(const IOdata& inputs,
                              const IOdata& desiredOutput,
                              IOdata& fieldSet) override;
    void residual(const IOdata& inputs,
                  const StateData& stateData,
                  double resid[],
                  const SolverMode& sMode) override;
    void algebraicUpdate(const IOdata& inputs,
                         const StateData& stateData,
                         double update[],
                         const SolverMode& sMode,
                         double alpha) override;
    void jacobianElements(const IOdata& inputs,
                          const StateData& stateData,
                          MatrixData<double>& matrixData,
                          const IOlocs& inputLocs,
                          const SolverMode& sMode) override;

  protected:
    index_t outputState(index_t outNum) const override;

  private:
    struct Neighbor {
        FreqDivSensor* sensor;
        Link* link;
        bool firstTerminal;
    };
    struct Machine {
        class DynamicGenerator* generator;
        double coefficient;
    };
    std::vector<Neighbor> neighbors;
    std::vector<Machine> machines;
    double diagonal = 0.0;
    static std::pair<double, double> lineCoefficients(const Neighbor& neighbor);
    double effectiveDiagonal() const;
    double frequencyResidual(const StateData& stateData,
                             const SolverMode& sMode,
                             double ownFrequency) const;
};

}  // namespace griddyn
