/*
 * Copyright (c) 2014-2026, Lawrence Livermore National Security
 * See the top-level NOTICE for additional details. All rights reserved.
 * SPDX-License-Identifier: BSD-3-Clause
 */

#include "LoadTemplateManager.h"
#include "ReaderInfo.h"
#include "core/CoreExceptions.h"
#include "core/CoreObject.h"
#include "core/coreDefinitions.hpp"
#include "fileInput.h"
#include "gmlc/utilities/stringConversion.h"
#include "gmlc/utilities/stringOps.h"
#include "gridDynReadDyrModels.h"
#include "griddyn/GridBus.h"
#include "griddyn/GridDynSimulation.h"
#include "griddyn/loads/LoadTemplateAdapters.h"
#include "loadModelReaderHelper.h"
#include <algorithm>
#include <array>
#include <charconv>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <format>
#include <fstream>
#include <limits>
#include <map>
#include <numbers>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace griddyn {
namespace {
    struct UnsupportedDydModelSummary {
        std::size_t mCount = 0;
        std::size_t mFirstLine = 0;
        std::string mFirstBus;
        std::string mFirstMachine;
    };

    struct IgnoredDydParameterSummary {
        std::size_t mCount = 0;
        std::size_t mFirstLine = 0;
        std::string mFirstBus;
        std::string mFirstMachine;
        std::string mFirstValue;
    };

    bool isZeroDydParameter(std::string_view value)
    {
        const std::string valueString{value};
        char* parseEnd = nullptr;
        const double parsedValue = std::strtod(valueString.c_str(), &parseEnd);
        return (parseEnd != valueString.c_str()) &&
            (parseEnd == valueString.c_str() + valueString.size()) && std::isfinite(parsedValue) &&
            (parsedValue == 0.0);
    }

    std::string formatDydNumber(double value)
    {
        return std::format("{:.17g}", value);
    }

    void addIgnoredDydParameter(IgnoredDydParameterSummary& summary,
                                const stringVec& lineTokens,
                                std::size_t recordLineNumber,
                                std::string_view value)
    {
        ++summary.mCount;
        if (summary.mFirstLine == 0U) {
            summary.mFirstLine = recordLineNumber;
            summary.mFirstBus = lineTokens.empty() ? "<missing>" : lineTokens[0];
            summary.mFirstMachine = (lineTokens.size() > 2U) ? lineTokens[2] : "<missing>";
            summary.mFirstValue = value;
        }
    }

    bool isDydDirectModel(std::string_view modelName)
    {
        static constexpr std::array directModels{
            "gencls", "genrou", "genroe", "gensae", "gensal",  "esdc1a",  "esdc2a", "ieeet1",
            "ieeet3", "ieeex1", "ac7b",   "ac8b",   "esst1a",  "esst2a",  "esst3a", "esst4b",
            "expic1", "scrx",   "esac6a", "exst1",  "exac1",   "esac1a",  "exac2",  "exac4",
            "exdc2",  "tgov1",  "hygov",  "gast",   "ggov1",   "ieeeg1",  "ieesgo", "ieeest",
            "sexs",   "regca1", "regcp1", "reeca1", "reeca1e", "reeca1g", "reecb1", "repca1",
            "regcv1", "regcv2", "regf1",  "regf2",  "regf3",   "wtdta1",  "wtara1", "wtpta1",
            "wttqa1", "wtds",   "wt3g1",  "wt3e1",  "wt4g1",   "wt4e1",   "epcgen", "gpwscc"};
        const auto normalized = gmlc::utilities::convertToLowerCase(modelName);
        return std::ranges::any_of(directModels, [normalized](const char* directModel) {
            return normalized == directModel;
        });
    }

    std::optional<LoadTemplateScope> getDydWSCCLoadScope(std::string_view modelName)
    {
        if (modelName == "wlwscc") {
            return LoadTemplateScope::System;
        }
        if (modelName == "alwscc") {
            return LoadTemplateScope::Area;
        }
        if (modelName == "zlwscc") {
            return LoadTemplateScope::Zone;
        }
        if (modelName == "blwscc") {
            return LoadTemplateScope::Bus;
        }
        return std::nullopt;
    }

    bool isDydIgnoredNonessentialModel(std::string_view modelName)
    {
        static constexpr std::array ignoredModels{"fmetr", "vmetr", "lsdt1"};
        return std::ranges::any_of(ignoredModels, [modelName](const char* ignoredModel) {
            return modelName == ignoredModel;
        });
    }

    WSCCParameters parseWSCCParameters(const stringVec& payload, std::string_view modelName)
    {
        if ((payload.size() != 10U) && (payload.size() != 11U)) {
            throw InvalidParameterValue(std::string{modelName} +
                                        " requires 10 documented parameters and an optional "
                                        "11th VMIN parameter");
        }
        std::array<double, 11> values{};
        for (std::size_t index = 0; index < payload.size(); ++index) {
            char* parseEnd = nullptr;
            values[index] = std::strtod(payload[index].c_str(), &parseEnd);
            if ((parseEnd == payload[index].c_str()) ||
                (parseEnd != payload[index].c_str() + payload[index].size()) ||
                !std::isfinite(values[index])) {
                throw InvalidParameterValue(std::string{modelName} + " parameter " +
                                            std::to_string(index + 1U) + " is not a finite number");
            }
        }
        return WSCCParameters{.p1 = values[0],
                              .q1 = values[1],
                              .p2 = values[2],
                              .q2 = values[3],
                              .p3 = values[4],
                              .q3 = values[5],
                              .p4 = values[6],
                              .q4 = values[7],
                              .lpd = values[8],
                              .lqd = values[9],
                              .vmin = values[10]};
    }

