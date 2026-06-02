/*
 * Copyright (c) 2014-2026, Lawrence Livermore National Security
 * See the top-level NOTICE for additional details. All rights reserved.
 * SPDX-License-Identifier: BSD-3-Clause
 */

#include "fileInput.h"
#include "gmlc/utilities/stringOps.h"
#include "griddyn/Generator.h"
#include "griddyn/events/Event.h"
#include "griddyn/griddyn-config.h"
#include "griddyn/links/AcLine.h"
#include "griddyn/links/AdjustableTransformer.h"
#include "griddyn/loads/MotorLoad.h"
#include "griddyn/loads/ZipLoad.h"
#include "griddyn/primary/AcBus.h"
#include "griddyn/relays/Pmu.h"
#include "readerHelper.h"

#ifdef ENABLE_OPTIMIZATION_LIBRARY
#    include "optimization/gridDynOpt.h"
#    include "optimization/models/gridGenOpt.h"
#else
#    include "griddyn/simulation/GridSimulation.h"
#endif

#include "griddyn/Exciter.h"
#include "griddyn/Stabilizer.h"
#include "griddyn/genmodels/otherGenModels.h"
#include "griddyn/governors/GovernorTypes.h"
#include <compare>
#include <cstdlib>
#include <fstream>
#include <iostream>
#include <memory>
#include <string>
#include <utility>
#include <vector>

