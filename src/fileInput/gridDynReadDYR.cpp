/*
 * Copyright (c) 2014-2026, Lawrence Livermore National Security
 * See the top-level NOTICE for additional details. All rights reserved.
 * SPDX-License-Identifier: BSD-3-Clause
 */

#include "LoadTemplateManager.h"
#include "ReaderInfo.h"
#include "core/CoreExceptions.h"
#include "core/CoreObject.h"
#include "core/ObjectFactory.hpp"
#include "core/coreDefinitions.hpp"
#include "fileInput.h"
#include "gmlc/utilities/stringConversion.h"
#include "gmlc/utilities/stringOps.h"
#include "gmlc/utilities/string_viewConversion.h"
#include "gridDynReadDyrModels.h"
#include "griddyn/Exciter.h"
#include "griddyn/GenModel.h"
#include "griddyn/Generator.h"
#include "griddyn/Governor.h"
#include "griddyn/GridArea.h"
#include "griddyn/GridBus.h"
#include "griddyn/GridDynSimulation.h"
#include "griddyn/Load.h"
#include "griddyn/Stabilizer.h"
#include "griddyn/VoltageCompensator.h"
#include "griddyn/generators/DynamicGenerator.h"
#include "griddyn/generators/RenewableGenerator.h"
#include "griddyn/governors/GovernorHygov.h"
#include "griddyn/governors/GovernorIeeeG1.h"
#include "griddyn/governors/GovernorIeeeG2.h"
#include "griddyn/governors/GovernorReheat.h"
#include "griddyn/links/AcLine.h"
#include "griddyn/links/AdjustableTransformer.h"
#include "griddyn/loads/CompositeLoad.h"
#include "griddyn/loads/ElectronicLoad.h"
#include "griddyn/loads/IEELLoad.h"
#include "griddyn/loads/LoadTemplateAdapters.h"
#include "griddyn/loads/MotorDLoad.h"
#include "griddyn/loads/WECCMotor3.h"
#include "griddyn/loads/ZipLoad.h"
#include "griddyn/primary/AcBus.h"
#include "griddyn/relays/BusMeasurementSensor.h"
#include "griddyn/relays/TimeOverCurrentRelay.h"
#include "griddyn/renewables/REECA1.h"
#include "griddyn/renewables/REECA1E.h"
#include "griddyn/renewables/REECA1G.h"
#include "griddyn/renewables/REECB1.h"
#include "griddyn/renewables/REGCA1.h"
#include "griddyn/renewables/REGCP1.h"
#include "griddyn/renewables/REPCA1.h"
#include "griddyn/renewables/WT3E1.h"
#include "griddyn/renewables/WT3G1.h"
#include "griddyn/renewables/WT4E1.h"
#include "griddyn/renewables/WT4G1.h"
#include "griddyn/renewables/WTARA1.h"
#include "griddyn/renewables/WTDS.h"
#include "griddyn/renewables/WTDTA1.h"
#include "griddyn/renewables/WTPTA1.h"
#include "griddyn/renewables/WTTQA1.h"
#include "griddyn/stabilizers/StabilizerIEEEST.h"
#include "griddyn/stabilizers/StabilizerIee2st.h"
#include "griddyn/stabilizers/StabilizerPss2a.h"
#include "griddyn/stabilizers/StabilizerST2CUT.h"
#include "griddyn/stabilizers/StabilizerStab3.h"
#include "griddyn/voltagecompensators/VoltageCompensatorIeeeVC.h"
#include "loadModelReaderHelper.h"
#include <algorithm>
#include <array>
#include <charconv>
#include <cmath>
#include <complex>
#include <cstddef>
#include <format>
#include <fstream>
#include <limits>
#include <map>
#include <memory>
#include <span>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>

namespace griddyn {
namespace {
    bool isDyrCommentLine(const std::string& line)
    {
        return line.starts_with("/*") || line.starts_with("//");
    }

    std::string normalizeDyrModelType(std::string type)
    {
        gmlc::utilities::stringOps::trimString(type);
        if ((type.size() >= 2) && (type.front() == '\'') && (type.back() == '\'')) {
            auto modelName = type.substr(1, type.size() - 2);
            gmlc::utilities::stringOps::trimString(modelName);
            type = '\'' + modelName + '\'';
        }
        return type;
    }

    void loadGENCLS(CoreObject* parentObject, stringVec& tokens);
    void loadGENROU(CoreObject* parentObject, stringVec& tokens);
    void loadCSVGN1(CoreObject* parentObject, stringVec& tokens);
    void loadGENTPJ(CoreObject* parentObject, stringVec& tokens);
    void loadGENROE(CoreObject* parentObject, stringVec& tokens);
    void loadGENSAE(CoreObject* parentObject, stringVec& tokens);
    void loadGENSAL(CoreObject* parentObject, stringVec& tokens);
    void loadESDC1A(CoreObject* parentObject, stringVec& tokens);
    void loadESDC2A(CoreObject* parentObject, stringVec& tokens);
    void loadIEEET1(CoreObject* parentObject, stringVec& tokens);
    void loadIEEET2(CoreObject* parentObject, stringVec& tokens);
    void loadIEEET3(CoreObject* parentObject, stringVec& tokens);
    void loadIEEEX1(CoreObject* parentObject, stringVec& tokens);
    void loadAC7B(CoreObject* parentObject, stringVec& tokens);
    void loadAC8B(CoreObject* parentObject, stringVec& tokens);
    void loadESST1A(CoreObject* parentObject, stringVec& tokens);
    void loadESST2A(CoreObject* parentObject, stringVec& tokens);
    void loadESST3A(CoreObject* parentObject, stringVec& tokens);
    void loadESST4B(CoreObject* parentObject, stringVec& tokens);
    void loadEXPIC1(CoreObject* parentObject, stringVec& tokens);
    void loadSCRX(CoreObject* parentObject, stringVec& tokens);
    void loadESAC6A(CoreObject* parentObject, stringVec& tokens);
    void loadEXST1(CoreObject* parentObject, stringVec& tokens);
    void loadEXAC1(CoreObject* parentObject, stringVec& tokens);
    void loadESAC1A(CoreObject* parentObject, stringVec& tokens);
    void loadESAC5A(CoreObject* parentObject, stringVec& tokens);
    void loadEXAC2(CoreObject* parentObject, stringVec& tokens);
    void loadEXAC4(CoreObject* parentObject, stringVec& tokens);
    void loadTGOV1(CoreObject* parentObject, stringVec& tokens);
    void loadHYGOV(CoreObject* parentObject, stringVec& tokens);
    void loadGovernorVariant(CoreObject* parentObject, stringVec& tokens, std::string_view model);
    void loadGGOV1(CoreObject* parentObject, stringVec& tokens);
    void loadGAST(CoreObject* parentObject, stringVec& tokens);
    void loadGPWSCC(CoreObject* parentObject, stringVec& tokens);
    void loadIEEEG1(CoreObject* parentObject, stringVec& tokens);
    void loadIEEEG2(CoreObject* parentObject, stringVec& tokens);
    void loadIEEEVC(CoreObject* parentObject, stringVec& tokens);
    void loadIEESGO(CoreObject* parentObject, stringVec& tokens);
    void loadIEEEST(CoreObject* parentObject, stringVec& tokens, bool zeroGain = false);
    void loadIEE2ST(CoreObject* parentObject, stringVec& tokens, bool zeroGain = false);
    void loadPSS2A(CoreObject* parentObject, stringVec& tokens, bool zeroGain = false);
    void loadSTAB3(CoreObject* parentObject, stringVec& tokens, bool zeroGain = false);
    void loadST2CUT(CoreObject* parentObject, stringVec& tokens, bool zeroGain = false);
    void loadEXDC2(CoreObject* parentObject, stringVec& tokens);
    void loadSEXS(CoreObject* parentObject, stringVec& tokens);
    void loadRenewable(CoreObject* parentObject, stringVec& tokens, std::string_view modelName);
    void loadMeasurement(CoreObject* parentObject, stringVec& tokens, std::string_view modelName);
    void loadTIOCR1(CoreObject* parentObject, stringVec& tokens);
    void loadIEELAL(CoreObject* parentObject, const stringVec& tokens);
    void loadCMLDBLU1(CoreObject* parentObject, const stringVec& tokens);
    Generator* requireDyrGenerator(CoreObject* parentObject,
                                   const stringVec& tokens,
                                   std::string_view modelName);

    struct UnsupportedDyrModelSummary {
        std::size_t mCount = 0;
        std::size_t mFirstLine = 0;
        std::string mFirstBus;
        std::string mFirstMachine;
    };

    void addUnsupportedModel(std::map<std::string, UnsupportedDyrModelSummary>& unsupportedModels,
                             std::string_view modelName,
                             const stringVec& lineTokens,
                             std::size_t recordLineNumber)
    {
        auto& summary = unsupportedModels[std::string{modelName}];
        ++summary.mCount;
        if (summary.mFirstLine == 0U) {
            summary.mFirstLine = recordLineNumber;
            summary.mFirstBus = lineTokens.empty() ? "<missing>" : lineTokens[0];
            summary.mFirstMachine = (lineTokens.size() > 2U) ? lineTokens[2] : "<missing>";
        }
    }

    void loadIEE2ST(CoreObject* parentObject, stringVec& tokens, bool zeroGain)
    {
        if (tokens.size() != 23U) {
            throw InvalidParameterValue("IEE2ST DYR record must contain 23 fields");
        }
        auto* generator =
            dynamic_cast<DynamicGenerator*>(requireDyrGenerator(parentObject, tokens, "IEE2ST"));
        if (generator == nullptr) {
            throw InvalidParameterValue("IEE2ST requires a dynamic generator");
        }
        const auto params = gmlc::utilities::str2vector(tokens, kNullVal);
        auto* stabilizer = new stabilizers::StabilizerIee2st();
        stabilizer->set("mode1", params[3]);
        stabilizer->set("busr1", params[4]);
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
        if (zeroGain) {
            stabilizer->set("k1", 0.0);
            stabilizer->set("k2", 0.0);
        }
        generator->add(stabilizer);
    }

    void loadPSS2A(CoreObject* parentObject, stringVec& tokens, bool zeroGain)
    {
        if (tokens.size() != 24U) {
            throw InvalidParameterValue("PSS2A DYR record must contain 24 fields");
        }
        auto* generator =
            dynamic_cast<DynamicGenerator*>(requireDyrGenerator(parentObject, tokens, "PSS2A"));
        if (generator == nullptr) {
            throw InvalidParameterValue("PSS2A requires a dynamic generator");
        }
        const auto params = gmlc::utilities::str2vector(tokens, kNullVal);
        auto* stabilizer = new stabilizers::StabilizerPss2a();
        stabilizer->set("mode1", params[3]);
        stabilizer->set("busr1", params[4]);
        stabilizer->set("mode2", params[5]);
        stabilizer->set("busr2", params[6]);
        stabilizer->set("tw1", params[7]);
        stabilizer->set("tw2", params[8]);
        stabilizer->set("t6", params[9]);
        stabilizer->set("tw3", params[10]);
        stabilizer->set("tw4", params[11]);
        stabilizer->set("t7", params[12]);
        stabilizer->set("ks2", params[13]);
        stabilizer->set("ks3", params[14]);
        stabilizer->set("t8", params[15]);
        stabilizer->set("t9", params[16]);
        stabilizer->set("ks1", params[17]);
        stabilizer->set("t1", params[18]);
        stabilizer->set("t2", params[19]);
        stabilizer->set("t3", params[20]);
        stabilizer->set("t4", params[21]);
        stabilizer->set("vstmax", params[22]);
        stabilizer->set("vstmin", params[23]);
        if (zeroGain) {
            stabilizer->set("ks1", 0.0);
        }
        generator->add(stabilizer);
    }

