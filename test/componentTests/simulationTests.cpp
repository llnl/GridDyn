/*
 * Copyright (c) 2014-2026, Lawrence Livermore National Security
 * See the top-level NOTICE for additional details. All rights reserved.
 * SPDX-License-Identifier: BSD-3-Clause
 */

#include "../gtestHelper.h"
#include "core/ObjectFactoryTemplates.hpp"
#include "gmlc/utilities/vectorOps.hpp"
#include "griddyn/griddyn-config.h"
#include <algorithm>
#include <cstdio>
#include <gtest/gtest.h>
#include <iostream>
#include <set>
#include <string>
#include <utility>
#include <vector>

#ifdef GRIDDYN_ENABLE_OPTIMIZATION_LIBRARY
#    include "optimization/optObjectFactory.h"
#endif

class SimulationTests: public GridDynSimulationTestFixture, public ::testing::Test {};

TEST_F(SimulationTests, SimulationOrderingTests) {}

namespace {
class CountingRootObject: public griddyn::CoreObject {
  public:
    int addCount = 0;
    std::vector<griddyn::CoreObject*> addedObjects;

    void add(griddyn::CoreObject* obj) override
    {
        if (obj == nullptr) {
            return;
        }

        ++addCount;
        obj->addOwningReference();
        obj->setParent(this);
        addedObjects.push_back(obj);
    }

    void remove(griddyn::CoreObject* obj) override
    {
        const auto foundObject = std::find(addedObjects.begin(), addedObjects.end(), obj);
        if (foundObject == addedObjects.end()) {
            return;
        }

        griddyn::removeReference(*foundObject, this);
        addedObjects.erase(foundObject);
    }

    ~CountingRootObject() override
    {
        for (auto* obj : addedObjects) {
            griddyn::removeReference(obj, this);
        }
    }
};

class FactoryTestGridObject: public griddyn::CoreObject {
  public:
    FactoryTestGridObject() = default;
    explicit FactoryTestGridObject(const std::string& objectName): CoreObject(objectName) {}
};
}  // namespace

TEST(CoreFactoryTests, PrepObjectsIgnoresInvalidRequests)
{
    CountingRootObject root;
    griddyn::TypeFactory<FactoryTestGridObject> factory("core-factory-prep-invalid-test", "object");

    factory.prepObjects(0, &root);
    EXPECT_EQ(root.addCount, 0);
    EXPECT_EQ(factory.remainingPrepped(), 0U);

    factory.prepObjects(2, nullptr);
    EXPECT_EQ(root.addCount, 0);
    EXPECT_EQ(factory.remainingPrepped(), 0U);
}

TEST(CoreFactoryTests, PrepObjectsCreatesNonEmptyHolderOnce)
{
    CountingRootObject root;
    griddyn::TypeFactory<FactoryTestGridObject> factory("core-factory-prep-test", "object");

    factory.prepObjects(3, &root);
    EXPECT_EQ(root.addCount, 1);
    EXPECT_EQ(factory.remainingPrepped(), 3U);

    factory.prepObjects(2, &root);
    EXPECT_EQ(root.addCount, 1);
    EXPECT_EQ(factory.remainingPrepped(), 3U);

    auto* object = factory.makeTypeObject();
    ASSERT_NE(object, nullptr);
    EXPECT_EQ(factory.remainingPrepped(), 2U);

    factory.prepObjects(2, &root);
    EXPECT_EQ(root.addCount, 1);
    EXPECT_EQ(factory.remainingPrepped(), 2U);

    factory.prepObjects(5, &root);
    EXPECT_EQ(root.addCount, 1);
    EXPECT_EQ(factory.remainingPrepped(), 5U);

    factory.makeTypeObject();
    factory.makeTypeObject();
    EXPECT_EQ(root.addCount, 1);
    EXPECT_EQ(factory.remainingPrepped(), 3U);

    factory.makeTypeObject();
    EXPECT_EQ(root.addCount, 2);
    EXPECT_EQ(factory.remainingPrepped(), 2U);
}

#ifdef GRIDDYN_ENABLE_OPTIMIZATION_LIBRARY
namespace {
class FactoryTestOptObject: public griddyn::GridOptObject {
  public:
    FactoryTestOptObject() = default;
    explicit FactoryTestOptObject(griddyn::CoreObject* obj) { add(obj); }

    griddyn::CoreObject* sourceObject = nullptr;

    void add(griddyn::CoreObject* obj) override { sourceObject = obj; }
};
}  // namespace

TEST(OptimizationFactoryTests, PrepObjectsReusesAttachedHolder)
{
    CountingRootObject root;
    FactoryTestGridObject gridObject;
    griddyn::OptObjectFactory<FactoryTestOptObject, FactoryTestGridObject> factory(
        "factory-prep-test", "object");

    factory.prepObjects(3, &root);
    EXPECT_EQ(root.addCount, 1);
    EXPECT_EQ(factory.remainingPrepped(), 3U);

    factory.prepObjects(2, &root);
    EXPECT_EQ(root.addCount, 1);
    EXPECT_EQ(factory.remainingPrepped(), 3U);

    auto* optObject = factory.makeTypeObject(&gridObject);
    ASSERT_NE(optObject, nullptr);
    EXPECT_EQ(optObject->sourceObject, &gridObject);
    EXPECT_EQ(factory.remainingPrepped(), 2U);

    factory.prepObjects(2, &root);
    EXPECT_EQ(root.addCount, 1);
    EXPECT_EQ(factory.remainingPrepped(), 2U);

    factory.prepObjects(3, &root);
    EXPECT_EQ(root.addCount, 2);
    EXPECT_EQ(factory.remainingPrepped(), 3U);
}

TEST(OptimizationFactoryTests, PrepObjectsIgnoresInvalidRequests)
{
    CountingRootObject root;
    griddyn::OptObjectFactory<FactoryTestOptObject, FactoryTestGridObject> factory(
        "factory-prep-invalid-test", "object");

    factory.prepObjects(0, &root);
    EXPECT_EQ(root.addCount, 0);
    EXPECT_EQ(factory.remainingPrepped(), 0U);

    factory.prepObjects(2, nullptr);
    EXPECT_EQ(root.addCount, 0);
    EXPECT_EQ(factory.remainingPrepped(), 0U);
}
#endif