namespace griddyn {
// using namespace units;

namespace {
    void loadPSATBusArray(CoreObject* parentObject,
                          double basepower,
                          const mArray& buses,
                          const mArray& swingBuses,
                          const mArray& pvBuses,
                          const mArray& pqBuses,
                          const stringVec& busnames,
                          std::vector<GridBus*>& busList);
    void loadPSATGenArray(CoreObject* parentObject,
                          const mArray& gens,
                          const std::vector<GridBus*>& busList);
    void loadPSATLinkArray(CoreObject* parentObject,
                           const mArray& links,
                           const std::vector<GridBus*>& busList);
    void loadPSATLinkArrayB(CoreObject* parentObject,
                            const mArray& links,
                            const std::vector<GridBus*>& busList);
    void loadPSATShuntArray(CoreObject* parentObject,
                            const mArray& shunts,
                            const std::vector<GridBus*>& busList);
    void loadPSATLTCArray(CoreObject* parentObject,
                          const mArray& ltcData,
                          const std::vector<GridBus*>& busList);
    void loadPSATPHSArray(CoreObject* parentObject,
                          const mArray& phsData,
                          const std::vector<GridBus*>& busList);
    void loadPSATSynArray(CoreObject* parentObject,
                          const mArray& synData,
                          const std::vector<GridBus*>& busList);
    void loadPSATExcArray(CoreObject* parentObject,
                          const mArray& excData,
                          const std::vector<GridBus*>& busList);
    void loadPsatFaultArray(CoreObject* parentObject,
                            const mArray& faultData,
                            const std::vector<GridBus*>& busList);
    void loadPsatBreakerArray(CoreObject* parentObject,
                              const mArray& breakerData,
                              const std::vector<GridBus*>& busList);
    void loadPsatMotorArray(CoreObject* parentObject,
                            const mArray& motorData,
                            const std::vector<GridBus*>& busList);
    /** load a PSAT PMU data*/
    void loadPsatPmuArray(CoreObject* parentObject,
                          const mArray& pmuData,
                          const std::vector<GridBus*>& busList);
    void loadOtherObjectData(CoreObject* parentObject,
                             const std::string& filetext,
                             const std::vector<GridBus*>& busList);
    const std::vector<
        std::pair<std::string, void (*)(CoreObject*, const mArray&, const std::vector<GridBus*>&)>>
        ARRAY_IDENTIFIERS{
            {"Shunt.con", loadPSATShuntArray},
            {"Line.con", loadPSATLinkArray},
            {"Lines.con", loadPSATLinkArrayB},
            {"Gen.con", loadPSATGenArray},
            {"Ltc.con", loadPSATLTCArray},
            {"Phs.con", loadPSATPHSArray},
            {"Syn.con", loadPSATSynArray},
            {"Exc.con", loadPSATExcArray},
            //{ "Tg.con",loadPSATTgArray },
            {"Fault.con", loadPsatFaultArray},
            {"Breaker.con", loadPsatBreakerArray},
            {"Not.con", loadPsatMotorArray},
            {"Pmu.con", loadPsatPmuArray},
        };
}  // namespace

void loadPSAT(CoreObject* parentObject,
              const std::string& filetext,
              const BasicReaderInfo& readerOptions)
{
    const double basepower = readerOptions.base;
    // std::string tstr;
    mArray busArrayData;
    mArray swingBusData;
    mArray pqBusData;
    mArray pvBusData;
    std::vector<GridBus*> busList;
    /*
    A = filetext.find(basename + ".baseMVA") const;
    if (A != std::string::npos)
    {
            B = filetext.find_first_of('=', A);
            C = filetext.find_first_of(";\n", A);
            tstr = filetext.substr(B + 1, C - B - 1);
            paramRead(tstr, basepower);
            parentObject->set("basepower", basepower);
    }
    */
    // get the list of bus names
    bool nmfnd = false;
    GridSimulation::resetObjectCounters();  // reset all the object counters to 0 to make sure all
                                            // the numbers
    // match up

    stringVec busNames;
    auto busNameStart = filetext.find("Varname.bus");
    if (busNameStart != std::string::npos) {
        const size_t busNameEquals = filetext.find_first_of('=', busNameStart);
        busNames = readMatlabCellArray(filetext, busNameEquals + 1);
        nmfnd = true;
    }
    if (!nmfnd) {
        busNameStart = filetext.find("Bus.names");
        if (busNameStart != std::string::npos) {
            const size_t busNameEquals = filetext.find_first_of('=', busNameStart);
            busNames = readMatlabCellArray(filetext, busNameEquals + 1);
            nmfnd = true;
        }
    }
    if (nmfnd) {
        if (!(readerOptions.prefix.empty())) {
            for (auto& busName : busNames) {
                busName.insert(0, 1, '_');
                busName.insert(0, readerOptions.prefix);
            }
        }
    }
    // now find the bus structure
    auto busArrayStart = filetext.find("Bus.con");
    if (busArrayStart != std::string::npos) {
        const size_t busArrayEquals = filetext.find_first_of('=', busArrayStart);
        readMatlabArray(filetext, busArrayEquals + 1, busArrayData);
        readMatlabArray("SW.con", filetext, swingBusData);

        if (busNames.size() != busArrayData.size()) {
            if (busNames.empty()) {
                busNames.resize(busArrayData.size());
                for (stringVec::size_type kk = 0; kk < busArrayData.size(); ++kk) {
                    if (readerOptions.prefix.empty()) {
                        busNames[kk] = "Bus-" + std::to_string(busArrayData[kk][0]);
                    } else {
                        busNames[kk] =
                            readerOptions.prefix + "_Bus-" + std::to_string(busArrayData[kk][0]);
                    }
                }
            } else {
                std::cout
                    << "WARNING: number of bus names does not match the number of buses listed\n";
            }
        }
        readMatlabArray("PV.con", filetext, pvBusData);
        readMatlabArray("PQ.con", filetext, pqBusData);
        loadPSATBusArray(parentObject,
                         basepower,
                         busArrayData,
                         swingBusData,
                         pvBusData,
                         pqBusData,
                         busNames,
                         busList);
    }
    loadOtherObjectData(parentObject, filetext, busList);
}

namespace {

    void loadOtherObjectData(CoreObject* parentObject,
                             const std::string& filetext,
                             const std::vector<GridBus*>& busList)
    {
        mArray objectArrayData;
        for (const auto& namepair : ARRAY_IDENTIFIERS) {
            auto arrayStart = filetext.find(namepair.first);
            if (arrayStart != std::string::npos) {
                const size_t arrayEquals = filetext.find_first_of('=', arrayStart);
                readMatlabArray(filetext, arrayEquals + 1, objectArrayData);
                namepair.second(parentObject, objectArrayData, busList);
            }
        }
    }

