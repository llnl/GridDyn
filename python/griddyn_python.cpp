/*
 * Copyright (c) 2014-2026, Lawrence Livermore National Security
 * See the top-level NOTICE for additional details. All rights reserved.
 * SPDX-License-Identifier: BSD-3-Clause
 */

#include "core/CoreExceptions.h"
#include "fileInput/fileInput.h"
#include "griddyn/Generator.h"
#include "griddyn/GridArea.h"
#include "griddyn/GridDynSimulation.h"
#include "griddyn/GridBus.h"
#include "griddyn/Link.h"
#include "griddyn/Load.h"
#include "griddyn/Relay.h"
#include "griddyn/gridDynVersion.hpp"
#include "griddyn/relays/Sensor.h"
#include "runner/gridDynRunner.h"
#include <filesystem>
#include <memory>
#include <nanobind/nanobind.h>
#include <nanobind/stl/string.h>
#include <nanobind/stl/vector.h>
#include <stdexcept>
#include <string>
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

std::shared_ptr<griddyn::GridDynSimulation>
    simulationFromRunner(const std::shared_ptr<griddyn::GriddynRunner>& runner)
{
    auto sim = runner->getSim();
    if (!sim) {
        throw InvalidObjectError("simulation is not available");
    }
    return sim;
}

std::size_t normalizeIndex(std::ptrdiff_t index, std::size_t size, const char* typeName)
{
    if (index < 0) {
        index += static_cast<std::ptrdiff_t>(size);
    }
    if (index < 0 || static_cast<std::size_t>(index) >= size) {
        throw nb::index_error((std::string(typeName) + " index out of range").c_str());
    }
    return static_cast<std::size_t>(index);
}

std::string objectName(const griddyn::CoreObject* object)
{
    return object != nullptr ? object->getName() : std::string();
}

std::vector<griddyn::GridBus*>
    busVectorFromRunner(const std::shared_ptr<griddyn::GriddynRunner>& runner)
{
    std::vector<griddyn::GridBus*> buses;
    simulationFromRunner(runner)->getBusVector(buses);
    return buses;
}

std::vector<griddyn::Link*>
    linkVectorFromRunner(const std::shared_ptr<griddyn::GriddynRunner>& runner)
{
    std::vector<griddyn::Link*> links;
    simulationFromRunner(runner)->getLinkVector(links);
    return links;
}

void appendAreaVector(griddyn::GridArea* area, std::vector<griddyn::GridArea*>& areas)
{
    if (area == nullptr) {
        return;
    }
    const auto count = static_cast<std::size_t>(area->getInt("areacount"));
    for (std::size_t index = 0; index < count; ++index) {
        auto* subArea = area->getArea(static_cast<index_t>(index));
        if (subArea != nullptr) {
            areas.push_back(subArea);
            appendAreaVector(subArea, areas);
        }
    }
}

std::vector<griddyn::GridArea*>
    areaVectorFromRunner(const std::shared_ptr<griddyn::GriddynRunner>& runner)
{
    std::vector<griddyn::GridArea*> areas;
    appendAreaVector(simulationFromRunner(runner).get(), areas);
    return areas;
}

void appendRelayVector(griddyn::GridArea* area, std::vector<griddyn::Relay*>& relays)
{
    if (area == nullptr) {
        return;
    }
    const auto relayCount = static_cast<std::size_t>(area->getInt("relaycount"));
    for (std::size_t index = 0; index < relayCount; ++index) {
        auto* relay = area->getRelay(static_cast<index_t>(index));
        if (relay != nullptr) {
            relays.push_back(relay);
        }
    }
    const auto areaCount = static_cast<std::size_t>(area->getInt("areacount"));
    for (std::size_t index = 0; index < areaCount; ++index) {
        appendRelayVector(area->getArea(static_cast<index_t>(index)), relays);
    }
}

std::vector<griddyn::Relay*>
    relayVectorFromRunner(const std::shared_ptr<griddyn::GriddynRunner>& runner)
{
    std::vector<griddyn::Relay*> relays;
    appendRelayVector(simulationFromRunner(runner).get(), relays);
    return relays;
}

std::vector<griddyn::Sensor*>
    sensorVectorFromRunner(const std::shared_ptr<griddyn::GriddynRunner>& runner)
{
    std::vector<griddyn::Sensor*> sensors;
    for (auto* relay : relayVectorFromRunner(runner)) {
        if (auto* sensor = dynamic_cast<griddyn::Sensor*>(relay); sensor != nullptr) {
            sensors.push_back(sensor);
        }
    }
    return sensors;
}

struct SecondaryWithBus {
    griddyn::GridBus* bus = nullptr;
    griddyn::GridSecondary* object = nullptr;
};

std::vector<SecondaryWithBus>
    generatorVectorFromRunner(const std::shared_ptr<griddyn::GriddynRunner>& runner)
{
    std::vector<SecondaryWithBus> generators;
    for (auto* bus : busVectorFromRunner(runner)) {
        if (bus == nullptr) {
            continue;
        }
        const auto count = static_cast<std::size_t>(bus->getInt("gencount"));
        for (std::size_t index = 0; index < count; ++index) {
            auto* gen = bus->getGen(static_cast<index_t>(index));
            if (gen != nullptr) {
                generators.push_back({bus, gen});
            }
        }
    }
    return generators;
}

std::vector<SecondaryWithBus>
    loadVectorFromRunner(const std::shared_ptr<griddyn::GriddynRunner>& runner)
{
    std::vector<SecondaryWithBus> loads;
    for (auto* bus : busVectorFromRunner(runner)) {
        if (bus == nullptr) {
            continue;
        }
        const auto count = static_cast<std::size_t>(bus->getInt("loadcount"));
        for (std::size_t index = 0; index < count; ++index) {
            auto* load = bus->getLoad(static_cast<index_t>(index));
            if (load != nullptr) {
                loads.push_back({bus, load});
            }
        }
    }
    return loads;
}

nb::object dataframeFromDicts(const nb::list& rows)
{
    auto pandas = nb::module_::import_("pandas");
    return pandas.attr("DataFrame")(rows);
}

void setObjectParameter(griddyn::CoreObject* object, const std::string& field, const nb::object& value)
{
    if (object == nullptr) {
        throw InvalidObjectError("object is not available");
    }
    if (PyUnicode_Check(value.ptr())) {
        object->set(field, nb::cast<std::string>(value));
        return;
    }
    if (PyBool_Check(value.ptr())) {
        object->set(field, PyObject_IsTrue(value.ptr()) ? 1.0 : 0.0);
        return;
    }
    if (PyFloat_Check(value.ptr()) || PyLong_Check(value.ptr())) {
        object->set(field, nb::cast<double>(value));
        return;
    }
    throw nb::type_error("set value must be a string, bool, int, or float");
}

