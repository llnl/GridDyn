/*
 * Copyright (c) 2014-2026, Lawrence Livermore National Security
 * See the top-level NOTICE for additional details. All rights reserved.
 * SPDX-License-Identifier: BSD-3-Clause
 */

#include "../gtestHelper.h"
#include "core/CoreExceptions.h"
#include "griddyn/GridBus.h"
#include "griddyn/Load.h"
#include "griddyn/Relay.h"
#include "griddyn/links/AcLine.h"
#include "griddyn/primary/AcBus.h"
#include "griddyn/simulation/GridDynActions.h"
#include "griddyn/solvers/SolverInterface.h"
#include <cmath>
#include <filesystem>
#include <fstream>
#include <gtest/gtest.h>
#include <memory>
#include <string>
#include <string_view>

namespace {
using griddyn::ActionError;
using griddyn::GridDynAction;
using Action = GridDynAction::GdAction;

constexpr char dynamicCase[] = GRIDDYN_TEST_DIRECTORY "/dyn_tests1/test_dynSimple1.xml";
constexpr char staticCase[] = GRIDDYN_TEST_DIRECTORY "/pFlow_tests/continuation_two_bus.xml";
constexpr char scriptDirectory[] = GRIDDYN_TEST_DIRECTORY "/runnerTests/scripts/";
}  // namespace

TEST(GridDynActionTests, ParsesDynamicModeAndOptionalTimes)
{
    struct TestCase {
        std::string_view mText;
        Action mMode;
        double mStop;
        double mStep;
    };
    constexpr double omitted = griddyn::kNullVal;
    constexpr TestCase cases[]{
        {.mText = "dynamic", .mMode = Action::DYNAMIC_DEFAULT, .mStop = omitted, .mStep = omitted},
        {.mText = "dynamic 0.1", .mMode = Action::DYNAMIC_DEFAULT, .mStop = 0.1, .mStep = omitted},
        {.mText = "dynamic 0.1 0.02 # times in seconds",
         .mMode = Action::DYNAMIC_DEFAULT,
         .mStop = 0.1,
         .mStep = 0.02},
        {.mText = "dynamic dae", .mMode = Action::DYNAMIC_DAE, .mStop = omitted, .mStep = omitted},
        {.mText = "dynamic DAE 0.1", .mMode = Action::DYNAMIC_DAE, .mStop = 0.1, .mStep = omitted},
        {.mText = "dynamic part",
         .mMode = Action::DYNAMIC_PART,
         .mStop = omitted,
         .mStep = omitted},
        {.mText = "dynamic partitioned 0.1 0.02",
         .mMode = Action::DYNAMIC_PART,
         .mStop = 0.1,
         .mStep = 0.02},
        {.mText = "dynamic decoupled 0.1",
         .mMode = Action::DYNAMIC_DECOUPLED,
         .mStop = 0.1,
         .mStep = omitted},
        {.mText = "dynamicdae 0.1", .mMode = Action::DYNAMIC_DAE, .mStop = 0.1, .mStep = omitted},
        {.mText = "dynamicpart 0.1 0.02",
         .mMode = Action::DYNAMIC_PART,
         .mStop = 0.1,
         .mStep = 0.02},
        {.mText = "dynamicdecoupled 0.1 0.02",
         .mMode = Action::DYNAMIC_DECOUPLED,
         .mStop = 0.1,
         .mStep = 0.02},
    };
    for (const auto& test : cases) {
        SCOPED_TRACE(test.mText);
        const GridDynAction action(test.mText);
        EXPECT_EQ(action.command, test.mMode);
        EXPECT_DOUBLE_EQ(action.val_double, test.mStop);
        EXPECT_DOUBLE_EQ(action.val_double2, test.mStep);
    }
}

TEST(GridDynActionTests, RejectsIncompleteOrUnrecognizedOperands)
{
    constexpr std::string_view cases[]{"dynamic unknown",
                                       "dynamic unknown 0.1",
                                       "dynamic 0.1s",
                                       "contingency",
                                       "contingency simplified",
                                       "checkpoint",
                                       "checkpoint \"\"",
                                       "checkpoint \"unterminated file.h5",
                                       "rollback",
                                       "rollback file.h5 local extra",
                                       "load state",
                                       "save powerflow-checkpoint unquoted file.json",
                                       "continuation",
                                       "continuation 1e999",
                                       "continuation 2.0bad",
                                       "continuation 2 target",
                                       "continuation 2 target -1",
                                       "continuation 2 target 0.5 target 1",
                                       "continuation 2 monitor 10,",
                                       "continuation 2 monitor 999999999999999999999999",
                                       "opf ac",
                                       "opf native preview extra",
                                       "action",
                                       "action \"unterminated script.gds",
                                       "action script.gds extra",
                                       "add",
                                       "add event # no description",
                                       "add event \"\"",
                                       "get",
                                       "get \"\"",
                                       "get bus1:voltage \"unterminated file.txt",
                                       "get bus1:voltage file.txt extra",
                                       "get --help bus1 extra",
                                       "contingency simplified # line selected.csv 0 1"};
    GridDynAction action;
    for (const auto text : cases) {
        SCOPED_TRACE(text);
        EXPECT_THROW(action.process(text), griddyn::InvalidParameterValue);
    }
}

TEST(GridDynActionQueueTests, AddsAnEventAfterAdvancingTheSimulation)
{
    auto simulation = griddyn::readSimXMLFile(dynamicCase);
    ASSERT_NE(simulation, nullptr);
    const auto* bus = simulation->getBus(0);
    ASSERT_NE(bus, nullptr);
    const auto* load = bus->getLoad(0);
    ASSERT_NE(load, nullptr);
    simulation->addActions("run 0.1; add event @0.15 | bus1::load#0:p(MW) = 110 # later change\n"
                           "run 0.2");

    ASSERT_EQ(simulation->run(0.14), FUNCTION_EXECUTION_SUCCESS);
    EXPECT_EQ(simulation->getSimulationTime(), griddyn::CoreTime(0.14));
    EXPECT_EQ(simulation->getEventTime(), griddyn::CoreTime(0.15));
    EXPECT_DOUBLE_EQ(load->get("p"), 1.5);

    ASSERT_EQ(simulation->run(), FUNCTION_EXECUTION_SUCCESS);
    EXPECT_EQ(simulation->getSimulationTime(), griddyn::CoreTime(0.2));
    EXPECT_DOUBLE_EQ(load->get("p"), 1.1);
}

TEST(GridDynActionQueueTests, AddsRepeatingAndCompoundEventsFromDescriptions)
{
    auto simulation = griddyn::readSimXMLFile(dynamicCase);
    ASSERT_NE(simulation, nullptr);
    const auto* bus = simulation->getBus(0);
    ASSERT_NE(bus, nullptr);
    const auto* load = bus->getLoad(0);
    ASSERT_NE(load, nullptr);
    simulation->addActions("add event bus1::load3:p=1.2,1.1 @0.1,0.2+0.3; "
                           "add event \"@0.25>bus1::load3:p=1.25;bus1::load3:q=0.05\"; run 0.5");

    ASSERT_EQ(simulation->run(0.15), FUNCTION_EXECUTION_SUCCESS);
    EXPECT_DOUBLE_EQ(load->get("p"), 1.2);
    ASSERT_EQ(simulation->run(0.22), FUNCTION_EXECUTION_SUCCESS);
    EXPECT_DOUBLE_EQ(load->get("p"), 1.1);
    ASSERT_EQ(simulation->run(0.3), FUNCTION_EXECUTION_SUCCESS);
    EXPECT_DOUBLE_EQ(load->get("p"), 1.25);
    EXPECT_DOUBLE_EQ(load->get("q"), 0.05);
    ASSERT_EQ(simulation->run(0.45), FUNCTION_EXECUTION_SUCCESS);
    EXPECT_DOUBLE_EQ(load->get("p"), 1.2);
}

