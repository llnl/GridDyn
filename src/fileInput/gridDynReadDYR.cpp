/*
 * Copyright (c) 2014-2026, Lawrence Livermore National Security
 * See the top-level NOTICE for additional details. All rights reserved.
 * SPDX-License-Identifier: BSD-3-Clause
 */

#include "ReaderInfo.h"
#include "core/CoreExceptions.h"
#include "core/CoreObject.h"
#include "core/ObjectFactory.hpp"
#include "core/coreDefinitions.hpp"
#include "fileInput.h"
#include "gmlc/utilities/stringConversion.h"
#include "gmlc/utilities/stringOps.h"
#include "griddyn/Exciter.h"
#include "griddyn/GenModel.h"
#include "griddyn/Generator.h"
#include "griddyn/Governor.h"
#include "griddyn/GridBus.h"
#include "griddyn/Stabilizer.h"
#include "griddyn/generators/DynamicGenerator.h"
#include "griddyn/governors/GovernorIeeeG1.h"
#include "griddyn/stabilizers/StabilizerST2CUT.h"
#include <cmath>
#include <fstream>
#include <iostream>
#include <memory>
#include <string>

namespace griddyn {
namespace {
    void loadGENROU(CoreObject* parentObject, stringVec& tokens);
    void loadESDC1A(CoreObject* parentObject, stringVec& tokens);
    void loadESST3A(CoreObject* parentObject, stringVec& tokens);
    void loadEXST1(CoreObject* parentObject, stringVec& tokens);
    void loadTGOV1(CoreObject* parentObject, stringVec& tokens);
    void loadIEEEG1(CoreObject* parentObject, stringVec& tokens);
    void loadST2CUT(CoreObject* parentObject, stringVec& tokens);
    void loadEXDC2(CoreObject* parentObject, stringVec& tokens);
    void loadSEXS(CoreObject* parentObject, stringVec& tokens);
}  // namespace

void loadDyr(CoreObject* parentObject,
             const std::string& fileName,
             const BasicReaderInfo& /*readerOptions*/)
{
    std::ifstream file(fileName.c_str(), std::ios::in);
    std::string line;  // line storage
    std::string continuedLine;

    if (!(file.is_open())) {
        parentObject->log(parentObject, PrintLevel::ERROR, "Unable to open file " + fileName);
        //    return;
    }
    while (std::getline(file, line)) {
        gmlc::utilities::stringOps::trimString(line);
        if (line.empty()) {
            continue;
        }
        while (line.back() != '/') {
            if (std::getline(file, continuedLine)) {
                gmlc::utilities::stringOps::trimString(continuedLine);
                line += ' ' + continuedLine;
            } else {
                break;
            }
        }
        auto lineTokens = gmlc::utilities::stringOps::splitline(
            line, " \t\n,", gmlc::utilities::stringOps::delimiter_compression::on);
        // get rid of the '/' at the end of the last string
        auto lstr = lineTokens.back();
        lineTokens.pop_back();
        lstr = lstr.substr(0, lstr.size() - 1);
        if (!lstr.empty()) {
            lineTokens.push_back(lstr);
        }
        auto type = lineTokens[1];
        gmlc::utilities::stringOps::trimString(type);
        if (type == "'GENROU'") {
            loadGENROU(parentObject, lineTokens);
        } else if (type == "'ESDC1A'") {
            loadESDC1A(parentObject, lineTokens);
        } else if (type == "'ESST3A'") {
            loadESST3A(parentObject, lineTokens);
        } else if (type == "'EXST1'") {
            loadEXST1(parentObject, lineTokens);
        } else if (type == "'EXDC2'") {
            loadEXDC2(parentObject, lineTokens);
        } else if (type == "'TGOV1'") {
            loadTGOV1(parentObject, lineTokens);
        } else if (type == "'IEEEG1'") {
            loadIEEEG1(parentObject, lineTokens);
        } else if (type == "'ST2CUT'") {
            loadST2CUT(parentObject, lineTokens);
        } else if (type == "'SEXS'") {
            loadSEXS(parentObject, lineTokens);
        } else {
            std::cout << "unknown object type " << type << '\n';
        }
    }
}

namespace {
    void loadGENROU(CoreObject* parentObject, stringVec& tokens)
    {
        const int busId = std::stoi(tokens[0]);
        const auto* bus = static_cast<GridBus*>(parentObject->findByUserID("bus", busId));
        const int genId = std::stoi(tokens[2]);
        auto* gen = bus->getGen(genId - 1);

        auto params = gmlc::utilities::str2vector(tokens, kNullVal);

        auto cof = CoreObjectFactory::instance();
        auto* genModel = static_cast<GenModel*>(cof->createObject("genmodel", "genrou"));
        // Attach first so the RAW source resistance/reactance is transferred to
        // the model before the DYR machine parameters replace the source Xd.
        gen->add(genModel);
        genModel->set("tdop", params[3]);
        genModel->set("tdopp", params[4]);
        genModel->set("tqop", params[5]);
        genModel->set("tqopp", params[6]);
        genModel->set("h", params[7]);
        genModel->set("d", params[8]);
        genModel->set("xd", params[9]);
        genModel->set("xq", params[10]);
        genModel->set("xdp", params[11]);
        genModel->set("xqp", params[12]);
        genModel->set("xdpp", params[13]);
        genModel->set("xqpp", params[13]);
        genModel->set("xl", params[14]);
        genModel->set("s1", params[15]);
        genModel->set("s12", params[16]);
    }

