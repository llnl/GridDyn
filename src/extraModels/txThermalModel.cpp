/*
 * Copyright (c) 2014-2020, Lawrence Livermore National Security
 * See the top-level NOTICE for additional details. All rights reserved.
 * SPDX-License-Identifier: BSD-3-Clause
 */

#include "txThermalModel.h"

#include "core/coreExceptions.h"
#include "core/coreObjectTemplates.hpp"
#include "gmlc/utilities/stringOps.h"
#include "griddyn/Link.h"
#include "griddyn/blocks/delayBlock.h"
#include "griddyn/events/Event.h"
#include "griddyn/measurement/Condition.h"
#include "griddyn/measurement/grabberSet.h"
#include "griddyn/measurement/gridGrabbers.h"
#include <cmath>
#include <memory>
#include <string>
#include <utility>

namespace griddyn {
namespace extra {
    txThermalModel::txThermalModel(const std::string& objName): sensor(objName)
    {
        opFlags.reset(continuous_flag);  // this is a not a continuous model
        outputStrings = {{"ambient", "ambientTemp", "airTemp"},
                         {"top_oil", "top_oil_temp"},
                         {"hot_spot", "hot_spot_temp"}};  // preset the outputNames
        m_outputSize = 3;
    }

    coreObject* txThermalModel::clone(coreObject* obj) const
    {
        auto* nobj = cloneBase<txThermalModel, sensor>(this, obj);
        if (nobj == nullptr) {
            return obj;
        }

        nobj->Ttor = Ttor;
        nobj->DThs = DThs;
        nobj->DTtor = DTtor;
        nobj->Tgr = Tgr;
        nobj->mp_LR = mp_LR;
        nobj->mp_n = mp_n;
        nobj->mp_m = mp_m;
        nobj->ambientTemp = ambientTemp;
        nobj->dTempdt = dTempdt;
        nobj->rating = rating;
        nobj->Plossr = Plossr;
        nobj->m_C = m_C;
        nobj->m_k = m_k;
        nobj->alarmTemp1 = alarmTemp1;
        nobj->alarmTemp2 = alarmTemp2;
        nobj->cutoutTemp = cutoutTemp;
        nobj->alarmDelay = alarmDelay;
        return nobj;
    }

    void txThermalModel::setFlag(const std::string& flag, bool val)
    {
        if (flag == "auto") {
            opFlags.set(auto_parameter_load, val);
        } else if (flag == "parameter_updates") {
            opFlags.set(enable_parameter_updates, val);
        } else if (flag == "enable_alarms") {
            opFlags.set(enable_alarms, val);
        } else {
            sensor::setFlag(flag, val);
        }
    }

    void txThermalModel::set(const std::string& param, const std::string& val)
    {
        if ((param == "txtype") || (param == "cooling")) {
            auto v2 = gmlc::utilities::convertToLowerCase(val);
            if (v2 == "auto") {
                opFlags.set(auto_parameter_load);
            } else if (v2 == "oa") {
                DThs = 25.0;
                DTtor = 55.0;
                Ttor = 3.0 * 3600.0;
                Tgr = 5.0 * 60.0;
                mp_LR = 3.2;
                mp_n = 0.8;
                mp_m = 0.8;
            } else if (v2 == "fa") {
                DThs = 30.0;
                DTtor = 50.0;
                Ttor = 2.0 * 3600.0;
                Tgr = 5.0 * 60.0;
                mp_LR = 4.5;
                mp_n = 0.8;
                mp_m = 0.8;
            } else if (v2 == "fahot") {
                DThs = 35.0;
                DTtor = 45.0;
                Ttor = 1.25 * 3600.0;
                Tgr = 5.0 * 60.0;
                mp_LR = 6.5;
                mp_n = 0.9;
                mp_m = 0.8;
            } else if (v2 == "ndfoa") {
                DThs = 35.0;
                DTtor = 45.0;
                Ttor = 1.25 * 3600.0;
                Tgr = 5.0 * 60.0;
                mp_LR = 6.5;
                mp_n = 1.0;
                mp_m = 0.8;
            } else if (v2 == "dfoa") {
                DThs = 35.0;
                DTtor = 45.0;
                Ttor = 1.25 * 3600.0;
                Tgr = 5.0 * 60.0;
                mp_LR = 6.5;
                mp_n = 1.0;
                mp_m = 1.0;
            }
        } else {
            sensor::set(param, val);
        }
    }