    void loadPSATBusArray(CoreObject* parentObject,
                          double basepower,
                          const mArray& buses,
                          const mArray& swingBuses,
                          const mArray& pvBuses,
                          const mArray& pqBuses,
                          const stringVec& busnames,
                          std::vector<GridBus*>& busList)
    {
        for (size_t busIndex = 0; busIndex < buses.size(); ++busIndex) {
            auto ind1 = static_cast<index_t>(buses[busIndex][0]);
            if (std::cmp_greater_equal(ind1, busList.size())) {
                busList.resize((ind1 * 2) + 1);
            }
            auto* bus = busList[ind1];
            if (bus == nullptr) {
                busList[ind1] = new AcBus(busnames[busIndex]);
                bus = busList[ind1];
                bus->set("basepower", basepower);
                bus->setUserID(static_cast<int>(ind1));
                parentObject->add(bus);
            }

            bus->set("basevoltage", buses[busIndex][1]);
            if (buses[busIndex].size() > 2) {
                bus->set("voltage", buses[busIndex][2]);
            }
            if (buses[busIndex].size() > 3) {
                bus->set("angle", buses[busIndex][3]);
            }
        }

        for (const auto& swInfo : swingBuses) {
            auto ind1 = static_cast<size_t>(swInfo[0]);
            auto* bus = busList[ind1];
            bus->set("type", "swing");
            bus->set("vtarget", swInfo[3]);
            bus->set("atarget", swInfo[4]);

            auto* gen = new Generator();
            bus->add(gen);
            if (swInfo.size() >= 7) {
                gen->set("qmax", swInfo[5]);
                gen->set("qmin", swInfo[6]);
            }
            if (swInfo.size() >= 9) {
                bus->set("vmax", swInfo[7]);
                bus->set("vmin", swInfo[8]);
            }
            if (swInfo.size() >= 10) {
                gen->set("p", swInfo[9]);
            }
        }
        for (const auto& pvInfo : pvBuses) {
            auto ind1 = static_cast<size_t>(pvInfo[0]);
            auto* bus = busList[ind1];
            bus->set("type", "PV");
            bus->set("vtarget", pvInfo[4]);
            auto* gen = new Generator;
            bus->add(gen);
            gen->set("p", pvInfo[3]);

            if (pvInfo.size() >= 7) {
                gen->set("qmax", pvInfo[5]);
                gen->set("qmin", pvInfo[6]);
            }
            if (pvInfo.size() >= 9) {
                bus->set("vmax", pvInfo[7]);
                bus->set("vmin", pvInfo[8]);
            }
            if (pvInfo.size() >= 10) {
            }
        }

        for (const auto& pqInfo : pqBuses) {
            auto ind1 = static_cast<size_t>(pqInfo[0]);
            auto* bus = busList[ind1];
            const auto activePower = pqInfo[3];
            const auto reactivePower = pqInfo[4];
            if ((activePower != 0.0) || (reactivePower != 0.0)) {
                auto* load = new ZipLoad(activePower, reactivePower);
                bus->add(load);
            }

            if (pqInfo.size() >= 7) {
                bus->set("vmax", pqInfo[5]);
                bus->set("vmin", pqInfo[6]);
            }
        }
    }

    void loadPSATGenArray(CoreObject* /*parentObject*/,
                          const mArray& gens,
                          const std::vector<GridBus*>& busList)
    {
        using units::MVAR;
        using units::MW;

        for (const auto& genInfo : gens) {
            auto ind1 = static_cast<size_t>(genInfo[0]);
            GridBus* bus = busList[ind1];
            auto* gen = new Generator();
            bus->add(gen);
            if (genInfo[1] != 0) {
                gen->set("p", genInfo[1], MW);
            }
            if (genInfo[2] != 0) {
                gen->set("q", genInfo[2], MVAR);
            }
            gen->set("qmax", genInfo[3], MVAR);
            gen->set("qmin", genInfo[4], MVAR);
            bus->set("vtarget", genInfo[5]);
            if (genInfo[6] > 0.0) {
                gen->set("mbase", genInfo[6], MVAR);
            }
            if (genInfo[7] <= 0) {
                gen->disable();
            }
            if (genInfo[8] != 0) {
                gen->set("pmax", genInfo[8], MW);
            }

            if (genInfo[9] != 0) {
                gen->set("pmin", genInfo[9], MW);
            }
        }
    }

    /*
    Column Variable Description Unit
    1 - Bus number int
    2 Sn Power rating MVA
    3 PS0 Forecasted active power pu
    4 PSmax Maximum power bid pu
    5 PSmin Minimum power bid pu
    6 PS Actual active power bid pu
    7 CP0 Fixed cost(active power) $ / h
    8 CP1 Proportional cost(active power) $ / MWh
    9 CP2 Quadratic cost(active power) $ / MW2h
    10 CQ0 Fixed cost(reactive power) $ / h
    11 CQ1 Proportional cost(reactive power) $ / MVArh
    12 CQ2 Quadratic cost(reactive power) $ / MVAr2h
    13 u Commitment variable boolean
    14 kTB Tie breaking cost $ / MWh
    */
    /* Branch data
        Column Variable Description Unit
        1 k From Bus int
        2 m To Bus int
        3 Sn Power rating MVA
        4 Vn Voltage rating kV
        5 fn Frequency rating Hz
        6 - not used -
        7 kT Primary and secondary voltage ratio kV/kV
        8 r Resistance pu
        9 x Reactance pu
        10 - not used -
        y 11 a Fixed tap ratio pu/pu
        y 12  Fixed phase shift deg
        y 13 Imax Current limit pu
        y 14 Pmax Active power limit pu
        y 15 Smax Apparent power limit pu
        */

