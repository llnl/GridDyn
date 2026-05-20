# ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
#
# Copyright (c) 2014-2026, Lawrence Livermore National Security
# See the top-level NOTICE for additional details. All rights reserved.
#
# SPDX-License-Identifier: BSD-3-Clause
#
# ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

set(genmodel_headers
    GenModel.h
    genmodels/GenModelClassical.h
    genmodels/GenModel3.h
    genmodels/GenModel4.h
    genmodels/GenModel5.h
    genmodels/GenModel5type2.h
    genmodels/GenModel5type3.h
    genmodels/GenModel6.h
    genmodels/GenModel6type2.h
    genmodels/GenModelGENROU.h
    genmodels/GenModel8.h
    genmodels/GenModelInverter.h
    genmodels/otherGenModels.h
)

set(genmodel_sources
    ${genmodel_headers}
    genmodels/GenModel.cpp
    genmodels/GenModelClassical.cpp
    genmodels/GenModel3.cpp
    genmodels/GenModel4.cpp
    genmodels/GenModel5.cpp
    genmodels/GenModel5type2.cpp
    genmodels/GenModel5type3.cpp
    genmodels/GenModel6.cpp
    genmodels/GenModel6type2.cpp
    genmodels/GenModelGENROU.cpp
    genmodels/GenModel8.cpp
    genmodels/GenModelInverter.cpp
)

set(generator_headers Generator.h generators/VariableGenerator.h generators/IsocController.h
                      generators/DynamicGenerator.h
)

set(generator_sources
    ${generator_headers} generators/Generator.cpp generators/VariableGenerator.cpp
    generators/IsocController.cpp generators/DynamicGenerator.cpp
)

set(gridSource_headers
    Source.h
    sources/sourceTypes.h
    sources/RampSource.h
    sources/FileSource.h
    sources/RandomSource.h
    sources/PulseSource.h
    sources/SineSource.h
    sources/FunctionSource.h
    sources/CommSource.h
    sources/BlockSource.h
    sources/GrabberSource.h
)

set(gridSource_sources
    ${gridSource_headers}
    sources/Source.cpp
    sources/RampSource.cpp
    sources/FileSource.cpp
    sources/RandomSource.cpp
    sources/PulseSource.cpp
    sources/SineSource.cpp
    sources/FunctionSource.cpp
    sources/CommSource.cpp
    sources/BlockSource.cpp
    sources/GrabberSource.cpp
)

set(exciter_headers
    Exciter.h
    Stabilizer.h
    exciters/ExciterDC1A.h
    exciters/ExciterDC2A.h
    exciters/ExciterIEEEtype1.h
    exciters/ExciterIEEEtype2.h
    exciters/ExciterSEXS.h
)

set(exciter_sources
    ${exciter_headers}
    exciters/Exciter.cpp
    exciters/ExciterDC1A.cpp
    exciters/ExciterDC2A.cpp
    exciters/ExciterIEEEtype1.cpp
    exciters/ExciterIEEEtype2.cpp
    exciters/ExciterSEXS.cpp
    stabilizers/Stabilizer.cpp
)

set(gov_headers
    Governor.h
    governors/GovernorTypes.h
    governors/GovernorHydro.h
    governors/GovernorIeeeSimple.h
    governors/GovernorReheat.h
    governors/GovernorSteamNR.h
    governors/GovernorSteamTCSR.h
    governors/GovernorTgov1.h
)

set(gov_sources
    ${gov_headers}
    governors/Governor.cpp
    governors/GovernorHydro.cpp
    governors/GovernorIeeeSimple.cpp
    governors/GovernorReheat.cpp
    governors/GovernorSteamNR.cpp
    governors/GovernorSteamTCSR.cpp
    governors/GovernorTgov1.cpp
)

set(block_headers
    Block.h
    blocks/IntegralBlock.h
    blocks/DeadbandBlock.h
    blocks/DelayBlock.h
    blocks/NullBlock.h
    blocks/PidBlock.h
    blocks/ControlBlock.h
    blocks/DerivativeBlock.h
    blocks/FunctionBlock.h
    blocks/LutBlock.h
    blocks/TransferFunctionBlock.h
    blocks/BlockSequence.h
    blocks/FilteredDerivativeBlock.h
    blocks/ValueLimiter.h
    blocks/RampLimiter.h
    blocks/blockLibrary.h
)

