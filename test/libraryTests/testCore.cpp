/*
 * Copyright (c) 2014-2026, Lawrence Livermore National Security
 * See the top-level NOTICE for additional details. All rights reserved.
 * SPDX-License-Identifier: BSD-3-Clause
 */

#include "../gtestHelper.h"
#include "core/CoreExceptions.h"
#include "core/ModelDescription.h"
#include "core/ObjectFactory.hpp"
#include "griddyn/Governor.h"
#include "griddyn/exciters/ExciterIEEEtype1.h"
#include "griddyn/genmodels/GenModel4.h"
#include "griddyn/loads/FileLoad.h"
#include "griddyn/loads/SourceLoad.h"
#include "griddyn/loads/ZipLoad.h"
#include <bitset>
#include <cstddef>
#include <cstdint>
#include <gtest/gtest.h>
#include <iostream>
#include <memory>
#include <set>
#include <string>
#include <type_traits>
#include <typeindex>
#include <variant>

// test case for CoreObject object

using namespace griddyn;

TEST(CoreTests, CoreObject)
{
    auto* obj1 = new CoreObject();
    auto* obj2 = new CoreObject();

    EXPECT_EQ(obj1->getName().compare("object_" + std::to_string(obj1->getID())), 0);

    // check to make sure the object m_oid counter is working

    EXPECT_GT(obj2->getID(), obj1->getID());
    // check if the name setting is working
    std::string name1 = "test_object";
    obj1->setName(name1);
    EXPECT_EQ(obj1->getName().compare(name1), 0);

    obj1->setName("test_object");
    EXPECT_EQ(obj1->getName().compare("test_object"), 0);

    double ntime = 1;
    obj1->set("nextupdatetime", ntime);
    EXPECT_DOUBLE_EQ(double(obj1->getNextUpdateTime()), ntime);

    // use alternative form for set
    obj2->set("nextupdatetime", 0.1);
    EXPECT_DOUBLE_EQ(double(obj2->getNextUpdateTime()), 0.1);

    EXPECT_TRUE(compareUpdates(obj2, obj1));

    CoreObject* obj3 = nullptr;
    obj3 = obj1->clone(obj3);

    // check the copy constructor
    EXPECT_DOUBLE_EQ(double(obj3->getNextUpdateTime()), ntime);
    EXPECT_EQ(obj3->getName().compare("test_object"), 0);
    EXPECT_TRUE(obj3->isRoot());

    ntime = 3;
    obj1->set("nextupdatetime", ntime);
    obj3 = obj1->clone(obj3);
    EXPECT_DOUBLE_EQ(double(obj3->getNextUpdateTime()), ntime);
    EXPECT_EQ(obj3->getName(), "test_object");
    // check the parameter not found function
    EXPECT_THROW(obj3->set("bob", 0.5), UnrecognizedParameter);
    delete obj1;
    delete obj2;
    delete obj3;
}

// testcase for GenModel Object
TEST(CoreTests, GenModel)
{
    auto* machine = new genmodels::GenModel4();
    const auto machineId = machine->getID();
    EXPECT_EQ(machine->getName().compare("genModel4_" + std::to_string(machineId)), 0);
    std::string temp1 = "gen_model 5";
    machine->setName(temp1);
    EXPECT_EQ(machine->getName().compare(temp1), 0);

    machine->setName("namebbghs");
    EXPECT_EQ(machine->getName().compare("namebbghs"), 0);
    machine->dynInitializeA(timeZero, 0);
    EXPECT_EQ(machine->stateSize(cLocalSolverMode), 6_cnt);
    machine->set("h", 4.5);
    machine->setOffset(0, cLocalSolverMode);
    EXPECT_EQ(machine->findIndex("freq", cLocalSolverMode), static_cast<index_t>(3));
    delete machine;
}

TEST(CoreTests, Exciter)
{
    Exciter* exciter = new exciters::ExciterIEEEtype1();
    std::string temp1 = "exciter 2";
    exciter->setName(temp1);
    EXPECT_EQ(exciter->getName().compare(temp1), 0);
    exciter->dynInitializeA(0.0, 0);
    EXPECT_EQ(exciter->stateSize(cLocalSolverMode), 3_ind);
    exciter->set("tf", 4.5);
    exciter->setOffset(0, cLocalSolverMode);
    EXPECT_EQ(exciter->findIndex("vr", cLocalSolverMode), static_cast<index_t>(1));
    delete exciter;
}