    void loadPSATLinkArray(CoreObject* parentObject,
                           const mArray& links,
                           const std::vector<GridBus*>& busList)
    {
        for (const auto& lnkInfo : links) {
            auto ind1 = static_cast<index_t>(lnkInfo[0]);
            auto* bus1 = busList[ind1];

            auto ind2 = static_cast<index_t>(lnkInfo[1]);
            auto* bus2 = busList[ind2];
            auto* lnk = new AcLine();

            lnk->updateBus(bus1, 1);
            lnk->updateBus(bus2, 2);
            parentObject->add(lnk);
            const bool isTransformer = (lnkInfo[6] != 0.0);

            if (isTransformer) {
                lnk->set("r", lnkInfo[7]);
                lnk->set("x", lnkInfo[8]);
            } else {
                const double length = lnkInfo[5];
                if (length > 0.0) {
                    lnk->set("r", lnkInfo[7] * length);
                    lnk->set("x", lnkInfo[8] * length);
                    lnk->set("b", lnkInfo[9] * length);
                } else {
                    lnk->set("r", lnkInfo[7]);
                    lnk->set("x", lnkInfo[8]);
                    lnk->set("b", lnkInfo[9]);
                }
            }

            if (lnkInfo[5] != 0.0) {
                lnk->set("ratinga", lnkInfo[2], units::MVAR);
            }

            if (lnkInfo.size() >= 11) {
                if (lnkInfo[10] > 0.05)  // just make sure list a tap
                {
                    lnk->set("tap", lnkInfo[10]);
                }
            }
            if (lnkInfo.size() >= 12) {
                if (lnkInfo[11] != 0) {
                    lnk->set("tapangle", lnkInfo[11], units::deg);
                }
            }
        }
    }

    void loadPSATLinkArrayB(CoreObject* parentObject,
                            const mArray& links,
                            const std::vector<GridBus*>& busList)
    {
        for (const auto& lnkInfo : links) {
            auto ind1 = static_cast<index_t>(lnkInfo[0]);
            auto* bus1 = busList[ind1];
            auto ind2 = static_cast<index_t>(lnkInfo[1]);
            auto* bus2 = busList[ind2];
            auto* lnk = new AcLine();
            lnk->updateBus(bus1, 1);
            lnk->updateBus(bus2, 2);
            parentObject->add(lnk);
            const bool isTransformer = (lnkInfo[6] != 0.0);

            if (isTransformer) {
                lnk->set("r", lnkInfo[7]);
                lnk->set("x", lnkInfo[8]);
            } else {
                const double length = lnkInfo[5];
                lnk->set("r", lnkInfo[7] * length);
                lnk->set("x", lnkInfo[8] * length);
                lnk->set("b", lnkInfo[9] * length);
            }

            if (lnkInfo[5] != 0) {
                lnk->set("ratinga", lnkInfo[2], units::MVAR);
            }

            if (lnkInfo.size() >= 11) {
                if (lnkInfo[10] > 0.05)  // just make sure list a tap
                {
                    lnk->set("tap", lnkInfo[10]);
                }
            }
            if (lnkInfo.size() >= 12) {
                if (lnkInfo[11] != 0.0) {
                    lnk->set("tapangle", lnkInfo[11], units::deg);
                }
            }
        }
    }