    void loadESDC1A(CoreObject* parentObject, stringVec& tokens)
    {
        const int busId = std::stoi(tokens[0]);
        const auto* bus = static_cast<GridBus*>(parentObject->findByUserID("bus", busId));
        const int genId = std::stoi(tokens[2]);
        auto* gen = bus->getGen(genId - 1);

        auto params = gmlc::utilities::str2vector(tokens, kNullVal);
        Exciter* exciterModel;
        auto cof = CoreObjectFactory::instance();
        if (params[6] > 0.0)  // dc1a model must have tb>0 otherwise revert to type1
        {
            exciterModel = static_cast<Exciter*>(cof->createObject("exciter", "dc1a"));
        } else {
            exciterModel = static_cast<Exciter*>(cof->createObject("exciter", "type1"));
        }
        // TODO(phlpt): TR not implemented yet; no voltage compensation implemented.
        // exciterModel->set("tr", params[3]);
        exciterModel->set("ka", params[4]);
        exciterModel->set("ta", params[5]);
        if (params[6] > 0) {
            exciterModel->set("tb", params[6]);
            exciterModel->set("tc", params[7]);
        }
        exciterModel->set("vrmax", params[8]);
        exciterModel->set("vrmin", params[9]);
        exciterModel->set("ke", params[10]);
        exciterModel->set("te", params[11]);
        exciterModel->set("kf", params[12]);
        exciterModel->set("tf", params[13]);
        // TODO(phlpt): Compute the saturation coefficients to translate appropriately.

        gen->add(exciterModel);
    }

    void loadESST3A(CoreObject* parentObject, stringVec& tokens)
    {
        const int busId = std::stoi(tokens[0]);
        const auto* bus = static_cast<GridBus*>(parentObject->findByUserID("bus", busId));
        const int genId = std::stoi(tokens[2]);
        auto* gen = bus->getGen(genId - 1);

        const auto params = gmlc::utilities::str2vector(tokens, kNullVal);
        auto cof = CoreObjectFactory::instance();
        auto* exciterModel = static_cast<Exciter*>(cof->createObject("exciter", "esst3a"));
        // PSS/E order matches the ANDES psse-dyr.yaml ESST3A schema.
        exciterModel->set("tr", params[3]);
        exciterModel->set("vimax", params[4]);
        exciterModel->set("vimin", params[5]);
        exciterModel->set("km", params[6]);
        exciterModel->set("tc", params[7]);
        exciterModel->set("tb", params[8]);
        exciterModel->set("ka", params[9]);
        exciterModel->set("ta", params[10]);
        exciterModel->set("vrmax", params[11]);
        exciterModel->set("vrmin", params[12]);
        exciterModel->set("kg", params[13]);
        exciterModel->set("kp", params[14]);
        exciterModel->set("ki", params[15]);
        exciterModel->set("vbmax", params[16]);
        exciterModel->set("kc", params[17]);
        exciterModel->set("xl", params[18]);
        exciterModel->set("vgmax", params[19]);
        exciterModel->set("thetap", params[20]);
        exciterModel->set("tm", params[21]);
        exciterModel->set("vmmax", params[22]);
        exciterModel->set("vmmin", params[23]);

        gen->add(exciterModel);
    }

    void loadEXST1(CoreObject* parentObject, stringVec& tokens)
    {
        const int busId = std::stoi(tokens[0]);
        const auto* bus = static_cast<GridBus*>(parentObject->findByUserID("bus", busId));
        const int genId = std::stoi(tokens[2]);
        auto* gen = bus->getGen(genId - 1);

        const auto params = gmlc::utilities::str2vector(tokens, kNullVal);
        auto cof = CoreObjectFactory::instance();
        auto* exciterModel = static_cast<Exciter*>(cof->createObject("exciter", "exst1"));
        // Exact ANDES psse-dyr.yaml order: TR, VIMAX, VIMIN, TC, TB,
        // KA, TA, VRMAX, VRMIN, KC, KF, TF.
        exciterModel->set("tr", params[3]);
        exciterModel->set("vimax", params[4]);
        exciterModel->set("vimin", params[5]);
        exciterModel->set("tc", params[6]);
        exciterModel->set("tb", params[7]);
        exciterModel->set("ka", params[8]);
        exciterModel->set("ta", params[9]);
        exciterModel->set("vrmax", params[10]);
        exciterModel->set("vrmin", params[11]);
        exciterModel->set("kc", params[12]);
        exciterModel->set("kf", params[13]);
        exciterModel->set("tf", params[14]);

        gen->add(exciterModel);
    }

