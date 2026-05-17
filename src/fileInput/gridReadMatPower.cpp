/*
 * Copyright (c) 2014-2026, Lawrence Livermore National Security
 * See the top-level NOTICE for additional details. All rights reserved.
 * SPDX-License-Identifier: BSD-3-Clause
 */

#include "core/objectFactoryTemplates.hpp"
#include "fileInput.h"
#include "griddyn/Generator.h"
#include "griddyn/gridBus.h"
#include "griddyn/griddyn-config.h"
#include "griddyn/links/acLine.h"
#include "griddyn/loads/zipLoad.h"
#include "readerHelper.h"

#ifdef ENABLE_OPTIMIZATION_LIBRARY
#    include "optimization/gridDynOpt.h"
#    include "optimization/models/gridGenOpt.h"
#    include "optimization/optObjectFactory.h"
#else
#    include "griddyn/simulation/gridSimulation.h"
#endif

#include "gmlc/utilities/stringConversion.h"
#include <cstdlib>
#include <string>
#include <vector>

namespace griddyn {
using gmlc::utilities::numeric_conversion;
using units::deg;
using units::MVAR;
using units::MW;
using units::rad;
using units::s;

using mArray = std::vector<std::vector<double>>;

// NOLINTBEGIN(misc-use-internal-linkage,readability-identifier-length,misc-const-correctness,readability-qualified-auto,modernize-use-integer-sign-comparison,readability-math-missing-parentheses)
void loadBusArray(CoreObject* parentObject,
                  double basepower,
                  mArray& buses,
                  std::vector<gridBus*>& busList,
                  const basicReaderInfo& readerOptions);
int loadGenArray(CoreObject* parentObject,
                 mArray& gens,
                 std::vector<gridBus*>& busList,
                 const basicReaderInfo& readerOptions);
void loadGenCostArray(CoreObject* parentObject, mArray& genCost, int gencount);
void loadLinkArray(CoreObject* parentObject,
                   mArray& lnks,
                   std::vector<gridBus*>& busList,
                   const basicReaderInfo& readerOptions);
// wrapper function to detect m file format for matpower or PSAT

void loadMatPower(CoreObject* parentObject,
                  const std::string& filetext,
                  const std::string& basename,
                  const basicReaderInfo& readerOptions)
{
    double basepower = readerOptions.base;
    gridSimulation::resetObjectCounters();  // reset all the object counters to 0
    mArray M1;
    int gencount = 0;
    std::vector<gridBus*> busList;
    size_t baseMVALoc = filetext.find(basename + ".baseMVA");
    if (baseMVALoc != std::string::npos) {
        size_t B = filetext.find_first_of('=', baseMVALoc);
        size_t C = filetext.find_first_of(";\n", baseMVALoc);
        auto tstr = filetext.substr(B + 1, C - B - 1);
        basepower = numeric_conversion(tstr, 0.0);
        parentObject->set("basepower", basepower);
    }
    // now find the bus structure
    if (readMatlabArray(basename + ".bus", filetext, M1)) {
        loadBusArray(parentObject, basepower, M1, busList, readerOptions);
    }
    if (readMatlabArray(basename + ".gen", filetext, M1)) {
        gencount = loadGenArray(parentObject, M1, busList, readerOptions);
    }
    if (readMatlabArray(basename + ".branch", filetext, M1)) {
        loadLinkArray(parentObject, M1, busList, readerOptions);
    }
    if (readMatlabArray(basename + ".gencost", filetext, M1)) {
        loadGenCostArray(parentObject, M1, gencount);
    }
}

void loadBusArray(CoreObject* parentObject,
                  double basepower,
                  mArray& buses,
                  std::vector<gridBus*>& busList,
                  const basicReaderInfo& /*readerOptions*/)
{
    Load* ld = nullptr;
    auto busFactory = dynamic_cast<typeFactory<gridBus>*>(
        coreObjectFactory::instance()->getFactory("bus")->getFactory(""));
    busFactory->prepObjects(static_cast<count_t>(buses.size()), parentObject);

    auto loadFactory = dynamic_cast<typeFactory<Load>*>(
        coreObjectFactory::instance()->getFactory("load")->getFactory(""));
    loadFactory->prepObjects(static_cast<count_t>(buses.size()), parentObject);
    for (const auto& busData : buses) {
        auto ind1 = static_cast<index_t>(busData[0]);
        if (ind1 >= static_cast<index_t>(busList.size())) {
            busList.resize(ind1 * 2 + 1);
        }
        if (busList[ind1] == nullptr) {
            busList[ind1] = busFactory->makeTypeObject();
            busList[ind1]->set("basepower", basepower);
            busList[ind1]->setName("Bus_" + std::to_string(ind1));
            busList[ind1]->setUserID(ind1);
            parentObject->add(busList[ind1]);
        }
        gridBus* bus = busList[ind1];
        ind1 = static_cast<int>(busData[1]);
        if (ind1 == 2) {
            bus->set("type", "PV");
        } else if (ind1 == 3) {
            bus->set("type", "SLK");
        } else if (ind1 == 4) {
            bus->disable();
        }
        bus->set("basevoltage", busData[9]);
        // check the constant load
        if ((busData[2] != 0.0) || (busData[3] != 0.0)) {
            ld = loadFactory->makeTypeObject();
            bus->add(ld);
            ld->set("p", busData[2], MW);
            ld->set("q", busData[3], MVAR);
            ld->setFlag("no_pqvoltage_limit");
        } else {
            ld = nullptr;
        }
        if (busData[4] != 0.0) {
            if (ld == nullptr) {
                ld = loadFactory->makeTypeObject();
                bus->add(ld);
            }
            ld->set("yp", busData[4], MW);
        }
        if (busData[5] != 0) {
            if (ld == nullptr) {
                ld = loadFactory->makeTypeObject();
                bus->add(ld);
            }
            ld->set("yq", -busData[5], MVAR);
        }
        // buses[kk][6] is the area which should be used at some point

        bus->setVoltageAngle(busData[7], convert(busData[8], deg, rad));
        bus->set("vmax", busData[11]);
        bus->set("vmin", busData[12]);
    }
}
/*
see: http://www.pserc.cornell.edu/matpower/docs/ref/matpower6.0/idx_gen.html
GEN BUS 1 bus number
PG 2 real power output (MW)
QG 3 reactive power output (MVAr)
QMAX 4 maximum reactive power output (MVAr)
QMIN 5 minimum reactive power output (MVAr)
VG 6 voltage magnitude setpoint (pu)
MBASE 7 total MVA base of machine, defaults to baseMVA
GEN STATUS 8 machine status,
> 0 - machine in-service
<= 0 - machine out-of-service
PMAX 9 maximum real power output (MW)
PMIN 10 minimum real power output (MW)
PC1* 11 lower real power output of PQ capability curve (MW)
PC2* 12 upper real power output of PQ capability curve (MW)
QC1MIN* 13 minimum reactive power output at PC1 (MVAr)
QC1MAX* 14 maximum reactive power output at PC1 (MVAr)
QC2MIN* 15 minimum reactive power output at PC2 (MVAr)
QC2MAX* 16 maximum reactive power output at PC2 (MVAr)
RAMP AGC* 17 ramp rate for load following/AGC (MW/min)
RAMP 10* 18 ramp rate for 10 minute reserves (MW)
RAMP 30* 19 ramp rate for 30 minute reserves (MW)
RAMP Q* 20 ramp rate for reactive power (2 sec timescale) (MVAr/min)
APF* 21 area participation factor
MU PMAX† 22 Kuhn-Tucker multiplier on upper Pg limit (u/MW)
MU PMIN† 23 Kuhn-Tucker multiplier on lower Pg limit (u/MW)
MU QMAX† 24 Kuhn-Tucker multiplier on upper Qg limit (u/MVAr)
MU QMIN† 25 Kuhn-Tucker multiplier on lower Qg limit (u/MVAr)
*/

int loadGenArray(CoreObject* parentObject,
                 mArray& gens,
                 std::vector<gridBus*>& busList,
                 const basicReaderInfo& readerOptions)
{
    const auto& bri = readerOptions;
    index_t kk = 1;
    std::string gtype = (bri.checkFlag(ASSUME_POWERFLOW_ONLY)) ? "simple" : "";
    auto genFactory = dynamic_cast<typeFactory<Generator>*>(
        coreObjectFactory::instance()->getFactory("generator")->getFactory(gtype));
    genFactory->prepObjects(static_cast<count_t>(gens.size()), parentObject);

    for (auto& genLine : gens) {
        auto ind1 = static_cast<index_t>(genLine[0]);
        auto bus = busList[ind1];
        Generator* gen = genFactory->makeTypeObject("gen" + std::to_string(kk));
        gen->setUserID(kk);
        ++kk;
        bus->add(gen);
        if (genLine[1] != 0) {
            gen->set("p", genLine[1], MW);
        }
        if (genLine[2] != 0.0) {
            gen->set("q", genLine[2], MVAR);
        }
        gen->set("qmax", genLine[3], MVAR);
        gen->set("qmin", genLine[4], MVAR);

        if (genLine[6] > 0.0) {
            gen->set("mbase", genLine[6], MVAR);
        }
        if (genLine[7] <= 0.0) {
            gen->disable();
            if (genLine[5] != 1.0) {
                if (!bri.checkFlag(NO_GENERATOR_BUS_VOLTAGE_RESET)) {
                    bus->set("vtarget", genLine[5]);
                }
            }
        } else {
            if (!bri.checkFlag(NO_GENERATOR_BUS_VOLTAGE_RESET)) {
                bus->set("vtarget", genLine[5]);
                // bus->set("voltage", genLine[5]);
            }
        }

        if (genLine[8] != 0.0) {
            gen->set("pmax", genLine[8], MW);
        }

        if (genLine[9] != 0) {
            gen->set("pmin", genLine[9], MW);
        }

        if (genLine.size() >= 21) {
            if ((genLine[10] != 0) && (genLine[11] != 0)) {
                std::vector<double> PC{genLine[10], genLine[11]};
                std::vector<double> Qmin{genLine[12], genLine[14]};
                std::vector<double> Qmax{genLine[13], genLine[15]};
                gen->setCapabilityCurve(PC, Qmin, Qmax);
            }
            if (genLine[16] != 0) {
                gen->set("rampreg", genLine[16], MW / s);
            }
            if (genLine[17] != 0) {
                gen->set("ramp10", genLine[17], MW / s);
            }
            if (genLine[18] != 0) {
                gen->set("ramp30", genLine[18], MW / s);
            }
            if (genLine[19] != 0) {
                gen->set("rampq", genLine[19], MW / s);
            }
            if (genLine[20] != 0) {
                gen->set("apf", genLine[20]);
            }
        }
    }
    return (kk - 1);
}
/*
see: http://www.pserc.cornell.edu/matpower/docs/ref/matpower5.0/idx_cost.html
MODEL                   1 cost model, 1 = piecewise linear, 2 = polynomial
gridState_t::STARTUP    2 startup cost in US dollars*
SHUTDOWN                3 shutdown cost in US dollars*
NCOST                   4 number of cost coefficients for polynomial cost function,
                          or number of data points for piecewise linear
COST                    5 parameters defining total cost function f(p) begin in this column,
                          units of f and p are $/hr and MW (or MVAr), respectively
                          (MODEL = 1) : p0, f0, p1, f1, ..., pn, fn
                            where p0 < p1 < ... < pn and the cost f(p) is defined by
                            the coordinates (p0, f0), (p1, f1), ... , (pn, fn)
                            of the end/break-points of the piecewise linear cost
                          (MODEL = 2) ) cn, ..., c1, c0
                            n + 1 coefficients of n-th order polynomial cost, starting with
                            highest order, where cost is f(p) = cn*p^n + ... + c1*p + c0
*/
#ifdef ENABLE_OPTIMIZATION_LIBRARY
void loadGenCostArray(CoreObject* parentObject, mArray& genCost, int gencount)
{
    auto gdo = dynamic_cast<gridDynOptimization*>(parentObject->getRoot());
    if (gdo == nullptr)  // return if the core object doesn't support optimization
    {
        return;
    }

    gridGenOpt* go;
    gridOptObject* oo;
    CoreObject* obj;
    int mode = 0;
    int numc = 0;
    int q = 0;
    std::vector<double> coeff;

    auto genOptFactory = dynamic_cast<optObjectFactory<gridGenOpt, Generator>*>(
        coreOptObjectFactory::instance()->getFactory("")->getFactory("generator"));
    genOptFactory->prepObjects(static_cast<count_t>(genCost.size()), parentObject);

    std::vector<gridGenOpt*> genOptList(gencount);

    int kk = 1;
    for (auto& genLine : genCost) {
        if (kk > gencount) {
            q = 1;
            go = genOptList[kk - gencount - 1];
        } else {
            obj = parentObject->getSubObject("gen", kk);
            if (obj == nullptr) {
                continue;
            }
            go = genOptFactory->makeTypeObject(obj);
            genOptList[kk - 1] = go;
            q = 0;
            oo = gdo->makeOptimizationObjectPath(obj->getParent());
            oo->add(go);
        }

        ++kk;
        mode = static_cast<int>(genLine[0]);
        numc = static_cast<int>(genLine[3]);
        coeff.resize(numc);
        for (int ii = 0; ii < numc; ii++) {
            coeff[ii] = genLine[4 + ii];
        }
        go->loadCostCoeff(coeff, q);
        if (mode == 1) {
            go->set("piecewise linear cost", 1);
        }
    }
}
#else
void loadGenCostArray(CoreObject* /*parentObject*/, mArray& /*genCost*/, int /*gencount*/) {}
#endif
/*
see: http://www.pserc.cornell.edu/matpower/docs/ref/matpower6.0/idx_brch.html
Branch data
F BUS 1 \from" bus number
T BUS 2 \to" bus number
BR R 3 resistance (pu)
BR X 4 reactance (pu)
BR B 5 total line charging susceptance (pu)
RATE A 6 MVA rating A (long term rating)
RATE B 7 MVA rating B (short term rating)
RATE C 8 MVA rating C (emergency rating)
TAP 9 transformer off nominal turns ratio, (taps at \from" bus,
impedance at \to" bus, i.e. if r = x = 0, tap = jVf j
jVtj )
SHIFT 10 transformer phase shift angle (degrees), positive ) delay
BR STATUS 11 initial branch status, 1 = in-service, 0 = out-of-service
ANGMIN* 12 minimum angle difference, angle(Vf) - angle(Vt) (degrees)
ANGMAX* 13 maximum angle difference, angle(Vf) - angle(Vt) (degrees)
PF† 14 real power injected at \from" bus end (MW)
QF† 15 reactive power injected at \from" bus end (MVAr)
PT† 16 real power injected at \to" bus end (MW)
QT† 17 reactive power injected at \to" bus end (MVAr)
MU SF‡ 18 Kuhn-Tucker multiplier on MVA limit at \from" bus (u/MVA)
MU ST‡ 19 Kuhn-Tucker multiplier on MVA limit at \to" bus (u/MVA)
MU ANGMIN‡ 20 Kuhn-Tucker multiplier lower angle difference limit (u/degree)
MU ANGMAX‡ 21 Kuhn-Tucker multiplier upper angle difference limit (u/degree)
*/

void loadLinkArray(CoreObject* parentObject,
                   mArray& lnks,
                   std::vector<gridBus*>& busList,
                   const basicReaderInfo& /*readerOptions*/)
{
    auto linkFactory = dynamic_cast<typeFactory<Link>*>(
        coreObjectFactory::instance()->getFactory("link")->getFactory(""));
    linkFactory->prepObjects(static_cast<count_t>(lnks.size()), parentObject);
    index_t kk = 0;
    for (const auto& linkData : lnks) {
        auto ind1 = static_cast<index_t>(linkData[0]);
        gridBus* bus1 = busList[ind1];

        auto ind2 = static_cast<index_t>(linkData[1]);
        gridBus* bus2 = busList[ind2];
        Link* lnk = linkFactory->makeTypeObject();
        ++kk;
        lnk->setUserID(kk);
        lnk->updateBus(bus1, 1);
        lnk->updateBus(bus2, 2);
        parentObject->add(lnk);
        lnk->set("r", linkData[2]);
        lnk->set("x", linkData[3]);
        lnk->set("b", linkData[4]);

        if (linkData[5] != 0.0) {
            lnk->set("ratinga", linkData[5], MW);
        }

        if (linkData[6] != 0.0) {
            lnk->set("ratingb", linkData[6], MW);
        }
        if (linkData[7] != 0.0) {
            lnk->set("ratingc", linkData[7], MW);
        }
        if (linkData[8] > 0.05)  // just make sure list a tap
        {
            lnk->set("tap", linkData[8]);
        }
        if (linkData[9] != 0.0) {
            lnk->set("tapangle", linkData[9], deg);
        }

        if (linkData[10] <= 0.0) {
            lnk->disconnect();
        }
        if (linkData.size() >= 13) {
            lnk->set("minangle", linkData[11], deg);
            lnk->set("maxangle", linkData[12], deg);
        }
    }
}

// NOLINTEND(misc-use-internal-linkage,readability-identifier-length,misc-const-correctness,readability-qualified-auto,modernize-use-integer-sign-comparison,readability-math-missing-parentheses)

}  // namespace griddyn