    void txThermalModel::set(const std::string& param, double val, units::unit unitType)
    {
        if ((param == "ambient") || (param == "ambienttemp")) {
            ambientTemp = units::convert(val, unitType, units::degC);
        } else if ((param == "dtempdt") || (param == "temp_rate_of_change")) {
            dTempdt = units::convert(val, unitType, units::degC);
        } else if ((param == "dths") || (param == "rated_hot_spot_rise") || (param == "dthsr")) {
            DThs = units::convert(val, unitType, units::degC);
        } else if ((param == "dttor") || (param == "rated_top_oil_rise") || (param == "dtto")) {
            DTtor = units::convert(val, unitType, units::degC);
        } else if ((param == "ttor") || (param == "oil_time_constant")) {
            Ttor = units::convert(val, unitType, units::second);
        } else if ((param == "tgr") || (param == "winding_time_constant")) {
            Tgr = units::convert(val, unitType, units::second);
        } else if ((param == "alarmtemp") || (param == "alarmtemp1")) {
            alarmTemp1 = units::convert(val, unitType, units::degC);
            if (opFlags[dyn_initialized]) {
                getCondition(0)->setConditionRHS(alarmTemp1);
                setConditionStatus(0,
                                   (alarmTemp1 > 0.1) ? condition_status_t::active :
                                                        condition_status_t::disabled);
            }
        } else if (param == "alarmtemp2") {
            alarmTemp2 = units::convert(val, unitType, units::degC);
            if (opFlags[dyn_initialized]) {
                getCondition(1)->setConditionRHS(alarmTemp1);
                setConditionStatus(1,
                                   (alarmTemp1 > 0.1) ? condition_status_t::active :
                                                        condition_status_t::disabled);
            }
        } else if (param == "cutouttemp") {
            cutoutTemp = units::convert(val, unitType, units::degC);
            if (opFlags[dyn_initialized]) {
                getCondition(2)->setConditionRHS(alarmTemp1);
                setConditionStatus(2,
                                   (alarmTemp1 > 0.1) ? condition_status_t::active :
                                                        condition_status_t::disabled);
            }
        } else if (param == "alarmdelay") {
            alarmDelay = units::convert(val, unitType, units::second);
            if (opFlags[dyn_initialized]) {
                setActionTrigger(0, 0, alarmDelay);
                setActionTrigger(1, 1, alarmDelay);
                setActionTrigger(2, 2, alarmDelay);
                setActionTrigger(3, 2, alarmDelay);
            }
        } else if ((param == "lr") || (param == "loss_ratio")) {
            mp_LR = val;
        } else if ((param == "m") || (param == "winding_exponent")) {
            mp_m = val;
        } else if ((param == "n") || (param == "oil_exponent")) {
            mp_n = val;
        } else {
            gridPrimary::set(param, val, unitType);  // NOLINT
        }
    }

    double txThermalModel::get(const std::string& param, units::unit unitType) const
    {
        return sensor::get(param, unitType);
    }

    void txThermalModel::add(coreObject* /*obj*/)
    {
        throw(unrecognizedObjectException(this));
    }

    void txThermalModel::dynObjectInitializeA(coreTime time0, std::uint32_t flags)
    {
        if (m_sourceObject == nullptr) {
            return sensor::dynObjectInitializeA(time0, flags);
        }

        if (updatePeriod > kHalfBigNum) {  // set the period to the period of the simulation to at
                                           // least 1/5 the winding time constant
            double pstep = getRoot()->get("steptime");
            if (pstep < 0) {
                pstep = 1.0;
            }
            double mtimestep = Tgr / 5.0;
            updatePeriod = pstep * std::floor(mtimestep / pstep);
            if (updatePeriod < pstep) {
                updatePeriod = pstep;
            }
        }

        rating = m_sourceObject->get("rating");
        double bp = m_sourceObject->get("basepower");
        if (opFlags[auto_parameter_load]) {
            if (rating * bp < 2.5) {
                set("cooling", "oa");
            } else if (rating * bp < 10) {
                set("cooling", "fa");
            } else if (rating * bp < 100) {
                set("cooling", "fahot");
            } else if (rating * bp < 200) {
                set("cooling", "ndfoa");
            } else {
                set("cooling", "dfoa");
            }
        }
        double resist = m_sourceObject->get("r");
        double conduct = m_sourceObject->get("g");  // get conductance
        Plossr = rating * rating * resist + conduct;  // loss is I^2*r+g in per unit;

        if (conduct > 0.0) {
            mp_LR = Plossr / conduct;
        }

        m_k = Plossr / DTtor;  // compute the radiation constant
        m_C = Ttor * Plossr / DTtor;  // compute the thermal mass constant
        if (!opFlags[dyn_initialized]) {
            sensor::setFlag("sampled", true);
            sensor::set("input0", "current");
            sensor::set("input1", "loss");
            sensor::set("input2", "attached");

            auto* b1 = new blocks::delayBlock(Ttor);
            auto* b2 = new blocks::delayBlock(Tgr);

            sensor::add(b1);
            sensor::add(b2);
            b1->parentSetFlag(separate_processing, true, this);
            b2->parentSetFlag(separate_processing, true, this);
            auto g1 = std::make_shared<customGrabber>();
            g1->setGrabberFunction("ambient", [this](coreObject* /*unused*/) -> double {
                return ambientTemp;
            });
            sensor::add(g1);

            m_outputSize = (m_outputSize > 3) ? m_outputSize : 3;

            outputMode.resize(m_outputSize);
            outputs.resize(m_outputSize);
            outGrabbers.resize(m_outputSize, nullptr);
            outputMode[0] = outputMode_t::direct;
            outputMode[1] = outputMode_t::block;
            outputs[0] =
                3;  // the first input was setup as the current, second as the loss, 3rd as attached
            outputs[1] = 0;
            sensor::set("output2", "block0+block1");
            auto c1 = make_condition("output1", ">", alarmTemp1, this);
            Relay::add(std::shared_ptr<Condition>(std::move(c1)));
            c1 = make_condition("output1", ">", alarmTemp2, this);
            Relay::add(std::shared_ptr<Condition>(std::move(c1)));
            c1 = make_condition("output1", ">", cutoutTemp, this);
            Relay::add(std::shared_ptr<Condition>(std::move(c1)));

            Relay::set("action", "alarm temperature_alarm1");  // NOLINT
            Relay::set("action", "alarm temperature_alarm2");  // NOLINT
            auto ge = std::make_unique<Event>();
            ge->setTarget(m_sinkObject, "switch1");
            ge->setValue(1.0);

            Relay::add(std::shared_ptr<Event>(std::move(ge)));
            ge = std::make_unique<Event>();

            ge->setTarget(m_sinkObject, "switch2");
            ge->setValue(1.0);

            Relay::add(std::shared_ptr<Event>(std::move(ge)));
            // add the triggers
            setActionTrigger(0, 0, alarmDelay);
            setActionTrigger(1, 1, alarmDelay);
            setActionTrigger(2, 2, alarmDelay);
            setActionTrigger(3, 2, alarmDelay);

            if (alarmTemp1 <= 0.1) {
                setConditionStatus(0, condition_status_t::disabled);
            }
            if (alarmTemp1 <= 0.1) {
                setConditionStatus(1, condition_status_t::disabled);
            }
            if (cutoutTemp <= 0.1) {
                setConditionStatus(2, condition_status_t::disabled);
            }
        }
        return sensor::dynObjectInitializeA(time0, flags);
    }