    index_t parseDydSelector(std::string_view value, std::string_view modelName)
    {
        if (value.empty() || value.starts_with('-')) {
            throw InvalidParameterValue(std::string{modelName} +
                                        " selector must be a nonnegative integer");
        }
        std::uint64_t parsedValue = 0;
        const auto result = std::from_chars(value.data(), value.data() + value.size(), parsedValue);
        if ((result.ec != std::errc{}) || (result.ptr != value.data() + value.size()) ||
            (parsedValue > std::numeric_limits<index_t>::max())) {
            throw InvalidParameterValue(std::string{modelName} +
                                        " selector must be a nonnegative integer");
        }
        return static_cast<index_t>(parsedValue);
    }

    bool isDydSectionHeader(std::string_view line)
    {
        const auto normalized = gmlc::utilities::convertToLowerCase(std::string{line});
        return normalized == "lodrep" || normalized == "models" || normalized == "netting" ||
            normalized == "end";
    }

    bool isDydOutOfServiceModelsHeader(std::string_view line)
    {
        const auto normalized = gmlc::utilities::convertToLowerCase(std::string{line});
        return normalized == "list of out of service models";
    }

    bool parseDydHeader(const std::string& line, std::size_t& colon, stringVec& header)
    {
        colon = line.find(':');
        if (colon == std::string::npos) {
            return false;
        }
        header = gmlc::utilities::stringOps::splitlineQuotes(
            line.substr(0, colon),
            " \t\n,",
            gmlc::utilities::stringOps::default_quote_chars,
            gmlc::utilities::stringOps::delimiter_compression::on);
        return header.size() >= 5U;
    }

    std::string canonicalDydModelName(std::string_view modelName)
    {
        auto normalized = gmlc::utilities::convertToLowerCase(modelName);
        if (normalized == "esac7b") {
            return "ac7b";
        }
        if (normalized == "regc_a") {
            return "regca1";
        }
        if (normalized == "reec_b") {
            return "reecb1";
        }
        if (normalized == "reec_a") {
            return "reeca1";
        }
        if (normalized == "repc_a") {
            return "repca1";
        }
        if (normalized == "wtgt_a") {
            return "wtdta1";
        }
        if (normalized == "wtga_a" || normalized == "wtgar_a") {
            return "wtara1";
        }
        if (normalized == "wtgp_a" || normalized == "wtgpt_a") {
            return "wtpta1";
        }
        if (normalized == "wtgq_a" || normalized == "wtgtrq_a") {
            return "wttqa1";
        }
        if (normalized == "wt3e") {
            return "wt3e1";
        }
        if (normalized == "wt3p") {
            return "wtpta1";
        }
        return normalized;
    }

