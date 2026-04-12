/*
 * Copyright (c) 2014-2020, Lawrence Livermore National Security
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

void loadMFile(coreObject* parentObject, const std::string& fileName, const basicReaderInfo& bri)
{
    std::ifstream infile(fileName.c_str(), std::ios::in);
    std::stringstream strStream;
    strStream << infile.rdbuf();
    std::string filetext = strStream.str();
    infile.close();
    if (filetext.empty()) {
        std::cout << "Warning file " << fileName << "is invalid or empty\n";
        return;
    }
    removeMatlabComments(filetext);
    size_t func = filetext.find("function");

    size_t mpc = filetext.find("mpc");
    if ((func != std::string::npos) || (mpc != std::string::npos)) {
        if (func == std::string::npos) {
            func = 0;
        }
        size_t A = filetext.find_first_of('=', func + 9);
        std::string bname =
            gmlc::utilities::stringOps::trim(filetext.substr(func + 9, A - func - 9));

        size_t B = filetext.find(bname + ".bus");
        if (B != std::string::npos) {
            loadMatPower(parentObject, filetext, bname, bri);
        } else {
            A = filetext.find("MatDyn");
            if (A != std::string::npos) {
                A = filetext.find("event");
                if (A != std::string::npos) {
                    loadMatDynEvent(parentObject, filetext, bri);
                } else {
                    loadMatDyn(parentObject, filetext, bri);
                }
            } else {
                A = filetext.find("exc");
                if (A != std::string::npos) {
                    B = filetext.find("gov");
                    if (B != std::string::npos) {
                        loadMatDyn(parentObject, filetext, bri);
                    } else {
                        std::cout << "unrecognized file structure\n";
                    }
                } else {
                    A = filetext.find("event");
                    if (A != std::string::npos) {
                        loadMatDynEvent(parentObject, filetext, bri);
                    } else {
                        std::cout << "unrecognized file structure\n";
                    }
                }
            }
        }
    } else {
        size_t A = filetext.find("Bus.con");  // look for the Psat bus configuration array
        if (A != std::string::npos) {
            loadPSAT(parentObject, filetext, bri);
        }
    }
}

void removeMatlabComments(std::string& text)
{
    size_t A = text.find_first_of('%');
    while (A != std::string::npos) {
        size_t B = text.find_first_of('\n', A);
        text.erase(A, B - A + 1);
        A = text.find_first_of('%');
    }
}

bool readMatlabArray(const std::string& Name, const std::string& text, mArray& matA)
{
    size_t A = text.find(Name);
    if (A != std::string::npos) {
        size_t B = text.find_first_of('=', A);
        readMatlabArray(text, B + 1, matA);
        return true;
    }
    return false;
}

void readMatlabArray(const std::string& text, size_t start, mArray& matA)
{
    using std::string_view;

    string_view svtext = text;
    size_t A = text.find_first_of('[', start);
    size_t B = text.find_first_of(']', A);
    auto Adat = svtext.substr(A + 1, B - A);
    matA.resize(0);

    std::vector<double> M;
    size_t D = 0;
    size_t C = Adat.find_first_of("];");
    while (C != std::string_view::npos) {
        auto line = Adat.substr(D, C - D);
        gmlc::utilities::string_viewOps::trimString(line);
        if (line.empty()) {
            D = C + 1;
            C = Adat.find_first_of(";]", D);
            continue;
        }
        auto Tline = gmlc::utilities::stringOps::splitline(
            line,
            gmlc::utilities::stringOps::whiteSpaceCharacters,
            gmlc::utilities::stringOps::delimiter_compression::on);
        M.resize(Tline.size());
        size_t offset = 0;
        for (size_t kk = 0; kk < Tline.size(); ++kk) {
            if (Tline[kk] == "...") {
                offset++;
                continue;
            }
            if (Tline[kk].empty()) {
                offset++;
                continue;
            }
            M[kk - offset] = numeric_conversion(Tline[kk], 0.0);
        }
        M.resize(Tline.size() - offset);
        matA.push_back(M);
        D = C + 1;
        C = Adat.find_first_of(";]", D);
    }
}

stringVec readMatlabCellArray(const std::string& text, size_t start)
{
    stringVec cell;

    size_t A = text.find_first_of('{', start);
    size_t B = text.find_first_of('}', A);
    std::string Adat = text.substr(A + 1, B - A);
    size_t C = Adat.find_first_of('\'', 0);
    while (C != std::string::npos) {
        size_t D = Adat.find_first_of(";,}", C + 1);
        if (D != std::string::npos) {
            auto line = Adat.substr(C, D - C);
            gmlc::utilities::stringOps::trimString(line);
            if (line[0] == '\'') {
                line = line.substr(1, line.size() - 2);
                gmlc::utilities::stringOps::trimString(line);
            }
            cell.push_back(line);
        }
        C = Adat.find_first_of('\'', D + 1);
    }
    return cell;
}

}  // namespace griddyn
