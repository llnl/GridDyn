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
static TypeFactory<DcBus> gbf("bus", std::to_array<std::string_view>({"dc", "hvdc"}));

using units::convert;
using units::unit;

DcBus::DcBus(const std::string& objName): GridBus(objName), busController(this) {}

CoreObject* DcBus::clone(CoreObject* obj) const
{
    auto nobj = cloneBase<DcBus, GridBus>(this, obj);
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
        return GridBus::add(lnk);
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

stateSizes DcBus::localStateSizes(const solverMode& sMode) const
{
    stateSizes busSS;
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

count_t DcBus::localJacobianCount(const solverMode& sMode) const
{
    count_t localJacSize = 0;
    if (hasAlgebraic(sMode)) {
        localJacSize = 1 + 2 * static_cast<count_t>(attachedLinks.size());
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
            if (iret > out) {
                out = iret;
            }
        }
    }
    for (auto& ld : attachedLoads) {
        if (ld->checkFlag(has_powerflow_adjustments)) {
            auto iret = ld->powerFlowAdjust(inputs, flags, level);
            if (iret > out) {
                out = iret;
            }
        }
    }
    // genP and genQ are defined negative for producing power so we flip the signs here
    S.genP = -S.genP;
    return out;
}
/*function to check the current status for any limit violations*/
void DcBus::pFlowCheck(std::vector<Violation>& Violation_vector)
{
    GridBus::pFlowCheck(Violation_vector);
}

// dynInitializeB states for dynamic solution
void DcBus::dynObjectInitializeA(coreTime time0, std::uint32_t flags)
{
    return GridBus::dynObjectInitializeA(time0, flags);
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

void DcBus::timestep(coreTime time, const IOdata& inputs, const solverMode& sMode)
{
    GridBus::timestep(time, inputs, sMode);
}

// set properties
void DcBus::set(std::string_view param, std::string_view val)
{
    auto val_lowerCase = gmlc::utilities::convertToLowerCase(val);
    if ((param == "type") || (param == "bustype") || (param == "pflowtype")) {
        if ((val_lowerCase == "slk") || (val_lowerCase == "swing") || (val_lowerCase == "slack")) {
            type = BusType::SLK;
            prevType = BusType::SLK;
        } else if (val_lowerCase == "pv") {
            type = BusType::PV;
            prevType = BusType::PV;
        } else if (val_lowerCase == "pq") {
            type = BusType::PQ;
            prevType = BusType::PQ;
        } else if ((val_lowerCase == "dynslk") || (val_lowerCase == "inf") ||
                   (val_lowerCase == "infinite")) {
            type = BusType::SLK;
            prevType = BusType::SLK;
            dynType = DynBusType::dynSLK;
        } else if ((val_lowerCase == "fixedangle") || (val_lowerCase == "fixangle") ||
                   (val_lowerCase == "ref")) {
            dynType = DynBusType::fixAngle;
        } else if ((val_lowerCase == "fixedvoltage") || (val_lowerCase == "fixvoltage")) {
            dynType = DynBusType::fixVoltage;
        } else if (val_lowerCase == "afix") {
            type = BusType::afix;
            prevType = BusType::afix;
        } else if (val_lowerCase == "normal") {
            dynType = DynBusType::normal;
        } else {
            throw(InvalidParameterValue(val));
        }
    } else if (param == "dyntype") {
        if ((val_lowerCase == "dynslk") || (val_lowerCase == "inf") || (val_lowerCase == "slk")) {
            dynType = DynBusType::dynSLK;
            type = BusType::SLK;
        } else if ((val_lowerCase == "fixedangle") || (val_lowerCase == "fixangle") ||
                   (val_lowerCase == "ref")) {
            dynType = DynBusType::fixAngle;
        } else if ((val_lowerCase == "fixedvoltage") || (val_lowerCase == "fixvoltage")) {
            dynType = DynBusType::fixVoltage;
        } else if ((val_lowerCase == "normal") || (val_lowerCase == "pq")) {
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
                         const solverMode& sMode,
                         const std::string& prefix) const
{
    if (hasAlgebraic(sMode)) {
        auto Voffset = offsets.getVOffset(sMode);

        count_t bst = 0;
        if (Voffset != kNullLocation) {
            if (static_cast<index_t>(stNames.size()) <= Voffset) {
                stNames.resize(static_cast<stringVec::size_type>(Voffset) + 1U);
            }
            stNames[Voffset] = getName() + ":voltage";
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
                     const double dstate_dt[],
                     const solverMode& sMode)
{
    auto Voffset = offsets.getVOffset(sMode);

    if (isDAE(sMode)) {
        if (Voffset != kNullLocation) {
            voltage = state[Voffset];
            m_dstate_dt[voltageInLocation] = dstate_dt[Voffset];
        }
    } else if (hasAlgebraic(sMode)) {
        if (Voffset != kNullLocation) {
            if (time > prevTime) {
                // m_dstate_dt[voltageInLocation] = (state[Voffset] - m_state[voltageInLocation]) /
                // (time - lastSetTime);
            }
            voltage = state[Voffset];
        }
    }
    GridBus::setState(time, state, dstate_dt, sMode);
}

void DcBus::guessState(coreTime time, double state[], double dstate_dt[], const solverMode& sMode)
{
    auto Voffset = offsets.getVOffset(sMode);

    if (!opFlags[slave_bus]) {
        if (Voffset != kNullLocation) {
            state[Voffset] = voltage;

            if (hasDifferential(sMode)) {
                dstate_dt[Voffset] = 0.0;
            }
        }
    }
    GridBus::guessState(time, state, dstate_dt, sMode);
}

// residual
void DcBus::residual(const IOdata& inputs,
                     const stateData& sD,
                     double resid[],
                     const solverMode& sMode)
{
    GridBus::residual(inputs, sD, resid, sMode);
    auto Voffset = offsets.getVOffset(sMode);
    // output

    if (Voffset != kNullLocation) {
        if (useVoltage(sMode)) {
            resid[Voffset] = S.sumP();
        } else {
            resid[Voffset] = sD.state[Voffset] - voltage;
        }
    }

    // printf("[%d] Bus %d V=%f theta=%f\n", seqID, id, v1,t1);
}

static const IOlocs inLoc{0, 1, 2};

void DcBus::computeDerivatives(const stateData& sD, const solverMode& sMode)
{
    matrixDataCompact<2, 3> partDeriv;
    if (!isConnected()) {
        return;
    }
    partDeriv.clear();

    for (auto& link : attachedLinks) {
        if (link->isEnabled()) {
            link->updateLocalCache(noInputs, sD, sMode);
            link->ioPartialDerivatives(getID(), sD, partDeriv, inLoc, sMode);
        }
    }
    if (!isExtended(sMode)) {
        for (auto& gen : attachedGens) {
            if (gen->isConnected()) {
                gen->updateLocalCache(outputs, sD, sMode);
                gen->ioPartialDerivatives(outputs, sD, partDeriv, inLoc, sMode);
            }
        }
        for (auto& load : attachedLoads) {
            if (load->isConnected()) {
                load->updateLocalCache(outputs, sD, sMode);
                load->ioPartialDerivatives(outputs, sD, partDeriv, inLoc, sMode);
            }
        }
    }
    dVdP = partDeriv.at(PoutLocation, voltageInLocation);
}
// Jacobian
void DcBus::jacobianElements(const IOdata& /*inputs*/,
                             const stateData& sD,
                             matrixData<double>& md,
                             const IOlocs& /*inputLocs*/,
                             const solverMode& sMode)
{
    auto inputs = getOutputs(noInputs, sD, sMode);

    // kinsolJacDense(state, J, ind, true);

    auto Voffset = offsets.getVOffset(sMode);
    // import bus values (current theta and voltage)

    computeDerivatives(sD, sMode);
    auto inputLocs = getOutputLocs(sMode);

    // printf("t=%f,id=%d, dpdt=%f, dpdv=%f, dqdt=%f, dqdv=%f\n", time, id, Ptii, Pvii, Qvii, Qtii);
    if (Voffset != kNullLocation) {
        if (useVoltage(sMode)) {
            md.assign(Voffset, Voffset, dVdP);
            inputLocs[voltageInLocation] = Voffset;
        } else {
            md.assign(Voffset, Voffset, 1.0);
            inputLocs[voltageInLocation] = kNullLocation;
        }
    }

    // matrixDataSparse of;
    of.setArray(md);
    of.setTranslation(PoutLocation,
                      useVoltage(sMode) ? inputLocs[voltageInLocation] : kNullLocation);
    for (auto& gen : attachedGens) {
        if (gen->jacSize(sMode) > 0) {
            gen->jacobianElements(inputs, sD, md, inputLocs, sMode);
            if (gen->isConnected()) {
                gen->outputPartialDerivatives(inputs, sD, of, sMode);
            }
        }
    }
    for (auto& load : attachedLoads) {
        if (load->jacSize(sMode) > 0) {
            load->jacobianElements(inputs, sD, md, inputLocs, sMode);
            if (load->isConnected()) {
                load->outputPartialDerivatives(inputs, sD, of, sMode);
            }
        }
    }
    id_type_t gid = getID();
    for (auto& link : attachedLinks) {
        link->outputPartialDerivatives(gid, sD, of, sMode);
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

IOlocs DcBus::getOutputLocs(const solverMode& sMode) const
{
    return {useVoltage(sMode) ? offsets.getVOffset(sMode) : kNullLocation,
            kNullLocation,
            kNullLocation};
}

index_t DcBus::getOutputLoc(const solverMode& sMode, index_t num) const
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
                     const solverMode& /*sMode*/,
                     ConvergeMode /*mode*/,
                     double /*tol*/)
// void DcBus::converge (const coreTime time, double state[], double dstate_dt[], const solverMode
// &sMode, double tol, int mode)
{
}

int DcBus::getMode(const solverMode& sMode) const
{
    if (isDynamic(sMode)) {
        if (isDifferentialOnly(sMode)) {
            return 3;
        }
        return (static_cast<int>(dynType) | 1);
    }
    return (static_cast<int>(type) | 1);
}

double DcBus::getVoltage(const double state[], const solverMode& sMode) const
{
    if (isLocal(sMode)) {
        return voltage;
    }
    if (useVoltage(sMode)) {
        auto Voffset = offsets.getVOffset(sMode);
        return (Voffset != kNullLocation) ? state[Voffset] : voltage;
    }
    return voltage;
}

double DcBus::getVoltage(const stateData& sD, const solverMode& sMode) const
{
    if (isLocal(sMode)) {
        return voltage;
    }
    if (useVoltage(sMode)) {
        auto Voffset = offsets.getVOffset(sMode);
        return (Voffset != kNullLocation) ? sD.state[Voffset] : voltage;
    }
    return voltage;
}

bool DcBus::useVoltage(const solverMode& sMode) const
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
        if (!(lnk->checkFlag(Link::fixed_target_power))) {
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