    void loadPSATShuntArray(CoreObject* /*parentObject*/,
                            const mArray& shunts,
                            const std::vector<GridBus*>& busList)
    {
        for (const auto& shuntInfo : shunts) {
            auto ind1 = static_cast<size_t>(shuntInfo[0]);
            auto* bus1 = busList[ind1];

            auto* load = bus1->getLoad();
            if (load == nullptr) {
                load = new ZipLoad();
                bus1->add(load);
            }

            const double conductance = shuntInfo[4];
            const double susceptance = shuntInfo[5];
            if (conductance != 0.0) {
                load->set("yp", conductance);
            }
            if (susceptance != 0.0) {
                load->set("yq", susceptance);
            }
            if (shuntInfo.size() > 6) {
                if (shuntInfo[6] == 0) {
                    load->disable();
                }
            }
        }
    }
    /*
    Column Variable Description Unit
    1 k Bus number(from) int
    2 m Bus number(to) int
    3 Sn Power rating MVA
    4 Vn Voltage rating kV
    5 fn Frequency rating Hz
    6 kT Nominal tap ratio kV / kV
    7 H Integral deviation pu
    8 K Inverse time constant 1 / s
    9 mmax Max tap ratio pu / pu
    10 mmin Min tap ratio pu / pu
    11 m Tap ratio step pu / pu
    12 Vref(Qref) Reference voltage(power) pu
    13 xT Transformer reactance pu
    14 rT Transformer resistance pu
    15 r Remote control bus number int
    16 - Control
    1 Secondary voltage Vm
    2 Reactive power Qm
    3 Remote voltage Vr
    int
    17 u Connection status{ 0, 1 }
    */
    void loadPSATLTCArray(CoreObject* parentObject,
                          const mArray& ltcData,
                          const std::vector<GridBus*>& busList)
    {
        for (const auto& ltcInfo : ltcData) {
            auto ind1 = static_cast<index_t>(ltcInfo[0]);
            GridBus* bus1 = busList[ind1];
            auto ind2 = static_cast<index_t>(ltcInfo[1]);
            GridBus* bus2 = busList[ind2];
            auto* lnk = new links::AdjustableTransformer();

            lnk->updateBus(bus1, 1);
            lnk->updateBus(bus2, 2);
            parentObject->add(lnk);
            lnk->set("r", ltcInfo[13]);
            lnk->set("x", ltcInfo[12]);
            lnk->set("mintap", ltcInfo[9]);
            lnk->set("maxtap", ltcInfo[8]);
            //    lnk->set("tap", ltcInfo[10]);
            lnk->set("stepsize", ltcInfo[11]);
            switch (static_cast<int>(ltcInfo[15])) {
                case 1:  // secondary voltage
                    lnk->set("mode", "v");
                    lnk->set("vtarget", ltcInfo[11]);
                    break;
                case 2:  // reactive power
                    lnk->set("mode", "mvar");
                    lnk->set("qtarget", ltcInfo[11]);
                    break;
                case 3:  // remote control voltage bus
                    lnk->set("mode", "v");
                    lnk->set("vtarget", ltcInfo[11]);
                    lnk->setControlBus(busList[static_cast<index_t>(ltcInfo[14])]);
                    break;
                default:
                    break;
            }
            // check if lnk is enabled
            if (ltcInfo.size() == 18) {
                if (ltcInfo[17] < 0.1)  // lnk is disabled
                {
                    lnk->disconnect();
                }
            } else {
                if (ltcInfo[16] < 0.1)  // lnk is disabled
                {
                    lnk->disconnect();
                }
            }
        }
    }
    /*
    1 k Bus number(from) int
    2 m Bus number(to) int
    3 Sn Power rating MVA
    4 Vn1 Primary voltage rating kV
    5 Vn2 Secondary voltage rating kV
    6 fn Frequency rating Hz
    7 Tm Measurement time constant s
    8 Kp Proportional gain -
    9 Ki Integral gain -
    10 Pref Reference power pu
    11 rT Transformer resistance pu
    12 xT Transformer reactance pu
    13 ÃƒÅ½Ã‚Â±max Maximum phase angle rad
    14 ÃƒÅ½Ã‚Â±min Minimum phase angle rad
    15 m Transformer fixed tap ratio pu / pu
    16 u Connection status{ 0, 1 }
    */
    void loadPSATPHSArray(CoreObject* parentObject,
                          const mArray& phs,
                          const std::vector<GridBus*>& busList)
    {
        for (const auto& phsInfo : phs) {
            auto ind1 = static_cast<index_t>(phsInfo[0]);
            auto* bus1 = busList[ind1];
            auto ind2 = static_cast<index_t>(phsInfo[1]);
            auto* bus2 = busList[ind2];
            auto* lnk = new links::AdjustableTransformer();

            lnk->updateBus(bus1, 1);
            lnk->updateBus(bus2, 2);
            parentObject->add(lnk);
            lnk->set("r", phsInfo[10]);
            lnk->set("x", phsInfo[11]);
            lnk->set("mintapangle", phsInfo[13]);
            lnk->set("maxtapangle", phsInfo[12]);
            lnk->set("tap", phsInfo[14]);

            lnk->set("mode", "mw");
            lnk->set("ptarget", phsInfo[9]);
            lnk->set("change", "continuous");
            // check if lnk is enabled
            if (phsInfo[15] < 0.1)  // lnk is disabled
            {
                lnk->disable();
            }
        }
    }
    /*
    1 - Bus number int all
    2 Sn Power rating MVA all
    3 Vn Voltage rating kV all
    4 fn Frequency rating Hz all
    5 - Machine model - all
    6 xl Leakage reactance pu all
    7 ra Armature resistance pu all
    8 xd d-axis synchronous reactance pu III, IV, V.1, V.2, V.3, VI, VIII
    9 xÃƒÂ¢Ã¢â€šÂ¬Ã‚Â²
    d d-axis transient reactance pu II, III, IV, V.1, V.2, V.3, VI, VIII
    10 xÃƒÂ¢Ã¢â€šÂ¬Ã‚Â²ÃƒÂ¢Ã¢â€šÂ¬Ã‚Â²
    d d-axis subtransient reactance pu V.2, VI, VIII
    11 TÃƒÂ¢Ã¢â€šÂ¬Ã‚Â²
    d0 d-axis open circuit transient time constant s III, IV, V.1, V.2, V.3, VI, VIII
    12 TÃƒÂ¢Ã¢â€šÂ¬Ã‚Â²ÃƒÂ¢Ã¢â€šÂ¬Ã‚Â²
    d0 d-axis open circuit subtransient time constant s V.2, VI, VIII
    13 xq q-axis synchronous reactance pu III, IV, V.1, V.2, V.3, VI, VIII
    14 xÃƒÂ¢Ã¢â€šÂ¬Ã‚Â²
    q q-axis transient reactance pu IV, V.1, VI, VIII
    15 xÃƒÂ¢Ã¢â€šÂ¬Ã‚Â²ÃƒÂ¢Ã¢â€šÂ¬Ã‚Â²
    q q-axis subtransient reactance pu V.2, VI, VIII
    16 TÃƒÂ¢Ã¢â€šÂ¬Ã‚Â²
    q0 q-axis open circuit transient time constant s IV, V.1, VI, VIII
    17 TÃƒÂ¢Ã¢â€šÂ¬Ã‚Â²ÃƒÂ¢Ã¢â€šÂ¬Ã‚Â²
    q0 q-axis open circuit subtransient time constant s V.1, V.2, VI, VIII
    18 M = 2H Mechanical starting time (2 ÃƒÆ’Ã¢â‚¬â€ inertia constant) kWs/kVA all
    19 D Damping coefficient ÃƒÂ¢Ã‹â€ Ã¢â‚¬â„¢ all
    ÃƒÂ¢Ã¢â€šÂ¬Ã‚Â  20 KÃƒÂÃ¢â‚¬Â° Speed feedback gain gain III, IV,
    V.1, V.2, VI ÃƒÂ¢Ã¢â€šÂ¬Ã‚Â  21 KP Active power feedback gain gain III, IV, V.1, V.2, VI
    ÃƒÂ¢Ã¢â€šÂ¬Ã‚Â  22 ÃƒÅ½Ã‚Â³P Active power ratio at node [0,1] all ÃƒÂ¢Ã¢â€šÂ¬Ã‚Â  23 ÃƒÅ½Ã‚Â³Q
    Reactive power ratio at node [0,1] all ÃƒÂ¢Ã¢â€šÂ¬Ã‚Â  24 TAA d-axis additional leakage time
    constant s V.2, VI, VIII ÃƒÂ¢Ã¢â€šÂ¬Ã‚Â  25 S(1.0) First saturation factor - III, IV, V.1, V.2,
    VI, VIII ÃƒÂ¢Ã¢â€šÂ¬Ã‚Â  26 S(1.2) Second saturation factor - III, IV, V.1, V.2, VI, VIII
    ÃƒÂ¢Ã¢â€šÂ¬Ã‚Â  27 nCOI Center of inertia number int all
    ÃƒÂ¢Ã¢â€šÂ¬Ã‚Â  28 u Connection status {0, 1} all
    */