TEST(CoreTests, Governor)
{
    auto* gov = new Governor();
    std::string temp1 = "gov 2";
    gov->set("name", temp1);
    EXPECT_EQ(gov->getName().compare(temp1), 0);
    gov->dynInitializeA(timeZero, 0);
    EXPECT_EQ(gov->stateSize(cLocalSolverMode), 4_cnt);
    gov->set("t1", 4.5);
    gov->setOffset(0, cLocalSolverMode);
    EXPECT_EQ(gov->findIndex("pm", cLocalSolverMode), static_cast<index_t>(0));
    delete gov;
}

TEST(CoreTests, UnitFunctions)
{
    using namespace units;
    // units_t u1;
    // units_t u2;
    double val1;
    double basepower = 100;
    //  double basevoltage = 10;
    // power conversions
    val1 = convert(100.0, MW, kW);
    EXPECT_NEAR(val1, 100000, 100000 * 1e-6);
    val1 = convert(100.0, MW, W);
    EXPECT_NEAR(val1, 100000000, 100000000 * 1e-6);
    val1 = convert(1000000.0, W, MW);
    EXPECT_NEAR(val1, 1, 1e-6);
    val1 = convert(100000.0, kW, MW);
    EXPECT_NEAR(val1, 100, 100 * 1e-6);
    val1 = convert(100000.0, kW, puMW, basepower);
    EXPECT_NEAR(val1, 1, 1e-6);
    // angle conversions
    val1 = convert(10, deg, rad);
    EXPECT_NEAR(val1, 0.17453292, 0.17453292 * 1e-6);
    val1 = convert(0.17453292, rad, deg);
    EXPECT_NEAR(val1, 10, 10 * 1e-6);
    // pu conversions
    val1 = convert(1.0, puOhm, ohm, 0.1, 0.6);
    EXPECT_NEAR(val1, 3.600, 3.600 * 1e-5);
    val1 = convert(1.0, puA, A, 100000.0, 600.0);
    EXPECT_NEAR(val1, 166.6666666, 166.6666666 * 1e-4);
}

TEST(CoreTests, ObjectFactory)
{
    auto cof = CoreObjectFactory::instance();
    CoreObject* obj = cof->createObject("load", "basic");
    auto* load = dynamic_cast<ZipLoad*>(obj);
    EXPECT_NE(load, nullptr);
    delete load;
    auto gsL = dynamic_cast<loads::SourceLoad*>(cof->createObject("load", "sine"));
    EXPECT_NE(gsL, nullptr);
    delete gsL;

    obj = cof->createObject("load");
    load = dynamic_cast<ZipLoad*>(obj);
    EXPECT_NE(load, nullptr);
    delete load;
}

TEST(CoreTests, GridDynTime)
{
    const CoreTime time(34.123141512);

    auto dval = static_cast<double>(time);
    EXPECT_NEAR(dval, 34.123141512, 34.123141512 * 1e-9);

    CoreTime rt2(-2.3);

    auto dval2 = static_cast<double>(rt2);
    EXPECT_NEAR(dval2, -2.3, 2.3e-9);

    CoreTime rt3(-1.0);

    auto dval3 = static_cast<double>(rt3);
    EXPECT_NEAR(dval3, -1.0, 1e-9);
}

