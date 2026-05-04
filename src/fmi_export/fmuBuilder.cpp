/*
 * Copyright (c) 2014-2026, Lawrence Livermore National Security
 * See the top-level NOTICE for additional details. All rights reserved.
 * SPDX-License-Identifier: BSD-3-Clause
 */

#include "fmuBuilder.h"

#include "CLI11/CLI11.hpp"
#include "fileInput/fileInput.h"
#include "fmiCollector.h"
#include "fmiCoordinator.h"
#include "fmiEvent.h"
#include "gmlc/utilities/stringOps.h"
#include "griddyn/gridDynSimulation.h"
#include "loadFMIExportObjects.h"
#include "utilities/zipUtilities.h"
#include <filesystem>
#include <iostream>
#include <memory>
#include <pugixml.hpp>
#include <set>
#include <string>
#include <utility>
#include <vector>

#ifndef GRIDDYNFMILIBRARY_BINARY_LOC
#    define GRIDDYNFMILIBRARY_BINARY_LOC ""
#endif

#ifndef FMILIBRARY_TYPE
#    define FMILIBRARY_TYPE "unknown"
#endif

namespace griddyn::fmi {
using std::filesystem::copy_file;
using std::filesystem::copy_options;
using std::filesystem::create_directory;
using std::filesystem::current_path;
using std::filesystem::exists;
using std::filesystem::filesystem_error;
using std::filesystem::path;
using std::filesystem::temp_directory_path;

fmuBuilder::fmuBuilder()
{
    loadComponents();
}

fmuBuilder::fmuBuilder(std::shared_ptr<gridDynSimulation> gds): GriddynRunner(std::move(gds))
{
    loadComponents();
}

void fmuBuilder::loadComponents()
{
    coord_ = make_owningPtr<fmiCoordinator>();
    auto gds = getSim();
    if (gds == nullptr) {
        resetSim(std::make_shared<gridDynSimulation>());
        gds = getSim();
    }
    gds->add(coord_.get());
    ri_ = std::make_unique<readerInfo>();
    loadFmiExportReaderInfoDefinitions(*ri_);
    ri_->captureFiles = true;
}

fmuBuilder::~fmuBuilder() = default;

static const std::set<std::string>
    valid_platforms{"all", "windows", "linux", "macos", "darwin", "win64", "linux64", "darwin64"};

std::shared_ptr<CLI::App> fmuBuilder::generateLocalCommandLineParser(readerInfo& ri)
{
    auto app = std::make_shared<CLI::App>("fmu options");
    app->add_option("--buildfmu,--fmu", fmuLoc, "fmu file to build");
    app->add_option("--platform", platform, "build the fmu for a specific platform")
        ->transform(CLI::IsMember(
            valid_platforms, CLI::ignore_case, CLI::ignore_underscore, CLI::ignore_space));

    app->add_flag("--keep_dir", keep_dir, "keep the temporary directory after building")
        ->ignore_underscore();
    loadFmiExportReaderInfoDefinitions(ri);
    return app;
}

/** helper function to copy a file and overwrite if requested*/
bool testCopyFile(path const& source, path const& dest, bool overwrite = false)
{
    std::cout << "copying " << source << " to " << dest << std::endl;
    copy_options option = copy_options::none;
    if (overwrite) {
        option = copy_options::overwrite_existing;
    }

    try {
        copy_file(source, dest, option);
        return true;
    }
    catch (filesystem_error const&) {
        return false;
    }
}

void fmuBuilder::MakeFmu(const std::string& fmuLocation)
{
    auto bpath = temp_directory_path();

    auto fmupath = path(fmuLocation);

    if (fmuLocation.empty()) {
        if (!fmuLoc.empty()) {
            fmupath = path(fmuLoc);
        } else {
            fmupath = path("griddyn.fmu");
        }
    }
    auto fmu_temp_dir = bpath / fmupath.stem();
    create_directory(fmu_temp_dir);

    copySharedLibrary(fmu_temp_dir.string());

    path resource_dir = fmu_temp_dir / "resources";
    create_directory(resource_dir);

    path sourcefile = getSim()->sourceFile;
    auto ext = gmlc::utilities::convertToLowerCase(sourcefile.extension().string());
    if (ext[0] == '.') {
        ext.erase(0, 1);
    }
    auto newFile = resource_dir;
    if (ext == "xml") {
        newFile /= "simulation.xml";
    } else if (ext == "json") {
        newFile /= "simulation.json";
    } else {
        if (sourcefile.empty()) {
            getSim()->log(nullptr, print_level::error, "no input file specified");
        } else {
            getSim()->log(nullptr, print_level::error, "for fmu's input file must be xml or json");
        }

        return;
    }
    // copy the resource files over to the resource directory
    testCopyFile(getSim()->sourceFile, newFile, true);

    for (const auto& file : ri_->getCapturedFiles()) {
        path f(file);
        if (exists(f)) {
            testCopyFile(f, resource_dir / f.filename());
        }
    }
    // now generate the model description file
    generateXML((fmu_temp_dir / "modelDescription.xml").string());

    if (fmupath.is_absolute()) {
        // now zip the fmu
        int status = utilities::zipFolder(fmupath.string(), fmu_temp_dir.string());
        if (status == 0) {
            getSim()->log(nullptr, print_level::summary, "fmu created at " + fmupath.string());
        } else {
            getSim()->log(nullptr,
                          print_level::error,
                          "zip status failure creating " + fmupath.string() +
                              "returned with error code " + std::to_string(status));
        }
    } else {
        auto path2 = current_path() / fmupath;
        int status = utilities::zipFolder(path2.string(), fmu_temp_dir.string());
        if (status == 0) {
            getSim()->log(nullptr, print_level::summary, "fmu created at " + path2.string());
        } else {
            getSim()->log(nullptr,
                          print_level::error,
                          "zip status failure creating " + fmupath.string() +
                              "returned with error code " + std::to_string(status));
        }
    }
}

void fmuBuilder::copySharedLibrary(const std::string& tempdir)
{
    path binary_dir = path(tempdir) / "binaries";
    create_directory(binary_dir);
    bool copySome = false;
    path executable(execPath);
    path execDir = executable.parent_path();
    if ((platform == "all") || (platform == "windows") || (platform == "win64")) {
        auto source = execDir / "win64" / "fmiGridDynSharedLib.dll";
        if (exists(source)) {
            create_directory(binary_dir / "win64");
            auto dest = binary_dir / "win64" / "fmiGridDynSharedLib.dll";
            testCopyFile(source, dest);
            copySome = true;
        } else {
            source = execDir / "win64" / "libfmiGridDynSharedLib.dll";
            if (exists(source)) {
                create_directory(binary_dir / "win64");
                auto dest = binary_dir / "win64" / "fmiGridDynSharedLib.dll";
                testCopyFile(source, dest);
                copySome = true;
            }
        }
    }

    if ((platform == "all") || (platform == "linux") || (platform == "linux64")) {
        auto source = execDir / "linux64" / "fmiGridDynSharedLib.so";
        if (exists(source)) {
            create_directory(binary_dir / "linux64");
            auto dest = binary_dir / "linux64" / "fmiGridDynSharedLib.so";
            testCopyFile(source, dest);
            copySome = true;
        } else {
            source = execDir / "linux64" / "libfmiGridDynSharedLib.so";
            if (exists(source)) {
                create_directory(binary_dir / "linux64");
                auto dest = binary_dir / "linux64" / "fmiGridDynSharedLib.so";
                testCopyFile(source, dest);
                copySome = true;
            }
        }
    }
    if ((platform == "all") || (platform == "macos") || (platform == "darwin") ||
        (platform == "darwin64")) {
        auto source = execDir / "darwin64" / "fmiGridDynSharedLib.so";
        if (exists(source)) {
            create_directory(binary_dir / "darwin64");
            auto dest = binary_dir / "darwin64" / "fmiGridDynSharedLib.so";
            testCopyFile(source, dest);
            copySome = true;
        } else if (exists(execDir / "darwin64" / "fmiGridDynSharedLib.dylib")) {
            source = execDir / "darwin64" / "fmiGridDynSharedLib.dylib";
            create_directory(binary_dir / "darwin64");
            auto dest = binary_dir / "darwin64" / "fmiGridDynSharedLib.dylib";
            testCopyFile(source, dest);
            copySome = true;
        } else if (exists(execDir / "darwin64" / "libfmiGridDynSharedLib.dylib")) {
            source = execDir / "darwin64" / "libfmiGridDynSharedLib.dylib";
            create_directory(binary_dir / "darwin64");
            auto dest = binary_dir / "darwin64" / "fmiGridDynSharedLib.dylib";
            testCopyFile(source, dest);
            copySome = true;
        } else if (exists(execDir / "darwin64" / "libfmiGridDynSharedLib.so")) {
            source = execDir / "darwin64" / "libfmiGridDynSharedLib.so";
            create_directory(binary_dir / "darwin64");
            auto dest = binary_dir / "darwin64" / "fmiGridDynSharedLib.so";
            testCopyFile(source, dest);
            copySome = true;
        }
    }
    auto binaryLocPath = path(GRIDDYNFMILIBRARY_LOC);
    if (exists(binaryLocPath / GRIDDYNFMILIBRARY_NAME)) {
        create_directory(binary_dir / FMILIBRARY_TYPE);
        auto source = binaryLocPath / GRIDDYNFMILIBRARY_NAME;
        auto dest = binary_dir / FMILIBRARY_TYPE / GRIDDYNFMILIBRARY_NAME;
        testCopyFile(source, dest);
        return;
    }
    if (exists(binaryLocPath / "fmiGridDynSharedLib.dll")) {
        create_directory(binary_dir / FMILIBRARY_TYPE);
        auto source = binaryLocPath / "fmiGridDynSharedLib.dll";
        auto dest = binary_dir / FMILIBRARY_TYPE / "fmiGridDynSharedLib.dll";
        testCopyFile(source, dest);
        return;
    }
    if (exists(binaryLocPath / "libfmiGridDynSharedLib.dll")) {
        create_directory(binary_dir / FMILIBRARY_TYPE);
        auto source = binaryLocPath / "libfmiGridDynSharedLib.dll";
        auto dest = binary_dir / FMILIBRARY_TYPE / "fmiGridDynSharedLib.dll";
        testCopyFile(source, dest);
        return;
    }
    if (exists(binaryLocPath / "fmiGridDynSharedLib.so")) {
        create_directory(binary_dir / FMILIBRARY_TYPE);
        auto source = binaryLocPath / "fmiGridDynSharedLib.so";
        auto dest = binary_dir / FMILIBRARY_TYPE / "fmiGridDynSharedLib.so";
        testCopyFile(source, dest);
        return;
    }
    if (exists(binaryLocPath / "libfmiGridDynSharedLib.so")) {
        create_directory(binary_dir / FMILIBRARY_TYPE);
        auto source = binaryLocPath / "libfmiGridDynSharedLib.so";
        auto dest = binary_dir / FMILIBRARY_TYPE / "fmiGridDynSharedLib.so";
        testCopyFile(source, dest);
        return;
    }
    if (exists(binaryLocPath / "fmiGridDynSharedLib.dylib")) {
        create_directory(binary_dir / FMILIBRARY_TYPE);
        auto source = binaryLocPath / "fmiGridDynSharedLib.dylib";
        auto dest = binary_dir / FMILIBRARY_TYPE / "fmiGridDynSharedLib.dylib";
        testCopyFile(source, dest);
        return;
    }
    if (exists(binaryLocPath / "libfmiGridDynSharedLib.dylib")) {
        create_directory(binary_dir / FMILIBRARY_TYPE);
        auto source = binaryLocPath / "libfmiGridDynSharedLib.dylib";
        auto dest = binary_dir / FMILIBRARY_TYPE / "fmiGridDynSharedLib.dylib";
        testCopyFile(source, dest);
        return;
    }
    if (copySome) {
        return;
    }

// Deal with Visual Studio locations
#ifndef NDEBUG
    if (exists(binaryLocPath / "Debug" / "fmiGridDynSharedLib.dll")) {
        create_directory(binary_dir / FMILIBRARY_TYPE);
        auto source = binaryLocPath / "Debug" / "fmiGridDynSharedLib.dll";
        auto dest = binary_dir / FMILIBRARY_TYPE / "fmiGridDynSharedLib.dll";
        testCopyFile(source, dest);
        return;
    }
#else
    if (exists(binaryLocPath / "Release" / "fmiGridDynSharedLib.dll")) {
        create_directory(binary_dir / FMILIBRARY_TYPE);
        auto source = binaryLocPath / "Release" / "fmiGridDynSharedLib.dll";
        auto dest = binary_dir / FMILIBRARY_TYPE / "fmiGridDynSharedLib.dll";
        testCopyFile(source, dest);
        return;
    }
#endif
    // now just search the current directory
    if (exists(GRIDDYNFMILIBRARY_NAME)) {
        create_directory(binary_dir / FMILIBRARY_TYPE);
        testCopyFile(GRIDDYNFMILIBRARY_NAME, binary_dir / FMILIBRARY_TYPE / GRIDDYNFMILIBRARY_NAME);
        return;
    }
    if (exists("fmiGridDynSharedLib.dll")) {
        create_directory(binary_dir / FMILIBRARY_TYPE);
        testCopyFile("fmiGridDynSharedLib.dll",
                     binary_dir / FMILIBRARY_TYPE / "fmiGridDynSharedLib.dll");
        return;
    }
    if (exists("libfmiGridDynSharedLib.dll")) {
        create_directory(binary_dir / FMILIBRARY_TYPE);
        testCopyFile("libfmiGridDynSharedLib.dll",
                     binary_dir / FMILIBRARY_TYPE / "fmiGridDynSharedLib.dll");
        return;
    }
    if (exists("fmiGridDynSharedLib.so")) {
        create_directory(binary_dir / FMILIBRARY_TYPE);
        testCopyFile("fmiGridDynSharedLib.so",
                     binary_dir / FMILIBRARY_TYPE / "fmiGridDynSharedLib.so");
        return;
    }
    if (exists("libfmiGridDynSharedLib.so")) {
        create_directory(binary_dir / FMILIBRARY_TYPE);
        testCopyFile("libfmiGridDynSharedLib.so",
                     binary_dir / FMILIBRARY_TYPE / "fmiGridDynSharedLib.so");
        return;
    }
    if (exists("fmiGridDynSharedLib.dylib")) {
        create_directory(binary_dir / FMILIBRARY_TYPE);
        testCopyFile("fmiGridDynSharedLib.dylib",
                     binary_dir / FMILIBRARY_TYPE / "fmiGridDynSharedLib.dylib");
        return;
    }
    if (exists("libfmiGridDynSharedLib.dylib")) {
        create_directory(binary_dir / FMILIBRARY_TYPE);
        testCopyFile("libfmiGridDynSharedLib.dylib",
                     binary_dir / FMILIBRARY_TYPE / "fmiGridDynSharedLib.dylib");
        return;
    }
    throw(std::runtime_error("unable to locate shared fmu library file"));
}

void fmuBuilder::generateXML(const std::string& xmlfile)
{
    pugi::xml_document doc;
    int index = 1;
    // add the standard xml declaration
    auto dec = doc.append_child(pugi::node_declaration);
    dec.append_attribute("version") = "1.0";
    // add the main xml root object
    auto pRoot = doc.append_child("fmiModelDescription");

    pRoot.append_attribute("fmiVersion") = "2.0";
    pRoot.append_attribute("modelName") = getSim()->getName().c_str();
    pRoot.append_attribute("guid") = "{82072fd0-2f55-4c42-b84c-e47ee14091d0}";

    auto desc = getSim()->getDescription();
    if (!desc.empty()) {
        pRoot.append_attribute("description") = desc.c_str();
    }

    pRoot.append_attribute("version") = getSim()->getString("version").c_str();

    // the cosimulation description section
    auto pElement = pRoot.append_child("CoSimulation");
    pElement.append_attribute("modelIdentifier") = GRIDDYNFMILIBRARY_BASE_NAME;
    pElement.append_attribute("canHandleVariableCommunicationStepSize") = "true";

    // log categories section

    pElement = pRoot.append_child("LogCategories");

    auto logElement = pElement.append_child("Category");
    logElement.append_attribute("name") = "logError";

    logElement = pElement.append_child("Category");
    logElement.append_attribute("name") = "logWarning";

    logElement = pElement.append_child("Category");
    logElement.append_attribute("name") = "logSummary";

    logElement = pElement.append_child("Category");
    logElement.append_attribute("name") = "logNormal";

    logElement = pElement.append_child("Category");
    logElement.append_attribute("name") = "logDebug";

    logElement = pElement.append_child("Category");
    logElement.append_attribute("name") = "logTrace";

    // next load all the scalar variables in the system
    pElement = pRoot.append_child("ModelVariables");

    auto sVariable = pElement.append_child("ScalarVariable");
    sVariable.append_attribute("name") = "run_asynchronously";
    sVariable.append_attribute("valueReference") = 0;

    sVariable.append_attribute("description") =
        "set to true to enable GridDyn to run Asynchronously";
    sVariable.append_attribute("causality") = "parameter";
    sVariable.append_attribute("variability") = "fixed";
    ++index;
    auto bType = sVariable.append_child("Boolean");
    bType.append_attribute("start") = false;

    sVariable = pElement.append_child("ScalarVariable");
    sVariable.append_attribute("name") = "record_directory";
    sVariable.append_attribute("valueReference") = 1;

    auto sType = sVariable.append_child("String");
    sType.append_attribute("start") = "";

    sVariable.append_attribute("description") = "set the directory to place GridDyn outputs";
    sVariable.append_attribute("causality") = "parameter";
    sVariable.append_attribute("variability") = "fixed";
    ++index;

    auto fmiInputs = coord_->getInputs();
    for (auto& input : fmiInputs) {
        sVariable = pElement.append_child("ScalarVariable");
        sVariable.append_attribute("name") = input.second.name.c_str();
        sVariable.append_attribute("valueReference") = input.first;
        auto evntdesc = input.second.evnt->getDescription();
        if (!evntdesc.empty()) {
            sVariable.append_attribute("description") = evntdesc.c_str();
        }
        sVariable.append_attribute("causality") = "input";
        sVariable.append_attribute("variability") = "continuous";
        auto rType = sVariable.append_child("Real");
        rType.append_attribute("start") = coord_->getOutput(input.first);
        ++index;
    }
    auto fmiParams = coord_->getParameters();
    for (auto& param : fmiParams) {
        sVariable = pElement.append_child("ScalarVariable");
        sVariable.append_attribute("name") = param.second.name.c_str();
        sVariable.append_attribute("valueReference") = param.first;
        auto evntdesc = param.second.evnt->getDescription();
        if (!evntdesc.empty()) {
            sVariable.append_attribute("description") = evntdesc.c_str();
        }
        sVariable.append_attribute("causality") = "parameter";
        if (fmiCoordinator::isStringParameter(param)) {
            sVariable.append_attribute("variability") = "fixed";
            auto sParamType = sVariable.append_child("String");
            sParamType.append_attribute("start") = "";
        } else {
            sVariable.append_attribute("variability") = "continuous";

            auto rType = sVariable.append_child("Real");
            rType.append_attribute("start") = coord_->getOutput(param.first);
        }

        ++index;
    }
    std::vector<int> outputIndices;
    auto fmiOutputs = coord_->getOutputs();
    for (auto& out : fmiOutputs) {
        sVariable = pElement.append_child("ScalarVariable");
        sVariable.append_attribute("name") = out.second.name.c_str();
        sVariable.append_attribute("valueReference") = out.first;
        sVariable.append_attribute("causality") = "output";
        sVariable.append_attribute("variability") = "continuous";
        // TODO(phlpt): Figure out how to generate descriptions.
        sVariable.append_child("Real");
        outputIndices.push_back(index);
        ++index;
    }
    // load the dependencies

    pElement = pRoot.append_child("ModelStructure");
    auto outputs = pElement.append_child("Outputs");
    for (auto& outind : outputIndices) {
        auto out = outputs.append_child("Unknown");
        out.append_attribute("index") = outind;
    }

    auto initUnkn = pElement.append_child("InitialUnknowns");
    for (auto& outind : outputIndices) {
        auto out = initUnkn.append_child("Unknown");
        out.append_attribute("index") = outind;
    }

    if (!doc.save_file(xmlfile.c_str())) {
        throw(std::runtime_error("unable to write file"));
    }
}

}  // namespace griddyn::fmi