nb::object modelFromObject(const std::shared_ptr<griddyn::GriddynRunner>& runner,
                           griddyn::CoreObject* object);

class PyModel {
  public:
    PyModel(std::shared_ptr<griddyn::GriddynRunner> runner,
            griddyn::CoreObject* object,
            std::string type):
        runner_(std::move(runner)), object_(object), type_(std::move(type))
    {
    }

    std::string name() const { return objectName(object_); }
    std::string type() const { return type_; }
    int userId() const { return object_->getUserID(); }
    bool enabled() const { return object_->isEnabled(); }
    std::string description() const { return object_->getDescription(); }
    double get(const std::string& field) const { return object_->get(field); }
    PyModel& set(const std::string& field, const nb::object& value)
    {
        setObjectParameter(object_, field, value);
        return *this;
    }
    std::string getString(const std::string& field) const { return object_->getString(field); }
    nb::object find(const std::string& name) const { return modelFromObject(runner_, object_->find(name)); }

    nb::dict asDict() const
    {
        nb::dict row;
        row["name"] = name();
        row["type"] = type();
        row["uid"] = userId();
        row["enabled"] = enabled();
        return row;
    }

  private:
    std::shared_ptr<griddyn::GriddynRunner> runner_;
    griddyn::CoreObject* object_ = nullptr;
    std::string type_;
};

class PyBus {
  public:
    PyBus(std::shared_ptr<griddyn::GriddynRunner> runner, griddyn::GridBus* bus):
        runner_(std::move(runner)), bus_(bus)
    {
    }

    std::string name() const { return objectName(bus_); }
    std::string type() const { return "bus"; }
    int userId() const { return bus_->getUserID(); }
    double voltage() const { return bus_->getVoltage(); }
    double angle() const { return bus_->getAngle(); }
    double frequency() const { return bus_->getFreq(); }
    double generationP() const { return bus_->getGenerationReal(); }
    double generationQ() const { return bus_->getGenerationReactive(); }
    double loadP() const { return bus_->getLoadReal(); }
    double loadQ() const { return bus_->getLoadReactive(); }
    double linkP() const { return bus_->getLinkReal(); }
    double linkQ() const { return bus_->getLinkReactive(); }
    double get(const std::string& field) const { return bus_->get(field); }
    PyBus& set(const std::string& field, const nb::object& value)
    {
        setObjectParameter(bus_, field, value);
        return *this;
    }
    std::string getString(const std::string& field) const { return bus_->getString(field); }
    nb::object find(const std::string& name) const { return modelFromObject(runner_, bus_->find(name)); }

    nb::dict asDict() const
    {
        nb::dict row;
        row["name"] = name();
        row["type"] = type();
        row["uid"] = userId();
        row["v"] = voltage();
        row["a"] = angle();
        row["f"] = frequency();
        row["p_gen"] = generationP();
        row["q_gen"] = generationQ();
        row["p_load"] = loadP();
        row["q_load"] = loadQ();
        row["p_link"] = linkP();
        row["q_link"] = linkQ();
        return row;
    }

  private:
    std::shared_ptr<griddyn::GriddynRunner> runner_;
    griddyn::GridBus* bus_ = nullptr;
};

class PyGenerator {
  public:
    PyGenerator(std::shared_ptr<griddyn::GriddynRunner> runner,
                griddyn::GridBus* bus,
                griddyn::Generator* generator):
        runner_(std::move(runner)), bus_(bus), generator_(generator)
    {
    }

    std::string name() const { return objectName(generator_); }
    std::string type() const { return "generator"; }
    std::string bus() const { return objectName(bus_); }
    int userId() const { return generator_->getUserID(); }
    double p() const { return generator_->getRealPower(); }
    double q() const { return generator_->getReactivePower(); }
    double pset() const { return generator_->getPset(); }
    double pmax() const { return generator_->getPmax(); }
    double pmin() const { return generator_->getPmin(); }
    double qmax() const { return generator_->getQmax(); }
    double qmin() const { return generator_->getQmin(); }
    double get(const std::string& field) const { return generator_->get(field); }
    PyGenerator& set(const std::string& field, const nb::object& value)
    {
        setObjectParameter(generator_, field, value);
        return *this;
    }
    std::string getString(const std::string& field) const { return generator_->getString(field); }

    nb::dict asDict() const
    {
        nb::dict row;
        row["name"] = name();
        row["type"] = type();
        row["uid"] = userId();
        row["bus"] = bus();
        row["p"] = p();
        row["q"] = q();
        row["pset"] = pset();
        row["pmax"] = pmax();
        row["pmin"] = pmin();
        row["qmax"] = qmax();
        row["qmin"] = qmin();
        return row;
    }

  private:
    std::shared_ptr<griddyn::GriddynRunner> runner_;
    griddyn::GridBus* bus_ = nullptr;
    griddyn::Generator* generator_ = nullptr;
};

class PyLoad {
  public:
    PyLoad(std::shared_ptr<griddyn::GriddynRunner> runner,
           griddyn::GridBus* bus,
           griddyn::GridLoad* load):
        runner_(std::move(runner)), bus_(bus), load_(load)
    {
    }

    std::string name() const { return objectName(load_); }
    std::string type() const { return "load"; }
    std::string bus() const { return objectName(bus_); }
    int userId() const { return load_->getUserID(); }
    double p() const { return load_->getRealPower(); }
    double q() const { return load_->getReactivePower(); }
    double get(const std::string& field) const { return load_->get(field); }
    PyLoad& set(const std::string& field, const nb::object& value)
    {
        setObjectParameter(load_, field, value);
        return *this;
    }
    std::string getString(const std::string& field) const { return load_->getString(field); }

    nb::dict asDict() const
    {
        nb::dict row;
        row["name"] = name();
        row["type"] = type();
        row["uid"] = userId();
        row["bus"] = bus();
        row["p"] = p();
        row["q"] = q();
        return row;
    }

  private:
    std::shared_ptr<griddyn::GriddynRunner> runner_;
    griddyn::GridBus* bus_ = nullptr;
    griddyn::GridLoad* load_ = nullptr;
};

class PyLink {
  public:
    PyLink(std::shared_ptr<griddyn::GriddynRunner> runner, griddyn::Link* link):
        runner_(std::move(runner)), link_(link)
    {
    }

    std::string name() const { return objectName(link_); }
    std::string type() const { return "link"; }
    int userId() const { return link_->getUserID(); }
    std::string bus1() const { return objectName(link_->getBus(1)); }
    std::string bus2() const { return objectName(link_->getBus(2)); }
    double p1() const { return link_->getRealPower(1); }
    double q1() const { return link_->getReactivePower(1); }
    double p2() const { return link_->getRealPower(2); }
    double q2() const { return link_->getReactivePower(2); }
    double loss() const { return link_->getLoss(); }
    double reactiveLoss() const { return link_->getReactiveLoss(); }
    double get(const std::string& field) const { return link_->get(field); }
    PyLink& set(const std::string& field, const nb::object& value)
    {
        setObjectParameter(link_, field, value);
        return *this;
    }
    std::string getString(const std::string& field) const { return link_->getString(field); }

