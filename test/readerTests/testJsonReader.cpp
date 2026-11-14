/*
 * Copyright (c) 2014-2026, Lawrence Livermore National Security
 * See the top-level NOTICE for additional details. All rights reserved.
 * SPDX-License-Identifier: BSD-3-Clause
 */

#include "../gtestHelper.h"
#include "fileInput/fileInput.h"
#include "formatInterpreters/jsonReaderElement.h"
#include "griddyn/links/DcLink.h"
#include "griddyn/links/VSCShunt.h"
#include "griddyn/loads/FDepLoad.h"
#include "griddyn/loads/ShuntTD.h"
#include "griddyn/loads/Svd.h"
#include "griddyn/primary/AcBus.h"
#include "griddyn/primary/DcBus.h"
#include <gtest/gtest.h>
#include <memory>
#include <string>
#include <string_view>

static constexpr std::string_view elementReaderTestDirectory{GRIDDYN_TEST_DIRECTORY
                                                             "/element_reader_tests/"};
static constexpr std::string_view comparisonTestDirectory{GRIDDYN_TEST_DIRECTORY
                                                          "/comparison_tests/"};

static std::string makeElementReaderTestPath(std::string_view fileName)
{
    return std::string{elementReaderTestDirectory} + std::string{fileName};
}

static std::string makeComparisonTestPath(std::string_view fileName)
{
    return std::string{comparisonTestDirectory} + std::string{fileName};
}

TEST(JsonElementReaderTests, ReadsObjectAttributesAndArrayElements)
{
    JsonReaderElement reader;
    ASSERT_TRUE(reader.loadFile(makeElementReaderTestPath("json_test1.json")));
    EXPECT_TRUE(reader.isValid());
    auto firstChild = reader.clone();
    ASSERT_NE(firstChild, nullptr);
    EXPECT_EQ(firstChild->getName(), makeElementReaderTestPath("json_test1.json"));

    auto sibling = firstChild->nextSibling();
    EXPECT_FALSE(sibling->isValid());

    auto att1 = firstChild->getFirstAttribute();
    EXPECT_EQ(att1.getName(), "age");
    EXPECT_EQ(att1.getValue(), 25);
    EXPECT_EQ(att1.getText(), "25");
    auto att2 = firstChild->getNextAttribute();
    EXPECT_EQ(att2.getName(), "firstName");
    EXPECT_EQ(att2.getValue(), readerNullVal);
    EXPECT_EQ(att2.getText(), "John");
    att2 = firstChild->getNextAttribute();
    EXPECT_EQ(att2.getName(), "isAlive");
    EXPECT_EQ(att2.getText(), "true");
    att2 = firstChild->getNextAttribute();
    EXPECT_EQ(att2.getName(), "lastName");
    EXPECT_EQ(att2.getText(), "Smith");

    att2 = firstChild->getNextAttribute();
    EXPECT_FALSE(att2.isValid());

    firstChild->moveToFirstChild();
    EXPECT_TRUE(firstChild->isValid());
    EXPECT_EQ(firstChild->getName(), "address");
    firstChild->moveToNextSibling();
    EXPECT_TRUE(firstChild->isValid());
    EXPECT_EQ(firstChild->getName(), "phoneNumbers");
    firstChild->moveToNextSibling();
    EXPECT_TRUE(firstChild->isValid());
    EXPECT_EQ(firstChild->getName(), "phoneNumbers");
    firstChild->moveToNextSibling();
    EXPECT_TRUE(firstChild->isValid());
    EXPECT_EQ(firstChild->getName(), "phoneNumbers");
    firstChild->moveToNextSibling();
    EXPECT_FALSE(firstChild->isValid());
    firstChild->moveToParent();
    EXPECT_EQ(firstChild->getName(), makeElementReaderTestPath("json_test1.json"));
}