    void loadEXDC2(CoreObject* parentObject, stringVec& tokens)
    {
        const int busId = std::stoi(tokens[0]);
        const auto* bus = static_cast<GridBus*>(parentObject->findByUserID("bus", busId));
        const int genId = std::stoi(tokens[2]);
        auto* gen = bus->getGen(genId - 1);

        auto params = gmlc::utilities::str2vector(tokens, kNullVal);

        auto cof = CoreObjectFactory::instance();
        auto* exciterModel = static_cast<Exciter*>(cof->createObject("exciter", "dc2a"));
        // TODO(phlpt): TR not implemented yet; no voltage compensation implemented.
        // exciterModel->set("tr", params[3]);
        exciterModel->set("ka", params[4]);
        exciterModel->set("ta", params[5]);
        exciterModel->set("tb", params[6]);
        exciterModel->set("tc", params[7]);
        exciterModel->set("vrmax", params[8]);
        exciterModel->set("vrmin", params[9]);
        exciterModel->set("ke", params[10]);
        exciterModel->set("te", params[11]);
        exciterModel->set("kf", params[12]);
        exciterModel->set("tf", params[13]);
        // TODO(phlpt): Compute the saturation coefficients to translate appropriately.

        gen->add(exciterModel);
    }

    void loadSEXS(CoreObject* parentObject, stringVec& tokens)
    {
        const int busId = std::stoi(tokens[0]);
        const auto* bus = static_cast<GridBus*>(parentObject->findByUserID("bus", busId));
        const int genId = std::stoi(tokens[2]);
        auto* gen = bus->getGen(genId - 1);

        auto params = gmlc::utilities::str2vector(tokens, kNullVal);
        auto cof = CoreObjectFactory::instance();
        auto* exciterModel = static_cast<Exciter*>(cof->createObject("exciter", "sexs"));

        // exciterModel->set("tr", params[3]);
        exciterModel->set("ka", params[5]);
        exciterModel->set("tb", params[4]);
        exciterModel->set("ta", params[3] * params[4]);
        exciterModel->set("te", params[6]);
        exciterModel->set("vrmax", params[8]);
        exciterModel->set("vrmin", params[7]);

        gen->add(exciterModel);
    }
    void loadTGOV1(CoreObject* parentObject, stringVec& tokens)
    {
        const int busId = std::stoi(tokens[0]);
        const auto* bus = static_cast<GridBus*>(parentObject->findByUserID("bus", busId));
        const int genId = std::stoi(tokens[2]);
        auto* gen = bus->getGen(genId - 1);

        auto params = gmlc::utilities::str2vector(tokens, kNullVal);

        auto cof = CoreObjectFactory::instance();
        auto* governorModel = static_cast<Governor*>(cof->createObject("governor", "tgov1"));
        // PSS/e TGOV1 order after the machine identifier is
        // R, T1, VMAX, VMIN, T2, T3, Dt.  This matches ANDES's
        // psse-dyr.yaml conversion schema.
        governorModel->set("r", params[3]);
        governorModel->set("t1", params[4]);
        governorModel->set("pmax", params[5]);
        governorModel->set("pmin", params[6]);
        governorModel->set("t2", params[7]);
        governorModel->set("t3", params[8]);
        governorModel->set("dt", params[9]);

        gen->add(governorModel);
    }

