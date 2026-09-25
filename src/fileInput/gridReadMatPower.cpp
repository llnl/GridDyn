/*
 * Copyright (c) 2014-2026, Lawrence Livermore National Security
 * See the top-level NOTICE for additional details. All rights reserved.
 * SPDX-License-Identifier: BSD-3-Clause
 */

#include "core/ObjectFactoryTemplates.hpp"
#include "fileInput.h"
#include "griddyn/Generator.h"
#include "griddyn/GridArea.h"
#include "griddyn/GridBus.h"
#include "griddyn/MatPowerCostCurve.h"
#include "griddyn/links/AcLine.h"
#include "griddyn/loads/ZipLoad.h"
#include "griddyn/simulation/GridSimulation.h"
#include "readerHelper.h"

#ifdef GRIDDYN_ENABLE_OPTIMIZATION_LIBRARY
#    include "optimization/gridDynOpt.h"
#endif

#include "gmlc/utilities/stringConversion.h"
#include <algorithm>
#include <cmath>
#include <compare>
#include <cstddef>
#include <cstdlib>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

namespace griddyn {
using gmlc::utilities::numeric_conversion;
using units::convert;
using units::deg;
using units::MVAR;
using units::MW;
using units::puMW;
using units::rad;

using mArray = std::vector<std::vector<double>>;

namespace {

    using AreaMap = std::unordered_map<int, GridArea*>;

    CoreObject* getMatPowerLinkParent(CoreObject* parentObject, Link* link)
    {
        if ((link == nullptr) || (link->terminalCount() != 2)) {
            return parentObject;
        }

        auto* bus1 = link->getBus(1);
        auto* bus2 = link->getBus(2);
        auto* area1 = (bus1 != nullptr) ? dynamic_cast<GridArea*>(bus1->getParent()) : nullptr;
        auto* area2 = (bus2 != nullptr) ? dynamic_cast<GridArea*>(bus2->getParent()) : nullptr;
        if ((area1 == nullptr) || (area1 != area2)) {
            return parentObject;
        }

        for (auto* object = area1; object != nullptr;
             object = dynamic_cast<GridArea*>(object->getParent())) {
            if (object == parentObject) {
                return area1;
            }
        }
        return parentObject;
    }

    AreaMap createMatPowerAreas(CoreObject* parentObject,
                                const mArray& areaData,
                                const mArray& buses,
                                const BasicReaderInfo& readerOptions);

    void loadBusArray(CoreObject* parentObject,
                      double basepower,
                      mArray& buses,
                      std::vector<GridBus*>& busList,
                      const BasicReaderInfo& readerOptions,
                      const AreaMap& areas);
    int loadGenArray(CoreObject* parentObject,
                     mArray& gens,
                     std::vector<GridBus*>& busList,
                     const BasicReaderInfo& readerOptions);
    void loadGenCostArray(CoreObject* parentObject, mArray& genCost, int gencount);
    void loadLinkArray(CoreObject* parentObject,
                       mArray& lnks,
                       std::vector<GridBus*>& busList,
                       const BasicReaderInfo& readerOptions);

}  // namespace

// wrapper function to detect m file format for matpower or PSAT

void loadMatPower(CoreObject* parentObject,
                  const std::string& filetext,
                  const std::string& basename,
                  const BasicReaderInfo& readerOptions)
{
    double basepower = readerOptions.base;
    GridSimulation::resetObjectCounters();  // reset all the object counters to 0
    mArray matlabArrayData;
    int gencount = 0;
    std::vector<GridBus*> busList;
    const size_t baseMVALoc = filetext.find(basename + ".baseMVA");
    if (baseMVALoc != std::string::npos) {
        const size_t equalsLocation = filetext.find_first_of('=', baseMVALoc);
        const size_t endLocation = filetext.find_first_of(";\n", baseMVALoc);
        auto tstr = filetext.substr(equalsLocation + 1, endLocation - equalsLocation - 1);
        basepower = numeric_conversion(tstr, 0.0);
        parentObject->set("basepower", basepower);
    }
    mArray busData;
    mArray areaData;
    readMatlabArray(basename + ".areas", filetext, areaData);
    // Read the area definitions before creating buses so each bus can be placed
    // directly into its owning GridArea.
    if (readMatlabArray(basename + ".bus", filetext, busData)) {
        const auto areas = createMatPowerAreas(parentObject, areaData, busData, readerOptions);
        loadBusArray(parentObject, basepower, busData, busList, readerOptions, areas);
    }
    // now find the remaining structures
    matlabArrayData.clear();
    if (readMatlabArray(basename + ".gen", filetext, matlabArrayData)) {
        gencount = loadGenArray(parentObject, matlabArrayData, busList, readerOptions);
    }
    if (readMatlabArray(basename + ".branch", filetext, matlabArrayData)) {
        loadLinkArray(parentObject, matlabArrayData, busList, readerOptions);
    }
    if (readMatlabArray(basename + ".gencost", filetext, matlabArrayData)) {
        loadGenCostArray(parentObject, matlabArrayData, gencount);
    }
}

namespace {

