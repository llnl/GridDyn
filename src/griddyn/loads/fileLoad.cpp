/*
 * Copyright (c) 2014-2026, Lawrence Livermore National Security
 * See the top-level NOTICE for additional details. All rights reserved.
 * SPDX-License-Identifier: BSD-3-Clause
 */

#include "fileLoad.h"

#include "../gridBus.h"
#include "core/coreObjectTemplates.hpp"
#include "gmlc/utilities/stringOps.h"
#include "gmlc/utilities/vectorOps.hpp"
#include <string>
#include <utility>

namespace griddyn {
namespace loads {
    using gmlc::utilities::convertToLowerCase;
    using gmlc::utilities::numeric_conversion;
    using gmlc::utilities::stringOps::splitline;
    using gmlc::utilities::stringOps::trailingStringInt;
    using gmlc::utilities::stringOps::trim;

    fileLoad::fileLoad(const std::string& objName): rampLoad(objName) {}

    fileLoad::fileLoad(const std::string& objName, std::string fileName):
        rampLoad(objName), fileName_(std::move(fileName))
    {
    }

    coreObject* fileLoad::clone(coreObject* obj) const
    {
        auto* nobj = cloneBase<fileLoad, rampLoad>(this, obj);
        if (nobj == nullptr) {
            return obj;
        }
        nobj->inputUnits = inputUnits;
        nobj->scaleFactor = scaleFactor;
        nobj->fileName_ = fileName_;

        return nobj;
    }

    void fileLoad::pFlowObjectInitializeA(coreTime time0, std::uint32_t flags)
    {
        currIndex = 0;
        count = loadFile();
        bool found = false;
        for (auto cc : columnkey) {
            if (cc >= 0) {
                found = true;
                break;
            }
        }
        if (!found) {
            auto Ncol = static_cast<index_t>(schedLoad.columns());
            for (index_t kk = 0; (kk < Ncol) && (kk < 8); ++kk) {
                columnkey[kk] = kk;
            }
        }
        rampLoad::pFlowObjectInitializeA(time0, flags);
        updateA(time0);
    }

    void fileLoad::updateA(coreTime time)
    {
        while (time >= schedLoad.time(currIndex)) {
            ++currIndex;
            if (currIndex >=
                count) {  // this should never happen since the last time should be very large
                currIndex = count;
                break;
            }
        }
        if (currIndex > 0) {
            --currIndex;  // back it off by 1
        }

        double diffrate = 0;

        prevTime = schedLoad.time(currIndex);
        auto dt = (currIndex < count - 1) ? (schedLoad.time(currIndex + 1) - prevTime) : maxTime;
        auto Ncol = static_cast<index_t>(schedLoad.columns());
        for (index_t pp = 0; pp < Ncol; ++pp) {
            if (columnkey[pp] < 0) {
                continue;
            }
            double val = schedLoad.data(pp, currIndex) * scaleFactor;
            if (currIndex < count - 1) {
                diffrate = (opFlags[use_step_change_flag]) ?
                    0.0 :
                    (schedLoad.data(pp, currIndex + 1) * scaleFactor - val) / dt;
            } else {
                diffrate = 0.0;
            }

            switch (columnkey[pp]) {
                case -1:
                    break;
                case 0:
                    setP(val);
                    dPdt = diffrate;
                    if (qratio != kNullVal) {
                        setQ(qratio * val);
                        dQdt = qratio * diffrate;
                    }
                    break;
                case 1:
                    setQ(val);
                    dQdt = diffrate;
                    break;
                case 2:
                    setIp(val);
                    dIpdt = diffrate;
                    if (qratio != kNullVal) {
                        setIq(qratio * val);
                        dIqdt = qratio * diffrate;
                    }
                    break;
                case 3:
                    setIq(val);
                    dIqdt = diffrate;
                    break;
                case 4:
                    setup(val);
                    dYpdt = diffrate;
                    if (qratio != kNullVal) {
                        setYq(qratio * val);
                        dYqdt = qratio * diffrate;
                    }
                    break;
                case 5:
                    setYq(val);
                    dYqdt = diffrate;
                    break;
                case 6:
                    setr(val);
                    drdt = diffrate;
                    break;
                case 7:
                    setx(val);
                    dxdt = diffrate;
                    break;
                default:
                    break;
            }
        }
        lastTime = prevTime;
        if (!opFlags[use_step_change_flag]) {
            rampLoad::updateLocalCache(noInputs, stateData(time), cLocalSolverMode);
        }
        lastUpdateTime = time;
        nextUpdateTime = (currIndex == count - 1) ? maxTime : schedLoad.time(currIndex + 1);
    }

