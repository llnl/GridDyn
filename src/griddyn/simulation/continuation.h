/*
 * Copyright (c) 2014-2026, Lawrence Livermore National Security
 * See the top-level NOTICE for additional details. All rights reserved.
 * SPDX-License-Identifier: BSD-3-Clause
 */

#pragma once

#include "../events/Event.h"
#include "../measurement/collector.h"
#include <string>
#include <vector>

namespace griddyn {
class gridDynSimulation;

class parameterSequence {
  public:
    coreObject* m_target;
    std::string m_field;
    double m_startValue;
    double m_stepSize;
    units::unit m_unitType = units::defunit;

  protected:
    int m_currentStep = 0;
    coreObject* m_obj = nullptr;

  public:
    parameterSequence();
    bool setTarget(coreObject* gdo, const std::string var = "");
    void setValue(double start, double step) const;
    void step();
    void step(int stepNumber);
};

class continuationSequence {
  public:
    static int contCount;  // static variable counting the number of created lines
    std::string name;
    int id;
    std::vector<std::shared_ptr<parameterSequence>>
        SequenceList;  //!< vector storing sequence objects
    std::vector<std::shared_ptr<Recorder>> recordList;  //!< vector storing recorder objects
  protected:
    int m_currentStep = 0;

  public:
    continuationSequence();
    void step();
    void step(int StepNumber);
};

}  // namespace griddyn
