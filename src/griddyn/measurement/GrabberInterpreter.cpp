/*
 * Copyright (c) 2014-2026, Lawrence Livermore National Security
 * See the top-level NOTICE for additional details. All rights reserved.
 * SPDX-License-Identifier: BSD-3-Clause
 */

/** @file
 * @brief Parse object measurements and expand built-in object/state selectors.
 *
 * Mathematical calls use MeasurementExpression; object-only all_state/all_model
 * selectors use a late-bound reader so submodels created during initialization
 * can supply both values and matching column descriptions.
 */

#include "GrabberInterpreter.hpp"

#include "../Generator.h"
#include "../GridArea.h"
#include "../GridBus.h"
#include "../Link.h"
#include "../Load.h"
#include "../simulation/GridSimulation.h"
#include "GridGrabbers.h"
#include "MeasurementExpression.hpp"
#include "ObjectGrabbers.h"
#include "gmlc/utilities/vectorOps.hpp"
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace griddyn {

static void autoGrabbers(CoreObject* obj, std::vector<std::unique_ptr<GridGrabber>>& grabbers);
static void allGrabbers(std::string_view mode,
                        CoreObject* obj,
                        std::vector<std::unique_ptr<GridGrabber>>& grabbers);

static void perObjectGrabbers(std::string_view cmd,
                              CoreObject* obj,
                              std::vector<std::unique_ptr<GridGrabber>>& grabbers);

namespace {
    /** @brief Collect current local states or their labels from a component subtree.
     * @param object Root component; other object types produce empty outputs.
     * @param[out] values Values in traversal order, or null to skip value collection.
     * @param[out] descriptions Component:state labels, or null to skip labels.
     *
     * Clears requested outputs and visits root states before children in stored
     * order. Repeating discovery at read time includes internal controller
     * blocks created during initialization; values and labels use one traversal.
     */
    void collectStates(CoreObject* object, std::vector<double>* values, stringVec* descriptions)
    {
        if (values != nullptr) {
            values->clear();
        }
        if (descriptions != nullptr) {
            descriptions->clear();
        }
        auto* root = dynamic_cast<GridComponent*>(object);
        if (root == nullptr) {
            return;
        }
        std::vector<GridComponent*> pending{root};
        while (!pending.empty()) {
            auto* component = pending.back();
            pending.pop_back();
            const auto names = component->localStateNames();
            for (size_t index = 0; index < names.size(); ++index) {
                if (values != nullptr) {
                    values->push_back(component->getState(static_cast<index_t>(index)));
                }
                if (descriptions != nullptr) {
                    descriptions->push_back(component->getName() + ':' + names[index]);
                }
            }
            const auto& children = component->getSubObjects();
            pending.insert(pending.end(), children.rbegin(), children.rend());
        }
    }

    /** @brief Read all current local states below an object as a recorder vector.
     *
     * Used by all_state and model/controller selectors. Discovers membership on
     * each read/description request so configuration can precede initialization.
     * Reads stored object states, not an externally supplied solver StateData,
     * and supplies no solver Jacobian. Targets must outlive the reader.
     *
     * This class serves object-side state enumeration; the state-field vector
     * factory in StateVectorGrabber.cpp instead binds bus/link field membership
     * and supports solver-state access. If discovery changes the output width,
     * the owning collector/recorder layout must be rechecked/reset.
     */
    class StateVectorGrabber final: public GridGrabber {
      public:
        /** @brief Bind value and label discovery to the selected component subtree. */
        explicit StateVectorGrabber(CoreObject* object)
        {
            field = "state";
            vectorGrab = true;
            mVectorGrabberFunction = [](CoreObject* target, std::vector<double>& values) {
                collectStates(target, &values, nullptr);
            };
            mVectorDescriptionFunction = [](CoreObject* target, stringVec& descriptions) {
                collectStates(target, nullptr, &descriptions);
            };
            GridGrabber::updateObject(object);
        }

        /** @brief Copy reader configuration while retaining its target association. */
        std::unique_ptr<GridGrabber> clone() const override
        {
            auto copy = std::make_unique<StateVectorGrabber>(mObject);
            cloneTo(copy.get());
            return copy;
        }

        using GridGrabber::getDesc;
        /** @brief Rediscover state labels, or use indexed labels for a custom description. */
        void getDesc(stringVec& descriptions) const override
        {
            if (customDesc) {
                GridGrabber::getDesc(descriptions);
            } else {
                collectStates(mObject, nullptr, &descriptions);
            }
        }
    };

    /** @brief Append a late-bound state vector when the target is a component. */
    void appendStateGrabbers(CoreObject* object,
                             std::vector<std::unique_ptr<GridGrabber>>& grabbers)
    {
        if (dynamic_cast<GridComponent*>(object) != nullptr) {
            grabbers.push_back(std::make_unique<StateVectorGrabber>(object));
        }
    }
}  // namespace

static GrabberInterpreter<GridGrabber,
                          MeasurementExpression<GridGrabber>,
                          MeasurementExpression<GridGrabber>>
    gInterpret([](std::string_view fld, CoreObject* obj) { return createGrabber(fld, obj); });

// Field lists can recursively expand per-object selections.
// NOLINTNEXTLINE(misc-no-recursion)
std::vector<std::unique_ptr<GridGrabber>> makeGrabbers(std::string_view command, CoreObject* obj)
{
    std::vector<std::unique_ptr<GridGrabber>> grabbers;
    auto gstr = splitGrabberList(command);
    gmlc::utilities::stringOps::trim(gstr);
    for (auto& cmd : gstr) {
        const auto firstGrabber = grabbers.size();
        auto renameloc = cmd.find(" as ");  // spaces are important
        // extract out a rename
        std::string rname;
        if (renameloc != std::string::npos) {
            rname = gmlc::utilities::stringOps::trim(cmd.substr(renameloc + 4));
            cmd.erase(renameloc, std::string::npos);
        }
        const auto lowerCommand = gmlc::utilities::convertToLowerCase(cmd);
        if (lowerCommand.starts_with("all")) {
            allGrabbers(lowerCommand, obj, grabbers);
        } else if (lowerCommand == "auto") {
            autoGrabbers(obj, grabbers);
        } else if (lowerCommand.starts_with("per_")) {
            perObjectGrabbers(cmd, obj, grabbers);
        } else {
            auto grabber = gInterpret.interpretGrabberBlock(cmd, obj);
            if (!grabber) {
                continue;
            }
            if (grabber->loaded || grabber->field.starts_with("state")) {
                if (!rname.empty()) {
                    grabber->setDescription(rname);
                }
                grabbers.push_back(std::move(grabber));
            } else if (grabber->field.starts_with("all")) {
                allGrabbers(grabber->field, grabber->getObject(), grabbers);
            } else if (grabber->field == "auto") {
                autoGrabbers(grabber->getObject(), grabbers);
            } else if (obj != nullptr) {
                obj->log(obj,
                         PrintLevel::WARNING,
                         "Unable to load recorder " + std::string{command});
            }
        }
        if (!rname.empty() && grabbers.size() == firstGrabber + 1) {
            grabbers.back()->setDescription(rname);
        }
    }
    return grabbers;
}

void autoGrabbers(CoreObject* obj, std::vector<std::unique_ptr<GridGrabber>>& grabbers)
{
    auto* bus = dynamic_cast<GridBus*>(obj);
    if (bus != nullptr) {
        grabbers.reserve(grabbers.size() + 5);
        grabbers.push_back(std::make_unique<ObjectGrabber<GridBus>>("voltage", bus));

        grabbers.push_back(std::make_unique<ObjectGrabber<GridBus>>("angle", bus));

        grabbers.push_back(std::make_unique<ObjectGrabber<GridBus>>("gen", bus));

        grabbers.push_back(std::make_unique<ObjectGrabber<GridBus>>("load", bus));

        grabbers.push_back(std::make_unique<ObjectGrabber<GridBus>>("freq", bus));
        return;
    }

    auto* load = dynamic_cast<GridLoad*>(obj);
    if (load != nullptr) {
        grabbers.reserve(grabbers.size() + 2);
        grabbers.push_back(std::make_unique<ObjectGrabber<GridLoad>>("p", load));

        grabbers.push_back(std::make_unique<ObjectGrabber<GridLoad>>("q", load));
        return;
    }

    auto* gen = dynamic_cast<Generator*>(obj);
    if (gen != nullptr) {
        grabbers.reserve(grabbers.size() + 2);
        grabbers.push_back(std::make_unique<ObjectGrabber<Generator>>("p", gen));

        grabbers.push_back(std::make_unique<ObjectGrabber<Generator>>("q", gen));

        return;
    }

    auto* lnk = dynamic_cast<Link*>(obj);
    if (lnk != nullptr) {
        grabbers.reserve(grabbers.size() + 3);
        grabbers.push_back(std::make_unique<ObjectGrabber<Link>>("p1", lnk));

        grabbers.push_back(std::make_unique<ObjectGrabber<Link>>("q1", lnk));

        grabbers.push_back(std::make_unique<ObjectGrabber<Link>>("loss", lnk));
        return;
    }

    // get the vector grabs if this is a simulation
    auto* gds = dynamic_cast<GridSimulation*>(obj);
    if (gds != nullptr) {
        grabbers.reserve(grabbers.size() + 4);

        grabbers.push_back(std::make_unique<ObjectGrabber<GridArea>>("voltage", gds));

        grabbers.push_back(std::make_unique<ObjectGrabber<GridArea>>("angle", gds));

        grabbers.push_back(std::make_unique<ObjectGrabber<GridArea>>("busgenerationreal", gds));

        grabbers.push_back(std::make_unique<ObjectGrabber<GridArea>>("busloadreal", gds));
        return;
    }

    auto* area = dynamic_cast<GridArea*>(obj);
    if (area != nullptr) {
        grabbers.reserve(grabbers.size() + 6);

        grabbers.push_back(std::make_unique<ObjectGrabber<GridArea>>("generationreal", area));

        grabbers.push_back(std::make_unique<ObjectGrabber<GridArea>>("generationreactive", area));

        grabbers.push_back(std::make_unique<ObjectGrabber<GridArea>>("loadreal", area));

        grabbers.push_back(std::make_unique<ObjectGrabber<GridArea>>("loadreactive", area));

        grabbers.push_back(std::make_unique<ObjectGrabber<GridArea>>("loss", area));

        grabbers.push_back(std::make_unique<ObjectGrabber<GridArea>>("tieflowreal", area));
        return;
    }
}
// NOLINTNEXTLINE(misc-no-recursion)
void perObjectGrabbers(std::string_view cmd,
                       CoreObject* obj,
                       std::vector<std::unique_ptr<GridGrabber>>& grabbers)
{
    if (!cmd.starts_with("per_")) {
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
    auto* subObject = obj->getSubObject(objType, index);
    while (subObject != nullptr) {
        auto gbrs = makeGrabbers(fieldlist, subObject);
        for (auto& grabber : gbrs) {
            grabbers.push_back(std::move(grabber));
        }
        ++index;
        subObject = obj->getSubObject(objType, index);
    }
}

void allGrabbers(std::string_view mode,
                 CoreObject* obj,
                 std::vector<std::unique_ptr<GridGrabber>>& grabbers)
{
    auto* bus = dynamic_cast<GridBus*>(obj);
    if (bus != nullptr) {
        grabbers.reserve(grabbers.size() + 5);

        grabbers.push_back(std::make_unique<ObjectGrabber<GridBus>>("voltage", bus));

        grabbers.push_back(std::make_unique<ObjectGrabber<GridBus>>("angle", bus));

        grabbers.push_back(std::make_unique<ObjectGrabber<GridBus>>("gen", bus));

        grabbers.push_back(std::make_unique<ObjectGrabber<GridBus>>("load", bus));

        grabbers.push_back(std::make_unique<ObjectGrabber<GridBus>>("freq", bus));
        return;
    }

    auto* load = dynamic_cast<GridLoad*>(obj);
    if (load != nullptr) {
        grabbers.reserve(grabbers.size() + 2);
        grabbers.push_back(std::make_unique<ObjectGrabber<GridLoad>>("p", load));

        grabbers.push_back(std::make_unique<ObjectGrabber<GridLoad>>("q", load));
        return;
    }

    auto* gen = dynamic_cast<Generator*>(obj);
    if (gen != nullptr) {
        if ((mode.empty()) || (mode == "all")) {
            grabbers.reserve(grabbers.size() + 2);
            grabbers.push_back(std::make_unique<ObjectOffsetGrabber<Generator>>("p", gen));

            grabbers.push_back(std::make_unique<ObjectOffsetGrabber<Generator>>("q", gen));
        } else if (mode == "all_state") {
            appendStateGrabbers(gen, grabbers);
        } else if (mode == "all_model") {
            appendStateGrabbers(gen->find("genmodel"), grabbers);
        } else if (mode == "all_gov") {
            appendStateGrabbers(gen->find("governor"), grabbers);
        } else if (mode == "all_ext") {
            appendStateGrabbers(gen->find("exciter"), grabbers);
        } else if (mode == "all_pss") {
            appendStateGrabbers(gen->find("pss"), grabbers);
        }
        return;
    }

    auto* lnk = dynamic_cast<Link*>(obj);
    if (lnk != nullptr) {
        grabbers.reserve(grabbers.size() + 6);

        grabbers.push_back(std::make_unique<ObjectGrabber<Link>>("angle", lnk));

        grabbers.push_back(std::make_unique<ObjectGrabber<Link>>("p1", lnk));

        grabbers.push_back(std::make_unique<ObjectGrabber<Link>>("p2", lnk));

        grabbers.push_back(std::make_unique<ObjectGrabber<Link>>("q1", lnk));

        grabbers.push_back(std::make_unique<ObjectGrabber<Link>>("q2", lnk));

        grabbers.push_back(std::make_unique<ObjectGrabber<Link>>("loss", lnk));
        return;
    }

    auto* area = dynamic_cast<GridArea*>(obj);
    if (area != nullptr) {
        if ((mode.empty()) || (mode == "all")) {
            grabbers.reserve(grabbers.size() + 6);

            grabbers.push_back(std::make_unique<ObjectGrabber<GridArea>>("generationreal", area));

            grabbers.push_back(
                std::make_unique<ObjectGrabber<GridArea>>("generationreactive", area));

            grabbers.push_back(std::make_unique<ObjectGrabber<GridArea>>("loadreal", area));

            grabbers.push_back(std::make_unique<ObjectGrabber<GridArea>>("loadreactive", area));

            grabbers.push_back(std::make_unique<ObjectGrabber<GridArea>>("loss", area));

            grabbers.push_back(std::make_unique<ObjectGrabber<GridArea>>("tieflowreal", area));
            return;
        }
        if (mode.starts_with("all_gen_")) {
            auto gfield = mode.substr(8);
            auto genCount = static_cast<count_t>(area->get("gencount"));
            Generator* ngen = nullptr;
            grabbers.reserve(grabbers.size() + genCount);
            for (index_t pp = 0; pp < genCount; ++pp) {
                ngen = static_cast<Generator*>(area->getSubObject("gen", pp));
                if (ngen != nullptr) {
                    grabbers.push_back(std::make_unique<ObjectGrabber<Generator>>(gfield, ngen));
                }
            }
            return;
        }
    }
}

}  // namespace griddyn