    void loadSTAB3(CoreObject* parentObject, stringVec& tokens, bool zeroGain)
    {
        if (tokens.size() != 8U) {
            throw InvalidParameterValue("STAB3 DYR record must contain 8 fields");
        }
        auto* generator =
            dynamic_cast<DynamicGenerator*>(requireDyrGenerator(parentObject, tokens, "STAB3"));
        if (generator == nullptr) {
            throw InvalidParameterValue("STAB3 requires a dynamic generator");
        }
        const auto params = gmlc::utilities::str2vector(tokens, kNullVal);
        auto* stabilizer = new stabilizers::StabilizerStab3();
        stabilizer->set("tt", params[3]);
        stabilizer->set("tx1", params[4]);
        stabilizer->set("tx2", params[5]);
        stabilizer->set("kx", params[6]);
        stabilizer->set("vlim", params[7]);
        if (zeroGain) {
            stabilizer->set("kx", 0.0);
        }
        generator->add(stabilizer);
    }
}  // namespace

namespace detail {
    bool loadDyrModelRecord(CoreObject* parentObject,
                            stringVec& lineTokens,
                            bool disableStabilizers,
                            count_t& zeroGainStabilizers)
    {
        if (lineTokens.size() < 2U) {
            return false;
        }
        const auto type = normalizeDyrModelType(lineTokens[1]);
        if (type == "'GENCLS'") {
            loadGENCLS(parentObject, lineTokens);
        } else if (type == "'GENROU'") {
            loadGENROU(parentObject, lineTokens);
        } else if (type == "'CSVGN1'") {
            loadCSVGN1(parentObject, lineTokens);
        } else if (type == "'IEELAL'") {
            loadIEELAL(parentObject, lineTokens);
        } else if (type == "'CMLDBLU1'") {
            loadCMLDBLU1(parentObject, lineTokens);
        } else if (type == "'GENTPJ'") {
            loadGENTPJ(parentObject, lineTokens);
        } else if (type == "'GENROE'") {
            loadGENROE(parentObject, lineTokens);
        } else if (type == "'GENSAE'") {
            loadGENSAE(parentObject, lineTokens);
        } else if (type == "'GENSAL'") {
            loadGENSAL(parentObject, lineTokens);
        } else if (type == "'ESDC1A'") {
            loadESDC1A(parentObject, lineTokens);
        } else if (type == "'ESDC2A'") {
            loadESDC2A(parentObject, lineTokens);
        } else if (type == "'IEEET1'") {
            loadIEEET1(parentObject, lineTokens);
        } else if (type == "'IEEET2'") {
            loadIEEET2(parentObject, lineTokens);
        } else if (type == "'IEEET3'") {
            loadIEEET3(parentObject, lineTokens);
        } else if (type == "'IEEEX1'") {
            loadIEEEX1(parentObject, lineTokens);
        } else if (type == "'AC7B'") {
            loadAC7B(parentObject, lineTokens);
        } else if (type == "'AC8B'") {
            loadAC8B(parentObject, lineTokens);
        } else if (type == "'ESST1A'") {
            loadESST1A(parentObject, lineTokens);
        } else if (type == "'ESST2A'") {
            loadESST2A(parentObject, lineTokens);
        } else if (type == "'ESST3A'") {
            loadESST3A(parentObject, lineTokens);
        } else if (type == "'ESST4B'") {
            loadESST4B(parentObject, lineTokens);
        } else if (type == "'EXPIC1'") {
            loadEXPIC1(parentObject, lineTokens);
        } else if (type == "'SCRX'") {
            loadSCRX(parentObject, lineTokens);
        } else if (type == "'ESAC6A'") {
            loadESAC6A(parentObject, lineTokens);
        } else if (type == "'EXST1'") {
            loadEXST1(parentObject, lineTokens);
        } else if (type == "'EXAC1'") {
            loadEXAC1(parentObject, lineTokens);
        } else if (type == "'ESAC1A'") {
            loadESAC1A(parentObject, lineTokens);
        } else if (type == "'ESAC5A'") {
            loadESAC5A(parentObject, lineTokens);
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
        } else if (type == "'TG2'") {
            loadGovernorVariant(parentObject, lineTokens, "TG2");
        } else if (type == "'TGOV1DB'") {
            loadGovernorVariant(parentObject, lineTokens, "TGOV1DB");
        } else if (type == "'TGOV1N'") {
            loadGovernorVariant(parentObject, lineTokens, "TGOV1N");
        } else if (type == "'TGOV1NDB'") {
            loadGovernorVariant(parentObject, lineTokens, "TGOV1NDB");
        } else if (type == "'HYGOVDB'") {
            loadGovernorVariant(parentObject, lineTokens, "HYGOVDB");
        } else if (type == "'HYGOV4'") {
            loadGovernorVariant(parentObject, lineTokens, "HYGOV4");
        } else if (type == "'GGOV1'") {
            loadGGOV1(parentObject, lineTokens);
        } else if (type == "'GAST'") {
            loadGAST(parentObject, lineTokens);
        } else if (type == "'GPWSCC'") {
            loadGPWSCC(parentObject, lineTokens);
        } else if (type == "'IEEEG1'") {
            loadIEEEG1(parentObject, lineTokens);
        } else if (type == "'IEEEG2'") {
            loadIEEEG2(parentObject, lineTokens);
        } else if (type == "'IEEEVC'") {
            loadIEEEVC(parentObject, lineTokens);
        } else if (type == "'IEESGO'") {
            loadIEESGO(parentObject, lineTokens);
        } else if (type == "'IEEEST'") {
            if (disableStabilizers) {
                ++zeroGainStabilizers;
            }
            loadIEEEST(parentObject, lineTokens, disableStabilizers);
        } else if (type == "'IEE2ST'") {
            if (disableStabilizers) {
                ++zeroGainStabilizers;
            }
            loadIEE2ST(parentObject, lineTokens, disableStabilizers);
        } else if (type == "'PSS2A'") {
            if (disableStabilizers) {
                ++zeroGainStabilizers;
            }
            loadPSS2A(parentObject, lineTokens, disableStabilizers);
        } else if (type == "'STAB3'") {
            if (disableStabilizers) {
                ++zeroGainStabilizers;
            }
            loadSTAB3(parentObject, lineTokens, disableStabilizers);
        } else if (type == "'ST2CUT'") {
            if (disableStabilizers) {
                ++zeroGainStabilizers;
            }
            loadST2CUT(parentObject, lineTokens, disableStabilizers);
        } else if (type == "'SEXS'") {
            loadSEXS(parentObject, lineTokens);
        } else if (type == "'REGCA1'") {
            loadRenewable(parentObject, lineTokens, "REGCA1");
        } else if (type == "'REGCP1'") {
            loadRenewable(parentObject, lineTokens, "REGCP1");
        } else if (type == "'REGCV1'" || type == "'REGCV2'" || type == "'REGF1'" ||
                   type == "'REGF2'" || type == "'REGF3'") {
            loadRenewable(parentObject, lineTokens, gmlc::utilities::stringOps::removeQuotes(type));
        } else if (type == "'EPCGEN'") {
            loadRenewable(parentObject, lineTokens, "EPCGEN");
        } else if (type == "'REECA1'") {
            loadRenewable(parentObject, lineTokens, "REECA1");
        } else if (type == "'REECA1E'") {
            loadRenewable(parentObject, lineTokens, "REECA1E");
        } else if (type == "'REECA1G'") {
            loadRenewable(parentObject, lineTokens, "REECA1G");
        } else if (type == "'REECB1'") {
            loadRenewable(parentObject, lineTokens, "REECB1");
        } else if (type == "'REECCU1'" || type == "'REECC1'") {
            loadRenewable(parentObject, lineTokens, "REECC1");
        } else if (type == "'REPCA1'") {
            loadRenewable(parentObject, lineTokens, "REPCA1");
        } else if (type == "'WTDTA1'") {
            loadRenewable(parentObject, lineTokens, "WTDTA1");
        } else if (type == "'WTDS'") {
            loadRenewable(parentObject, lineTokens, "WTDS");
        } else if (type == "'BUSROCOF'") {
            loadMeasurement(parentObject, lineTokens, "BUSROCOF");
        } else if (type == "'PLL1'") {
            loadMeasurement(parentObject, lineTokens, "PLL1");
        } else if (type == "'PLL2'") {
            loadMeasurement(parentObject, lineTokens, "PLL2");
        } else if (type == "'FREQDIV'") {
            loadMeasurement(parentObject, lineTokens, "FREQDIV");
        } else if (type == "'TIOCR1'") {
            loadTIOCR1(parentObject, lineTokens);
        } else if (type == "'WTARA1'") {
            loadRenewable(parentObject, lineTokens, "WTARA1");
        } else if (type == "'WTPTA1'") {
            loadRenewable(parentObject, lineTokens, "WTPTA1");
        } else if (type == "'WTTQA1'") {
            loadRenewable(parentObject, lineTokens, "WTTQA1");
        } else if (type == "'WT3P1'") {
            // The first-generation PSS/E WT3P1 pitch controller is the
            // WTPTA1 core without its Kcc cross term. Preserve its fixed Pset
            // while translating the parameter order to the existing model.
            if (lineTokens.size() != 12U) {
                throw InvalidParameterValue("WT3P1 DYR record must contain 9 parameters");
            }
            const auto params = gmlc::utilities::str2vector(lineTokens, kNullVal);
            if (std::any_of(params.begin() + 3, params.end(), [](double value) {
                    return !std::isfinite(value) || value == kNullVal;
                })) {
                throw InvalidParameterValue("WT3P1 DYR record has a nonnumeric parameter");
            }
            stringVec pitchTokens{lineTokens[0],
                                  "'WTPTA1'",
                                  lineTokens[2],
                                  lineTokens[4],
                                  lineTokens[3],
                                  lineTokens[6],
                                  lineTokens[5],
                                  "0",
                                  lineTokens[10],
                                  lineTokens[7],
                                  lineTokens[8],
                                  lineTokens[9],
                                  std::format("{:.17g}", -params[9]),
                                  lineTokens[11]};
            loadRenewable(parentObject, pitchTokens, "WTPTA1");
        } else if (type == "'WT3T1'") {
            // WT3T1 combines the legacy aerodynamic and shaft dynamics. Split
            // it into the existing WTARA1 and WTDS/WTDTA1 role components.
            if (lineTokens.size() != 11U) {
                throw InvalidParameterValue("WT3T1 DYR record must contain 8 parameters");
            }
            const auto params = gmlc::utilities::str2vector(lineTokens, kNullVal);
            if (std::any_of(params.begin() + 3, params.end(), [](double value) {
                    return !std::isfinite(value) || value == kNullVal;
                })) {
                throw InvalidParameterValue("WT3T1 DYR record has a nonnumeric parameter");
            }
            const double windSpeed = params[3];
            const double turbineInertiaFraction = params[8];
            if (windSpeed <= 0.0 || params[4] <= 0.0 || turbineInertiaFraction < 0.0 ||
                turbineInertiaFraction >= 1.0) {
                throw InvalidParameterValue("WT3T1 has invalid wind speed or inertia data");
            }
            const double theta0 = (windSpeed > 1.0) ?
                (params[7] / 0.75) * (1.0 - (1.0 / (windSpeed * windSpeed))) :
                0.0;
            stringVec aeroTokens{lineTokens[0],
                                 "'WTARA1'",
                                 lineTokens[2],
                                 lineTokens[6],
                                 std::format("{:.17g}", theta0)};
            loadRenewable(parentObject, aeroTokens, "WTARA1");

            stringVec shaftTokens{
                lineTokens[0], "'WTDS'", lineTokens[2], lineTokens[4], lineTokens[5], "1"};
            if (turbineInertiaFraction != 0.0) {
                shaftTokens[1] = "'WTDTA1'";
                shaftTokens.emplace_back(lineTokens[8]);
                shaftTokens.emplace_back(lineTokens[9]);
                shaftTokens.emplace_back(lineTokens[10]);
            }
            loadRenewable(parentObject,
                          shaftTokens,
                          (turbineInertiaFraction == 0.0) ? "WTDS" : "WTDTA1");
        } else if (type == "'WT3G1'") {
            loadRenewable(parentObject, lineTokens, "WT3G1");
        } else if (type == "'WT3E1'") {
            loadRenewable(parentObject, lineTokens, "WT3E1");
        } else if (type == "'WT4G1'") {
            loadRenewable(parentObject, lineTokens, "WT4G1");
        } else if (type == "'WT4E1'") {
            loadRenewable(parentObject, lineTokens, "WT4E1");
        } else {
            return false;
        }
        return true;
    }
}  // namespace detail

void loadDyr(CoreObject* parentObject,
             const std::string& fileName,
             const BasicReaderInfo& /*readerOptions*/)
{
    const auto* simulation = dynamic_cast<const GridDynSimulation*>(parentObject->getRoot());
    const bool disableStabilizers =
        (simulation != nullptr) && simulation->isFlagSet(DISABLE_STABILIZERS_FOR_DIAGNOSTICS);
    count_t zeroGainStabilizers = 0;
    std::ifstream file(fileName.c_str(), std::ios::in);
    std::string line;  // line storage
    std::string continuedLine;
    std::size_t lineNumber = 0;
    std::map<std::string, UnsupportedDyrModelSummary> unsupportedModels;

    if (!(file.is_open())) {
        parentObject->log(parentObject, PrintLevel::ERROR, "Unable to open file " + fileName);
        //    return;
    } else {
        warnIfStaticNetworkMissing(parentObject, "DYR", fileName);
    }
    while (std::getline(file, line)) {
        ++lineNumber;
        gmlc::utilities::stringOps::trimString(line);
        if (line.empty()) {
            continue;
        }
        if (isDyrCommentLine(line)) {
            continue;
        }
        const auto recordLineNumber = lineNumber;
        while (line.back() != '/') {
            if (std::getline(file, continuedLine)) {
                ++lineNumber;
                gmlc::utilities::stringOps::trimString(continuedLine);
                if (continuedLine.empty() || isDyrCommentLine(continuedLine)) {
                    continue;
                }
                line += ' ' + continuedLine;
            } else {
                break;
            }
        }
        auto lineTokens = gmlc::utilities::stringOps::splitlineQuotes(
            line,
            " \timeValue\n,",
            gmlc::utilities::stringOps::default_quote_chars,
            gmlc::utilities::stringOps::delimiter_compression::on);
        // get rid of the '/' at the end of the last string
        auto lstr = lineTokens.back();
        lineTokens.pop_back();
        lstr = lstr.substr(0, lstr.size() - 1);
        if (!lstr.empty()) {
            lineTokens.push_back(lstr);
        }
        if (lineTokens.size() < 2) {
            continue;
        }
        const auto modelName = gmlc::utilities::stringOps::removeQuotes(lineTokens[1]);
        try {
            if (!detail::loadDyrModelRecord(
                    parentObject, lineTokens, disableStabilizers, zeroGainStabilizers)) {
                addUnsupportedModel(unsupportedModels, modelName, lineTokens, recordLineNumber);
            }
        }
        catch (const InvalidParameterValue& error) {
            std::string message{fileName};
            message.push_back(':');
            message.append(std::to_string(recordLineNumber));
            message.append(" ").append(modelName).append(" bus ").append(lineTokens[0]);
            message.append(" machine ");
            message.append(lineTokens.size() > 2U ? lineTokens[2] : "<missing>");
            message.append(": ").append(error.what());
            throw InvalidParameterValue(message);
        }
    }
    if (!unsupportedModels.empty()) {
        std::string message = fileName + ": unsupported DYR models:";
        for (const auto& [modelName, summary] : unsupportedModels) {
            message += "\n  " + modelName + ": " + std::to_string(summary.mCount) +
                " record(s); first at line " + std::to_string(summary.mFirstLine) + ", bus " +
                summary.mFirstBus + " machine " + summary.mFirstMachine;
        }
        throw InvalidParameterValue(message);
    }
    if (disableStabilizers) {
        parentObject->log(parentObject,
                          PrintLevel::SUMMARY,
                          "DYR diagnostic: set zero output gain on " +
                              std::to_string(zeroGainStabilizers) + " stabilizer model records");
    }
}

namespace {
    Generator* findDyrGenerator(CoreObject* parentObject,
                                std::string_view busToken,
                                std::string_view generatorToken)
    {
        int busId = 0;
        const auto busResult =
            std::from_chars(busToken.data(), busToken.data() + busToken.size(), busId);
        if ((busResult.ec != std::errc{}) || (busResult.ptr != busToken.data() + busToken.size())) {
            return nullptr;
        }
        auto* bus = dynamic_cast<GridBus*>(parentObject->findByUserID("bus", busId));
        if (bus == nullptr) {
            return nullptr;
        }

        auto generatorId = gmlc::utilities::stringOps::removeQuotes(std::string{generatorToken});
        gmlc::utilities::stringOps::trimString(generatorId);
        if (generatorId.empty()) {
            return nullptr;
        }

        auto* generator =
            dynamic_cast<Generator*>(bus->find(bus->getName() + "_Gen_" + generatorId));
        if (generator != nullptr) {
            return generator;
        }

        // EPC and other steady-state readers may preserve quoting or whitespace
        // in the generated object name. Match the normalized machine-ID suffix
        // as a compatibility fallback before applying the legacy position rule.
        for (index_t generatorIndex = 0;; ++generatorIndex) {
            auto* candidate = bus->getGen(generatorIndex);
            if (candidate == nullptr) {
                break;
            }
            const auto candidateName = candidate->getName();
            const auto normalizedCandidateName = gmlc::utilities::convertToLowerCase(candidateName);
            const auto suffixPosition = normalizedCandidateName.rfind("_gen_");
            if (suffixPosition == std::string::npos) {
                continue;
            }
            auto candidateId = gmlc::utilities::stringOps::removeQuotes(
                candidateName.substr(suffixPosition + std::string_view{"_Gen_"}.size()));
            gmlc::utilities::stringOps::trimString(candidateId);
            if (candidateId == generatorId) {
                return candidate;
            }
        }

        // Older GridDyn DYR inputs treated a numeric machine ID as a one-based
        // generator position. Preserve that behavior only as a fallback when
        // no generator with the actual PSS/E machine ID exists.
        int generatorNumber = 0;
        const auto idResult = std::from_chars(generatorId.data(),
                                              generatorId.data() + generatorId.size(),
                                              generatorNumber);
        if ((idResult.ec != std::errc{}) ||
            (idResult.ptr != generatorId.data() + generatorId.size()) || (generatorNumber <= 0)) {
            return nullptr;
        }
        return bus->getGen(static_cast<index_t>(generatorNumber - 1));
    }

