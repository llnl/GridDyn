/*
 * Copyright (c) 2014-2026, Lawrence Livermore National Security
 * See the top-level NOTICE for additional details. All rights reserved.
 * SPDX-License-Identifier: BSD-3-Clause
 */

#include "fmiCoordinator.h"

#include "fmiCollector.h"
#include "fmiEvent.h"
#include "gmlc/containers/mapOps.hpp"
#include <algorithm>
#include <cstdio>
#include <string>
#include <utility>

namespace griddyn {
namespace fmi {
    fmiCoordinator::fmiCoordinator(const std::string& /* unused */): coreObject("fmiCoordinator") {}

    static auto searchFunc = [](const auto& vp1, const auto& vp2) {
        return (vp1.first < vp2.first);
    };

    void fmiCoordinator::registerParameter(const std::string& paramName, fmiEvent* evnt)
    {
        auto fnd = vrNames.find(paramName);
        if (fnd == vrNames.end()) {
            auto vr = nextVR++;
            paramVR.emplace_back(vr, inputSet{paramName, evnt});
            vrNames.emplace(paramName, vr);
        } else {
            auto vr = fnd->second;
            auto res2 = std::lower_bound(paramVR.begin(),
                                         paramVR.end(),
                                         vrInputPair(vr, inputSet()),
                                         searchFunc);
            if ((res2 != paramVR.end()) && (res2->first == vr)) {
                while ((res2 != paramVR.end()) && (res2->first == vr)) {
                    ++res2;
                }
                paramVR.emplace(res2, vr, inputSet{paramName, evnt});
            } else {
                vr = nextVR++;
                paramVR.emplace_back(vr, inputSet{paramName, evnt});
                vrNames.emplace(paramName, vr);
            }
        }
    }

    void fmiCoordinator::registerInput(const std::string& inputName, fmiEvent* evnt)
    {
        auto fnd = vrNames.find(inputName);
        if (fnd == vrNames.end()) {
            auto vr = nextVR++;
            inputVR.emplace_back(vr, inputSet{inputName, evnt});
            vrNames.emplace(inputName, vr);
        } else {
            auto vr = fnd->second;
            auto res2 = std::lower_bound(inputVR.begin(),
                                         inputVR.end(),
                                         vrInputPair(vr, inputSet()),
                                         searchFunc);
            while ((res2 != paramVR.end()) && (res2->first == vr)) {
                ++res2;
            }
            if ((res2 != paramVR.end()) && (res2->first == vr)) {
                inputVR.emplace(++res2, vr, inputSet{inputName, evnt});
            } else {
                vr = nextVR++;
                inputVR.emplace_back(vr, inputSet{inputName, evnt});
                vrNames.emplace(inputName, vr);
            }
        }
    }

    void
        fmiCoordinator::registerOutput(const std::string& outputName, int column, fmiCollector* out)
    {
        auto vr = nextVR++;
        outputVR.emplace_back(
            vr, outputSet{outputName, column, static_cast<index_t>(outputVR.size()), out});
        vrNames.emplace(outputName, vr);
        collectors.push_back(out);
        auto lst = std::unique(collectors.begin(), collectors.end());
        collectors.erase(lst, collectors.end());
        outputPoints.push_back(0.0);
    }

    bool fmiCoordinator::sendInput(index_t vr, double val)
    {
        auto res = std::lower_bound(inputVR.begin(),
                                    inputVR.end(),
                                    vrInputPair(vr, inputSet()),
                                    searchFunc);
        if ((res != inputVR.end()) && (res->first == vr)) {
            while ((res != inputVR.end()) && (res->first == vr)) {
                res->second.evnt->setValue(val);
                res->second.evnt->trigger();
                ++res;
            }
            return true;
        }
        res = std::lower_bound(paramVR.begin(),
                               paramVR.end(),
                               vrInputPair(vr, inputSet()),
                               searchFunc);
        if ((res != paramVR.end()) && (res->first == vr)) {
            while ((res != paramVR.end()) && (res->first == vr)) {
                res->second.evnt->setValue(val);
                res->second.evnt->trigger();
                ++res;
            }
            return true;
        }
        LOG_WARNING("invalid value reference " + std::to_string(vr));
        return false;
    }

    index_t fmiCoordinator::findVR(const std::string& varName) const
    {
        return mapFind(vrNames, varName, kNullLocation);
    }

    bool fmiCoordinator::sendInput(index_t vr, const char* s)
    {
        auto res = std::lower_bound(paramVR.begin(),
                                    paramVR.end(),
                                    vrInputPair(vr, inputSet()),
                                    searchFunc);
        if ((res != paramVR.end()) && (res->first == vr) &&
            (res->second.evnt->eventType == fmi::fmiEvent::fmiEventType::string_parameter)) {
            while ((res != paramVR.end()) && (res->first == vr) &&
                   (res->second.evnt->eventType == fmi::fmiEvent::fmiEventType::string_parameter)) {
                printf("updating string value %s to %s\n", res->second.name.c_str(), s);
                res->second.evnt->updateStringValue(s);
                res->second.evnt->trigger();
                ++res;
            }
            return true;
        }
        LOG_WARNING("invalid value reference " + std::to_string(vr));
        return false;
    }

    double fmiCoordinator::getOutput(index_t vr)
    {
        auto res = std::lower_bound(outputVR.begin(),
                                    outputVR.end(),
                                    vrOutputPair(vr, outputSet()),
                                    searchFunc);
        if ((res != outputVR.end()) && (res->first == vr)) {
            return outputPoints[res->second.outIndex];
        }
        auto res2 = std::lower_bound(paramVR.begin(),
                                     paramVR.end(),
                                     vrInputPair(vr, inputSet()),
                                     searchFunc);
        if ((res2 != paramVR.end()) && (res2->first == vr)) {
            return res2->second.evnt->query();
        }
        auto res3 = std::lower_bound(inputVR.begin(),
                                     inputVR.end(),
                                     vrInputPair(vr, inputSet()),
                                     searchFunc);
        if ((res3 != inputVR.end()) && (res3->first == vr)) {
            return res3->second.evnt->query();
        }
        return kNullVal;
    }

    void fmiCoordinator::updateOutputs(coreTime time)
    {
        for (auto col : collectors) {
            col->trigger(time);
        }
        for (auto& output : outputVR) {
            outputPoints[output.second.outIndex] =
                output.second.col->getValue(output.second.column);
        }
    }

    const std::string& fmiCoordinator::getFMIName() const
    {
        return getParent()->getName();
    }

    void fmiCoordinator::addHelper(std::shared_ptr<helperObject> ho)
    {
        std::lock_guard<std::mutex> hLock(helperProtector);
        helpers.push_back(std::move(ho));
    }

    bool fmiCoordinator::isStringParameter(const vrInputPair& param)
    {
        return (param.second.evnt->eventType == fmi::fmiEvent::fmiEventType::string_parameter);
    }

}  // namespace fmi
}  // namespace griddyn