TEST(JsonElementReaderTests, ReadsNestedObjectsAfterMissingFile)
{
    JsonReaderElement reader;
    EXPECT_FALSE(reader.loadFile(makeElementReaderTestPath("missing.json")));
    EXPECT_FALSE(reader.isValid());
    ASSERT_TRUE(reader.loadFile(makeElementReaderTestPath("json_test2.json")));
    auto firstChild = reader.clone();

    auto sibling = firstChild->firstChild();
    EXPECT_EQ(sibling->getName(), "bus");
    auto cChild = sibling->nextSibling();
    EXPECT_FALSE(cChild->isValid());
    sibling->moveToNextSibling();
    EXPECT_FALSE(sibling->isValid());

    auto busElement = firstChild->firstChild();

    EXPECT_EQ(busElement->getAttributeText("type"), "SLK");

    // Go through the children
    auto att1 = busElement->getFirstAttribute();
    EXPECT_EQ(att1.getName(), "angle");
    EXPECT_EQ(att1.getValue(), 0.0);
    att1 = busElement->getNextAttribute();
    EXPECT_EQ(att1.getName(), "name");
    EXPECT_EQ(att1.getText(), "bus1");
    att1 = busElement->getNextAttribute();
    att1 = busElement->getNextAttribute();
    EXPECT_EQ(att1.getName(), "voltage");
    EXPECT_EQ(att1.getText(), "1.04");
    EXPECT_NEAR(att1.getValue(), 1.04, 1e-6);

    auto busChild = busElement->firstChild();
    EXPECT_EQ(busChild->getName(), "generator");
    EXPECT_TRUE(busChild->getText().empty());
    auto att2 = busChild->getFirstAttribute();

    EXPECT_EQ(att2.getName(), "name");
    EXPECT_EQ(att2.getText(), "gen1");
    att2 = busChild->getNextAttribute();
    EXPECT_EQ(att2.getName(), "p");
    EXPECT_NEAR(att2.getValue(), 0.7160, 1e-5);

    // move busChild to the parent to make sure they are the same
    busChild->moveToParent();
    EXPECT_EQ(busChild->getName(), busElement->getName());

    // Now go back to the first child to do a few checks on attributes
    att1 = firstChild->getAttribute("name");
    EXPECT_EQ(att1.getName(), "name");
    EXPECT_EQ(att1.getText(), "test1");
}

TEST(JsonElementReaderTests, RestoresBookmarksAfterInvalidFormat)
{
    JsonReaderElement reader;
    EXPECT_FALSE(reader.loadFile(makeElementReaderTestPath("xmlElementReader_test2.xml")));
    EXPECT_FALSE(reader.isValid());
    ASSERT_TRUE(reader.loadFile(makeElementReaderTestPath("json_test3.json")));
    // test traversal using move commands
    auto main = reader.clone();
    // bookmark the top level
    main->bookmark();
    main->moveToFirstChild("bus");
    main->moveToFirstChild();
    // traverse to the generator
    EXPECT_EQ(main->getName(), "generator");
    main->restore();
    // restore to the root
    EXPECT_TRUE(main->isDocument());
    // traverse to the second bus and check name
    main->moveToFirstChild("bus");
    main->moveToNextSibling("bus");
    EXPECT_TRUE(main->isValid());
    EXPECT_EQ(main->getAttributeText("name"), "bus2");
    main->moveToParent();
    main->moveToFirstChild();
    main->moveToFirstChild();
    // check we are in the generator now
    EXPECT_EQ(main->getAttributeText("name"), "gen1");
    // make a bookmark
    main->bookmark();
    // traverse to the second bus
    main->moveToParent();
    main->moveToNextSibling("bus");
    EXPECT_NEAR(main->getAttributeValue("voltage"), 1.01, 1e-7);
    // traverse to the parent
    main->moveToParent();
    // restore and check if we are in the generator again
    main->restore();
    EXPECT_NEAR(main->getAttributeValue("p"), 0.7160, 1e-7);
}

TEST(DcReaderComparisonTests, ImportsAllAndesDcComponents)
{
    auto simulation = std::make_unique<griddyn::GridDynSimulation>();
    griddyn::loadFile(simulation.get(), makeComparisonTestPath("dc_components.json"));

    EXPECT_NE(dynamic_cast<griddyn::DcBus*>(simulation->find("ground_node")), nullptr);
    EXPECT_NE(dynamic_cast<griddyn::DcBus*>(simulation->find("node1")), nullptr);
    for (const auto* name : {"r", "l", "c", "rls", "rcp", "rlcp", "rcs", "rlcs"}) {
        EXPECT_NE(dynamic_cast<griddyn::links::DcLink*>(simulation->find(name)), nullptr) << name;
    }
    auto* vsc = dynamic_cast<griddyn::links::VSCShunt*>(simulation->find("vsc"));
    ASSERT_NE(vsc, nullptr);
    EXPECT_EQ(vsc->terminalCount(), 3U);
    ASSERT_NE(vsc->getBus(3), nullptr);
    EXPECT_EQ(vsc->getBus(3)->getName(), "ground_node");
}