    Generator* requireDyrGenerator(CoreObject* parentObject,
                                   const stringVec& tokens,
                                   std::string_view modelName)
    {
        auto* generator = findDyrGenerator(parentObject, tokens[0], tokens[2]);
        if (generator == nullptr) {
            int busId = 0;
            const auto busResult =
                std::from_chars(tokens[0].data(), tokens[0].data() + tokens[0].size(), busId);
            if ((busResult.ec == std::errc{}) &&
                (busResult.ptr == tokens[0].data() + tokens[0].size())) {
                auto* bus = dynamic_cast<GridBus*>(parentObject->findByUserID("bus", busId));
                if (bus == nullptr) {
                    throw InvalidParameterValue(std::string{modelName} + " requires existing bus " +
                                                tokens[0] + " for machine " + tokens[2]);
                }
                throw InvalidParameterValue(
                    std::string{modelName} + " requires generator machine " + tokens[2] +
                    " at bus " + tokens[0] + "; the bus contains " +
                    std::to_string(bus->getInt("gencount")) + " generator(s)");
            }
            throw InvalidParameterValue(std::string{modelName} +
                                        " requires an existing generator matching bus " +
                                        tokens[0] + " and machine " + tokens[2]);
        }
        return generator;
    }

    void loadIEELAL(CoreObject* parentObject, const stringVec& tokens)
    {
        if (tokens.size() != 17U) {
            throw InvalidParameterValue("IEELAL DYR record must contain 14 parameters");
        }
        if (tokens[0] != "0" || gmlc::utilities::stringOps::removeQuotes(tokens[2]) != "*") {
            throw InvalidParameterValue("IEELAL requires bus 0 and load ID *");
        }
        auto* simulation = dynamic_cast<GridDynSimulation*>(parentObject->getRoot());
        if (simulation == nullptr) {
            throw InvalidParameterValue("IEELAL requires a GridDynSimulation root");
        }

        IEELParameters parameters;
        for (std::size_t index = 0; index < 14U; ++index) {
            const auto& token = tokens[index + 3U];
            const std::string errorMessage =
                "IEELAL parameter " + std::to_string(index + 1U) + " must be a finite number";
            double value = std::numeric_limits<double>::quiet_NaN();
            try {
                value = gmlc::utilities::numeric_conversionComplete<double>(std::string_view{token},
                                                                            value);
            }
            catch (const std::out_of_range&) {
                throw InvalidParameterValue(errorMessage);
            }
            if (!std::isfinite(value)) {
                throw InvalidParameterValue(errorMessage);
            }
            if (index < parameters.coefficients.size()) {
                parameters.coefficients[index] = value;
            } else {
                parameters.exponents[index - parameters.coefficients.size()] = value;
            }
        }
        LoadTemplateManager templates;
        templates.setTemplate(LoadTemplateScope::System,
                              0,
                              loads::makeIEELALLoadTemplate(parameters));
        applyLoadTemplatesFromReader(*simulation, templates);
    }