TEST(GridDynActionQueueTests, InvalidEventDescriptionsStopTheQueueWithoutScheduling)
{
    auto simulation = griddyn::readSimXMLFile(dynamicCase);
    ASSERT_NE(simulation, nullptr);
    ASSERT_EQ(simulation->execute("run 0.1"), FUNCTION_EXECUTION_SUCCESS);
    const auto nextEvent = simulation->getEventTime();
    struct TestCase {
        std::string_view mCommand;
        ActionError::Kind mKind;
    };
    constexpr TestCase cases[]{
        {.mCommand = "add event @0.05|bus1::load3:p=1.1", .mKind = ActionError::Kind::EXECUTION},
        {.mCommand = "add event @0.15|bus999:p=1.1", .mKind = ActionError::Kind::TARGET},
        {.mCommand = "add event \"@0.15|bus1::load3:p=1.1;bus999:q=0.05\"",
         .mKind = ActionError::Kind::TARGET},
        {.mCommand = "add event bus1::load3:p=1.1", .mKind = ActionError::Kind::SYNTAX},
        {.mCommand = "add event @0.15|bus1::load3:p=1.1junk", .mKind = ActionError::Kind::SYNTAX},
        {.mCommand = "add event @0.15|bus1::load3:p(MW=110", .mKind = ActionError::Kind::SYNTAX},
        {.mCommand = "add event @0.15|bus1::load3:p=nan", .mKind = ActionError::Kind::SYNTAX},
        {.mCommand = "add event @1e400|bus1::load3:p=1.1", .mKind = ActionError::Kind::SYNTAX},
        {.mCommand = "add event @0.2,0.15|bus1::load3:p=1.1", .mKind = ActionError::Kind::SYNTAX},
        {.mCommand = "add event @0.2,0.3|bus1::load3:p=1.1,1.2,1.3",
         .mKind = ActionError::Kind::SYNTAX},
        {.mCommand = "add recorder bus1:voltage", .mKind = ActionError::Kind::UNSUPPORTED},
    };
    for (const auto& test : cases) {
        SCOPED_TRACE(test.mCommand);
        simulation->setName("before_event");
        simulation->add(test.mCommand);
        simulation->add("set name after_event");
        try {
            simulation->run();
            ADD_FAILURE() << "expected the add action to fail";
        }
        catch (const ActionError& error) {
            EXPECT_EQ(error.kind(), test.mKind) << error.what();
            EXPECT_TRUE(std::string_view(error.what()).contains(test.mCommand)) << error.what();
        }
        EXPECT_EQ(simulation->getName(), "before_event");
        EXPECT_EQ(simulation->getEventTime(), nextEvent);
        ASSERT_EQ(simulation->run(), FUNCTION_EXECUTION_SUCCESS);
        EXPECT_EQ(simulation->getName(), "after_event");
    }
}

TEST(GridDynActionTests, FieldQueryErrorsStopQueuedActions)
{
    griddyn::GridDynSimulation uninitialized;
    EXPECT_THROW(uninitialized.execute("get iterationcount"), ActionError);

    auto simulation = griddyn::readSimXMLFile(dynamicCase);
    ASSERT_NE(simulation, nullptr);
    struct TestCase {
        std::string_view mCommand;
        ActionError::Kind mKind;
    };
    constexpr TestCase cases[]{
        {.mCommand = "get bus999:voltage", .mKind = ActionError::Kind::TARGET},
        {.mCommand = "get bus1::load#0extra:p", .mKind = ActionError::Kind::TARGET},
        {.mCommand = "get bus1:missing_field", .mKind = ActionError::Kind::TARGET},
        {.mCommand = "get bus1:", .mKind = ActionError::Kind::SYNTAX},
        {.mCommand = "get bus1::load3:p(MW", .mKind = ActionError::Kind::SYNTAX},
        {.mCommand = "get bus1::load3:p()", .mKind = ActionError::Kind::SYNTAX},
        {.mCommand = "get bus1::load3:p(m^^2)", .mKind = ActionError::Kind::SYNTAX},
        {.mCommand = "get bus1::load3:p(s)", .mKind = ActionError::Kind::TARGET},
        {.mCommand = "get bus1:name(V)", .mKind = ActionError::Kind::TARGET},
        {.mCommand = "get name .", .mKind = ActionError::Kind::EXECUTION},
        {.mCommand = "get --help nonexistent", .mKind = ActionError::Kind::TARGET},
    };
    for (const auto& test : cases) {
        SCOPED_TRACE(test.mCommand);
        simulation->setName("before_query");
        simulation->add(test.mCommand);
        simulation->add("set name after_query");
        try {
            simulation->run();
            ADD_FAILURE() << "expected query failure";
        }
        catch (const ActionError& error) {
            EXPECT_EQ(error.kind(), test.mKind) << error.what();
            EXPECT_TRUE(std::string_view(error.what()).contains(test.mCommand)) << error.what();
        }
        EXPECT_EQ(simulation->getName(), "before_query");
        ASSERT_EQ(simulation->run(), FUNCTION_EXECUTION_SUCCESS);
        EXPECT_EQ(simulation->getName(), "after_query");
    }
}

TEST(GridDynActionTests, FieldQueriesAppendValuesInRequestedUnits)
{
    auto simulation = griddyn::readSimXMLFile(dynamicCase);
    ASSERT_NE(simulation, nullptr);
    const auto reportFile = std::filesystem::temp_directory_path() / "griddyn gdc19 values.txt";
    std::filesystem::remove(reportFile);
    const auto destination = " \"" + reportFile.string() + "\"";
    const auto initialTime = simulation->getSimulationTime();
    ASSERT_EQ(simulation->execute("set name NA"), FUNCTION_EXECUTION_SUCCESS);
    simulation->setDescription("\"Readings\"\\load\r\n\t\x01");
    simulation->add("get name" + destination);
    simulation->add("get description" + destination);
    simulation->add("get bus1::load#0:p(MW)" + destination);
    simulation->add("get \"bus1::load3:load p(MW)\"" + destination);
    ASSERT_EQ(simulation->run(), FUNCTION_EXECUTION_SUCCESS);
    EXPECT_EQ(simulation->getSimulationTime(), initialTime);
    EXPECT_EQ(simulation->getName(), "NA");

    std::ifstream report(reportFile);
    std::string name;
    ASSERT_TRUE(std::getline(report, name));
    EXPECT_EQ(name, "\"NA\"");
    std::string description;
    ASSERT_TRUE(std::getline(report, description));
    constexpr std::string_view expectedDescription = R"("\"Readings\"\\load\r\n\t\u{1}")";
    EXPECT_EQ(description, expectedDescription);
    double powerByIndex = 0.0;
    double powerByName = 0.0;
    ASSERT_TRUE(report >> powerByIndex >> powerByName);
    EXPECT_DOUBLE_EQ(powerByIndex, 150.0);
    EXPECT_DOUBLE_EQ(powerByName, 150.0);
    report.close();
    std::filesystem::remove(reportFile);
}

TEST(GridDynActionQueueTests, IncludesScriptsInPlaceAndHonorsTimeBound)
{
    auto simulation = griddyn::readSimXMLFile(dynamicCase);
    ASSERT_NE(simulation, nullptr);
    const auto* bus = simulation->getBus(0);
    ASSERT_NE(bus, nullptr);
    const auto* load = bus->getLoad(0);
    ASSERT_NE(load, nullptr);
    const auto originalPower = load->get("p");
    const auto script = std::string(scriptDirectory) + "outer actions.gds";

    simulation->addScriptFile(script);
    simulation->add("set name after_script");
    ASSERT_EQ(simulation->run(0.1), FUNCTION_EXECUTION_SUCCESS);
    EXPECT_EQ(simulation->getSimulationTime(), griddyn::CoreTime(0.1));
    EXPECT_EQ(simulation->getName(), "before_nested");
    EXPECT_DOUBLE_EQ(load->get("p"), originalPower);

    ASSERT_EQ(simulation->run(), FUNCTION_EXECUTION_SUCCESS);
    EXPECT_EQ(simulation->getSimulationTime(), griddyn::CoreTime(0.2));
    EXPECT_EQ(simulation->getName(), "after_script");
    EXPECT_DOUBLE_EQ(load->get("p"), 1.75);

    simulation->add("set name queued");
    ASSERT_EQ(simulation->execute("action \"" + script + "\""), FUNCTION_EXECUTION_SUCCESS);
    EXPECT_EQ(simulation->getName(), "after_nested");
    ASSERT_EQ(simulation->run(), FUNCTION_EXECUTION_SUCCESS);
    EXPECT_EQ(simulation->getName(), "queued");
}