TEST(CoreTests, RegisteredModelsRoundTripEveryDeclaredLocalStateField)
{
    auto factory = CoreObjectFactory::instance();
    std::set<std::type_index> tested;
    std::set<std::string> missingDescriptions;
    std::size_t fieldsTested = 0;
    for (const auto& category : factory->getFactoryNames()) {
        for (const auto& type : factory->getTypeNames(category)) {
            auto modelName = category;
            modelName.append("/").append(type);
            SCOPED_TRACE(modelName);
            std::unique_ptr<CoreObject> model(factory->createObject(category, type));
            ASSERT_NE(model, nullptr);
            const auto& description = model->getModelDescription();
            const auto& object = *model;
            if (!tested.insert(typeid(object)).second) {
                continue;
            }
            if (!description.describesExactType(object)) {
                missingDescriptions.insert(modelName);
                continue;
            }

            auto snapshot = ModelFieldSnapshot::capture(object, FieldSnapshotLevel::LOCAL_STATE);
            std::size_t fieldCount = 0;
            for (const auto* table = &description; table != nullptr; table = table->parent()) {
                for (const auto& field : table->fields()) {
                    if (field.kind() == ModelFieldKind::LOCAL_STATE) {
                        ++fieldCount;
                    }
                }
            }
            ASSERT_EQ(snapshot.mFields.size(), fieldCount);
            for (auto& entry : snapshot.mFields) {
                SCOPED_TRACE(entry.mDeclaringClass + "::" + entry.mName);
                const auto* owner = &description;
                while (owner != nullptr && owner->name() != entry.mDeclaringClass) {
                    owner = owner->parent();
                }
                ASSERT_NE(owner, nullptr);
                const auto* field = owner->findLocal(entry.mName);
                ASSERT_NE(field, nullptr);
                ASSERT_EQ(field->kind(), ModelFieldKind::LOCAL_STATE);
                entry.mValue = std::visit(
                    [](const auto& value) -> ModelFieldValue {
                        using Value = std::decay_t<decltype(value)>;
                        if constexpr (std::is_same_v<Value, CoreTime>) {
                            return value == timeOneSecond ? timeZero : timeOneSecond;
                        } else if constexpr (std::is_same_v<Value, bool>) {
                            return !value;
                        } else if constexpr (std::is_arithmetic_v<Value>) {
                            return value == Value{1} ? Value{0} : Value{1};
                        } else if constexpr (std::is_same_v<Value, std::string>) {
                            return value + " changed";
                        } else if constexpr (std::is_same_v<Value, std::bitset<64>>) {
                            return ~value;
                        } else {
                            auto copy = value;
                            for (auto& element : copy) {
                                element = element == 1.0 ? 0.0 : 1.0;
                            }
                            return copy;
                        }
                    },
                    entry.mValue);
                ++fieldsTested;
            }
            model->CoreObject::disable();
            snapshot.restore(*model);
            EXPECT_FALSE(model->isEnabled());
            const auto actual =
                ModelFieldSnapshot::capture(*model, FieldSnapshotLevel::LOCAL_STATE);
            ASSERT_EQ(actual.mFields.size(), snapshot.mFields.size());
            for (std::size_t ii = 0; ii < snapshot.mFields.size(); ++ii) {
                const auto& expected = snapshot.mFields[ii];
                SCOPED_TRACE(expected.mDeclaringClass + "::" + expected.mName);
                EXPECT_EQ(actual.mFields[ii].mDeclaringClass, expected.mDeclaringClass);
                EXPECT_EQ(actual.mFields[ii].mName, expected.mName);
                EXPECT_EQ(actual.mFields[ii].mValue, expected.mValue);
            }
        }
    }
    EXPECT_GT(tested.size(), 80U);
    EXPECT_GT(fieldsTested, 100U);
    RecordProperty("model_types", static_cast<int>(tested.size()));
    RecordProperty("local_state_fields", static_cast<int>(fieldsTested));
    EXPECT_TRUE(missingDescriptions.empty()) << ::testing::PrintToString(missingDescriptions);
}

/** Exercise construction, access, and cloning for factory-registered objects. */
TEST(CoreTests, ObjectProbe)
{
    auto cof = CoreObjectFactory::instance();
    auto componentList = cof->getFactoryNames();
    for (auto& comp : componentList) {
        auto componentFactory = cof->getFactory(comp);
        auto typeList = componentFactory->getTypeNames();
        for (auto& type : typeList) {
            auto* obj = componentFactory->makeObject(type);
            ASSERT_NE(obj, nullptr);
            obj->setName("bob");  // NOLINT
            EXPECT_EQ(obj->getName(), "bob");
            obj->setName(std::string());
            EXPECT_EQ(obj->getName(), "");
            obj->set("", "empty");  // this should not throw an exception
            obj->set("", 0.34, units::defunit);  // this should not throw an exception
            obj->setFlag("", false);  // This should not throw an exception
            obj->set("#unknown", "empty");  // this should not throw an exception
            obj->set("#unknown", 0.34, units::defunit);  // this should not throw an exception
            obj->setFlag("#unknown", false);  // This should not throw an exception

            if (obj->isCloneable()) {
                auto nobj = obj->clone();
                EXPECT_NE(nobj, nullptr);
                if (nobj != nullptr) {
                    auto* sameTypeObj = componentFactory->makeObject(type);
                    ASSERT_NE(sameTypeObj, nullptr);
                    auto* copiedObj = obj->clone(sameTypeObj);
                    EXPECT_NE(copiedObj, nullptr);
                    if (copiedObj != nullptr) {
                        EXPECT_EQ(typeid(*copiedObj), typeid(*obj));
                    }
                    if ((copiedObj != nullptr) && (copiedObj != sameTypeObj)) {
                        delete copiedObj;
                    }
                    delete sameTypeObj;
                    delete nobj;
                } else {
                    std::cout << "unable to clone " << comp << "::" << type << '\n';
                }
            }
            delete obj;
        }
    }
}