set(block_sources
    ${block_headers}
    blocks/Block.cpp
    blocks/IntegralBlock.cpp
    blocks/DeadbandBlock.cpp
    blocks/DelayBlock.cpp
    blocks/NullBlock.cpp
    blocks/PidBlock.cpp
    blocks/ControlBlock.cpp
    blocks/DerivativeBlock.cpp
    blocks/FunctionBlock.cpp
    blocks/LutBlock.cpp
    blocks/TransferFunctionBlock.cpp
    blocks/FilteredDerivativeBlock.cpp
    blocks/BlockSequence.cpp
    blocks/ValueLimiter.cpp
    blocks/RampLimiter.cpp
)

set(submodel_sources ${genmodel_sources} ${exciter_sources} ${gov_sources} ${gridSource_sources}
                     ${block_sources}
)

set(load_headers
    Load.h
    loads/RampLoad.h
    loads/ExponentialLoad.h
    loads/SourceLoad.h
    loads/FileLoad.h
    loads/FDepLoad.h
    loads/MotorLoad.h
    loads/MotorLoad3.h
    loads/MotorLoad5.h
    loads/GridLabDLoad.h
    loads/Svd.h
    loads/AggregateLoad.h
    loads/ZipLoad.h
    loads/FrequencySensitiveLoad.h
    loads/ThreePhaseLoad.h
)

set(load_sources
    ${load_headers}
    loads/Load.cpp
    loads/RampLoad.cpp
    loads/FileLoad.cpp
    loads/MotorLoad.cpp
    loads/MotorLoad3.cpp
    loads/MotorLoad5.cpp
    loads/ApproximatingLoad.cpp
    loads/ExponentialLoad.cpp
    loads/FDepLoad.cpp
    loads/AggregateLoad.cpp
    loads/SourceLoad.cpp
    loads/ZipLoad.cpp
    loads/Svd.cpp
    loads/ThreePhaseLoad.cpp
    loads/FrequencySensitiveLoad.cpp
    loads/GridLabDLoad.cpp
)

set(link_headers
    links/DcLink.h
    links/AcdcConverter.h
    links/AdjustableTransformer.h
    Link.h
    links/Subsystem.h
    links/Hvdc.h
    links/ZBreaker.h
    links/LongLine.h
    links/AcLine.h
    links/ThreeWindingTransformer.h
)

set(link_sources
    ${link_headers}
    links/Link.cpp
    links/AdjustableTransformer.cpp
    links/DcLink.cpp
    links/AcdcConverter.cpp
    links/Subsystem.cpp
    links/Hvdc.cpp
    links/ZBreaker.cpp
    links/LongLine.cpp
    links/AcLine.cpp
    links/ThreeWindingTransformer.cpp
)

set(simulation_headers
    simulation/Contingency.h
    simulation/Continuation.h
    GridDynSimulation.h
    simulation/GridSimulation.h
    simulation/Diagnostics.h
    simulation/PowerFlowErrorRecovery.h
    simulation/DynamicInitialConditionRecovery.h
    simulation/FaultResetRecovery.h
    simulation/GridDynActions.h
    simulation/GridDynSimulationFileOps.h
)

set(simulation_sources
    ${simulation_headers}
    simulation/gridDynContinuation.cpp
    simulation/GridDynSimulation.cpp
    simulation/Contingency.cpp
    simulation/gridDynPowerFlow.cpp
    simulation/gridDynDynamic.cpp
    simulation/GridSimulation.cpp
    simulation/GridDynActions.cpp
    simulation/GridDynSimulationFileOps.cpp
    simulation/Diagnostics.cpp
    simulation/PowerFlowErrorRecovery.cpp
    simulation/DynamicInitialConditionRecovery.cpp
    simulation/FaultResetRecovery.cpp
    simulation/gridDynContingency.cpp
)

set(solver_headers
    solvers/SolverInterface.h
    solvers/SundialsInterface.h
    solvers/IdaInterface.h
    solvers/KinsolInterface.h
    solvers/BasicSolver.h
    solvers/SundialsMatrixData.h
    solvers/BasicOdeSolver.h
    solvers/SolverMode.hpp
)