    void fileLoad::timestep(coreTime time, const IOdata& inputs, const solverMode& sMode)
    {
        if (time >= nextUpdateTime) {
            updateA(time);
        }

        rampLoad::timestep(time, inputs, sMode);
    }

    void fileLoad::setFlag(const std::string& flag, bool val)
    {
        if (flag == "absolute") {
            opFlags.set(use_absolute_time_flag, val);
        } else if (flag == "relative") {
            opFlags.set(use_absolute_time_flag, !val);
        } else if (flag == "step") {
            opFlags.set(use_step_change_flag, val);
        } else if (flag == "interpolate") {
            opFlags.set(use_step_change_flag, !val);
        } else {
            zipLoad::setFlag(flag, val);
        }
    }

    static int columnCode(const std::string& ldc)
    {
        auto lc = convertToLowerCase(ldc);
        int code = -1;
        if (lc == "p") {
            code = 0;
        } else if (lc == "q") {
            code = 1;
        } else if (lc == "ip") {
            code = 2;
        } else if (lc == "iq") {
            code = 3;
        } else if ((lc == "zr") || (lc == "yp") || (lc == "zp") || (lc == "yr")) {
            code = 4;
        } else if ((lc == "zq") || (lc == "yq")) {
            code = 5;
        } else if (lc == "r") {
            code = 6;
        } else if (lc == "x") {
            code = 7;
        }
        return code;
    }

    void fileLoad::set(const std::string& param, const std::string& val)
    {
        if ((param == "fileName") || (param == "file")) {
            fileName_ = val;
            if (opFlags[pFlow_initialized]) {
                count = 0;
                currIndex = 0;
                count = loadFile();
            }
        } else if (param.compare(0, 6, "column") == 0) {
            int col = trailingStringInt(param, -1);
            auto sp = splitline(val);
            trim(sp);
            if (col >= 0) {
                if (columnkey.size() < col + sp.size()) {
                    columnkey.resize(col + sp.size(), -1);
                }
            } else {
                if (columnkey.size() < sp.size()) {
                    columnkey.resize(sp.size(), -1);
                }
            }
            for (auto& str : sp) {
                int code = columnCode(str);
                if (col >= 0) {
                    columnkey[col] = code;
                    ++col;
                } else {
                    int ncol = 0;
                    while (columnkey[ncol] >= 0) {
                        ++ncol;
                        if (ncol == static_cast<int>(columnkey.size())) {
                            columnkey.push_back(-1);
                        }
                    }
                    columnkey[ncol] = code;
                }
            }
        } else if (param == "units") {
            inputUnits = units::unit_cast_from_string(val);
        } else if ((param == "mode") || (param == "timemode")) {
            setFlag(val, true);
        } else {
            zipLoad::set(param, val);  // NOLINT
        }
    }

    void fileLoad::set(const std::string& param, double val, units::unit unitType)
    {
        if ((param == "scalefactor") || (param == "scaling")) {
            scaleFactor = val;
        } else if (param == "qratio") {
            qratio = val;
        } else {
            zipLoad::set(param, val, unitType);  // NOLINT
        }
    }

    count_t fileLoad::loadFile()
    {
        try {
            schedLoad.loadFile(fileName_);
        }
        catch (const std::exception& e) {
            LOG_ERROR(std::string("unable to process file ") + e.what());
        }
        if (!schedLoad.empty()) {
            schedLoad.addData(maxTime, schedLoad.lastData());
            if (inputUnits != units::defunit) {
                double scalar =
                    units::convert(1.0, inputUnits, units::puMW, systemBasePower, localBaseVoltage);
                if (scalar != 1.0) {
                    schedLoad.scaleData(scalar);
                }
            }
        } else {
            schedLoad.addData(maxTime, getP());
        }
        if (columnkey.size() < schedLoad.columns()) {
            columnkey.resize(schedLoad.columns(), -1);
        }
        return schedLoad.size();
    }

}  // namespace loads
}  // namespace griddyn