TEST(GridDynActionQueueTests, ScriptErrorsReportLineAndLeaveLaterActionsPending)
{
    griddyn::GridDynSimulation simulation;
    simulation.addScriptFile(std::string(scriptDirectory) + "broken.gds");
    simulation.add("set name after_script");
    try {
        simulation.run();
        ADD_FAILURE() << "expected the script syntax error";
    }
    catch (const ActionError& error) {
        EXPECT_EQ(error.kind(), ActionError::Kind::SYNTAX);
        EXPECT_TRUE(std::string_view(error.what()).contains("broken.gds:3: action 'badverb'"))
            << error.what();
    }
    EXPECT_EQ(simulation.getName(), "before_error");
    ASSERT_EQ(simulation.run(), FUNCTION_EXECUTION_SUCCESS);
    EXPECT_EQ(simulation.getName(), "after_script");
}

TEST(GridDynActionQueueTests, RejectsMissingFilesAndIncludeCyclesAtTheirPosition)
{
    griddyn::GridDynSimulation simulation;
    simulation.add("set name before_missing");
    simulation.addScriptFile(std::string(scriptDirectory) + "missing.gds");
    simulation.add("set name before_cycle");
    simulation.addScriptFile(std::string(scriptDirectory) + "cycle.gds");
    try {
        simulation.run();
        ADD_FAILURE() << "expected the missing script error";
    }
    catch (const ActionError& error) {
        EXPECT_EQ(error.kind(), ActionError::Kind::EXECUTION);
        EXPECT_TRUE(std::string_view(error.what()).contains("unable to open script file"))
            << error.what();
    }
    EXPECT_EQ(simulation.getName(), "before_missing");

    try {
        simulation.run();
        ADD_FAILURE() << "expected the include cycle error";
    }
    catch (const ActionError& error) {
        EXPECT_EQ(error.kind(), ActionError::Kind::EXECUTION);
        EXPECT_TRUE(std::string_view(error.what()).contains("cycle.gds:1: action")) << error.what();
        EXPECT_TRUE(std::string_view(error.what()).contains("script include cycle"))
            << error.what();
    }
    EXPECT_EQ(simulation.getName(), "before_cycle");
    ASSERT_EQ(simulation.run(), FUNCTION_EXECUTION_SUCCESS);
    EXPECT_EQ(simulation.getName(), "after_cycle");
}

TEST(GridDynActionQueueTests, EmptyScriptDoesNotAdvanceSimulation)
{
    auto simulation = griddyn::readSimXMLFile(dynamicCase);
    ASSERT_NE(simulation, nullptr);
    const auto time = simulation->getSimulationTime();
    const auto name = simulation->getName();
    simulation->addScriptFile(std::string(scriptDirectory) + "empty.gds");
    ASSERT_EQ(simulation->run(), FUNCTION_EXECUTION_SUCCESS);
    EXPECT_EQ(simulation->getSimulationTime(), time);
    EXPECT_EQ(simulation->getName(), name);
}

TEST(GridDynActionTests, ReprocessingClearsContingencyFlag)
{
    GridDynAction action("contingency simplified line selected.csv 0 1");
    ASSERT_EQ(action.command, Action::CONTINGENCY);
    EXPECT_EQ(action.flag, 1);
    EXPECT_EQ(action.string1, "line");
    EXPECT_EQ(action.string2, "selected.csv");
    EXPECT_EQ(action.val_int1, 0);
    EXPECT_EQ(action.val_int2, 1);

    action.process("contingency gen");
    EXPECT_EQ(action.command, Action::CONTINGENCY);
    EXPECT_EQ(action.flag, 0);
    EXPECT_EQ(action.string1, "gen");
    EXPECT_TRUE(action.string2.empty());
    EXPECT_EQ(action.val_int1, -1);
    EXPECT_EQ(action.val_int2, -1);
}

TEST(GridDynActionTests, QueuedDynamicUsesSolverConfiguredAtExecution)
{
    struct TestCase {
        std::string_view mMethod;
        std::string_view mSolver;
    };
    constexpr TestCase cases[]{{.mMethod = "dae", .mSolver = "dae"},
                               {.mMethod = "partitioned", .mSolver = "differential"}};
    for (const auto& test : cases) {
        SCOPED_TRACE(test.mMethod);
        auto simulation = griddyn::readSimXMLFile(dynamicCase);
        ASSERT_NE(simulation, nullptr);
        simulation->add("dynamic 0.1");
        simulation->set("dynamicsolvermethod", test.mMethod);

        ASSERT_EQ(simulation->run(), FUNCTION_EXECUTION_SUCCESS);
        EXPECT_NEAR(static_cast<double>(simulation->getSimulationTime()), 0.1, 1e-8);
        const auto solver = simulation->getSolverInterface(test.mSolver);
        ASSERT_NE(solver, nullptr);
        EXPECT_GT(solver->get("solvercount"), 0.0);

        simulation->set("stoptime", 0.2);
        ASSERT_EQ(simulation->execute("dynamic"), FUNCTION_EXECUTION_SUCCESS);
        EXPECT_NEAR(static_cast<double>(simulation->getSimulationTime()), 0.2, 1e-8);
    }
}

TEST(GridDynActionTests, ExplicitDaeRequiresPriorSetupWithPartitionedDefault)
{
    auto simulation = griddyn::readSimXMLFile(dynamicCase);
    ASSERT_NE(simulation, nullptr);
    simulation->set("dynamicsolvermethod", "partitioned");
    const auto dae = simulation->getSolverMode("dae").offsetIndex;
    EXPECT_THROW(simulation->execute("dynamic dae 0.1"), ActionError);
    EXPECT_THROW(simulation->execute("step"), ActionError);
    EXPECT_EQ(simulation->getSolverInterface(dae), nullptr);
    EXPECT_EQ(simulation->currentProcessState(), griddyn::GridSimulation::GridState::STARTUP);

    ASSERT_EQ(simulation->execute("set defdae dae"), FUNCTION_EXECUTION_SUCCESS);
    ASSERT_EQ(simulation->execute("dynamic dae 0.1"), FUNCTION_EXECUTION_SUCCESS);
    EXPECT_NEAR(static_cast<double>(simulation->getSimulationTime()), 0.1, 1e-8);
    const auto solver = simulation->getSolverInterface("dae");
    ASSERT_NE(solver, nullptr);
    EXPECT_GT(solver->get("solvercount"), 0.0);
}

TEST(GridDynActionTests, UnconfiguredPartitionedModeFailsWithoutChangingState)
{
    auto simulation = griddyn::readSimXMLFile(dynamicCase);
    ASSERT_NE(simulation, nullptr);
    const auto algebraic = simulation->getSolverMode("dynalg").offsetIndex;
    const auto differential = simulation->getSolverMode("dyndiff").offsetIndex;

    for (const bool initialized : {false, true}) {
        SCOPED_TRACE(initialized);
        if (initialized) {
            ASSERT_EQ(simulation->dynInitialize(), FUNCTION_EXECUTION_SUCCESS);
        }
        const auto time = simulation->getSimulationTime();
        const auto state = simulation->currentProcessState();
        try {
            simulation->execute("dynamic part 0.2 0.05");
            ADD_FAILURE() << "expected an unconfigured solver error";
        }
        catch (const ActionError& error) {
            EXPECT_EQ(error.kind(), ActionError::Kind::TARGET);
            EXPECT_NE(std::string_view(error.what()).find("set dynamicsolvermethod partitioned"),
                      std::string_view::npos);
        }
        EXPECT_EQ(simulation->getSimulationTime(), time);
        EXPECT_EQ(simulation->currentProcessState(), state);
        EXPECT_EQ(simulation->getSolverInterface(algebraic), nullptr);
        EXPECT_EQ(simulation->getSolverInterface(differential), nullptr);
    }
}