    AreaMap createMatPowerAreas(CoreObject* parentObject,
                                const mArray& areaData,
                                const mArray& buses,
                                const BasicReaderInfo& readerOptions)
    {
        AreaMap areas;
        auto addArea = [&](double value) {
            const auto areaId = static_cast<int>(value);
            if ((areaId <= 0) || areas.contains(areaId)) {
                return;
            }
            auto areaName = "AREA_" + std::to_string(areaId);
            if (!readerOptions.prefix.empty()) {
                areaName = readerOptions.prefix + '_' + areaName;
            }
            auto* area = new GridArea(areaName);
            area->setUserID(static_cast<index_t>(areaId));
            try {
                parentObject->add(area);
            }
            catch (const ObjectAddFailure&) {
                addToParentWithRename(area, parentObject);
            }
            areas.emplace(areaId, area);
        };

        for (const auto& areaDataRow : areaData) {
            if (!areaDataRow.empty()) {
                addArea(areaDataRow[0]);
            }
        }
        // MATPOWER stores the bus area in column 7.  Some cases omit mpc.areas,
        // so retain those area IDs as usable placeholders as well.
        for (const auto& busData : buses) {
            if (busData.size() > 6) {
                addArea(busData[6]);
            }
        }
        return areas;
    }

    void loadBusArray(CoreObject* parentObject,
                      double basepower,
                      mArray& buses,
                      std::vector<GridBus*>& busList,
                      const BasicReaderInfo& /*readerOptions*/,
                      const AreaMap& areas)
    {
        GridLoad* load = nullptr;
        auto* busFactory = dynamic_cast<TypeFactory<GridBus>*>(
            CoreObjectFactory::instance()->getFactory("bus")->getFactory(""));
        busFactory->prepObjects(static_cast<count_t>(buses.size()), parentObject);

        auto* loadFactory = dynamic_cast<TypeFactory<GridLoad>*>(
            CoreObjectFactory::instance()->getFactory("load")->getFactory(""));
        loadFactory->prepObjects(static_cast<count_t>(buses.size()), parentObject);
        for (const auto& busData : buses) {
            auto ind1 = static_cast<index_t>(busData[0]);
            if (std::cmp_greater_equal(ind1, busList.size())) {
                busList.resize((ind1 * 2) + 1);
            }
            if (busList[ind1] == nullptr) {
                busList[ind1] = busFactory->makeTypeObject();
                busList[ind1]->set("basepower", basepower);
                busList[ind1]->setName("Bus_" + std::to_string(ind1));
                busList[ind1]->setUserID(ind1);
                auto* busParent = parentObject;
                if (busData.size() > 6) {
                    if (const auto area = areas.find(static_cast<int>(busData[6]));
                        area != areas.end()) {
                        busParent = area->second;
                    }
                }
                busParent->add(busList[ind1]);
            }
            GridBus* bus = busList[ind1];
            if (busData.size() > 10) {
                bus->set("zone", busData[10]);
            }
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
                load = loadFactory->makeTypeObject();
                bus->add(load);
                load->set("p", busData[2], MW);
                load->set("q", busData[3], MVAR);
                load->setFlag("no_pqvoltage_limit");
            } else {
                load = nullptr;
            }
            if (busData[4] != 0.0) {
                if (load == nullptr) {
                    load = loadFactory->makeTypeObject();
                    bus->add(load);
                }
                load->set("yp", busData[4], MW);
            }
            if (busData[5] != 0) {
                if (load == nullptr) {
                    load = loadFactory->makeTypeObject();
                    bus->add(load);
                }
                load->set("yq", -busData[5], MVAR);
            }
            // The MATPOWER bus record supplies the initial operating point;
            // an active generator's VG value is applied later as the PV/slack
            // voltage target.
            bus->setVoltageAngle(busData[7], convert(busData[8], deg, rad));
            if (busData[11] != 0.0) {
                bus->set("vmax", busData[11]);
            }
            if (busData[12] != 0.0) {
                bus->set("vmin", busData[12]);
            }
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
    MU PMAXÃ¢â‚¬Â  22 Kuhn-Tucker multiplier on upper Pg limit (u/MW)
    MU PMINÃ¢â‚¬Â  23 Kuhn-Tucker multiplier on lower Pg limit (u/MW)
    MU QMAXÃ¢â‚¬Â  24 Kuhn-Tucker multiplier on upper Qg limit (u/MVAr)
    MU QMINÃ¢â‚¬Â  25 Kuhn-Tucker multiplier on lower Qg limit (u/MVAr)
    */

    int loadGenArray(CoreObject* parentObject,
                     mArray& gens,
                     std::vector<GridBus*>& busList,
                     const BasicReaderInfo& readerOptions)
    {
        const auto& bri = readerOptions;
        const double basepower = parentObject->get("basepower", MW);
        index_t genIndex = 1;
        const std::string generatorType = (bri.checkFlag(ASSUME_POWERFLOW_ONLY)) ? "simple" : "";
        auto* genFactory = dynamic_cast<TypeFactory<Generator>*>(
            CoreObjectFactory::instance()->getFactory("generator")->getFactory(generatorType));
        genFactory->prepObjects(static_cast<count_t>(gens.size()), parentObject);

        for (auto& genLine : gens) {
            auto ind1 = static_cast<index_t>(genLine[0]);
            auto* bus = busList[ind1];
            Generator* gen = genFactory->makeTypeObject("gen" + std::to_string(genIndex));
            gen->setUserID(genIndex);
            ++genIndex;
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
                    if (!bri.checkFlag(USE_BUS_VOLTAGE_TARGETS)) {
                        bus->set("vtarget", genLine[5]);
                    }
                }
            } else {
                if (!bri.checkFlag(USE_BUS_VOLTAGE_TARGETS)) {
                    bus->set("vtarget", genLine[5]);
                }
            }

            // MATPOWER/PYPOWER PMAX and PMIN are optimization limits, and
            // zero is a meaningful bound.  Preserve the loaded matrix values
            // in the physical Generator so OPF adapters read the same single
            // source of truth as power-flow and dynamics controls.
            gen->set("pmax", genLine[8], MW);
            gen->set("pmin", genLine[9], MW);

            if (genLine.size() >= 21) {
                if ((genLine[10] != 0.0) || (genLine[11] != 0.0) || (genLine[12] != 0.0) ||
                    (genLine[13] != 0.0) || (genLine[14] != 0.0) || (genLine[15] != 0.0)) {
                    const std::vector<double> capabilityPower{
                        convert(genLine[10], MW, puMW, basepower),
                        convert(genLine[11], MW, puMW, basepower)};
                    const std::vector<double> minimumReactivePower{
                        convert(genLine[12], MVAR, puMW, basepower),
                        convert(genLine[14], MVAR, puMW, basepower)};
                    const std::vector<double> maximumReactivePower{
                        convert(genLine[13], MVAR, puMW, basepower),
                        convert(genLine[15], MVAR, puMW, basepower)};
                    gen->setCapabilityCurve(capabilityPower,
                                            minimumReactivePower,
                                            maximumReactivePower);
                }
                if (genLine[16] != 0) {
                    gen->set("rampagc", genLine[16]);
                }
                if (genLine[17] != 0) {
                    gen->set("ramp10", genLine[17]);
                }
                if (genLine[18] != 0) {
                    gen->set("ramp30", genLine[18]);
                }
                if (genLine[19] != 0) {
                    gen->set("rampq", genLine[19]);
                }
                // MATPOWER APF is its area participation factor. Preserve
                // explicit zero values as well as nonzero values.
                gen->set("participation", genLine[20]);
            }
        }
        return (genIndex - 1);
    }
    /*
    see: http://www.pserc.cornell.edu/matpower/docs/ref/matpower5.0/idx_cost.html
    MODEL                   1 cost model, 1 = piecewise linear, 2 = polynomial
    GridState::STARTUP    2 startup cost in US dollars*
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
    void loadGenCostArray(CoreObject* parentObject, mArray& genCost, int gencount)
    {
        // MATPOWER stores active-power cost curves first, followed optionally
        // by one reactive-power curve per generator.  Cost data belongs to
        // the optimization model, so only optimization simulations retain it.
        if (gencount <= 0) {
            return;
        }
#ifdef GRIDDYN_ENABLE_OPTIMIZATION_LIBRARY
        auto* optimization = dynamic_cast<GridDynOptimization*>(parentObject->getRoot());
#endif
        for (std::size_t rowIndex = 0; rowIndex < genCost.size(); ++rowIndex) {
            const auto& row = genCost[rowIndex];
            const bool reactive = std::cmp_greater_equal(rowIndex, gencount);
            const auto generatorIndex =
                reactive ? rowIndex - static_cast<std::size_t>(gencount) : rowIndex;
            if (std::cmp_greater_equal(generatorIndex, gencount) || row.size() < 4) {
                continue;
            }
            if (!std::isfinite(row[0]) || !std::isfinite(row[1]) || !std::isfinite(row[2]) ||
                !std::isfinite(row[3]) || row[3] < 1.0 || std::floor(row[3]) != row[3] ||
                row[3] > static_cast<double>(row.size() - 4) || (row[0] != 1.0 && row[0] != 2.0)) {
                continue;
            }
            const auto declaredCount = static_cast<std::size_t>(row[3]);
            std::size_t coefficientCount = declaredCount;
            if (row[0] == 1.0) {
                // NCOST counts points for piecewise linear curves, each of
                // which contributes both a power and a cost value.
                if (declaredCount < 2 || declaredCount > (row.size() - 4) / 2) {
                    continue;
                }
                coefficientCount *= 2;
            } else if (declaredCount > row.size() - 4) {
                continue;
            }
            bool finiteCoefficients = true;
            for (std::size_t coefficientIndex = 4; coefficientIndex < 4 + coefficientCount;
                 ++coefficientIndex) {
                finiteCoefficients = finiteCoefficients && std::isfinite(row[coefficientIndex]);
            }
            if (!finiteCoefficients) {
                continue;
            }
            auto* gen = dynamic_cast<Generator*>(
                parentObject->findByUserID("gen", static_cast<index_t>(generatorIndex + 1)));
            if (gen == nullptr) {
                continue;
            }
            MatPowerCostCurve curve;
            curve.model = static_cast<int>(row[0]);
            curve.startupCost = row[1];
            curve.shutdownCost = row[2];
            curve.coefficients.assign(row.begin() + 4,
                                      row.begin() +
                                          static_cast<std::ptrdiff_t>(4 + coefficientCount));
            if (!curve.valid()) {
                continue;
            }
#ifdef GRIDDYN_ENABLE_OPTIMIZATION_LIBRARY
            if (optimization != nullptr) {
                optimization->setGeneratorCostCurve(gen, curve, reactive);
            }
#else
            (void)curve;
#endif
        }
    }
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
    PFÃ¢â‚¬Â  14 real power injected at \from" bus end (MW)
    QFÃ¢â‚¬Â  15 reactive power injected at \from" bus end (MVAr)
    PTÃ¢â‚¬Â  16 real power injected at \to" bus end (MW)
    QTÃ¢â‚¬Â  17 reactive power injected at \to" bus end (MVAr)
    MU SFÃ¢â‚¬Â¡ 18 Kuhn-Tucker multiplier on MVA limit at \from" bus (u/MVA)
    MU STÃ¢â‚¬Â¡ 19 Kuhn-Tucker multiplier on MVA limit at \to" bus (u/MVA)
    MU ANGMINÃ¢â‚¬Â¡ 20 Kuhn-Tucker multiplier lower angle difference limit (u/degree)
    MU ANGMAXÃ¢â‚¬Â¡ 21 Kuhn-Tucker multiplier upper angle difference limit (u/degree)
    */

