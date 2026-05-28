/*
 * Copyright (c) 2014-2026, Lawrence Livermore National Security
 * See the top-level NOTICE for additional details. All rights reserved.
 * SPDX-License-Identifier: BSD-3-Clause
 */

#include "GrabberInterpreter.hpp"

#include "../Generator.h"
#include "../GridArea.h"
#include "../GridBus.h"
#include "../Link.h"
#include "../Load.h"
#include "../simulation/GridSimulation.h"
#include "GridGrabbers.h"
#include "ObjectGrabbers.h"
#include "gmlc/utilities/vectorOps.hpp"
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace griddyn {

void autoGrabbers(CoreObject* obj, std::vector<std::unique_ptr<GridGrabber>>& v);
void allGrabbers(std::string_view mode,
                 CoreObject* obj,
                 std::vector<std::unique_ptr<GridGrabber>>& v);

void perObjectGrabbers(std::string_view cmd,
                       CoreObject* obj,
                       std::vector<std::unique_ptr<GridGrabber>>& v);

static GrabberInterpreter<GridGrabber, OpGrabber, FunctionGrabber>
    gInterpret([](std::string_view fld, CoreObject* obj) { return createGrabber(fld, obj); });

bool isOperatorOutsideBlocks(const std::vector<std::pair<size_t, size_t>>& blocks, size_t loc)
{
    if (loc < blocks[0].first) {
        return true;
    }
    if (loc > blocks.back().second) {
        return true;
    }
    for (auto blk : blocks) {
        if ((loc > blk.first) && (loc < blk.second)) {
            return false;
        }
    }
    return true;
}
std::vector<std::unique_ptr<GridGrabber>> makeGrabbers(std::string_view command, CoreObject* obj)
{
    std::vector<std::unique_ptr<GridGrabber>> v;
    auto gstr = gmlc::utilities::stringOps::splitlineBracket(std::string{command});
    gmlc::utilities::stringOps::trim(gstr);
    for (auto& cmd : gstr) {
        auto renameloc = cmd.find(" as ");  // spaces are important
        // extract out a rename
        std::string rname;
        if (renameloc != std::string::npos) {
            rname = gmlc::utilities::stringOps::trim(cmd.substr(renameloc + 4));
            cmd.erase(renameloc, std::string::npos);
        }
        if (cmd.find_first_of(R"(:(+-/*\^?)") != std::string::npos) {
            auto ggb = gInterpret.interpretGrabberBlock(cmd, obj);
            if (ggb) {
                if (!rname.empty()) {
                    ggb->setDescription(rname);
                }

                if (ggb->loaded) {
                    v.push_back(std::move(ggb));
                } else if (ggb->field.compare(0, 3, "all") == 0) {
                    allGrabbers(cmd, ggb->getObject(), v);
                } else if (ggb->field == "auto") {
                    autoGrabbers(ggb->getObject(), v);
                } else if (ggb->field.compare(0, 5, "state") == 0) {
                    v.push_back(std::move(ggb));
                } else {
                    obj->log(obj,
                             PrintLevel::WARNING,
                             std::string{"Unable to load recorder "} + std::string{command});
                }
            }
        } else {
            std::string cmdlc = gmlc::utilities::convertToLowerCase(cmd);
            if (cmdlc.compare(0, 3, "all") == 0) {
                allGrabbers(cmd, obj, v);
            } else if (cmdlc == "auto") {
                autoGrabbers(obj, v);
            } else if (cmdlc.compare(0, 4, "per_") == 0) {
                perObjectGrabbers(cmd, obj, v);
            } else {  // create a single grabber
                auto ggb = createGrabber(cmdlc, obj);
                if (ggb) {
                    if (!rname.empty()) {
                        ggb->setDescription(rname);
                    }
                    v.push_back(std::move(ggb));
                }
            }
        }
    }
    return v;
}

void autoGrabbers(CoreObject* obj, std::vector<std::unique_ptr<GridGrabber>>& v)
{
    auto bus = dynamic_cast<GridBus*>(obj);
    if (bus != nullptr) {
        v.reserve(v.size() + 5);
        v.push_back(std::make_unique<ObjectGrabber<GridBus>>("voltage", bus));

        v.push_back(std::make_unique<ObjectGrabber<GridBus>>("angle", bus));

        v.push_back(std::make_unique<ObjectGrabber<GridBus>>("gen", bus));

        v.push_back(std::make_unique<ObjectGrabber<GridBus>>("load", bus));

        v.push_back(std::make_unique<ObjectGrabber<GridBus>>("freq", bus));
        return;
    }

    auto ld = dynamic_cast<GridLoad*>(obj);
    if (ld != nullptr) {
        v.reserve(v.size() + 2);
        v.push_back(std::make_unique<ObjectGrabber<GridLoad>>("p", ld));

        v.push_back(std::make_unique<ObjectGrabber<GridLoad>>("q", ld));
        return;
    }

    auto gen = dynamic_cast<Generator*>(obj);
    if (gen != nullptr) {
        v.reserve(v.size() + 2);
        v.push_back(std::make_unique<ObjectGrabber<Generator>>("p", gen));

        v.push_back(std::make_unique<ObjectGrabber<Generator>>("q", gen));

        return;
    }

    auto lnk = dynamic_cast<Link*>(obj);
    if (lnk != nullptr) {
        v.reserve(v.size() + 3);
        v.push_back(std::make_unique<ObjectGrabber<Link>>("p1", lnk));

        v.push_back(std::make_unique<ObjectGrabber<Link>>("q1", lnk));

        v.push_back(std::make_unique<ObjectGrabber<Link>>("loss", lnk));
        return;
    }

    // get the vector grabs if this is a simulation
    auto gds = dynamic_cast<GridSimulation*>(obj);
    if (gds != nullptr) {
        v.reserve(v.size() + 4);

        v.push_back(std::make_unique<ObjectGrabber<GridArea>>("voltage", gds));

        v.push_back(std::make_unique<ObjectGrabber<GridArea>>("angle", gds));

        v.push_back(std::make_unique<ObjectGrabber<GridArea>>("busgenerationreal", gds));

        v.push_back(std::make_unique<ObjectGrabber<GridArea>>("busloadreal", gds));
        return;
    }

    auto area = dynamic_cast<GridArea*>(obj);
    if (area != nullptr) {
        v.reserve(v.size() + 6);

        v.push_back(std::make_unique<ObjectGrabber<GridArea>>("generationreal", area));

        v.push_back(std::make_unique<ObjectGrabber<GridArea>>("generationreactive", area));

        v.push_back(std::make_unique<ObjectGrabber<GridArea>>("loadreal", area));

        v.push_back(std::make_unique<ObjectGrabber<GridArea>>("loadreactive", area));

        v.push_back(std::make_unique<ObjectGrabber<GridArea>>("loss", area));

        v.push_back(std::make_unique<ObjectGrabber<GridArea>>("tieflowreal", area));
        return;
    }
}
void perObjectGrabbers(std::string_view cmd,
                       CoreObject* obj,
                       std::vector<std::unique_ptr<GridGrabber>>& v)
{
    if (cmd.compare(0, 4, "per_") != 0) {
        return;
    }
    auto bstart = cmd.find_first_of('[');
    if (bstart == std::string::npos) {
        return;
    }
    std::string fieldlist{cmd.substr(bstart + 1)};
    while (!fieldlist.empty() && fieldlist.back() != ']') {
        fieldlist.pop_back();
    }
    if (!fieldlist.empty()) {
        fieldlist.pop_back();
    }

    auto objType = cmd.substr(4, bstart - 4);
    index_t index = 0;
    auto* sub_obj = obj->getSubObject(objType, index);
    while (sub_obj != nullptr) {
        auto gbrs = makeGrabbers(fieldlist, sub_obj);
        for (auto& gb : gbrs) {
            v.push_back(std::move(gb));
        }
        ++index;
        sub_obj = obj->getSubObject(objType, index);
    }
}

void allGrabbers(std::string_view mode,
                 CoreObject* obj,
                 std::vector<std::unique_ptr<GridGrabber>>& v)
{
    auto bus = dynamic_cast<GridBus*>(obj);
    if (bus != nullptr) {
        v.reserve(v.size() + 5);

        v.push_back(std::make_unique<ObjectGrabber<GridBus>>("voltage", bus));

        v.push_back(std::make_unique<ObjectGrabber<GridBus>>("angle", bus));

        v.push_back(std::make_unique<ObjectGrabber<GridBus>>("gen", bus));

        v.push_back(std::make_unique<ObjectGrabber<GridBus>>("load", bus));

        v.push_back(std::make_unique<ObjectGrabber<GridBus>>("freq", bus));
        return;
    }

    auto ld = dynamic_cast<GridLoad*>(obj);
    if (ld != nullptr) {
        v.reserve(v.size() + 2);
        v.push_back(std::make_unique<ObjectGrabber<GridLoad>>("p", ld));

        v.push_back(std::make_unique<ObjectGrabber<GridLoad>>("q", ld));
        return;
    }

    auto gen = dynamic_cast<Generator*>(obj);
    if (gen != nullptr) {
        if ((mode.empty()) || (mode == "all")) {
            v.reserve(v.size() + 2);
            v.push_back(std::make_unique<ObjectOffsetGrabber<Generator>>("p", gen));

            v.push_back(std::make_unique<ObjectOffsetGrabber<Generator>>("q", gen));
        } else if (mode == "all_state") {
            auto scount = gen->stateSize(cLocalSolverMode);
            v.reserve(v.size() + scount);
            for (index_t kk = 0; kk < scount; ++kk) {
                v.push_back(std::make_unique<ObjectOffsetGrabber<Generator>>(kk, gen));
            }
        } else if (mode == "all_model") {
            /*size_t scount = gen->stateSize();
            for (size_t kk = 0; kk < scount; ++kk)
            {
            ggb =std::make_shared<gridDynGenGrabber(kk, gen);
            v.push_back(ggb);
            }*/
        } else if (mode == "all_gov") {
            /*size_t scount = gen->stateSize();
            for (size_t kk = 0; kk < scount; ++kk)
            {
            ggb =std::make_shared<gridDynGenGrabber(kk, gen);
            v.push_back(ggb);
            }*/
        } else if (mode == "all_ext") {
            /*size_t scount = gen->stateSize();
            for (size_t kk = 0; kk < scount; ++kk)
            {
            ggb =std::make_shared<gridDynGenGrabber(kk, gen);
            v.push_back(ggb);
            }*/
        } else if (mode == "all_pss") {
            /*size_t scount = gen->stateSize();
            for (size_t kk = 0; kk < scount; ++kk)
            {
            ggb =std::make_shared<gridDynGenGrabber(kk, gen);
            v.push_back(ggb);
            }*/
        }
        return;
    }

    auto lnk = dynamic_cast<Link*>(obj);
    if (lnk != nullptr) {
        v.reserve(v.size() + 6);

        v.push_back(std::make_unique<ObjectGrabber<Link>>("angle", lnk));

        v.push_back(std::make_unique<ObjectGrabber<Link>>("p1", lnk));

        v.push_back(std::make_unique<ObjectGrabber<Link>>("p2", lnk));

        v.push_back(std::make_unique<ObjectGrabber<Link>>("q1", lnk));

        v.push_back(std::make_unique<ObjectGrabber<Link>>("q2", lnk));

        v.push_back(std::make_unique<ObjectGrabber<Link>>("loss", lnk));
        return;
    }

    auto area = dynamic_cast<GridArea*>(obj);
    if (area != nullptr) {
        if ((mode.empty()) || (mode == "all")) {
            v.reserve(v.size() + 6);

            v.push_back(std::make_unique<ObjectGrabber<GridArea>>("generationreal", area));

            v.push_back(std::make_unique<ObjectGrabber<GridArea>>("generationreactive", area));

            v.push_back(std::make_unique<ObjectGrabber<GridArea>>("loadreal", area));

            v.push_back(std::make_unique<ObjectGrabber<GridArea>>("loadreactive", area));

            v.push_back(std::make_unique<ObjectGrabber<GridArea>>("loss", area));

            v.push_back(std::make_unique<ObjectGrabber<GridArea>>("tieflowreal", area));
            return;
        }
        if (mode.compare(0, 8, "all_gen_") == 0) {
            auto gfield = mode.substr(8);
            auto genCount = static_cast<count_t>(area->get("gencount"));
            Generator* ngen = nullptr;
            v.reserve(v.size() + genCount);
            for (index_t pp = 0; pp < genCount; ++pp) {
                ngen = static_cast<Generator*>(area->getSubObject("gen", pp));
                if (ngen != nullptr) {
                    v.push_back(std::make_unique<ObjectGrabber<Generator>>(gfield, ngen));
                }
            }
            return;
        }
    }
}

}  // namespace griddyn