    nb::dict asDict() const
    {
        nb::dict row;
        row["name"] = name();
        row["type"] = type();
        row["uid"] = userId();
        row["bus1"] = bus1();
        row["bus2"] = bus2();
        row["p1"] = p1();
        row["q1"] = q1();
        row["p2"] = p2();
        row["q2"] = q2();
        row["loss"] = loss();
        row["q_loss"] = reactiveLoss();
        return row;
    }

  private:
    std::shared_ptr<griddyn::GriddynRunner> runner_;
    griddyn::Link* link_ = nullptr;
};

class PyArea {
  public:
    PyArea(std::shared_ptr<griddyn::GriddynRunner> runner, griddyn::GridArea* area):
        runner_(std::move(runner)), area_(area)
    {
    }

    std::string name() const { return objectName(area_); }
    std::string type() const { return "area"; }
    int userId() const { return area_->getUserID(); }
    bool enabled() const { return area_->isEnabled(); }
    int busCount() const { return area_->getInt("buscount"); }
    int linkCount() const { return area_->getInt("linkcount"); }
    int areaCount() const { return area_->getInt("areacount"); }
    int relayCount() const { return area_->getInt("relaycount"); }
    int generatorCount() const { return area_->getInt("gencount"); }
    int loadCount() const { return area_->getInt("loadcount"); }
    double generationP() const { return area_->getGenerationReal(); }
    double generationQ() const { return area_->getGenerationReactive(); }
    double loadP() const { return area_->getLoadReal(); }
    double loadQ() const { return area_->getLoadReactive(); }
    double loss() const { return area_->getLoss(); }
    double averageFrequency() const { return area_->getAvgFreq(); }
    double averageAngle() const { return area_->getAvgAngle(); }
    double tieP() const { return area_->getTieFlowReal(); }
    double get(const std::string& field) const { return area_->get(field); }
    PyArea& set(const std::string& field, const nb::object& value)
    {
        setObjectParameter(area_, field, value);
        return *this;
    }
    std::string getString(const std::string& field) const { return area_->getString(field); }
    nb::object find(const std::string& name) const
    {
        return modelFromObject(runner_, area_->find(name));
    }

    nb::dict asDict() const
    {
        nb::dict row;
        row["name"] = name();
        row["type"] = type();
        row["uid"] = userId();
        row["enabled"] = enabled();
        row["bus_count"] = busCount();
        row["link_count"] = linkCount();
        row["area_count"] = areaCount();
        row["relay_count"] = relayCount();
        row["gen_count"] = generatorCount();
        row["load_count"] = loadCount();
        row["p_gen"] = generationP();
        row["q_gen"] = generationQ();
        row["p_load"] = loadP();
        row["q_load"] = loadQ();
        row["loss"] = loss();
        row["avg_f"] = averageFrequency();
        row["avg_a"] = averageAngle();
        row["tie_p"] = tieP();
        return row;
    }

  private:
    std::shared_ptr<griddyn::GriddynRunner> runner_;
    griddyn::GridArea* area_ = nullptr;
};

class PyRelay {
  public:
    PyRelay(std::shared_ptr<griddyn::GriddynRunner> runner, griddyn::Relay* relay):
        runner_(std::move(runner)), relay_(relay)
    {
    }

    std::string name() const { return objectName(relay_); }
    std::string type() const { return "relay"; }
    int userId() const { return relay_->getUserID(); }
    bool enabled() const { return relay_->isEnabled(); }
    double get(const std::string& field) const { return relay_->get(field); }
    PyRelay& set(const std::string& field, const nb::object& value)
    {
        setObjectParameter(relay_, field, value);
        return *this;
    }
    std::string getString(const std::string& field) const { return relay_->getString(field); }

    nb::dict asDict() const
    {
        nb::dict row;
        row["name"] = name();
        row["type"] = type();
        row["uid"] = userId();
        row["enabled"] = enabled();
        return row;
    }

  protected:
    std::shared_ptr<griddyn::GriddynRunner> runner_;
    griddyn::Relay* relay_ = nullptr;
};

class PySensor {
  public:
    PySensor(std::shared_ptr<griddyn::GriddynRunner> runner, griddyn::Sensor* sensor):
        runner_(std::move(runner)), sensor_(sensor)
    {
    }

    std::string name() const { return objectName(sensor_); }
    std::string type() const { return "sensor"; }
    int userId() const { return sensor_->getUserID(); }
    bool enabled() const { return sensor_->isEnabled(); }
    double output(std::ptrdiff_t index = 0) const
    {
        if (index < 0) {
            throw nb::index_error("sensor output index out of range");
        }
        return sensor_->getOutput(static_cast<index_t>(index));
    }
    double get(const std::string& field) const { return sensor_->get(field); }
    PySensor& set(const std::string& field, const nb::object& value)
    {
        setObjectParameter(sensor_, field, value);
        return *this;
    }
    std::string getString(const std::string& field) const { return sensor_->getString(field); }

    nb::dict asDict() const
    {
        nb::dict row;
        row["name"] = name();
        row["type"] = type();
        row["uid"] = userId();
        row["enabled"] = enabled();
        return row;
    }

  private:
    std::shared_ptr<griddyn::GriddynRunner> runner_;
    griddyn::Sensor* sensor_ = nullptr;
};

nb::object modelFromObject(const std::shared_ptr<griddyn::GriddynRunner>& runner,
                           griddyn::CoreObject* object)
{
    if (object == nullptr) {
        return nb::none();
    }
    if (auto* sensor = dynamic_cast<griddyn::Sensor*>(object); sensor != nullptr) {
        return nb::cast(PySensor(runner, sensor));
    }
    if (auto* relay = dynamic_cast<griddyn::Relay*>(object); relay != nullptr) {
        return nb::cast(PyRelay(runner, relay));
    }
    if (auto* bus = dynamic_cast<griddyn::GridBus*>(object); bus != nullptr) {
        return nb::cast(PyBus(runner, bus));
    }
    if (auto* generator = dynamic_cast<griddyn::Generator*>(object); generator != nullptr) {
        const auto* secondary = static_cast<griddyn::GridSecondary*>(generator);
        return nb::cast(
            PyGenerator(runner, const_cast<griddyn::GridBus*>(secondary->getBus()), generator));
    }
    if (auto* load = dynamic_cast<griddyn::GridLoad*>(object); load != nullptr) {
        const auto* secondary = static_cast<griddyn::GridSecondary*>(load);
        return nb::cast(PyLoad(runner, const_cast<griddyn::GridBus*>(secondary->getBus()), load));
    }
    if (auto* link = dynamic_cast<griddyn::Link*>(object); link != nullptr) {
        return nb::cast(PyLink(runner, link));
    }
    if (auto* area = dynamic_cast<griddyn::GridArea*>(object); area != nullptr) {
        return nb::cast(PyArea(runner, area));
    }
    return nb::cast(PyModel(runner, object, "object"));
}