    void loadLinkArray(CoreObject* parentObject,
                       mArray& lnks,
                       std::vector<GridBus*>& busList,
                       const BasicReaderInfo& /*readerOptions*/)
    {
        auto* linkFactory = dynamic_cast<TypeFactory<Link>*>(
            CoreObjectFactory::instance()->getFactory("link")->getFactory(""));
        linkFactory->prepObjects(static_cast<count_t>(lnks.size()), parentObject);
        index_t linkIndex = 0;
        for (const auto& linkData : lnks) {
            auto ind1 = static_cast<index_t>(linkData[0]);
            GridBus* bus1 = busList[ind1];

            auto ind2 = static_cast<index_t>(linkData[1]);
            GridBus* bus2 = busList[ind2];
            Link* lnk = linkFactory->makeTypeObject();
            ++linkIndex;
            lnk->setUserID(linkIndex);
            lnk->updateBus(bus1, 1);
            lnk->updateBus(bus2, 2);
            getMatPowerLinkParent(parentObject, lnk)->add(lnk);
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
                // MATPOWER status 0 means the branch is out of service, not that an in-service
                // branch has opened its switches. Disabling it also avoids treating the default
                // (not-yet-initialized) terminal voltages as a fault during case import.
                lnk->disable();
            }
            if (linkData.size() >= 13) {
                // MATPOWER uses an all-zero pair for an unconstrained angle
                // limit.  Preserve a one-sided zero as an actual bound and
                // use MATPOWER's explicit unbounded sentinel in GridDyn so
                // optimization does not mistake the AcLine default for an
                // imported constraint.
                if ((linkData[11] == 0.0) && (linkData[12] == 0.0)) {
                    lnk->set("minangle", -360.0, deg);
                    lnk->set("maxangle", 360.0, deg);
                } else {
                    lnk->set("minangle", linkData[11], deg);
                    lnk->set("maxangle", linkData[12], deg);
                }
            }
        }
    }

}  // namespace

}  // namespace griddyn
