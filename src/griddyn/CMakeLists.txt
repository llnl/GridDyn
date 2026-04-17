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

set(generator_headers Generator.h generators/variableGenerator.h generators/isocController.h
                      generators/DynamicGenerator.h
)

set(generator_sources
    ${generator_headers} generators/Generator.cpp generators/variableGenerator.cpp
    generators/isocController.cpp generators/DynamicGenerator.cpp
)

set(gridSource_headers
    Source.h
    sources/sourceTypes.h
    sources/rampSource.h
    sources/fileSource.h
    sources/randomSource.h
    sources/pulseSource.h
    sources/sineSource.h
    sources/functionSource.h
    sources/commSource.h
    sources/blockSource.h
    sources/grabberSource.h
)

set(gridSource_sources
    ${gridSource_headers}
    sources/Source.cpp
    sources/rampSource.cpp
    sources/fileSource.cpp
    sources/randomSource.cpp
    sources/pulseSource.cpp
    sources/sineSource.cpp
    sources/functionSource.cpp
    sources/commSource.cpp
    sources/blockSource.cpp
    sources/grabberSource.cpp
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
    blocks/integralBlock.h
    blocks/deadbandBlock.h
    blocks/delayBlock.h
    blocks/nullBlock.h
    blocks/pidBlock.h
    blocks/controlBlock.h
    blocks/derivativeBlock.h
    blocks/functionBlock.h
    blocks/lutBlock.h
    blocks/transferFunctionBlock.h
    blocks/blockSequence.h
    blocks/filteredDerivativeBlock.h
    blocks/valueLimiter.h
    blocks/rampLimiter.h
    blocks/blockLibrary.h
)

set(block_sources
    ${block_headers}
    blocks/Block.cpp
    blocks/integralBlock.cpp
    blocks/deadbandBlock.cpp
    blocks/delayBlock.cpp
    blocks/nullBlock.cpp
    blocks/pidBlock.cpp
    blocks/controlBlock.cpp
    blocks/derivativeBlock.cpp
    blocks/functionBlock.cpp
    blocks/lutBlock.cpp
    blocks/transferFunctionBlock.cpp
    blocks/filteredDerivativeBlock.cpp
    blocks/blockSequence.cpp
    blocks/valueLimiter.cpp
    blocks/rampLimiter.cpp
)

set(submodel_sources ${genmodel_sources} ${exciter_sources} ${gov_sources} ${gridSource_sources}
                     ${block_sources}
)

set(load_headers
    Load.h
    loads/rampLoad.h
    loads/exponentialLoad.h
    loads/sourceLoad.h
    loads/fileLoad.h
    loads/fDepLoad.h
    loads/motorLoad.h
    loads/motorLoad3.h
    loads/motorLoad5.h
    loads/gridLabDLoad.h
    loads/svd.h
    loads/aggregateLoad.h
    loads/zipLoad.h
    loads/frequencySensitiveLoad.h
    loads/ThreePhaseLoad.h
)

set(load_sources
    ${load_headers}
    loads/Load.cpp
    loads/rampLoad.cpp
    loads/fileLoad.cpp
    loads/motorLoad.cpp
    loads/motorLoad3.cpp
    loads/motorLoad5.cpp
    loads/approximatingLoad.cpp
    loads/exponentialLoad.cpp
    loads/fDepLoad.cpp
    loads/aggregateLoad.cpp
    loads/sourceLoad.cpp
    loads/zipLoad.cpp
    loads/svd.cpp
    loads/ThreePhaseLoad.cpp
    loads/frequencySensitiveLoad.cpp
    loads/gridLabDLoad.cpp
)

set(link_headers
    links/dcLink.h
    links/acdcConverter.h
    links/adjustableTransformer.h
    Link.h
    links/subsystem.h
    links/hvdc.h
    links/zBreaker.h
    links/longLine.h
    links/acLine.h
    links/ThreeWindingTransformer.h
)

