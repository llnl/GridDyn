/*
 * Copyright (c) 2014-2026, Lawrence Livermore National Security
 * See the top-level NOTICE for additional details. All rights reserved.
 * SPDX-License-Identifier: BSD-3-Clause
 */

#include <nanobind/nanobind.h>
#include <nanobind/stl/string.h>
#include <nanobind/stl/vector.h>

#include "core/CoreExceptions.h"
#include "fileInput/fileInput.h"
#include "griddyn/GridDynSimulation.h"
#include "griddyn/gridDynVersion.hpp"
#include "runner/gridDynRunner.h"

#include <filesystem>
#include <memory>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

namespace nb = nanobind;
using namespace nb::literals;

namespace {

PyObject* pyInvalidObjectError = nullptr;
PyObject* pyInvalidParameterError = nullptr;
PyObject* pyFileLoadError = nullptr;
PyObject* pySolveError = nullptr;
PyObject* pyExecutionError = nullptr;
PyObject* pyGridDynError = nullptr;

class GridDynError: public std::runtime_error {
  public:
    using std::runtime_error::runtime_error;
};

class InvalidObjectError: public GridDynError {
  public:
    using GridDynError::GridDynError;
};

class InvalidParameterError: public GridDynError {
  public:
    using GridDynError::GridDynError;
};

class FileLoadError: public GridDynError {
  public:
    using GridDynError::GridDynError;
};

class SolveError: public GridDynError {
  public:
    using GridDynError::GridDynError;
};

class ExecutionError: public GridDynError {
  public:
    using GridDynError::GridDynError;
};

std::string withObjectContext(const griddyn::CoreObjectException& exc)
{
    auto who = exc.who();
    if (who.empty()) {
        return exc.what();
    }
    return std::string(exc.what()) + " [" + who + "]";
}

class PySimulation {
  public:
    explicit PySimulation(std::string name = "", std::string type = "default")
    {
        if (!type.empty() && type != "default") {
            throw InvalidParameterError("unsupported simulation type: " + type);
        }
        auto sim = std::make_shared<griddyn::GridDynSimulation>(
            name.empty() ? std::string("gridDynSim_#") : std::move(name));
        runner_ = std::make_shared<griddyn::GriddynRunner>(std::move(sim));
    }

    void load(const nb::object& path, std::string format = "")
    {
        auto filePath = pathToString(path);
        if (!std::filesystem::exists(filePath)) {
            throw FileLoadError("file does not exist: " + filePath);
        }
        auto sim = simulation();
        nb::gil_scoped_release release;
        griddyn::loadFile(sim.get(), filePath, nullptr, std::move(format));
    }

    void initialize()
    {
        nb::gil_scoped_release release;
        runner_->simInitialize();
    }

    void initializeFromString(const std::string& args)
    {
        nb::gil_scoped_release release;
        const auto result = runner_->InitializeFromString(args);
        if (result < 0) {
            throw ExecutionError("simulation initialization failed");
        }
    }

    void initializeFromArgs(const std::vector<std::string>& args)
    {
        std::vector<std::string> ownedArgs;
        ownedArgs.reserve(args.size() + 1);
        ownedArgs.emplace_back("griddyn");
        ownedArgs.insert(ownedArgs.end(), args.begin(), args.end());

        std::vector<char*> argv;
        argv.reserve(ownedArgs.size());
        for (auto& arg : ownedArgs) {
            argv.push_back(arg.data());
        }

        nb::gil_scoped_release release;
        const auto result =
            runner_->Initialize(static_cast<int>(argv.size()), argv.data(), false);
        if (result < 0) {
            throw ExecutionError("simulation initialization failed");
        }
    }

    void powerflow()
    {
        auto sim = simulation();
        nb::gil_scoped_release release;
        const auto result = sim->powerflow();
        if (result < 0) {
            throw SolveError("powerflow failed");
        }
    }

    double run()
    {
        nb::gil_scoped_release release;
        return static_cast<double>(runner_->Run());
    }

    double runUntil(double time)
    {
        auto sim = simulation();
        nb::gil_scoped_release release;
        const auto result = sim->run(griddyn::CoreTime(time));
        if (result < 0) {
            throw SolveError("simulation run failed");
        }
        return static_cast<double>(sim->getSimulationTime());
    }

    double step(double time)
    {
        nb::gil_scoped_release release;
        return static_cast<double>(runner_->Step(griddyn::CoreTime(time)));
    }

    void reset()
    {
        nb::gil_scoped_release release;
        const auto result = runner_->Reset();
        if (result < 0) {
            throw ExecutionError("simulation reset failed");
        }
    }

    double time() const { return static_cast<double>(simulation()->getSimulationTime()); }