    void loadIEEEG1(CoreObject* parentObject, stringVec& tokens)
    {
        if (tokens.size() != 25U) {
            throw InvalidParameterValue("IEEEG1 DYR record must contain 25 fields");
        }
        const int busId = std::stoi(tokens[0]);
        const auto* bus = static_cast<GridBus*>(parentObject->findByUserID("bus", busId));
        const int genId = std::stoi(tokens[2]);
        if ((bus == nullptr) || (genId <= 0)) {
            throw InvalidParameterValue("IEEEG1 primary generator identity");
        }
        auto* primary = dynamic_cast<DynamicGenerator*>(bus->getGen(genId - 1));
        if (primary == nullptr) {
            throw InvalidParameterValue("IEEEG1 requires a dynamic primary generator");
        }

        const auto params = gmlc::utilities::str2vector(tokens, kNullVal);
        const int secondBusId = static_cast<int>(params[3]);
        DynamicGenerator* secondary = nullptr;
        if (secondBusId != 0) {
            const auto* secondBus =
                dynamic_cast<GridBus*>(parentObject->findByUserID("bus", secondBusId));
            const int secondGenId = static_cast<int>(params[4]);
            if ((secondBus == nullptr) || (secondGenId <= 0)) {
                throw InvalidParameterValue("IEEEG1 secondary generator identity");
            }
            secondary = dynamic_cast<DynamicGenerator*>(secondBus->getGen(secondGenId - 1));
            if ((secondary == nullptr) || (secondary == primary)) {
                throw InvalidParameterValue(
                    "IEEEG1 requires a distinct dynamic secondary generator");
            }
        } else {
            constexpr double zeroTolerance = 1e-12;
            if ((std::abs(params[15]) > zeroTolerance) || (std::abs(params[18]) > zeroTolerance) ||
                (std::abs(params[21]) > zeroTolerance) || (std::abs(params[24]) > zeroTolerance)) {
                throw InvalidParameterValue(
                    "single-generator IEEEG1 requires K2, K4, K6, and K8 to be zero");
            }
        }

        auto cof = CoreObjectFactory::instance();
        std::unique_ptr<governors::GovernorIeeeG1> governor(
            dynamic_cast<governors::GovernorIeeeG1*>(cof->createObject("governor", "ieeeg1")));
        if (governor == nullptr) {
            throw InvalidParameterValue("IEEEG1 factory registration");
        }

        // Exact frozen ANDES psse-dyr.yaml order after BUS and ID:
        // BUS2, ID2, K, T1, T2, T3, UO, UC, PMAX, PMIN,
        // T4, K1, K2, T5, K3, K4, T6, K5, K6, T7, K7, K8.
        governor->set("k", params[5]);
        governor->set("t1", params[6]);
        governor->set("t2", params[7]);
        governor->set("t3", params[8]);
        governor->set("uo", params[9]);
        governor->set("uc", params[10]);
        governor->set("pmax", params[11]);
        governor->set("pmin", params[12]);
        governor->set("t4", params[13]);
        governor->set("k1", params[14]);
        governor->set("k2", params[15]);
        governor->set("t5", params[16]);
        governor->set("k3", params[17]);
        governor->set("k4", params[18]);
        governor->set("t6", params[19]);
        governor->set("k5", params[20]);
        governor->set("k6", params[21]);
        governor->set("t7", params[22]);
        governor->set("k7", params[23]);
        governor->set("k8", params[24]);

        auto* governorPointer = governor.release();
        primary->add(governorPointer);
        if (secondary != nullptr) {
            secondary->setMechanicalPowerSource(governorPointer,
                                                governors::GovernorIeeeG1::lpOutput);
        }
    }

    void loadST2CUT(CoreObject* parentObject, stringVec& tokens)
    {
        if (tokens.size() != 23U) {
            throw InvalidParameterValue("ST2CUT DYR record must contain 23 fields");
        }
        const int busId = std::stoi(tokens[0]);
        const auto* bus = static_cast<GridBus*>(parentObject->findByUserID("bus", busId));
        const int genId = std::stoi(tokens[2]);
        auto* generator = dynamic_cast<DynamicGenerator*>(bus->getGen(genId - 1));
        if (generator == nullptr) {
            throw InvalidParameterValue("ST2CUT requires a dynamic generator");
        }

        const auto params = gmlc::utilities::str2vector(tokens, kNullVal);
        auto* stabilizer = new stabilizers::StabilizerST2CUT();
        // Exact frozen ANDES psse-dyr.yaml order after BUS and ID:
        // MODE, BUSR, MODE2, BUSR2, K1, K2, T1, T2, T3, T4,
        // T5, T6, T7, T8, T9, T10, LSMAX, LSMIN, VCU, VCL.
        stabilizer->set("mode", params[3]);
        stabilizer->set("busr", params[4]);
        stabilizer->set("mode2", params[5]);
        stabilizer->set("busr2", params[6]);
        stabilizer->set("k1", params[7]);
        stabilizer->set("k2", params[8]);
        stabilizer->set("t1", params[9]);
        stabilizer->set("t2", params[10]);
        stabilizer->set("t3", params[11]);
        stabilizer->set("t4", params[12]);
        stabilizer->set("t5", params[13]);
        stabilizer->set("t6", params[14]);
        stabilizer->set("t7", params[15]);
        stabilizer->set("t8", params[16]);
        stabilizer->set("t9", params[17]);
        stabilizer->set("t10", params[18]);
        stabilizer->set("lsmax", params[19]);
        stabilizer->set("lsmin", params[20]);
        stabilizer->set("vcu", params[21]);
        stabilizer->set("vcl", params[22]);
        generator->add(stabilizer);
    }
}  // namespace

}  // namespace griddyn
