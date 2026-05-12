/*
 * Copyright (c) 2014-2026, Lawrence Livermore National Security
 * See the top-level NOTICE for additional details. All rights reserved.
 * SPDX-License-Identifier: BSD-3-Clause
 */

#pragma once

#include "griddyn/gridDynDefinitions.hpp"
#include <iostream>
#include <stdexcept>
#include <string>
#include <vector>

#define READER_DEFAULT_PRINT READER_NO_PRINT

#define LEVELPRINT(LEVEL, X)                                                                       \
    if (LEVEL <= readerConfig::printMode) (std::cout << X << '\n')

#define WARNPRINT(LEVEL, X)                                                                        \
    if (LEVEL <= readerConfig::warnMode)                                                           \
    (++readerConfig::warnCount,                                                                    \
     std::cout << "WARNING(" << readerConfig::warnCount << "): " << X << '\n')

// helper function for grabbing parameters and attributes from an xml file

namespace griddyn {
class gridParameter;
class readerInfo;
class coreObject;
class basicReaderInfo;

void paramStringProcess(gridParameter& param, readerInfo& readerInformation);

double convertBV(std::string& baseVoltageCode);

using mArray = std::vector<std::vector<double>>;

double interpretString(const std::string& command, readerInfo& readerInformation);

// NOTE:PT I am leaving these as size_t since they are part of file reading and text location types
// and spread across multiple files
void readMatlabArray(const std::string& text, size_t start, mArray& matlabArray);
bool readMatlabArray(const std::string& name, const std::string& text, mArray& matlabArray);
stringVec readMatlabCellArray(const std::string& text, size_t start);
void removeMatlabComments(std::string& text);

void loadPSAT(coreObject* parentObject, const std::string& fileText, const basicReaderInfo& bri);
void loadMatPower(coreObject* parentObject,
                  const std::string& fileText,
                  const std::string& baseName,
                  const basicReaderInfo& bri);
void loadMatDyn(coreObject* parentObject, const std::string& fileText, const basicReaderInfo& bri);
void loadMatDynEvent(coreObject* parentObject,
                     const std::string& fileText,
                     const basicReaderInfo& bri);

}  // namespace griddyn