    void loadPSATSynArray(CoreObject* /*parentObject*/,
                          const mArray& syn,
                          const std::vector<GridBus*>& busList)
    {
        using genmodels::GenModel3;
        using genmodels::GenModel4;
        using genmodels::GenModel5;
        using genmodels::GenModel5type2;
        using genmodels::GenModel5type3;
        using genmodels::GenModel6type2;
        using genmodels::GenModel8;

        int index = 1;
        for (const auto& genData : syn) {
            auto ind1 = static_cast<size_t>(genData[0]);
            auto* bus1 = busList[ind1];
            auto* gen = bus1->getGen(0);
            if (gen == nullptr) {
                continue;
            }
            gen->setUserID(index);
            ++index;
            auto mode = genData[4];

            GenModel* generatorModel = nullptr;
            if (mode < 2.1) {
                // second order classical model
                generatorModel = new GenModel();
            } else if (mode < 3.1) {
                // 3rd order model
                generatorModel = new GenModel3();
            } else if (mode < 4.1) {
                // 4th order model
                generatorModel = new GenModel4();
            } else if (mode < 5.15) {
                // 5th order model type 1
                generatorModel = new GenModel5();
            } else if (mode < 5.25) {
                // 5th order model type 2
                generatorModel = new GenModel5type2();
            } else if (mode < 5.35) {
                // 5th order model type 3
                generatorModel = new GenModel5type3();
            } else if (mode < 6.05) {
                // 6th order model
                generatorModel = new GenModel6type2();
            } else if (mode < 8.05) {
                // 8th order model
                generatorModel = new GenModel8();
            }
            if (generatorModel == nullptr) {
                std::cout << "genModel " << mode << " not implemented yet\n";
                continue;
            }
            generatorModel->set("rating", genData[1], units::MW);
            gen->set("basevoltage", genData[2], units::kV);
            const double leakageReactance = genData[5];
            generatorModel->set("xl", genData[5]);
            generatorModel->set("r", genData[6]);
            generatorModel->set("xdp", genData[8] - leakageReactance);
            generatorModel->set("h", genData[17] / 2.0);
            generatorModel->set("d",
                                genData[18],
                                units::puHz);  // the damping coefficient in PSAT is in puHz
            if (mode > 2.1)  // deal with the voltage speed adjustment
            {
                if (genData.size() >= 21) {
                    generatorModel->set("kw", genData[19]);
                    generatorModel->set("kp", genData[20]);
                }
                generatorModel->set("xd", genData[7] - leakageReactance);
                generatorModel->set("tdop", genData[10]);
                generatorModel->set("xq", genData[12] - leakageReactance);
            }
            if (mode >= 3.1) {
                generatorModel->set("xqp", genData[13] - leakageReactance);
                generatorModel->set("tqop", genData[15]);
            }
            if (mode > 4.9) {
                generatorModel->set("tqopp", genData[16]);
            }
            if ((mode == 5.2) || (mode >= 6)) {
                if (genData.size() >= 24) {
                    generatorModel->set("taa", genData[23]);
                }
                generatorModel->set("xdpp", genData[9] - leakageReactance);
                generatorModel->set("tdopp", genData[11]);
                generatorModel->set("xqpp", genData[14] - leakageReactance);
            }
        }
    }

