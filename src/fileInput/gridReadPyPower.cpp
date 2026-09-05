/*
 * Copyright (c) 2014-2026, Lawrence Livermore National Security
 * See the top-level NOTICE for additional details. All rights reserved.
 * SPDX-License-Identifier: BSD-3-Clause
 */

#include "fileInput.h"
#include "readerHelper.h"
#include <fstream>
#include <iostream>
#include <sstream>
#include <string>
#include <string_view>

namespace griddyn {
namespace {

    size_t findPpcAssignment(const std::string& text, std::string_view key)
    {
        for (const char quote : {'\'', '"'}) {
            const std::string name =
                "ppc[" + std::string(1, quote) + std::string(key) + quote + "]";
            const size_t position = text.find(name);
            if (position != std::string::npos) {
                return position;
            }
        }
        return std::string::npos;
    }

    bool appendPpcArray(const std::string& text, std::string_view key, std::string& matPowerText)
    {
        const size_t assignment = findPpcAssignment(text, key);
        if (assignment == std::string::npos) {
            return false;
        }
        const size_t arrayStart = text.find("array", assignment);
        const size_t openBracket = text.find('[', arrayStart);
        if ((arrayStart == std::string::npos) || (openBracket == std::string::npos)) {
            return false;
        }

        size_t closeBracket = openBracket;
        int depth = 0;
        for (; closeBracket < text.size(); ++closeBracket) {
            if (text[closeBracket] == '[') {
                ++depth;
            } else if (text[closeBracket] == ']' && --depth == 0) {
                break;
            }
        }
        if (closeBracket == text.size()) {
            return false;
        }

        matPowerText += "mpc." + std::string(key) + " = [\n";
        for (size_t position = openBracket + 1; position < closeBracket;) {
            const size_t rowStart = text.find('[', position);
            if ((rowStart == std::string::npos) || (rowStart >= closeBracket)) {
                break;
            }
            const size_t rowEnd = text.find(']', rowStart + 1);
            if ((rowEnd == std::string::npos) || (rowEnd > closeBracket)) {
                return false;
            }
            for (size_t index = rowStart + 1; index < rowEnd; ++index) {
                const char character = text[index];
                matPowerText +=
                    (character == ',' || character == '\r' || character == '\n') ? ' ' : character;
            }
            matPowerText += ";\n";
            position = rowEnd + 1;
        }
        matPowerText += "];\n";
        return true;
    }

}  // namespace

void loadPyPower(CoreObject* parentObject,
                 const std::string& fileName,
                 const BasicReaderInfo& readerOptions)
{
    const std::ifstream input(fileName);
    std::stringstream inputStream;
    inputStream << input.rdbuf();
    const std::string pyPowerText = inputStream.str();
    if (pyPowerText.empty()) {
        std::cout << "Warning file " << fileName << " is invalid or empty\n";
        return;
    }

    const size_t baseAssignment = findPpcAssignment(pyPowerText, "baseMVA");
    if (baseAssignment == std::string::npos) {
        std::cout << "Warning file " << fileName << " does not contain a PYPOWER ppc dictionary\n";
        return;
    }
    const size_t equals = pyPowerText.find('=', baseAssignment);
    if (equals == std::string::npos) {
        std::cout << "Warning file " << fileName << " has an invalid PYPOWER baseMVA assignment\n";
        return;
    }
    const size_t lineEnd = pyPowerText.find_first_of("\r\n#", equals);

    std::string matPowerText =
        "mpc.baseMVA = " + pyPowerText.substr(equals + 1, lineEnd - equals - 1) + ";\n";
    if (!appendPpcArray(pyPowerText, "bus", matPowerText)) {
        std::cout << "Warning file " << fileName << " does not contain a valid PYPOWER bus array\n";
        return;
    }
    appendPpcArray(pyPowerText, "gen", matPowerText);
    appendPpcArray(pyPowerText, "branch", matPowerText);
    appendPpcArray(pyPowerText, "gencost", matPowerText);
    loadMatPower(parentObject, matPowerText, "mpc", readerOptions);
}

}  // namespace griddyn
