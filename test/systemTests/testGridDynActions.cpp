/*
 * Copyright (c) 2014-2026, Lawrence Livermore National Security
 * See the top-level NOTICE for additional details. All rights reserved.
 * SPDX-License-Identifier: BSD-3-Clause
 */

#include "../gtestHelper.h"
#include "core/CoreExceptions.h"
#include "griddyn/GridBus.h"
#include "griddyn/Load.h"
#include "griddyn/simulation/GridDynActions.h"
#include "griddyn/solvers/SolverInterface.h"
#include <gtest/gtest.h>
#include <string>
#include <string_view>

namespace {
using griddyn::ActionError;
using griddyn::GridDynAction;
using Action = GridDynAction::GdAction;

constexpr char dynamicCase[] = GRIDDYN_TEST_DIRECTORY "/dyn_tests1/test_dynSimple1.xml";
constexpr char staticCase[] = GRIDDYN_TEST_DIRECTORY "/pFlow_tests/continuation_two_bus.xml";
}  // namespace

TEST(GridDynActionTests, ParsesDynamicModeAndOptionalTimes)
{
    struct TestCase {
        std::string_view text;
        Action mode;
        double stop;
        double step;
    };
    constexpr double omitted = griddyn::kNullVal;
    constexpr TestCase cases[]{
        {.text = "dynamic", .mode = Action::DYNAMIC_DEFAULT, .stop = omitted, .step = omitted},
        {.text = "dynamic 0.1", .mode = Action::DYNAMIC_DEFAULT, .stop = 0.1, .step = omitted},
        {.text = "dynamic 0.1 0.02 # times in seconds",
         .mode = Action::DYNAMIC_DEFAULT,
         .stop = 0.1,
         .step = 0.02},
        {.text = "dynamic dae", .mode = Action::DYNAMIC_DAE, .stop = omitted, .step = omitted},
        {.text = "dynamic DAE 0.1", .mode = Action::DYNAMIC_DAE, .stop = 0.1, .step = omitted},
        {.text = "dynamic part", .mode = Action::DYNAMIC_PART, .stop = omitted, .step = omitted},
        {.text = "dynamic partitioned 0.1 0.02",
         .mode = Action::DYNAMIC_PART,
         .stop = 0.1,
         .step = 0.02},
        {.text = "dynamic decoupled 0.1",
         .mode = Action::DYNAMIC_DECOUPLED,
         .stop = 0.1,
         .step = omitted},
        {.text = "dynamicdae 0.1", .mode = Action::DYNAMIC_DAE, .stop = 0.1, .step = omitted},
        {.text = "dynamicpart 0.1 0.02", .mode = Action::DYNAMIC_PART, .stop = 0.1, .step = 0.02},
        {.text = "dynamicdecoupled 0.1 0.02",
         .mode = Action::DYNAMIC_DECOUPLED,
         .stop = 0.1,
         .step = 0.02},
    };
    for (const auto& test : cases) {
        SCOPED_TRACE(test.text);
        const GridDynAction action(test.text);
        EXPECT_EQ(action.command, test.mode);
        EXPECT_DOUBLE_EQ(action.val_double, test.stop);
        EXPECT_DOUBLE_EQ(action.val_double2, test.step);
    }
}

TEST(GridDynActionTests, RejectsIncompleteOrUnrecognizedOperands)
{
    constexpr std::string_view cases[]{"dynamic unknown",
                                       "dynamic unknown 0.1",
                                       "dynamic 0.1s",
                                       "contingency",
                                       "contingency simplified",
                                       "contingency simplified # line selected.csv 0 1"};
    GridDynAction action;
    for (const auto text : cases) {
        SCOPED_TRACE(text);
        EXPECT_THROW(action.process(text), griddyn::InvalidParameterValue);
    }
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
    EXPECT_EQ(action.val_int1, 0);
    EXPECT_EQ(action.val_int2, 0);
}

TEST(GridDynActionTests, QueuedDynamicUsesSolverConfiguredAtExecution)
{
    struct TestCase {
        std::string_view method;
        std::string_view solver;
    };
    constexpr TestCase cases[]{{.method = "dae", .solver = "dae"},
                               {.method = "partitioned", .solver = "differential"}};
    for (const auto& test : cases) {
        SCOPED_TRACE(test.method);
        auto simulation = griddyn::readSimXMLFile(dynamicCase);
        ASSERT_NE(simulation, nullptr);
        simulation->add("dynamic 0.1");
        simulation->set("dynamicsolvermethod", test.method);

        ASSERT_EQ(simulation->run(), FUNCTION_EXECUTION_SUCCESS);
        EXPECT_NEAR(static_cast<double>(simulation->getSimulationTime()), 0.1, 1e-8);
        const auto solver = simulation->getSolverInterface(test.solver);
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
        FAIL() << "expected an execution error";
    }
    catch (const ActionError& error) {
        EXPECT_EQ(error.kind(), ActionError::Kind::EXECUTION);
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
        std::string_view command;
        ActionError::Kind kind;
    };
    constexpr TestCase cases[]{
        {.command = "contingency", .kind = ActionError::Kind::SYNTAX},
        {.command = "set missing:voltage 1.02", .kind = ActionError::Kind::TARGET},
        {.command = "set @missing:voltage 1.02", .kind = ActionError::Kind::TARGET},
        {.command = "set :voltage 1.02", .kind = ActionError::Kind::TARGET},
        {.command = "setsolver dae ida", .kind = ActionError::Kind::UNSUPPORTED},
        {.command = "print voltage destination", .kind = ActionError::Kind::UNSUPPORTED},
        {.command = "continuation", .kind = ActionError::Kind::UNSUPPORTED},
    };
    for (const auto& test : cases) {
        SCOPED_TRACE(test.command);
        try {
            simulation.execute(test.command);
            FAIL() << "expected a command error";
        }
        catch (const ActionError& error) {
            EXPECT_EQ(error.kind(), test.kind);
            EXPECT_NE(std::string_view(error.what()).find(test.command), std::string_view::npos);
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

TEST(GridDynActionTests, RejectsMissingSolversAndUnsupportedTextSelection)
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
            FAIL() << "expected an unsupported solver selection";
        }
        catch (const ActionError& error) {
            EXPECT_EQ(error.kind(), ActionError::Kind::UNSUPPORTED);
            EXPECT_NE(std::string_view(error.what()).find(command), std::string_view::npos);
        }
    }
    EXPECT_EQ(simulation.getSolverInterface("missing"), nullptr);
    EXPECT_EQ(simulation.getSolverInterface(griddyn::cDaeSolverMode.offsetIndex), nullptr);
    EXPECT_DOUBLE_EQ(powerflow->get("printlevel"), printLevel);

    simulation.solverSet("powerflow", "printlevel", "none");
    EXPECT_DOUBLE_EQ(powerflow->get("printlevel"), 0.0);
}