    void loadCMLDBLU1(CoreObject* parentObject, const stringVec& tokens)
    {
        // PSS/E 32 CMLDBLU1 has 132 CONs following bus, model name, and load ID.
        constexpr std::size_t cmpldwConCount = 132U;
        if (tokens.size() != cmpldwConCount + 3U) {
            throw InvalidParameterValue("CMLDBLU1 DYR record must contain 132 parameters");
        }

        std::array<double, cmpldwConCount> con{};
        for (std::size_t index = 0; index < con.size(); ++index) {
            const auto& token = tokens[index + 3U];
            const std::string errorMessage =
                "CMLDBLU1 parameter " + std::to_string(index + 1U) + " must be a finite number";
            try {
                con[index] = gmlc::utilities::numeric_conversionComplete<double>(
                    std::string_view{token}, std::numeric_limits<double>::quiet_NaN());
            }
            catch (const std::out_of_range&) {
                throw InvalidParameterValue(errorMessage);
            }
            if (!std::isfinite(con[index])) {
                throw InvalidParameterValue(errorMessage);
            }
        }

        const auto near = [](double first, double second) {
            return std::abs(first - second) <= 1.0e-9;
        };
        if ((con[2U] < 0.0) || (con[3U] < 0.0) || (con[5U] < 0.0)) {
            throw InvalidParameterValue("CMLDBLU1 Rfdr, Xfdr, and Xxf must be nonnegative");
        }
        if ((con[6U] <= 0.0) || (con[7U] <= 0.0)) {
            throw InvalidParameterValue("CMLDBLU1 TfixHS and TfixLS must be positive");
        }
        if ((con[5U] <= 1.0e-9) && (!near(con[6U], 1.0) || !near(con[7U], 1.0))) {
            throw InvalidParameterValue(
                "CMLDBLU1 fixed transformer taps require a nonzero Xxf transformer");
        }
        if (!near(con[8U], -1.0) && !near(con[8U], 0.0) && !near(con[8U], 1.0)) {
            throw InvalidParameterValue("CMLDBLU1 LTC flag must be -1, 0, or 1 for PSS/E data");
        }
        if (near(con[8U], 1.0)) {
            throw InvalidParameterValue(
                "CMLDBLU1 dynamic LTC operation is not implemented; use LTC=-1 for initialization-only control");
        }
        if (near(con[8U], -1.0) && con[5U] <= 1.0e-9) {
            throw InvalidParameterValue("CMLDBLU1 LTC control requires a nonzero Xxf transformer");
        }
        if ((con[4U] < 0.0) || (con[4U] > 1.0)) {
            throw InvalidParameterValue("CMLDBLU1 Fb must be in [0, 1]");
        }
        if (near(con[8U], -1.0) && (!near(con[16U], 0.0) || !near(con[17U], 0.0))) {
            throw InvalidParameterValue(
                "CMLDBLU1 LTC line-drop compensation Rcmp/Xcmp is not implemented");
        }
        if (near(con[8U], -1.0) &&
            ((con[9U] <= 0.0) || (con[10U] < con[9U]) || (con[11U] <= 0.0) || (con[12U] <= 0.0) ||
             (con[13U] <= con[12U]))) {
            throw InvalidParameterValue(
                "CMLDBLU1 initialization-only LTC requires valid tap limits, step, and voltage band");
        }

        std::array<double, 4> motorFractions{con[18U], con[19U], con[20U], con[21U]};
        double electronicFraction = con[22U];
        double motorFractionTotal = 0.0;
        for (const double fraction : motorFractions) {
            if ((fraction < 0.0) || (fraction > 1.0)) {
                throw InvalidParameterValue("CMLDBLU1 motor fractions must be in [0, 1]");
            }
            motorFractionTotal += fraction;
        }
        if (electronicFraction < 0.0 || electronicFraction > 1.0) {
            throw InvalidParameterValue("CMLDBLU1 Fel must be in [0, 1]");
        }
        if ((electronicFraction > 1.0e-9) &&
            ((con[23U] < -1.0) || (con[23U] > 1.0) || (con[25U] < 0.0) || (con[24U] <= con[25U]))) {
            throw InvalidParameterValue(
                "CMLDBLU1 electronic load requires PFel in [-1, 1] and Vd1 > Vd2 >= 0");
        }
        const double allocatedFraction = motorFractionTotal + electronicFraction;
        if (allocatedFraction > 1.0) {
            // WECC assigns no static remainder and renormalizes the dynamic
            // fractions when their sum exceeds the original load.
            for (auto& fraction : motorFractions) {
                fraction /= allocatedFraction;
            }
            electronicFraction /= allocatedFraction;
            motorFractionTotal /= allocatedFraction;
        }

        const double staticFraction = std::max(0.0, 1.0 - motorFractionTotal - electronicFraction);
        if (staticFraction > 1.0e-9) {
            if ((std::abs(con[26U]) > 1.0) || (std::abs(con[26U]) < 1.0e-6)) {
                throw InvalidParameterValue(
                    "CMLDBLU1 PFs must be a nonzero value in [-1, 1] for a static remainder");
            }
        }

        constexpr std::array<std::size_t, 3> motorTypeIndices{37U, 57U, 77U};
        for (const auto motorTypeIndex : motorTypeIndices) {
            const double motorType = con[motorTypeIndex];
            if (!near(motorType, 1.0) && !near(motorType, 3.0)) {
                throw InvalidParameterValue("CMLDBLU1 MtypA/B/C must be 1 or 3");
            }
        }

        // Motor D's PSS/E extensions are accepted only at the WECC values implemented
        // by MotorDLoad. Values that alter its characteristic or filtered inputs must
        // be implemented before the reader can safely map them.
        if (motorFractions[3] > 1.0e-9) {
            constexpr std::array<std::pair<std::size_t, double>, 14> motorDConstants{{
                {100U, 0.0},  // Tf: MotorDLoad currently uses the instantaneous frequency input.
                {106U, 1.0},  // LFadj
                {107U, 0.0},  // Kp1
                {108U, 1.0},  // Np1
                {109U, 6.0},  // Kq1
                {110U, 2.0},  // Nq1
                {111U, 12.0},  // Kp2
                {112U, 3.2},  // Np2
                {113U, 11.0},  // Kq2
                {114U, 2.5},  // Nq2
                {115U, 0.86},  // Vbrk
                {118U, 1.0},  // CmpKpf
                {119U, -3.3},  // CmpKqf
                {0U, 0.0},  // Reserved sentinel; Mbase is validated below as finite.
            }};
            for (std::size_t index = 0; index + 1U < motorDConstants.size(); ++index) {
                if (!near(con[motorDConstants[index].first], motorDConstants[index].second)) {
                    throw InvalidParameterValue("CMLDBLU1 Motor D parameter " +
                                                std::to_string(motorDConstants[index].first + 1U) +
                                                " is outside the implemented WECC characteristic");
                }
            }
        }

        int busId = 0;
        const auto busResult =
            std::from_chars(tokens[0].data(), tokens[0].data() + tokens[0].size(), busId);
        if ((busResult.ec != std::errc{}) ||
            (busResult.ptr != tokens[0].data() + tokens[0].size())) {
            throw InvalidParameterValue("CMLDBLU1 requires a numeric bus number");
        }
        auto* bus = dynamic_cast<GridBus*>(parentObject->findByUserID("bus", busId));
        if (bus == nullptr) {
            throw InvalidParameterValue("CMLDBLU1 requires existing bus " + tokens[0]);
        }
        auto* previousLoad = bus->getLoad(0);
        if ((previousLoad == nullptr) || (bus->getLoad(1) != nullptr)) {
            throw InvalidParameterValue(
                "CMLDBLU1 requires exactly one existing load at the specified bus; load IDs are not yet addressable");
        }
        if (previousLoad->isFixedShunt()) {
            throw InvalidParameterValue("CMLDBLU1 cannot replace a fixed shunt");
        }

        auto* area = dynamic_cast<GridArea*>(bus->getParent());
        if (area == nullptr) {
            throw InvalidParameterValue("CMLDBLU1 requires its bus to belong to a GridArea");
        }

        const double initialP = previousLoad->getRealPower();
        const double initialQ = previousLoad->getReactivePower();
        const double systemBaseMVA = area->get("basepower", units::MW);
        const double loadMW = initialP * systemBaseMVA;
        double distributionBaseMVA = loadMW / 0.8;
        if (con[0U] > 0.0) {
            distributionBaseMVA = con[0U];
        } else if (con[0U] < 0.0) {
            distributionBaseMVA = loadMW / std::abs(con[0U]);
        }
        if (!std::isfinite(distributionBaseMVA) || (distributionBaseMVA <= 0.0) ||
            !std::isfinite(systemBaseMVA) || (systemBaseMVA <= 0.0)) {
            throw InvalidParameterValue(
                "CMLDBLU1 cannot determine a positive distribution MVA base");
        }
        const double distributionToSystem = systemBaseMVA / distributionBaseMVA;
        const bool hasTransformer = con[5U] > 1.0e-9;
        // WECC omits the feeder equivalent when Xfdr is zero, even if Rfdr is
        // populated, and represents its reactive compensation with one shunt.
        const bool hasFeeder = con[3U] > 1.0e-9;
        const double transformerReactance = con[5U] * distributionToSystem * con[6U] * con[6U];
        const double feederResistance = hasFeeder ? con[2U] * distributionToSystem : 0.0;
        const double feederReactance = con[3U] * distributionToSystem;
        const double substationSusceptance = con[1U] * distributionToSystem;
        const double sourceVoltage = bus->getVoltage();
        if (!std::isfinite(sourceVoltage) || (sourceVoltage <= 0.0)) {
            throw InvalidParameterValue("CMLDBLU1 requires a positive initial system-bus voltage");
        }

        // Estimate the far-end operating point for component normalization and the
        // feeder compensation. The WECC initialization procedure iterates these values
        // with the network power flow; this first implementation uses the initial
        // transmission-bus P/Q and a one-pass series-drop estimate.
        const auto sourcePhasor = std::polar(sourceVoltage, bus->getAngle());
        const auto sourcePower = std::complex<double>{initialP, initialQ};
        const auto sourceCurrent = std::conj(sourcePower / sourcePhasor);
        const double fixedTapRatio = con[6U] / con[7U];
        auto lowSidePhasor = sourcePhasor / fixedTapRatio;
        if (hasTransformer) {
            lowSidePhasor -= std::complex<double>{0.0, transformerReactance} * sourceCurrent;
        }
        auto loadPhasor = lowSidePhasor;
        if (hasFeeder) {
            loadPhasor -= std::complex<double>{feederResistance, feederReactance} * sourceCurrent;
        }
        const double initialLoadVoltage = std::abs(loadPhasor);
        if (!std::isfinite(initialLoadVoltage) || (initialLoadVoltage <= 0.0)) {
            throw InvalidParameterValue(
                "CMLDBLU1 internal network has no positive load-bus voltage");
        }
        const double currentSquared = std::norm(sourceCurrent);
        const double initialLoadP = initialP - (currentSquared * feederResistance);
        const double seriesReactiveConsumption =
            currentSquared * (transformerReactance + feederReactance);
        const double estimatedFeederCompensation =
            seriesReactiveConsumption - (substationSusceptance * std::norm(lowSidePhasor));
        if (!std::isfinite(initialLoadP) || !std::isfinite(estimatedFeederCompensation)) {
            throw InvalidParameterValue(
                "CMLDBLU1 internal network initialization estimate is invalid");
        }
        std::unique_ptr<loads::IEELLoad> staticComponent;
        double staticReactiveOverride = 0.0;
        bool useStaticReactiveOverride = false;
        if (staticFraction > 1.0e-9) {
            // CMLDBLU1 stores the static curve as two coefficient/exponent pairs
            // plus a constant-power remainder. IEEL has three terms, so the third
            // coefficient is the residual needed to make the curve sum to one at
            // 1 pu. Normalize the active-power base at the estimated load-end voltage to
            // retain the static MW share at the initial operating point. The CMPLDW
            // PFs field defines Q0 independently; scale the IEEL reactive curve to
            // express that Q0 using CompositeLoad's shared component P/Q fractions.
            constexpr double minimumCurveMagnitude = 1.0e-12;
            const double initialVoltage = initialLoadVoltage;
            if (!std::isfinite(initialVoltage) || (initialVoltage <= 0.0)) {
                throw InvalidParameterValue(
                    "CMLDBLU1 static polynomial requires a positive initial bus voltage");
            }

            const double p1c = con[27U];
            const double p1e = con[28U];
            const double p2c = con[29U];
            const double p2e = con[30U];
            const double q1c = con[32U];
            const double q1e = con[33U];
            const double q2c = con[34U];
            const double q2e = con[35U];
            const auto curveValue = [initialVoltage](double firstCoefficient,
                                                     double firstExponent,
                                                     double secondCoefficient,
                                                     double secondExponent) {
                return (firstCoefficient * std::pow(initialVoltage, firstExponent)) +
                    (secondCoefficient * std::pow(initialVoltage, secondExponent)) +
                    (1.0 - firstCoefficient - secondCoefficient);
            };
            const double pCurveAtInitial = curveValue(p1c, p1e, p2c, p2e);
            const double qCurveAtInitial = curveValue(q1c, q1e, q2c, q2e);
            const double staticPBase = initialLoadP * staticFraction;
            const double staticQBase = initialQ * staticFraction;
            double staticP0 = 0.0;
            double pCoefficientScale = 1.0;
            if (std::abs(staticPBase) > minimumCurveMagnitude) {
                if (!std::isfinite(pCurveAtInitial) ||
                    (std::abs(pCurveAtInitial) <= minimumCurveMagnitude)) {
                    throw InvalidParameterValue(
                        "CMLDBLU1 active static curve is zero or non-finite at the initial voltage");
                }
                staticP0 = staticPBase / pCurveAtInitial;
                pCoefficientScale = staticP0 / staticPBase;
            }

            const double staticQ0 = staticP0 * std::tan(std::acos(con[26U]));
            double qCoefficientScale = 1.0;
            if (std::abs(staticQBase) > minimumCurveMagnitude) {
                qCoefficientScale = staticQ0 / staticQBase;
            } else if (std::abs(staticQ0) > minimumCurveMagnitude) {
                // CompositeLoad normally allocates Q using the same fraction
                // as P. PFs can require Q even when the original load has none.
                staticReactiveOverride = staticQ0;
                useStaticReactiveOverride = true;
            }
            if (!std::isfinite(qCurveAtInitial) || !std::isfinite(staticP0) ||
                !std::isfinite(staticQ0) || !std::isfinite(pCoefficientScale) ||
                !std::isfinite(qCoefficientScale)) {
                throw InvalidParameterValue(
                    "CMLDBLU1 static polynomial cannot be normalized at the initial operating point");
            }

            IEELParameters parameters;
            parameters.coefficients = {p1c * pCoefficientScale,
                                       p2c * pCoefficientScale,
                                       (1.0 - p1c - p2c) * pCoefficientScale,
                                       q1c * qCoefficientScale,
                                       q2c * qCoefficientScale,
                                       (1.0 - q1c - q2c) * qCoefficientScale,
                                       con[31U],
                                       con[36U]};
            parameters.exponents = {p1e, p2e, 0.0, q1e, q2e, 0.0};
            if (!std::all_of(parameters.coefficients.begin(),
                             parameters.coefficients.end(),
                             [](double value) { return std::isfinite(value); })) {
                throw InvalidParameterValue(
                    "CMLDBLU1 static polynomial coefficients overflowed during normalization");
            }
            staticComponent = std::make_unique<loads::IEELLoad>("static_remainder");
            staticComponent->setIEELParameters(parameters);
        }
        auto composite = std::make_unique<loads::CompositeLoad>(
            "cmpldw_" + std::to_string(busId) + "_" +
            gmlc::utilities::stringOps::removeQuotes(tokens[2]));

        std::size_t componentIndex = 0U;
        const auto addComponent = [&composite, &componentIndex](GridLoad* component,
                                                                double fraction) {
            composite->add(component);
            ++componentIndex;
            composite->set("fraction" + std::to_string(componentIndex), fraction);
        };

        for (std::size_t motorIndex = 0; motorIndex < 3U; ++motorIndex) {
            const double fraction = motorFractions[motorIndex];
            if (fraction <= 1.0e-9) {
                continue;
            }
            const auto start = motorTypeIndices[motorIndex];
            if (near(con[start], 1.0)) {
                auto motor =
                    std::make_unique<loads::MotorDLoad>("motor_" + std::to_string(motorIndex + 1U));
                motor->set("lfm", con[start + 1U]);
                motor->set("comppf", con[start + 2U]);
                motor->set("vstall", con[start + 3U]);
                motor->set("rstall", con[start + 4U]);
                motor->set("xstall", con[start + 5U]);
                motor->set("tstall", con[start + 6U]);
                motor->set("frst", con[start + 7U]);
                motor->set("vrst", con[start + 8U]);
                motor->set("trst", con[start + 9U]);
                motor->set("fuvr", con[start + 10U]);
                motor->set("vtr1", con[start + 11U]);
                motor->set("ttr1", con[start + 12U]);
                motor->set("vtr2", con[start + 13U]);
                motor->set("ttr2", con[start + 14U]);
                motor->set("vc1off", con[start + 15U]);
                motor->set("vc2off", con[start + 16U]);
                motor->set("vc1on", con[start + 17U]);
                motor->set("vc2on", con[start + 18U]);
                motor->set("tth", con[start + 19U]);
                // CMLDBLU1's fixed A/B/C blocks omit the thermal thresholds and
                // voltage lag. Use the defaults from NERC reference Table A.10.
                motor->set("th1t", 0.7);
                motor->set("th2t", 1.9);
                motor->set("tv", 0.025);
                addComponent(motor.release(), fraction);
            } else {
                auto motor =
                    std::make_unique<loads::WECCMotor3>("motor_" + std::to_string(motorIndex + 1U));
                motor->set("lfm", con[start + 1U]);
                motor->set("rs", con[start + 2U]);
                motor->set("ls", con[start + 3U]);
                motor->set("lp", con[start + 4U]);
                motor->set("lpp", con[start + 5U]);
                motor->set("tpo", con[start + 6U]);
                motor->set("tppo", con[start + 7U]);
                motor->set("h", con[start + 8U]);
                motor->set("etrq", con[start + 9U]);
                for (std::size_t stage = 0; stage < 2U; ++stage) {
                    const auto stageStart = start + 10U + (stage * 5U);
                    const auto suffix = std::to_string(stage + 1U);
                    motor->set("vtr" + suffix, con[stageStart]);
                    motor->set("ttr" + suffix, con[stageStart + 1U]);
                    motor->set("ftr" + suffix, con[stageStart + 2U]);
                    motor->set("vrc" + suffix, con[stageStart + 3U]);
                    motor->set("trc" + suffix, con[stageStart + 4U]);
                }
                addComponent(motor.release(), fraction);
            }
        }

        if (motorFractions[3] > 1.0e-9) {
            auto motor = std::make_unique<loads::MotorDLoad>("motor_d");
            motor->set("tstall", con[97U]);
            motor->set("trst", con[98U]);
            motor->set("tv", con[99U]);
            motor->set("lfm", con[101U]);
            motor->set("comppf", con[102U]);
            motor->set("vstall", con[103U]);
            motor->set("rstall", con[104U]);
            motor->set("xstall", con[105U]);
            motor->set("frst", con[116U]);
            motor->set("vrst", con[117U]);
            motor->set("vc1off", con[120U]);
            motor->set("vc2off", con[121U]);
            motor->set("vc1on", con[122U]);
            motor->set("vc2on", con[123U]);
            motor->set("tth", con[124U]);
            motor->set("th1t", con[125U]);
            motor->set("th2t", con[126U]);
            motor->set("fuvr", con[127U]);
            motor->set("vtr1", con[128U]);
            motor->set("ttr1", con[129U]);
            motor->set("vtr2", con[130U]);
            motor->set("ttr2", con[131U]);
            addComponent(motor.release(), motorFractions[3]);
        }

        if (electronicFraction > 1.0e-9) {
            auto electronic = std::make_unique<loads::ElectronicLoad>("electronic");
            electronic->set("pfel", con[23U]);
            electronic->set("vd1", con[24U]);
            electronic->set("vd2", con[25U]);
            // CMLDBLU1's fixed 132-CON record has no Frcel entry. Use the
            // reference default while preserving the model's adjustable XML API.
            electronic->set("frcel", 0.8);
            addComponent(electronic.release(), electronicFraction);
        }

        if (staticFraction > 1.0e-9) {
            addComponent(staticComponent.release(), staticFraction);
            if (useStaticReactiveOverride) {
                composite->setComponentReactiveBase(static_cast<index_t>(componentIndex - 1U),
                                                    staticReactiveOverride);
            }
        }
        if (componentIndex == 0U) {
            throw InvalidParameterValue("CMLDBLU1 has no supported load components");
        }

        const auto loadId = gmlc::utilities::stringOps::removeQuotes(tokens[2]);
        const auto networkName = "cmpldw_" + std::to_string(busId) + "_" + loadId;
        GridBus* lowSideBus = bus;
        if (hasTransformer) {
            auto lowSide = std::make_unique<AcBus>(networkName + "_low_side");
            lowSide->set("voltage", std::abs(lowSidePhasor));
            lowSide->set("angle", std::arg(lowSidePhasor));
            lowSide->Network = bus->Network;
            lowSide->set("basevoltage", bus->get("basevoltage", units::kV), units::kV);
            lowSideBus = lowSide.get();
            area->add(lowSide.release());
        }

        if (hasTransformer) {
            std::unique_ptr<AcLine> transformer;
            if (near(con[8U], -1.0)) {
                auto adjustable =
                    std::make_unique<links::AdjustableTransformer>(networkName + "_transformer");
                adjustable->set("controlmode", "voltage");
                adjustable->set("change", "stepped");
                adjustable->set("mintap", (con[9U] + con[7U] - 1.0) / con[6U]);
                adjustable->set("maxtap", (con[10U] + con[7U] - 1.0) / con[6U]);
                adjustable->set("stepsize", con[11U] / con[6U]);
                adjustable->set("vmin", con[12U]);
                adjustable->set("vmax", con[13U]);
                adjustable->setControlBus(lowSideBus);
                transformer = std::move(adjustable);
            } else {
                transformer = std::make_unique<AcLine>(networkName + "_transformer");
            }
            transformer->set("x", transformerReactance);
            transformer->set("ratio", fixedTapRatio);
            transformer->updateBus(bus, 1);
            transformer->updateBus(lowSideBus, 2);
            area->add(transformer.release());
        }

        if (std::abs(substationSusceptance) > 1.0e-12) {
            auto substationShunt = std::make_unique<ZipLoad>(networkName + "_bss");
            substationShunt->set("yq", -substationSusceptance);
            lowSideBus->add(substationShunt.release());
        }

        GridBus* loadBus = lowSideBus;
        if (hasFeeder) {
            auto loadEnd = std::make_unique<AcBus>(networkName + "_load_end");
            loadEnd->set("voltage", initialLoadVoltage);
            loadEnd->set("angle", std::arg(loadPhasor));
            loadEnd->Network = bus->Network;
            loadEnd->set("basevoltage", bus->get("basevoltage", units::kV), units::kV);
            loadBus = loadEnd.get();
            area->add(loadEnd.release());

            auto feeder = std::make_unique<AcLine>(networkName + "_feeder");
            feeder->set("r", feederResistance);
            feeder->set("x", feederReactance);
            feeder->set("b1", con[4U] * estimatedFeederCompensation);
            feeder->set("b2", (1.0 - con[4U]) * estimatedFeederCompensation);
            feeder->updateBus(lowSideBus, 1);
            feeder->updateBus(loadBus, 2);
            area->add(feeder.release());
        } else if (std::abs(estimatedFeederCompensation) > 1.0e-12) {
            // With no feeder reactance, WECC places the computed feeder
            // compensation as a single shunt at the low-side bus. This also
            // applies when the transformer is omitted and lowSideBus is the
            // original system bus.
            auto feederShunt = std::make_unique<ZipLoad>(networkName + "_bfeeder");
            feederShunt->set("yq", -estimatedFeederCompensation);
            lowSideBus->add(feederShunt.release());
        }

        auto* replacement = composite.release();
        if (loadBus == bus) {
            bus->replaceLoad(previousLoad, replacement);
        } else {
            bus->remove(previousLoad);
            loadBus->add(replacement);
        }
        replacement->setLoad(initialLoadP, initialQ);
    }