set(link_sources
    ${link_headers}
    links/Link.cpp
    links/adjustableTransformer.cpp
    links/dcLink.cpp
    links/acdcConverter.cpp
    links/subsystem.cpp
    links/hvdc.cpp
    links/zBreaker.cpp
    links/longLine.cpp
    links/acLine.cpp
    links/ThreeWindingTransformer.cpp
)

set(simulation_headers
    simulation/contingency.h
    simulation/continuation.h
    gridDynSimulation.h
    simulation/gridSimulation.h
    simulation/diagnostics.h
    simulation/powerFlowErrorRecovery.h
    simulation/dynamicInitialConditionRecovery.h
    simulation/faultResetRecovery.h
    simulation/gridDynActions.h
    simulation/gridDynSimulationFileOps.h
)

set(simulation_sources
    ${simulation_headers}
    simulation/gridDynContinuation.cpp
    simulation/gridDynSimulation.cpp
    simulation/contingency.cpp
    simulation/gridDynPowerFlow.cpp
    simulation/gridDynDynamic.cpp
    simulation/gridSimulation.cpp
    simulation/gridDynActions.cpp
    simulation/gridDynSimulationFileOps.cpp
    simulation/diagnostics.cpp
    simulation/powerFlowErrorRecovery.cpp
    simulation/dynamicInitialConditionRecovery.cpp
    simulation/faultResetRecovery.cpp
    simulation/gridDynContingency.cpp
)

set(solver_headers
    solvers/solverInterface.h
    solvers/sundialsInterface.h
    solvers/idaInterface.h
    solvers/kinsolInterface.h
    solvers/basicSolver.h
    solvers/sundialsMatrixData.h
    solvers/basicOdeSolver.h
    solvers/solverMode.hpp
)

set(solver_sources
    ${solver_headers}
    solvers/idaInterface.cpp
    solvers/kinsolInterface.cpp
    solvers/solverInterface.cpp
    solvers/basicSolver.cpp
    solvers/sundialsMatrixData.cpp
    solvers/sundialsMatrixDataDense.cpp
    solvers/sundialsMatrixDataSparseRow.cpp
    solvers/sundialsMatrixDataSparseColumn.cpp
    solvers/sundialsInterface.cpp
    solvers/basicOdeSolver.cpp
)

if(GRIDDYN_ENABLE_CVODE)
    list(APPEND solver_sources solvers/cvodeInterface.cpp solvers/cvodeInterface.h)

    list(APPEND solver_headers solvers/cvodeInterface.h)

endif(GRIDDYN_ENABLE_CVODE)

if(GRIDDYN_ENABLE_ARKODE)
    list(APPEND solver_sources solvers/arkodeInterface.cpp solvers/arkodeInterface.h)

    list(APPEND solver_headers solvers/arkodeInterface.h)
endif(GRIDDYN_ENABLE_ARKODE)

set(core_headers
    gridComponent.h
    gridPrimary.h
    gridSecondary.h
    gridSubModel.h
    gridComponentHelperClasses.h
    numericEstimationFunctions.h
    gridDynDefinitions.hpp
    offsetTable.h
    gridDynVersion.hpp
)

set(core_sources
    ${core_headers}
    gridComponent.cpp
    gridPrimary.cpp
    gridSecondary.cpp
    gridSubModel.cpp
    gridComponentHelperClasses.cpp
    numericEstimationFunctions.cpp
    offsetTable.cpp
)

set(primary_headers
    gridBus.h
    Area.h
    primary/dcBus.h
    allGridDynObjects.h
    primary/BusControls.h
    primary/listMaintainer.h
    primary/infiniteBus.h
    primary/acBus.h
    primary/DcBusControls.h
)

set(primary_sources
    ${primary_headers}
    primary/gridBus.cpp
    primary/Area.cpp
    primary/dcBus.cpp
    primary/infiniteBus.cpp
    primary/BusControls.cpp
    primary/DcBusControls.cpp
    primary/acBus.cpp
    primary/listMaintainer.cpp
)