set(solver_sources
    ${solver_headers}
    solvers/IdaInterface.cpp
    solvers/KinsolInterface.cpp
    solvers/SolverInterface.cpp
    solvers/BasicSolver.cpp
    solvers/SundialsMatrixData.cpp
    solvers/sundialsMatrixDataDense.cpp
    solvers/sundialsMatrixDataSparseRow.cpp
    solvers/sundialsMatrixDataSparseColumn.cpp
    solvers/SundialsInterface.cpp
    solvers/BasicOdeSolver.cpp
)

if(GRIDDYN_ENABLE_CVODE)
    list(APPEND solver_sources solvers/CvodeInterface.cpp solvers/CvodeInterface.h)

    list(APPEND solver_headers solvers/CvodeInterface.h)

endif(GRIDDYN_ENABLE_CVODE)

if(GRIDDYN_ENABLE_ARKODE)
    list(APPEND solver_sources solvers/ArkodeInterface.cpp solvers/ArkodeInterface.h)

    list(APPEND solver_headers solvers/ArkodeInterface.h)
endif(GRIDDYN_ENABLE_ARKODE)

set(core_headers
    GridComponent.h
    GridPrimary.h
    GridSecondary.h
    GridSubModel.h
    GridComponentHelperClasses.h
    NumericEstimationFunctions.h
    gridDynDefinitions.hpp
    OffsetTable.h
    gridDynVersion.hpp
)

set(core_sources
    ${core_headers}
    GridComponent.cpp
    GridPrimary.cpp
    GridSecondary.cpp
    GridSubModel.cpp
    GridComponentHelperClasses.cpp
    NumericEstimationFunctions.cpp
    OffsetTable.cpp
)

set(primary_headers
    GridBus.h
    GridArea.h
    primary/DcBus.h
    allGridDynObjects.h
    primary/BusControls.h
    primary/ListMaintainer.h
    primary/InfiniteBus.h
    primary/AcBus.h
    primary/DcBusControls.h
)

set(primary_sources
    ${primary_headers}
    primary/GridBus.cpp
    primary/GridArea.cpp
    primary/DcBus.cpp
    primary/InfiniteBus.cpp
    primary/BusControls.cpp
    primary/DcBusControls.cpp
    primary/AcBus.cpp
    primary/ListMaintainer.cpp
)

set(event_headers
    events/Event.h
    events/EventAdapters.h
    events/EventQueue.h
    events/EventInterface.hpp
    events/ReversibleEvent.h
    events/ParameterOperator.h
    events/Player.h
    events/InterpolatingPlayer.h
    events/CompoundEvent.h
    events/CompoundEventPlayer.h
)

set(event_sources
    ${event_headers}
    events/Event.cpp
    events/EventAdapters.cpp
    events/EventQueue.cpp
    events/Player.cpp
    events/InterpolatingPlayer.cpp
    events/CompoundEvent.cpp
    events/CompoundEventPlayer.cpp
    events/ReversibleEvent.cpp
    events/ParameterOperator.cpp
)
set(measurement_headers
    measurement/GridGrabbers.h
    measurement/ObjectGrabbers.h
    measurement/Collector.h
    measurement/Recorder.h
    measurement/StateGrabber.h
    measurement/Condition.h
    measurement/GrabberSet.h
    measurement/GrabberInterpreter.hpp
)

set(measurement_sources
    ${measurement_headers}
    measurement/Recorder.cpp
    measurement/GridGrabbers.cpp
    measurement/GrabberInterpreter.cpp
    measurement/stateGrabbers.cpp
    measurement/Condition.cpp
    measurement/ObjectGrabbers.cpp
    measurement/compoundCondition.cpp
    measurement/Collector.cpp
    measurement/GrabberSet.cpp
)

set(comm_headers
    comms/CommMessage.h
    comms/Communicator.h
    comms/CommunicationsCore.h
    comms/SchedulerMessage.h
    comms/ControlMessage.h
    comms/CommManager.h
)

set(comm_sources
    ${comm_headers}
    comms/CommMessage.cpp
    comms/Communicator.cpp
    comms/CommunicationsCore.cpp
    comms/SchedulerMessage.cpp
    comms/ControlMessage.cpp
    comms/CommManager.cpp
)

set(relay_headers
    Relay.h
    relays/ZonalRelay.h
    relays/LoadRelay.h
    relays/BusRelay.h
    relays/Fuse.h
    relays/Breaker.h
    relays/Sensor.h
    relays/DifferentialRelay.h
    relays/ControlRelay.h
    relays/Pmu.h
)