    int requireDyrInteger(const stringVec& tokens,
                          std::size_t index,
                          std::string_view modelName,
                          std::string_view fieldName)
    {
        int value = 0;
        if (index >= tokens.size()) {
            throw InvalidParameterValue(std::string{modelName} + " is missing " +
                                        std::string{fieldName});
        }
        const auto parsed = std::from_chars(tokens[index].data(),
                                            tokens[index].data() + tokens[index].size(),
                                            value);
        if (parsed.ec != std::errc{} || parsed.ptr != tokens[index].data() + tokens[index].size()) {
            throw InvalidParameterValue(std::string{modelName} + " requires an integer " +
                                        std::string{fieldName});
        }
        return value;
    }

    Link* findDyrTransformer(CoreObject* parentObject, int bus1Id, int bus2Id, int circuit)
    {
        auto* bus1 = dynamic_cast<GridBus*>(parentObject->findByUserID("bus", bus1Id));
        auto* bus2 = dynamic_cast<GridBus*>(parentObject->findByUserID("bus", bus2Id));
        if (bus1 == nullptr || bus2 == nullptr) {
            return nullptr;
        }
        for (index_t index = 0;; ++index) {
            auto* link = bus1->getLink(index);
            if (link == nullptr) {
                break;
            }
            const bool joinsBuses = (link->getBus(1) == bus2) || (link->getBus(2) == bus2);
            if (joinsBuses && static_cast<int>(link->get("circuit")) == circuit &&
                dynamic_cast<links::AdjustableTransformer*>(link) != nullptr) {
                return link;
            }
        }
        return nullptr;
    }

    void loadTIOCR1(CoreObject* parentObject, stringVec& tokens)
    {
        // This is the constrained form observed in the InterPSS/PSS/E Bus200
        // case.  TIOCR1's complete PSS/E semantics are not established yet.
        if (tokens.size() != 31U) {
            throw InvalidParameterValue(
                "TIOCR1 currently supports only the observed 29-parameter record form");
        }
        const int bus1Id = requireDyrInteger(tokens, 0, "TIOCR1", "first transformer bus");
        const int bus2Id = requireDyrInteger(tokens, 2, "TIOCR1", "second transformer bus");
        const int monitoredBusId = requireDyrInteger(tokens, 6, "TIOCR1", "monitored terminal bus");
        const int circuit = requireDyrInteger(tokens, 10, "TIOCR1", "circuit");
        if (tokens[7] != "BL" || tokens[3] != "1" || tokens[4] != "1" || tokens[5] != "1" ||
            tokens[30] != "1") {
            throw InvalidParameterValue(
                "TIOCR1 only supports the observed BL/1/1/1/.../1 compatibility variant");
        }

        const auto params = gmlc::utilities::str2vector(tokens, kNullVal);
        for (const auto index :
             {3U,  4U,  5U,  6U,  8U,  9U,  10U, 11U, 12U, 13U, 14U, 15U, 16U, 17U,
              18U, 19U, 20U, 21U, 22U, 23U, 24U, 25U, 26U, 27U, 28U, 29U, 30U}) {
            if (!std::isfinite(params[index]) || params[index] == kNullVal) {
                throw InvalidParameterValue("TIOCR1 contains a nonnumeric parameter");
            }
        }
        if (params[29] != 0.05) {
            throw InvalidParameterValue(
                "TIOCR1 only supports the observed penultimate parameter value 0.05");
        }
        if (circuit <= 0 || bus1Id == bus2Id ||
            (monitoredBusId != bus1Id && monitoredBusId != bus2Id)) {
            throw InvalidParameterValue("TIOCR1 has invalid transformer or monitored bus data");
        }

        // The three repeated references are identical in every known record.
        // Requiring that shape prevents us from guessing whether other TIOCR1
        // variants represent phases, multiple trip targets, or separate CTs.
        for (index_t group = 0; group < 3; ++group) {
            const auto first = 8U + (3U * group);
            if (params[first] != bus1Id || params[first + 1U] != bus2Id ||
                params[first + 2U] != circuit) {
                throw InvalidParameterValue(
                    "TIOCR1 only supports three repeated references to its transformer");
            }
        }

        auto* transformer = findDyrTransformer(parentObject, bus1Id, bus2Id, circuit);
        if (transformer == nullptr) {
            throw InvalidParameterValue("TIOCR1 requires an existing adjustable transformer " +
                                        std::to_string(bus1Id) + "-" + std::to_string(bus2Id) +
                                        " circuit " + std::to_string(circuit));
        }
        auto* monitoredBus =
            dynamic_cast<GridBus*>(parentObject->findByUserID("bus", monitoredBusId));
        auto* owner =
            monitoredBus == nullptr ? nullptr : dynamic_cast<GridArea*>(monitoredBus->getParent());
        if (owner == nullptr) {
            throw InvalidParameterValue("TIOCR1 monitored bus must belong to an area");
        }

        index_t terminal = 0;
        if (transformer->getBus(1) == monitoredBus) {
            terminal = 1;
        } else if (transformer->getBus(2) == monitoredBus) {
            terminal = 2;
        }
        if (terminal == 0) {
            throw InvalidParameterValue("TIOCR1 monitored bus is not a transformer terminal");
        }

        const auto relayName = "TIOCR1_" + std::to_string(bus1Id) + "_" + std::to_string(bus2Id) +
            "_" + std::to_string(circuit);
        for (index_t index = 0; owner->getRelay(index) != nullptr; ++index) {
            if (owner->getRelay(index)->getName() == relayName) {
                throw InvalidParameterValue("TIOCR1 duplicates relay " + relayName);
            }
        }

        const auto voltageBase = monitoredBus->get("basevoltage", units::kV);
        if (!std::isfinite(voltageBase) || voltageBase <= 0.0) {
            throw InvalidParameterValue("TIOCR1 monitored bus has an invalid voltage base");
        }

        std::array<relays::TimeOverCurrentRelay::TimeCurrentPoint, 6> points{};
        for (std::size_t point = 0; point < points.size(); ++point) {
            // The observed TIOCR1 values are interpreted as kA on the
            // monitored transformer terminal.  Convert to A here because the
            // relay's public table API accepts an explicit units argument.
            points[point] = {.current = params[17U + (2U * point)] * 1000.0,
                             .time = params[18U + (2U * point)]};
        }

        auto relay = std::make_unique<relays::TimeOverCurrentRelay>(relayName);
        relay->setSource(transformer);
        relay->setSink(transformer);
        relay->set("terminal", static_cast<double>(terminal));
        relay->set("voltagebase", voltageBase, units::kV);
        relay->setTimeCurrentCurve(points, units::A);
        owner->add(relay.release());
    }

    void loadMeasurement(CoreObject* parentObject, stringVec& tokens, std::string_view modelName)
    {
        std::size_t expected = 3U;
        if (modelName == "BUSROCOF" || modelName == "PLL1") {
            expected = 8U;
        } else if (modelName == "PLL2") {
            expected = 6U;
        }
        if (tokens.size() != expected) {
            throw InvalidParameterValue(std::string{modelName} +
                                        " DYR record has the wrong field count");
        }
        if (modelName == "BUSROCOF") {
            requireDyrGenerator(parentObject, tokens, modelName);
        }
        int busId = 0;
        const auto parsed =
            std::from_chars(tokens[0].data(), tokens[0].data() + tokens[0].size(), busId);
        if (parsed.ec != std::errc{} || parsed.ptr != tokens[0].data() + tokens[0].size()) {
            throw InvalidParameterValue(std::string{modelName} + " requires a numeric bus ID");
        }
        auto* source = dynamic_cast<GridBus*>(parentObject->findByUserID("bus", busId));
        auto* owner = source == nullptr ? nullptr : dynamic_cast<GridArea*>(source->getParent());
        if (owner == nullptr) {
            throw InvalidParameterValue(std::string{modelName} + " requires a bus in an area");
        }
        const auto nameIndex = modelName == "BUSROCOF" ? 3U : 2U;
        const auto name = gmlc::utilities::stringOps::removeQuotes(tokens[nameIndex]);
        if (name.empty()) {
            throw InvalidParameterValue(std::string{modelName} + " requires a measurement name");
        }
        for (index_t index = 0; owner->getRelay(index) != nullptr; ++index) {
            auto* existing = owner->getRelay(index);
            if (existing->getName() == name) {
                throw InvalidParameterValue(std::string{modelName} + " duplicates a relay name");
            }
        }
        std::unique_ptr<BusMeasurementSensor> sensor;
        if (modelName == "BUSROCOF") {
            sensor = std::make_unique<BusROCOFSensor>(name);
        } else if (modelName == "PLL1") {
            sensor = std::make_unique<PLL1Sensor>(name);
        } else if (modelName == "PLL2") {
            sensor = std::make_unique<PLL2Sensor>(name);
        } else {
            sensor = std::make_unique<FreqDivSensor>(name);
        }
        sensor->setSource(source);
        const auto params = gmlc::utilities::str2vector(tokens, kNullVal);
        const auto setFields = [&](std::span<const std::string_view> fields, std::size_t first) {
            for (std::size_t index = 0; index < fields.size(); ++index) {
                const double value = params[first + index];
                if (!std::isfinite(value) || value == kNullVal) {
                    throw InvalidParameterValue(std::string{modelName} +
                                                " DYR record has a nonnumeric field");
                }
                sensor->set(fields[index], value);
            }
        };
        if (modelName == "BUSROCOF") {
            static constexpr auto fields =
                std::to_array<std::string_view>({"tf", "tw", "tr", "fn"});
            setFields(fields, 4);
        } else if (modelName == "PLL1") {
            static constexpr auto fields =
                std::to_array<std::string_view>({"kp", "ki", "tf", "tp", "fn"});
            setFields(fields, 3);
        } else if (modelName == "PLL2") {
            static constexpr auto fields = std::to_array<std::string_view>({"kp", "ki", "fn"});
            setFields(fields, 3);
        }
        if (modelName == "BUSROCOF") {
            for (const auto* const key : {"tf", "tw", "tr", "fn"}) {
                if (sensor->get(key) <= 0.0) {
                    throw InvalidParameterValue(
                        "BUSROCOF DYR time constants and fn must be positive");
                }
            }
        } else if (modelName == "PLL1" || modelName == "PLL2") {
            if (sensor->get("kp") < 0.0 || sensor->get("ki") < 0.0 || sensor->get("fn") <= 0.0 ||
                (modelName == "PLL1" && (sensor->get("tf") <= 0.0 || sensor->get("tp") <= 0.0))) {
                throw InvalidParameterValue(std::string{modelName} + " DYR parameters are invalid");
            }
        }
        owner->add(sensor.get());
        [[maybe_unused]] auto* areaOwnedSensor = sensor.release();
    }