TEST(AndesShuntModelReaderTests, ImportsFLoadAndShuntVariants)
{
    auto simulation = std::make_unique<griddyn::GridDynSimulation>();
    griddyn::loadFile(simulation.get(), makeComparisonTestPath("shunt_model_imports.json"));

    auto* fload = dynamic_cast<griddyn::loads::FDepLoad*>(simulation->find("BUS1::FLoad_1"));
    ASSERT_NE(fload, nullptr);
    EXPECT_NEAR(fload->get("kp"), 90.0, 1.0e-12);
    EXPECT_NEAR(fload->get("kq"), 80.0, 1.0e-12);
    EXPECT_NEAR(fload->get("vref"), 1.02, 1.0e-12);

    auto* acBus = dynamic_cast<griddyn::AcBus*>(simulation->find("BUS1"));
    ASSERT_NE(acBus, nullptr);
    EXPECT_EQ(fload->getFrequencyBus(), acBus);
    EXPECT_NEAR(acBus->get("tf"), 0.02, 1.0e-12);
    EXPECT_NEAR(acBus->get("tw"), 0.02, 1.0e-12);
    EXPECT_NEAR(acBus->get("fn"), 60.0, 1.0e-12);

    auto* pqLoad = simulation->find("BUS1::PQ_1");
    ASSERT_NE(pqLoad, nullptr);
    EXPECT_FALSE(pqLoad->isEnabled());

    auto* shuntTD = dynamic_cast<griddyn::loads::ShuntTD*>(simulation->find("BUS1::ShuntTD_1"));
    ASSERT_NE(shuntTD, nullptr);
    EXPECT_EQ(shuntTD->outputNames().size(), 5U);

    auto* shuntSw = dynamic_cast<griddyn::loads::Svd*>(simulation->find("BUS1::ShuntSw_1"));
    ASSERT_NE(shuntSw, nullptr);
    EXPECT_NEAR(shuntSw->get("vref"), 1.0, 1.0e-12);
    EXPECT_NEAR(shuntSw->get("dv"), 0.05, 1.0e-12);
    EXPECT_NEAR(shuntSw->get("dt"), 0.2, 1.0e-12);
    EXPECT_NEAR(shuntSw->get("min_iter"), 2.0, 1.0e-12);
    EXPECT_NEAR(shuntSw->get("err_tol"), 0.01, 1.0e-12);
    EXPECT_EQ(shuntSw->get("andesstep"), 2.0);
    EXPECT_NEAR(shuntSw->get("effectiveb"), 0.1, 1.0e-12);

    ASSERT_EQ(simulation->dynInitialize(), 0);
    EXPECT_GE(acBus->diffSize(griddyn::cDaeSolverMode), 2U);
}

TEST(AndesShuntModelReaderTests, KeepsInvalidFLoadBusFrequencyLinkLocal)
{
    auto simulation = std::make_unique<griddyn::GridDynSimulation>();
    EXPECT_NO_THROW(griddyn::loadFile(simulation.get(),
                                      makeComparisonTestPath("shunt_model_invalid_busf.json")));

    auto* fload = dynamic_cast<griddyn::loads::FDepLoad*>(simulation->find("BUS1::FLoad_1"));
    ASSERT_NE(fload, nullptr);
    EXPECT_NEAR(fload->get("kp"), 90.0, 1.0e-12);
    EXPECT_NEAR(fload->get("kq"), 80.0, 1.0e-12);
    EXPECT_EQ(fload->getFrequencyBus(), nullptr);
    EXPECT_EQ(simulation->dynInitialize(), 0);
    EXPECT_NEAR(fload->getRealPower(), 0.45, 1.0e-12);
    EXPECT_NEAR(fload->getReactivePower(), 0.16, 1.0e-12);
}