TEST(GridDynActionTests, PriorQueueConfigurationAllowsPartitionedRunAfterDae)
{
    auto simulation = griddyn::readSimXMLFile(dynamicCase);
    ASSERT_NE(simulation, nullptr);
    ASSERT_EQ(simulation->run(0.1), FUNCTION_EXECUTION_SUCCESS);
    const auto dae = simulation->getSolverInterface("dae");
    ASSERT_NE(dae, nullptr);
    const auto daeCalls = dae->get("solvercount");

    simulation->add("set dynamicsolvermethod partitioned");
    simulation->add("run 0.2");
    ASSERT_EQ(simulation->run(), FUNCTION_EXECUTION_SUCCESS);
    EXPECT_NEAR(static_cast<double>(simulation->getSimulationTime()), 0.2, 1e-8);
    EXPECT_NEAR(simulation->getBus(0)->getVoltage(), 1.0, 1e-8);
    EXPECT_DOUBLE_EQ(dae->get("solvercount"), daeCalls);
    const auto differential = simulation->getSolverInterface("differential");
    ASSERT_NE(differential, nullptr);
    EXPECT_GT(differential->get("solvercount"), 0.0);
}

TEST(GridDynActionTests, ExplicitPartitionedModeUsesPreviouslyRegisteredSolverRoles)
{
    auto simulation = griddyn::readSimXMLFile(dynamicCase);
    ASSERT_NE(simulation, nullptr);
    const auto dae = simulation->getSolverMode("dae").offsetIndex;

    ASSERT_EQ(simulation->execute("set defdyndiff dyndiff"), FUNCTION_EXECUTION_SUCCESS);
    EXPECT_THROW(simulation->execute("dynamic part 0.2 0.05"), ActionError);
    ASSERT_EQ(simulation->execute("set defdynalg dynalg"), FUNCTION_EXECUTION_SUCCESS);
    ASSERT_EQ(simulation->execute("dynamic part 0.2 0.05"), FUNCTION_EXECUTION_SUCCESS);
    EXPECT_NEAR(static_cast<double>(simulation->getSimulationTime()), 0.2, 1e-8);
    EXPECT_NEAR(simulation->getBus(0)->getVoltage(), 1.0, 1e-8);
    EXPECT_EQ(simulation->getSolverInterface(dae), nullptr);
}

TEST(GridDynActionQueueTests, QueuedRunAdvancesModelInOrder)
{
    auto simulation = griddyn::readSimXMLFile(dynamicCase);
    ASSERT_NE(simulation, nullptr);
    simulation->set("stoptime", 0.3);
    simulation->add("# before the first action");
    simulation->add("set bus1::load3:p 1.6");
    simulation->add("run 0.2");
    simulation->add("set bus1::load3:p+ 0.1");
    simulation->add("run");
    simulation->add("ignore trailing text");

    ASSERT_EQ(simulation->execute("run 0.1"), FUNCTION_EXECUTION_SUCCESS);
    EXPECT_NEAR(static_cast<double>(simulation->getSimulationTime()), 0.1, 1e-8);
    const auto* load = simulation->getBus(0)->getLoad(0);
    ASSERT_NE(load, nullptr);
    EXPECT_NEAR(load->get("p"), 1.5, 1e-8);

    ASSERT_EQ(simulation->run(), FUNCTION_EXECUTION_SUCCESS);
    EXPECT_NEAR(static_cast<double>(simulation->getSimulationTime()), 0.3, 1e-8);
    EXPECT_NEAR(load->get("p"), 1.7, 1e-8);
}

TEST(GridDynActionQueueTests, NoOpQueueFallsBackToConfiguredRun)
{
    auto simulation = griddyn::readSimXMLFile(dynamicCase);
    ASSERT_NE(simulation, nullptr);
    simulation->set("stoptime", 0.2);
    simulation->set("action", " ; # comment only ; ignore trailing text");
    GridDynAction ignored(Action::IGNORE_ACTION);
    simulation->add(ignored);

    ASSERT_EQ(simulation->run(), FUNCTION_EXECUTION_SUCCESS);
    EXPECT_NEAR(static_cast<double>(simulation->getSimulationTime()), 0.2, 1e-8);
}

TEST(GridDynActionQueueTests, TimeBoundPausesAdvancesAndPreservesOrder)
{
    auto simulation = griddyn::readSimXMLFile(dynamicCase);
    ASSERT_NE(simulation, nullptr);
    simulation->add("set bus1::load3:p+ 0.1");
    simulation->add("run 0.2");
    simulation->add("set name halfway");
    simulation->add("dynamic 0.4");
    simulation->add("set name finished");
    const auto* load = simulation->getBus(0)->getLoad(0);
    ASSERT_NE(load, nullptr);

    for (int attempt = 0; attempt < 2; ++attempt) {
        ASSERT_EQ(simulation->run(0.1), FUNCTION_EXECUTION_SUCCESS);
        EXPECT_NEAR(static_cast<double>(simulation->getSimulationTime()), 0.1, 1e-8);
        EXPECT_EQ(simulation->getName(), "test1");
        EXPECT_NEAR(load->get("p"), 1.6, 1e-8);
    }

    ASSERT_EQ(simulation->run(0.3), FUNCTION_EXECUTION_SUCCESS);
    EXPECT_NEAR(static_cast<double>(simulation->getSimulationTime()), 0.3, 1e-8);
    EXPECT_EQ(simulation->getName(), "halfway");

    ASSERT_EQ(simulation->run(0.4), FUNCTION_EXECUTION_SUCCESS);
    EXPECT_NEAR(static_cast<double>(simulation->getSimulationTime()), 0.4, 1e-8);
    EXPECT_EQ(simulation->getName(), "finished");
    EXPECT_NEAR(load->get("p"), 1.6, 1e-8);
}

TEST(GridDynActionQueueTests, TimeBoundAppliesToEventModeAndIterate)
{
    for (const auto* command : {"eventmode 0.2 0.01", "iterate 0.01 0.2"}) {
        SCOPED_TRACE(command);
        auto simulation = griddyn::readSimXMLFile(staticCase);
        ASSERT_NE(simulation, nullptr);
        simulation->add(command);
        simulation->add("set name finished");

        ASSERT_EQ(simulation->run(0.1), FUNCTION_EXECUTION_SUCCESS);
        EXPECT_NEAR(static_cast<double>(simulation->getSimulationTime()), 0.1, 1e-8);
        EXPECT_NE(simulation->getName(), "finished");

        ASSERT_EQ(simulation->run(), FUNCTION_EXECUTION_SUCCESS);
        EXPECT_NEAR(static_cast<double>(simulation->getSimulationTime()), 0.2, 1e-8);
        EXPECT_EQ(simulation->getName(), "finished");
    }
}

TEST(GridDynActionQueueTests, FailureAndExceptionLeaveFollowingActionsForRetry)
{
    auto simulation = griddyn::readSimXMLFile(dynamicCase);
    ASSERT_NE(simulation, nullptr);
    simulation->add("set bus1::load3:p+ 0.1");
    simulation->add("check powerflow .");  // A directory cannot be opened as a report file.
    simulation->add("set name between_failures");
    simulation->add("set dynamicsolvermethod unknown");
    simulation->add("run 0.2");
    simulation->add("set name finished");

    EXPECT_NE(simulation->run(), FUNCTION_EXECUTION_SUCCESS);
    EXPECT_NE(simulation->getLastActionError().find("check powerflow ."), std::string::npos);
    EXPECT_EQ(simulation->getName(), "test1");
    try {
        simulation->run();
        FAIL() << "expected an invalid solver method error";
    }
    catch (const ActionError& error) {
        EXPECT_EQ(error.kind(), ActionError::Kind::SYNTAX);
        EXPECT_NE(std::string_view(error.what()).find("set dynamicsolvermethod unknown"),
                  std::string_view::npos);
    }
    EXPECT_EQ(simulation->getName(), "between_failures");

    ASSERT_EQ(simulation->run(), FUNCTION_EXECUTION_SUCCESS);
    EXPECT_NEAR(static_cast<double>(simulation->getSimulationTime()), 0.2, 1e-8);
    EXPECT_EQ(simulation->getName(), "finished");
    const auto* load = simulation->getBus(0)->getLoad(0);
    ASSERT_NE(load, nullptr);
    EXPECT_NEAR(load->get("p"), 1.6, 1e-8);
}