    void loadRenewable(CoreObject* parentObject, stringVec& tokens, std::string_view modelName)
    {
        static constexpr auto regcaFields = std::to_array<std::string_view>({"lvplsw",
                                                                             "tg",
                                                                             "rrpwr",
                                                                             "brkpt",
                                                                             "zerox",
                                                                             "lvpl1",
                                                                             "volim",
                                                                             "lvpnt1",
                                                                             "lvpnt0",
                                                                             "iolim",
                                                                             "tfltr",
                                                                             "khv",
                                                                             "iqrmax",
                                                                             "iqrmin",
                                                                             "accel"});
        static constexpr auto epcgenFields = std::to_array<std::string_view>({"rsrc",
                                                                              "xsrc",
                                                                              "tfrq",
                                                                              "ofpdb",
                                                                              "ufpdb",
                                                                              "ofpdroop",
                                                                              "ufpdroop",
                                                                              "vbreak",
                                                                              "imax",
                                                                              "pmax",
                                                                              "pmin",
                                                                              "pref"});
        static constexpr auto reecaFields = std::to_array<std::string_view>(
            {"pfflag", "vflag", "qflag", "pflag", "pqflag", "vdip",  "vup",   "trv",   "dbd1",
             "dbd2",   "kqv",   "iqh1",  "iql1",  "vref0",  "iqfrz", "thld",  "thld2", "tp",
             "qmax",   "qmin",  "vmax",  "vmin",  "kqp",    "kqi",   "kvp",   "kvi",   "vref1",
             "tiq",    "dpmax", "dpmin", "pmax",  "pmin",   "imax",  "tpord", "vq1",   "iq1",
             "vq2",    "iq2",   "vq3",   "iq3",   "vq4",    "iq4",   "vp1",   "ip1",   "vp2",
             "ip2",    "vp3",   "ip3",   "vp4",   "ip4"});
        static constexpr auto repcaFields = std::to_array<std::string_view>(
            {"vcflag", "refflag", "fflag", "tfltr", "kp",   "ki",    "tft",   "tfv",
             "vfrz",   "rc",      "xc",    "kc",    "emax", "emin",  "dbd1",  "dbd2",
             "qmax",   "qmin",    "kpg",   "kig",   "tp",   "fdbd1", "fdbd2", "femax",
             "femin",  "pmax",    "pmin",  "tg",    "ddn",  "dup"});
        static constexpr auto reecbFields = std::to_array<std::string_view>(
            {"pfflag", "vflag", "qflag", "pqflag", "vdip",  "vup",  "trv",  "dbd1", "dbd2", "kqv",
             "iqh1",   "iql1",  "vref0", "tp",     "qmax",  "qmin", "vmax", "vmin", "kqp",  "kqi",
             "kvp",    "kvi",   "tiq",   "dpmax",  "dpmin", "pmax", "pmin", "imax", "tpord"});
        static constexpr auto reeccFields = std::to_array<std::string_view>(
            {"pfflag", "vflag",  "qflag",  "pqflag", "vdip", "vup",   "trv",   "dbd1", "dbd2",
             "kqv",    "iqh1",   "iql1",   "vref0",  "tp",   "qmax",  "qmin",  "vmax", "vmin",
             "kqp",    "kqi",    "kvp",    "kvi",    "tiq",  "dpmax", "dpmin", "pmax", "pmin",
             "imax",   "tpord",  "vq1",    "iq1",    "vq2",  "iq2",   "vq3",   "iq3",  "vq4",
             "iq4",    "vp1",    "ip1",    "vp2",    "ip2",  "vp3",   "ip3",   "vp4",  "ip4",
             "t",      "socini", "socmax", "socmin"});
        static constexpr auto wtdtaFields =
            std::to_array<std::string_view>({"h", "damp", "htfrac", "freq1", "dshaft"});
        static constexpr auto wt3gFields =
            std::to_array<std::string_view>({"xeq", "kpll", "kipll", "pllmax", "prated"});
        static constexpr auto wt3eFields = std::to_array<std::string_view>(
            {"tfv", "kpv",    "kiv",    "xc",    "tfp",    "kpp",       "kip",  "pmx",
             "pmn", "qmx",    "qmn",    "ipmax", "trv",    "rpmx",      "rpmn", "tpower",
             "kqi", "vmincl", "vmaxcl", "kqv",   "xiqmin", "xiqmax",    "tv",   "tp",
             "fn",  "wpmin",  "wp20",   "wp40",  "wp60",   "pminspeed", "wp100"});
        static constexpr auto wt4gFields = std::to_array<std::string_view>({"tiqcmd",
                                                                            "tipcmd",
                                                                            "vlvpl1",
                                                                            "vlvpl2",
                                                                            "glvpl",
                                                                            "vhvrcr",
                                                                            "curhvrcr",
                                                                            "riplvpl",
                                                                            "tlvpl"});
        static constexpr auto wt4eFields = std::to_array<std::string_view>(
            {"tfv",    "kpv",   "kiv", "kpp",  "kip",    "kf",     "tf",  "qmx",
             "qmn",    "ipmax", "trv", "dpmx", "dpmn",   "tpower", "kqi", "vmincl",
             "vmaxcl", "kvi",   "tv",  "tp",   "imaxtd", "iphl",   "iqhl"});
        static constexpr auto wtaraFields = std::to_array<std::string_view>({"ka", "theta0"});
        static constexpr auto wtptaFields = std::to_array<std::string_view>({"kiw",
                                                                             "kpw",
                                                                             "kic",
                                                                             "kpc",
                                                                             "kcc",
                                                                             "tp",
                                                                             "tetamax",
                                                                             "tetamin",
                                                                             "rtetamax",
                                                                             "rtetamin"});
        static constexpr auto wttqaFields = std::to_array<std::string_view>({"tflag",
                                                                             "kpp",
                                                                             "kip",
                                                                             "tp",
                                                                             "twref",
                                                                             "temax",
                                                                             "temin",
                                                                             "p1",
                                                                             "spd1",
                                                                             "p2",
                                                                             "spd2",
                                                                             "p3",
                                                                             "spd3",
                                                                             "p4",
                                                                             "spd4"});
        static constexpr auto regcv1Fields = std::to_array<std::string_view>({"fn",
                                                                              "tc",
                                                                              "kw",
                                                                              "kv",
                                                                              "m",
                                                                              "d",
                                                                              "ra",
                                                                              "xs",
                                                                              "kpvd",
                                                                              "kivd",
                                                                              "kpvq",
                                                                              "kivq",
                                                                              "kpid",
                                                                              "kiid",
                                                                              "kpiq",
                                                                              "kiiq"});
        static constexpr auto regcv2Fields = std::to_array<std::string_view>(
            {"fn", "kw", "kv", "m", "d", "ra", "xs", "kpvd", "kivd", "kpvq", "kivq", "tid", "tiq"});
        static constexpr auto regfFields = std::to_array<std::string_view>(
            {"fn",     "rf",   "xf",   "dwmax",  "dwmin",  "wdrp", "qdrp", "tr",
             "te",     "kpi",  "kii",  "kpv",    "kiv",    "pmax", "pmin", "kpplim",
             "kiplim", "qmax", "qmin", "kpqlim", "kiqlim", "tpm"});

        std::size_t expected = 19U;
        if (modelName == "REGCP1" && tokens.size() == 19U) {
            expected = 19U;
        } else if (modelName == "REGCA1" || modelName == "REGCP1") {
            expected = 18U;
        } else if (modelName == "REECA1") {
            expected = 54U;
        } else if (modelName == "REECA1E") {
            expected = 57U;
        } else if (modelName == "REECA1G") {
            expected = 56U;
        } else if (modelName == "REECB1") {
            expected = 33U;
        } else if (modelName == "REECC1") {
            expected = 4U + reeccFields.size();
        } else if (modelName == "REGCV1") {
            expected = 3U + regcv1Fields.size();
        } else if (modelName == "REGCV2") {
            expected = 3U + regcv2Fields.size();
        } else if (modelName == "REGF1" || modelName == "REGF3") {
            expected = 3U + regfFields.size();
        } else if (modelName == "REGF2") {
            expected = 6U + regfFields.size();
        } else if (modelName == "EPCGEN") {
            expected = 3U + epcgenFields.size();
        } else if (modelName == "REPCA1") {
            expected = 37U;
        } else if (modelName == "WTDTA1") {
            if (tokens.size() == 10U) {
                expected = 10U;
            } else if (tokens.size() == 9U) {
                expected = 9U;
            } else {
                expected = 8U;
            }
        } else if (modelName == "WTDS") {
            expected = (tokens.size() == 7U) ? 7U : 6U;
        } else if (modelName == "WTARA1") {
            expected = 5U;
        } else if (modelName == "WTPTA1") {
            expected = (tokens.size() == 14U) ? 14U : 13U;
        } else if (modelName == "WTTQA1") {
            expected = (tokens.size() == 19U) ? 19U : 18U;
        } else if (modelName == "WT3G1") {
            expected = 9U;
        } else if (modelName == "WT3E1") {
            // PSS/E WT3E1 retains six leading machine/mode fields before the
            // electrical-control parameter block used by WT3E1.
            expected = 9U + wt3eFields.size();
        } else if (modelName == "WT4G1") {
            expected = 4U + wt4gFields.size();
        } else if (modelName == "WT4E1") {
            expected = 7U + wt4eFields.size();
        }
        if (tokens.size() != expected) {
            throw InvalidParameterValue(std::string{modelName} +
                                        " DYR record has the wrong field count");
        }
        auto* generator = requireDyrGenerator(parentObject, tokens, modelName);
        const auto params = gmlc::utilities::str2vector(tokens, kNullVal);
        std::string const modelKey = gmlc::utilities::convertToLowerCase(modelName);
        std::unique_ptr<RenewableComponent> model(dynamic_cast<RenewableComponent*>(
            CoreObjectFactory::instance()->createObject("renewable_model", modelKey)));
        if (model == nullptr) {
            throw InvalidParameterValue(std::string{modelName} + " factory is unavailable");
        }
        const auto setFields = [&](const auto& fields, std::size_t firstIndex) {
            for (std::size_t index = 0; index < fields.size(); ++index) {
                const auto value = params[firstIndex + index];
                if (!std::isfinite(value) || value == kNullVal) {
                    throw InvalidParameterValue(std::string{modelName} +
                                                " DYR record has a nonnumeric field");
                }
                model->set(fields[index], value);
            }
        };
        if (modelName == "REGCA1" || modelName == "REGCP1") {
            setFields(regcaFields, 3);
            if (modelName == "REGCP1" && tokens.size() == 19U) {
                model->set("pll", gmlc::utilities::stringOps::removeQuotes(tokens[18]));
            }
        } else if (modelName == "REECA1") {
            if (params[3] != 0.0) {
                throw InvalidParameterValue("REECA1 remote BUSR is not yet supported");
            }
            setFields(reecaFields, 4);
        } else if (modelName == "REECA1E") {
            if (params[3] != 0.0) {
                throw InvalidParameterValue("REECA1E remote BUSR is not yet supported");
            }
            setFields(reecaFields, 4);
            static constexpr auto frequencyFields = std::to_array<std::string_view>({"kf", "kdf"});
            setFields(frequencyFields, 54);
            model->set("busroc", gmlc::utilities::stringOps::removeQuotes(tokens[56]));
        } else if (modelName == "REECA1G") {
            if (params[3] != 0.0) {
                throw InvalidParameterValue("REECA1G remote BUSR is not yet supported");
            }
            setFields(reecaFields, 4);
            static constexpr auto speedFields = std::to_array<std::string_view>({"kf"});
            setFields(speedFields, 54);
            model->set("sg", gmlc::utilities::stringOps::removeQuotes(tokens[55]));
        } else if (modelName == "REECB1") {
            if (params[3] != 0.0) {
                throw InvalidParameterValue("REECB1 remote BUSR is not yet supported");
            }
            setFields(reecbFields, 4);
        } else if (modelName == "REECC1") {
            if (params[3] != 0.0) {
                throw InvalidParameterValue("REECC1 remote BUSR is not yet supported");
            }
            setFields(reeccFields, 4);
        } else if (modelName == "REGCV1") {
            setFields(regcv1Fields, 3);
        } else if (modelName == "REGCV2") {
            setFields(regcv2Fields, 3);
        } else if (modelName == "REGF1" || modelName == "REGF2" || modelName == "REGF3") {
            setFields(regfFields, 3);
            if (modelName == "REGF2") {
                static constexpr auto vsmFields = std::to_array<std::string_view>({"mf", "dd"});
                setFields(vsmFields, 3U + regfFields.size());
                model->set("pll", gmlc::utilities::stringOps::removeQuotes(tokens.back()));
            }
        } else if (modelName == "EPCGEN") {
            setFields(epcgenFields, 3);
        } else if (modelName == "REPCA1") {
            if (params[4] != 0.0 || params[5] != 0.0) {
                throw InvalidParameterValue(
                    "REPCA1 monitored-line measurement is not yet supported");
            }
            model->set("vbus", params[3]);
            setFields(repcaFields, 7);
        } else if (modelName == "WTDTA1") {
            setFields(wtdtaFields, 3);
            if (tokens.size() >= 9U) {
                model->set("w0", params[8]);
            }
            if (tokens.size() == 10U) {
                const double modelBase = params[9];
                const double machineBase = generator->get("mbase", units::MVAR);
                if (!std::isfinite(modelBase) || modelBase <= 0.0 || !std::isfinite(machineBase) ||
                    machineBase <= 0.0) {
                    throw InvalidParameterValue(
                        "WTDTA1 requires a positive model and machine base");
                }
                const double baseRatio = modelBase / machineBase;
                model->set("h", params[3] * baseRatio);
                model->set("dshaft", params[7] * baseRatio);
            }
        } else if (modelName == "WTDS") {
            static constexpr auto wtdsFields = std::to_array<std::string_view>({"h", "d", "w0"});
            setFields(wtdsFields, 3);
            if (tokens.size() == 7U) {
                const double modelBase = params[6];
                const double machineBase = generator->get("mbase", units::MVAR);
                if (!std::isfinite(modelBase) || modelBase <= 0.0 || !std::isfinite(machineBase) ||
                    machineBase <= 0.0) {
                    throw InvalidParameterValue("WTDS requires a positive model and machine base");
                }
                model->set("h", params[3] * (modelBase / machineBase));
            }
        } else if (modelName == "WTARA1") {
            setFields(wtaraFields, 3);
        } else if (modelName == "WTPTA1") {
            setFields(wtptaFields, 3);
            if (tokens.size() == 14U) {
                model->set("pset", params[13]);
            }
        } else if (modelName == "WTTQA1") {
            setFields(wttqaFields, 3);
            if (tokens.size() == 19U) {
                const double rate = params[18];
                if (rate == 0.0) {
                    // PSS/E uses TRATE=0 to mean the static machine MVA base.
                    const double machineBase = generator->get("mbase", units::MVAR);
                    if (!std::isfinite(machineBase) || machineBase <= 0.0) {
                        throw InvalidParameterValue(
                            "WTTQA1 TRATE=0 requires a positive machine MVA base");
                    }
                    model->set("trate", machineBase);
                } else {
                    model->set("trate", rate);
                }
            }
        } else if (modelName == "WT3G1") {
            // WIND_BASE at field 4 belongs to the generator machine base;
            // the WT3G electrical equations start with XEQ.
            setFields(wt3gFields, 4);
        } else if (modelName == "WT3E1") {
            model->set("varflg", params[3]);
            model->set("vlrflg", params[4]);
            setFields(wt3eFields, 9);
        } else if (modelName == "WT4G1") {
            // MBASE is represented by RenewableGenerator::machineBasePower.
            setFields(wt4gFields, 4);
        } else if (modelName == "WT4E1") {
            model->set("pfaflg", params[3]);
            model->set("varflg", params[4]);
            model->set("pqflag", params[5]);
            model->set("pssematch", params[6]);
            setFields(wt4eFields, 7);
        } else {
            setFields(wttqaFields, 3);
        }
        // Validate mode flags and numerical ranges before replacing a steady-state generator.
        model->dynInitializeA(0.0, 0);

        auto* renewable = dynamic_cast<RenewableGenerator*>(generator);
        std::unique_ptr<RenewableGenerator> replacement;
        if (renewable == nullptr) {
            if (!generator->getSubObjects().empty()) {
                throw InvalidParameterValue(std::string{modelName} +
                                            " conflicts with an attached synchronous model");
            }
            replacement = std::make_unique<RenewableGenerator>(generator->getName());
            generator->Generator::clone(replacement.get());
            renewable = replacement.get();
        }
        const auto role = static_cast<index_t>(model->role());
        if (renewable->getSubObject("renewable_component", role) != nullptr) {
            throw InvalidParameterValue(std::string{modelName} +
                                        " duplicate renewable role for bus " + tokens[0] +
                                        " machine " + tokens[2]);
        }
        if (replacement != nullptr) {
            auto* bus = dynamic_cast<GridBus*>(generator->getParent());
            if (bus == nullptr) {
                throw InvalidParameterValue("renewable DYR generator has no parent bus");
            }
            bus->replaceGenerator(generator, replacement.get());
            auto* releasedReplacement = replacement.release();
            (void)releasedReplacement;
        }
        renewable->add(model.get());
        auto* releasedModel = model.release();
        (void)releasedModel;
    }

    void loadGENCLS(CoreObject* parentObject, stringVec& tokens)
    {
        if (tokens.size() < 5) {
            throw InvalidParameterValue("GENCLS DYR record");
        }

        auto* gen = requireDyrGenerator(parentObject, tokens, "GENCLS");

        const auto params = gmlc::utilities::str2vector(tokens, kNullVal);
        auto* genModel = static_cast<GenModel*>(
            CoreObjectFactory::instance()->createObject("genmodel", "gencls"));
        // The RAW generator supplies ra and stateValue'd. Attach before applying the
        // two GENCLS DYR parameters so DynamicGenerator transfers ZSOURCE.
        gen->add(genModel);
        genModel->set("h", params[3]);
        genModel->set("d", params[4]);
    }

