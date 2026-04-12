/*
 * LLNS Copyright Start
 * Copyright (c) 2014-2018, Lawrence Livermore National Security
 * This work was performed under the auspices of the U.S. Department
 * of Energy by Lawrence Livermore National Laboratory in part under
 * Contract W-7405-Eng-48 and in part under Contract DE-AC52-07NA27344.
 * Produced at the Lawrence Livermore National Laboratory.
 * All rights reserved.
 * For details, see the LICENSE file.
 * LLNS Copyright End
 */

#include "core/coreExceptions.h"
#include "core/coreOwningPtr.hpp"
#include "core/objectFactory.hpp"
#include "griddyn/gridComponentHelperClasses.h"
#include "griddyn/gridDynSimulation.h"
#include "griddyn_export_advanced.h"
#include "internal/griddyn_export_internal.h"
#include "utilities/matrixDataCustomWriteOnly.hpp"
#include <vector>

using griddyn::dyn_initialized;
using griddyn::emptyStateData;
using griddyn::gridComponent;
using griddyn::gridDynSimulation;
using griddyn::IOdata;
using griddyn::IOlocs;
using griddyn::solverMode;
using griddyn::stateData;

static constexpr char invalidComponent[] = "the Griddyn object is not valid";
static constexpr char invalidSolver[] = "the given solver key was not valid";

int gridDynObjectStateSize(GridDynObject obj, SolverKey key, GridDynError* err)
{
    gridComponent* comp = getComponentPointer(obj);

    if (comp == nullptr) {
        assignError(err, griddyn_error_invalid_object, invalidComponent);
        return griddyn_error_invalid_object;
    }
    const auto& sMode = static_cast<const solverKeyInfo*>(key)->sMode_;
    if ((sMode.offsetIndex < 0) || (sMode.offsetIndex > 500)) {
        assignError(err, griddyn_error_invalid_object, invalidSolver);
        return griddyn_error_invalid_object;
    }
    return static_cast<int>(comp->stateSize(sMode));
}

void setUpSolverKeyInfo(solverKeyInfo* key, gridComponent* comp)
{
    auto* root = dynamic_cast<gridDynSimulation*>(comp->getRoot());
    auto ssize = root->stateSize(key->sMode_);
    key->stateBuffer.resize(ssize);
    key->dstateBuffer.resize(ssize);
}

void TranslateToLocal(const std::vector<double>& orig,
                      double* newData,
                      const gridComponent* comp,
                      const solverMode& sMode)
{
    auto offsets = comp->getOffsets(sMode);
    double* cData = newData;
    for (index_t ii = 0; ii < offsets.total.vSize; ii++) {
        *cData = orig[offsets.vOffset + ii];
        ++cData;
    }

    for (index_t ii = 0; ii < offsets.total.aSize; ii++) {
        *cData = orig[offsets.aOffset + ii];
        ++cData;
    }

    for (index_t ii = 0; ii < offsets.total.algSize; ii++) {
        *cData = orig[offsets.algOffset + ii];
        ++cData;
    }

    for (index_t ii = 0; ii < offsets.total.diffSize; ii++) {
        *cData = orig[offsets.algOffset + ii];
        ++cData;
    }
}

void CopyFromLocal(std::vector<double>& dest,
                   const double* localData,
                   const gridComponent* comp,
                   const solverMode& sMode)
{
    auto offsets = comp->getOffsets(sMode);
    const double* cData = localData;
    for (index_t ii = 0; ii < offsets.total.vSize; ii++) {
        dest[offsets.vOffset + ii] = *cData;
        ++cData;
    }

    for (index_t ii = 0; ii < offsets.total.aSize; ii++) {
        dest[offsets.aOffset + ii] = *cData;
        ++cData;
    }

    for (index_t ii = 0; ii < offsets.total.algSize; ii++) {
        dest[offsets.algOffset + ii] = *cData;
        ++cData;
    }

    for (index_t ii = 0; ii < offsets.total.diffSize; ii++) {
        dest[offsets.diffOffset + ii] = *cData;
        ++cData;
    }
}