    /*
    Table 16.1: Turbine Governor Type I Data Format (Tg.con)
    Column Variable Description Unit
    1 - Generator number int
    2 1 Turbine governor type int
    3 !ref Reference speed pu
    4 R Droop pu/pu
    5 Tmax Maximum turbine output pu
    6 Tmin Minimum turbine output pu
    7 Ts Governor time constant s
    8 Tc Servo time constant s
    9 T3 Transient gain time constant s
    10 T4 Power fraction time constant s
    11 T5 PSfrag replacements Reheat time constant s
    */

    void loadPSATExcArray(CoreObject* parentObject,
                          const mArray& excData,
                          const std::vector<GridBus*>& /*busList*/)
    {
        Generator* gen;
        Exciter* exciter = nullptr;
        index_t ind1;
        double mode;
        for (const auto& eData : excData) {
            ind1 = static_cast<index_t>(eData[0]);

            gen = static_cast<Generator*>(parentObject->findByUserID("gen", ind1));
            if (gen == nullptr) {
                continue;
            }

            mode = eData[1];
            if (mode < 2.1) {  // second and third order models
                exciter = new Exciter();
            }

            if (exciter == nullptr) {
                std::cout << "exciter " << mode << " not implemented yet\n";
                continue;
            }
            exciter->set("r", eData[3]);
            exciter->set("pmax", eData[4]);
            exciter->set("pmin", eData[5]);

            exciter->set("t1", eData[6]);
            exciter->set("t2", eData[7]);
            exciter->set("t3", eData[8]);
            exciter->set("t4", eData[9]);
        }
    }