template<class Item>
nb::list namesFromItems(const std::vector<Item>& items)
{
    nb::list names;
    for (const auto& item : items) {
        names.append(objectName(item));
    }
    return names;
}

template<class Item>
std::size_t findItemByName(const std::vector<Item>& items, const std::string& name, const char* typeName)
{
    for (std::size_t index = 0; index < items.size(); ++index) {
        if (objectName(items[index]) == name) {
            return index;
        }
    }
    throw nb::key_error((std::string(typeName) + " not found: " + name).c_str());
}

std::size_t findSecondaryByName(
    const std::vector<SecondaryWithBus>& items, const std::string& name, const char* typeName)
{
    for (std::size_t index = 0; index < items.size(); ++index) {
        if (objectName(items[index].object) == name) {
            return index;
        }
    }
    throw nb::key_error((std::string(typeName) + " not found: " + name).c_str());
}

class PyBusCollection {
  public:
    explicit PyBusCollection(std::shared_ptr<griddyn::GriddynRunner> runner):
        runner_(std::move(runner))
    {
    }

    std::size_t size() const { return busVectorFromRunner(runner_).size(); }
    nb::list names() const { return namesFromItems(busVectorFromRunner(runner_)); }

    PyBus getByIndex(std::ptrdiff_t index) const
    {
        auto buses = busVectorFromRunner(runner_);
        return PyBus(runner_, buses[normalizeIndex(index, buses.size(), "bus")]);
    }

    PyBus getByName(const std::string& name) const
    {
        auto buses = busVectorFromRunner(runner_);
        return PyBus(runner_, buses[findItemByName(buses, name, "bus")]);
    }

    PyBus getItem(const nb::object& key) const
    {
        if (nb::isinstance<nb::int_>(key)) {
            return getByIndex(nb::cast<std::ptrdiff_t>(key));
        }
        if (nb::isinstance<nb::str>(key)) {
            return getByName(nb::cast<std::string>(key));
        }
        throw nb::type_error("bus indices must be integers or names");
    }

    nb::list asDicts() const
    {
        nb::list rows;
        for (auto* bus : busVectorFromRunner(runner_)) {
            rows.append(PyBus(runner_, bus).asDict());
        }
        return rows;
    }

    nb::object asDataFrame() const { return dataframeFromDicts(asDicts()); }

  private:
    std::shared_ptr<griddyn::GriddynRunner> runner_;
};

class PyGeneratorCollection {
  public:
    explicit PyGeneratorCollection(std::shared_ptr<griddyn::GriddynRunner> runner):
        runner_(std::move(runner))
    {
    }

    std::size_t size() const { return generatorVectorFromRunner(runner_).size(); }

    nb::list names() const
    {
        nb::list names;
        for (const auto& item : generatorVectorFromRunner(runner_)) {
            names.append(objectName(item.object));
        }
        return names;
    }

    PyGenerator getByIndex(std::ptrdiff_t index) const
    {
        auto generators = generatorVectorFromRunner(runner_);
        const auto normalized = normalizeIndex(index, generators.size(), "generator");
        auto item = generators[normalized];
        return PyGenerator(runner_, item.bus, static_cast<griddyn::Generator*>(item.object));
    }

    PyGenerator getByName(const std::string& name) const
    {
        auto generators = generatorVectorFromRunner(runner_);
        auto item = generators[findSecondaryByName(generators, name, "generator")];
        return PyGenerator(runner_, item.bus, static_cast<griddyn::Generator*>(item.object));
    }

    PyGenerator getItem(const nb::object& key) const
    {
        if (nb::isinstance<nb::int_>(key)) {
            return getByIndex(nb::cast<std::ptrdiff_t>(key));
        }
        if (nb::isinstance<nb::str>(key)) {
            return getByName(nb::cast<std::string>(key));
        }
        throw nb::type_error("generator indices must be integers or names");
    }

    nb::list asDicts() const
    {
        nb::list rows;
        for (const auto& item : generatorVectorFromRunner(runner_)) {
            rows.append(
                PyGenerator(runner_, item.bus, static_cast<griddyn::Generator*>(item.object))
                    .asDict());
        }
        return rows;
    }

    nb::object asDataFrame() const { return dataframeFromDicts(asDicts()); }

  private:
    std::shared_ptr<griddyn::GriddynRunner> runner_;
};

class PyLoadCollection {
  public:
    explicit PyLoadCollection(std::shared_ptr<griddyn::GriddynRunner> runner):
        runner_(std::move(runner))
    {
    }

    std::size_t size() const { return loadVectorFromRunner(runner_).size(); }

    nb::list names() const
    {
        nb::list names;
        for (const auto& item : loadVectorFromRunner(runner_)) {
            names.append(objectName(item.object));
        }
        return names;
    }

    PyLoad getByIndex(std::ptrdiff_t index) const
    {
        auto loads = loadVectorFromRunner(runner_);
        const auto normalized = normalizeIndex(index, loads.size(), "load");
        auto item = loads[normalized];
        return PyLoad(runner_, item.bus, static_cast<griddyn::GridLoad*>(item.object));
    }

    PyLoad getByName(const std::string& name) const
    {
        auto loads = loadVectorFromRunner(runner_);
        auto item = loads[findSecondaryByName(loads, name, "load")];
        return PyLoad(runner_, item.bus, static_cast<griddyn::GridLoad*>(item.object));
    }

    PyLoad getItem(const nb::object& key) const
    {
        if (nb::isinstance<nb::int_>(key)) {
            return getByIndex(nb::cast<std::ptrdiff_t>(key));
        }
        if (nb::isinstance<nb::str>(key)) {
            return getByName(nb::cast<std::string>(key));
        }
        throw nb::type_error("load indices must be integers or names");
    }

    nb::list asDicts() const
    {
        nb::list rows;
        for (const auto& item : loadVectorFromRunner(runner_)) {
            rows.append(
                PyLoad(runner_, item.bus, static_cast<griddyn::GridLoad*>(item.object)).asDict());
        }
        return rows;
    }

    nb::object asDataFrame() const { return dataframeFromDicts(asDicts()); }

  private:
    std::shared_ptr<griddyn::GriddynRunner> runner_;
};

class PyLinkCollection {
  public:
    explicit PyLinkCollection(std::shared_ptr<griddyn::GriddynRunner> runner):
        runner_(std::move(runner))
    {
    }