TEST(GridDynActionQueueTests, SyntaxErrorStopsAtItsPositionInTheScript)
{
    auto simulation = griddyn::readSimXMLFile(dynamicCase);
    ASSERT_NE(simulation, nullptr);
    simulation->addActions("set name before; badverb; set name after", "script.xml");

    try {
        simulation->run();
        FAIL() << "expected a syntax error";
    }
    catch (const ActionError& error) {
        EXPECT_EQ(error.kind(), ActionError::Kind::SYNTAX);
        EXPECT_NE(std::string_view(error.what()).find("script.xml: action 'badverb'"),
                  std::string_view::npos);
    }
    EXPECT_EQ(simulation->getName(), "before");
    ASSERT_EQ(simulation->run(), FUNCTION_EXECUTION_SUCCESS);
    EXPECT_EQ(simulation->getName(), "after");
}

TEST(GridDynActionTests, ReportsDirectCommandErrorCategories)
{
    griddyn::GridDynSimulation simulation;
    struct TestCase {
        std::string_view mCommand;
        ActionError::Kind mKind;
    };
    constexpr TestCase cases[]{
        {.mCommand = "contingency", .mKind = ActionError::Kind::SYNTAX},
        {.mCommand = "set missing:voltage 1.02", .mKind = ActionError::Kind::TARGET},
        {.mCommand = "set @missing:voltage 1.02", .mKind = ActionError::Kind::TARGET},
        {.mCommand = "set :voltage 1.02", .mKind = ActionError::Kind::TARGET},
        {.mCommand = "setsolver dae missing", .mKind = ActionError::Kind::TARGET},
        {.mCommand = "checkpoint capture.h5 missing", .mKind = ActionError::Kind::TARGET},
        {.mCommand = "rollback capture.h5 everything", .mKind = ActionError::Kind::SYNTAX},
        {.mCommand = "load unknown capture.h5", .mKind = ActionError::Kind::UNSUPPORTED},
        {.mCommand = "print voltage destination", .mKind = ActionError::Kind::UNSUPPORTED},
        {.mCommand = "continuation", .mKind = ActionError::Kind::SYNTAX},
        {.mCommand = "opf", .mKind = ActionError::Kind::UNSUPPORTED},
    };
    for (const auto& test : cases) {
        SCOPED_TRACE(test.mCommand);
        try {
            simulation.execute(test.mCommand);
            FAIL() << "expected a command error";
        }
        catch (const ActionError& error) {
            EXPECT_EQ(error.kind(), test.mKind);
            EXPECT_NE(std::string_view(error.what()).find(test.mCommand), std::string_view::npos);
        }
    }
}

TEST(GridDynActionTests, InvalidObjectSelectorsLeaveTargetsUnchanged)
{
    auto simulation = griddyn::readSimXMLFile(dynamicCase);
    ASSERT_NE(simulation, nullptr);
    const auto* bus = simulation->getBus(0);
    ASSERT_NE(bus, nullptr);
    const auto* load = bus->getLoad(0);
    ASSERT_NE(load, nullptr);
    const auto voltage = bus->get("voltage");
    const auto power = load->get("p");

    constexpr std::string_view commands[]{
        "set missing:voltage 1.02",
        "set @missing:voltage 1.02",
        "set :voltage 1.02",
        "set bus1:bogus:voltage 1.02",
        "set bus1::voltage 1.02",
        "set bus1//load3:p 0.25",
        "set bus1:::load3:p 0.25",
        "set bus1::missing:p 0.25",
        "set bus1::load3?bogus:p 0.25",
        "set bus1::load#0extra:p 0.25",
        "set bus1::load#+-0:p 0.25",
        "set bus#999999999999999999999:voltage 1.02",
    };
    for (const auto command : commands) {
        SCOPED_TRACE(command);
        const auto expectedPrefix = std::string("action '").append(command).append("' failed: ");
        try {
            simulation->execute(command);
            ADD_FAILURE() << "expected an invalid object target";
        }
        catch (const ActionError& error) {
            EXPECT_EQ(error.kind(), ActionError::Kind::TARGET);
            EXPECT_TRUE(std::string_view(error.what()).starts_with(expectedPrefix)) << error.what();
        }
        EXPECT_DOUBLE_EQ(bus->get("voltage"), voltage);
        EXPECT_DOUBLE_EQ(load->get("p"), power);
    }
}

TEST(GridDynActionQueueTests, InvalidNestedTargetStopsBeforeValidUnitAssignment)
{
    auto simulation = griddyn::readSimXMLFile(dynamicCase);
    ASSERT_NE(simulation, nullptr);
    const auto* load = simulation->getBus(0)->getLoad(0);
    ASSERT_NE(load, nullptr);
    const auto power = load->get("p");
    simulation->addActions("set bus1::load3:bogus:p(MW) 50; set bus1::load3:p(MW) 25",
                           "targets.xml");

    try {
        simulation->run();
        ADD_FAILURE() << "expected an invalid nested target";
    }
    catch (const ActionError& error) {
        EXPECT_EQ(error.kind(), ActionError::Kind::TARGET);
        EXPECT_NE(std::string_view(error.what())
                      .find("targets.xml: action 'set bus1::load3:bogus:p(MW) 50'"),
                  std::string_view::npos);
    }
    EXPECT_DOUBLE_EQ(load->get("p"), power);

    ASSERT_EQ(simulation->run(), FUNCTION_EXECUTION_SUCCESS);
    EXPECT_DOUBLE_EQ(load->get("p", units::MW), 25.0);
    EXPECT_DOUBLE_EQ(load->get("p"), 0.25);
    ASSERT_EQ(simulation->execute("set bus#0/load#0?p(MW) 50"), FUNCTION_EXECUTION_SUCCESS);
    EXPECT_DOUBLE_EQ(load->get("p", units::MW), 50.0);
}

TEST(GridDynActionTests, RejectsMissingSolversInBothSolverCommandForms)
{
    griddyn::GridDynSimulation simulation;
    simulation.solverSet("powerflow", "printlevel", 2.0);
    const auto powerflow = simulation.getSolverInterface("powerflow");
    ASSERT_NE(powerflow, nullptr);
    const auto printLevel = powerflow->get("printlevel");
    EXPECT_DOUBLE_EQ(printLevel, 2.0);
    ASSERT_EQ(simulation.getSolverInterface(griddyn::cDaeSolverMode.offsetIndex), nullptr);

    const auto expectTargetError = [](const auto& operation) {
        try {
            operation();
            FAIL() << "expected a missing solver target";
        }
        catch (const ActionError& error) {
            EXPECT_EQ(error.kind(), ActionError::Kind::TARGET);
            EXPECT_NE(std::string_view(error.what()).find("solver 'missing'"),
                      std::string_view::npos);
        }
    };
    expectTargetError([&] { simulation.solverSet("missing", "printlevel", 0.0); });
    expectTargetError([&] { simulation.solverSet("missing", "printlevel", "none"); });

    GridDynAction setting;
    setting.command = Action::SETSOLVER;
    setting.string1 = "missing";
    setting.string2 = "printlevel";
    setting.val_double = 0.0;
    expectTargetError([&] { simulation.execute(setting); });

    constexpr std::string_view commands[]{"setsolver dae ida",
                                          "setsolver dae 999999999999999999999"};
    for (const auto command : commands) {
        SCOPED_TRACE(command);
        try {
            simulation.execute(command);
            FAIL() << "expected a missing solver target";
        }
        catch (const ActionError& error) {
            EXPECT_EQ(error.kind(), ActionError::Kind::TARGET);
            EXPECT_NE(std::string_view(error.what()).find(command), std::string_view::npos);
        }
    }
    EXPECT_EQ(simulation.getSolverInterface("missing"), nullptr);
    EXPECT_EQ(simulation.getSolverInterface(griddyn::cDaeSolverMode.offsetIndex), nullptr);
    EXPECT_DOUBLE_EQ(powerflow->get("printlevel"), printLevel);

    simulation.solverSet("powerflow", "printlevel", "none");
    EXPECT_DOUBLE_EQ(powerflow->get("printlevel"), 0.0);
}