    // Convert the positional PSLF fields to the order expected by the
    // existing PSS/E DYR loaders.  PSLF writes a number of model-specific
    // fields after the compatible prefix; those fields are deliberately
    // ignored only where the schema comments in the DYD identify them.
    bool prepareDydPayload(std::string_view sourceModelName,
                           std::string_view modelName,
                           const stringVec& sourcePayload,
                           stringVec& payload)
    {
        const auto source = gmlc::utilities::convertToLowerCase(sourceModelName);

        if (source == "esac7b") {
            // PSLF ESAC7B:
            // Tr Kpr Kir Kdr Tdr Vrmax Vrmin Kpa Kia Vamax Vamin Kp Kl Te
            // Vfemax Vemin Ke Kc Kd Kf1 Kf2 Kf3 Tf E1 SE1 E2 SE2 spdmlt
            // GridDyn's AC7B loader uses the IEEE/PSS/E order and has no
            // spdmlt input.
            static constexpr std::array<std::size_t, 27> ac7bOrder{0U,  1U,  2U,  3U,  4U,  5U,
                                                                   6U,  7U,  8U,  9U,  10U, 11U,
                                                                   12U, 13U, 17U, 18U, 16U, 19U,
                                                                   20U, 21U, 22U, 15U, 14U, 23U,
                                                                   24U, 25U, 26U};
            if (sourcePayload.size() != 28U) {
                return false;
            }
            payload.reserve(ac7bOrder.size());
            for (const auto index : ac7bOrder) {
                payload.push_back(sourcePayload[index]);
            }
            return true;
        }

        if (source == "esst4b") {
            // PSLF ESST4B has Angp between Kp and Ki and carries Vgmax at
            // the end.  GridDyn's Thetap corresponds to Angp; Vgmax is not
            // part of its ESST4B implementation.
            static constexpr std::array<std::size_t, 17> esst4bOrder{
                0U, 1U, 2U, 4U, 5U, 3U, 6U, 7U, 8U, 9U, 10U, 11U, 13U, 16U, 14U, 15U, 12U};
            if (sourcePayload.size() != 18U) {
                return false;
            }
            payload.reserve(esst4bOrder.size());
            for (const auto index : esst4bOrder) {
                payload.push_back(sourcePayload[index]);
            }
            return true;
        }

        if (source == "reec_b") {
            // PSLF REEC_B starts with MVAB, whereas the DYR REECB1 record
            // starts with BUSR.  MVAB is already represented by the static
            // generator/EPC data, and GridDyn currently supports only local
            // control, so synthesize BUSR=0 and move the four flags to the
            // DYR positions.
            if (sourcePayload.size() != 30U) {
                return false;
            }
            payload.emplace_back("0");
            for (std::size_t index = 26U; index < 30U; ++index) {
                payload.push_back(sourcePayload[index]);
            }
            for (std::size_t index = 1U; index < 26U; ++index) {
                payload.push_back(sourcePayload[index]);
            }
            return true;
        }

        if (source == "regc_a") {
            // PSLF REGC_A:
            // LVPLSW RRPWR BRKPT ZEROX LVPL1 VTMAX LVPNT1 LVPNT0 QMIN ACCEL
            // TG TFLTR IQRMAX IQRMIN XE
            // GridDyn's REGCA1 adds TG earlier in the record and uses IOLIM,
            // KHV, and ACCEL in the final positions.  QMIN is the closest
            // static current limit field, while PSLF XE has no REGCA1
            // equivalent; use the model default for KHV instead of shifting
            // either field into the wrong dynamic parameter.
            if (sourcePayload.size() != 15U) {
                return false;
            }
            static constexpr std::array<std::size_t, 11> regcaOrder{
                0U, 10U, 1U, 2U, 3U, 4U, 5U, 6U, 7U, 8U, 11U};
            payload.reserve(15U);
            for (const auto index : regcaOrder) {
                payload.push_back(sourcePayload[index]);
            }
            payload.emplace_back("0");
            payload.push_back(sourcePayload[12U]);
            payload.push_back(sourcePayload[13U]);
            payload.push_back(sourcePayload[9U]);
            return true;
        }

        if (source == "reec_a") {
            // PSLF REEC_A writes MVAB first, then the control parameters,
            // followed by the five control flags and the P/Q voltage curves.
            // REECA1's shared DYR loader expects BUSR, all five flags, then
            // the control parameters and curves.  A zero MVAB means the
            // generator's static machine base, which is GridDyn's convention.
            if (sourcePayload.size() != 51U || !isZeroDydParameter(sourcePayload.front())) {
                return false;
            }
            payload.reserve(51U);
            payload.emplace_back("0");  // local BUSR
            payload.insert(payload.end(), sourcePayload.begin() + 30U, sourcePayload.begin() + 35U);
            payload.insert(payload.end(), sourcePayload.begin() + 1U, sourcePayload.begin() + 30U);
            payload.insert(payload.end(), sourcePayload.begin() + 35U, sourcePayload.end());
            return true;
        }

        if (source == "repc_a") {
            // PSLF REPC_A starts with MVAB.  Its single reactive deadband is
            // represented by the lower and upper deadbands of REPCA1.
            if (sourcePayload.size() != 30U || !isZeroDydParameter(sourcePayload.front())) {
                return false;
            }
            payload.reserve(34U);
            // REPCA1 has four leading fields for remote-bus/measurement
            // options.  The local PSLF form uses zero for all four.
            payload.insert(payload.end(), 4U, "0");
            payload.push_back(sourcePayload[11U]);  // VcompFlag
            payload.push_back(sourcePayload[6U]);  // RefFlag
            payload.push_back(sourcePayload[29U]);  // FreqFlag
            payload.insert(payload.end(),
                           sourcePayload.begin() + 1U,
                           sourcePayload.begin() + 6U);  // Tfltr through Tfv
            payload.insert(payload.end(),
                           sourcePayload.begin() + 7U,
                           sourcePayload.begin() + 14U);  // Vfrz through Dbd
            payload.push_back(sourcePayload[14U]);  // Dbd upper limit
            payload.insert(payload.end(),
                           sourcePayload.begin() + 15U,
                           sourcePayload.begin() + 29U);  // remaining controller values
            return true;
        }

        if (source == "wtgt_a") {
            // WTGT_A's Ht/Hg/Kshaft form is equivalent to the existing
            // two-mass WTDTA1 form after deriving total inertia, turbine
            // inertia fraction, and the shaft-frequency parameter used by
            // WTDTA1. The leading MVAB field may be zero (machine base) or
            // explicit; preserve an explicit base so the shared loader can
            // convert the inertia and damping to the static generator base.
            if ((sourcePayload.size() != 5U && sourcePayload.size() != 6U)) {
                return false;
            }
            const auto params = gmlc::utilities::str2vector(sourcePayload, kNullVal);
            if (std::any_of(params.begin(), params.end(), [](double value) {
                    return !std::isfinite(value) || value == kNullVal;
                })) {
                return false;
            }
            const double sourceBase = params[0];
            const double ht = params[1];
            const double hg = params[2];
            const double totalInertia = ht + hg;
            const double w0 = (params.size() > 5U) ? params[5] : 1.0;
            if (!std::isfinite(sourceBase) || !std::isfinite(ht) || !std::isfinite(hg) ||
                !std::isfinite(params[3]) || !std::isfinite(params[4]) || !std::isfinite(w0) ||
                ht <= 0.0 || hg < 0.0 || !std::isfinite(totalInertia) || totalInertia <= 0.0 ||
                params[3] < 0.0 || w0 <= 0.0) {
                return false;
            }
            if (hg == 0.0 || params[4] <= 0.0) {
                // PowerWorld collapses a zero or negative shaft resonance to
                // its single-mass form. loadDyd selects WTDS for these inputs.
                payload = sourcePayload;
                return true;
            }
            const double htFraction = ht / totalInertia;
            const double freq1 =
                std::sqrt(params[4] * totalInertia / (2.0 * ht * hg)) / (2.0 * std::numbers::pi);
            if (!std::isfinite(htFraction) || !std::isfinite(freq1) || htFraction <= 0.0 ||
                htFraction >= 1.0 || freq1 <= 0.0) {
                return false;
            }
            payload = {formatDydNumber(totalInertia),
                       "0",
                       formatDydNumber(htFraction),
                       formatDydNumber(freq1),
                       formatDydNumber(params[3]),
                       formatDydNumber(w0)};
            if (sourceBase > 0.0) {
                payload.push_back(formatDydNumber(sourceBase));
            }
            return true;
        }

        if (source == "wtga_a" || source == "wtgar_a") {
            // The leading PSLF MVAB field is zero for machine-base data.
            if (sourcePayload.size() != 3U || !isZeroDydParameter(sourcePayload.front())) {
                return false;
            }
            payload.assign(sourcePayload.begin() + 1U, sourcePayload.end());
            return true;
        }

        if (source == "wtgp_a" || source == "wtgpt_a") {
            // WTGPT_A and WTPTA1 share the same ten control parameters.
            if (sourcePayload.size() != 11U || !isZeroDydParameter(sourcePayload.front())) {
                return false;
            }
            payload.assign(sourcePayload.begin() + 1U, sourcePayload.end());
            return true;
        }

        if (source == "wtgq_a" || source == "wtgtrq_a") {
            // PSLF DYD order is MVAB, Kip, Kpp, Tp, Twref, Temax, Temin,
            // four power/speed pairs, and Tflag. WTTQA1 puts Tflag first and
            // Kpp before Kip. The optional leading MVAB is only equivalent to
            // the machine base when zero, as used by the supplied DYD cases.
            const bool hasModelBase = sourcePayload.size() == 16U;
            if ((!hasModelBase && sourcePayload.size() != 15U) ||
                (hasModelBase && !isZeroDydParameter(sourcePayload.front()))) {
                return false;
            }
            const auto offset = hasModelBase ? 1U : 0U;
            payload.reserve(15U);
            payload.push_back(sourcePayload[offset + 14U]);  // Tflag
            payload.push_back(sourcePayload[offset + 2U]);  // Kpp
            payload.push_back(sourcePayload[offset + 1U]);  // Kip
            payload.insert(payload.end(),
                           sourcePayload.begin() + static_cast<std::ptrdiff_t>(offset + 3U),
                           sourcePayload.begin() + static_cast<std::ptrdiff_t>(offset + 15U));
            return true;
        }

        if (source == "wt3e") {
            // PSLF WT3E and PSS/E WT3E1 use the same core controls but order
            // their fields differently. The final PSLF MWCap is not a WT3E1
            // parameter; the static generator supplies the machine base.
            // WT3E1 also has four reserved PSS/E fields before the controls.
            if (sourcePayload.size() != 33U) {
                return false;
            }
            const auto sourceValues = gmlc::utilities::str2vector(sourcePayload, kNullVal);
            if (std::any_of(sourceValues.begin(), sourceValues.end(), [](double value) {
                    return !std::isfinite(value) || value == kNullVal;
                })) {
                return false;
            }
            payload.reserve(37U);
            payload.push_back(sourcePayload[0U]);  // VARFLG
            payload.push_back(sourcePayload[1U]);  // VLTFLG
            payload.insert(payload.end(), 4U, "0");
            static constexpr std::array<std::size_t, 30> wt3eOrder{31U, 29U, 28U, 25U, 2U,  3U,
                                                                   4U,  6U,  7U,  18U, 19U, 9U,
                                                                   26U, 8U,  5U,  16U, 21U, 20U,
                                                                   17U, 23U, 22U, 30U, 24U, 27U,
                                                                   10U, 11U, 12U, 13U, 14U, 15U};
            for (std::size_t index = 0; index < wt3eOrder.size(); ++index) {
                payload.push_back(sourcePayload[wt3eOrder[index]]);
                if (index == 13U) {
                    payload.push_back(formatDydNumber(-sourceValues[8U]));  // RPMIN
                }
            }
            return true;
        }

        if (source == "wt3p") {
            // WT3P is the first-generation equivalent of WTPTA1 without its
            // Kcc cross term. Its fixed Pset is carried as an additional
            // WTPTA1 parameter so it is not silently discarded.
            if (sourcePayload.size() != 9U) {
                return false;
            }
            const auto sourceValues = gmlc::utilities::str2vector(sourcePayload, kNullVal);
            if (std::any_of(sourceValues.begin(), sourceValues.end(), [](double value) {
                    return !std::isfinite(value) || value == kNullVal;
                })) {
                return false;
            }
            payload = {sourcePayload[1U],
                       sourcePayload[0U],
                       sourcePayload[3U],
                       sourcePayload[2U],
                       "0",
                       sourcePayload[7U],
                       sourcePayload[4U],
                       sourcePayload[5U],
                       sourcePayload[6U],
                       formatDydNumber(-sourceValues[6U]),
                       sourcePayload[8U]};
            return true;
        }

        if (source == "wt3t") {
            // WT3T combines aerodynamics and the shaft. loadDyd splits it into
            // the existing WTARA1 and WTDS/WTDTA1 components to retain the
            // renewable-component role boundaries.
            if (sourcePayload.size() != 8U) {
                return false;
            }
            payload = sourcePayload;
            return true;
        }

        if (source == "esac1a") {
            // PSLF ESAC1A order is TR, TB, TC, KA, TA, VAMAX, VAMIN, TE,
            // KF, TF, KC, KD, KE, E1, SE1, E2, SE2, VRMAX, VRMIN, Spdmlt.
            // The shared PSS/E DYR loader expects VRMAX/VRMIN before TE and
            // VAMAX/VAMIN at the end. Spdmlt is not part of the PSS/E record.
            static constexpr std::array<std::size_t, 19> esac1aOrder{0U,
                                                                     1U,
                                                                     2U,
                                                                     3U,
                                                                     4U,
                                                                     17U,
                                                                     18U,
                                                                     7U,
                                                                     8U,
                                                                     9U,
                                                                     10U,
                                                                     11U,
                                                                     12U,
                                                                     13U,
                                                                     14U,
                                                                     15U,
                                                                     16U,
                                                                     5U,
                                                                     6U};
            if (sourcePayload.size() < esac1aOrder.size()) {
                return false;
            }
            payload.reserve(esac1aOrder.size());
            for (const auto index : esac1aOrder) {
                payload.push_back(sourcePayload[index]);
            }
            return true;
        }

        std::size_t payloadLimit = 0U;
        // GENSAL and HYGOV consume 12-field prefixes. Extended HYGOV records
        // append options (trip, deadband, curves, and blade control) that
        // GridDyn does not currently implement.
        if ((modelName == "gensal") || (modelName == "hygov")) {
            payloadLimit = 12U;
        } else if (modelName == "genrou") {
            payloadLimit = 14U;
        } else if (modelName == "exac1") {
            payloadLimit = 17U;
        } else if (modelName == "gast") {
            payloadLimit = 9U;
        } else if (modelName == "ggov1") {
            payloadLimit = 35U;
        } else if (modelName == "ieeeg1") {
            payloadLimit = 20U;
        } else if (modelName == "ieeest") {
            // PSLF IEEEST appends Tdelay after the 19 PSS/E parameters;
            // StabilizerIEEEST does not currently implement that delay. The
            // DYD reader warns when this trailing parameter is nonzero.
            payloadLimit = 19U;
        } else if (modelName == "gpwscc") {
            payloadLimit = 32U;
        } else if (modelName == "regca1") {
            payloadLimit = 15U;
        }
        if ((payloadLimit != 0U) && (sourcePayload.size() < payloadLimit)) {
            return false;
        }
        const auto end = (payloadLimit == 0U) ? sourcePayload.size() : payloadLimit;
        payload.assign(sourcePayload.begin(), sourcePayload.begin() + end);
        return true;
    }

