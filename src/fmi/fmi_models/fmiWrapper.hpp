/*
 * Copyright (c) 2014-2026, Lawrence Livermore National Security
 * See the top-level NOTICE for additional details. All rights reserved.
 * SPDX-License-Identifier: BSD-3-Clause
 */

#pragma once

#include "core/coreExceptions.h"
#include "core/coreObjectTemplates.hpp"
#include "gmlc/utilities/stringOps.h"
#include "gmlc/utilities/vectorOps.hpp"
#include "griddyn/gridComponent.h"
#include <cassert>
#include <string>
#include <type_traits>
#include <vector>

namespace griddyn::fmi {
/** @brief template class to wrap a component type with an FMIsubmodel
@tparam FMItype the type of fmiSubmodel to incorporate
@tparam BaseObj the component class this object is a part of
*/
template<class FMItype, class BaseObj>
class FmiWrapper: public BaseObj {
  protected:
    FMItype* fmisub = nullptr;  //!< a pointer to an fmi submodel of some kind either
                                //!< FMIMESubModel or FMICoSimSubmodel
    std::vector<std::string> inputNames_specified;  //!< storage for the specified input names
    std::vector<std::string> outputNames_specified;  //!< storage for the specified output names

  public:
    static_assert(std::is_base_of<GridComponent, BaseObj>::value,
                  "BaseObj must inherit from GridComponent");
    /** constructor taking an object name
@param[in] objName the name of the object
*/
    FmiWrapper(const std::string& objName): BaseObj(objName) {}
    virtual CoreObject* clone(CoreObject* obj) const override
    {
        auto nobj = cloneBase<FmiWrapper, BaseObj>(this, obj);
        if (nobj == nullptr) {
            return obj;
        }
        nobj->inputNames_specified = inputNames_specified;
        nobj->outputNames_specified = outputNames_specified;
        if (fmisub != nullptr) {
            nobj->fmisub = static_cast<FMItype*>(fmisub->clone());
        }
        return nobj;
    }

    /** function call to get a set of input string to use for the FMI inputs
@details this defaults to the regular input strings call but there are a few reasons that this
could be distinct from the general object input and output strings so a separate call is
provided with the default to the regular call
@return a reference to a vector of string vectors containing the potential names of the inputs
*/
    virtual const std::vector<stringVec>& getFmiInputNames() const { return BaseObj::inputNames(); }

