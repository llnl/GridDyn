#pragma once

/*
 * Copyright (c) 2014-2026, Lawrence Livermore National Security
 * See the top-level NOTICE for additional details. All rights reserved.
 * SPDX-License-Identifier: BSD-3-Clause
 */

/** @file
@brief file containing classes and types for managing information about FMU's
*/

#include "../FMI2/fmi2TypesPlatform.h"
#include "fmiEnumDefinitions.h"
#include "utilities/matrixDataOrdered.hpp"

#ifdef __GNUC__
#    pragma GCC diagnostic push
#    pragma GCC diagnostic ignored "-Wshadow"
#    include <boost/container/small_vector.hpp>
#    pragma GCC diagnostic pop
#else
#    include <boost/container/small_vector.hpp>
#endif

#include <bitset>
#include <map>
#include <memory>
#include <string>
#include <vector>

/** data class containing the default experiment information*/
class fmuDefaultExpirement {
  public:
    double startTime = 0.0;
    double stopTime = 0.0;
    double stepSize = 0.00;
    double tolerance = 1e-8;
};

/** data class containing information about a variable*/
class variableInformation {
  public:
    int index = -1;
    int derivativeIndex = -1;
    fmi2ValueReference valueRef = 0;
    int aliasIndex = 0;
    std::string name;
    std::string description;
    std::string declType;
    std::string unit;
    std::string initial;
    bool multiSet = false;
    bool reinit = false;
    bool derivative = false;
    bool isAlias = false;
    fmi_variability variability = fmi_variability_type_t::continuous;
    fmi_causality causality = fmi_causality_type_t::local;
    fmi_variable_type type = fmi_variable_type_t::real;
    double start = 0;
    double min = -1e48;
    double max = 1e48;
};

typedef struct {
    std::string name;
    double factor;
    double offset;
} unitDef;

/** data class for storing fmi unit information*/
class fmiUnit {
  public:
    std::string name;
    double factor = 1.0;
    double offset = 0.0;
    std::vector<unitDef> baseUnits;

    std::vector<unitDef> displayUnits;
};

/**data class matching the definition of an FMI type*/
class fmiTypeDefinition {
  public:
    std::string name;
    std::string description;
    std::string quantity;
    std::string unit;
    std::string displayUnit;
    fmi_variable_type type;
    bool relativeQuantity = false;
    bool unbounded = false;
    double min;
    double max;
    double nominal;
};

/** class for storing references to fmi variables*/
class fmiVariableSet {
  public:
    fmiVariableSet();
    fmiVariableSet(fmi2ValueReference newvr);
    fmiVariableSet(const fmiVariableSet& vset);
    fmiVariableSet(fmiVariableSet&& vset);

    fmiVariableSet& operator=(const fmiVariableSet& other);
    fmiVariableSet& operator=(fmiVariableSet&& other);

    const fmi2ValueReference* getValueRef() const;
    size_t getVRcount() const;
    fmi_variable_type_t getType() const;
    /** add a new reference
    @param[in] newvr the value reference to add
    */
    void push(fmi2ValueReference newvr);
    /** add a variable set the existing variable set*
    @param[in] vset the variableSet to add
    */
    void push(const fmiVariableSet& vset);
    /** reserve a set amount of space in the set
    @param[in] newSize the number of elements to reserve*/
    void reserve(size_t newSize);
    void remove(fmi2ValueReference rmvr);
    void clear();

  private:
    fmi_variable_type type = fmi_variable_type_t::real;
    // boost::container::small_vector<fmi2ValueReference, 4> vrset;
    std::vector<fmi2ValueReference> vrset;
};

class readerElement;
/** class to extract and store the information in an FMU XML file*/
class fmiInfo {
  private:
    std::map<std::string, std::string> headerInfo;  //!< the header information contained in strings
    double fmiVersion;  //!< the fmi version used
    int numberOfEvents;  //!< the number of defined events
    int maxOrder;  //!< the maximum derivative order for CoSimulation FMU's
    std::bitset<32> capabilities;  //!< bitset containing the capabilities of the FMU
    std::vector<variableInformation> variables;  //!< information all the defined variables
    std::vector<fmiUnit> units;  //!< all the units defined in the FMU
    fmuDefaultExpirement
        defaultExpirement;  //!< the information about the specified default experiment

    std::map<std::string, int>
        variableLookup;  //!< map translating strings to indices into the variables array

    matrixDataOrdered<sparse_ordering::row_ordered, int>
        outputDep;  //!< the output dependency information
    matrixDataOrdered<sparse_ordering::row_ordered, int>
        derivDep;  //!< the derivative dependency information
    matrixDataOrdered<sparse_ordering::row_ordered, int>
        unknownDep;  //!< the initial unknown dependency information
    std::vector<int> outputs;  //!< a list of the output indices
    std::vector<int> parameters;  //!< a list of all the parameters
    std::vector<int> local;  //!< a list of the local variables
    std::vector<int> states;  //!< a list of the states
    std::vector<int> deriv;  //!< a list of the derivative information
    std::vector<int> initUnknown;  //!< a list of the unknowns
    std::vector<int> inputs;  //!< a list of the inputs
  public:
    fmiInfo();
    explicit fmiInfo(const std::string& xmlFile);
    int loadFile(const std::string& xmlFile);
    /** check if a given flag is set*/
    bool checkFlag(fmuCapabilityFlags flag) const;
    /** get the counts for various items in a fmu
    @details
    @param[in] countType the type of counts to get
    @return the count*/
    int getCounts(const std::string& countType) const;
    const std::string& getString(const std::string& field) const;
    /** get a Real variable by name*/
    double getReal(const std::string& field) const;
    const variableInformation& getVariableInfo(const std::string& variableName) const;
    const variableInformation& getVariableInfo(unsigned int index) const;
    /** get a set of variables for the specified parameters*/
    fmiVariableSet getReferenceSet(const std::vector<std::string>& variableList) const;
    /** get a variable set with a single member*/
    fmiVariableSet getVariableSet(const std::string& variable) const;
    /** get a variable set with a single member based on index*/
    fmiVariableSet getVariableSet(unsigned int index) const;
    /** get a set of the current outputs*/
    fmiVariableSet getOutputReference() const;
    /** get a set of the current inputs*/
    fmiVariableSet getInputReference() const;
    /** get a list of variable names by type
    @param[in] type the type of variable
    @return a vector of strings with the names of the variables
    */
    std::vector<std::string> getVariableNames(const std::string& type) const;
    /** get a list of variable indices by type
    @param[in] type the type of variable
    @return a vector of ints with the indices of the variables
    */
    const std::vector<int>& getVariableIndices(const std::string& type) const;
    /** get the variable indices of the derivative dependencies*/
    const std::vector<std::pair<index_t, int>>& getDerivDependencies(int variableIndex) const;
    const std::vector<std::pair<index_t, int>>& getOutputDependencies(int variableIndex) const;
    const std::vector<std::pair<index_t, int>>& getUnknownDependencies(int variableIndex) const;

  private:
    void loadFmiHeader(std::shared_ptr<readerElement>& rd);
    void loadVariables(std::shared_ptr<readerElement>& rd);
    void loadUnitInformation(std::shared_ptr<readerElement>& rd);
    void loadStructure(std::shared_ptr<readerElement>& rd);
};

enum class fmuMode {
    instantiatedMode,
    initializationMode,
    continuousTimeMode,
    eventMode,
    stepMode,  //!< step Mode is a synonym for event mode that make more sense for cosimulation
    terminated,
    error,
};

bool checkType(const variableInformation& info,
               fmi_variable_type_t type,
               fmi_causality_type_t caus);