    void txThermalModel::dynObjectInitializeB(const IOdata& inputs,
                                              const IOdata& desiredOutput,
                                              IOdata& fieldSet)
    {
        dataSources[0]->setGain(1.0 / rating);
        dataSources[1]->setGain(1.0 / Plossr);

        double I = dataSources[0]->grabData();

        double K2 = dataSources[1]->grabData();
        double at = dataSources[2]->grabData();
        IOdata iset(1);
        if (at > 0.1) {
            double DTtou = DTtor * pow((I * I * mp_LR + 1) / (mp_LR + 1), mp_n);
            double DTgu = DThs * pow(K2, mp_m);

            iset[0] = DTtou + ambientTemp;
            filterBlocks[0]->dynInitializeB(
                iset,
                iset,
                iset);  // I don't care what the result is so I use the same vector for all inputs
            iset[0] = DTgu;
            filterBlocks[1]->dynInitializeB(iset, iset, iset);
        } else {
            iset[0] = ambientTemp;
            filterBlocks[0]->dynInitializeB(iset, iset, iset);
            iset[0] = 0;
            filterBlocks[1]->dynInitializeB(iset, iset, iset);
        }
        // skip over sensor::dynInitializeB since the filter blocks are initialized here.
        return Relay::dynObjectInitializeB(inputs, desiredOutput, fieldSet);  // NOLINT
    }

    void txThermalModel::updateA(coreTime time)
    {
        auto dt = time - prevTime;
        if (dt == timeZero) {
            return;
        }
        double at = dataSources[2]->grabData();
        ambientTemp = ambientTemp + dt * dTempdt;
        if (at > 0.1) {
            double I = dataSources[0]->grabData();
            double K2 = dataSources[1]->grabData();

            double DTtou = DTtor * pow((I * I * mp_LR + 1) / (mp_LR + 1), mp_n);
            double DTgu = DThs * pow(K2, mp_m);

            // update the time constants if required
            if (mp_n != 1.0) {
                double Toc = filterBlocks[0]->getOutput();
                double r1 = (Toc - ambientTemp) / DTtor;
                double r2 = DTtou / DTtor;
                double Tto = Ttor * ((r1 - r2) / (pow(r1, 1.0 / mp_n) - pow(r2, 1.0 / mp_n)));
                filterBlocks[0]->set("t1", Tto);
            }
            if (mp_m != 1.0) {
                double Thsc = filterBlocks[1]->getOutput();
                double r1 = (Thsc) / DThs;
                double r2 = DTgu / DThs;
                double Tg = Ttor * ((r1 - r2) / (pow(r1, 1.0 / mp_m) - pow(r2, 1.0 / mp_m)));
                filterBlocks[1]->set("t1", Tg);
            }

            filterBlocks[0]->step(time, ambientTemp + DTtou);
            filterBlocks[1]->step(time, DTgu);
        } else {
            filterBlocks[0]->step(time, ambientTemp);
            filterBlocks[1]->step(time, 0);
        }
        // printf("%f:%s A=%f to(%f)=%f hs(%f)=%f\n",time, name.c_str(), ambientTemp,
        // DTtou+ambientTemp, o1, DTgu, o2);
        Relay::updateA(time);  // NOLINT
        prevTime = time;
    }

    void txThermalModel::timestep(coreTime time,
                                  const IOdata& /*inputs*/,
                                  const solverMode& /*sMode*/)
    {
        updateA(time);
    }

}  // namespace extra
}  // namespace griddyn
