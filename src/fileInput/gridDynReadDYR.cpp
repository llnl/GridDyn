/*
 * Copyright (c) 2014-2026, Lawrence Livermore National Security
 * See the top-level NOTICE for additional details. All rights reserved.
 * SPDX-License-Identifier: BSD-3-Clause
 */

#include "core/coreDefinitions.hpp"
#include "core/coreObject.h"
#include "core/objectFactory.hpp"
#include "fileInput.h"
#include "gmlc/utilities/stringConversion.h"
#include "gmlc/utilities/stringOps.h"
#include "griddyn/Exciter.h"
#include "griddyn/GenModel.h"
#include "griddyn/Generator.h"
#include "griddyn/Governor.h"
#include "griddyn/gridBus.h"
#include "readerInfo.h"
#include <fstream>
#include <iostream>
#include <string>

namespace griddyn {
namespace {
    void loadGENROU(CoreObject* parentObject, stringVec& tokens);
    void loadESDC1A(CoreObject* parentObject, stringVec& tokens);
    void loadTGOV1(CoreObject* parentObject, stringVec& tokens);
    void loadEXDC2(CoreObject* parentObject, stringVec& tokens);
    void loadSEXS(CoreObject* parentObject, stringVec& tokens);
}  // namespace

void loadDyr(CoreObject* parentObject,
             const std::string& fileName,
             const basicReaderInfo& /*readerOptions*/)
{
    std::ifstream file(fileName.c_str(), std::ios::in);
    std::string line;  // line storage
    std::string continuedLine;

    if (!(file.is_open())) {
        parentObject->log(parentObject, print_level::error, "Unable to open file " + fileName);
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
        } else if (type == "'EXDC2'") {
            loadEXDC2(parentObject, lineTokens);
        } else if (type == "'TGOV1'") {
            loadTGOV1(parentObject, lineTokens);
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
        const auto* bus = static_cast<gridBus*>(parentObject->findByUserID("bus", busId));
        const int genId = std::stoi(tokens[2]);
        auto* gen = bus->getGen(genId - 1);

        auto params = gmlc::utilities::str2vector(tokens, kNullVal);

        auto cof = coreObjectFactory::instance();
        auto* genModel = static_cast<GenModel*>(cof->createObject("genmodel", "6"));
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

        gen->add(genModel);
    }

    void loadESDC1A(CoreObject* parentObject, stringVec& tokens)
    {
        const int busId = std::stoi(tokens[0]);
        const auto* bus = static_cast<gridBus*>(parentObject->findByUserID("bus", busId));
        const int genId = std::stoi(tokens[2]);
        auto* gen = bus->getGen(genId - 1);

        auto params = gmlc::utilities::str2vector(tokens, kNullVal);
        Exciter* exciterModel;
        auto cof = coreObjectFactory::instance();
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

    void loadEXDC2(CoreObject* parentObject, stringVec& tokens)
    {
        const int busId = std::stoi(tokens[0]);
        const auto* bus = static_cast<gridBus*>(parentObject->findByUserID("bus", busId));
        const int genId = std::stoi(tokens[2]);
        auto* gen = bus->getGen(genId - 1);

        auto params = gmlc::utilities::str2vector(tokens, kNullVal);

        auto cof = coreObjectFactory::instance();
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
        const auto* bus = static_cast<gridBus*>(parentObject->findByUserID("bus", busId));
        const int genId = std::stoi(tokens[2]);
        auto* gen = bus->getGen(genId - 1);

        auto params = gmlc::utilities::str2vector(tokens, kNullVal);
        auto cof = coreObjectFactory::instance();
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
        const auto* bus = static_cast<gridBus*>(parentObject->findByUserID("bus", busId));
        const int genId = std::stoi(tokens[2]);
        auto* gen = bus->getGen(genId - 1);

        auto params = gmlc::utilities::str2vector(tokens, kNullVal);

        auto cof = coreObjectFactory::instance();
        auto* governorModel = static_cast<Governor*>(cof->createObject("governor", "tgov1"));
        // TODO(phlpt): TR not implemented yet; no voltage compensation implemented.
        // governorModel->set("tr", params[3]);
        governorModel->set("r", params[3]);
        governorModel->set("t1", params[4]);
        governorModel->set("pmax", params[5]);
        governorModel->set("pmin", params[6]);
        governorModel->set("t2", params[6]);
        governorModel->set("t3", params[7]);
        governorModel->set("dt", params[8]);

        gen->add(governorModel);
    }
}  // namespace

}  // namespace griddyn

