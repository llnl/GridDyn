/*
 * Copyright (c) 2014-2020, Lawrence Livermore National Security
 * See the top-level NOTICE for additional details. All rights reserved.
 * SPDX-License-Identifier: BSD-3-Clause
 */

#include "../gtestHelper.h"
#include "core/coreExceptions.h"
#include "core/objectFactory.hpp"
#include "griddyn/Governor.h"
#include "griddyn/exciters/ExciterIEEEtype1.h"
#include "griddyn/genmodels/GenModel4.h"
#include "griddyn/loads/fileLoad.h"
#include "griddyn/loads/sourceLoad.h"
#include "griddyn/loads/zipLoad.h"

#include <gtest/gtest.h>

// test case for coreObject object

using namespace griddyn;

TEST(CoreTests, CoreObject)
{
    auto* obj1 = new coreObject();
    auto* obj2 = new coreObject();

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

    coreObject* obj3 = nullptr;
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
    EXPECT_THROW(obj3->set("bob", 0.5), unrecognizedParameter);
    delete (obj1);
    delete (obj2);
    delete (obj3);
}

// testcase for GenModel Object
TEST(CoreTests, GenModel)
{
    auto gm = new genmodels::GenModel4();
    auto id = gm->getID();
    EXPECT_EQ(gm->getName().compare("genModel4_" + std::to_string(id)), 0);
    std::string temp1 = "gen_model 5";
    gm->setName(temp1);
    EXPECT_EQ(gm->getName().compare(temp1), 0);

    gm->setName("namebbghs");
    EXPECT_EQ(gm->getName().compare("namebbghs"), 0);
    gm->dynInitializeA(timeZero, 0);
    EXPECT_EQ(gm->stateSize(cLocalSolverMode), 6_cnt);
    gm->set("h", 4.5);
    gm->setOffset(0, cLocalSolverMode);
    EXPECT_EQ(gm->findIndex("freq", cLocalSolverMode), static_cast<index_t>(3));
    delete (gm);
}

TEST(CoreTests, Exciter)
{
    Exciter* ex = new exciters::ExciterIEEEtype1();
    std::string temp1 = "exciter 2";
    ex->setName(temp1);
    EXPECT_EQ(ex->getName().compare(temp1), 0);
    ex->dynInitializeA(0.0, 0);
    EXPECT_EQ(ex->stateSize(cLocalSolverMode), 3_ind);
    ex->set("tf", 4.5);
    ex->setOffset(0, cLocalSolverMode);
    EXPECT_EQ(ex->findIndex("vr", cLocalSolverMode), static_cast<index_t>(1));
    delete (ex);
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
    delete (gov);
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
    auto cof = coreObjectFactory::instance();
    coreObject* obj = cof->createObject("load", "basic");
    auto ld = dynamic_cast<zipLoad*>(obj);
    EXPECT_NE(ld, nullptr);
    delete ld;
    auto gsL = dynamic_cast<loads::sourceLoad*>(cof->createObject("load", "sine"));
    EXPECT_NE(gsL, nullptr);
    delete gsL;

    obj = cof->createObject("load");
    ld = dynamic_cast<zipLoad*>(obj);
    EXPECT_NE(ld, nullptr);
    delete ld;
}

TEST(CoreTests, GridDynTime)
{
    coreTime rt(34.123141512);

    auto dval = static_cast<double>(rt);
    EXPECT_NEAR(dval, 34.123141512, 34.123141512 * 1e-9);

    coreTime rt2(-2.3);

    auto dval2 = static_cast<double>(rt2);
    EXPECT_NEAR(dval2, -2.3, 2.3e-9);

    coreTime rt3(-1.0);

    auto dval3 = static_cast<double>(rt3);
    EXPECT_NEAR(dval3, -1.0, 1e-9);
}

/** test case to construct all objects and do some potentially problematic things to them to ensure
the object doesn't break or cause a fault
*/

TEST(CoreTests, ObjectProbe)
{
    auto cof = coreObjectFactory::instance();
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
                    if (sameTypeObj != nullptr) {
                        delete sameTypeObj;
                    }
                    delete (nobj);
                } else {
                    std::cout << "unable to clone " << comp << "::" << type << '\n';
                }
            }
            delete obj;
        }
    }
}
