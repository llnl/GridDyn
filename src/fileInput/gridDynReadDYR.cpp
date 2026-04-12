/*
 * Copyright (c) 2014-2020, Lawrence Livermore National Security
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
#include <fstream>
#include <iostream>
#include <memory>
#include <string>

namespace griddyn {
namespace {
    void loadGENROU(coreObject* parentObject, stringVec& tokens);
    void loadESDC1A(coreObject* parentObject, stringVec& tokens);
    void loadTGOV1(coreObject* parentObject, stringVec& tokens);
    void loadEXDC2(coreObject* parentObject, stringVec& tokens);
    void loadSEXS(coreObject* parentObject, stringVec& tokens);
}  // namespace

void loadDYR(coreObject* parentObject, const std::string& fileName, const basicReaderInfo& /*bri*/)
{
    std::ifstream file(fileName.c_str(), std::ios::in);
    std::string line;  // line storage
    std::string line2;

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
            if (std::getline(file, line2)) {
                gmlc::utilities::stringOps::trimString(line2);
                line += ' ' + line2;
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
    void loadGENROU(coreObject* parentObject, stringVec& tokens)
    {
        int id = std::stoi(tokens[0]);
        gridBus* bus = static_cast<gridBus*>(parentObject->findByUserID("bus", id));
        id = std::stoi(tokens[2]);
        Generator* gen = bus->getGen(id - 1);

        auto params = gmlc::utilities::str2vector(tokens, kNullVal);

        auto cof = coreObjectFactory::instance();
        auto* sm = static_cast<GenModel*>(cof->createObject("genmodel", "6"));
        sm->set("tdop", params[3]);
        sm->set("tdopp", params[4]);
        sm->set("tqop", params[5]);
        sm->set("tqopp", params[6]);
        sm->set("h", params[7]);
        sm->set("d", params[8]);
        sm->set("xd", params[9]);
        sm->set("xq", params[10]);
        sm->set("xdp", params[11]);
        sm->set("xqp", params[12]);
        sm->set("xdpp", params[13]);
        sm->set("xqpp", params[13]);
        sm->set("xl", params[14]);
        sm->set("s1", params[15]);
        sm->set("s12", params[16]);

        gen->add(sm);
    }

    void loadESDC1A(coreObject* parentObject, stringVec& tokens)
    {
        int id = std::stoi(tokens[0]);
        gridBus* bus = static_cast<gridBus*>(parentObject->findByUserID("bus", id));
        id = std::stoi(tokens[2]);
        Generator* gen = bus->getGen(id - 1);

        auto params = gmlc::utilities::str2vector(tokens, kNullVal);
        Exciter* sm;
        auto cof = coreObjectFactory::instance();
        if (params[6] > 0.0)  // dc1a model must have tb>0 otherwise revert to type1
        {
            sm = static_cast<Exciter*>(cof->createObject("exciter", "dc1a"));
        } else {
            sm = static_cast<Exciter*>(cof->createObject("exciter", "type1"));
        }
        // TODO(phlpt): TR not implemented yet; no voltage compensation implemented.
        // sm->set("tr", params[3]);
        sm->set("ka", params[4]);
        sm->set("ta", params[5]);
        if (params[6] > 0) {
            sm->set("tb", params[6]);
            sm->set("tc", params[7]);
        }
        sm->set("vrmax", params[8]);
        sm->set("vrmin", params[9]);
        sm->set("ke", params[10]);
        sm->set("te", params[11]);
        sm->set("kf", params[12]);
        sm->set("tf", params[13]);
        // TODO(phlpt): Compute the saturation coefficients to translate appropriately.

        gen->add(sm);
    }

    void loadEXDC2(coreObject* parentObject, stringVec& tokens)
    {
        int id = std::stoi(tokens[0]);
        gridBus* bus = static_cast<gridBus*>(parentObject->findByUserID("bus", id));
        id = std::stoi(tokens[2]);
        Generator* gen = bus->getGen(id - 1);

        auto params = gmlc::utilities::str2vector(tokens, kNullVal);

        auto cof = coreObjectFactory::instance();
        auto* sm = static_cast<Exciter*>(cof->createObject("exciter", "dc2a"));
        // TODO(phlpt): TR not implemented yet; no voltage compensation implemented.
        // sm->set("tr", params[3]);
        sm->set("ka", params[4]);
        sm->set("ta", params[5]);
        sm->set("tb", params[6]);
        sm->set("tc", params[7]);
        sm->set("vrmax", params[8]);
        sm->set("vrmin", params[9]);
        sm->set("ke", params[10]);
        sm->set("te", params[11]);
        sm->set("kf", params[12]);
        sm->set("tf", params[13]);
        // TODO(phlpt): Compute the saturation coefficients to translate appropriately.

        gen->add(sm);
    }

    void loadSEXS(coreObject* parentObject, stringVec& tokens)
    {
        int id = std::stoi(tokens[0]);
        gridBus* bus = static_cast<gridBus*>(parentObject->findByUserID("bus", id));
        id = std::stoi(tokens[2]);
        Generator* gen = bus->getGen(id - 1);

        auto params = gmlc::utilities::str2vector(tokens, kNullVal);
        auto cof = coreObjectFactory::instance();
        auto* sm = static_cast<Exciter*>(cof->createObject("exciter", "sexs"));

        // sm->set("tr", params[3]);
        sm->set("ka", params[5]);
        sm->set("tb", params[4]);
        sm->set("ta", params[3] * params[4]);
        sm->set("te", params[6]);
        sm->set("vrmax", params[8]);
        sm->set("vrmin", params[7]);

        gen->add(sm);
    }
    void loadTGOV1(coreObject* parentObject, stringVec& tokens)
    {
        int id = std::stoi(tokens[0]);
        gridBus* bus = static_cast<gridBus*>(parentObject->findByUserID("bus", id));
        id = std::stoi(tokens[2]);
        Generator* gen = bus->getGen(id - 1);

        auto params = gmlc::utilities::str2vector(tokens, kNullVal);

        auto cof = coreObjectFactory::instance();
        auto* sm = static_cast<Governor*>(cof->createObject("governor", "tgov1"));
        // TODO(phlpt): TR not implemented yet; no voltage compensation implemented.
        // sm->set("tr", params[3]);
        sm->set("r", params[3]);
        sm->set("t1", params[4]);
        sm->set("pmax", params[5]);
        sm->set("pmin", params[6]);
        sm->set("t2", params[6]);
        sm->set("t3", params[7]);
        sm->set("dt", params[8]);

        gen->add(sm);
    }
}  // namespace

}  // namespace griddyn
