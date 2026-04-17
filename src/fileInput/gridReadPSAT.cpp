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
#include "griddyn/links/acLine.h"
#include "griddyn/links/adjustableTransformer.h"
#include "griddyn/loads/motorLoad.h"
#include "griddyn/loads/zipLoad.h"
#include "griddyn/primary/acBus.h"
#include "griddyn/relays/pmu.h"
#include "readerHelper.h"

#ifdef ENABLE_OPTIMIZATION_LIBRARY
#    include "optimization/gridDynOpt.h"
#    include "optimization/models/gridGenOpt.h"
#else
#    include "griddyn/simulation/gridSimulation.h"
#endif

#include "griddyn/Exciter.h"
#include "griddyn/Stabilizer.h"
#include "griddyn/genmodels/otherGenModels.h"
#include "griddyn/governors/GovernorTypes.h"
#include <cstdlib>
#include <fstream>
#include <iostream>
#include <memory>
#include <string>
#include <utility>
#include <vector>

namespace griddyn {
// using namespace units;

void loadPSATBusArray(coreObject* parentObject,
                      double basepower,
                      const mArray& buses,
                      const mArray& SW,
                      const mArray& PV,
                      const mArray& PQ,
                      const stringVec& busnames,
                      std::vector<gridBus*>& busList);
void loadPSATGenArray(coreObject* parentObject,
                      const mArray& gens,
                      const std::vector<gridBus*>& busList);
void loadPSATLinkArray(coreObject* parentObject,
                       const mArray& lnks,
                       const std::vector<gridBus*>& busList);
void loadPSATLinkArrayB(coreObject* parentObject,
                        const mArray& lnks,
                        const std::vector<gridBus*>& busList);
void loadPSATShuntArray(coreObject* parentObject,
                        const mArray& shunts,
                        const std::vector<gridBus*>& busList);
void loadPSATLTCArray(coreObject* parentObject,
                      const mArray& ltc,
                      const std::vector<gridBus*>& busList);
void loadPSATPHSArray(coreObject* parentObject,
                      const mArray& phs,
                      const std::vector<gridBus*>& busList);
void loadPSATSynArray(coreObject* parentObject,
                      const mArray& syn,
                      const std::vector<gridBus*>& busList);
void loadPSATExcArray(coreObject* parentObject,
                      const mArray& excData,
                      const std::vector<gridBus*>& busList);
void loadPSATTgArray(coreObject* parentObject,
                     const mArray& tg,
                     const std::vector<gridBus*>& busList);
void loadPsatFaultArray(coreObject* parentObject,
                        const mArray& fault,
                        const std::vector<gridBus*>& busList);
void loadPsatBreakerArray(coreObject* parentObject,
                          const mArray& brkr,
                          const std::vector<gridBus*>& busList);
void loadPsatSupplyArray(coreObject* parentObject,
                         const mArray& genCost,
                         const std::vector<gridBus*>& busList);
void loadPsatMotorArray(coreObject* parentObject,
                        const mArray& mtr,
                        const std::vector<gridBus*>& busList);
/** load a PSAT PMU data*/
void loadPsatPmuArray(coreObject* parentObject,
                      const mArray& pmuData,
                      const std::vector<gridBus*>& busList);
void loadOtherObjectData(coreObject* parentObject,
                         const std::string& filetext,
                         const std::vector<gridBus*>& busList);
static const std::vector<
    std::pair<std::string, void (*)(coreObject*, const mArray&, const std::vector<gridBus*>&)>>
    arrayIdentifiers{
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
        //{ "Supply.con",loadPsatSupplyArray },
        {"Not.con", loadPsatMotorArray},
        {"Pmu.con", loadPsatPmuArray},
    };

void loadPSAT(coreObject* parentObject, const std::string& filetext, const basicReaderInfo& bri)
{
    double basepower = 100;
    // std::string tstr;
    mArray M1, SW, PQ, PV;
    std::vector<gridBus*> busList;
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
    gridSimulation::resetObjectCounters();  // reset all the object counters to 0 to make sure all
                                            // the numbers
    // match up

    stringVec Vnames;
    auto A = filetext.find("Varname.bus");
    if (A != std::string::npos) {
        size_t B = filetext.find_first_of('=', A);
        Vnames = readMatlabCellArray(filetext, B + 1);
        nmfnd = true;
    }
    if (!nmfnd) {
        A = filetext.find("Bus.names");
        if (A != std::string::npos) {
            size_t B = filetext.find_first_of('=', A);
            Vnames = readMatlabCellArray(filetext, B + 1);
            nmfnd = true;
        }
    }
    if (nmfnd) {
        if (!(bri.prefix.empty())) {
            for (auto& vk : Vnames) {
                vk = bri.prefix + '_' + vk;
            }
        }
    }
    // now find the bus structure
    A = filetext.find("Bus.con");
    if (A != std::string::npos) {
        size_t B = filetext.find_first_of('=', A);
        readMatlabArray(filetext, B + 1, M1);
        readMatlabArray("SW.con", filetext, SW);

        if (Vnames.size() != M1.size()) {
            if (Vnames.empty()) {
                Vnames.resize(M1.size());
                for (stringVec::size_type kk = 0; kk < M1.size(); ++kk) {
                    if (bri.prefix.empty()) {
                        Vnames[kk] = "Bus-" + std::to_string(M1[kk][0]);
                    } else {
                        Vnames[kk] = bri.prefix + "_Bus-" + std::to_string(M1[kk][0]);
                    }
                }
            } else {
                std::cout
                    << "WARNING: number of bus names does not match the number of buses listed\n";
            }
        }
        readMatlabArray("PV.con", filetext, PV);
        readMatlabArray("PQ.con", filetext, PQ);
        loadPSATBusArray(parentObject, basepower, M1, SW, PV, PQ, Vnames, busList);
    }
    loadOtherObjectData(parentObject, filetext, busList);
}

void loadOtherObjectData(coreObject* parentObject,
                         const std::string& filetext,
                         const std::vector<gridBus*>& busList)
{
    mArray M1;
    for (auto& namepair : arrayIdentifiers) {
        auto A = filetext.find(namepair.first);
        if (A != std::string::npos) {
            size_t B = filetext.find_first_of('=', A);
            readMatlabArray(filetext, B + 1, M1);
            namepair.second(parentObject, M1, busList);
        }
    }
}

void loadPSATBusArray(coreObject* parentObject,
                      double basepower,
                      const mArray& buses,
                      const mArray& SW,
                      const mArray& PV,
                      const mArray& PQ,
                      const stringVec& busnames,
                      std::vector<gridBus*>& busList)
{
    for (size_t kk = 0; kk < buses.size(); ++kk) {
        auto ind1 = static_cast<index_t>(buses[kk][0]);
        if (ind1 >= static_cast<index_t>(busList.size())) {
            busList.resize(ind1 * 2 + 1);
        }
        auto bus = busList[ind1];
        if (bus == nullptr) {
            busList[ind1] = new acBus(busnames[kk]);
            bus = busList[ind1];
            bus->set("basepower", basepower);
            bus->setUserID(static_cast<int>(ind1));
            parentObject->add(bus);
        }

        bus->set("basevoltage", buses[kk][1]);
        if (buses[kk].size() > 2) {
            bus->set("voltage", buses[kk][2]);
        }
        if (buses[kk].size() > 3) {
            bus->set("angle", buses[kk][3]);
        }
    }

    for (auto& swInfo : SW) {
        auto ind1 = static_cast<size_t>(swInfo[0]);
        auto bus = busList[ind1];
        bus->set("type", "swing");
        bus->set("vtarget", swInfo[3]);
        bus->set("atarget", swInfo[4]);

        auto gen = new Generator();
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
    for (auto& pvInfo : PV) {
        auto ind1 = static_cast<size_t>(pvInfo[0]);
        auto bus = busList[ind1];
        bus->set("type", "PV");
        bus->set("vtarget", pvInfo[4]);
        auto gen = new Generator;
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

    for (auto& pqInfo : PQ) {
        auto ind1 = static_cast<size_t>(pqInfo[0]);
        auto bus = busList[ind1];
        auto P = pqInfo[3];
        auto Q = pqInfo[4];
        if ((P != 0.0) || (Q != 0.0)) {
            auto ld = new zipLoad(P, Q);
            bus->add(ld);
        }

        if (pqInfo.size() >= 7) {
            bus->set("vmax", pqInfo[5]);
            bus->set("vmin", pqInfo[6]);
        }
    }
}

void loadPSATGenArray(coreObject* /*parentObject*/,
                      const mArray& gens,
                      const std::vector<gridBus*>& busList)
{
    using units::MVAR;
    using units::MW;

    for (auto& genInfo : gens) {
        auto ind1 = static_cast<size_t>(genInfo[0]);
        gridBus* bus = busList[ind1];
        Generator* gen = new Generator();
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
#ifndef ENABLE_OPTIMIZATION_LIBRARY
void loadPsatSupplyArray(coreObject* /*parentObject*/,
                         const mArray& /*genCost*/,
                         const std::vector<gridBus*>& /*busList*/)
{
}
#else
void loadPsatSupplyArray(coreObject* parentObject,
                         const mArray& genCost,
                         const std::vector<gridBus*>& busList)
{
    auto gdo = dynamic_cast<gridDynOptimization*>(parentObject->getRoot());
    if (gdo == nullptr) {
        return;
    }
    for (auto& genLine : genCost) {
        auto bus = busList[static_cast<size_t>(genLine[0])];
        auto gen = bus->getGen();
        if (gen != nullptr) {
            continue;
        }
        auto go = new gridGenOpt(gen);
        auto oo = gdo->makeOptObjectPath(bus);
        oo->add(go);

        if (genLine[2] != 0.0) {
            go->set("forecast", genLine[2], MW);
        }
        if (genLine[3] != 0.0) {
            go->set("pmax", genLine[3], MW);
        }
        if (genLine[4] != 0.0) {
            go->set("pmin", genLine[4], MW);
        }

        if (genLine[6] > 0.0) {
            go->set("constantp", genLine[6], currency / hr);
        }
        if (genLine[9] > 0.0) {
            go->set("constantq", genLine[9], currency / hr);
        }
        if (genLine[7] > 0.0) {
            go->set("linearp", genLine[7], currency / MW / hr);
        }
        if (genLine[10] > 0.0) {
            go->set("linearq", genLine[10], currency / MVAR / hr);
        }
        if (genLine[8] != 0.0) {
            go->set("quadraticp", genLine[8], currency / (MW.pow(2)) / hr);
        }
        if (genLine[11] != 0.0) {
            go->set("quadraticq", genLine[11], currency / (MVAR.pow(2)) / hr);
        }
        if (genLine[13] != 0.0) {
            go->set("penalty", genLine[13], currency / MW / hr);
        }
    }
}
#endif  // ENABLE_OPTIMIZATION_LIBRARY

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

void loadPSATLinkArray(coreObject* parentObject,
                       const mArray& lnks,
                       const std::vector<gridBus*>& busList)
{
    ;

    for (auto& lnkInfo : lnks) {
        auto ind1 = static_cast<index_t>(lnkInfo[0]);
        auto bus1 = busList[ind1];

        auto ind2 = static_cast<index_t>(lnkInfo[1]);
        auto bus2 = busList[ind2];
        auto lnk = new acLine();

        lnk->updateBus(bus1, 1);
        lnk->updateBus(bus2, 2);
        parentObject->add(lnk);
        bool tx = (lnkInfo[6] != 0.0);

        if (tx) {
            lnk->set("r", lnkInfo[7]);
            lnk->set("x", lnkInfo[8]);
        } else {
            double len = lnkInfo[5];
            if (len > 0.0) {
                lnk->set("r", lnkInfo[7] * len);
                lnk->set("x", lnkInfo[8] * len);
                lnk->set("b", lnkInfo[9] * len);
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

void loadPSATLinkArrayB(coreObject* parentObject,
                        const mArray& lnks,
                        const std::vector<gridBus*>& busList)
{
    for (auto& lnkInfo : lnks) {
        auto ind1 = static_cast<index_t>(lnkInfo[0]);
        auto bus1 = busList[ind1];
        auto ind2 = static_cast<index_t>(lnkInfo[1]);
        auto bus2 = busList[ind2];
        auto lnk = new acLine();
        lnk->updateBus(bus1, 1);
        lnk->updateBus(bus2, 2);
        parentObject->add(lnk);
        bool tx = (lnkInfo[6] != 0.0);

        if (tx) {
            lnk->set("r", lnkInfo[7]);
            lnk->set("x", lnkInfo[8]);
        } else {
            double len = lnkInfo[5];
            lnk->set("r", lnkInfo[7] * len);
            lnk->set("x", lnkInfo[8] * len);
            lnk->set("b", lnkInfo[9] * len);
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

void loadPSATShuntArray(coreObject* /*parentObject*/,
                        const mArray& shunts,
                        const std::vector<gridBus*>& busList)
{
    for (auto& shuntInfo : shunts) {
        auto ind1 = static_cast<size_t>(shuntInfo[0]);
        auto bus1 = busList[ind1];

        auto ld = bus1->getLoad();
        if (ld == nullptr) {
            ld = new zipLoad();
            bus1->add(ld);
        }

        double g = shuntInfo[4];
        double b = shuntInfo[5];
        if (g != 0.0) {
            ld->set("yp", g);
        }
        if (b != 0.0) {
            ld->set("yq", b);
        }
        if (shuntInfo.size() > 6) {
            if (shuntInfo[6] == 0) {
                ld->disable();
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
void loadPSATLTCArray(coreObject* parentObject,
                      const mArray& ltc,
                      const std::vector<gridBus*>& busList)
{
    for (auto& ltcInfo : ltc) {
        auto ind1 = static_cast<index_t>(ltcInfo[0]);
        gridBus* bus1 = busList[ind1];
        auto ind2 = static_cast<index_t>(ltcInfo[1]);
        gridBus* bus2 = busList[ind2];
        auto lnk = new links::adjustableTransformer();

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
13 αmax Maximum phase angle rad
14 αmin Minimum phase angle rad
15 m Transformer fixed tap ratio pu / pu
16 u Connection status{ 0, 1 }
*/
void loadPSATPHSArray(coreObject* parentObject,
                      const mArray& phs,
                      const std::vector<gridBus*>& busList)
{
    for (auto& phsInfo : phs) {
        auto ind1 = static_cast<index_t>(phsInfo[0]);
        auto bus1 = busList[ind1];
        auto ind2 = static_cast<index_t>(phsInfo[1]);
        auto bus2 = busList[ind2];
        auto lnk = new links::adjustableTransformer();

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
9 x′
d d-axis transient reactance pu II, III, IV, V.1, V.2, V.3, VI, VIII
10 x′′
d d-axis subtransient reactance pu V.2, VI, VIII
11 T′
d0 d-axis open circuit transient time constant s III, IV, V.1, V.2, V.3, VI, VIII
12 T′′
d0 d-axis open circuit subtransient time constant s V.2, VI, VIII
13 xq q-axis synchronous reactance pu III, IV, V.1, V.2, V.3, VI, VIII
14 x′
q q-axis transient reactance pu IV, V.1, VI, VIII
15 x′′
q q-axis subtransient reactance pu V.2, VI, VIII
16 T′
q0 q-axis open circuit transient time constant s IV, V.1, VI, VIII
17 T′′
q0 q-axis open circuit subtransient time constant s V.1, V.2, VI, VIII
18 M = 2H Mechanical starting time (2 × inertia constant) kWs/kVA all
19 D Damping coefficient − all
† 20 Kω Speed feedback gain gain III, IV, V.1, V.2, VI
† 21 KP Active power feedback gain gain III, IV, V.1, V.2, VI
† 22 γP Active power ratio at node [0,1] all
† 23 γQ Reactive power ratio at node [0,1] all
† 24 TAA d-axis additional leakage time constant s V.2, VI, VIII
† 25 S(1.0) First saturation factor - III, IV, V.1, V.2, VI, VIII
† 26 S(1.2) Second saturation factor - III, IV, V.1, V.2, VI, VIII
† 27 nCOI Center of inertia number int all
† 28 u Connection status {0, 1} all
*/

void loadPSATSynArray(coreObject* /*parentObject*/,
                      const mArray& syn,
                      const std::vector<gridBus*>& busList)
{
    using genmodels::GenModel3;
    using genmodels::GenModel4;
    using genmodels::GenModel5;
    using genmodels::GenModel5type2;
    using genmodels::GenModel5type3;
    using genmodels::GenModel6type2;
    using genmodels::GenModel8;

    int index = 1;
    for (auto genData : syn) {
        auto ind1 = static_cast<size_t>(genData[0]);
        auto bus1 = busList[ind1];
        auto gen = bus1->getGen(0);
        if (gen == nullptr) {
            continue;
        }
        gen->setUserID(index);
        ++index;
        auto mode = genData[4];

        GenModel* gm = nullptr;
        if (mode < 2.1) {
            // second order classical model
            gm = new GenModel();
        } else if (mode < 3.1) {
            // 3rd order model
            gm = new GenModel3();
        } else if (mode < 4.1) {
            // 4th order model
            gm = new GenModel4();
        } else if (mode < 5.15) {
            // 5th order model type 1
            gm = new GenModel5();
        } else if (mode < 5.25) {
            // 5th order model type 2
            gm = new GenModel5type2();
        } else if (mode < 5.35) {
            // 5th order model type 3
            gm = new GenModel5type3();
        } else if (mode < 6.05) {
            // 6th order model
            gm = new GenModel6type2();
        } else if (mode < 8.05) {
            // 8th order model
            gm = new GenModel8();
        }
        if (gm == nullptr) {
            std::cout << "genModel " << mode << " not implemented yet\n";
            continue;
        }
        gm->set("rating", genData[1], units::MW);
        gen->set("basevoltage", genData[2], units::kV);
        double xl = genData[5];
        gm->set("xl", genData[5]);
        gm->set("r", genData[6]);
        gm->set("xdp", genData[8] - xl);
        gm->set("h", genData[17] / 2.0);
        gm->set("d", genData[18], units::puHz);  // the damping coefficient in PSAT is in puHz
        if (mode > 2.1)  // deal with the voltage speed adjustment
        {
            if (genData.size() >= 21) {
                gm->set("kw", genData[19]);
                gm->set("kp", genData[20]);
            }
            gm->set("xd", genData[7] - xl);
            gm->set("tdop", genData[10]);
            gm->set("xq", genData[12] - xl);
        }
        if (mode >= 3.1) {
            gm->set("xqp", genData[13] - xl);
            gm->set("tqop", genData[15]);
        }
        if (mode > 4.9) {
            gm->set("tqopp", genData[16]);
        }
        if ((mode == 5.2) || (mode >= 6)) {
            if (genData.size() >= 24) {
                gm->set("taa", genData[23]);
            }
            gm->set("xdpp", genData[9] - xl);
            gm->set("tdopp", genData[11]);
            gm->set("xqpp", genData[14] - xl);
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

void loadPSATTgArray(coreObject* parentObject,
                     const mArray& tg,
                     const std::vector<gridBus*>& /*busList*/)
{
    Governor* gm = nullptr;
    // gridBus *bus1;
    index_t ind1;
    double mode;
    double temp;
    for (auto govData : tg) {
        temp = govData[0];
        ind1 = static_cast<index_t>(temp);

        auto gen = static_cast<Generator*>(parentObject->findByUserID("gen", ind1));
        if (gen == nullptr) {
            continue;
        }
        mode = govData[1];
        if (mode < 1.1) {
            // second order classical model
            gm = new Governor();
        } else if (mode < 2.1) {
            // 3rd order model
            gm = new governors::GovernorIeeeSimple();
        }

        if (gm == nullptr) {
            std::cout << "governor " << mode << " not implemented yet\n";
            continue;
        }
        gm->set("r", govData[3]);
        gm->set("pmax", govData[4]);
        gm->set("pmin", govData[5]);

        gm->set("t1", govData[6]);
        gm->set("t2", govData[7]);
        gm->set("t3", govData[8]);
        gm->set("t4", govData[9]);
    }
}

void loadPSATExcArray(coreObject* parentObject,
                      const mArray& excData,
                      const std::vector<gridBus*>& /*busList*/)
{
    Generator* gen;
    Exciter* gm = nullptr;
    index_t ind1;
    double mode;
    for (auto eData : excData) {
        ind1 = static_cast<index_t>(eData[0]);

        gen = static_cast<Generator*>(parentObject->findByUserID("gen", ind1));
        if (gen == nullptr) {
            continue;
        }

        mode = eData[1];
        if (mode < 1.1) {  // second order classical model
            gm = new Exciter();
        } else if (mode < 2.1) {  // 3rd order model
            gm = new Exciter();
        }

        if (gm == nullptr) {
            std::cout << "exciter " << mode << " not implemented yet\n";
            continue;
        }
        gm->set("r", eData[3]);
        gm->set("pmax", eData[4]);
        gm->set("pmin", eData[5]);

        gm->set("t1", eData[6]);
        gm->set("t2", eData[7]);
        gm->set("t3", eData[8]);
        gm->set("t4", eData[9]);
    }
}

void loadPsatFaultArray(coreObject* parentObject,
                        const mArray& fault,
                        const std::vector<gridBus*>& busList)
{
    auto gds = dynamic_cast<gridSimulation*>(parentObject->getRoot());
    if (gds == nullptr) {  // can't make faults if we don't have access to the simulation
        return;
    }

    for (auto& flt : fault) {
        auto ind = static_cast<index_t>(flt[0]);
        auto bus = busList[ind];

        auto ld = new zipLoad("faultLoad");
        bus->add(ld);

        if (flt[6] != 0) {
            auto evnt1 = std::make_shared<Event>(flt[4]);
            auto evnt2 = std::make_shared<Event>(flt[5]);
            evnt1->setTarget(ld, "r");
            evnt1->setValue(flt[6]);
            evnt2->setTarget(ld, "r");
            evnt2->setValue(0.0);
            gds->add(std::move(evnt1));
            gds->add(std::move(evnt2));
        }

        if (flt[7] != 0) {
            auto evnt1 = std::make_shared<Event>(flt[4]);
            auto evnt2 = std::make_shared<Event>(flt[5]);
            evnt1->setTarget(ld, "x");
            evnt1->setValue(flt[7]);
            evnt2->setTarget(ld, "x");
            evnt2->setValue(0.0);
            gds->add(std::move(evnt1));
            gds->add(std::move(evnt2));
        }
    }
}

void loadPsatPmuArray(coreObject* parentObject,
                      const mArray& pmuData,
                      const std::vector<gridBus*>& busList)
{
    auto* gds = dynamic_cast<gridSimulation*>(parentObject->getRoot());
    if (gds == nullptr) {  // can't add the sensors if there is no simulation
        return;
    }
    index_t count = 1;
    for (auto& pmuLine : pmuData) {
        auto ind = static_cast<index_t>(pmuLine[0]);
        auto bus = busList[ind];

        auto pmu = new relays::pmu();
        pmu->setUserID(count);
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

void loadPsatBreakerArray(coreObject* parentObject,
                          const mArray& brkr,
                          const std::vector<gridBus*>& /*busList*/)
{
    auto* gds = dynamic_cast<gridSimulation*>(parentObject->getRoot());
    if (gds == nullptr) {  // can't make faults if we don't have access to the simulation
        return;
    }
    for (auto& brk : brkr) {
        auto ind = static_cast<index_t>(brk[0]);
        auto lnk = static_cast<Link*>(parentObject->findByUserID("link", ind));
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

void loadPsatMotorArray(coreObject* /*parentObject*/,
                        const mArray& mtr,
                        const std::vector<gridBus*>& busList)
{
    for (auto& mtrline : mtr) {
        auto ind1 = static_cast<index_t>(mtrline[0]);
        gridBus* bus1 = busList[ind1];

        auto motor = new loads::motorLoad();
        bus1->add(motor);
        // TODO(phlpt): Add parameters.
    }
}

}  // namespace griddyn