void gridDynObjectGuessState(GridDynObject obj,
                             double time,
                             double* states,
                             double* dstate_dt,
                             SolverKey key,
                             GridDynError* err)
{
    gridComponent* comp = getComponentPointer(obj);

    if (comp == nullptr) {
        assignError(err, griddyn_error_invalid_object, invalidComponent);
        return;
    }
    if (states == nullptr) {
        static constexpr char emptyState[] = "given state buffer is null";
        assignError(err, griddyn_error_insufficient_space, emptyState);
        return;
    }
    auto* keyInfo = static_cast<solverKeyInfo*>(key);
    if (keyInfo == nullptr) {
        assignError(err, griddyn_error_invalid_object, invalidSolver);
        return;
    }
    if (keyInfo->stateBuffer.empty()) {
        setUpSolverKeyInfo(keyInfo, comp);
    }
    if (comp->checkFlag(dyn_initialized)) {
        if (dstate_dt == nullptr) {
            static constexpr char emptyState[] =
                "given dstate buffer is null for dynamic operation";
            assignError(err, griddyn_error_insufficient_space, emptyState);
            return;
        }
        comp->guessState(time,
                         keyInfo->stateBuffer.data(),
                         keyInfo->dstateBuffer.data(),
                         keyInfo->sMode_);
    } else {
        comp->guessState(time,
                         keyInfo->stateBuffer.data(),
                         keyInfo->dstateBuffer.data(),
                         keyInfo->sMode_);
    }
    TranslateToLocal(keyInfo->stateBuffer, states, comp, keyInfo->sMode_);
    TranslateToLocal(keyInfo->dstateBuffer, dstate_dt, comp, keyInfo->sMode_);
}

void gridDynObjectSetState(GridDynObject obj,
                           double time,
                           const double* states,
                           const double* dstate_dt,
                           SolverKey key,
                           GridDynError* err)
{
    gridComponent* comp = getComponentPointer(obj);

    if (comp == nullptr) {
        assignError(err, griddyn_error_invalid_object, invalidComponent);
        return;
    }
    auto* keyInfo = static_cast<solverKeyInfo*>(key);
    if (keyInfo == nullptr) {
        assignError(err, griddyn_error_invalid_object, invalidSolver);
        return;
    }
    if (keyInfo->stateBuffer.empty()) {
        setUpSolverKeyInfo(keyInfo, comp);
    }
    CopyFromLocal(keyInfo->stateBuffer, states, comp, keyInfo->sMode_);
    if (hasDifferential(keyInfo->sMode_)) {
        CopyFromLocal(keyInfo->dstateBuffer, dstate_dt, comp, keyInfo->sMode_);
    }
    comp->setState(time,
                   keyInfo->stateBuffer.data(),
                   keyInfo->dstateBuffer.data(),
                   keyInfo->sMode_);
}

void gridDynObjectGetStateVariableTypes(GridDynObject obj,
                                        double* types,
                                        SolverKey key,
                                        GridDynError* err)
{
    gridComponent* comp = getComponentPointer(obj);

    if (comp == nullptr) {
        assignError(err, griddyn_error_invalid_object, invalidComponent);
        return;
    }
    auto* keyInfo = static_cast<solverKeyInfo*>(key);
    if (keyInfo == nullptr) {
        assignError(err, griddyn_error_invalid_object, invalidSolver);
        return;
    }
    if (keyInfo->stateBuffer.empty()) {
        setUpSolverKeyInfo(keyInfo, comp);
    }
    comp->getVariableType(keyInfo->stateBuffer.data(), keyInfo->sMode_);
    TranslateToLocal(keyInfo->stateBuffer, types, comp, keyInfo->sMode_);
}

void gridDynObjectResidual(GridDynObject obj,
                           const double* inputs,
                           int inputSize,
                           double* resid,
                           SolverKey key,
                           GridDynError* err)
{
    gridComponent* comp = getComponentPointer(obj);

    if (comp == nullptr) {
        assignError(err, griddyn_error_invalid_object, invalidComponent);
        return;
    }
    auto* keyInfo = static_cast<solverKeyInfo*>(key);
    if (keyInfo == nullptr) {
        assignError(err, griddyn_error_invalid_object, invalidSolver);
        return;
    }
    if (keyInfo->stateBuffer.empty()) {
        setUpSolverKeyInfo(keyInfo, comp);
    }
    comp->residual(IOdata(inputs, inputs + inputSize),
                   emptyStateData,
                   keyInfo->stateBuffer.data(),
                   keyInfo->sMode_);
    TranslateToLocal(keyInfo->stateBuffer, resid, comp, keyInfo->sMode_);
}

void gridDynObjectDerivative(GridDynObject obj,
                             const double* inputs,
                             int inputSize,
                             double* deriv,
                             SolverKey key,
                             GridDynError* err)
{
    gridComponent* comp = getComponentPointer(obj);

    if (comp == nullptr) {
        assignError(err, griddyn_error_invalid_object, invalidComponent);
        return;
    }
    if (!comp->checkFlag(dyn_initialized)) {
        static constexpr char notInitialized[] =
            "the object has not been initialized for dynamic operations";
        assignError(err, griddyn_error_object_not_initialized, notInitialized);
        return;
    }
    auto* keyInfo = static_cast<solverKeyInfo*>(key);
    if (keyInfo == nullptr) {
        assignError(err, griddyn_error_invalid_object, invalidSolver);
        return;
    }
    if (keyInfo->stateBuffer.empty()) {
        setUpSolverKeyInfo(keyInfo, comp);
    }
    comp->derivative(IOdata(inputs, inputs + inputSize),
                     emptyStateData,
                     keyInfo->stateBuffer.data(),
                     keyInfo->sMode_);
    TranslateToLocal(keyInfo->stateBuffer, deriv, comp, keyInfo->sMode_);
}