    std::size_t size() const { return linkVectorFromRunner(runner_).size(); }
    nb::list names() const { return namesFromItems(linkVectorFromRunner(runner_)); }

    PyLink getByIndex(std::ptrdiff_t index) const
    {
        auto links = linkVectorFromRunner(runner_);
        return PyLink(runner_, links[normalizeIndex(index, links.size(), "link")]);
    }

    PyLink getByName(const std::string& name) const
    {
        auto links = linkVectorFromRunner(runner_);
        return PyLink(runner_, links[findItemByName(links, name, "link")]);
    }

    PyLink getItem(const nb::object& key) const
    {
        if (nb::isinstance<nb::int_>(key)) {
            return getByIndex(nb::cast<std::ptrdiff_t>(key));
        }
        if (nb::isinstance<nb::str>(key)) {
            return getByName(nb::cast<std::string>(key));
        }
        throw nb::type_error("link indices must be integers or names");
    }

    nb::list asDicts() const
    {
        nb::list rows;
        for (auto* link : linkVectorFromRunner(runner_)) {
            rows.append(PyLink(runner_, link).asDict());
        }
        return rows;
    }

    nb::object asDataFrame() const { return dataframeFromDicts(asDicts()); }

  private:
    std::shared_ptr<griddyn::GriddynRunner> runner_;
};

class PyAreaCollection {
  public:
    explicit PyAreaCollection(std::shared_ptr<griddyn::GriddynRunner> runner):
        runner_(std::move(runner))
    {
    }

    std::size_t size() const { return areaVectorFromRunner(runner_).size(); }
    nb::list names() const { return namesFromItems(areaVectorFromRunner(runner_)); }

    PyArea getByIndex(std::ptrdiff_t index) const
    {
        auto areas = areaVectorFromRunner(runner_);
        return PyArea(runner_, areas[normalizeIndex(index, areas.size(), "area")]);
    }

    PyArea getByName(const std::string& name) const
    {
        auto areas = areaVectorFromRunner(runner_);
        return PyArea(runner_, areas[findItemByName(areas, name, "area")]);
    }

    PyArea getItem(const nb::object& key) const
    {
        if (nb::isinstance<nb::int_>(key)) {
            return getByIndex(nb::cast<std::ptrdiff_t>(key));
        }
        if (nb::isinstance<nb::str>(key)) {
            return getByName(nb::cast<std::string>(key));
        }
        throw nb::type_error("area indices must be integers or names");
    }

    nb::list asDicts() const
    {
        nb::list rows;
        for (auto* area : areaVectorFromRunner(runner_)) {
            rows.append(PyArea(runner_, area).asDict());
        }
        return rows;
    }

    nb::object asDataFrame() const { return dataframeFromDicts(asDicts()); }

  private:
    std::shared_ptr<griddyn::GriddynRunner> runner_;
};

class PyRelayCollection {
  public:
    explicit PyRelayCollection(std::shared_ptr<griddyn::GriddynRunner> runner):
        runner_(std::move(runner))
    {
    }

    std::size_t size() const { return relayVectorFromRunner(runner_).size(); }
    nb::list names() const { return namesFromItems(relayVectorFromRunner(runner_)); }

    PyRelay getByIndex(std::ptrdiff_t index) const
    {
        auto relays = relayVectorFromRunner(runner_);
        return PyRelay(runner_, relays[normalizeIndex(index, relays.size(), "relay")]);
    }

    PyRelay getByName(const std::string& name) const
    {
        auto relays = relayVectorFromRunner(runner_);
        return PyRelay(runner_, relays[findItemByName(relays, name, "relay")]);
    }

    PyRelay getItem(const nb::object& key) const
    {
        if (nb::isinstance<nb::int_>(key)) {
            return getByIndex(nb::cast<std::ptrdiff_t>(key));
        }
        if (nb::isinstance<nb::str>(key)) {
            return getByName(nb::cast<std::string>(key));
        }
        throw nb::type_error("relay indices must be integers or names");
    }

    nb::list asDicts() const
    {
        nb::list rows;
        for (auto* relay : relayVectorFromRunner(runner_)) {
            rows.append(PyRelay(runner_, relay).asDict());
        }
        return rows;
    }

    nb::object asDataFrame() const { return dataframeFromDicts(asDicts()); }

  private:
    std::shared_ptr<griddyn::GriddynRunner> runner_;
};

class PySensorCollection {
  public:
    explicit PySensorCollection(std::shared_ptr<griddyn::GriddynRunner> runner):
        runner_(std::move(runner))
    {
    }

    std::size_t size() const { return sensorVectorFromRunner(runner_).size(); }
    nb::list names() const { return namesFromItems(sensorVectorFromRunner(runner_)); }

    PySensor getByIndex(std::ptrdiff_t index) const
    {
        auto sensors = sensorVectorFromRunner(runner_);
        return PySensor(runner_, sensors[normalizeIndex(index, sensors.size(), "sensor")]);
    }

    PySensor getByName(const std::string& name) const
    {
        auto sensors = sensorVectorFromRunner(runner_);
        return PySensor(runner_, sensors[findItemByName(sensors, name, "sensor")]);
    }

    PySensor getItem(const nb::object& key) const
    {
        if (nb::isinstance<nb::int_>(key)) {
            return getByIndex(nb::cast<std::ptrdiff_t>(key));
        }
        if (nb::isinstance<nb::str>(key)) {
            return getByName(nb::cast<std::string>(key));
        }
        throw nb::type_error("sensor indices must be integers or names");
    }

    nb::list asDicts() const
    {
        nb::list rows;
        for (auto* sensor : sensorVectorFromRunner(runner_)) {
            rows.append(PySensor(runner_, sensor).asDict());
        }
        return rows;
    }

    nb::object asDataFrame() const { return dataframeFromDicts(asDicts()); }

  private:
    std::shared_ptr<griddyn::GriddynRunner> runner_;
};

class PyPowerFlowRoutine {
  public:
    explicit PyPowerFlowRoutine(std::shared_ptr<griddyn::GriddynRunner> runner):
        runner_(std::move(runner))
    {
    }

    void run()
    {
        auto sim = simulationFromRunner(runner_);
        nb::gil_scoped_release release;
        const auto result = sim->powerflow();
        if (result < 0) {
            throw SolveError("powerflow failed");
        }
    }

  private:
    std::shared_ptr<griddyn::GriddynRunner> runner_;
};

class PyTimeDomainRoutine {
  public:
    explicit PyTimeDomainRoutine(std::shared_ptr<griddyn::GriddynRunner> runner):
        runner_(std::move(runner))
    {
    }

    void initialize()
    {
        nb::gil_scoped_release release;
        runner_->simInitialize();
    }

    double run()
    {
        nb::gil_scoped_release release;
        return static_cast<double>(runner_->Run());
    }