TEST(GridDynActionTests, AppliesCommonLexicalRulesAcrossVerbs)
{
    struct TestCase {
        std::string_view mText;
        Action mCommand;
    };
    constexpr TestCase cases[]{
        {.mText = "\t# Comment", .mCommand = Action::IGNORE_ACTION},
        {.mText = "IGNORE extra text", .mCommand = Action::IGNORE_ACTION},
        {.mText = "SeT\tbus#0/load#0:p(MW)\t+25 # note", .mCommand = Action::SET_ACTION},
        {.mText = "setall GEN p(MW) 25", .mCommand = Action::SETALL},
        {.mText = "setsolver DAE SolverA", .mCommand = Action::SETSOLVER},
        {.mText = "print voltage stdout", .mCommand = Action::PRINT},
        {.mText = "initialize 0.1", .mCommand = Action::INITIALIZE},
        {.mText = "powerflow", .mCommand = Action::POWERFLOW},
        {.mText = "reset +1", .mCommand = Action::RESET},
        {.mText = "iterate 0.05 2", .mCommand = Action::ITERATE},
        {.mText = "eventmode 2 0.05", .mCommand = Action::EVENTMODE},
        {.mText = "step DAE 0.1", .mCommand = Action::STEP},
        {.mText = "run 2", .mCommand = Action::RUN},
        {.mText = "save RECORDER", .mCommand = Action::SAVE},
        {.mText = R"(load state "State A.h5")", .mCommand = Action::LOAD},
        {.mText = R"(check POWERFLOW --ASSERT "# Readings.txt")", .mCommand = Action::CHECK},
        {.mText = R"(add EVENT "@0.1|bus1::load#0:p=1")", .mCommand = Action::ADD},
        {.mText = R"(checkpoint "Frame #1.h5" DAE)", .mCommand = Action::CHECKPOINT},
        {.mText = "rollback capture.h5 MINIMAL", .mCommand = Action::ROLLBACK},
        {.mText = R"(contingency SIMPLIFIED LINE "Study A.csv" 2)",
         .mCommand = Action::CONTINGENCY},
        {.mText = "continuation 2 QLIMITS", .mCommand = Action::CONTINUATION},
        {.mText = "opf NATIVE PREVIEW", .mCommand = Action::OPF},
        {.mText = R"(action "Path With Spaces.gds")", .mCommand = Action::ACTION_FILE},
        {.mText = R"cmd(get "bus1::load3:load p(MW)")cmd", .mCommand = Action::GET_ACTION},
    };
    for (const auto& test : cases) {
        SCOPED_TRACE(test.mText);
        EXPECT_EQ(GridDynAction(test.mText).command, test.mCommand);
    }
    const GridDynAction pathAction(R"(get time "C:\Data\Study A.txt")");
    constexpr std::string_view expectedPath = R"(C:\Data\Study A.txt)";
    EXPECT_EQ(pathAction.string2, expectedPath);
    EXPECT_EQ(GridDynAction("set name 25xyz").string2, "25xyz");
    EXPECT_EQ(GridDynAction(R"(set name "25")").string2, "25");

    constexpr std::string_view rejected[]{"set name value extra",
                                          "setall load p 25xyz",
                                          "setsolver dae a b c",
                                          "print",
                                          "powerflow extra",
                                          "initialize nan",
                                          "initialize 1 2",
                                          "reset 99999999999999",
                                          "reset 1suffix",
                                          "reset -1",
                                          "reset 5",
                                          "iterate junk 2s",
                                          "iterate 0 2",
                                          "eventmode 2 0.0000000001",
                                          "eventmode 2 -1",
                                          "dynamic dae 2 1",
                                          "dynamicpart 2 inf",
                                          "dynamicdae 1e20",
                                          "step partitioned",
                                          "step 0",
                                          "run 0.1s",
                                          "run 1 2",
                                          "check",
                                          "check powerflow report extra",
                                          "contingency line file.csv -1 2",
                                          "contingency line file.csv 0 0",
                                          "contingency line file.csv 2x",
                                          "save state file extra",
                                          R"(get name "file"extra)"};
    for (const auto text : rejected) {
        SCOPED_TRACE(text);
        EXPECT_THROW(GridDynAction{std::string_view{text}}, griddyn::InvalidParameterValue);
    }
}

TEST(GridDynActionQueueTests, PreservesQuotedTextAndRejectsPartialNumbersInPlace)
{
    auto simulation = griddyn::readSimXMLFile(dynamicCase);
    ASSERT_NE(simulation, nullptr);
    auto* load = simulation->getBus(0)->getLoad();
    ASSERT_NE(load, nullptr);
    simulation->addActions(R"(SET description "A \"quote\"; # text";)"
                           "\nset bus1::load#0:p 25xyz; set bus1::load#0:p(MW) 25");

    EXPECT_THROW(simulation->run(), ActionError);
    EXPECT_EQ(simulation->getDescription(), R"(A "quote"; # text)");
    EXPECT_DOUBLE_EQ(load->get("p"), 1.5);
    EXPECT_THROW(simulation->execute("set bus1::load#0:p(MW 25"), ActionError);
    ASSERT_EQ(simulation->run(), FUNCTION_EXECUTION_SUCCESS);
    EXPECT_DOUBLE_EQ(load->get("p"), 0.25);
}

TEST(GridDynActionTests, SelectsConfiguredSolversAndEditsTheirSettings)
{
    auto simulation = griddyn::readSimXMLFile(dynamicCase);
    ASSERT_NE(simulation, nullptr);
    ASSERT_EQ(simulation->execute("set dynamicsolvermethod dae"), FUNCTION_EXECUTION_SUCCESS);
    const auto original = simulation->getSolverInterface("dae");
    ASSERT_NE(original, nullptr);
    const std::shared_ptr<griddyn::SolverInterface> alternate =
        griddyn::makeSolver("ida", "AlternateDae");
    ASSERT_NE(alternate, nullptr);
    auto mode = griddyn::cDaeSolverMode;
    mode.offsetIndex = kNullLocation;
    alternate->setSimulationData(mode);
    simulation->add(alternate);
    ASSERT_EQ(simulation->execute("setsolver DAE AlternateDae"), FUNCTION_EXECUTION_SUCCESS);
    ASSERT_EQ(simulation->getSolverInterface("dae"), alternate);
    ASSERT_EQ(simulation->execute("setsolver AlternateDae printlevel 2"),
              FUNCTION_EXECUTION_SUCCESS);
    EXPECT_DOUBLE_EQ(alternate->get("printlevel"), 2.0);
    ASSERT_EQ(simulation->execute("setsolver AlternateDae printlevel none"),
              FUNCTION_EXECUTION_SUCCESS);
    EXPECT_DOUBLE_EQ(alternate->get("printlevel"), 0.0);
    EXPECT_THROW(simulation->execute("setsolver DAEE AlternateDae"), ActionError);
    EXPECT_THROW(simulation->execute("setsolver powerflow AlternateDae"), ActionError);
    EXPECT_THROW(simulation->execute("setsolver AlternateDae nofield 5"), ActionError);
    EXPECT_THROW(simulation->execute("setsolver AlternateDae printlevel nonsense"), ActionError);

    const auto originalIndex = std::to_string(original->getSolverMode().offsetIndex);
    ASSERT_EQ(simulation->execute("setsolver dae " + originalIndex), FUNCTION_EXECUTION_SUCCESS);
    EXPECT_EQ(simulation->getSolverInterface("dae"), original);
    const auto selection =
        "setsolver dae " + std::to_string(alternate->getSolverMode().offsetIndex);
    ASSERT_EQ(simulation->execute(selection), FUNCTION_EXECUTION_SUCCESS);
    EXPECT_EQ(simulation->getSolverInterface("dae"), alternate);
    ASSERT_EQ(simulation->execute("dynamic dae 0.2"), FUNCTION_EXECUTION_SUCCESS);
    EXPECT_TRUE(alternate->isInitialized());
    EXPECT_EQ(simulation->getSimulationTime(), griddyn::CoreTime(0.2));
}

TEST(GridDynActionTests, InitializesByStateAndResetsModelValues)
{
    auto simulation = griddyn::readSimXMLFile(dynamicCase);
    ASSERT_NE(simulation, nullptr);
    using GridState = griddyn::GridDynSimulation::GridState;
    ASSERT_EQ(simulation->currentProcessState(), GridState::STARTUP);
    ASSERT_EQ(simulation->execute("initialize 0.05"), FUNCTION_EXECUTION_SUCCESS);
    EXPECT_EQ(simulation->currentProcessState(), GridState::INITIALIZED);
    EXPECT_EQ(simulation->getSimulationTime(), griddyn::CoreTime(0.05));
    EXPECT_THROW(simulation->execute("initialize 0.1"), ActionError);
    ASSERT_EQ(simulation->execute("powerflow"), FUNCTION_EXECUTION_SUCCESS);
    ASSERT_EQ(simulation->execute("set dynamicsolvermethod decoupled"), FUNCTION_EXECUTION_SUCCESS);
    EXPECT_THROW(simulation->execute("initialize"), ActionError);
    EXPECT_EQ(simulation->currentProcessState(), GridState::POWERFLOW_COMPLETE);
    ASSERT_EQ(simulation->execute("set dynamicsolvermethod dae"), FUNCTION_EXECUTION_SUCCESS);
    ASSERT_EQ(simulation->execute("initialize 0.1"), FUNCTION_EXECUTION_SUCCESS);
    EXPECT_EQ(simulation->currentProcessState(), GridState::DYNAMIC_INITIALIZED);
    EXPECT_EQ(simulation->getSimulationTime(), griddyn::CoreTime(0.1));
    ASSERT_EQ(simulation->execute("initialize"), FUNCTION_EXECUTION_SUCCESS);
    EXPECT_THROW(simulation->execute("initialize 0.15"), ActionError);

    ASSERT_EQ(simulation->execute("reset"), FUNCTION_EXECUTION_SUCCESS);
    EXPECT_EQ(simulation->currentProcessState(), GridState::DYNAMIC_INITIALIZED);
    EXPECT_EQ(simulation->getSimulationTime(), griddyn::CoreTime(0.1));

    auto staticSimulation = griddyn::readSimXMLFile(staticCase);
    ASSERT_NE(staticSimulation, nullptr);
    auto* demand = staticSimulation->getBus(1);
    ASSERT_NE(demand, nullptr);
    struct ResetCase {
        int mLevel;
        double mVoltage;
        double mAngle;
    };
    constexpr ResetCase levels[]{
        {.mLevel = 0, .mVoltage = 0.8, .mAngle = 0.15},
        {.mLevel = 1, .mVoltage = 1.0, .mAngle = 0.15},
        {.mLevel = 2, .mVoltage = 0.8, .mAngle = 0.0},
        {.mLevel = 3, .mVoltage = 1.0, .mAngle = 0.0},
        {.mLevel = 4, .mVoltage = 1.0, .mAngle = 0.0},
    };
    for (const auto& level : levels) {
        SCOPED_TRACE(level.mLevel);
        demand->setVoltageAngle(0.8, 0.15);
        ASSERT_EQ(staticSimulation->execute("reset " + std::to_string(level.mLevel)),
                  FUNCTION_EXECUTION_SUCCESS);
        EXPECT_DOUBLE_EQ(demand->getVoltage(), level.mVoltage);
        EXPECT_DOUBLE_EQ(demand->getAngle(), level.mAngle);
    }
}

TEST(GridDynActionQueueTests, RelativeStepsStopAtEventsAndResumeAtTheOriginalBound)
{
    auto simulation = griddyn::readSimXMLFile(dynamicCase);
    ASSERT_NE(simulation, nullptr);
    ASSERT_EQ(simulation->execute("dynamic dae 0.2"), FUNCTION_EXECUTION_SUCCESS);
    EXPECT_THROW(simulation->execute("run 0.19"), ActionError);
    ASSERT_EQ(simulation->execute("set timestep 0.1"), FUNCTION_EXECUTION_SUCCESS);
    ASSERT_EQ(simulation->execute("step DAE"), FUNCTION_EXECUTION_SUCCESS);
    EXPECT_EQ(simulation->getSimulationTime(), griddyn::CoreTime(0.3));
    ASSERT_EQ(simulation->execute("add event @0.35|bus1::load3:p=1.2"), FUNCTION_EXECUTION_SUCCESS);
    ASSERT_EQ(simulation->execute("step 0.1"), FUNCTION_EXECUTION_SUCCESS);
    EXPECT_EQ(simulation->getSimulationTime(), griddyn::CoreTime(0.35));
    EXPECT_DOUBLE_EQ(simulation->getBus(0)->getLoad()->get("p"), 1.2);

    simulation->addActions("step 0.1; set name completed");
    ASSERT_EQ(simulation->run(0.4), FUNCTION_EXECUTION_SUCCESS);
    EXPECT_EQ(simulation->getSimulationTime(), griddyn::CoreTime(0.4));
    EXPECT_NE(simulation->getName(), "completed");
    ASSERT_EQ(simulation->run(), FUNCTION_EXECUTION_SUCCESS);
    EXPECT_EQ(simulation->getSimulationTime(), griddyn::CoreTime(0.45));
    EXPECT_EQ(simulation->getName(), "completed");

    simulation->addActions("add event @0.5|bus1::load3:p=1.3; step 0.1; set name at_bound");
    ASSERT_EQ(simulation->run(0.5), FUNCTION_EXECUTION_SUCCESS);
    EXPECT_EQ(simulation->getSimulationTime(), griddyn::CoreTime(0.5));
    EXPECT_EQ(simulation->getName(), "at_bound");
}

TEST(GridDynActionTests, StaticRunAdvancesWithEventsAndExplicitDynamicsRequiresStates)
{
    auto simulation = griddyn::readSimXMLFile(staticCase);
    ASSERT_NE(simulation, nullptr);
    ASSERT_EQ(simulation->execute("add event @0.15|demand::demand_load:p=0.3"),
              FUNCTION_EXECUTION_SUCCESS);
    ASSERT_EQ(simulation->execute("run 0.2"), FUNCTION_EXECUTION_SUCCESS);
    EXPECT_EQ(simulation->getSimulationTime(), griddyn::CoreTime(0.2));
    EXPECT_DOUBLE_EQ(simulation->getBus(1)->getLoad()->get("p"), 0.3);
    EXPECT_THROW(simulation->execute("dynamic dae 0.3"), ActionError);
    EXPECT_THROW(simulation->execute("step 0.1"), ActionError);
    EXPECT_EQ(simulation->getSimulationTime(), griddyn::CoreTime(0.2));
}

TEST(GridDynActionTests, BulkSetTraversesNestedAreasAndValidatesMatches)
{
    class RestrictedLoad final: public griddyn::GridLoad {
      public:
        using GridLoad::set;

        void set(std::string_view field, double value, units::unit unit) override
        {
            if (field == "p") {
                if (value < 0.0) {
                    throw griddyn::InvalidParameterValue(field);
                }
                throw griddyn::UnrecognizedParameter(field);
            }
            GridLoad::set(field, value, unit);
        }
    };

    griddyn::GridDynSimulation simulation;
    auto* area = new griddyn::GridArea("nested");
    simulation.add(area);
    auto* nestedBus = new griddyn::AcBus("nestedBus");
    area->add(nestedBus);
    nestedBus->add(new griddyn::GridLoad("demand"));
    auto* rootBus = new griddyn::AcBus("rootBus");
    simulation.add(rootBus);
    rootBus->add(new griddyn::GridLoad("demand"));
    nestedBus->add(new RestrictedLoad());
    auto* link = new griddyn::AcLine("nestedLink");
    area->add(link);
    auto* relay = new griddyn::Relay("nestedRelay");
    area->add(relay);

    ASSERT_EQ(simulation.execute("setall BUS voltage 1.02"), FUNCTION_EXECUTION_SUCCESS);
    ASSERT_EQ(simulation.execute("setall LOAD p(MW) 25"), FUNCTION_EXECUTION_SUCCESS);
    ASSERT_EQ(simulation.execute("setall LINK r 0.04"), FUNCTION_EXECUTION_SUCCESS);
    ASSERT_EQ(simulation.execute("setall RELAY enabled 0"), FUNCTION_EXECUTION_SUCCESS);
    EXPECT_DOUBLE_EQ(link->get("r"), 0.04);
    EXPECT_FALSE(relay->isEnabled());
    EXPECT_DOUBLE_EQ(rootBus->getLoad()->get("p", units::MW), 25.0);
    EXPECT_DOUBLE_EQ(nestedBus->getLoad()->get("p", units::MW), 25.0);
    EXPECT_DOUBLE_EQ(rootBus->getVoltage(), 1.02);
    EXPECT_DOUBLE_EQ(nestedBus->getVoltage(), 1.02);
    EXPECT_THROW(simulation.execute("setall load nonexistent 1"), ActionError);
    EXPECT_THROW(simulation.execute("setall generator p 1"), ActionError);
    EXPECT_THROW(simulation.execute("setall invalid p 1"), ActionError);
    EXPECT_DOUBLE_EQ(nestedBus->getLoad()->get("p", units::MW), 25.0);
    EXPECT_THROW(simulation.execute("setall load p -0.15"), ActionError);
    EXPECT_DOUBLE_EQ(rootBus->getLoad()->get("p"), -0.15);
    EXPECT_DOUBLE_EQ(nestedBus->getLoad()->get("p"), -0.15);
}

TEST(GridDynActionQueueTests, CheckReportsViolationsAndAssertionsStopTheQueue)
{
    auto simulation = griddyn::readSimXMLFile(staticCase);
    ASSERT_NE(simulation, nullptr);
    const auto report = std::filesystem::temp_directory_path() / "gdc13 powerflow report.txt";
    std::filesystem::remove(report);
    const auto destination = " \"" + report.string() + '"';
    ASSERT_EQ(simulation->execute("powerflow"), FUNCTION_EXECUTION_SUCCESS);
    ASSERT_EQ(simulation->execute("check powerflow --assert" + destination),
              FUNCTION_EXECUTION_SUCCESS);
    ASSERT_EQ(simulation->execute("set demand:vmin 1.05"), FUNCTION_EXECUTION_SUCCESS);
    ASSERT_EQ(simulation->execute("check POWERFLOW" + destination), FUNCTION_EXECUTION_SUCCESS);
    EXPECT_THROW(simulation->execute("check unknown"), ActionError);

    simulation->addActions("check powerflow --assert; set name continued");
    EXPECT_THROW(simulation->run(), ActionError);
    EXPECT_NE(simulation->getName(), "continued");
    ASSERT_EQ(simulation->run(), FUNCTION_EXECUTION_SUCCESS);
    EXPECT_EQ(simulation->getName(), "continued");

    std::ifstream contents(report);
    std::string expected;
    ASSERT_TRUE(std::getline(contents, expected));
    EXPECT_TRUE(expected.starts_with("Violations for continuation_two_bus at "));
    ASSERT_TRUE(std::getline(contents, expected));
    EXPECT_TRUE(expected.starts_with("Violations for continuation_two_bus at "));
    ASSERT_TRUE(std::getline(contents, expected));
    EXPECT_TRUE(expected.starts_with("demand["));
    contents.close();
    std::filesystem::remove(report);
}

TEST(GridDynActionTests, DistinguishesResultFilesFromUnsupportedSaveFormats)
{
    auto simulation = griddyn::readSimXMLFile(staticCase);
    ASSERT_NE(simulation, nullptr);
    ASSERT_EQ(simulation->execute("powerflow"), FUNCTION_EXECUTION_SUCCESS);
    const auto result = std::filesystem::temp_directory_path() / "gdc11 results.CSV";
    std::filesystem::remove(result);
    const auto destination = '"' + result.string() + '"';
    ASSERT_EQ(simulation->execute("SAVE POWERFLOW " + destination), FUNCTION_EXECUTION_SUCCESS);

    std::ifstream contents(result);
    std::string line;
    ASSERT_TRUE(std::getline(contents, line));
    EXPECT_TRUE(line.starts_with("basepower=100"));
    ASSERT_TRUE(std::getline(contents, line));
    EXPECT_TRUE(line.contains("voltage(pu)"));
    ASSERT_TRUE(std::getline(contents, line));
    EXPECT_TRUE(line.contains("source"));
    contents.close();
    std::filesystem::remove(result);
    simulation->set("powerflowfile", result.string());
    ASSERT_EQ(simulation->execute("save powerflow"), FUNCTION_EXECUTION_SUCCESS);
    EXPECT_TRUE(std::filesystem::exists(result));
    std::filesystem::remove(result);

    const auto unsupported = std::filesystem::temp_directory_path() / "gdc11 result.raw";
    EXPECT_THROW(simulation->execute("save powerflow \"" + unsupported.string() + '"'),
                 ActionError);
    EXPECT_FALSE(std::filesystem::exists(unsupported));
    EXPECT_THROW(simulation->execute("save recorder ignored.csv"), ActionError);
    EXPECT_THROW(simulation->execute("save jacstate unused.h5"), ActionError);
    EXPECT_THROW(simulation->execute("save voltage unused.csv"), ActionError);
    EXPECT_THROW(simulation->execute("save powerflow \"no-such-gdc11-dir/results.csv\""),
                 ActionError);
    EXPECT_THROW(simulation->execute(GridDynAction(Action::INVALID)), ActionError);
}

TEST(GridDynActionTests, DirectActionsConfigureSolveInspectAndSave)
{
    auto simulation = griddyn::readSimXMLFile(staticCase);
    ASSERT_NE(simulation, nullptr);
    const auto directory = std::filesystem::temp_directory_path();
    const auto readingsPath = directory / "gdc18 direct readings.txt";
    const auto resultsPath = directory / "gdc18 direct results.csv";
    std::filesystem::remove(readingsPath);
    std::filesystem::remove(resultsPath);
    const auto readings = '"' + readingsPath.generic_string() + '"';
    ASSERT_EQ(simulation->execute("set demand::demand_load:p(MW) 30"), FUNCTION_EXECUTION_SUCCESS);
    ASSERT_EQ(simulation->execute("powerflow"), FUNCTION_EXECUTION_SUCCESS);
    ASSERT_EQ(simulation->execute("iterate 0.05 0.2"), FUNCTION_EXECUTION_SUCCESS);
    ASSERT_EQ(simulation->execute("get time(ms) " + readings), FUNCTION_EXECUTION_SUCCESS);
    ASSERT_EQ(simulation->execute("get demand::demand_load:p(MW) " + readings),
              FUNCTION_EXECUTION_SUCCESS);
    ASSERT_EQ(simulation->execute("get demand:voltage " + readings), FUNCTION_EXECUTION_SUCCESS);
    ASSERT_EQ(simulation->execute("save powerflow \"" + resultsPath.generic_string() + '"'),
              FUNCTION_EXECUTION_SUCCESS);

    const auto expectedVoltage = std::sqrt((1.0 + std::sqrt(0.91)) / 2.0);
    EXPECT_EQ(simulation->getSimulationTime(), griddyn::CoreTime(0.2));
    EXPECT_NEAR(simulation->getBus(1)->getVoltage(), expectedVoltage, 1e-6);
    std::ifstream values(readingsPath);
    double timeMs = 0.0;
    double loadMw = 0.0;
    double voltage = 0.0;
    ASSERT_TRUE(values >> timeMs >> loadMw >> voltage);
    EXPECT_NEAR(timeMs, 200.0, 1e-5);
    EXPECT_DOUBLE_EQ(loadMw, 30.0);
    EXPECT_NEAR(voltage, expectedVoltage, 1e-6);
    values.close();
    std::filesystem::remove(readingsPath);

    std::ifstream results(resultsPath);
    std::string line;
    ASSERT_TRUE(std::getline(results, line));
    EXPECT_TRUE(line.starts_with("basepower=100"));
    ASSERT_TRUE(std::getline(results, line));
    EXPECT_TRUE(line.contains("Pload(MW)"));
    ASSERT_TRUE(std::getline(results, line));
    ASSERT_TRUE(std::getline(results, line));
    EXPECT_TRUE(line.contains(R"("demand")"));
    EXPECT_TRUE(line.contains(", 30.00000,"));
    results.close();
    std::filesystem::remove(resultsPath);
}
