/*
 * Copyright (c) 2014-2026, Lawrence Livermore National Security
 * See the top-level NOTICE for additional details. All rights reserved.
 * SPDX-License-Identifier: BSD-3-Clause
 */

#include "../GridArea.h"
#include "../GridBus.h"
#include "../measurement/ObjectGrabbers.h"
#include "../simulation/Contingency.h"
#include "AcLine.h"
#include "AcDcConverter.h"
#include "AdjustableTransformer.h"
#include "DcLink.h"
#include "core/CoreExceptions.h"
#include "core/CoreObjectTemplates.hpp"
#include "core/ObjectFactoryTemplates.hpp"
#include "core/ObjectInterpreter.h"
#include "gmlc/utilities/stringOps.h"
#include "gmlc/utilities/vectorOps.hpp"
#include "utilities/matrixDataCompact.hpp"
#include <algorithm>
#include <cmath>
#include <complex>
#include <cstdio>
#include <print>
#include <queue>
#include <string>
#include <vector>

namespace griddyn {
using units::convert;
using units::puMW;
using units::unit;

// make the object factory types

static TypeFactory<Link> gBlf("link",
                              std::to_array<std::string_view>({"trivial", "basic", "transport"}));

static ChildTypeFactory<AcLine, Link> gLf(
    "link",
    std::to_array<std::string_view>({"ac", "line", "phaseshifter", "phase_shifter", "transformer"}),
    "ac");

namespace links {
    static ChildTypeFactory<AdjustableTransformer, Link>
        gFad("link",
             std::to_array<std::string_view>({"adjust", "adjustable", "adjustabletransformer"}));

    static ChildTypeFactory<DcLink, Link>
        gDclnk("link", std::to_array<std::string_view>({"dc", "dclink", "dcline"}));

