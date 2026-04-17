/*
 * Copyright (c) 2014-2026, Lawrence Livermore National Security
 * See the top-level NOTICE for additional details. All rights reserved.
 * SPDX-License-Identifier: BSD-3-Clause
 */

#include "pmu.h"

#include "../Link.h"
#include "../blocks/delayBlock.h"
#include "../blocks/filteredDerivativeBlock.h"
#include "../comms/Communicator.h"
#include "../comms/controlMessage.h"
#include "../events/Event.h"
#include "../gridBus.h"
#include "../measurement/Condition.h"
#include "../measurement/grabberSet.h"
#include "../measurement/gridGrabbers.h"
#include "core/coreExceptions.h"
#include "core/coreObjectTemplates.hpp"
#include <cmath>
#include <memory>
#include <string>
#include <utility>

namespace griddyn {
namespace relays {
    pmu::pmu(const std::string& objName): sensor(objName)
    {
        outputStrings = {{"voltage"}, {"angle"}, {"frequency"}, {"rocof"}};
    }

    coreObject* pmu::clone(coreObject* obj) const
    {
        auto* nobj = cloneBase<pmu, sensor>(this, obj);
        if (nobj == nullptr) {
            return obj;
        }

        nobj->Tv = Tv;
        nobj->Ttheta = Ttheta;
        nobj->Trocof = Trocof;
        nobj->Tcurrent = Tcurrent;
        nobj->transmissionPeriod = transmissionPeriod;
        nobj->sampleRate = sampleRate;
        return nobj;
    }

    void pmu::setFlag(const std::string& flag, bool val)
    {
        if ((flag == "transmit") || (flag == "transmitactive") || (flag == "transmit_active")) {
            opFlags.set(transmit_active, val);
        } else if ((flag == "three_phase") || (flag == "3phase") ||
                   (flag == "three_phase_active")) {
            opFlags.set(three_phase_capable, val);
        } else if ((flag == "current_active") || (flag == "current")) {
            opFlags.set(current_active, val);
        } else {
            sensor::setFlag(flag, val);
        }
    }

    void pmu::set(const std::string& param, const std::string& val)
    {
        if (param.empty()) {
        } else {
            sensor::set(param, val);
        }
    }

    using units::convert;
    using units::unit;

    void pmu::set(const std::string& param, double val, unit unitType)
    {
        if ((param == "tv") || (param == "voltagedelay")) {
            Tv = val;
            if (opFlags[dyn_initialized]) {
            }
        } else if ((param == "ttheta") || (param == "tangle") || (param == "angledelay")) {
            Ttheta = val;
            if (opFlags[dyn_initialized]) {
            }
        } else if (param == "trocof") {
            Trocof = val;
            if (opFlags[dyn_initialized]) {
            }
        } else if ((param == "tcurrent") || (param == "tI") || (param == "currentdelay")) {
            Tcurrent = val;
            if (opFlags[dyn_initialized]) {
            }
        } else if ((param == "transmitrate") || (param == "rate")) {
            transmissionPeriod = (val >= kMin_Res) ? 1.0 / val : kBigNum;
        } else if ((param == "transmitperiod") || (param == "period")) {
            transmissionPeriod = convert(val, unitType, units::second);
        } else if (param == "samplerate") {
            sampleRate = val;
        } else {
            sensor::set(param, val, unitType);
        }
    }

    double pmu::get(const std::string& param, units::unit unitType) const
    {
        if ((param == "tv") || (param == "voltagedelay")) {
            return Tv;
        }
        if ((param == "ttheta") || (param == "tangle") || (param == "angledelay")) {
            return Ttheta;
        }
        if ((param == "tcurrent") || (param == "tI") || (param == "currentdelay")) {
            return Tcurrent;
        }
        if (param == "trocof") {
            return Trocof;
        }
        if ((param == "transmitrate") || (param == "rate")) {
            return 1.0 / transmissionPeriod;
        }
        if (param == "transmitperiod") {
            return transmissionPeriod;
        }
        if (param == "samplerate") {
            return sampleRate;
        }
        return sensor::get(param, unitType);
    }

    void pmu::dynObjectInitializeA(coreTime time0, std::uint32_t flags)
    {
        if (m_sourceObject == nullptr) {
            // we know the parent is most likely an area so find the corresponding bus that matches
            // the user ID
            if (getUserID() != kNullLocation) {
                m_sourceObject = getParent()->getSubObject("bus", getUserID());
            }
        }
        // if the first check failed just try to find a bus
        if (m_sourceObject == nullptr) {
            m_sourceObject = getParent()->find("bus");
        }
        if (m_sourceObject == nullptr) {
            LOG_WARNING("no pmu target specified");
            disable();
            return;
        }
        // check for 3 phase sensors
        if (dynamic_cast<gridComponent*>(m_sourceObject) != nullptr) {
            if (static_cast<gridComponent*>(m_sourceObject)->checkFlag(three_phase_capable)) {
                if (!opFlags[three_phase_set]) {
                    opFlags[three_phase_active] = true;
                }
            } else {
                opFlags[three_phase_active] = false;
            }
        }

        if (dynamic_cast<gridBus*>(m_sourceObject) != nullptr) {
            // no way to get current from a bus
            opFlags[current_active] = false;
        }
        generateOutputNames();
        createFilterBlocks();
        return sensor::dynObjectInitializeA(time0, flags);
    }

