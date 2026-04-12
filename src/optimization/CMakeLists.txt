# ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
#
# Copyright (c) 2014-2020, Lawrence Livermore National Security
# See the top-level NOTICE for additional details. All rights reserved.
#
# SPDX-License-Identifier: BSD-3-Clause
#
# ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

# project  name

set(opt_sim_headers optHelperClasses.h gridDynOpt.h gridOptObjects.h optObjectFactory.h
                    optimizerInterface.h
)

set(opt_sim_sources
    ${opt_sim_headers}
    optHelperClasses.cpp
    gridDynOpt.cpp
    gridOptObjects.cpp
    optimizerInterface.cpp
    optObjectFactory.cpp
)

set(opt_model_headers
    models/gridBusOpt.h
    models/gridLinkOpt.h
    models/gridGenOpt.h
    models/gridLoadOpt.h
    models/gridAreaOpt.h
    models/gridRelayOpt.h
)

set(opt_model_sources
    ${opt_model_headers}
    models/gridBusOpt.cpp
    models/gridLinkOpt.cpp
    models/gridGenOpt.cpp
    models/gridRelayOpt.cpp
    models/gridLoadOpt.cpp
    models/gridAreaOpt.cpp
)

set(opt_files ${opt_sim_sources} ${opt_model_sources})

set(header_files ${opt_sim_headers} ${opt_models_headers})

add_library(optimization STATIC ${opt_files})

target_link_libraries(optimization PUBLIC griddyn)

source_group("optimization" FILES ${opt_sim_sources})
source_group("models" FILES ${opt_model_sources})

install(FILES ${header_files} DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}/griddyn/optimization
        COMPONENT headers
)