set(relay_sources
    ${relay_headers}
    relays/Relay.cpp
    relays/ZonalRelay.cpp
    relays/LoadRelay.cpp
    relays/BusRelay.cpp
    relays/Fuse.cpp
    relays/Breaker.cpp
    relays/Sensor.cpp
    relays/DifferentialRelay.cpp
    relays/ControlRelay.cpp
    relays/Pmu.cpp
)

set(controller_headers
    controllers/SchedulerInfo.h
    controllers/Scheduler.h
    controllers/ReserveDispatcher.h
    controllers/AGControl.h
    controllers/ControlSystem.h
    controllers/Dispatcher.h
)

set(controller_sources
    ${controller_headers}
    controllers/Scheduler.cpp
    controllers/schedulerRamp.cpp
    controllers/schedulerReg.cpp
    controllers/ReserveDispatcher.cpp
    controllers/ControlSystem.cpp
    controllers/AGControl.cpp
    controllers/Dispatcher.cpp
)

set(sublibrary_files
    ${event_sources}
    ${measurement_sources}
    ${griddynIO_sources}
    ${gridSource_sources}
    ${controller_sources}
    ${comm_sources}
    ${solver_sources}
)

set(model_files ${relay_sources} ${submodel_sources} ${load_sources} ${link_sources}
                ${generator_sources}
)

set(header_files
    ${exciter_headers}
    ${gov_headers}
    ${block_headers}
    ${gridSource_headers}
    ${core_headers}
    ${primary_headers}
    ${link_headers}
    ${solver_headers}
    ${controller_headers}
    ${comm_headers}
    ${event_headers}
    ${measurement_headers}
    ${gridDynIO_headers}
    ${simulation_headers}
    ${submodel_headers}
    ${load_headers}
    ${relay_headers}
    ${generator_headers}
)

add_library(
    griddyn STATIC ${simulation_sources} ${model_files} ${core_sources} ${primary_sources}
                   ${sublibrary_files}
)

target_include_directories(
    griddyn
    PUBLIC $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}>
           # TODO is this right?
           $<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}>
)

target_include_directories(
    griddyn
    # TODO: revisit whether this broad ThirdParty include path is still needed.
    SYSTEM PRIVATE $<BUILD_INTERFACE:${PROJECT_SOURCE_DIR}/ThirdParty>
)

target_link_libraries(griddyn PUBLIC coreObjects)

target_link_libraries(griddyn PUBLIC SUNDIALS::SUNDIALS)
target_link_libraries(griddyn PUBLIC coupling_static_lib)
target_link_libraries(griddyn PUBLIC pugixml::pugixml)
if(GRIDDYN_ENABLE_KLU)
    if(KLU_INCLUDE_DIR)
        target_include_directories(griddyn PRIVATE ${KLU_INCLUDE_DIR})
    endif()

    if(GRIDDYN_SUITESPARSE_LOCAL_BUILD)
        target_link_libraries(griddyn PUBLIC klu)
    else()
        target_link_libraries(griddyn PUBLIC Suitesparse::klu)
    endif()

endif(GRIDDYN_ENABLE_KLU)

source_group("loads" FILES ${load_sources})
source_group("exciters" FILES ${exciter_sources})
source_group("governors" FILES ${gov_sources})
source_group("genmodels" FILES ${genmodel_sources})
source_group("links" FILES ${link_sources})
source_group("events" FILES ${event_sources})
source_group("measurement" FILES ${measurement_sources})
source_group("generators" FILES ${generator_sources})
source_group("sources" FILES ${gridSource_sources})
source_group("simulation" FILES ${simulation_sources})
source_group("core" FILES ${core_sources})
source_group("primary" FILES ${primary_sources})
source_group("relays" FILES ${relay_sources})
source_group("comms" FILES ${comm_sources})
source_group("controllers" FILES ${controller_sources})
source_group("blocks" FILES ${block_sources})
source_group("solvers" FILES ${solver_sources})

# create a griddyn-config.h file for program options
configure_file(
    ${PROJECT_SOURCE_DIR}/config/griddyn-config.h.in ${CONFIGURE_TARGET_LOCATION}/griddyn-config.h
)
install(FILES ${CONFIGURE_TARGET_LOCATION}/griddyn-config.h
        DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}/griddyn COMPONENT headers
)
