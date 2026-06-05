/*
 * Copyright (c) 2014-2026, Lawrence Livermore National Security
 * See the top-level NOTICE for additional details. All rights reserved.
 * SPDX-License-Identifier: BSD-3-Clause
 */

#pragma once

#include "../Generator.h"
#include <string>
#include <vector>

namespace griddyn {
class GenModel;
class Exciter;
class Stabilizer;
class Governor;
class IsocController;
class Source;
/**
@ brief class describing a generator intended for dynamic simulations
 a generator is a power production unit in GridDyn.  the base generator class implements methods set
forth in the GridSecondary class and inherits from that class it has mechanics and interfaces for
handling any and all of 4 different components, namely and exciter, governor, generator model, and a
power system stabilizer. as well as control for the power set point and voltage set point
*/
class DynamicGenerator: public Generator {
  public:
    enum class DynModel {
        INVALID,
        SIMPLE,
        DC,
        TRANSIENT,
        DETAILED,
        MODEL_ONLY,
        TYPICAL,
        SUBTRANSIENT,
        RENEWABLE,
        NONE,
    };

    /** @brief enum indicating subModel locations in the subObject structure*/
    enum SubModelLocations {
        GEN_MODEL_LOC = 1,
        EXCITER_LOC = 2,
        GOVERNOR_LOC = 3,
        PSS_LOC = 4,
        PSET_LOC = 5,
        VSET_LOC = 6,
        ISOC_CONTROL_LOC = 7,
    };

  protected:
    GenModel* genModel = nullptr;  //!< generator model
    Exciter* ext = nullptr;  //!< exciter model
    Governor* gov = nullptr;  //!< governor model
    Stabilizer* pss = nullptr;  //!< power system stabilizer type
    Source* pSetControl = nullptr;  //!< source for throttle control
    Source* vSetControl = nullptr;  //!< source for voltage level control
    IsocController* isoc = nullptr;  //!< pointer to a isochronous controller
    // const double *m_stateTemp = nullptr;                       //!< temporary state
    // vector(assumed not writable) const double *m_dstate_dt_Temp = nullptr;                   //!<
    // a temporary deriv vector;

    // std::vector<double> m_state_ind;                              //!< a vector holding the
    // indices to indicate if a variable is a algebraic or differential state count_t SSize = 0;
    // //!< the total number of states
    double m_Eft = 0;  //!< place to store a constant the exciter field
    double m_Pmech = 0;  //!< place to store a constant power output
  public:
    static DynModel dynModelFromString(const std::string& dynModelType);
    /** @brief default constructor
    @param[in] dynModel  a string with the dynmodel description*/
    DynamicGenerator(DynModel dynModel, const std::string& objName = "gen_$");
    explicit DynamicGenerator(const std::string& objName = "gen_$");
    virtual CoreObject* clone(CoreObject* obj = nullptr) const override;

    virtual void dynObjectInitializeA(CoreTime time0, std::uint32_t flags) override;

    virtual void dynObjectInitializeB(const IOdata& inputs,
                                      const IOdata& desiredOutput,
                                      IOdata& fieldSet) override;
    virtual void setState(CoreTime time,
                          const double state[],
                          const double dstateDt[],
                          const SolverMode& sMode) override;  // for saving the state
    virtual void guessState(CoreTime time,
                            double state[],
                            double dstateDt[],
                            const SolverMode& sMode) override;  // for initial setting of the state
    virtual void updateLocalCache(const IOdata& inputs,
                                  const StateData& stateDataValue,
                                  const SolverMode& sMode) override;
    virtual void set(std::string_view param, std::string_view val) override;
    virtual void
        set(std::string_view param, double val, units::unit unitType = units::defunit) override;
    // virtual double get (const std::string &param, units::unit unitType = units::defunit) const
    // override;
    virtual void setFlag(std::string_view flag, bool val = true) override;

    virtual void add(CoreObject* obj) override;
    /** @brief additional add function specific to subModels
    @param[in] obj submodel to add
    @throw unrecognizedObjectError is object is not valid*/
    virtual void add(GridSubModel* obj) override;