    double runUntil(double time)
    {
        auto sim = simulationFromRunner(runner_);
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

    double time() const
    {
        return static_cast<double>(simulationFromRunner(runner_)->getSimulationTime());
    }

  private:
    std::shared_ptr<griddyn::GriddynRunner> runner_;
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

    static PySimulation
        fromFile(const nb::object& path, std::string format = "", std::string name = "")
    {
        PySimulation sim(std::move(name));
        sim.load(path, std::move(format));
        return sim;
    }

    PySimulation& load(const nb::object& path, std::string format = "")
    {
        auto filePath = pathToString(path);
        if (!std::filesystem::exists(filePath)) {
            throw FileLoadError("file does not exist: " + filePath);
        }
        auto sim = simulation();
        nb::gil_scoped_release release;
        griddyn::loadFile(sim.get(), filePath, nullptr, std::move(format));
        return *this;
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
        const auto result = runner_->Initialize(static_cast<int>(argv.size()), argv.data(), false);
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

    void setName(const std::string& name) { simulation()->setName(name); }

    double get(const std::string& field) const { return simulation()->get(field); }

    PySimulation& set(const std::string& field, const nb::object& value)
    {
        setObjectParameter(simulation().get(), field, value);
        return *this;
    }

    std::string getString(const std::string& field) const { return simulation()->getString(field); }

    nb::object find(const std::string& name) const
    {
        auto sim = simulation();
        return modelFromObject(runner_, sim->find(name));
    }

    PyPowerFlowRoutine powerFlowRoutine() const { return PyPowerFlowRoutine(runner_); }

    PyTimeDomainRoutine timeDomainRoutine() const { return PyTimeDomainRoutine(runner_); }

    PyBusCollection busCollection() const { return PyBusCollection(runner_); }

    PyGeneratorCollection generatorCollection() const { return PyGeneratorCollection(runner_); }

    PyLoadCollection loadCollection() const { return PyLoadCollection(runner_); }

    PyLinkCollection linkCollection() const { return PyLinkCollection(runner_); }

    PyAreaCollection areaCollection() const { return PyAreaCollection(runner_); }

    PyRelayCollection relayCollection() const { return PyRelayCollection(runner_); }

    PySensorCollection sensorCollection() const { return PySensorCollection(runner_); }

  private:
    static std::string pathToString(const nb::object& path)
    {
        auto fspath = nb::module_::import_("os").attr("fspath");
        return nb::cast<std::string>(fspath(path));
    }

    std::shared_ptr<griddyn::GridDynSimulation> simulation() const
    {
        return simulationFromRunner(runner_);
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

    nb::class_<PyPowerFlowRoutine>(mod, "PowerFlowRoutine")
        .def("run", &PyPowerFlowRoutine::run)
        .def("__repr__", [](const PyPowerFlowRoutine&) { return "<griddyn.PowerFlowRoutine>"; });

    nb::class_<PyTimeDomainRoutine>(mod, "TimeDomainRoutine")
        .def("initialize", &PyTimeDomainRoutine::initialize)
        .def("init", &PyTimeDomainRoutine::initialize)
        .def("run", &PyTimeDomainRoutine::run)
        .def("run_until", &PyTimeDomainRoutine::runUntil, "time"_a)
        .def("run_to", &PyTimeDomainRoutine::runUntil, "time"_a)
        .def("step", &PyTimeDomainRoutine::step, "time"_a)
        .def_prop_ro("time", &PyTimeDomainRoutine::time)
        .def("__repr__", [](const PyTimeDomainRoutine& routine) {
            return "<griddyn.TimeDomainRoutine time=" + std::to_string(routine.time()) + ">";
        });

    nb::class_<PyModel>(mod, "Model")
        .def_prop_ro("name", &PyModel::name)
        .def_prop_ro("type", &PyModel::type)
        .def_prop_ro("uid", &PyModel::userId)
        .def_prop_ro("enabled", &PyModel::enabled)
        .def_prop_ro("description", &PyModel::description)
        .def("get", &PyModel::get, "field"_a)
        .def("set", &PyModel::set, "field"_a, "value"_a, nb::rv_policy::reference_internal)
        .def("get_string", &PyModel::getString, "field"_a)
        .def("find", &PyModel::find, "name"_a)
        .def("as_dict", &PyModel::asDict)
        .def("__repr__", [](const PyModel& model) {
            return "<griddyn.Model name='" + model.name() + "' type='" + model.type() + "'>";
        });

    nb::class_<PyBus>(mod, "Bus")
        .def_prop_ro("name", &PyBus::name)
        .def_prop_ro("type", &PyBus::type)
        .def_prop_ro("uid", &PyBus::userId)
        .def_prop_ro("v", &PyBus::voltage)
        .def_prop_ro("voltage", &PyBus::voltage)
        .def_prop_ro("a", &PyBus::angle)
        .def_prop_ro("angle", &PyBus::angle)
        .def_prop_ro("f", &PyBus::frequency)
        .def_prop_ro("frequency", &PyBus::frequency)
        .def_prop_ro("p_gen", &PyBus::generationP)
        .def_prop_ro("q_gen", &PyBus::generationQ)
        .def_prop_ro("p_load", &PyBus::loadP)
        .def_prop_ro("q_load", &PyBus::loadQ)
        .def_prop_ro("p_link", &PyBus::linkP)
        .def_prop_ro("q_link", &PyBus::linkQ)
        .def("get", &PyBus::get, "field"_a)
        .def("set", &PyBus::set, "field"_a, "value"_a, nb::rv_policy::reference_internal)
        .def("get_string", &PyBus::getString, "field"_a)
        .def("find", &PyBus::find, "name"_a)
        .def("as_dict", &PyBus::asDict)
        .def("__repr__", [](const PyBus& bus) {
            return "<griddyn.Bus name='" + bus.name() + "' v=" + std::to_string(bus.voltage()) +
                " a=" + std::to_string(bus.angle()) + ">";
        });

    auto generatorClass = nb::class_<PyGenerator>(mod, "Generator")
        .def_prop_ro("name", &PyGenerator::name)
        .def_prop_ro("type", &PyGenerator::type)
        .def_prop_ro("uid", &PyGenerator::userId)
        .def_prop_ro("bus", &PyGenerator::bus)
        .def_prop_ro("p", &PyGenerator::p)
        .def_prop_ro("q", &PyGenerator::q)
        .def_prop_ro("pset", &PyGenerator::pset)
        .def_prop_ro("pmax", &PyGenerator::pmax)
        .def_prop_ro("pmin", &PyGenerator::pmin)
        .def_prop_ro("qmax", &PyGenerator::qmax)
        .def_prop_ro("qmin", &PyGenerator::qmin)
        .def("get", &PyGenerator::get, "field"_a)
        .def("set", &PyGenerator::set, "field"_a, "value"_a, nb::rv_policy::reference_internal)
        .def("get_string", &PyGenerator::getString, "field"_a)
        .def("as_dict", &PyGenerator::asDict)
        .def("__repr__", [](const PyGenerator& gen) {
            return "<griddyn.Generator name='" + gen.name() + "' bus='" + gen.bus() +
                "' p=" + std::to_string(gen.p()) + " q=" + std::to_string(gen.q()) + ">";
        });
    mod.attr("Gen") = generatorClass;

    nb::class_<PyLoad>(mod, "Load")
        .def_prop_ro("name", &PyLoad::name)
        .def_prop_ro("type", &PyLoad::type)
        .def_prop_ro("uid", &PyLoad::userId)
        .def_prop_ro("bus", &PyLoad::bus)
        .def_prop_ro("p", &PyLoad::p)
        .def_prop_ro("q", &PyLoad::q)
        .def("get", &PyLoad::get, "field"_a)
        .def("set", &PyLoad::set, "field"_a, "value"_a, nb::rv_policy::reference_internal)
        .def("get_string", &PyLoad::getString, "field"_a)
        .def("as_dict", &PyLoad::asDict)
        .def("__repr__", [](const PyLoad& load) {
            return "<griddyn.Load name='" + load.name() + "' bus='" + load.bus() +
                "' p=" + std::to_string(load.p()) + " q=" + std::to_string(load.q()) + ">";
        });

    nb::class_<PyLink>(mod, "Link")
        .def_prop_ro("name", &PyLink::name)
        .def_prop_ro("type", &PyLink::type)
        .def_prop_ro("uid", &PyLink::userId)
        .def_prop_ro("bus1", &PyLink::bus1)
        .def_prop_ro("bus2", &PyLink::bus2)
        .def_prop_ro("p1", &PyLink::p1)
        .def_prop_ro("q1", &PyLink::q1)
        .def_prop_ro("p2", &PyLink::p2)
        .def_prop_ro("q2", &PyLink::q2)
        .def_prop_ro("loss", &PyLink::loss)
        .def_prop_ro("q_loss", &PyLink::reactiveLoss)
        .def("get", &PyLink::get, "field"_a)
        .def("set", &PyLink::set, "field"_a, "value"_a, nb::rv_policy::reference_internal)
        .def("get_string", &PyLink::getString, "field"_a)
        .def("as_dict", &PyLink::asDict)
        .def("__repr__", [](const PyLink& link) {
            return "<griddyn.Link name='" + link.name() + "' bus1='" + link.bus1() +
                "' bus2='" + link.bus2() + "'>";
        });

    nb::class_<PyArea>(mod, "Area")
        .def_prop_ro("name", &PyArea::name)
        .def_prop_ro("type", &PyArea::type)
        .def_prop_ro("uid", &PyArea::userId)
        .def_prop_ro("enabled", &PyArea::enabled)
        .def_prop_ro("bus_count", &PyArea::busCount)
        .def_prop_ro("link_count", &PyArea::linkCount)
        .def_prop_ro("area_count", &PyArea::areaCount)
        .def_prop_ro("relay_count", &PyArea::relayCount)
        .def_prop_ro("gen_count", &PyArea::generatorCount)
        .def_prop_ro("load_count", &PyArea::loadCount)
        .def_prop_ro("p_gen", &PyArea::generationP)
        .def_prop_ro("q_gen", &PyArea::generationQ)
        .def_prop_ro("p_load", &PyArea::loadP)
        .def_prop_ro("q_load", &PyArea::loadQ)
        .def_prop_ro("loss", &PyArea::loss)
        .def_prop_ro("avg_f", &PyArea::averageFrequency)
        .def_prop_ro("avg_a", &PyArea::averageAngle)
        .def_prop_ro("tie_p", &PyArea::tieP)
        .def("get", &PyArea::get, "field"_a)
        .def("set", &PyArea::set, "field"_a, "value"_a, nb::rv_policy::reference_internal)
        .def("get_string", &PyArea::getString, "field"_a)
        .def("find", &PyArea::find, "name"_a)
        .def("as_dict", &PyArea::asDict)
        .def("__repr__", [](const PyArea& area) {
            return "<griddyn.Area name='" + area.name() + "' buses=" +
                std::to_string(area.busCount()) + " links=" + std::to_string(area.linkCount()) +
                ">";
        });

    nb::class_<PyRelay>(mod, "Relay")
        .def_prop_ro("name", &PyRelay::name)
        .def_prop_ro("type", &PyRelay::type)
        .def_prop_ro("uid", &PyRelay::userId)
        .def_prop_ro("enabled", &PyRelay::enabled)
        .def("get", &PyRelay::get, "field"_a)
        .def("set", &PyRelay::set, "field"_a, "value"_a, nb::rv_policy::reference_internal)
        .def("get_string", &PyRelay::getString, "field"_a)
        .def("as_dict", &PyRelay::asDict)
        .def("__repr__", [](const PyRelay& relay) {
            return "<griddyn.Relay name='" + relay.name() + "'>";
        });

    nb::class_<PySensor>(mod, "Sensor")
        .def_prop_ro("name", &PySensor::name)
        .def_prop_ro("type", &PySensor::type)
        .def_prop_ro("uid", &PySensor::userId)
        .def_prop_ro("enabled", &PySensor::enabled)
        .def("output", &PySensor::output, "index"_a = 0)
        .def("get", &PySensor::get, "field"_a)
        .def("set", &PySensor::set, "field"_a, "value"_a, nb::rv_policy::reference_internal)
        .def("get_string", &PySensor::getString, "field"_a)
        .def("as_dict", &PySensor::asDict)
        .def("__repr__", [](const PySensor& sensor) {
            return "<griddyn.Sensor name='" + sensor.name() + "'>";
        });

    nb::class_<PyBusCollection>(mod, "BusCollection")
        .def("__len__", &PyBusCollection::size)
        .def("__getitem__", &PyBusCollection::getItem, "key"_a)
        .def_prop_ro("names", &PyBusCollection::names)
        .def("as_dicts", &PyBusCollection::asDicts)
        .def("to_list", &PyBusCollection::asDicts)
        .def("as_dataframe", &PyBusCollection::asDataFrame)
        .def("to_dataframe", &PyBusCollection::asDataFrame)
        .def("__repr__", [](const PyBusCollection& buses) {
            return "<griddyn.BusCollection size=" + std::to_string(buses.size()) + ">";
        });

    auto generatorCollectionClass = nb::class_<PyGeneratorCollection>(mod, "GeneratorCollection")
        .def("__len__", &PyGeneratorCollection::size)
        .def("__getitem__", &PyGeneratorCollection::getItem, "key"_a)
        .def_prop_ro("names", &PyGeneratorCollection::names)
        .def("as_dicts", &PyGeneratorCollection::asDicts)
        .def("to_list", &PyGeneratorCollection::asDicts)
        .def("as_dataframe", &PyGeneratorCollection::asDataFrame)
        .def("to_dataframe", &PyGeneratorCollection::asDataFrame)
        .def("__repr__", [](const PyGeneratorCollection& gens) {
            return "<griddyn.GeneratorCollection size=" + std::to_string(gens.size()) + ">";
        });
    mod.attr("GenCollection") = generatorCollectionClass;

    nb::class_<PyLoadCollection>(mod, "LoadCollection")
        .def("__len__", &PyLoadCollection::size)
        .def("__getitem__", &PyLoadCollection::getItem, "key"_a)
        .def_prop_ro("names", &PyLoadCollection::names)
        .def("as_dicts", &PyLoadCollection::asDicts)
        .def("to_list", &PyLoadCollection::asDicts)
        .def("as_dataframe", &PyLoadCollection::asDataFrame)
        .def("to_dataframe", &PyLoadCollection::asDataFrame)
        .def("__repr__", [](const PyLoadCollection& loads) {
            return "<griddyn.LoadCollection size=" + std::to_string(loads.size()) + ">";
        });

    nb::class_<PyLinkCollection>(mod, "LinkCollection")
        .def("__len__", &PyLinkCollection::size)
        .def("__getitem__", &PyLinkCollection::getItem, "key"_a)
        .def_prop_ro("names", &PyLinkCollection::names)
        .def("as_dicts", &PyLinkCollection::asDicts)
        .def("to_list", &PyLinkCollection::asDicts)
        .def("as_dataframe", &PyLinkCollection::asDataFrame)
        .def("to_dataframe", &PyLinkCollection::asDataFrame)
        .def("__repr__", [](const PyLinkCollection& links) {
            return "<griddyn.LinkCollection size=" + std::to_string(links.size()) + ">";
        });

    nb::class_<PyAreaCollection>(mod, "AreaCollection")
        .def("__len__", &PyAreaCollection::size)
        .def("__getitem__", &PyAreaCollection::getItem, "key"_a)
        .def_prop_ro("names", &PyAreaCollection::names)
        .def("as_dicts", &PyAreaCollection::asDicts)
        .def("to_list", &PyAreaCollection::asDicts)
        .def("as_dataframe", &PyAreaCollection::asDataFrame)
        .def("to_dataframe", &PyAreaCollection::asDataFrame)
        .def("__repr__", [](const PyAreaCollection& areas) {
            return "<griddyn.AreaCollection size=" + std::to_string(areas.size()) + ">";
        });

    nb::class_<PyRelayCollection>(mod, "RelayCollection")
        .def("__len__", &PyRelayCollection::size)
        .def("__getitem__", &PyRelayCollection::getItem, "key"_a)
        .def_prop_ro("names", &PyRelayCollection::names)
        .def("as_dicts", &PyRelayCollection::asDicts)
        .def("to_list", &PyRelayCollection::asDicts)
        .def("as_dataframe", &PyRelayCollection::asDataFrame)
        .def("to_dataframe", &PyRelayCollection::asDataFrame)
        .def("__repr__", [](const PyRelayCollection& relays) {
            return "<griddyn.RelayCollection size=" + std::to_string(relays.size()) + ">";
        });

    nb::class_<PySensorCollection>(mod, "SensorCollection")
        .def("__len__", &PySensorCollection::size)
        .def("__getitem__", &PySensorCollection::getItem, "key"_a)
        .def_prop_ro("names", &PySensorCollection::names)
        .def("as_dicts", &PySensorCollection::asDicts)
        .def("to_list", &PySensorCollection::asDicts)
        .def("as_dataframe", &PySensorCollection::asDataFrame)
        .def("to_dataframe", &PySensorCollection::asDataFrame)
        .def("__repr__", [](const PySensorCollection& sensors) {
            return "<griddyn.SensorCollection size=" + std::to_string(sensors.size()) + ">";
        });

    nb::class_<PySimulation>(mod, "Simulation")
        .def(nb::init<std::string, std::string>(), "name"_a = "", "type"_a = "default")
        .def_static("from_file", &PySimulation::fromFile, "path"_a, "format"_a = "", "name"_a = "")
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
        .def("get", &PySimulation::get, "field"_a)
        .def("set", &PySimulation::set, "field"_a, "value"_a, nb::rv_policy::reference_internal)
        .def("get_string", &PySimulation::getString, "field"_a)
        .def("find", &PySimulation::find, "name"_a)
        .def_prop_rw("name", &PySimulation::name, &PySimulation::setName)
        .def_prop_ro("time", &PySimulation::time)
        .def_prop_ro("PFlow", &PySimulation::powerFlowRoutine)
        .def_prop_ro("pflow", &PySimulation::powerFlowRoutine)
        .def_prop_ro("TDS", &PySimulation::timeDomainRoutine)
        .def_prop_ro("tds", &PySimulation::timeDomainRoutine)
        .def_prop_ro("Bus", &PySimulation::busCollection)
        .def_prop_ro("bus", &PySimulation::busCollection)
        .def_prop_ro("buses", &PySimulation::busCollection)
        .def_prop_ro("Generator", &PySimulation::generatorCollection)
        .def_prop_ro("generator", &PySimulation::generatorCollection)
        .def_prop_ro("generators", &PySimulation::generatorCollection)
        .def_prop_ro("Gen", &PySimulation::generatorCollection)
        .def_prop_ro("gen", &PySimulation::generatorCollection)
        .def_prop_ro("gens", &PySimulation::generatorCollection)
        .def_prop_ro("Load", &PySimulation::loadCollection)
        .def_prop_ro("loads", &PySimulation::loadCollection)
        .def_prop_ro("Link", &PySimulation::linkCollection)
        .def_prop_ro("link", &PySimulation::linkCollection)
        .def_prop_ro("links", &PySimulation::linkCollection)
        .def_prop_ro("Area", &PySimulation::areaCollection)
        .def_prop_ro("area", &PySimulation::areaCollection)
        .def_prop_ro("areas", &PySimulation::areaCollection)
        .def_prop_ro("Sensor", &PySimulation::sensorCollection)
        .def_prop_ro("sensor", &PySimulation::sensorCollection)
        .def_prop_ro("sensors", &PySimulation::sensorCollection)
        .def_prop_ro("Relay", &PySimulation::relayCollection)
        .def_prop_ro("relay", &PySimulation::relayCollection)
        .def_prop_ro("relays", &PySimulation::relayCollection)
        .def("__repr__", [](const PySimulation& sim) {
            return "<griddyn.Simulation name='" + sim.name() +
                "' time=" + std::to_string(sim.time()) + ">";
        });
}