void gridDynObjectAlgebraicUpdate(GridDynObject obj,
                                  const double* inputs,
                                  int inputSize,
                                  double* update,
                                  double alpha,
                                  SolverKey key,
                                  GridDynError* err)
{
    gridComponent* comp = getComponentPointer(obj);

    if (comp == nullptr) {
        assignError(err, griddyn_error_invalid_object, invalidComponent);
        return;
    }
    auto* keyInfo = static_cast<solverKeyInfo*>(key);
    if (keyInfo == nullptr) {
        assignError(err, griddyn_error_invalid_object, invalidSolver);
        return;
    }
    if (keyInfo->stateBuffer.empty()) {
        setUpSolverKeyInfo(keyInfo, comp);
    }
    comp->algebraicUpdate(IOdata(inputs, inputs + inputSize),
                          emptyStateData,
                          keyInfo->stateBuffer.data(),
                          keyInfo->sMode_,
                          alpha);
    TranslateToLocal(keyInfo->stateBuffer, update, comp, keyInfo->sMode_);
}

const IOlocs defInputlocs{kNullLocation,
                          kNullLocation,
                          kNullLocation,
                          kNullLocation,
                          kNullLocation,
                          kNullLocation,
                          kNullLocation,
                          kNullLocation,
                          kNullLocation,
                          kNullLocation,
                          kNullLocation};
void gridDynObjectJacobian(GridDynObject obj,
                           const double* inputs,
                           int inputSize,
                           double cj,
                           void (*insert)(int, int, double),
                           SolverKey key,
                           GridDynError* err)
{
    gridComponent* comp = getComponentPointer(obj);

    if (comp == nullptr) {
        assignError(err, griddyn_error_invalid_object, invalidComponent);
        return;
    }
    auto* keyInfo = static_cast<solverKeyInfo*>(key);
    if (keyInfo == nullptr) {
        assignError(err, griddyn_error_invalid_object, invalidSolver);
        return;
    }
    matrixDataCustomWriteOnly<double> md;
    md.setFunction([insert](index_t row, index_t col, double val) {
        insert(static_cast<int>(row), static_cast<int>(col), val);
    });
    stateData sD;
    sD.cj = cj;
    comp->jacobianElements(
        IOdata(inputs, inputs + inputSize), sD, md, defInputlocs, keyInfo->sMode_);
}

const IOlocs defInputlocs_act{0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10};

void gridDynObjecIoPartialDerivatives(GridDynObject obj,
                                      const double* inputs,
                                      int inputSize,
                                      void (*insert)(int, int, double),
                                      SolverKey key,
                                      GridDynError* err)
{
    gridComponent* comp = getComponentPointer(obj);

    if (comp == nullptr) {
        assignError(err, griddyn_error_invalid_object, invalidComponent);
        return;
    }
    auto* keyInfo = static_cast<solverKeyInfo*>(key);
    if (keyInfo == nullptr) {
        assignError(err, griddyn_error_invalid_object, invalidSolver);
        return;
    }
    matrixDataCustomWriteOnly<double> md;
    md.setFunction([insert](index_t row, index_t col, double val) {
        insert(static_cast<int>(row), static_cast<int>(col), val);
    });
    comp->ioPartialDerivatives(
        IOdata(inputs, inputs + inputSize), emptyStateData, md, defInputlocs, keyInfo->sMode_);
}

void gridDynObjectOutputPartialDerivatives(GridDynObject obj,
                                           const double* inputs,
                                           int inputSize,
                                           void (*insert)(int, int, double),
                                           SolverKey key,
                                           GridDynError* err)
{
    gridComponent* comp = getComponentPointer(obj);

    if (comp == nullptr) {
        assignError(err, griddyn_error_invalid_object, invalidComponent);
        return;
    }
    auto* keyInfo = static_cast<solverKeyInfo*>(key);
    if (keyInfo == nullptr) {
        assignError(err, griddyn_error_invalid_object, invalidSolver);
        return;
    }

    matrixDataCustomWriteOnly<double> md;
    md.setFunction([insert](index_t row, index_t col, double val) {
        insert(static_cast<int>(row), static_cast<int>(col), val);
    });
    comp->outputPartialDerivatives(IOdata(inputs, inputs + inputSize),
                                   emptyStateData,
                                   md,
                                   keyInfo->sMode_);
}