    virtual void algebraicUpdate(const IOdata& inputs,
                                 const StateData& stateDataValue,
                                 double update[],
                                 const SolverMode& sMode,
                                 double alpha) override;
    virtual void residual(const IOdata& inputs,
                          const StateData& stateDataValue,
                          double resid[],
                          const SolverMode& sMode) override;
    virtual IOdata getOutputs(const IOdata& inputs,
                              const StateData& stateDataValue,
                              const SolverMode& sMode) const override;

    virtual void derivative(const IOdata& inputs,
                            const StateData& stateDataValue,
                            double deriv[],
                            const SolverMode& sMode) override;

    virtual void outputPartialDerivatives(const IOdata& inputs,
                                          const StateData& stateDataValue,
                                          MatrixData<double>& matrixDataValue,
                                          const SolverMode& sMode) override;
    virtual void ioPartialDerivatives(const IOdata& inputs,
                                      const StateData& stateDataValue,
                                      MatrixData<double>& matrixDataValue,
                                      const IOlocs& inputLocs,
                                      const SolverMode& sMode) override;
    virtual count_t outputDependencyCount(index_t num, const SolverMode& sMode) const override;

    virtual void jacobianElements(const IOdata& inputs,
                                  const StateData& stateDataValue,
                                  MatrixData<double>& matrixDataValue,
                                  const IOlocs& inputLocs,
                                  const SolverMode& sMode) override;
    virtual void getStateName(stringVec& stNames,
                              const SolverMode& sMode,
                              const std::string& prefix) const override;

    virtual void timestep(CoreTime time, const IOdata& inputs, const SolverMode& sMode) override;

    virtual void rootTest(const IOdata& inputs,
                          const StateData& stateDataValue,
                          double roots[],
                          const SolverMode& sMode) override;
    virtual void rootTrigger(CoreTime time,
                             const IOdata& inputs,
                             const std::vector<int>& rootMask,
                             const SolverMode& sMode) override;
    virtual ChangeCode rootCheck(const IOdata& inputs,
                                 const StateData& stateDataValue,
                                 const SolverMode& sMode,
                                 CheckLevel level) override;

    using Generator::getReactivePower;
    using Generator::getRealPower;
    virtual double getRealPower(const IOdata& inputs,
                                const StateData& stateDataValue,
                                const SolverMode& sMode) const override;
    virtual double getReactivePower(const IOdata& inputs,
                                    const StateData& stateDataValue,
                                    const SolverMode& sMode) const override;

    virtual index_t findIndex(std::string_view field, const SolverMode& sMode) const override;
    virtual CoreObject* find(std::string_view object) const override;
    virtual CoreObject* getSubObject(std::string_view typeName, index_t num) const override;
    virtual double getFreq(const StateData& stateDataValue,
                           const SolverMode& sMode,
                           index_t* freqOffset = nullptr) const override;
    virtual double getAngle(const StateData& stateDataValue,
                            const SolverMode& sMode,
                            index_t* angleOffset = nullptr) const override;

  protected:
    virtual double pSetControlUpdate(const IOdata& inputs,
                                     const StateData& stateDataValue,
                                     const SolverMode& sMode);
    virtual double vSetControlUpdate(const IOdata& inputs,
                                     const StateData& stateDataValue,
                                     const SolverMode& sMode);
    virtual index_t pSetLocation(const SolverMode& sMode);
    virtual index_t vSetLocation(const SolverMode& sMode);

  protected:
    class SubModelInputs {
      public:
        std::vector<IOdata> inputs;
        count_t seqID = 0;
        SubModelInputs();
    };
    class SubModelInputLocs {
      public:
        IOlocs genModelInputLocsInternal;
        IOlocs genModelInputLocsExternal;
        std::vector<IOlocs> inputLocs;
        count_t seqID = 0;
        SubModelInputLocs();
    };
    SubModelInputs subInputs;
    SubModelInputLocs subInputLocs;

    virtual void generateSubModelInputs(const IOdata& inputs,
                                        const StateData& stateDataValue,
                                        const SolverMode& sMode);
    virtual void generateSubModelInputLocs(const IOlocs& inputLocs,
                                           const StateData& stateDataValue,
                                           const SolverMode& sMode);

    GridSubModel* replaceModel(GridSubModel* newObject, GridSubModel* oldObject, index_t newIndex);

    void buildDynModel(DynModel dynModel);
};

}  // namespace griddyn
