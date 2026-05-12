/*
 * Copyright (c) 2014-2026, Lawrence Livermore National Security
 * See the top-level NOTICE for additional details. All rights reserved.
 * SPDX-License-Identifier: BSD-3-Clause
 */

#include "elementReaderTemplates.hpp"
#include "fileInput.h"
#include "gmlc/utilities/stringConversion.h"
#include "griddyn/gridDynSimulation.h"
#include "griddyn/solvers/solverInterface.h"
#include "readElement.h"
#include <memory>
#include <string>

namespace griddyn {
using readerConfig::defMatchType;
// aP is the link element

static const IgnoreListType solverIgnoreFields{"flags", "name", "type", "index"};

void loadSolverElement(std::shared_ptr<readerElement>& element,
                       readerInfo& ri,
                       gridDynSimulation* parentObject)
{
    std::shared_ptr<SolverInterface> solverDefinition;
    std::string type = getElementField(element, "type", defMatchType);
    if (type.empty()) {
    }
    std::string solverIdentifier = getElementField(element, "index", defMatchType);
    // check for the field attributes
    if (!solverIdentifier.empty()) {
        int index = gmlc::utilities::numeric_conversion(solverIdentifier, -1);

        if (index >= 0) {
            solverDefinition = parentObject->getSolverInterface(index);
        }
        if (!(solverDefinition)) {
            if (!type.empty()) {
                solverDefinition = makeSolver(type);
                if (solverDefinition) {
                    solverDefinition->set("index", index);
                }
            }
        }
    }
    solverIdentifier = getElementField(element, "name", defMatchType);
    if (!solverIdentifier.empty()) {
        if (solverDefinition) {
            if (solverDefinition->getSolverMode().offsetIndex >
                1)  // don't allow overriding the names on solvermode index 0 and 1
            {
                solverDefinition->setName(solverIdentifier);
            }
        } else {
            solverDefinition = parentObject->getSolverInterface(solverIdentifier);
            if (!(solverDefinition)) {
                if (!type.empty()) {
                    solverDefinition = makeSolver(type);
                    if (solverDefinition) {
                        solverDefinition->setName(solverIdentifier);
                    }
                }
            }
        }
    }
    if (!(solverDefinition)) {
        if (type.empty()) {
            if (!solverIdentifier.empty()) {
                solverDefinition = makeSolver(solverIdentifier);
            }
        } else {
            solverDefinition = makeSolver(type);
        }
        if (!(solverDefinition)) {
            return;
        }
        if (!solverIdentifier.empty()) {
            solverDefinition->setName(solverIdentifier);
        }
    }
    std::string indexField = getElementField(element, "index", defMatchType);
    if (!indexField.empty()) {
        solverDefinition->set("flags", indexField);
    }

    setAttributes(solverDefinition.get(), element, "solver", ri, solverIgnoreFields);
    setParams(solverDefinition.get(), element, "solver", ri, solverIgnoreFields);
    // add the solver
    parentObject->add(solverDefinition);
}

}  // namespace griddyn