    void pmu::generateOutputNames()
    {
        // 4 different scenarios
        if (opFlags[three_phase_active]) {
            if (opFlags[current_active]) {
                // three phase voltage and current
                outputStrings = {{"voltageA"},
                                 {"angleA"},
                                 {"voltageB"},
                                 {"angleB"},
                                 {"voltageC"},
                                 {"angleC"},
                                 {"current_realA"},
                                 {"current_imagA"},
                                 {"current_realB"},
                                 {"current_imagB"},
                                 {"current_realC"},
                                 {"current_imagC"},
                                 {"frequency"},
                                 {"rocof"}};
            } else {
                // three phase voltage
                outputStrings = {{"voltageA"},
                                 {"angleA"},
                                 {"voltageB"},
                                 {"angleB"},
                                 {"voltageC"},
                                 {"angleC"},
                                 {"frequency"},
                                 {"rocof"}};
            }
        } else {
            if (opFlags[current_active]) {
                // single phase voltage and current
                outputStrings = {{"voltage"},
                                 {"angle"},
                                 {"current_real"},
                                 {"current_imag"},
                                 {"frequency"},
                                 {"rocof"}};
            } else {
                // single phase voltage
                outputStrings = {{"voltage", "v"},
                                 {"angle", "ang", "theta"},
                                 {"frequency", "freq", "f"},
                                 {"rocof"}};
            }
        }
    }
    /** generate the filter blocks and inputs for the sensor object*/
    void pmu::createFilterBlocks()
    {
        // 4 different scenarios
        if (opFlags[three_phase_active]) {
            if (opFlags[current_active]) {  // NOLINT
                // three phase voltage and current
            } else {
                // three phase voltage
            }
        } else {
            auto* vBlock = new blocks::delayBlock(Tv);
            vBlock->setName("voltage");
            add(vBlock);
            vBlock = new blocks::delayBlock(Ttheta);
            vBlock->setName("angle");
            add(vBlock);
            set("input0", "voltage");
            set("input1", "angle");
            set("blockinput0", 0);
            set("blockinput1", 1);
            setupOutput(0, "block0");
            setupOutput(1, "block1");
            if (opFlags[current_active]) {
                vBlock = new blocks::delayBlock(Tcurrent);
                vBlock->setName("current_real");
                add(vBlock);
                vBlock = new blocks::delayBlock(Tcurrent);
                vBlock->setName("current_reactive");
                add(vBlock);
                set("input2", "current_real");
                set("input3", "current_reactive");
                set("blockinput2", 2);
                set("blockinput3", 3);
                setupOutput(2, "block2");
                setupOutput(3, "block3");
            }
            auto* fblock = new blocks::filteredDerivativeBlock("freq");
            fblock->set("t1", Ttheta);
            fblock->set("t2", Trocof);
            add(fblock);
            set("blockinput" + std::to_string(fblock->locIndex), 1);
            setupOutput(fblock->locIndex, "block" + std::to_string(fblock->locIndex));
            setupOutput(fblock->locIndex + 1, "blockderiv" + std::to_string(fblock->locIndex));
        }
    }

    void pmu::updateA(coreTime time)
    {
        sensor::updateA(time);
        if (time >= nextTransmitTime) {
            generateAndTransmitMessage();
            nextTransmitTime = lastTransmitTime + transmissionPeriod;
            if (nextTransmitTime <= time) {
                nextTransmitTime = time + transmissionPeriod;
            }
        }
    }

    coreTime pmu::updateB()
    {
        sensor::updateB();
        if (nextUpdateTime > nextTransmitTime) {
            nextUpdateTime = nextTransmitTime;
        }
        return nextUpdateTime;
    }

    void pmu::generateAndTransmitMessage() const
    {
        if (opFlags[use_commLink]) {
            const auto& oname = outputNames();

            auto message =
                std::make_shared<commMessage>(comms::controlMessagePayload::GET_RESULT_MULTIPLE);

            auto* payload = message->getPayload<comms::controlMessagePayload>();
            auto res = getOutputs(noInputs, emptyStateData, cLocalSolverMode);

            payload->multiFields.resize(res.size());
            payload->multiValues.resize(res.size());
            payload->multiUnits.resize(res.size());
            payload->m_time = prevTime;
            for (index_t ii = 0; ii < static_cast<index_t>(res.size()); ++ii) {
                payload->multiFields[ii] = oname[ii][0];
                payload->multiValues[ii] = res[ii];
                payload->multiUnits[ii] = to_string(outputUnits(ii));
            }

            cManager.send(std::move(message));
        }
    }

}  // namespace relays
}  // namespace griddyn
