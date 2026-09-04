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
#include "griddyn/governors/GovernorHygov.h"
#include "griddyn/governors/GovernorIeeeG1.h"
#include "griddyn/governors/GovernorReheat.h"
#include "griddyn/stabilizers/StabilizerIEEEST.h"
#include "griddyn/stabilizers/StabilizerST2CUT.h"
#include <array>
#include <cmath>
#include <fstream>
#include <iostream>
#include <memory>
#include <string>

namespace griddyn {
namespace {
    void loadGENCLS(CoreObject* parentObject, stringVec& tokens);
    void loadGENROU(CoreObject* parentObject, stringVec& tokens);
    void loadGENSAL(CoreObject* parentObject, stringVec& tokens);
    void loadESDC1A(CoreObject* parentObject, stringVec& tokens);
    void loadESDC2A(CoreObject* parentObject, stringVec& tokens);
    void loadIEEET1(CoreObject* parentObject, stringVec& tokens);
    void loadESST3A(CoreObject* parentObject, stringVec& tokens);
    void loadESST4B(CoreObject* parentObject, stringVec& tokens);
    void loadEXPIC1(CoreObject* parentObject, stringVec& tokens);
    void loadEXST1(CoreObject* parentObject, stringVec& tokens);
    void loadEXAC1(CoreObject* parentObject, stringVec& tokens);
    void loadEXAC2(CoreObject* parentObject, stringVec& tokens);
    void loadEXAC4(CoreObject* parentObject, stringVec& tokens);
    void loadTGOV1(CoreObject* parentObject, stringVec& tokens);
    void loadHYGOV(CoreObject* parentObject, stringVec& tokens);
    void loadGGOV1(CoreObject* parentObject, stringVec& tokens);
    void loadGAST(CoreObject* parentObject, stringVec& tokens);
    void loadIEEEG1(CoreObject* parentObject, stringVec& tokens);
    void loadIEESGO(CoreObject* parentObject, stringVec& tokens);
    void loadIEEEST(CoreObject* parentObject, stringVec& tokens);
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
        if (type == "'GENCLS'") {
            loadGENCLS(parentObject, lineTokens);
        } else if (type == "'GENROU'") {
            loadGENROU(parentObject, lineTokens);
        } else if (type == "'GENSAL'") {
            loadGENSAL(parentObject, lineTokens);
        } else if (type == "'ESDC1A'") {
            loadESDC1A(parentObject, lineTokens);
        } else if (type == "'ESDC2A'") {
            loadESDC2A(parentObject, lineTokens);
        } else if (type == "'IEEET1'") {
            loadIEEET1(parentObject, lineTokens);
        } else if (type == "'ESST3A'") {
            loadESST3A(parentObject, lineTokens);
        } else if (type == "'ESST4B'") {
            loadESST4B(parentObject, lineTokens);
        } else if (type == "'EXPIC1'") {
            loadEXPIC1(parentObject, lineTokens);
        } else if (type == "'EXST1'") {
            loadEXST1(parentObject, lineTokens);
        } else if (type == "'EXAC1'") {
            loadEXAC1(parentObject, lineTokens);
        } else if (type == "'EXAC2'") {
            loadEXAC2(parentObject, lineTokens);
        } else if (type == "'EXAC4'") {
            loadEXAC4(parentObject, lineTokens);
        } else if (type == "'EXDC2'") {
            loadEXDC2(parentObject, lineTokens);
        } else if (type == "'TGOV1'") {
            loadTGOV1(parentObject, lineTokens);
        } else if (type == "'HYGOV'") {
            loadHYGOV(parentObject, lineTokens);
        } else if (type == "'GGOV1'") {
            loadGGOV1(parentObject, lineTokens);
        } else if (type == "'GAST'") {
            loadGAST(parentObject, lineTokens);
        } else if (type == "'IEEEG1'") {
            loadIEEEG1(parentObject, lineTokens);
        } else if (type == "'IEESGO'") {
            loadIEESGO(parentObject, lineTokens);
        } else if (type == "'IEEEST'") {
            loadIEEEST(parentObject, lineTokens);
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
    void loadGENCLS(CoreObject* parentObject, stringVec& tokens)
    {
        if (tokens.size() < 5) {
            throw InvalidParameterValue("GENCLS DYR record");
        }

        const int busId = std::stoi(tokens[0]);
        auto* bus = dynamic_cast<GridBus*>(parentObject->findByUserID("bus", busId));
        if (bus == nullptr) {
            throw InvalidParameterValue("GENCLS bus");
        }

        auto generatorId = gmlc::utilities::stringOps::removeQuotes(tokens[2]);
        gmlc::utilities::stringOps::trimString(generatorId);
        auto* gen = dynamic_cast<Generator*>(bus->find(bus->getName() + "_Gen_" + generatorId));
        if (gen == nullptr) {
            const int generatorIndex = std::stoi(generatorId) - 1;
            gen = bus->getGen(generatorIndex);
        }
        if (gen == nullptr) {
            throw InvalidParameterValue("GENCLS generator");
        }

        const auto params = gmlc::utilities::str2vector(tokens, kNullVal);
        auto* genModel = static_cast<GenModel*>(
            CoreObjectFactory::instance()->createObject("genmodel", "gencls"));
        // The RAW generator supplies ra and x'd. Attach before applying the
        // two GENCLS DYR parameters so DynamicGenerator transfers ZSOURCE.
        gen->add(genModel);
        genModel->set("h", params[3]);
        genModel->set("d", params[4]);
    }

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

    void loadGENSAL(CoreObject* parentObject, stringVec& tokens)
    {
        if (tokens.size() != 15U) {
            throw InvalidParameterValue("GENSAL DYR record must contain 15 fields");
        }
        const int busId = std::stoi(tokens[0]);
        const auto* bus = static_cast<GridBus*>(parentObject->findByUserID("bus", busId));
        const int genId = std::stoi(tokens[2]);
        if ((bus == nullptr) || (genId <= 0)) {
            throw InvalidParameterValue("GENSAL generator identity");
        }
        auto* gen = bus->getGen(genId - 1);
        if (gen == nullptr) {
            throw InvalidParameterValue("GENSAL generator identity");
        }
        const auto params = gmlc::utilities::str2vector(tokens, kNullVal);
        auto* model = static_cast<GenModel*>(
            CoreObjectFactory::instance()->createObject("genmodel", "gensal"));
        // Attach before replacing the RAW source reactance, as for GENROU.
        gen->add(model);
        model->set("tdop", params[3]);
        model->set("tdopp", params[4]);
        model->set("tqopp", params[5]);
        model->set("h", params[6]);
        model->set("d", params[7]);
        model->set("xd", params[8]);
        model->set("xq", params[9]);
        model->set("xdp", params[10]);
        model->set("xpp", params[11]);
        model->set("xl", params[12]);
        model->set("s10", params[13]);
        model->set("s12", params[14]);
    }

    void loadESDC1A(CoreObject* parentObject, stringVec& tokens)
    {
        if (tokens.size() != 19U) {
            throw InvalidParameterValue("ESDC1A DYR record must contain 19 fields");
        }
        const int busId = std::stoi(tokens[0]);
        const auto* bus = static_cast<GridBus*>(parentObject->findByUserID("bus", busId));
        const int genId = std::stoi(tokens[2]);
        auto* gen = bus->getGen(genId - 1);

        const auto params = gmlc::utilities::str2vector(tokens, kNullVal);
        const bool hasLeadLag = params[6] > 0.0;
        const auto* const modelName = hasLeadLag ? "esdc1a" : "ieeet1";
        auto* exciterModel = static_cast<Exciter*>(
            CoreObjectFactory::instance()->createObject("exciter", modelName));
        exciterModel->set("tr", params[3]);
        exciterModel->set("ka", params[4]);
        exciterModel->set("ta", params[5]);
        if (hasLeadLag) {
            exciterModel->set("tb", params[6]);
            exciterModel->set("tc", params[7]);
        }
        exciterModel->set("vrmax", params[8]);
        exciterModel->set("vrmin", params[9]);
        exciterModel->set("ke", params[10]);
        exciterModel->set("te", params[11]);
        exciterModel->set("kf", params[12]);
        exciterModel->set("tf", params[13]);
        exciterModel->set("e1", params[15]);
        exciterModel->set("se1", params[16]);
        exciterModel->set("e2", params[17]);
        exciterModel->set("se2", params[18]);

        gen->add(exciterModel);
    }

    void loadESDC2A(CoreObject* parentObject, stringVec& tokens)
    {
        if (tokens.size() != 19U) {
            throw InvalidParameterValue("ESDC2A DYR record must contain 19 fields");
        }
        const int busId = std::stoi(tokens[0]);
        const auto* bus = static_cast<GridBus*>(parentObject->findByUserID("bus", busId));
        const int genId = std::stoi(tokens[2]);
        auto* gen = bus->getGen(genId - 1);
        const auto params = gmlc::utilities::str2vector(tokens, kNullVal);
        auto* exciterModel =
            static_cast<Exciter*>(CoreObjectFactory::instance()->createObject("exciter", "esdc2a"));
        exciterModel->set("tr", params[3]);
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
        exciterModel->set("e1", params[15]);
        exciterModel->set("se1", params[16]);
        exciterModel->set("e2", params[17]);
        exciterModel->set("se2", params[18]);
        gen->add(exciterModel);
    }

    void loadIEEET1(CoreObject* parentObject, stringVec& tokens)
    {
        if (tokens.size() != 17U) {
            throw InvalidParameterValue("IEEET1 DYR record must contain 17 fields");
        }
        const int busId = std::stoi(tokens[0]);
        const auto* bus = static_cast<GridBus*>(parentObject->findByUserID("bus", busId));
        const int genId = std::stoi(tokens[2]);
        auto* gen = bus->getGen(genId - 1);
        const auto params = gmlc::utilities::str2vector(tokens, kNullVal);
        auto* exciterModel =
            static_cast<Exciter*>(CoreObjectFactory::instance()->createObject("exciter", "ieeet1"));
        exciterModel->set("tr", params[3]);
        exciterModel->set("ka", params[4]);
        exciterModel->set("ta", params[5]);
        exciterModel->set("vrmax", params[6]);
        exciterModel->set("vrmin", params[7]);
        exciterModel->set("ke", params[8]);
        exciterModel->set("te", params[9]);
        exciterModel->set("kf", params[10]);
        exciterModel->set("tf", params[11]);
        exciterModel->set("e1", params[13]);
        exciterModel->set("se1", params[14]);
        exciterModel->set("e2", params[15]);
        exciterModel->set("se2", params[16]);
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

    void loadESST4B(CoreObject* parentObject, stringVec& tokens)
    {
        if (tokens.size() != 20U) {
            throw InvalidParameterValue("ESST4B DYR record must contain 20 fields");
        }
        const int busId = std::stoi(tokens[0]);
        const auto* bus = static_cast<GridBus*>(parentObject->findByUserID("bus", busId));
        const int genId = std::stoi(tokens[2]);
        if ((bus == nullptr) || (genId <= 0)) {
            throw InvalidParameterValue("ESST4B generator identity");
        }
        auto* gen = bus->getGen(genId - 1);
        if (gen == nullptr) {
            throw InvalidParameterValue("ESST4B generator identity");
        }
        const auto params = gmlc::utilities::str2vector(tokens, kNullVal);
        auto* model =
            static_cast<Exciter*>(CoreObjectFactory::instance()->createObject("exciter", "esst4b"));
        model->set("tr", params[3]);
        model->set("kpr", params[4]);
        model->set("kir", params[5]);
        model->set("vrmax", params[6]);
        model->set("vrmin", params[7]);
        model->set("ta", params[8]);
        model->set("kpm", params[9]);
        model->set("kim", params[10]);
        model->set("vmmax", params[11]);
        model->set("vmmin", params[12]);
        model->set("kg", params[13]);
        model->set("kp", params[14]);
        model->set("ki", params[15]);
        model->set("vbmax", params[16]);
        model->set("kc", params[17]);
        model->set("xl", params[18]);
        model->set("thetap", params[19]);
        gen->add(model);
    }

    void loadEXPIC1(CoreObject* parentObject, stringVec& tokens)
    {
        if (tokens.size() != 27U) {
            throw InvalidParameterValue("EXPIC1 DYR record must contain 27 fields");
        }
        const int busId = std::stoi(tokens[0]);
        const auto* bus = static_cast<GridBus*>(parentObject->findByUserID("bus", busId));
        const int genId = std::stoi(tokens[2]);
        if ((bus == nullptr) || (genId <= 0)) {
            throw InvalidParameterValue("EXPIC1 generator identity");
        }
        auto* gen = bus->getGen(genId - 1);
        if (gen == nullptr) {
            throw InvalidParameterValue("EXPIC1 requires an existing generator");
        }
        const auto params = gmlc::utilities::str2vector(tokens, kNullVal);
        auto* model =
            static_cast<Exciter*>(CoreObjectFactory::instance()->createObject("exciter", "expic1"));
        static constexpr std::array<std::string_view, 24> names{"tr",    "ka",     "ta1",    "vr1",
                                                                "vr2",   "ta2",    "ta3",    "ta4",
                                                                "vrmax", "vrmin",  "kf",     "tf1",
                                                                "tf2",   "efdmax", "efdmin", "ke",
                                                                "te",    "e1",     "se1",    "e2",
                                                                "se2",   "kp",     "ki",     "kc"};
        for (std::size_t ii = 0; ii < names.size(); ++ii) {
            model->set(names[ii], params[ii + 3]);
        }
        gen->add(model);
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

    void loadEXAC1(CoreObject* parentObject, stringVec& tokens)
    {
        const int busId = std::stoi(tokens[0]);
        const auto* bus = static_cast<GridBus*>(parentObject->findByUserID("bus", busId));
        auto* gen = bus->getGen(std::stoi(tokens[2]) - 1);
        const auto params = gmlc::utilities::str2vector(tokens, kNullVal);
        auto* exciter =
            static_cast<Exciter*>(CoreObjectFactory::instance()->createObject("exciter", "exac1"));
        // Exact ANDES psse-dyr.yaml order: TR, TB, TC, KA, TA, VRMAX,
        // VRMIN, TE, KF, TF, KC, KD, KE, E1, SE1, E2, SE2.
        exciter->set("tr", params[3]);
        exciter->set("tb", params[4]);
        exciter->set("tc", params[5]);
        exciter->set("ka", params[6]);
        exciter->set("ta", params[7]);
        exciter->set("vrmax", params[8]);
        exciter->set("vrmin", params[9]);
        exciter->set("te", params[10]);
        exciter->set("kf", params[11]);
        exciter->set("tf", params[12]);
        exciter->set("kc", params[13]);
        exciter->set("kd", params[14]);
        exciter->set("ke", params[15]);
        exciter->set("e1", params[16]);
        exciter->set("se1", params[17]);
        exciter->set("e2", params[18]);
        exciter->set("se2", params[19]);
        gen->add(exciter);
    }

    void loadEXAC2(CoreObject* parentObject, stringVec& tokens)
    {
        const int busId = std::stoi(tokens[0]);
        const auto* bus = static_cast<GridBus*>(parentObject->findByUserID("bus", busId));
        auto* gen = bus->getGen(std::stoi(tokens[2]) - 1);
        const auto params = gmlc::utilities::str2vector(tokens, kNullVal);
        auto* exciter =
            static_cast<Exciter*>(CoreObjectFactory::instance()->createObject("exciter", "exac2"));
        // Exact ANDES psse-dyr.yaml order: TR, TB, TC, KA, TA, VAMAX,
        // VAMIN, KB, VRMAX, VRMIN, TE, KL, KH, KF, TF, KC, KD, KE, VLR,
        // E1, SE1, E2, SE2.
        exciter->set("tr", params[3]);
        exciter->set("tb", params[4]);
        exciter->set("tc", params[5]);
        exciter->set("ka", params[6]);
        exciter->set("ta", params[7]);
        exciter->set("vamax", params[8]);
        exciter->set("vamin", params[9]);
        exciter->set("kb", params[10]);
        exciter->set("vrmax", params[11]);
        exciter->set("vrmin", params[12]);
        exciter->set("te", params[13]);
        exciter->set("kl", params[14]);
        exciter->set("kh", params[15]);
        exciter->set("kf", params[16]);
        exciter->set("tf", params[17]);
        exciter->set("kc", params[18]);
        exciter->set("kd", params[19]);
        exciter->set("ke", params[20]);
        exciter->set("vlr", params[21]);
        exciter->set("e1", params[22]);
        exciter->set("se1", params[23]);
        exciter->set("e2", params[24]);
        exciter->set("se2", params[25]);
        gen->add(exciter);
    }

    void loadEXAC4(CoreObject* parentObject, stringVec& tokens)
    {
        const int busId = std::stoi(tokens[0]);
        const auto* bus = static_cast<GridBus*>(parentObject->findByUserID("bus", busId));
        auto* gen = bus->getGen(std::stoi(tokens[2]) - 1);
        const auto params = gmlc::utilities::str2vector(tokens, kNullVal);
        auto* exciter =
            static_cast<Exciter*>(CoreObjectFactory::instance()->createObject("exciter", "exac4"));
        // Exact ANDES psse-dyr.yaml order: TR, VIMAX, VIMIN, TC, TB, KA,
        // TA, VRMAX, VRMIN, KC.
        exciter->set("tr", params[3]);
        exciter->set("vimax", params[4]);
        exciter->set("vimin", params[5]);
        exciter->set("tc", params[6]);
        exciter->set("tb", params[7]);
        exciter->set("ka", params[8]);
        exciter->set("ta", params[9]);
        exciter->set("vrmax", params[10]);
        exciter->set("vrmin", params[11]);
        exciter->set("kc", params[12]);
        gen->add(exciter);
    }

    void loadEXDC2(CoreObject* parentObject, stringVec& tokens)
    {
        if (tokens.size() != 19U) {
            throw InvalidParameterValue("EXDC2 DYR record must contain 19 fields");
        }
        const int busId = std::stoi(tokens[0]);
        const auto* bus = static_cast<GridBus*>(parentObject->findByUserID("bus", busId));
        const int genId = std::stoi(tokens[2]);
        auto* gen = bus->getGen(genId - 1);

        auto params = gmlc::utilities::str2vector(tokens, kNullVal);

        auto cof = CoreObjectFactory::instance();
        auto* exciterModel = static_cast<Exciter*>(cof->createObject("exciter", "exdc2"));
        exciterModel->set("tr", params[3]);
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
        exciterModel->set("e1", params[15]);
        exciterModel->set("se1", params[16]);
        exciterModel->set("e2", params[17]);
        exciterModel->set("se2", params[18]);

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

    void loadHYGOV(CoreObject* parentObject, stringVec& tokens)
    {
        if (tokens.size() != 15U) {
            throw InvalidParameterValue("HYGOV DYR record must contain 15 fields");
        }
        const int busId = std::stoi(tokens[0]);
        const auto* bus = static_cast<GridBus*>(parentObject->findByUserID("bus", busId));
        const int genId = std::stoi(tokens[2]);
        if ((bus == nullptr) || (genId <= 0)) {
            throw InvalidParameterValue("HYGOV generator identity");
        }
        auto* gen = bus->getGen(genId - 1);
        if (gen == nullptr) {
            throw InvalidParameterValue("HYGOV requires an existing generator");
        }

        const auto params = gmlc::utilities::str2vector(tokens, kNullVal);
        auto cof = CoreObjectFactory::instance();
        std::unique_ptr<governors::GovernorHygov> governor(
            dynamic_cast<governors::GovernorHygov*>(cof->createObject("governor", "hygov")));
        if (governor == nullptr) {
            throw InvalidParameterValue("HYGOV factory registration");
        }

        // PSS/e HYGOV order after the machine identifier is
        // R, r, Tr, Tf, Tg, VELM, GMAX, GMIN, Tw, At, Dturb, qNL.
        // This matches ANDES's psse-dyr.yaml conversion schema.
        governor->set("r", params[3]);
        governor->set("temporarydroop", params[4]);
        governor->set("tr", params[5]);
        governor->set("tf", params[6]);
        governor->set("tg", params[7]);
        governor->set("velm", params[8]);
        governor->set("gmax", params[9]);
        governor->set("gmin", params[10]);
        governor->set("tw", params[11]);
        governor->set("at", params[12]);
        governor->set("dturb", params[13]);
        governor->set("qnl", params[14]);

        gen->add(governor.release());
    }

    void loadGGOV1(CoreObject* parentObject, stringVec& tokens)
    {
        if (tokens.size() != 38U) {
            throw InvalidParameterValue("GGOV1 DYR record must contain 38 fields");
        }
        const int busId = std::stoi(tokens[0]);
        const auto* bus = static_cast<GridBus*>(parentObject->findByUserID("bus", busId));
        const int genId = std::stoi(tokens[2]);
        if ((bus == nullptr) || (genId <= 0)) {
            throw InvalidParameterValue("GGOV1 generator identity");
        }
        auto* gen = bus->getGen(genId - 1);
        if (gen == nullptr) {
            throw InvalidParameterValue("GGOV1 generator identity");
        }
        const auto params = gmlc::utilities::str2vector(tokens, kNullVal);
        auto* model = static_cast<Governor*>(
            CoreObjectFactory::instance()->createObject("governor", "ggov1"));
        static constexpr std::array<std::string_view, 35> names{
            "rselect", "fswitch", "r",     "tpelec", "maxerr", "minerr", "kpgov",
            "kigov",   "kdgov",   "tdgov", "vmax",   "vmin",   "tact",   "kturb",
            "wfnl",    "tb",      "tc",    "teng",   "tfload", "kpload", "kiload",
            "ldref",   "dm",      "ropen", "rclose", "kimw",   "aset",   "ka",
            "ta",      "trate",   "db",    "tsa",    "tsb",    "rup",    "rdown"};
        for (std::size_t ii = 0; ii < names.size(); ++ii) {
            model->set(names[ii], params[ii + 3]);
        }
        gen->add(model);
    }

    void loadGAST(CoreObject* parentObject, stringVec& tokens)
    {
        if (tokens.size() != 12U) {
            throw InvalidParameterValue("GAST DYR record must contain 12 fields");
        }
        const int busId = std::stoi(tokens[0]);
        const auto* bus = static_cast<GridBus*>(parentObject->findByUserID("bus", busId));
        const int genId = std::stoi(tokens[2]);
        if ((bus == nullptr) || (genId <= 0)) {
            throw InvalidParameterValue("GAST generator identity");
        }
        auto* gen = bus->getGen(genId - 1);
        if (gen == nullptr) {
            throw InvalidParameterValue("GAST requires an existing generator");
        }
        const auto params = gmlc::utilities::str2vector(tokens, kNullVal);
        auto* model =
            static_cast<Governor*>(CoreObjectFactory::instance()->createObject("governor", "gast"));
        static constexpr std::array<std::string_view, 9> names{
            "r", "t1", "t2", "t3", "at", "kt", "vmax", "vmin", "dt"};
        for (std::size_t ii = 0; ii < names.size(); ++ii) {
            model->set(names[ii], params[ii + 3]);
        }
        gen->add(model);
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

    void loadIEESGO(CoreObject* parentObject, stringVec& tokens)
    {
        // BUS, 'IEESGO', ID, T1, T2, T3, T4, T5, T6, K1, K2, K3, PMAX, PMIN /
        if (tokens.size() != 14U) {
            throw InvalidParameterValue("IEESGO DYR record must contain 14 fields");
        }
        const int busId = std::stoi(tokens[0]);
        const int genId = std::stoi(tokens[2]);
        const auto* bus = dynamic_cast<GridBus*>(parentObject->findByUserID("bus", busId));
        if ((bus == nullptr) || (genId <= 0)) {
            throw InvalidParameterValue("IEESGO generator identity");
        }
        auto* gen = bus->getGen(genId - 1);
        if (gen == nullptr) {
            throw InvalidParameterValue("IEESGO requires an existing generator");
        }
        const auto params = gmlc::utilities::str2vector(tokens, kNullVal);
        std::unique_ptr<governors::GovernorReheat> governor(
            dynamic_cast<governors::GovernorReheat*>(
                CoreObjectFactory::instance()->createObject("governor", "ieesgo")));
        if (governor == nullptr) {
            throw InvalidParameterValue("IEESGO factory registration");
        }
        static constexpr std::array<std::string_view, 11> names{
            "t1", "t2", "t3", "t4", "t5", "t6", "k1", "k2", "k3", "pmax", "pmin"};
        for (std::size_t ii = 0; ii < names.size(); ++ii) {
            governor->set(names[ii], params[ii + 3]);
        }
        gen->add(governor.release());
    }

    void loadST2CUT(CoreObject* parentObject, stringVec& tokens)
    {
        if (tokens.size() != 23U) {
            throw InvalidParameterValue("ST2CUT DYR record must contain 23 fields");
        }
        const int busId = std::stoi(tokens[0]);
        const auto* bus = static_cast<GridBus*>(parentObject->findByUserID("bus", busId));
        const int genId = std::stoi(tokens[2]);
        if ((bus == nullptr) || (genId <= 0)) {
            throw InvalidParameterValue("ST2CUT generator identity");
        }
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

    void loadIEEEST(CoreObject* parentObject, stringVec& tokens)
    {
        if (tokens.size() != 22U) {
            throw InvalidParameterValue("IEEEST DYR record must contain 22 fields");
        }
        const int busId = std::stoi(tokens[0]);
        const auto* bus = static_cast<GridBus*>(parentObject->findByUserID("bus", busId));
        const int genId = std::stoi(tokens[2]);
        if ((bus == nullptr) || (genId <= 0)) {
            throw InvalidParameterValue("IEEEST generator identity");
        }
        auto* generator = dynamic_cast<DynamicGenerator*>(bus->getGen(genId - 1));
        if (generator == nullptr) {
            throw InvalidParameterValue("IEEEST requires a dynamic generator");
        }

        const auto params = gmlc::utilities::str2vector(tokens, kNullVal);
        auto* stabilizer = new stabilizers::StabilizerIEEEST();
        // Exact frozen ANDES psse-dyr.yaml order after BUS and ID:
        // MODE, BUSR, A1, A2, A3, A4, A5, A6, T1, T2, T3, T4,
        // T5, T6, KS, LSMAX, LSMIN, VCU, VCL.
        stabilizer->set("mode", params[3]);
        stabilizer->set("busr", params[4]);
        stabilizer->set("a1", params[5]);
        stabilizer->set("a2", params[6]);
        stabilizer->set("a3", params[7]);
        stabilizer->set("a4", params[8]);
        stabilizer->set("a5", params[9]);
        stabilizer->set("a6", params[10]);
        stabilizer->set("t1", params[11]);
        stabilizer->set("t2", params[12]);
        stabilizer->set("t3", params[13]);
        stabilizer->set("t4", params[14]);
        stabilizer->set("t5", params[15]);
        stabilizer->set("t6", params[16]);
        stabilizer->set("ks", params[17]);
        stabilizer->set("lsmax", params[18]);
        stabilizer->set("lsmin", params[19]);
        stabilizer->set("vcu", params[20]);
        stabilizer->set("vcl", params[21]);
        generator->add(stabilizer);
    }
}  // namespace

}  // namespace griddyn