    static TypeFactoryArg<AcDcConverter, AcDcConverter::Mode>
        gDcrect("link",
                std::to_array<std::string_view>({"rectifier", "rect"}),
                AcDcConverter::Mode::RECTIFIER);
    static TypeFactoryArg<AcDcConverter, AcDcConverter::Mode>
        gDcinv("link",
               std::to_array<std::string_view>({"inverter", "inv"}),
               AcDcConverter::Mode::INVERTER);
    static ChildTypeFactory<AcDcConverter, Link>
        gAcdc("link", std::to_array<std::string_view>({"acdc", "acdcconverter", "dcconverter"}));
}  // namespace links
std::atomic<count_t> Link::linkCount(0);
// helper defines to have things make more sense
#define DEFAULTPOWERCOMP (this->*(flowCalc[0]))
#define MODEPOWERCOMP (this->*(flowCalc[getLinkApprox(sMode)]))
#define DERIVCOMP (this->*(derivCalc[getLinkApprox(sMode)]))
#define DEFAULTDERIVCOMP (this->*(derivCalc[0]))

Link::Link(const std::string& objName): GridPrimary(objName)
{
    // default values
    setUserID(++linkCount);
    updateName();
}

CoreObject* Link::clone(CoreObject* obj) const
{
    auto* lnk = cloneBaseFactory<Link, GridPrimary>(this, obj, &gLf);
    if (lnk == nullptr) {
        return obj;
    }

    lnk->Pset = Pset;
    lnk->Erating = Erating;
    lnk->ratingB = ratingB;
    lnk->ratingA = ratingA;
    lnk->lossFraction = lossFraction;
    lnk->circuitNum = circuitNum;
    lnk->zone = zone;
    return lnk;
}

Link::~Link() = default;

// timestepP link's buses
void Link::updateBus(GridBus* bus, index_t busNumber)
{
    if (busNumber == 1) {
        if (B1 != nullptr) {
            B1->remove(this);
        }
        B1 = bus;
        if (B1 != nullptr) {
            B1->add(this);
        }

    } else if (busNumber == 2) {
        if (B2 != nullptr) {
            B2->remove(this);
        }
        B2 = bus;
        if (B2 != nullptr) {
            B2->add(this);
        }
    } else {
        throw(ObjectAddFailure(this));
    }
}

void Link::followNetwork(int network, std::queue<GridBus*>& stk)
{
    if (isConnected() && opFlags[network_connected]) {
        if (B1->Network != network) {
            stk.push(B1);
        }
        if (B2->Network != network) {
            stk.push(B2);
        }
    }
}

void Link::pFlowCheck(std::vector<Violation>& violationVector)
{
    const double mva = std::max(getCurrent(0), getCurrent(1));
    if (mva > ratingA) {
        Violation viol(getName(), MVA_EXCEED_RATING_A);
        viol.level = mva;
        viol.limit = ratingA;
        viol.percentViolation = (mva - ratingA) / ratingA * 100;
        violationVector.push_back(viol);
    }
    if (mva > ratingB) {
        Violation viol(getName(), MVA_EXCEED_RATING_B);

        viol.level = mva;
        viol.limit = ratingB;
        viol.percentViolation = (mva - ratingB) / ratingB * 100;
        violationVector.push_back(viol);
    }
    if (mva > Erating) {
        Violation viol(getName(), MVA_EXCEED_ERATING);
        viol.level = mva;
        viol.limit = Erating;
        viol.percentViolation = (mva - Erating) / Erating * 100;
        violationVector.push_back(viol);
    }
}

double Link::quickupdateP()
{
    return Pset;
}
void Link::timestep(const coreTime time, const IOdata& /*inputs*/, const SolverMode& /*sMode*/)
{
    if (!isEnabled()) {
        return;
    }

    updateLocalCache();
    prevTime = time;
    /*if (scheduled)
    {
    Psched=sched->timestepP(time);
    }*/
}

static const stringVec LOC_NUM_STRINGS{"loss", "switch1", "switch2", "p"};
static const stringVec LOC_STR_STRINGS{"from", "to"};
static const stringVec FLAG_STRINGS{};
void Link::getParameterStrings(stringVec& pstr, ParamStringType pstype) const
{
    getParamString<Link, GridPrimary>(
        this, pstr, LOC_NUM_STRINGS, LOC_STR_STRINGS, FLAG_STRINGS, pstype);
}

// set properties
void Link::set(std::string_view param, std::string_view val)
{
    if ((param == "bus1") || (param == "from")) {
        auto* bus = dynamic_cast<GridBus*>(locateObject(std::string{val}, getParent()));

        if (bus != nullptr) {
            updateBus(bus, 1);
        } else {
            throw(InvalidParameterValue(param));
        }
    } else if ((param == "bus2") || (param == "to")) {
        auto* bus = dynamic_cast<GridBus*>(locateObject(std::string{val}, getParent()));
        if (bus != nullptr) {
            updateBus(bus, 2);
        } else {
            throw(InvalidParameterValue(param));
        }
    } else if (param == "status") {
        auto statusValue = gmlc::utilities::convertToLowerCase(val);
        if ((statusValue == "closed") || (statusValue == "connected")) {
            reconnect();
        } else if ((statusValue == "open") || (statusValue == "disconnected")) {
            disconnect();
        }
    } else {
        GridPrimary::set(param, val);
    }
}

// true is open
// false is closed
void Link::switchMode(index_t num, bool mode)
{
    if (num == 2) {
        if (mode == opFlags[switch2_open_flag]) {
            return;
        }

        opFlags.flip(switch2_open_flag);

        if (opFlags[pFlow_initialized]) {
            logging::debug(this,
                           "Switch2 changed||state ={}, link status ={}",
                           (opFlags[switch2_open_flag]) ? "OPEN" : "CLOSED",
                           (isConnected()) ? "CONNECTED" : "DISCONNECTED");
            if (isConnected()) {
                reconnect();
            } else {
                switchChange(2);

                if (!B1->checkCapable()) {
                    B1->disconnect();
                }
                if (!B2->checkCapable()) {
                    B2->disconnect();
                }
                updateLocalCache();
                alert(this, CONNECTIVITY_CHANGE);
            }
        }
    } else {
        if (mode == opFlags[switch1_open_flag]) {
            return;
        }
        opFlags.flip(switch1_open_flag);

        if (opFlags[pFlow_initialized]) {
            logging::debug(this,
                           "Switch1 changed||state ={}, link status ={}",
                           (opFlags[switch1_open_flag]) ? "OPEN" : "CLOSED",
                           (isConnected()) ? "CONNECTED" : "DISCONNECTED");
            if (isConnected()) {
                reconnect();
            } else {
                switchChange(1);
                if (!B1->checkCapable()) {
                    B1->disconnect();
                }
                if (!B2->checkCapable()) {
                    B2->disconnect();
                }
                updateLocalCache();
                alert(this, CONNECTIVITY_CHANGE);
            }
        }
    }
}

void Link::switchChange(int /*switchNum*/)
{
    computePowers();
}

bool Link::testAndTrip([[maybe_unused]] int tripLevel)
{
    return false;
}

void Link::disconnect()
{
    if (isConnected()) {
        opFlags.set(switch1_open_flag, true);
        opFlags.set(switch2_open_flag, true);
        switchChange(1);
        switchChange(2);
        if (!B1->checkCapable()) {
            B1->disconnect();
        }
        if (!B2->checkCapable()) {
            B2->disconnect();
        }
        updateLocalCache();
        logging::debug(this, "disconnecting line");
        alert(this, CONNECTIVITY_CHANGE);
    }
}

void Link::reconnect()
{
    if (!isConnected()) {
        if (opFlags[switch1_open_flag]) {
            opFlags.reset(switch1_open_flag);
            switchChange(1);
        }
        if (opFlags[switch2_open_flag]) {
            opFlags.reset(switch2_open_flag);
            switchChange(2);
        }
        logging::debug(this, "reconnecting line");
        updateLocalCache();
    }

    if (B1->checkFlag(disconnected)) {
        if (!(B2->checkFlag(disconnected))) {
            B1->reconnect(B2);
            updateLocalCache();
        }
    } else if (B2->checkFlag(disconnected)) {
        B2->reconnect(B1);
        updateLocalCache();
    }
}

void Link::set(std::string_view param, double val, unit unitType)
{
    if ((param == "state") || (param == "switch") || (param == "switch1") || (param == "breaker") ||
        (param == "breaker_open") || (param == "breaker1") || (param == "breaker_open1")) {
        switchMode(1, (val > 0.01));
    } else if ((param == "switch2") || (param == "breaker2") || (param == "breaker_open2")) {
        switchMode(2, (val > 0.01));
    } else if ((param == "breaker_close1") || (param == "breaker_close")) {
        switchMode(1, (val < 0.01));
    } else if (param == "breaker_close2") {
        switchMode(2, (val < 0.01));
    } else if (param == "pset") {
        Pset = convert(val, unitType, puMW, systemBasePower);
        opFlags.set(fixed_target_power);
        computePowers();
    } else if ((param == "loss") || (param == "lossfraction")) {
        lossFraction = val;
        computePowers();
    } else if ((param == "ratinga") || (param == "rating")) {
        ratingA = convert(val, unitType, puMW, systemBasePower);
    } else if (param == "ratingb") {
        ratingB = convert(val, unitType, puMW, systemBasePower);
    } else if ((param == "ratinge") || (param == "emergency_rating") || (param == "erating") ||
               (param == "ratingc")) {
        Erating = convert(val, unitType, puMW, systemBasePower);
    } else if (param == "circuit") {
        circuitNum = static_cast<index_t>(val);
    } else {
        GridPrimary::set(param, val, unitType);
    }
}

CoreObject* Link::getSubObject(std::string_view typeName, index_t num) const
{
    if (typeName == "bus") {
        if (num == 1) {
            return B1;
        }
        if (num == 2) {
            return B2;
        }
        return nullptr;
    }
    return nullptr;
}

double Link::get(std::string_view param, unit unitType) const
{
    double val = kNullVal;

    if ((param == "breaker1") || (param == "switch1") || (param == "breaker_open1")) {
        val = static_cast<double>(opFlags[switch1_open_flag]);
    } else if ((param == "breaker2") || (param == "switch2") || (param == "breaker_open2")) {
        val = static_cast<double>(opFlags[switch2_open_flag]);
    } else if ((param == "connected") || (param == "breaker")) {
        val = static_cast<double>(isConnected());
    } else if ((param == "set") || (param == "pset")) {
        val = units::convert(Pset, puMW, unitType, systemBasePower);
    } else if (param == "linkcount") {
        val = 1.0;
    } else if ((param == "buscount") || (param == "gencount") || (param == "loadcount") ||
               (param == "relaycount")) {
        val = 0.0;
    } else if ((param == "rating") || (param == "ratinga")) {
        val = ratingA;
    } else if (param == "ratingb") {
        val = ratingB;
    } else if (param == "erating") {
        val = Erating;
    } else if (param == "loss") {
        val = convert(getLoss(), puMW, unitType, systemBasePower);
    } else if (param == "lossfraction") {
        val = lossFraction;
    } else if (param == "circuit") {
        val = circuitNum;
    } else {
        auto fptr = getObjectFunction(this, std::string{param});
        if (fptr.first) {
            CoreObject* tobj = const_cast<Link*>(this);
            val = convert(fptr.first(tobj), fptr.second, unitType, systemBasePower);
        } else {
            val = GridPrimary::get(param, unitType);
        }
    }
    return val;
}

void Link::pFlowObjectInitializeA(coreTime /*time0*/, std::uint32_t /*flags*/)
{
    if (B1 == nullptr) {
        opFlags.set(switch1_open_flag);
    }
    if (B2 == nullptr) {
        opFlags.set(switch2_open_flag);
    }
}

bool Link::isConnected() const
{
    return (!(opFlags[switch1_open_flag] || opFlags[switch2_open_flag]));
}
int Link::fixRealPower(double power,
                       id_type_t measureTerminal,
                       id_type_t /*fixedTerminal*/,
                       unit unitType)
{
    if (measureTerminal == 1) {
        Pset = convert(power, unitType, puMW, systemBasePower);
    } else {
        Pset = convert(power, unitType, puMW, systemBasePower) / (1.0 - lossFraction);
    }
    opFlags.set(fixed_target_power);
    return 1;
}

static IOlocs gALoc{0, 1};

int Link::fixPower(double rPower,
                   double /*qPower*/,
                   id_type_t measureTerminal,
                   id_type_t fixedTerminal,
                   units::unit unitType)
{
    return fixRealPower(rPower, measureTerminal, fixedTerminal, unitType);
}

void Link::dynObjectInitializeA(coreTime /*time0*/, std::uint32_t /*flags*/)
{
    if ((B1 == nullptr || !B1->isEnabled()) || (B2 == nullptr || !B2->isEnabled())) {
        disable();
    }
}

void Link::computePowers()
{
    if (isConnected()) {
        linkFlows.P1 = Pset;
        linkFlows.P2 = Pset - (std::abs(Pset) * lossFraction);
    } else {
        linkFlows.P1 = 0;
        linkFlows.P2 = 0;
    }
}

void Link::ioPartialDerivatives(id_type_t /*busId*/,
                                const StateData& /*sD*/,
                                matrixData<double>& /*md*/,
                                const IOlocs& /*inputLocs*/,
                                const SolverMode& /*sMode*/)
{
}

void Link::outputPartialDerivatives(id_type_t /*busId*/,
                                    const StateData& /*sD*/,
                                    matrixData<double>& /*md*/,
                                    const SolverMode& /*sMode*/)
{
}

count_t Link::outputDependencyCount(index_t /*num*/, const SolverMode& /*sMode*/) const
{
    return 0;
}
IOdata Link::getOutputs(const IOdata& /*inputs*/,
                        const StateData& stateData,
                        const SolverMode& sMode) const
{
    return getOutputs(1, stateData, sMode);
}

static bool isBus2(id_type_t busId, GridBus* bus)
{
    return ((busId == 2) || (isSameObject(busId, bus)));
}

IOdata Link::getOutputs(id_type_t busId, const StateData& /*sD*/, const SolverMode& /*sMode*/) const
{
    // set from/to buses
    IOdata out{0.0, 0.0};

    if (isBus2(busId, B2)) {
        out[PoutLocation] = Pset;
    } else {
        out[PoutLocation] = Pset - (std::abs(Pset) * lossFraction);
    }
    return out;
}

void Link::disable()
{
    if (!isEnabled()) {
        return;
    }
    if ((opFlags[has_pflow_states]) || (opFlags[has_dyn_states])) {
        alert(this, STATE_COUNT_CHANGE);
    } else {
        alert(this, JAC_COUNT_CHANGE);
    }
    CoreObject::disable();
    if ((B1 != nullptr) && (B1->isEnabled())) {
        if (!(B1->checkCapable())) {
            B1->disable();
        }
    }
    if ((B2 != nullptr) && (B2->isEnabled())) {
        if (!(B2->checkCapable())) {
            B2->disable();
        }
    }
}

double Link::getMaxTransfer() const
{
    if (!isConnected()) {
        return 0;
    }
    if (Erating > 0) {
        return Erating;
    }
    if (ratingB > 0) {
        return ratingB;
    }
    if (ratingA > 0) {
        return ratingA;
    }

    return kBigNum;
}

double Link::getBusAngle(id_type_t busId) const
{
    if (busId < 500_ind) {
        const auto* bus = getBus(static_cast<index_t>(busId));
        if (bus != nullptr) {
            return bus->getAngle();
        }
    }
    // these are special cases for getting opposite angles as called by the attached buses
    if (isSameObject(busId, B2)) {
        return (B1 != nullptr) ? B1->getAngle() : kNullVal;
    }
    if (isSameObject(busId, B1)) {
        return (B2 != nullptr) ? B2->getAngle() : kNullVal;
    }
    // now just default to the original behavior
    auto* bus = getBus(static_cast<index_t>(busId));
    if (bus != nullptr) {
        return bus->getAngle();
    }
    return kNullVal;
}

double Link::getBusAngle(const StateData& stateData, const SolverMode& sMode, id_type_t busId) const
{
    if (busId < 500_ind) {
        const auto* bus = getBus(static_cast<index_t>(busId));
        if (bus != nullptr) {
            return bus->getAngle();
        }
    }
    // these are special cases for getting opposite angles as called by the attached buses
    if (isSameObject(busId, B2)) {
        return (B1 != nullptr) ? B1->getAngle(stateData, sMode) : kNullVal;
    }
    if (isSameObject(busId, B1)) {
        return (B2 != nullptr) ? B2->getAngle(stateData, sMode) : kNullVal;
    }
    // now just default to the original behavior
    const auto* bus = getBus(static_cast<index_t>(busId));
    if (bus != nullptr) {
        return bus->getAngle(stateData, sMode);
    }
    return kNullVal;
}

double Link::getVoltage(id_type_t busId) const
{
    if (isBus2(busId, B2)) {
        return B2->getVoltage();
    }
    return B1->getVoltage();
}

void Link::setState(coreTime time,
                    const double /*state*/[],
                    const double /*dstate_dt*/[],
                    const SolverMode& /*sMode*/)
{
    prevTime = time;
}

void Link::updateLocalCache(const IOdata& /*inputs*/,
                            const StateData& stateData,
                            const SolverMode& sMode)
{
    if (!isEnabled()) {
        return;
    }
    if ((linkInfo.seqID == stateData.seqID) && (stateData.seqID != 0)) {
        return;  // already computed
    }
    linkInfo.v1 = B1->getVoltage(stateData, sMode);
    const double angle1 = B1->getAngle(stateData, sMode);
    linkInfo.v2 = B2->getVoltage(stateData, sMode);
    const double angle2 = B2->getAngle(stateData, sMode);

    linkInfo.theta1 = angle1 - angle2;
    linkInfo.theta2 = angle2 - angle1;
    linkInfo.seqID = stateData.seqID;
}

void Link::updateLocalCache()
{
    if (!isEnabled()) {
        return;
    }
    linkInfo.v1 = B1->getVoltage();
    const double angle1 = B1->getAngle();
    linkInfo.v2 = B2->getVoltage();
    const double angle2 = B2->getAngle();

    linkInfo.theta1 = angle1 - angle2;
    linkInfo.theta2 = angle2 - angle1;
}

GridBus* Link::getBus(index_t busInd) const
{
    // for Links it is customary to refer to the buses as 1 and 2, but for indexing schemes they
    // sometimes atart at
    // 0
    // so this function will return Bus 1 for indices <=1 and Bus if the index is 2.
    if (busInd <= 1) {
        return B1;
    }
    if ((B1 != nullptr) && (busInd == B1->getID())) {
        return B1;
    }
    if (busInd == 2) {
        return B2;
    }
    if ((B2 != nullptr) && (busInd == B2->getID())) {
        return B2;
    }
    return nullptr;
}

double Link::getRealPower(id_type_t busId) const
{
    return (isBus2(busId, B2)) ? linkFlows.P2 : linkFlows.P1;
}
double Link::getReactivePower(id_type_t busId) const
{
    return (isBus2(busId, B2)) ? linkFlows.Q2 : linkFlows.Q1;
}

double Link::remainingCapacity() const
{
    return getMaxTransfer() - std::abs(linkFlows.P1);
}
double Link::getAngle(const double state[], const SolverMode& sMode) const
{
    const double angle1 = B1->getAngle(state, sMode);
    const double angle2 = B2->getAngle(state, sMode);
    return angle1 - angle2;
}

double Link::getAngle() const
{
    return (linkInfo.theta1);
}
double Link::getLoss() const
{
    return std::abs(linkFlows.P1 + linkFlows.P2);
}
double Link::getReactiveLoss() const
{
    return std::abs(linkFlows.Q1 + linkFlows.Q2);
}
double Link::getRealImpedance(id_type_t busId) const
{
    if (isBus2(busId, B2))  // from bus
    {
        const std::complex<double> impedance =
            (linkInfo.v2 * linkInfo.v2) / std::complex<double>(linkFlows.P2, linkFlows.Q2);
        return std::isnormal(impedance.real()) ? impedance.real() : kBigNum;
    }
    const std::complex<double> impedance =
        (linkInfo.v1 * linkInfo.v1) / std::complex<double>(linkFlows.P1, linkFlows.Q1);
    return std::isnormal(impedance.real()) ? impedance.real() : kBigNum;
}
double Link::getImagImpedance(id_type_t busId) const
{
    if (isBus2(busId, B2))  // from bus
    {
        const std::complex<double> impedance =
            (linkInfo.v2 * linkInfo.v2) / std::complex<double>(linkFlows.P2, linkFlows.Q2);
        return std::isnormal(impedance.imag()) ? impedance.imag() : kBigNum;
    }
    const std::complex<double> impedance =
        (linkInfo.v1 * linkInfo.v1) / std::complex<double>(linkFlows.P1, linkFlows.Q1);
    return std::isnormal(impedance.imag()) ? impedance.imag() : kBigNum;
}

double Link::getTotalImpedance(id_type_t busId) const
{
    using gmlc::utilities::signn;
    if (isBus2(busId, B2))  // from bus
    {
        //  printf("id2 impedance=%f\n", signn(linkFlows.P2 +
        //  linkFlows.Q2)*(linkInfo.v2*linkInfo.v2) / std::hypot(linkFlows.P2, linkFlows.Q2));
        const double val = signn(linkFlows.P2 + linkFlows.Q2) * (linkInfo.v2 * linkInfo.v2) /
            std::hypot(linkFlows.P2, linkFlows.Q2);
        return (std::isnormal(val) ? val : kBigNum);
    }
    // printf("id1 impedance=%f\n", signn(linkFlows.P1 + linkFlows.Q1)*(linkInfo.v1*linkInfo.v1) /
    // std::hypot(linkFlows.P1, linkFlows.Q1));
    const double val = signn(linkFlows.P1 + linkFlows.Q1) * (linkInfo.v1 * linkInfo.v1) /
        std::hypot(linkFlows.P1, linkFlows.Q1);
    return (std::isnormal(val) ? val : kBigNum);
}

double Link::getCurrent(id_type_t busId) const
{
    double val;
    if (isBus2(busId, B2))  // from bus
    {
        auto pmag = std::hypot(linkFlows.P2, linkFlows.Q2);
        val = (pmag != 0.0) ? pmag / linkInfo.v2 : 0.0;
    } else {
        auto pmag = std::hypot(linkFlows.P1, linkFlows.Q1);
        val = (pmag != 0.0) ? pmag / linkInfo.v1 : 0.0;
    }
    return (std::isnormal(val) ? val : 0.0);
}

double Link::getCurrentAngle(id_type_t busId) const
{
    double val;
    if (isBus2(busId, B2))  // from bus
    {
        val = std::atan2(linkFlows.Q2, linkFlows.P2);
    } else {
        val = std::atan2(linkFlows.Q1, linkFlows.P1);
    }
    return val;
}

double Link::getRealCurrent(id_type_t busId) const
{
    double val;
    if (isBus2(busId, B2))  // from bus
    {
        val = linkFlows.P2 / (linkInfo.v2);
    } else {
        val = linkFlows.P1 / (linkInfo.v1);
    }
    return (std::isnormal(val) ? val : 0.0);
}
double Link::getImagCurrent(id_type_t busId) const
{
    double val;
    if (isBus2(busId, B2))  // from bus
    {
        val = linkFlows.Q2 / (linkInfo.v2);
    } else {
        val = linkFlows.Q1 / (linkInfo.v1);
    }
    return (std::isnormal(val) ? val : 0.0);
}

Link* getMatchingLink(Link* lnk, GridPrimary* src, GridPrimary* sec)
{
    Link* matchingLink = nullptr;
    if (lnk->isRoot()) {
        return nullptr;
    }
    if (isSameObject(lnk->getParent(), src))  // if this is true then things are easy
    {
        matchingLink = sec->getLink(lnk->locIndex);
    } else {
        std::vector<int> lkind;
        auto* par = dynamic_cast<GridPrimary*>(lnk->getParent());
        if (par == nullptr) {
            return nullptr;
        }
        lkind.push_back(lnk->locIndex);
        while (par->getID() != src->getID()) {
            lkind.push_back(par->locIndex);
            par = dynamic_cast<GridPrimary*>(par->getParent());
            if (par == nullptr) {
                return nullptr;
            }
        }
        // now work our way backwards through the secondary
        par = sec;
        for (size_t kk = lkind.size() - 1; kk > 0; --kk) {
            par = dynamic_cast<GridPrimary*>(par->getGridArea(lkind[kk]));
        }
        matchingLink = par->getLink(lkind[0]);
    }
    return matchingLink;
}

bool compareLink(Link* lnk1, Link* lnk2, bool cmpBus, bool printDiff)
{
    if (cmpBus) {
        bool ret = compareBus(lnk1->getBus(1), lnk2->getBus(1), printDiff);
        ret = ret && compareBus(lnk1->getBus(2), lnk2->getBus(2), printDiff);
        return ret;
    }
    if (typeid(lnk1) != typeid(lnk2)) {
        if (printDiff) {
            std::println("Links are of different types");
        }
        return false;
    }
    if ((dynamic_cast<AcLine*>(lnk1) != nullptr) && (dynamic_cast<AcLine*>(lnk2) != nullptr)) {
        if (std::abs(lnk1->get("r") - lnk2->get("r")) > 0.0001) {
            if (printDiff) {
                std::println("Links have different r");
            }
            return false;
        }
        if (std::abs(lnk1->get("x") - lnk2->get("x")) > 0.0001) {
            if (printDiff) {
                std::println("Links have different x");
            }
            return false;
        }
        if (std::abs(lnk1->get("b") - lnk2->get("b")) > 0.0001) {
            if (printDiff) {
                std::println("Links have different b");
            }
            return false;
        }
    }
    return true;
}

}  // namespace griddyn