    /** function call to get a set of output strings to use for the FMI outputs
@details this defaults to the regular output strings call but there are a few reasons that this
could be distinct from the general object input and output strings so a separate call is
provided with the default to the regular call
@return a reference to a vector of string vectors containing the potential names of the inputs
*/
    virtual const std::vector<stringVec>& getFmiOutputNames() const
    {
        return BaseObj::outputNames();
    }
    /** function to help match the IO of the fmi to the IO of the component*/
    void configureFmiIo()
    {
        using gmlc::utilities::ensureSizeAtLeast;
        using gmlc::utilities::stringOps::findCloseStringMatch;
        using gmlc::utilities::stringOps::string_match_type;

        auto ostrings = fmisub->getOutputNames();
        auto istrings = fmisub->getInputNames();

        auto& iNames = getFmiInputNames();
        auto& oNames = getFmiOutputNames();

        stringVec inputNames_actual(BaseObj::m_inputSize);
        stringVec outputNames_actual(BaseObj::m_outputSize);

        // Make sure the specified sizes are big enough
        ensureSizeAtLeast(inputNames_specified, BaseObj::m_inputSize);
        ensureSizeAtLeast(outputNames_specified, BaseObj::m_outputSize);

        // deal with inputs
        for (index_t ii = 0; ii < BaseObj::m_inputSize; ++ii) {
            if (inputNames_specified[ii].empty()) {
                int ind = findCloseStringMatch(iNames[ii], istrings, string_match_type::close);
                if (ind >= 0) {
                    inputNames_actual[ii] = istrings[ind];
                } else {
                    // now try some alternative representations
                    stringVec altInames;
                    altInames.reserve(inputNames_actual[ii].size() * 2 + 4);
                    for (auto& iname : iNames[ii]) {
                        altInames.emplace_back(iname + "_in");
                        altInames.emplace_back(iname + "_i");
                    }
                    altInames.emplace_back("in" + std::to_string(ii));
                    altInames.emplace_back("input" + std::to_string(ii));
                    altInames.emplace_back("in[" + std::to_string(ii) + "]");
                    altInames.emplace_back("input[" + std::to_string(ii) + "]");
                    ind = findCloseStringMatch(altInames, istrings, string_match_type::close);
                    if (ind >= 0) {
                        inputNames_actual[ii] = istrings[ind];
                    } else {
                        BaseObj::log(this,
                                     print_level::warning,
                                     "unable to match inputs for input#" + std::to_string(ii) +
                                         "(" + iNames[ii][0] + ")");
                    }
                }
            } else {
                int ind = findCloseStringMatch({inputNames_specified[ii]},
                                               istrings,
                                               string_match_type::exact);
                if (ind >= 0) {
                    inputNames_actual[ii] = istrings[ind];
                } else {
                    ind = findCloseStringMatch({inputNames_specified[ii],
                                                inputNames_specified[ii] + "_in"},
                                               istrings,
                                               string_match_type::close);
                    if (ind >= 0) {
                        inputNames_actual[ii] = istrings[ind];
                    } else {
                        BaseObj::log(this,
                                     print_level::warning,
                                     "unable to match inputs for input#" + std::to_string(ii) +
                                         "(" + inputNames_specified[ii] + ")");
                    }
                }
            }
        }

        // deal with outputs

        for (index_t ii = 0; ii < BaseObj::m_outputSize; ++ii) {
            if (outputNames_specified[ii].empty()) {
                int ind = findCloseStringMatch(oNames[ii], ostrings, string_match_type::close);
                if (ind >= 0) {
                    outputNames_actual[ii] = ostrings[ind];
                } else {  // now try some alternative representations
                    stringVec altOnames;
                    altOnames.reserve(outputNames_actual[ii].size() * 2 + 4);
                    for (auto& oname : oNames[ii]) {
                        altOnames.emplace_back(oname + "_out");
                        altOnames.emplace_back(oname + "_o");
                    }
                    ind = findCloseStringMatch(altOnames, ostrings, string_match_type::close);
                    if (ind >= 0) {
                        outputNames_actual[ii] = ostrings[ind];
                    } else {
                        BaseObj::log(this,
                                     print_level::warning,
                                     "unable to match inputs for input" + std::to_string(ii));
                    }
                }
            } else {
                int ind = findCloseStringMatch({inputNames_specified[ii]},
                                               istrings,
                                               string_match_type::exact);
                if (ind >= 0) {
                    inputNames_actual[ii] = istrings[ind];
                } else {
                    ind = findCloseStringMatch({outputNames_specified[ii],
                                                outputNames_specified[ii] + "_out"},
                                               ostrings,
                                               string_match_type::close);
                    if (ind >= 0) {
                        outputNames_actual[ii] = ostrings[ind];
                    } else {
                        BaseObj::log(this,
                                     print_level::warning,
                                     "unable to match inputs for output" + std::to_string(ii));
                    }
                }
            }
        }

        std::string inputs;
        for (auto& iname : inputNames_actual) {
            if (inputs.empty()) {
                inputs = iname;
            } else {
                inputs += ", " + iname;
            }
        }
        fmisub->set("inputs", inputs);
        std::string outputs;
        for (auto& oname : outputNames_actual) {
            if (outputs.empty()) {
                outputs = oname;
            } else {
                outputs += ", " + oname;
            }
        }

        fmisub->set("outputs", outputs);
    }

    void pFlowObjectInitializeA(coreTime time0, std::uint32_t flags) override
    {
        if (fmisub->isLoaded()) {
            configureFmiIo();
            SET_CONTROLFLAG(flags, force_constant_pflow_initialization);
            fmisub->pFlowInitializeA(time0, flags);
            BaseObj::pFlowObjectInitializeA(time0, flags);
        } else {
            this->disable();
        }
    }

    void pFlowObjectInitializeB() override
    {
        // printf("entering pflow Init B wrapper\n");
        if (BaseObj::isEnabled()) {
            fmisub->pFlowInitializeB();
            BaseObj::pFlowObjectInitializeB();
        }
        // printf("finished pflow Init B wrapper\n");
    }