    void loadPsatFaultArray(CoreObject* parentObject,
                            const mArray& fault,
                            const std::vector<GridBus*>& busList)
    {
        auto* gds = dynamic_cast<GridSimulation*>(parentObject->getRoot());
        if (gds == nullptr) {  // can't make faults if we don't have access to the simulation
            return;
        }

        for (const auto& flt : fault) {
            auto ind = static_cast<index_t>(flt[0]);
            auto* bus = busList[ind];

            auto* load = new ZipLoad("faultLoad");
            bus->add(load);

            if (flt[6] != 0) {
                auto evnt1 = std::make_shared<Event>(flt[4]);
                auto evnt2 = std::make_shared<Event>(flt[5]);
                evnt1->setTarget(load, "r");
                evnt1->setValue(flt[6]);
                evnt2->setTarget(load, "r");
                evnt2->setValue(0.0);
                gds->add(std::move(evnt1));
                gds->add(std::move(evnt2));
            }

            if (flt[7] != 0) {
                auto evnt1 = std::make_shared<Event>(flt[4]);
                auto evnt2 = std::make_shared<Event>(flt[5]);
                evnt1->setTarget(load, "x");
                evnt1->setValue(flt[7]);
                evnt2->setTarget(load, "x");
                evnt2->setValue(0.0);
                gds->add(std::move(evnt1));
                gds->add(std::move(evnt2));
            }
        }
    }

    void loadPsatPmuArray(CoreObject* parentObject,
                          const mArray& pmuData,
                          const std::vector<GridBus*>& busList)
    {
        auto* gds = dynamic_cast<GridSimulation*>(parentObject->getRoot());
        if (gds == nullptr) {  // can't add the sensors if there is no simulation
            return;
        }
    for (index_t pmuIndex = 0; std::cmp_less(pmuIndex, pmuData.size()); ++pmuIndex) {
        const auto& pmuLine = pmuData[pmuIndex];
            auto ind = static_cast<index_t>(pmuLine[0]);
            auto* bus = busList[ind];

            auto* pmu = new relays::Pmu();
            pmu->setUserID(pmuIndex + 1);
            pmu->set("samplerate", pmuLine[2]);
            pmu->set("tv", pmuLine[3]);
            pmu->set("ttheta", pmuLine[4]);
            pmu->setSource(bus);
            if ((pmuLine.size() > 5) && (pmuLine[5] < 0.1)) {
                pmu->disable();
            }
            gds->add(pmu);
        }
    }

    void loadPsatBreakerArray(CoreObject* parentObject,
                              const mArray& breakerData,
                              const std::vector<GridBus*>& /*busList*/)
    {
        auto* gds = dynamic_cast<GridSimulation*>(parentObject->getRoot());
        if (gds == nullptr) {  // can't make faults if we don't have access to the simulation
            return;
        }
        for (const auto& brk : breakerData) {
            auto ind = static_cast<index_t>(brk[0]);
            auto* lnk = static_cast<Link*>(parentObject->findByUserID("link", ind));
            double status = 1.0;
            if (brk[5] < 0.1) {
                lnk->disable();
                status = 0.0;
            }
            auto evnt1 = std::make_shared<Event>(brk[6]);
            auto evnt2 = std::make_shared<Event>(brk[7]);
            evnt1->setTarget(lnk, "enabled");
            evnt1->setValue((status < 0.1) ? 1.0 : 0.0);
            evnt2->setTarget(lnk, "enabled");
            evnt2->setValue(status);
            gds->add(std::move(evnt1));
            gds->add(std::move(evnt2));
        }
    }

    void loadPsatMotorArray(CoreObject* /*parentObject*/,
                            const mArray& motorData,
                            const std::vector<GridBus*>& busList)
    {
        for (const auto& mtrline : motorData) {
            auto ind1 = static_cast<index_t>(mtrline[0]);
            GridBus* bus1 = busList[ind1];

            auto* motor = new loads::MotorLoad();
            bus1->add(motor);
            // TODO(phlpt): Add parameters.
        }
    }

}  // namespace
}  // namespace griddyn
