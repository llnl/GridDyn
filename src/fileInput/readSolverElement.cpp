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
namespace {
    const IgnoreListType& solverIgnoreFields()
    {
        static const auto* ignoreFields = new IgnoreListType{"flags", "name", "type", "index"};
        return *ignoreFields;
    }
}  // namespace

void loadSolverElement(std::shared_ptr<readerElement>& element,
                       readerInfo& readerInformation,
                       gridDynSimulation* parentObject)
{
    std::shared_ptr<SolverInterface> solverDefinition;
    const std::string type = getElementField(element, "type", defMatchType);
    if (type.empty()) {
    }
    std::string solverIdentifier = getElementField(element, "index", defMatchType);
    // check for the field attributes
    if (!solverIdentifier.empty()) {
        const int index = gmlc::utilities::numeric_conversion(solverIdentifier, -1);

        if (index >= 0) {
            solverDefinition = parentObject->getSolverInterface(index);
        }
        if (!solverDefinition) {
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
            if (!solverDefinition) {
                if (!type.empty()) {
                    solverDefinition = makeSolver(type);
                    if (solverDefinition) {
                        solverDefinition->setName(solverIdentifier);
                    }
                }
            }
        }
    }
    if (!solverDefinition) {
        if (type.empty()) {
            if (!solverIdentifier.empty()) {
                solverDefinition = makeSolver(solverIdentifier);
            }
        } else {
            solverDefinition = makeSolver(type);
        }
        if (!solverDefinition) {
            return;
        }
        if (!solverIdentifier.empty()) {
            solverDefinition->setName(solverIdentifier);
        }
    }
    const std::string indexField = getElementField(element, "index", defMatchType);
    if (!indexField.empty()) {
        solverDefinition->set("flags", indexField);
    }

    setAttributes(
        solverDefinition.get(), element, "solver", readerInformation, solverIgnoreFields());
    setParams(solverDefinition.get(), element, "solver", readerInformation, solverIgnoreFields());
    // add the solver
    parentObject->add(solverDefinition);
}

}  // namespace griddyn
