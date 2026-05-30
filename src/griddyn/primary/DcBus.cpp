/*
 * Copyright (c) 2014-2026, Lawrence Livermore National Security
 * See the top-level NOTICE for additional details. All rights reserved.
 * SPDX-License-Identifier: BSD-3-Clause
 */

#include "DcBus.h"

#include "../Generator.h"
#include "../GridArea.h"
#include "../Link.h"
#include "../Load.h"
#include "core/CoreExceptions.h"
#include "core/CoreObjectTemplates.hpp"
#include "core/ObjectFactoryTemplates.hpp"
#include "gmlc/utilities/stringOps.h"
#include "gmlc/utilities/vectorOps.hpp"
#include "utilities/matrixDataCompact.hpp"
#include <iostream>
#include <string>

namespace griddyn {
static TypeFactory<DcBus> gBf("bus", std::to_array<std::string_view>({"dc", "hvdc"}));

using units::unit;

DcBus::DcBus(const std::string& objName): GridBus(objName), busController(this) {}

CoreObject* DcBus::clone(CoreObject* obj) const
{
    auto* nobj = cloneBase<DcBus, GridBus>(this, obj);
    if (nobj == nullptr) {
        return obj;
    }
    nobj->vTarget = vTarget;
    nobj->participation = participation;
    return nobj;
}

// add link
void DcBus::add(Link* lnk)
{
    if ((lnk->checkFlag(dc_only)) || (lnk->checkFlag(dc_capable))) {
        GridBus::add(lnk);
        return;
    }

    throw(UnrecognizedObjectException(this));
}

// dynInitializeB states
void DcBus::pFlowObjectInitializeA(coreTime time0, std::uint32_t flags)
{
    GridBus::pFlowObjectInitializeA(time0, flags);
}

void DcBus::pFlowObjectInitializeB()
{
    GridBus::pFlowObjectInitializeB();

    propogatePower();
}

StateSizes DcBus::localStateSizes(const SolverMode& sMode) const
{
    StateSizes busSS;
    if (hasAlgebraic(sMode)) {
        busSS.vSize = 1;

        // check for slave bus mode
        if (opFlags[slave_bus]) {
            busSS.vSize = 0;
        }

        if (isExtended(sMode))  // in extended state mode we have P and Q as states
        {
            if (isDC(sMode)) {
                busSS.algSize = 1;
            } else {
                busSS.algSize = 2;
            }
        }
    }
    return busSS;
}

count_t DcBus::localJacobianCount(const SolverMode& sMode) const
{
    count_t localJacSize = 0;
    if (hasAlgebraic(sMode)) {
        localJacSize = 1 + (2 * static_cast<count_t>(attachedLinks.size()));
        // check for slave bus mode
        if (opFlags[slave_bus]) {
            localJacSize -= 1;
        }
    }
    return localJacSize;
}

ChangeCode DcBus::powerFlowAdjust(const IOdata& /*inputs*/, std::uint32_t flags, CheckLevel level)
{
    auto out = ChangeCode::NO_CHANGE;
    // genP and genQ are defined negative for producing power so we flip the signs here
    S.genP = -S.genP;
    if (!CHECK_CONTROLFLAG(flags, ignore_bus_limits)) {
        switch (type) {
            case BusType::SLK:
            case BusType::afix:

                if (S.genP < busController.Pmin) {
                    S.genP = busController.Pmin;
                    if (attachedGens.size() == 1) {
                        attachedGens[0]->set("p", S.genP);
                    } else {
                        // TODO(phlpt): Figure out what to do here.
                        // for (auto &gen : attachedGens)
                        //  {
                        // gen->set ("p", gen->getGeneration);
                        //   }
                    }
                    type = BusType::PQ;
                    alert(this, JAC_COUNT_CHANGE);
                    out = ChangeCode::JACOBIAN_CHANGE;
                    if (prevType == BusType::SLK) {
                        alert(this, SLACK_BUS_CHANGE);
                    }
                } else if (S.genP > busController.Pmax) {
                    S.genP = busController.Pmax;
                    type = BusType::PQ;
                    if (attachedGens.size() == 1) {
                        attachedGens[0]->set("p", S.genP);
                    } else {
                        // TODO(phlpt): Figure out what to do in this case.
                        // for (auto &gen : attachedGens)
                        //  {
                        // gen->set ("p", gen->Pmax);
                        //  }
                    }
                    alert(this, JAC_COUNT_CHANGE);
                    out = ChangeCode::JACOBIAN_CHANGE;
                    if (prevType == BusType::SLK) {
                        alert(this, SLACK_BUS_CHANGE);
                    }
                }
                break;
            default:
                break;
        }
    }
    auto inputs = getOutputs(noInputs, emptyStateData, cLocalSolverMode);
    for (auto& gen : attachedGens) {
        if (gen->checkFlag(has_powerflow_adjustments)) {
            auto iret = gen->powerFlowAdjust(inputs, flags, level);
            out = std::max(iret, out);
        }
    }
    for (auto& load : attachedLoads) {
        if (load->checkFlag(has_powerflow_adjustments)) {
            auto iret = load->powerFlowAdjust(inputs, flags, level);
            out = std::max(iret, out);
        }
    }
    // genP and genQ are defined negative for producing power so we flip the signs here
    S.genP = -S.genP;
    return out;
}
/*function to check the current status for any limit violations*/
void DcBus::pFlowCheck(std::vector<Violation>& violationVector)
{
    GridBus::pFlowCheck(violationVector);
}

// dynInitializeB states for dynamic solution
void DcBus::dynObjectInitializeA(coreTime time0, std::uint32_t flags)
{
    GridBus::dynObjectInitializeA(time0, flags);
    return;
}

// dynInitializeB states for dynamic solution part 2  //final clean up
void DcBus::dynObjectInitializeB(const IOdata& inputs,
                                 const IOdata& desiredOutput,
                                 IOdata& fieldSet)
{
    GridBus::dynObjectInitializeB(inputs, desiredOutput, fieldSet);
    S.genQ = 0;
    angle = 0;
}

void DcBus::timestep(coreTime time, const IOdata& inputs, const SolverMode& sMode)
{
    GridBus::timestep(time, inputs, sMode);
}

// set properties
void DcBus::set(std::string_view param, std::string_view val)
{
    auto valLowerCase = gmlc::utilities::convertToLowerCase(val);
    if ((param == "type") || (param == "bustype") || (param == "pflowtype")) {
        if ((valLowerCase == "slk") || (valLowerCase == "swing") || (valLowerCase == "slack")) {
            type = BusType::SLK;
            prevType = BusType::SLK;
        } else if (valLowerCase == "pv") {
            type = BusType::PV;
            prevType = BusType::PV;
        } else if (valLowerCase == "pq") {
            type = BusType::PQ;
            prevType = BusType::PQ;
        } else if ((valLowerCase == "dynslk") || (valLowerCase == "inf") ||
                   (valLowerCase == "infinite")) {
            type = BusType::SLK;
            prevType = BusType::SLK;
            dynType = DynBusType::dynSLK;
        } else if ((valLowerCase == "fixedangle") || (valLowerCase == "fixangle") ||
                   (valLowerCase == "ref")) {
            dynType = DynBusType::fixAngle;
        } else if ((valLowerCase == "fixedvoltage") || (valLowerCase == "fixvoltage")) {
            dynType = DynBusType::fixVoltage;
        } else if (valLowerCase == "afix") {
            type = BusType::afix;
            prevType = BusType::afix;
        } else if (valLowerCase == "normal") {
            dynType = DynBusType::normal;
        } else {
            throw(InvalidParameterValue(val));
        }
    } else if (param == "dyntype") {
        if ((valLowerCase == "dynslk") || (valLowerCase == "inf") || (valLowerCase == "slk")) {
            dynType = DynBusType::dynSLK;
            type = BusType::SLK;
        } else if ((valLowerCase == "fixedangle") || (valLowerCase == "fixangle") ||
                   (valLowerCase == "ref")) {
            dynType = DynBusType::fixAngle;
        } else if ((valLowerCase == "fixedvoltage") || (valLowerCase == "fixvoltage")) {
            dynType = DynBusType::fixVoltage;
        } else if ((valLowerCase == "normal") || (valLowerCase == "pq")) {
            dynType = DynBusType::normal;
        } else {
            throw(InvalidParameterValue(val));
        }
    } else {
        GridBus::set(param, val);
    }
}

void DcBus::set(std::string_view param, double val, unit unitType)
{
    if (param.empty()) {
    } else {
        GridBus::set(param, val, unitType);
    }
}

void DcBus::getStateName(stringVec& stNames,
                         const SolverMode& sMode,
                         const std::string& prefix) const
{
    if (hasAlgebraic(sMode)) {
        auto voffset = offsets.getVOffset(sMode);

        count_t bst = 0;
        if (voffset != kNullLocation) {
            if (std::cmp_less_equal(stNames.size(), static_cast<stringVec::size_type>(voffset))) {
                stNames.resize(static_cast<stringVec::size_type>(voffset) + 1U);
            }
            stNames[voffset] = getName() + ":voltage";
            ++bst;
        }

        if (stateSize(sMode) == bst) {
            return;
        }
    }
    GridBus::getStateName(stNames, sMode, prefix);
}

// pass the solution
void DcBus::setState(coreTime time,
                     const double state[],
                     const double dstateDt[],
                     const SolverMode& sMode)
{
    auto voffset = offsets.getVOffset(sMode);

    if (isDAE(sMode)) {
        if (voffset != kNullLocation) {
            voltage = state[voffset];
            m_dstate_dt[voltageInLocation] = dstateDt[voffset];
        }
    } else if (hasAlgebraic(sMode)) {
        if (voffset != kNullLocation) {
            if (time > prevTime) {
                // m_dstate_dt[voltageInLocation] = (state[Voffset] - m_state[voltageInLocation]) /
                // (time - lastSetTime);
            }
            voltage = state[voffset];
        }
    }
    GridBus::setState(time, state, dstateDt, sMode);
}

void DcBus::guessState(coreTime time, double state[], double dstateDt[], const SolverMode& sMode)
{
    auto voffset = offsets.getVOffset(sMode);

    if (!opFlags[slave_bus]) {
        if (voffset != kNullLocation) {
            state[voffset] = voltage;

            if (hasDifferential(sMode)) {
                dstateDt[voffset] = 0.0;
            }
        }
    }
    GridBus::guessState(time, state, dstateDt, sMode);
}

// residual
void DcBus::residual(const IOdata& inputs,
                     const StateData& stateDataValue,
                     double resid[],
                     const SolverMode& sMode)
{
    GridBus::residual(inputs, stateDataValue, resid, sMode);
    auto voffset = offsets.getVOffset(sMode);
    // output

    if (voffset != kNullLocation) {
        if (useVoltage(sMode)) {
            resid[voffset] = S.sumP();
        } else {
            resid[voffset] = stateDataValue.state[voffset] - voltage;
        }
    }

    // printf("[%d] Bus %d V=%f theta=%f\n", seqID, id, v1,t1);
}

static const IOlocs IN_LOC{0, 1, 2};

void DcBus::computeDerivatives(const StateData& stateDataValue, const SolverMode& sMode)
{
    matrixDataCompact<2, 3> partDeriv;
    if (!isConnected()) {
        return;
    }
    partDeriv.clear();

    for (auto& link : attachedLinks) {
        if (link->isEnabled()) {
            link->updateLocalCache(noInputs, stateDataValue, sMode);
            link->ioPartialDerivatives(getID(), stateDataValue, partDeriv, IN_LOC, sMode);
        }
    }
    if (!isExtended(sMode)) {
        for (auto& gen : attachedGens) {
            if (gen->isConnected()) {
                gen->updateLocalCache(outputs, stateDataValue, sMode);
                gen->ioPartialDerivatives(outputs, stateDataValue, partDeriv, IN_LOC, sMode);
            }
        }
        for (auto& load : attachedLoads) {
            if (load->isConnected()) {
                load->updateLocalCache(outputs, stateDataValue, sMode);
                load->ioPartialDerivatives(outputs, stateDataValue, partDeriv, IN_LOC, sMode);
            }
        }
    }
    dVdP = partDeriv.at(PoutLocation, voltageInLocation);
}
// Jacobian
void DcBus::jacobianElements(const IOdata& /*inputs*/,
                             const StateData& stateDataValue,
                             matrixData<double>& matrixDataValue,
                             const IOlocs& /*inputLocs*/,
                             const SolverMode& sMode)
{
    auto inputs = getOutputs(noInputs, stateDataValue, sMode);

    // kinsolJacDense(state, J, ind, true);

    auto voffset = offsets.getVOffset(sMode);
    // import bus values (current theta and voltage)

    computeDerivatives(stateDataValue, sMode);
    auto inputLocs = getOutputLocs(sMode);

    // printf("t=%f,id=%d, dpdt=%f, dpdv=%f, dqdt=%f, dqdv=%f\n", time, id, Ptii, Pvii, Qvii, Qtii);
    if (voffset != kNullLocation) {
        if (useVoltage(sMode)) {
            matrixDataValue.assign(voffset, voffset, dVdP);
            inputLocs[voltageInLocation] = voffset;
        } else {
            matrixDataValue.assign(voffset, voffset, 1.0);
            inputLocs[voltageInLocation] = kNullLocation;
        }
    }

    // matrixDataSparse of;
    of.setArray(matrixDataValue);
    of.setTranslation(PoutLocation,
                      useVoltage(sMode) ? inputLocs[voltageInLocation] : kNullLocation);
    for (auto& gen : attachedGens) {
        if (gen->jacSize(sMode) > 0) {
            gen->jacobianElements(inputs, stateDataValue, matrixDataValue, inputLocs, sMode);
            if (gen->isConnected()) {
                gen->outputPartialDerivatives(inputs, stateDataValue, of, sMode);
            }
        }
    }
    for (auto& load : attachedLoads) {
        if (load->jacSize(sMode) > 0) {
            load->jacobianElements(inputs, stateDataValue, matrixDataValue, inputLocs, sMode);
            if (load->isConnected()) {
                load->outputPartialDerivatives(inputs, stateDataValue, of, sMode);
            }
        }
    }
    const id_type_t gid = getID();
    for (auto& link : attachedLinks) {
        link->outputPartialDerivatives(gid, stateDataValue, of, sMode);
    }
    /*if (inputLocs[voltageInLocation] != kNullLocation)
      {
        if (useVoltage (sMode))
          {
            md.copyTranslateRow (&of, PoutLocation, inputLocs[voltageInLocation]);
          }
      }
          */
}

IOlocs DcBus::getOutputLocs(const SolverMode& sMode) const
{
    return {useVoltage(sMode) ? offsets.getVOffset(sMode) : kNullLocation,
            kNullLocation,
            kNullLocation};
}

index_t DcBus::getOutputLoc(const SolverMode& sMode, index_t num) const
{
    if (num == voltageInLocation) {
        return useVoltage(sMode) ? offsets.getVOffset(sMode) : kNullLocation;
    }
    return kNullLocation;
}

// TODO(phlpt): Write this function.
void DcBus::converge(coreTime /*time*/,
                     double /*state*/[],
                     double /*dstate_dt*/[],
                     const SolverMode& /*sMode*/,
                     ConvergeMode /*mode*/,
                     double /*tol*/)
// void DcBus::converge (const coreTime time, double state[], double dstate_dt[], const SolverMode
// &sMode, double tol, int mode)
{
}

int DcBus::getMode(const SolverMode& sMode) const
{
    if (isDynamic(sMode)) {
        if (isDifferentialOnly(sMode)) {
            return 3;
        }
        return static_cast<int>(static_cast<unsigned int>(dynType) | 1U);
    }
    return static_cast<int>(static_cast<unsigned int>(type) | 1U);
}

double DcBus::getVoltage(const double state[], const SolverMode& sMode) const
{
    if (isLocal(sMode)) {
        return voltage;
    }
    if (useVoltage(sMode)) {
        auto voffset = offsets.getVOffset(sMode);
        return (voffset != kNullLocation) ? state[voffset] : voltage;
    }
    return voltage;
}

double DcBus::getVoltage(const StateData& stateDataValue, const SolverMode& sMode) const
{
    if (isLocal(sMode)) {
        return voltage;
    }
    if (useVoltage(sMode)) {
        auto voffset = offsets.getVOffset(sMode);
        return (voffset != kNullLocation) ? stateDataValue.state[voffset] : voltage;
    }
    return voltage;
}

bool DcBus::useVoltage(const SolverMode& sMode) const
{
    bool ret = true;
    if (isDifferentialOnly(sMode)) {
        ret = false;
    } else if (isDynamic(sMode)) {
        if ((dynType == DynBusType::fixVoltage) || (dynType == DynBusType::dynSLK)) {
            ret = false;
        }
    } else {
        if ((type == BusType::PV) || (type == BusType::SLK)) {
            ret = false;
        }
    }

    return ret;
}

int DcBus::propogatePower(bool makeSlack)
{
    int ret = 0;
    if (makeSlack) {
        prevType = type;
        type = BusType::SLK;
    }

    computePowerAdjustments();
    int unfixed = 0;
    Link* dc1 = nullptr;
    for (auto& lnk : attachedLinks) {
        if (!(lnk->checkFlag(Link::FIXED_TARGET_POWER))) {
            ++unfixed;
            dc1 = lnk;
        }
    }
    if (unfixed == 1) {
        ret = dc1->fixRealPower(dc1->getRealPower(getID()) - (S.sumP()), getID());
    }
    return ret;
}

void DcBus::computePowerAdjustments()
{
    // declaring an embedded function
    auto cid = getID();

    S.reset();

    for (auto& link : attachedLinks) {
        if ((link->isConnected()) && (!busController.hasAdjustments(link->getID()))) {
            S.linkP += link->getRealPower(cid);
        }
    }
    for (auto& load : attachedLoads) {
        if ((load->isConnected()) && (!busController.hasAdjustments(load->getID()))) {
            S.loadP += load->getRealPower(voltage);
        }
    }
    for (auto& gen : attachedGens) {
        if ((gen->isConnected()) && (!busController.hasAdjustments(gen->getID()))) {
            S.genP += gen->getRealPower();
        }
    }
}

}  // namespace griddyn
