/*
 * Copyright (c) 2014-2026, Lawrence Livermore National Security
 * See the top-level NOTICE for additional details. All rights reserved.
 * SPDX-License-Identifier: BSD-3-Clause
 */

#include "fileInput.h"
#include "gmlc/utilities/stringOps.h"
#include "gmlc/utilities/string_viewConversion.h"
#include "readerHelper.h"
#include <fstream>
#include <iostream>
#include <sstream>
#include <string>
#include <string_view>
#include <vector>

namespace griddyn {
using gmlc::utilities::numeric_conversion;

void loadMatlabFile(coreObject* parentObject,
               const std::string& fileName,
               const basicReaderInfo& readerOptions)
{
    std::ifstream infile(fileName.c_str(), std::ios::in);
    std::stringstream stringStream;
    stringStream << infile.rdbuf();
    std::string fileText = stringStream.str();
    infile.close();
    if (fileText.empty()) {
        std::cout << "Warning file " << fileName << "is invalid or empty\n";
        return;
    }
    removeMatlabComments(fileText);
    size_t functionPos = fileText.find("function");

    const size_t matPowerPos = fileText.find("mpc");
    if ((functionPos != std::string::npos) || (matPowerPos != std::string::npos)) {
        if (functionPos == std::string::npos) {
            functionPos = 0;
        }
        size_t equalsPos = fileText.find_first_of('=', functionPos + 9);
        const std::string baseName = gmlc::utilities::stringOps::trim(
            fileText.substr(functionPos + 9, equalsPos - functionPos - 9));

        size_t busPos = fileText.find(baseName + ".bus");
        if (busPos != std::string::npos) {
            loadMatPower(parentObject, fileText, baseName, readerOptions);
        } else {
            equalsPos = fileText.find("MatDyn");
            if (equalsPos != std::string::npos) {
                equalsPos = fileText.find("event");
                if (equalsPos != std::string::npos) {
                    loadMatDynEvent(parentObject, fileText, readerOptions);
                } else {
                    loadMatDyn(parentObject, fileText, readerOptions);
                }
            } else {
                equalsPos = fileText.find("exc");
                if (equalsPos != std::string::npos) {
                    busPos = fileText.find("gov");
                    if (busPos != std::string::npos) {
                        loadMatDyn(parentObject, fileText, readerOptions);
                    } else {
                        std::cout << "unrecognized file structure\n";
                    }
                } else {
                    equalsPos = fileText.find("event");
                    if (equalsPos != std::string::npos) {
                        loadMatDynEvent(parentObject, fileText, readerOptions);
                    } else {
                        std::cout << "unrecognized file structure\n";
                    }
                }
            }
        }
    } else {
        const size_t busConfigPos =
            fileText.find("Bus.con");  // look for the Psat bus configuration array
        if (busConfigPos != std::string::npos) {
            loadPSAT(parentObject, fileText, readerOptions);
        }
    }
}

void removeMatlabComments(std::string& text)
{
    size_t commentPos = text.find_first_of('%');
    while (commentPos != std::string::npos) {
        const size_t lineEndPos = text.find_first_of('\n', commentPos);
        text.erase(commentPos, lineEndPos - commentPos + 1);
        commentPos = text.find_first_of('%');
    }
}

bool readMatlabArray(const std::string& name, const std::string& text, mArray& matlabArray)
{
    const size_t startPos = text.find(name);
    if (startPos != std::string::npos) {
        const size_t equalsPos = text.find_first_of('=', startPos);
        readMatlabArray(text, equalsPos + 1, matlabArray);
        return true;
    }
    return false;
}

void readMatlabArray(const std::string& text, size_t start, mArray& matlabArray)
{
    using std::string_view;

    string_view svtext = text;
    const size_t openBracketPos = text.find_first_of('[', start);
    const size_t closeBracketPos = text.find_first_of(']', openBracketPos);
    auto arrayData = svtext.substr(openBracketPos + 1, closeBracketPos - openBracketPos);
    matlabArray.resize(0);

    std::vector<double> rowValues;
    size_t dataStart = 0;
    size_t delimiterPos = arrayData.find_first_of("];");
    while (delimiterPos != std::string_view::npos) {
        auto line = arrayData.substr(dataStart, delimiterPos - dataStart);
        gmlc::utilities::string_viewOps::trimString(line);
        if (line.empty()) {
            dataStart = delimiterPos + 1;
            delimiterPos = arrayData.find_first_of(";]", dataStart);
            continue;
        }
        auto tokens = gmlc::utilities::stringOps::splitline(
            line,
            gmlc::utilities::stringOps::whiteSpaceCharacters,
            gmlc::utilities::stringOps::delimiter_compression::on);
        rowValues.resize(tokens.size());
        size_t offset = 0;
        for (size_t tokenIndex = 0; tokenIndex < tokens.size(); ++tokenIndex) {
            if (tokens[tokenIndex] == "...") {
                offset++;
                continue;
            }
            if (tokens[tokenIndex].empty()) {
                offset++;
                continue;
            }
            rowValues[tokenIndex - offset] = numeric_conversion(tokens[tokenIndex], 0.0);
        }
        rowValues.resize(tokens.size() - offset);
        matlabArray.push_back(rowValues);
        dataStart = delimiterPos + 1;
        delimiterPos = arrayData.find_first_of(";]", dataStart);
    }
}

stringVec readMatlabCellArray(const std::string& text, size_t start)
{
    stringVec cell;

    const size_t openBracePos = text.find_first_of('{', start);
    const size_t closeBracePos = text.find_first_of('}', openBracePos);
    const std::string arrayData = text.substr(openBracePos + 1, closeBracePos - openBracePos);
    size_t quotePos = arrayData.find_first_of('\'', 0);
    while (quotePos != std::string::npos) {
        const size_t delimiterPos = arrayData.find_first_of(";,}", quotePos + 1);
        if (delimiterPos != std::string::npos) {
            auto line = arrayData.substr(quotePos, delimiterPos - quotePos);
            gmlc::utilities::stringOps::trimString(line);
            if (line[0] == '\'') {
                line = line.substr(1, line.size() - 2);
                gmlc::utilities::stringOps::trimString(line);
            }
            cell.push_back(line);
        }
        quotePos = arrayData.find_first_of('\'', delimiterPos + 1);
    }
    return cell;
}

}  // namespace griddyn