set(event_headers
    events/Event.h
    events/eventAdapters.h
    events/eventQueue.h
    events/eventInterface.hpp
    events/reversibleEvent.h
    events/parameterOperator.h
    events/Player.h
    events/interpolatingPlayer.h
    events/compoundEvent.h
    events/compoundEventPlayer.h
)

set(event_sources
    ${event_headers}
    events/Event.cpp
    events/eventAdapters.cpp
    events/eventQueue.cpp
    events/Player.cpp
    events/interpolatingPlayer.cpp
    events/compoundEvent.cpp
    events/compoundEventPlayer.cpp
    events/reversibleEvent.cpp
    events/parameterOperator.cpp
)
set(measurement_headers
    measurement/gridGrabbers.h
    measurement/objectGrabbers.h
    measurement/collector.h
    measurement/Recorder.h
    measurement/stateGrabber.h
    measurement/Condition.h
    measurement/grabberSet.h
    measurement/grabberInterpreter.hpp
)

set(measurement_sources
    ${measurement_headers}
    measurement/Recorder.cpp
    measurement/gridGrabbers.cpp
    measurement/grabberInterpreter.cpp
    measurement/stateGrabbers.cpp
    measurement/Condition.cpp
    measurement/objectGrabbers.cpp
    measurement/compoundCondition.cpp
    measurement/collector.cpp
    measurement/grabberSet.cpp
)

set(comm_headers
    comms/commMessage.h
    comms/Communicator.h
    comms/communicationsCore.h
    comms/schedulerMessage.h
    comms/controlMessage.h
    comms/commManager.h
)

set(comm_sources
    ${comm_headers}
    comms/commMessage.cpp
    comms/Communicator.cpp
    comms/communicationsCore.cpp
    comms/schedulerMessage.cpp
    comms/controlMessage.cpp
    comms/commManager.cpp
)

set(relay_headers
    Relay.h
    relays/zonalRelay.h
    relays/loadRelay.h
    relays/busRelay.h
    relays/fuse.h
    relays/breaker.h
    relays/sensor.h
    relays/differentialRelay.h
    relays/controlRelay.h
    relays/pmu.h
)

set(relay_sources
    ${relay_headers}
    relays/Relay.cpp
    relays/zonalRelay.cpp
    relays/loadRelay.cpp
    relays/busRelay.cpp
    relays/fuse.cpp
    relays/breaker.cpp
    relays/sensor.cpp
    relays/differentialRelay.cpp
    relays/controlRelay.cpp
    relays/pmu.cpp
)

set(controller_headers
    controllers/schedulerInfo.h
    controllers/scheduler.h
    controllers/reserveDispatcher.h
    controllers/AGControl.h
    controllers/controlSystem.h
    controllers/dispatcher.h
)

set(controller_sources
    ${controller_headers}
    controllers/scheduler.cpp
    controllers/schedulerRamp.cpp
    controllers/schedulerReg.cpp
    controllers/reserveDispatcher.cpp
    controllers/controlSystem.cpp
    controllers/AGControl.cpp
    controllers/dispatcher.cpp
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
    # target_link_libraries(this, ticpp::ticpp cereal::cereal (and maybe others)) when available
    # (why is ticpp here at all? cereal is a little more justifiable)
    PRIVATE $<BUILD_INTERFACE:${PROJECT_SOURCE_DIR}/ThirdParty>
)

target_link_libraries(griddyn PUBLIC coreObjects)

target_link_libraries(griddyn PUBLIC SUNDIALS::SUNDIALS)
target_link_libraries(griddyn PUBLIC coupling_static_lib)
if(GRIDDYN_ENABLE_KLU)
    if(GRIDDYN_SUITESPARSE_LOCAL_BUILD)
        target_link_libraries(griddyn PUBLIC klu)
        target_include_directories(griddyn PRIVATE ${KLU_INCLUDE_DIR})
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