    bool prepareEpcgenPayload(const stringVec& rawPayload, stringVec& payload)
    {
        static constexpr std::array<std::string_view, 12> fieldNames{"rsrc",
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
                                                                     "pref"};
        static constexpr std::array<std::string_view, 12> defaults{"0.0",
                                                                   "0.0",
                                                                   "0.1",
                                                                   "60.1",
                                                                   "59.9",
                                                                   "1.67",
                                                                   "1.67",
                                                                   "0.7",
                                                                   "1.0",
                                                                   "1.0",
                                                                   "-1.0",
                                                                   "0.0"};
        std::array<std::string, fieldNames.size()> values;
        for (std::size_t index = 0; index < fieldNames.size(); ++index) {
            values[index] = defaults[index];
        }

        bool sawNamedField = false;
        for (std::size_t index = 0; index < rawPayload.size(); ++index) {
            const auto token = gmlc::utilities::stringOps::removeQuotes(rawPayload[index]);
            const auto normalized = gmlc::utilities::convertToLowerCase(token);
            std::size_t fieldIndex = 0U;
            while ((fieldIndex < fieldNames.size()) && (fieldNames[fieldIndex] != normalized)) {
                ++fieldIndex;
            }
            if (fieldIndex < fieldNames.size()) {
                if (index + 1U >= rawPayload.size()) {
                    return false;
                }
                const auto value = gmlc::utilities::stringOps::removeQuotes(rawPayload[++index]);
                if (value.empty() || value.contains('=')) {
                    return false;
                }
                values[fieldIndex] = value;
                sawNamedField = true;
                continue;
            }

            // #4, the model library name, and the library version identify
            // the PSLF model schema; they are not EPCGEN dynamic values.
            if (token.empty() || token.starts_with('#') || token == "epsbes.p" ||
                token == "convepc.p") {
                continue;
            }
            if (!sawNamedField && token.find_first_not_of("0123456789") == std::string::npos) {
                continue;
            }
            // A v7 EPCGEN record is named. Reject an unexpected token rather
            // than treating it as a positional value with the wrong meaning.
            return false;
        }
        if (!sawNamedField) {
            return false;
        }
        payload.assign(values.begin(), values.end());
        return true;
    }

