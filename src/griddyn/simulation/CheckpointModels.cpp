/*
 * Copyright (c) 2026, Lawrence Livermore National Security
 * See the top-level NOTICE for additional details. All rights reserved.
 * SPDX-License-Identifier: BSD-3-Clause
 */

#include "../GenModel.h"
#include "../Generator.h"
#include "../GridArea.h"
#include "../GridBus.h"
#include "../GridComponent.h"
#include "../GridDynSimulation.h"
#include "../GridPrimary.h"
#include "../GridSecondary.h"
#include "../GridSubModel.h"
#include "../Link.h"
#include "../Load.h"
#include "../generators/DynamicGenerator.h"
#include "../links/AcLine.h"
#include "../links/AdjustableTransformer.h"
#include "../loads/RampLoad.h"
#include "../loads/ZipLoad.h"
#include "../primary/AcBus.h"
#include "GridSimulation.h"
#include "core/CheckpointArchive.h"
#include <string>

namespace griddyn {
void GridComponent::checkpointFields(CheckpointArchive& archive)
{
    CoreObject::checkpointFields(archive);
    archive.model<GridComponent>("GridComponent");
    archive.state("opFlags", opFlags);
    archive.fixedState("m_state", m_state);
    archive.fixedState("m_dstate_dt", m_dstate_dt);
    archive.parameter("systemBaseFrequency", systemBaseFrequency);
    archive.parameter("systemBasePower", systemBasePower);
    archive.parameter("localBaseVoltage", localBaseVoltage);
}

void GridPrimary::checkpointFields(CheckpointArchive& archive)
{
    GridComponent::checkpointFields(archive);
    archive.model<GridPrimary>("GridPrimary");
    archive.parameter("zone", zone);
}

void GridSecondary::checkpointFields(CheckpointArchive& archive)
{
    GridComponent::checkpointFields(archive);
    archive.model<GridSecondary>("GridSecondary");
}

void GridArea::checkpointFields(CheckpointArchive& archive)
{
    GridPrimary::checkpointFields(archive);
    archive.model<GridArea>("GridArea");
    archive.require(objectHolder.empty(), "auxiliary area objects");
    archive.require(pendingSwitchedShuntAdjustment == nullptr, "a pending switched-shunt action");
    archive.require(agcController == nullptr, "sampled area controllers");
    archive.require(interAreaTransfers.empty(), "inter-area transfer schedules");
    archive.state("switchedShuntAdjustmentClaimed", switchedShuntAdjustmentClaimed);
    archive.state("powerFlowAdjustmentsThisPass", powerFlowAdjustmentsThisPass);
    archive.state("masterBus", masterBus);
    archive.references("rootObjects", rootObjects);
    archive.references("pFlowAdjustObjects", pFlowAdjustObjects);
    archive.parameter("fTarget", fTarget);
    archive.parameter("maxPowerFlowAdjustmentsPerPass", maxPowerFlowAdjustmentsPerPass);
    archive.parameter("scheduledNetInterchangeMW", scheduledNetInterchangeMW);
    archive.parameter("interchangeToleranceMW", interchangeToleranceMW);
    archive.parameter("interchangeSlackBusUserID", interchangeSlackBusUserID);
    archive.parameter("interchangeSlackBusName", interchangeSlackBusName);
    archive.onRestore([this]() { interchangeSlackBus = nullptr; });
}

void GridBus::checkpointFields(CheckpointArchive& archive)
{
    GridPrimary::checkpointFields(archive);
    archive.model<GridBus>("GridBus");
    archive.state("type", type);
    archive.state("dynType", dynType);
    archive.state("voltage", voltage);
    archive.state("angle", angle);
    archive.state("freq", freq);
    archive.fixedState("outputs", outputs);
    archive.state("lowVtime", lowVtime);
    archive.state("Network", Network);
    archive.parameter("localBaseVoltage", localBaseVoltage);
    archive.parameter("Vtol", Vtol);
    archive.parameter("Atol", Atol);
}

void AcBus::checkpointFields(CheckpointArchive& archive)
{
    GridBus::checkpointFields(archive);
    archive.model<AcBus>("AcBus");
    archive.require(!fblock, "bus frequency-estimator state");
    archive.require(busController.slaveBusses.empty() && busController.masterBus == nullptr &&
                        busController.directBus == nullptr,
                    "merged buses");
    archive.state("oCount", oCount);
    archive.state("prevType", prevType);
    archive.state("prevDynType", prevDynType);
    archive.state("tieError", tieError);
    archive.state("prevPower", prevPower);
    archive.state("lastSetTime", lastSetTime);
    archive.state("lastAcceptedVoltage", lastAcceptedVoltage);
    archive.state("lastAcceptedAngle", lastAcceptedAngle);
    archive.state("pFlowInitialVoltage", pFlowInitialVoltage);
    archive.state("pFlowInitialVoltageCaptured", pFlowInitialVoltageCaptured);
    archive.state("ignoreReactivePowerLimits", ignoreReactivePowerLimits);
    archive.parameter("aTarget", aTarget);
    archive.parameter("vTarget", vTarget);
    archive.parameter("participation", participation);
    archive.parameter("refAngle", refAngle);
    archive.parameter("Vmin", Vmin);
    archive.parameter("Vmax", Vmax);
    archive.parameter("Tw", Tw);
    archive.parameter("frequencyFilterTf", frequencyFilterTf);
    archive.parameter("frequencyFilterTw", frequencyFilterTw);
    archive.parameter("frequencyFilterFn", frequencyFilterFn);
    archive.parameter("frequencyFilterConfigured", frequencyFilterConfigured);
    archive.state("control.Qmin", busController.Qmin);
    archive.state("control.Qmax", busController.Qmax);
    archive.state("control.Pmin", busController.Pmin);
    archive.state("control.Pmax", busController.Pmax);
    archive.state("control.autogenPact", busController.autogenPact);
    archive.state("control.autogenQact", busController.autogenQact);
    archive.state("control.vcfrac", busController.vcfrac);
    archive.state("control.vclinkFrac", busController.vclinkFrac);
    archive.state("control.pcfrac", busController.pcfrac);
    archive.state("control.pclinkFrac", busController.pclinkFrac);
    archive.references("control.vControlObjects", busController.vControlObjects);
    archive.references("control.proxyVControlObject", busController.proxyVControlObject);
    archive.references("control.vControlLinks", busController.vControlLinks);
    archive.references("control.pControlObjects", busController.pControlObjects);
    archive.references("control.proxyPControlObject", busController.proxyPControlObject);
    archive.references("control.pControlLinks", busController.pControlLinks);
    archive.parameter("control.autogenP", busController.autogenP);
    archive.parameter("control.autogenQ", busController.autogenQ);
    archive.parameter("control.autogenDelay", busController.autogenDelay);
    archive.parameter("control.vcfracInput", busController.vcfracInput);
    archive.parameter("control.vclinkFracInput", busController.vclinkFracInput);
    archive.onRestore([this]() { lastSmode = kInvalidLocation; });
}

void GridLoad::checkpointFields(CheckpointArchive& archive)
{
    GridSecondary::checkpointFields(archive);
    archive.model<GridLoad>("GridLoad");
    archive.state("P", P);
    archive.state("Q", Q);
    archive.parameter("P", P);
    archive.parameter("Q", Q);
    archive.parameter("pfq", pfq);
    archive.parameter("fixedShunt", fixedShunt);
}

void ZipLoad::checkpointFields(CheckpointArchive& archive)
{
    GridLoad::checkpointFields(archive);
    archive.model<ZipLoad>("ZipLoad");
    archive.state("Pout", Pout);
    archive.state("Qout", Qout);
    archive.state("lastTime", lastTime);
    archive.state("Ip", Ip);
    archive.state("Iq", Iq);
    archive.state("Yp", Yp);
    archive.state("Yq", Yq);
    archive.parameter("Ip", Ip);
    archive.parameter("Iq", Iq);
    archive.parameter("Yp", Yp);
    archive.parameter("Yq", Yq);
    archive.parameter("Vpqmin", Vpqmin);
    archive.parameter("Vpqmax", Vpqmax);
    archive.onRestore([this]() {
        trigVVlow = 1.0 / (Vpqmin * Vpqmin);
        trigVVhigh = 1.0 / (Vpqmax * Vpqmax);
    });
}

void loads::RampLoad::checkpointFields(CheckpointArchive& archive)
{
    ZipLoad::checkpointFields(archive);
    archive.model<loads::RampLoad>("loads::RampLoad");
    archive.parameter("dPdt", dPdt);
    archive.parameter("dQdt", dQdt);
    archive.parameter("drdt", drdt);
    archive.parameter("dxdt", dxdt);
    archive.parameter("dIpdt", dIpdt);
    archive.parameter("dIqdt", dIqdt);
    archive.parameter("dYpdt", dYpdt);
    archive.parameter("dYqdt", dYqdt);
}

void Generator::checkpointFields(CheckpointArchive& archive)
{
    GridSecondary::checkpointFields(archive);
    archive.model<Generator>("Generator");
    archive.require(sched == nullptr, "generator schedulers");
    archive.require(bounds == nullptr, "generator capability curves");
    archive.state("P", P);
    archive.state("Q", Q);
    archive.state("ignoreReactivePowerLimits", ignoreReactivePowerLimits);
    archive.parameter("Pset", Pset);
    archive.parameter("dPdt", dPdt);
    archive.parameter("dQdt", dQdt);
    archive.parameter("Qmax", Qmax);
    archive.parameter("Qmin", Qmin);
    archive.parameter("Qbias", Qbias);
    archive.parameter("Pmax", Pmax);
    archive.parameter("Pmin", Pmin);
    archive.parameter("participation", participation);
    archive.parameter("rampAgc", rampAgc);
    archive.parameter("ramp10", ramp10);
    archive.parameter("ramp30", ramp30);
    archive.parameter("rampQ", rampQ);
    archive.parameter("vRegFraction", vRegFraction);
    archive.parameter("machineBasePower", machineBasePower);
    archive.parameter("m_Vtarget", m_Vtarget);
    archive.parameter("m_Rs", m_Rs);
    archive.parameter("m_Xs", m_Xs);
}

void Link::checkpointFields(CheckpointArchive& archive)
{
    GridPrimary::checkpointFields(archive);
    archive.model<Link>("Link");
    archive.parameter("circuitNum", circuitNum);
    archive.parameter("ratingA", ratingA);
    archive.parameter("ratingB", ratingB);
    archive.parameter("Erating", Erating);
    archive.parameter("Pset", Pset);
    archive.parameter("lossFraction", lossFraction);
}

void AcLine::checkpointFields(CheckpointArchive& archive)
{
    Link::checkpointFields(archive);
    archive.model<AcLine>("AcLine");
    archive.state("tap", tap);
    archive.state("tapAngle", tapAngle);
    archive.state("fault", fault);
    archive.parameter("mp_B1", mp_B1);
    archive.parameter("mp_G1", mp_G1);
    archive.parameter("mp_B2", mp_B2);
    archive.parameter("mp_G2", mp_G2);
    archive.parameter("fault", fault);
    archive.parameter("tap", tap);
    archive.parameter("tapAngle", tapAngle);
    archive.parameter("minAngle", minAngle);
    archive.parameter("maxAngle", maxAngle);
    archive.parameter("length", length);
    archive.parameter("r", r);
    archive.parameter("x", x);
    archive.onRestore([this]() {
        const double admittanceSquared = 1.0 / (r * r + x * x);
        g = r * admittanceSquared;
        b = -x * admittanceSquared;
        updateLocalCache();
    });
}

void GridSubModel::checkpointFields(CheckpointArchive& archive)
{
    GridComponent::checkpointFields(archive);
    archive.model<GridSubModel>("GridSubModel");
    archive.state("m_output", m_output);
}

void GenModel::checkpointFields(CheckpointArchive& archive)
{
    GridSubModel::checkpointFields(archive);
    archive.model<GenModel>("GenModel");
    archive.parameter("machineBasePower", machineBasePower);
    archive.parameter("Xd", Xd);
    archive.parameter("Rs", Rs);
    archive.reference("bus", bus);
}

void DynamicGenerator::checkpointFields(CheckpointArchive& archive)
{
    Generator::checkpointFields(archive);
    archive.model<DynamicGenerator>("DynamicGenerator");
    archive.require(ext == nullptr && gov == nullptr && pss == nullptr && pSetControl == nullptr &&
                        vSetControl == nullptr && isoc == nullptr &&
                        voltageCompensator == nullptr && oel == nullptr && uel == nullptr &&
                        !mechanicalPowerSourceExplicit,
                    "dynamic generator controllers in the power-flow profile");
    archive.state("m_Eft", m_Eft);
    archive.state("m_Pmech", m_Pmech);
    archive.state("activePowerCommandUnclamped", activePowerCommandUnclamped);
    archive.state("mechanicalPowerWasInvalid", mechanicalPowerWasInvalid);
}

void links::AdjustableTransformer::checkpointFields(CheckpointArchive& archive)
{
    AcLine::checkpointFields(archive);
    archive.model<links::AdjustableTransformer>("links::AdjustableTransformer");
    archive.require(!opFlags[USE_LOOKUP_TABLE], "transformer impedance lookup tables");
    archive.state("tap0", tap0);
    archive.state("tapAngle0", tapAngle0);
    archive.state("adjCount", adjCount);
    archive.state("oCount", oCount);
    archive.state("prevAdjust", prevAdjust);
    archive.state("prevValue", prevValue);
    archive.state("direction", direction);
    archive.parameter("cMode", cMode);
    archive.parameter("stepSize", stepSize);
    archive.parameter("maxTapAngle", maxTapAngle);
    archive.parameter("minTapAngle", minTapAngle);
    archive.parameter("minTap", minTap);
    archive.parameter("maxTap", maxTap);
    archive.parameter("Vtarget", Vtarget);
    archive.parameter("Vmax", Vmax);
    archive.parameter("Vmin", Vmin);
    archive.parameter("Ptarget", Ptarget);
    archive.parameter("Pmin", Pmin);
    archive.parameter("Pmax", Pmax);
    archive.parameter("Qtarget", Qtarget);
    archive.parameter("Qmax", Qmax);
    archive.parameter("Qmin", Qmin);
    archive.parameter("direction", direction);
    archive.parameter("tapMaxChangeRate", tapMaxChangeRate);
    archive.parameter("sample_rate", sample_rate);
    archive.parameter("stepDelay", stepDelay);
    archive.parameter("mp_Tm", mp_Tm);
    archive.parameter("dTapdt", dTapdt);
    archive.parameter("dTapAdt", dTapAdt);
    archive.parameter("directionExplicit", directionExplicit);
    archive.parameter("controlNum", controlNum);
    archive.parameter("controlName", controlName);
    archive.reference("controlBus", controlBus);
}

void GridSimulation::checkpointFields(CheckpointArchive& archive)
{
    GridArea::checkpointFields(archive);
    archive.model<GridSimulation>("GridSimulation");
    archive.require(stateRecorder == nullptr, "periodic state-output callbacks");
    archive.state("pState", pState);
    archive.state("errorCode", errorCode);
    archive.state("currentTime", currentTime);
    archive.state("simulationTime", simulationTime);
    archive.state("timeReturn", timeReturn);
    archive.state("nextStopTime", nextStopTime);
    archive.state("nextRecordTime", nextRecordTime);
    archive.state("lastStateRecordTime", lastStateRecordTime);
    archive.parameter("startTime", startTime);
    archive.parameter("stopTime", stopTime);
    archive.parameter("stepTime", stepTime);
    archive.parameter("minUpdateTime", minUpdateTime);
    archive.parameter("maxUpdateTime", maxUpdateTime);
    archive.parameter("absTime", absTime);
    archive.parameter("recordStart", recordStart);
    archive.parameter("recordStop", recordStop);
    archive.parameter("state_record_period", state_record_period);
}

void GridDynSimulation::checkpointFields(CheckpointArchive& archive)
{
    GridSimulation::checkpointFields(archive);
    archive.model<GridDynSimulation>("GridDynSimulation");
    archive.state("haltCount", haltCount);
    archive.state("residCount", residCount);
    archive.state("evalCount", evalCount);
    archive.state("partitionedAlgebraicCallCount", partitionedAlgebraicCallCount);
    archive.state("JacobianCallCount", JacobianCallCount);
    archive.state("rootCount", rootCount);
    archive.state("controlFlags", controlFlags);
    archive.state("busCount", busCount);
    archive.state("linkCount", linkCount);
    archive.state("networkCount", networkCount);
    archive.references("slkBusses", slkBusses);
    archive.references("singleStepObjects", singleStepObjects);
    archive.parameter("controlFlags", controlFlags);
    archive.parameter("max_Vadjust_iterations", max_Vadjust_iterations);
    archive.parameter("max_Padjust_iterations", max_Padjust_iterations);
    archive.parameter("probeStepTime", probeStepTime);
    archive.parameter("powerAdjustThreshold", powerAdjustThreshold);
    archive.parameter("powerFlowStartTime", powerFlowStartTime);
    archive.parameter("tolerance.voltage", tols.voltageTolerance);
    archive.parameter("tolerance.angle", tols.angleTolerance);
    archive.parameter("tolerance.default", tols.defaultTolerance);
    archive.parameter("tolerance.relaxation", tols.toleranceRelaxationFactor);
    archive.parameter("tolerance.relative", tols.rtol);
    archive.parameter("tolerance.time", tols.timeTol);
}

}  // namespace griddyn