    void loadGENROU(CoreObject* parentObject, stringVec& tokens)
    {
        auto* gen = requireDyrGenerator(parentObject, tokens, "GENROU");

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

    void loadCSVGN1(CoreObject* parentObject, stringVec& tokens)
    {
        // PSS/E order: bus, model, machine ID, K, T1, T2, T3, T4, T5,
        // RMIN, VMAX, VMIN, CBASE.
        if (tokens.size() != 13U) {
            throw InvalidParameterValue("CSVGN1 DYR record must contain 13 fields");
        }

        auto* gen = requireDyrGenerator(parentObject, tokens, "CSVGN1");
        const auto params = gmlc::utilities::str2vector(tokens, kNullVal);
        auto* model = static_cast<GenModel*>(
            CoreObjectFactory::instance()->createObject("genmodel", "csvgn1"));

        // CSVGN1 replaces the synchronous machine on this PSS/E generator
        // record. Preserve the RAW generator's MBASE for the SVC reactor base.
        gen->add(model);
        model->set("base", gen->get("mbase", units::MVAR));
        model->set("k", params[3]);
        model->set("t1", params[4]);
        model->set("t2", params[5]);
        model->set("t3", params[6]);
        model->set("t4", params[7]);
        model->set("t5", params[8]);
        model->set("rmin", params[9]);
        model->set("vmax", params[10]);
        model->set("vmin", params[11]);
        model->set("cbase", params[12]);
    }

    void loadGENTPJ(CoreObject* parentObject, stringVec& tokens)
    {
        if (tokens.size() != 19U) {
            throw InvalidParameterValue("GENTPJ DYR record must contain 19 fields");
        }
        auto* gen = requireDyrGenerator(parentObject, tokens, "GENTPJ");
        const auto params = gmlc::utilities::str2vector(tokens, kNullVal);
        auto* model = static_cast<GenModel*>(
            CoreObjectFactory::instance()->createObject("genmodel", "gentpj"));
        gen->add(model);
        model->set("tdop", params[3]);
        model->set("tdopp", params[4]);
        model->set("tqop", params[5]);
        model->set("tqopp", params[6]);
        model->set("h", params[7]);
        model->set("d", params[8]);
        model->set("xd", params[9]);
        model->set("xq", params[10]);
        model->set("xdp", params[11]);
        model->set("xqp", params[12]);
        model->set("xdpp", params[13]);
        model->set("xqpp", params[14]);
        model->set("xl", params[15]);
        model->set("s10", params[16]);
        model->set("s12", params[17]);
        model->set("kis", params[18]);
    }

    void loadGENROE(CoreObject* parentObject, stringVec& tokens)
    {
        if (tokens.size() != 17U) {
            throw InvalidParameterValue("GENROE DYR record must contain 17 fields");
        }
        auto* gen = requireDyrGenerator(parentObject, tokens, "GENROE");
        const auto params = gmlc::utilities::str2vector(tokens, kNullVal);
        auto* genModel = static_cast<GenModel*>(
            CoreObjectFactory::instance()->createObject("genmodel", "genroe"));
        // As with GENROU, attach first so the RAW ZSOURCE resistance is retained.
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
        genModel->set("s10", params[15]);
        genModel->set("s12", params[16]);
    }

    void loadGENSAE(CoreObject* parentObject, stringVec& tokens)
    {
        if (tokens.size() != 15U) {
            throw InvalidParameterValue("GENSAE DYR record must contain 15 fields");
        }
        auto* gen = requireDyrGenerator(parentObject, tokens, "GENSAE");
        const auto params = gmlc::utilities::str2vector(tokens, kNullVal);
        auto* model = static_cast<GenModel*>(
            CoreObjectFactory::instance()->createObject("genmodel", "gensae"));
        // The salient-pole records use the same DYR order as GENSAL. Attach
        // before replacing Xd so the RAW source resistance is preserved.
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

    void loadGENSAL(CoreObject* parentObject, stringVec& tokens)
    {
        if (tokens.size() != 15U) {
            throw InvalidParameterValue("GENSAL DYR record must contain 15 fields");
        }
        auto* gen = requireDyrGenerator(parentObject, tokens, "GENSAL");
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
        auto* gen = requireDyrGenerator(parentObject, tokens, "ESDC1A");

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
        auto* gen = requireDyrGenerator(parentObject, tokens, "ESDC2A");
        const auto params = gmlc::utilities::str2vector(tokens, kNullVal);
        // PSS/E does not implement the ESDC2A Switch selector.  Reject a
        // nonzero value rather than silently importing a different model.
        if (params[14] != 0.0) {
            throw InvalidParameterValue("ESDC2A nonzero Switch is unsupported");
        }
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
        auto* gen = requireDyrGenerator(parentObject, tokens, "IEEET1");
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

    void loadIEEET2(CoreObject* parentObject, stringVec& tokens)
    {
        if (tokens.size() != 17U) {
            throw InvalidParameterValue("IEEET2 DYR record must contain 17 fields");
        }
        auto* gen = requireDyrGenerator(parentObject, tokens, "IEEET2");
        const auto params = gmlc::utilities::str2vector(tokens, kNullVal);
        auto* exciterModel =
            static_cast<Exciter*>(CoreObjectFactory::instance()->createObject("exciter", "ieeet2"));
        if (exciterModel == nullptr) {
            throw InvalidParameterValue("IEEET2 factory registration");
        }

        // Exact PSS/E/OpenIPSL order after BUS and machine ID:
        // TR, KA, TA, VRMAX, VRMIN, KE, TE, KF, TF1, TF2,
        // E1, SE1, E2, SE2.
        static constexpr std::array<std::string_view, 14> names{"tr",
                                                                "ka",
                                                                "ta",
                                                                "vrmax",
                                                                "vrmin",
                                                                "ke",
                                                                "te",
                                                                "kf",
                                                                "tf1",
                                                                "tf2",
                                                                "e1",
                                                                "se1",
                                                                "e2",
                                                                "se2"};
        for (std::size_t index = 0; index < names.size(); ++index) {
            exciterModel->set(names[index], params[index + 3]);
        }
        gen->add(exciterModel);
    }

    void loadIEEET3(CoreObject* parentObject, stringVec& tokens)
    {
        if (tokens.size() != 15U) {
            throw InvalidParameterValue("IEEET3 DYR record must contain 15 fields");
        }
        auto* gen = requireDyrGenerator(parentObject, tokens, "IEEET3");
        const auto params = gmlc::utilities::str2vector(tokens, kNullVal);
        auto* model =
            static_cast<Exciter*>(CoreObjectFactory::instance()->createObject("exciter", "ieeet3"));
        // PSS/E order verified against the ANDES psse-dyr.yaml IEEET3 schema.
        static constexpr std::array<std::string_view, 12> names{
            "tr", "ka", "ta", "vrmax", "vrmin", "vbmax", "ke", "te", "kf", "tf", "kp", "ki"};
        for (std::size_t index = 0; index < names.size(); ++index) {
            model->set(names[index], params[index + 3]);
        }
        gen->add(model);
    }

    void loadIEEEX1(CoreObject* parentObject, stringVec& tokens)
    {
        if (tokens.size() != 19U) {
            throw InvalidParameterValue("IEEEX1 DYR record must contain 19 fields");
        }
        auto* gen = requireDyrGenerator(parentObject, tokens, "IEEEX1");
        const auto params = gmlc::utilities::str2vector(tokens, kNullVal);
        // PSS/E does not implement the IEEEX1 Switch selector.  Reject an
        // unverified branch instead of silently importing a different model.
        if (params[14] != 0.0) {
            throw InvalidParameterValue("IEEEX1 nonzero Switch is unsupported");
        }
        auto* exciterModel =
            static_cast<Exciter*>(CoreObjectFactory::instance()->createObject("exciter", "ieeex1"));
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

    void loadAC7B(CoreObject* parentObject, stringVec& tokens)
    {
        if (tokens.size() != 30U) {
            throw InvalidParameterValue("AC7B DYR record must contain 30 fields");
        }
        auto* gen = requireDyrGenerator(parentObject, tokens, "AC7B");
        const auto params = gmlc::utilities::str2vector(tokens, kNullVal);
        auto* model =
            static_cast<Exciter*>(CoreObjectFactory::instance()->createObject("exciter", "ac7b"));
        // AC7B uses the 27-parameter order of the IEEE/OpenIPSL block:
        // TR, KPR, KIR, KDR, TDR, VRMAX, VRMIN, KPA, KIA, VAMAX, VAMIN,
        // KP, KL, TE, KC, KD, KE, KF1, KF2, KF3, TF3, VEMIN, VFEMAX,
        // E1, SE1, E2, SE2.
        static constexpr std::array<std::string_view, 27> names{"tr",  "kpr",   "kir",    "kdr",
                                                                "tdr", "vrmax", "vrmin",  "kpa",
                                                                "kia", "vamax", "vamin",  "kp",
                                                                "kl",  "te",    "kc",     "kd",
                                                                "ke",  "kf1",   "kf2",    "kf3",
                                                                "tf3", "vemin", "vfemax", "e1",
                                                                "se1", "e2",    "se2"};
        for (std::size_t index = 0; index < names.size(); ++index) {
            model->set(names[index], params[index + 3]);
        }
        gen->add(model);
    }

    void loadAC8B(CoreObject* parentObject, stringVec& tokens)
    {
        if (tokens.size() != 24U) {
            throw InvalidParameterValue("AC8B DYR record must contain 24 fields");
        }
        auto* gen = requireDyrGenerator(parentObject, tokens, "AC8B");
        const auto params = gmlc::utilities::str2vector(tokens, kNullVal);
        auto* model =
            static_cast<Exciter*>(CoreObjectFactory::instance()->createObject("exciter", "ac8b"));
        // Exact ANDES psse-dyr.yaml order.
        static constexpr std::array<std::string_view, 21> names{"tr",    "kpr",    "kir",   "kdr",
                                                                "tdr",   "vpmax",  "vpmin", "vrmax",
                                                                "vrmin", "vfemax", "vemin", "ta",
                                                                "ka",    "te",     "kc",    "kd",
                                                                "ke",    "e1",     "se1",   "e2",
                                                                "se2"};
        for (std::size_t index = 0; index < names.size(); ++index) {
            model->set(names[index], params[index + 3]);
        }
        gen->add(model);
    }

    void loadESST1A(CoreObject* parentObject, stringVec& tokens)
    {
        if (tokens.size() != 23U) {
            throw InvalidParameterValue("ESST1A DYR record must contain 23 fields");
        }
        auto* gen = requireDyrGenerator(parentObject, tokens, "ESST1A");
        const auto params = gmlc::utilities::str2vector(tokens, kNullVal);
        auto* model =
            static_cast<Exciter*>(CoreObjectFactory::instance()->createObject("exciter", "esst1a"));
        // PSS/E order: UEL, VOS, TR, VIMAX, VIMIN, TC, TB, TC1, TB1,
        // KA, TA, VAMAX, VAMIN, VRMAX, VRMIN, KC, KF, TF, KLR, ILR.
        // UEL and VOS are selectors, not continuous signals; the standard
        // vss interface provides the selected VOS source.
        model->set("uel", params[3]);
        model->set("vos", params[4]);
        model->set("tr", params[5]);
        model->set("vimax", params[6]);
        model->set("vimin", params[7]);
        model->set("tc", params[8]);
        model->set("tb", params[9]);
        model->set("tc1", params[10]);
        model->set("tb1", params[11]);
        model->set("ka", params[12]);
        model->set("ta", params[13]);
        model->set("vamax", params[14]);
        model->set("vamin", params[15]);
        model->set("vrmax", params[16]);
        model->set("vrmin", params[17]);
        model->set("kc", params[18]);
        model->set("kf", params[19]);
        model->set("tf", params[20]);
        model->set("klr", params[21]);
        model->set("ilr", params[22]);
        gen->add(model);
    }

    void loadESST2A(CoreObject* parentObject, stringVec& tokens)
    {
        if (tokens.size() != 16U) {
            throw InvalidParameterValue("ESST2A DYR record must contain 16 fields");
        }
        auto* gen = requireDyrGenerator(parentObject, tokens, "ESST2A");
        const auto params = gmlc::utilities::str2vector(tokens, kNullVal);
        auto* model =
            static_cast<Exciter*>(CoreObjectFactory::instance()->createObject("exciter", "esst2a"));
        // GridDyn's compact 13-parameter layout for the OpenIPSL ESST2A core.
        // Separate VUEL/VOEL selector fields are intentionally not accepted.
        static constexpr std::array<std::string_view, 13> names{
            "tr", "ka", "ta", "vrmax", "vrmin", "kp", "ki", "kc", "kf", "tf", "ke", "te", "efdmax"};
        for (std::size_t index = 0; index < names.size(); ++index) {
            model->set(names[index], params[index + 3]);
        }
        gen->add(model);
    }

    void loadESST3A(CoreObject* parentObject, stringVec& tokens)
    {
        auto* gen = requireDyrGenerator(parentObject, tokens, "ESST3A");

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
        auto* gen = requireDyrGenerator(parentObject, tokens, "ESST4B");
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
        auto* gen = requireDyrGenerator(parentObject, tokens, "EXPIC1");
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

    void loadSCRX(CoreObject* parentObject, stringVec& tokens)
    {
        if (tokens.size() != 11U) {
            throw InvalidParameterValue("SCRX DYR record must contain 11 fields");
        }
        auto* gen = requireDyrGenerator(parentObject, tokens, "SCRX");

        const auto params = gmlc::utilities::str2vector(tokens, kNullVal);
        auto* model =
            static_cast<Exciter*>(CoreObjectFactory::instance()->createObject("exciter", "scrx"));
        static constexpr std::array<std::string_view, 8> names{
            "tatb", "tb", "k", "te", "emin", "emax", "cswitch", "rcrfd"};
        for (std::size_t ii = 0; ii < names.size(); ++ii) {
            model->set(names[ii], params[ii + 3]);
        }
        gen->add(model);
    }

    void loadESAC6A(CoreObject* parentObject, stringVec& tokens)
    {
        if (tokens.size() != 26U) {
            throw InvalidParameterValue("ESAC6A DYR record must contain 26 fields");
        }
        auto* gen = requireDyrGenerator(parentObject, tokens, "ESAC6A");

        const auto params = gmlc::utilities::str2vector(tokens, kNullVal);
        auto* model =
            static_cast<Exciter*>(CoreObjectFactory::instance()->createObject("exciter", "esac6a"));
        static constexpr std::array<std::string_view, 23> names{"tr",    "ka",    "ta",    "tk",
                                                                "tb",    "tc",    "vamax", "vamin",
                                                                "vrmax", "vrmin", "te",    "vfelim",
                                                                "kh",    "vhmax", "th",    "tj",
                                                                "kc",    "kd",    "ke",    "e1",
                                                                "se1",   "e2",    "se2"};
        for (std::size_t ii = 0; ii < names.size(); ++ii) {
            model->set(names[ii], params[ii + 3]);
        }
        gen->add(model);
    }

    void loadEXST1(CoreObject* parentObject, stringVec& tokens)
    {
        auto* gen = requireDyrGenerator(parentObject, tokens, "EXST1");

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
        auto* gen = requireDyrGenerator(parentObject, tokens, "EXAC1");
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

    void loadESAC1A(CoreObject* parentObject, stringVec& tokens)
    {
        // BUS, 'ESAC1A', ID, TR, TB, TC, KA, TA, VRMAX, VRMIN, TE, KF, TF,
        // KC, KD, KE, E1, SE1, E2, SE2, VAMAX, VAMIN /
        if (tokens.size() != 22U) {
            throw InvalidParameterValue("ESAC1A DYR record must contain 22 fields");
        }
        auto* gen = requireDyrGenerator(parentObject, tokens, "ESAC1A");
        const auto params = gmlc::utilities::str2vector(tokens, kNullVal);
        auto* exciter =
            static_cast<Exciter*>(CoreObjectFactory::instance()->createObject("exciter", "esac1a"));
        static constexpr std::array<std::string_view, 19> names{"tr",
                                                                "tb",
                                                                "tc",
                                                                "ka",
                                                                "ta",
                                                                "vrmax",
                                                                "vrmin",
                                                                "te",
                                                                "kf",
                                                                "tf",
                                                                "kc",
                                                                "kd",
                                                                "ke",
                                                                "e1",
                                                                "se1",
                                                                "e2",
                                                                "se2",
                                                                "vamax",
                                                                "vamin"};
        for (std::size_t index = 0; index < names.size(); ++index) {
            exciter->set(names[index], params[index + 3]);
        }
        gen->add(exciter);
    }

    void loadESAC5A(CoreObject* parentObject, stringVec& tokens)
    {
        // PSS/E CON(J..J+14): TR, KA, TA, VRMAX, VRMIN, KE, TE, KF,
        // TF1, TF2, TF3, E1, SE(E1), E2, SE(E2).
        if (tokens.size() != 18U) {
            throw InvalidParameterValue("ESAC5A DYR record must contain 18 fields");
        }
        auto* gen = requireDyrGenerator(parentObject, tokens, "ESAC5A");
        const auto params = gmlc::utilities::str2vector(tokens, kNullVal);
        auto* exciter =
            static_cast<Exciter*>(CoreObjectFactory::instance()->createObject("exciter", "esac5a"));
        static constexpr std::array<std::string_view, 15> names{"tr",
                                                                "ka",
                                                                "ta",
                                                                "vrmax",
                                                                "vrmin",
                                                                "ke",
                                                                "te",
                                                                "kf",
                                                                "tf1",
                                                                "tf2",
                                                                "tf3",
                                                                "e1",
                                                                "se1",
                                                                "e2",
                                                                "se2"};
        for (std::size_t index = 0; index < names.size(); ++index) {
            exciter->set(names[index], params[index + 3]);
        }
        gen->add(exciter);
    }

    void loadEXAC2(CoreObject* parentObject, stringVec& tokens)
    {
        auto* gen = requireDyrGenerator(parentObject, tokens, "EXAC2");
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
        auto* gen = requireDyrGenerator(parentObject, tokens, "EXAC4");
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
        auto* gen = requireDyrGenerator(parentObject, tokens, "EXDC2");

        auto params = gmlc::utilities::str2vector(tokens, kNullVal);
        // EXDC2 uses the same DC2A schema as ESDC2A, including the PSS/E
        // Switch field, which PSS/E does not implement.
        if (params[14] != 0.0) {
            throw InvalidParameterValue("EXDC2 nonzero Switch is unsupported");
        }

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
        auto* gen = requireDyrGenerator(parentObject, tokens, "SEXS");

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
        auto* gen = requireDyrGenerator(parentObject, tokens, "TGOV1");

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
        auto* gen = requireDyrGenerator(parentObject, tokens, "HYGOV");

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

    void loadGovernorVariant(CoreObject* parentObject, stringVec& tokens, std::string_view model)
    {
        // These six names are not part of ANDES's psse-dyr.yaml. This is an
        // explicit GridDyn extension using the model's ANDES parameter order.
        static constexpr std::array<std::string_view, 8> tg2{
            "r", "pmax", "pmin", "dbl", "dbu", "dbc", "t1", "t2"};
        static constexpr std::array<std::string_view, 7> tgov{
            "r", "pmax", "pmin", "t1", "t2", "t3", "dt"};
        static constexpr std::array<std::string_view, 9> tgovdb{
            "r", "pmax", "pmin", "t1", "t2", "t3", "dt", "dbl", "dbu"};
        static constexpr std::array<std::string_view, 14> hygovdb{"r",
                                                                  "temporarydroop",
                                                                  "gmax",
                                                                  "gmin",
                                                                  "velm",
                                                                  "tf",
                                                                  "tr",
                                                                  "tg",
                                                                  "dturb",
                                                                  "qnl",
                                                                  "tw",
                                                                  "at",
                                                                  "dbl",
                                                                  "dbu"};
        static constexpr std::array<std::string_view, 14> hygov4{"rperm",
                                                                 "rtemp",
                                                                 "uo",
                                                                 "uc",
                                                                 "pmax",
                                                                 "pmin",
                                                                 "tp",
                                                                 "tg",
                                                                 "tr",
                                                                 "tw",
                                                                 "at",
                                                                 "dturb",
                                                                 "hdam",
                                                                 "qnl"};
        std::span<const std::string_view> names;
        if (model == "TG2") {
            names = tg2;
        } else if (model == "TGOV1N") {
            names = tgov;
        } else if (model == "TGOV1DB" || model == "TGOV1NDB") {
            names = tgovdb;
        } else if (model == "HYGOVDB") {
            names = hygovdb;
        } else {
            names = hygov4;
        }
        if (tokens.size() != names.size() + 3U) {
            throw InvalidParameterValue(std::string(model) +
                                        " DYR record has the wrong field count");
        }
        auto* gen = requireDyrGenerator(parentObject, tokens, model);
        const auto params = gmlc::utilities::str2vector(tokens, kNullVal);
        std::string factoryName(model);
        gmlc::utilities::makeLowerCase(factoryName);
        std::unique_ptr<Governor> governor(static_cast<Governor*>(
            CoreObjectFactory::instance()->createObject("governor", factoryName)));
        if (governor == nullptr) {
            throw InvalidParameterValue(std::string(model) + " governor factory registration");
        }
        for (std::size_t parameterIndex = 0; parameterIndex < names.size(); ++parameterIndex) {
            governor->set(names[parameterIndex], params[parameterIndex + 3]);
        }
        gen->add(governor.release());
    }

    void loadGGOV1(CoreObject* parentObject, stringVec& tokens)
    {
        if (tokens.size() != 38U) {
            throw InvalidParameterValue("GGOV1 DYR record must contain 38 fields");
        }
        auto* gen = requireDyrGenerator(parentObject, tokens, "GGOV1");
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
        auto* gen = requireDyrGenerator(parentObject, tokens, "GAST");
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

    void loadGPWSCC(CoreObject* parentObject, stringVec& tokens)
    {
        // PSLF GPWSCC order after the machine identifier is:
        // MWCap, Pmax, Pmin, R, Td, Tf, Tp, Velopen, Velclose, Kp, Kd,
        // Ki, Kg, Tturb, Aturb, Bturb, Tt, db1, Eps, db2, then Gv/Pgv 1..6.
        if (tokens.size() != 35U) {
            throw InvalidParameterValue("GPWSCC DYR record must contain 35 fields");
        }
        auto* gen = requireDyrGenerator(parentObject, tokens, "GPWSCC");
        const auto params = gmlc::utilities::str2vector(tokens, kNullVal);
        auto* model = static_cast<Governor*>(
            CoreObjectFactory::instance()->createObject("governor", "gpwscc"));
        if (model == nullptr) {
            throw InvalidParameterValue("GPWSCC governor factory registration");
        }
        static constexpr std::array<std::string_view, 32> names{
            "mwcap",    "gmax", "gmin", "r",    "td",  "tf",    "tp",    "velopen",
            "velclose", "kp",   "kd",   "ki",   "kg",  "tturb", "aturb", "bturb",
            "tt",       "db1",  "eps",  "db2",  "gv1", "pgv1",  "gv2",   "pgv2",
            "gv3",      "pgv3", "gv4",  "pgv4", "gv5", "pgv5",  "gv6",   "pgv6"};
        for (std::size_t index = 0; index < names.size(); ++index) {
            if (!std::isfinite(params[index + 3U]) || (params[index + 3U] == kNullVal)) {
                delete model;
                throw InvalidParameterValue("GPWSCC DYR record has a nonnumeric field");
            }
            model->set(names[index], params[index + 3U]);
        }
        // GPWSCC's MWCap is converted to the same machine base used by the
        // dynamic generator.  The static SAVE/EPC reader establishes MBASE
        // before the dynamic model is attached.
        model->set("mvabase", gen->get("mbase", units::MVAR));
        gen->add(model);
    }

    void loadIEEEG1(CoreObject* parentObject, stringVec& tokens)
    {
        if (tokens.size() != 25U) {
            throw InvalidParameterValue("IEEEG1 DYR record must contain 25 fields");
        }
        auto* primary =
            dynamic_cast<DynamicGenerator*>(requireDyrGenerator(parentObject, tokens, "IEEEG1"));
        if (primary == nullptr) {
            throw InvalidParameterValue("IEEEG1 requires a dynamic primary generator");
        }

        const auto params = gmlc::utilities::str2vector(tokens, kNullVal);
        const int secondBusId = std::stoi(tokens[3]);
        DynamicGenerator* secondary = nullptr;
        if (secondBusId != 0) {
            secondary = dynamic_cast<DynamicGenerator*>(
                findDyrGenerator(parentObject, tokens[3], tokens[4]));
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

    void loadIEEEG2(CoreObject* parentObject, stringVec& tokens)
    {
        // BUS, 'IEEEG2', ID, K, T1, T2, T3, PMAX, PMIN, T4
        if (tokens.size() != 10U) {
            throw InvalidParameterValue("IEEEG2 DYR record must contain 10 fields");
        }
        auto* generator =
            dynamic_cast<DynamicGenerator*>(requireDyrGenerator(parentObject, tokens, "IEEEG2"));
        if (generator == nullptr) {
            throw InvalidParameterValue("IEEEG2 requires a dynamic generator");
        }
        const auto params = gmlc::utilities::str2vector(tokens, kNullVal);
        std::unique_ptr<governors::GovernorIeeeG2> governor(
            dynamic_cast<governors::GovernorIeeeG2*>(
                CoreObjectFactory::instance()->createObject("governor", "ieeeg2")));
        if (governor == nullptr) {
            throw InvalidParameterValue("IEEEG2 factory registration");
        }
        static constexpr std::array<std::string_view, 7> names{
            "k", "t1", "t2", "t3", "pmax", "pmin", "t4"};
        for (std::size_t index = 0; index < names.size(); ++index) {
            if (!std::isfinite(params[index + 3U]) || (params[index + 3U] == kNullVal)) {
                throw InvalidParameterValue("IEEEG2 DYR record has a nonnumeric field");
            }
            governor->set(names[index], params[index + 3U]);
        }
        generator->add(governor.release());
    }

    void loadIEEEVC(CoreObject* parentObject, stringVec& tokens)
    {
        // BUS, 'IEEEVC', ID, RC, XC
        if (tokens.size() != 5U) {
            throw InvalidParameterValue("IEEEVC DYR record must contain 5 fields");
        }
        auto* generator =
            dynamic_cast<DynamicGenerator*>(requireDyrGenerator(parentObject, tokens, "IEEEVC"));
        if (generator == nullptr) {
            throw InvalidParameterValue("IEEEVC requires a dynamic generator");
        }
        const auto params = gmlc::utilities::str2vector(tokens, kNullVal);
        std::unique_ptr<voltagecompensators::VoltageCompensatorIeeeVC> compensator(
            dynamic_cast<voltagecompensators::VoltageCompensatorIeeeVC*>(
                CoreObjectFactory::instance()->createObject("voltagecompensator", "ieeevc")));
        if (compensator == nullptr) {
            throw InvalidParameterValue("IEEEVC factory registration");
        }
        if (!std::isfinite(params[3]) || !std::isfinite(params[4]) || params[3] == kNullVal ||
            params[4] == kNullVal) {
            throw InvalidParameterValue("IEEEVC DYR record has a nonnumeric field");
        }
        compensator->set("rc", params[3]);
        compensator->set("xc", params[4]);
        generator->add(compensator.release());
    }

    void loadIEESGO(CoreObject* parentObject, stringVec& tokens)
    {
        // BUS, 'IEESGO', ID, T1, T2, T3, T4, T5, T6, K1, K2, K3, PMAX, PMIN /
        if (tokens.size() != 14U) {
            throw InvalidParameterValue("IEESGO DYR record must contain 14 fields");
        }
        auto* gen = requireDyrGenerator(parentObject, tokens, "IEESGO");
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

    void loadST2CUT(CoreObject* parentObject, stringVec& tokens, bool zeroGain)
    {
        if (tokens.size() != 23U) {
            throw InvalidParameterValue("ST2CUT DYR record must contain 23 fields");
        }
        auto* generator =
            dynamic_cast<DynamicGenerator*>(requireDyrGenerator(parentObject, tokens, "ST2CUT"));
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
        if (zeroGain) {
            stabilizer->set("k1", 0.0);
            stabilizer->set("k2", 0.0);
        }
        generator->add(stabilizer);
    }

    void loadIEEEST(CoreObject* parentObject, stringVec& tokens, bool zeroGain)
    {
        if (tokens.size() != 22U) {
            throw InvalidParameterValue("IEEEST DYR record must contain 22 fields");
        }
        auto* generator =
            dynamic_cast<DynamicGenerator*>(requireDyrGenerator(parentObject, tokens, "IEEEST"));
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
        if (zeroGain) {
            stabilizer->set("ks", 0.0);
        }
        generator->add(stabilizer);
    }
}  // namespace

}  // namespace griddyn