    std::string name() const { return simulation()->getName(); }

    void setName(std::string_view name) { simulation()->setName(name); }

  private:
    static std::string pathToString(const nb::object& path)
    {
        auto fspath = nb::module_::import_("os").attr("fspath");
        return nb::cast<std::string>(fspath(path));
    }

    std::shared_ptr<griddyn::GridDynSimulation> simulation() const
    {
        auto sim = runner_->getSim();
        if (!sim) {
            throw InvalidObjectError("simulation is not available");
        }
        return sim;
    }

    std::shared_ptr<griddyn::GriddynRunner> runner_;
};

void setPythonError(PyObject* type, const std::string& message)
{
    PyErr_SetString(type != nullptr ? type : PyExc_RuntimeError, message.c_str());
}

void setPythonError(PyObject* type, const char* message)
{
    PyErr_SetString(type != nullptr ? type : PyExc_RuntimeError, message);
}

void translateGridDynException(const std::exception_ptr& ptr, void* /*payload*/)
{
    try {
        if (ptr) {
            std::rethrow_exception(ptr);
        }
    }
    catch (const griddyn::UnrecognizedObjectException& exc) {
        setPythonError(pyInvalidObjectError, withObjectContext(exc));
    }
    catch (const griddyn::ObjectAddFailure& exc) {
        setPythonError(pyExecutionError, withObjectContext(exc));
    }
    catch (const griddyn::ObjectRemoveFailure& exc) {
        setPythonError(pyExecutionError, withObjectContext(exc));
    }
    catch (const griddyn::UnrecognizedParameter& exc) {
        setPythonError(pyInvalidParameterError, exc.what());
    }
    catch (const griddyn::InvalidParameterValue& exc) {
        setPythonError(pyInvalidParameterError, exc.what());
    }
    catch (const griddyn::FileOperationError& exc) {
        setPythonError(pyFileLoadError, exc.what());
    }
    catch (const griddyn::ExecutionFailure& exc) {
        setPythonError(pyExecutionError, withObjectContext(exc));
    }
    catch (const griddyn::CoreObjectException& exc) {
        setPythonError(pyGridDynError, withObjectContext(exc));
    }
    catch (const std::invalid_argument& exc) {
        setPythonError(pyInvalidParameterError, exc.what());
    }
}

}  // namespace

NB_MODULE(_core, mod)
{
    mod.doc() = "Python bindings for the GridDyn simulation API.";
    mod.attr("__version__") = griddyn::versionString;

    auto gridDynError = nb::exception<GridDynError>(mod, "GridDynError");
    auto invalidObjectError =
        nb::exception<InvalidObjectError>(mod, "InvalidObjectError", gridDynError);
    auto invalidParameterError =
        nb::exception<InvalidParameterError>(mod, "InvalidParameterError", gridDynError);
    auto fileLoadError = nb::exception<FileLoadError>(mod, "FileLoadError", gridDynError);
    auto solveError = nb::exception<SolveError>(mod, "SolveError", gridDynError);
    auto executionError = nb::exception<ExecutionError>(mod, "ExecutionError", gridDynError);

    pyGridDynError = gridDynError.ptr();
    pyInvalidObjectError = invalidObjectError.ptr();
    pyInvalidParameterError = invalidParameterError.ptr();
    pyFileLoadError = fileLoadError.ptr();
    pySolveError = solveError.ptr();
    pyExecutionError = executionError.ptr();

    nb::register_exception_translator(&translateGridDynException);

    nb::class_<PySimulation>(mod, "Simulation")
        .def(nb::init<std::string, std::string>(), "name"_a = "", "type"_a = "default")
        .def("load", &PySimulation::load, "path"_a, "format"_a = "")
        .def("load_file", &PySimulation::load, "path"_a, "format"_a = "")
        .def("initialize", &PySimulation::initialize)
        .def("initialize_from_string", &PySimulation::initializeFromString, "args"_a)
        .def("initialize_from_args", &PySimulation::initializeFromArgs, "args"_a)
        .def("powerflow", &PySimulation::powerflow)
        .def("run", &PySimulation::run)
        .def("run_until", &PySimulation::runUntil, "time"_a)
        .def("run_to", &PySimulation::runUntil, "time"_a)
        .def("step", &PySimulation::step, "time"_a)
        .def("reset", &PySimulation::reset)
        .def_prop_rw("name", &PySimulation::name, &PySimulation::setName)
        .def_prop_ro("time", &PySimulation::time)
        .def(
            "__repr__",
            [](const PySimulation& sim) {
                return "<griddyn.Simulation name='" + sim.name() + "' time=" +
                    std::to_string(sim.time()) + ">";
            });
}