    void set(std::string_view param, std::string_view val) override
    {
        using gmlc::utilities::convertToLowerCase;
        using gmlc::utilities::stringOps::findCloseStringMatch;
        using gmlc::utilities::stringOps::string_match_type;
        using gmlc::utilities::stringOps::trailingStringInt;
        auto param2 = convertToLowerCase(param);
        if ((param2 == "fmu") || (param2 == "fmu_dir") || (param2 == "file")) {
            if (fmisub != nullptr) {
                BaseObj::remove(fmisub);
            }
            fmisub = new FMItype(this->getName());
            this->addSubObject(fmisub);
            assert(isSameObject(fmisub->getParent(), this));
            fmisub->set("fmu", val);
        } else if (param2.compare(param2.size() - 3, 2, "in") == 0) {
            for (auto& istr : this->inputNames()) {
                int ind = findCloseStringMatch({param2.substr(0, param2.size() - 2), param2},
                                               istr,
                                               string_match_type::close);
                if (ind >= 0) {
                    inputNames_specified[ind] = val;
                    return;
                }
            }
        } else if (param2.compare(param2.size() - 4, 2, "out") == 0) {
            for (auto& ostr : this->outputNames()) {
                int ind = findCloseStringMatch({param2.substr(0, param2.size() - 3), param2},
                                               ostr,
                                               string_match_type::close);
                if (ind >= 0) {
                    outputNames_specified[ind] = val;
                    return;
                }
            }
        } else if (param2.compare(0, 2, "in") == 0) {
            int num = trailingStringInt(param2);
            if (num < BaseObj::m_inputSize) {
                inputNames_specified[num] = val;
            }
        } else if (param2.compare(0, 2, "out") == 0) {
            int num = trailingStringInt(param2);
            if (num < BaseObj::m_outputSize) {
                outputNames_specified[num] = val;
            }
        } else {
            if (!BaseObj::opFlags[pFlow_initialized]) {
                for (auto& istr : this->inputNames()) {
                    int ind = findCloseStringMatch({param2}, istr, string_match_type::close);
                    if (ind >= 0) {
                        inputNames_specified[ind] = val;
                        return;
                    }
                }
                for (auto& ostr : this->outputNames()) {
                    int ind = findCloseStringMatch({param2}, ostr, string_match_type::close);
                    if (ind >= 0) {
                        outputNames_specified[ind] = val;
                        return;
                    }
                }
            }

            if (fmisub != nullptr) {
                try {
                    fmisub->set(param, val);
                    return;
                }
                catch (const unrecognizedParameter&) {
                }
            }
            BaseObj::set(param, val);
        }
    }
    void set(std::string_view param, double val, units::unit unitType) override
    {
        bool valid = false;
        try {
            BaseObj::set(param, val, unitType);
            valid = true;
        }
        catch (const unrecognizedParameter&) {
        }

        if (fmisub != nullptr) {
            try {
                fmisub->set(param, val, unitType);
                valid = true;
            }
            catch (const unrecognizedParameter&) {
            }
        }

        if (!valid) {
            throw(unrecognizedParameter(param));
        }
    }

    void getParameterStrings(stringVec& pstr, paramStringType pstype) const override
    {
        switch (pstype) {
            case paramStringType::all:
                fmisub->getParameterStrings(pstr, paramStringType::localnum);
                BaseObj::getParameterStrings(pstr, paramStringType::numeric);
                pstr.push_back("#");
                fmisub->getParameterStrings(pstr, paramStringType::localstr);
                BaseObj::getParameterStrings(pstr, paramStringType::str);
                break;
            case paramStringType::localnum:
                fmisub->getParameterStrings(pstr, paramStringType::localnum);
                break;
            case paramStringType::localstr:
                fmisub->getParameterStrings(pstr, paramStringType::localstr);
                break;
            case paramStringType::localflags:
                fmisub->getParameterStrings(pstr, paramStringType::localflags);
                break;
            case paramStringType::numeric:
                fmisub->getParameterStrings(pstr, paramStringType::localnum);
                BaseObj::getParameterStrings(pstr, paramStringType::numeric);
                break;
            case paramStringType::str:
                fmisub->getParameterStrings(pstr, paramStringType::localstr);
                BaseObj::getParameterStrings(pstr, paramStringType::str);
                break;
            case paramStringType::flags:
                fmisub->getParameterStrings(pstr, paramStringType::localflags);
                BaseObj::getParameterStrings(pstr, paramStringType::flags);
                break;
        }
    }

    IOdata getOutputs(const IOdata& inputs,
                      const stateData& sD,
                      const solverMode& sMode) const override
    {
        return fmisub->getOutputs(inputs, sD, sMode);
    }
};
}  // namespace griddyn::fmi