    void addUnsupportedModel(std::map<std::string, UnsupportedDydModelSummary>& unsupportedModels,
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
}  // namespace

void loadDyd(CoreObject* parentObject,
             const std::string& fileName,
             const BasicReaderInfo& /*readerOptions*/)
{
    const auto* simulation = dynamic_cast<const GridDynSimulation*>(parentObject->getRoot());
    const bool disableStabilizers =
        (simulation != nullptr) && simulation->isFlagSet(DISABLE_STABILIZERS_FOR_DIAGNOSTICS);
    count_t zeroGainStabilizers = 0;
    std::ifstream file(fileName.c_str(), std::ios::in);
    std::string line;
    std::size_t lineNumber = 0;
    std::map<std::string, UnsupportedDydModelSummary> unsupportedModels;
    std::map<std::string, UnsupportedDydModelSummary> ignoredNonessentialModels;
    UnsupportedDydModelSummary singleMassWtgtFallbacks;
    IgnoredDydParameterSummary ignoredIeeestTdelay;
    IgnoredDydParameterSummary ignoredEsac1aSpdmlt;
    LoadTemplateManager wsccLoadTemplates;
    bool hasWsccLoadTemplates = false;
    bool inOutOfServiceModels = false;

    if (!file.is_open()) {
        parentObject->log(parentObject, PrintLevel::ERROR, "Unable to open file " + fileName);
        return;
    }

    warnIfStaticNetworkMissing(parentObject, "DYD", fileName);

    std::vector<std::pair<std::size_t, std::string>> inputLines;
    while (std::getline(file, line)) {
        ++lineNumber;
        gmlc::utilities::stringOps::trimString(line);
        if (!line.empty() && !line.starts_with('#')) {
            inputLines.emplace_back(lineNumber, line);
        }
    }

    for (std::size_t lineIndex = 0U; lineIndex < inputLines.size(); ++lineIndex) {
        const auto recordLineNumber = inputLines[lineIndex].first;
        const auto& firstLine = inputLines[lineIndex].second;

        if (isDydOutOfServiceModelsHeader(firstLine)) {
            inOutOfServiceModels = true;
            continue;
        }
        if (isDydSectionHeader(firstLine)) {
            inOutOfServiceModels = false;
            continue;
        }
        if (inOutOfServiceModels) {
            // These records describe dynamic models that are explicitly out
            // of service. Their abbreviated payloads (commonly just ": 0")
            // are status markers, not model parameter sets to instantiate.
            continue;
        }

        // A PSLF DYD model record has a colon separating its identifying fields
        // from the parameter payload. Section headers and NETTING records do not.
        std::size_t colon = 0U;
        stringVec header;
        if (!parseDydHeader(firstLine, colon, header)) {
            continue;
        }

        // DYD continuation lines are not required to end with a slash. In
        // fact, this file uses slash-separated groups on several lines and
        // leaves the final group unterminated. Collect through the next model
        // header or section header, then tokenize the logical record once.
        std::string payloadText = firstLine.substr(colon + 1U);
        std::size_t nextLineIndex = lineIndex + 1U;
        for (; nextLineIndex < inputLines.size(); ++nextLineIndex) {
            std::size_t nextColon = 0U;
            stringVec nextHeader;
            if (parseDydHeader(inputLines[nextLineIndex].second, nextColon, nextHeader) ||
                isDydSectionHeader(inputLines[nextLineIndex].second)) {
                break;
            }
            payloadText.push_back(' ');
            payloadText.append(inputLines[nextLineIndex].second);
        }
        lineIndex = nextLineIndex - 1U;

        const auto sourceModelName = gmlc::utilities::convertToLowerCase(
            gmlc::utilities::stringOps::removeQuotes(header[0]));
        const auto canonicalModelName = canonicalDydModelName(sourceModelName);
        const auto displayModelName = gmlc::utilities::convertToUpperCase(sourceModelName);
        stringVec lineTokens{gmlc::utilities::stringOps::removeQuotes(header[1]),
                             "'" + gmlc::utilities::convertToUpperCase(canonicalModelName) + "'",
                             gmlc::utilities::stringOps::removeQuotes(header[4])};
        if (isDydIgnoredNonessentialModel(sourceModelName)) {
            addUnsupportedModel(ignoredNonessentialModels,
                                displayModelName,
                                lineTokens,
                                recordLineNumber);
            continue;
        }

        const auto rawPayload = gmlc::utilities::stringOps::splitlineQuotes(
            payloadText,
            " \t\n,/",
            gmlc::utilities::stringOps::default_quote_chars,
            gmlc::utilities::stringOps::delimiter_compression::on);
        stringVec positionalPayload;
        stringVec normalizedPayload;
        std::string gpwsccMWCap;
        bool hasUnsupportedField = false;
        bool payloadIsSupported = false;
        if (sourceModelName == "epcgen") {
            payloadIsSupported = prepareEpcgenPayload(rawPayload, normalizedPayload);
        } else {
            for (const auto& token : rawPayload) {
                if (token.empty() || token == "/" || token.starts_with('#')) {
                    continue;
                }
                const auto lowerToken = gmlc::utilities::convertToLowerCase(token);
                // Machine bases are supplied by the static network reader. PSLF
                // uses both named MVA and MW-capacity fields across model families.
                if (lowerToken.starts_with("mwcap=")) {
                    if (canonicalModelName == "gpwscc") {
                        if (!gpwsccMWCap.empty()) {
                            hasUnsupportedField = true;
                            break;
                        }
                        gpwsccMWCap = token.substr(6);
                        if (gpwsccMWCap.empty()) {
                            hasUnsupportedField = true;
                            break;
                        }
                    }
                    continue;
                }
                if (lowerToken.starts_with("mva=")) {
                    continue;
                }
                // Other named fields require a model-specific adapter and must not
                // be accidentally interpreted as positional numeric parameters.
                if (token.contains('=')) {
                    hasUnsupportedField = true;
                    break;
                }
                positionalPayload.push_back(token);
            }
            if ((sourceModelName == "ieeest") && (positionalPayload.size() > 19U) &&
                !isZeroDydParameter(positionalPayload[19U])) {
                addIgnoredDydParameter(ignoredIeeestTdelay,
                                       lineTokens,
                                       recordLineNumber,
                                       positionalPayload[19U]);
            }
            if ((sourceModelName == "esac1a") && (positionalPayload.size() > 19U) &&
                !isZeroDydParameter(positionalPayload[19U])) {
                addIgnoredDydParameter(ignoredEsac1aSpdmlt,
                                       lineTokens,
                                       recordLineNumber,
                                       positionalPayload[19U]);
            }
            if ((canonicalModelName == "gpwscc") && !hasUnsupportedField) {
                if (gpwsccMWCap.empty()) {
                    hasUnsupportedField = true;
                } else {
                    positionalPayload.insert(positionalPayload.begin(), gpwsccMWCap);
                }
            }
            payloadIsSupported = !hasUnsupportedField &&
                prepareDydPayload(sourceModelName,
                                  canonicalModelName,
                                  positionalPayload,
                                  normalizedPayload);
            if (((sourceModelName == "wt3e") || (sourceModelName == "repc_a")) &&
                (header.size() > 5U)) {
                // The local implementations do not yet represent PSLF remote
                // bus/branch measurements carried in the DYD header.
                payloadIsSupported = false;
            }
        }
        if (const auto wsccScope = getDydWSCCLoadScope(sourceModelName)) {
            try {
                if (hasUnsupportedField) {
                    throw InvalidParameterValue(displayModelName +
                                                " contains an unsupported named parameter");
                }
                auto* mutableSimulation = dynamic_cast<GridDynSimulation*>(parentObject->getRoot());
                if (mutableSimulation == nullptr) {
                    throw InvalidParameterValue(displayModelName +
                                                " requires a GridDynSimulation root");
                }
                const auto parameters = parseWSCCParameters(positionalPayload, displayModelName);
                index_t selector = 0;
                if (*wsccScope != LoadTemplateScope::System) {
                    selector = parseDydSelector(header[1], displayModelName);
                    if ((*wsccScope == LoadTemplateScope::Area) &&
                        (mutableSimulation->findByUserID("area", selector) == nullptr)) {
                        throw InvalidParameterValue(displayModelName + " area selector " +
                                                    std::to_string(selector) + " was not found");
                    }
                    if (*wsccScope == LoadTemplateScope::Zone) {
                        if (selector > static_cast<index_t>(std::numeric_limits<int>::max())) {
                            throw InvalidParameterValue(
                                displayModelName + " zone selector exceeds the supported range");
                        }
                        std::vector<GridBus*> buses;
                        mutableSimulation->getBusVector(buses);
                        const auto zone = static_cast<int>(selector);
                        if (std::ranges::none_of(buses, [zone](const GridBus* bus) {
                                return bus->zone == zone;
                            })) {
                            throw InvalidParameterValue(displayModelName + " zone selector " +
                                                        std::to_string(selector) +
                                                        " was not found");
                        }
                    }
                    if ((*wsccScope == LoadTemplateScope::Bus) &&
                        (mutableSimulation->findByUserID("bus", selector) == nullptr)) {
                        throw InvalidParameterValue(displayModelName + " bus selector " +
                                                    std::to_string(selector) + " was not found");
                    }
                }
                wsccLoadTemplates.setTemplate(*wsccScope,
                                              selector,
                                              loads::makeWSCCLoadTemplate(parameters));
                hasWsccLoadTemplates = true;
            }
            catch (const InvalidParameterValue& error) {
                std::string message{fileName};
                message.push_back(':');
                message.append(std::to_string(recordLineNumber));
                message.append(" ").append(displayModelName).append(" selector ");
                message.append(header.size() > 1U ? header[1] : "<missing>");
                message.append(": ").append(error.what());
                throw InvalidParameterValue(message);
            }
            continue;
        }

        if (sourceModelName == "wtgt_a") {
            if (!payloadIsSupported) {
                addUnsupportedModel(unsupportedModels,
                                    displayModelName,
                                    lineTokens,
                                    recordLineNumber);
                continue;
            }
            try {
                const auto params = gmlc::utilities::str2vector(positionalPayload, kNullVal);
                if ((params.size() != 5U && params.size() != 6U) ||
                    std::any_of(params.begin(), params.end(), [](double value) {
                        return !std::isfinite(value) || value == kNullVal;
                    })) {
                    throw InvalidParameterValue("WTGT_A requires five or six finite parameters");
                }
                const double ht = params[1];
                const double hg = params[2];
                const double totalInertia = ht + hg;
                const double w0 = params.size() > 5U ? params[5] : 1.0;
                if (ht <= 0.0 || hg < 0.0 || !std::isfinite(totalInertia) || totalInertia <= 0.0 ||
                    params[3] < 0.0 || w0 <= 0.0) {
                    throw InvalidParameterValue("WTGT_A has invalid inertia, damping, or speed");
                }

                stringVec shaftTokens = lineTokens;
                if (hg == 0.0 || params[4] <= 0.0) {
                    // PowerWorld treats a nonpositive shaft resonance as the
                    // single-mass model, with the combined turbine/generator H.
                    shaftTokens[1] = "'WTDS'";
                    shaftTokens.emplace_back(formatDydNumber(totalInertia));
                    shaftTokens.emplace_back("0");
                    shaftTokens.emplace_back(formatDydNumber(w0));
                    if (params[0] > 0.0) {
                        shaftTokens.emplace_back(formatDydNumber(params[0]));
                    }
                    ++singleMassWtgtFallbacks.mCount;
                    if (singleMassWtgtFallbacks.mFirstLine == 0U) {
                        singleMassWtgtFallbacks.mFirstLine = recordLineNumber;
                        singleMassWtgtFallbacks.mFirstBus = lineTokens[0];
                        singleMassWtgtFallbacks.mFirstMachine = lineTokens[2];
                    }
                } else {
                    shaftTokens[1] = "'WTDTA1'";
                    shaftTokens.insert(shaftTokens.end(),
                                       normalizedPayload.begin(),
                                       normalizedPayload.end());
                }
                if (!detail::loadDyrModelRecord(
                        parentObject, shaftTokens, disableStabilizers, zeroGainStabilizers)) {
                    throw InvalidParameterValue("WTGT_A drivetrain component is unavailable");
                }
            }
            catch (const InvalidParameterValue& error) {
                std::string message{fileName};
                message.push_back(':');
                message.append(std::to_string(recordLineNumber));
                message.append(" WTGT_A bus ").append(lineTokens[0]).append(" machine ");
                message.append(lineTokens.size() > 2U ? lineTokens[2] : "<missing>");
                message.append(": ").append(error.what());
                throw InvalidParameterValue(message);
            }
            continue;
        }

        if (sourceModelName == "wt3t") {
            if (!payloadIsSupported) {
                addUnsupportedModel(unsupportedModels,
                                    displayModelName,
                                    lineTokens,
                                    recordLineNumber);
                continue;
            }
            try {
                const auto params = gmlc::utilities::str2vector(normalizedPayload, kNullVal);
                if (params.size() != 8U ||
                    std::any_of(params.begin(), params.end(), [](double value) {
                        return !std::isfinite(value) || value == kNullVal;
                    })) {
                    throw InvalidParameterValue("WT3T requires eight finite parameters");
                }
                if (params[0] <= 0.0 || params[1] <= 0.0 || params[5] < 0.0 || params[5] >= 1.0) {
                    throw InvalidParameterValue("WT3T has invalid wind speed or inertia data");
                }

                // WT3T initializes pitch from wind speed and Theta2 above
                // rated speed; WTARA1 then supplies the matching linear aero
                // response, while WTDS/WTDTA1 carries the separate shaft role.
                const double theta0 = (params[0] > 1.0) ?
                    (params[4] / 0.75) * (1.0 - (1.0 / (params[0] * params[0]))) :
                    0.0;
                stringVec aeroTokens = lineTokens;
                aeroTokens[1] = "'WTARA1'";
                aeroTokens.emplace_back(normalizedPayload[3U]);
                aeroTokens.emplace_back(formatDydNumber(theta0));
                if (!detail::loadDyrModelRecord(
                        parentObject, aeroTokens, disableStabilizers, zeroGainStabilizers)) {
                    throw InvalidParameterValue("WT3T aerodynamic component is unavailable");
                }

                stringVec shaftTokens = lineTokens;
                if (params[5] == 0.0) {
                    // A zero turbine inertia fraction selects the documented
                    // single-mass form; Freq1 and Dshaft are unused there.
                    shaftTokens[1] = "'WTDS'";
                    shaftTokens.emplace_back(normalizedPayload[1U]);
                    shaftTokens.emplace_back(normalizedPayload[2U]);
                    shaftTokens.emplace_back("1");
                } else {
                    shaftTokens[1] = "'WTDTA1'";
                    shaftTokens.emplace_back(normalizedPayload[1U]);
                    shaftTokens.emplace_back(normalizedPayload[2U]);
                    shaftTokens.emplace_back(normalizedPayload[5U]);
                    shaftTokens.emplace_back(normalizedPayload[6U]);
                    shaftTokens.emplace_back(normalizedPayload[7U]);
                }
                if (!detail::loadDyrModelRecord(
                        parentObject, shaftTokens, disableStabilizers, zeroGainStabilizers)) {
                    throw InvalidParameterValue("WT3T shaft component is unavailable");
                }
            }
            catch (const InvalidParameterValue& error) {
                std::string message{fileName};
                message.push_back(':');
                message.append(std::to_string(recordLineNumber));
                message.append(" WT3T bus ").append(lineTokens[0]).append(" machine ");
                message.append(lineTokens.size() > 2U ? lineTokens[2] : "<missing>");
                message.append(": ").append(error.what());
                throw InvalidParameterValue(message);
            }
            continue;
        }
        const bool directModel = isDydDirectModel(canonicalModelName);
        if (payloadIsSupported && directModel) {
            if (sourceModelName == "ieeeg1") {
                // PSLF IEEEG1 is a single-generator record. The DYR loader
                // also supports the PSS/E two-generator form, so provide a
                // zero secondary bus and a harmless ID.
                lineTokens.emplace_back("0");
                lineTokens.emplace_back("'1'");
            }
            lineTokens.insert(lineTokens.end(), normalizedPayload.begin(), normalizedPayload.end());
        }
        try {
            if (!payloadIsSupported || !directModel ||
                !detail::loadDyrModelRecord(
                    parentObject, lineTokens, disableStabilizers, zeroGainStabilizers)) {
                addUnsupportedModel(unsupportedModels,
                                    displayModelName,
                                    lineTokens,
                                    recordLineNumber);
            }
        }
        catch (const InvalidParameterValue& error) {
            std::string message{fileName};
            message.push_back(':');
            message.append(std::to_string(recordLineNumber));
            message.append(" ").append(displayModelName).append(" bus ");
            message.append(lineTokens.empty() ? "<missing>" : lineTokens[0]);
            message.append(" machine ");
            message.append(lineTokens.size() > 2U ? lineTokens[2] : "<missing>");
            message.append(": ").append(error.what());
            throw InvalidParameterValue(message);
        }
    }

    if (!ignoredNonessentialModels.empty()) {
        std::string message =
            fileName + ": ignored nonessential DYD models (not loaded into the dynamic system):";
        for (const auto& [modelName, summary] : ignoredNonessentialModels) {
            message += "\n  " + modelName + " (warning): " + std::to_string(summary.mCount) +
                " record(s); first at line " + std::to_string(summary.mFirstLine) + ", bus " +
                summary.mFirstBus + " machine " + summary.mFirstMachine;
        }
        parentObject->log(parentObject, PrintLevel::WARNING, message);
    }
    if (ignoredIeeestTdelay.mCount > 0U) {
        const std::string message = fileName +
            ": ignored nonzero or invalid PSLF IEEEST Tdelay values (time delay is not modeled): " +
            std::to_string(ignoredIeeestTdelay.mCount) + " record(s); first at line " +
            std::to_string(ignoredIeeestTdelay.mFirstLine) + ", bus " +
            ignoredIeeestTdelay.mFirstBus + " machine " + ignoredIeeestTdelay.mFirstMachine +
            ", Tdelay=" + ignoredIeeestTdelay.mFirstValue;
        parentObject->log(parentObject, PrintLevel::WARNING, message);
    }
    if (ignoredEsac1aSpdmlt.mCount > 0U) {
        const std::string message = fileName +
            ": ignored nonzero or invalid PSLF ESAC1A Spdmlt values (generator-speed output "
            "scaling is not modeled): " +
            std::to_string(ignoredEsac1aSpdmlt.mCount) + " record(s); first at line " +
            std::to_string(ignoredEsac1aSpdmlt.mFirstLine) + ", bus " +
            ignoredEsac1aSpdmlt.mFirstBus + " machine " + ignoredEsac1aSpdmlt.mFirstMachine +
            ", Spdmlt=" + ignoredEsac1aSpdmlt.mFirstValue;
        parentObject->log(parentObject, PrintLevel::WARNING, message);
    }
    if (singleMassWtgtFallbacks.mCount > 0U) {
        const std::string message = fileName +
            ": WTGT_A records with Hg=0 or KShaft<=0 were loaded as the documented single-mass "
            "WTDS form: " +
            std::to_string(singleMassWtgtFallbacks.mCount) + " record(s); first at line " +
            std::to_string(singleMassWtgtFallbacks.mFirstLine) + ", bus " +
            singleMassWtgtFallbacks.mFirstBus + " machine " + singleMassWtgtFallbacks.mFirstMachine;
        parentObject->log(parentObject, PrintLevel::WARNING, message);
    }
    if (!unsupportedModels.empty()) {
        std::string message = fileName + ": unsupported DYD models:";
        for (const auto& [modelName, summary] : unsupportedModels) {
            message += "\n  " + modelName + ": " + std::to_string(summary.mCount) +
                " record(s); first at line " + std::to_string(summary.mFirstLine) + ", bus " +
                summary.mFirstBus + " machine " + summary.mFirstMachine;
        }
        throw InvalidParameterValue(message);
    }
    if (hasWsccLoadTemplates) {
        if (auto* mutableSimulation = dynamic_cast<GridDynSimulation*>(parentObject->getRoot());
            mutableSimulation != nullptr) {
            applyLoadTemplatesFromReader(*mutableSimulation, wsccLoadTemplates);
        }
    }
    if (disableStabilizers) {
        parentObject->log(parentObject,
                          PrintLevel::SUMMARY,
                          "DYD diagnostic: set zero output gain on " +
                              std::to_string(zeroGainStabilizers) + " stabilizer model records");
    }
}
}  // namespace griddyn
