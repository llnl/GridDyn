# GridDyn compatibility shim for newer SUNDIALS KLU detection.
#
# The upstream SUNDIALS module expects to discover KLU and to create the
# imported target SUNDIALS::KLU. GridDyn may already have KLU/SuiteSparse
# targets available from addKLU/addlibSuiteSparse, so we bridge them here.

set(KLU_FOUND TRUE)
set(KLU_WORKS TRUE CACHE BOOL "KLU works with SUNDIALS as configured" FORCE)

# Preserve include hints for SUNDIALS diagnostics/consumers.
if(NOT KLU_INCLUDE_DIR AND DEFINED KLU_INCLUDE_DIRS)
    set(KLU_INCLUDE_DIR "${KLU_INCLUDE_DIRS}")
endif()

set(_griddyn_klu_link_targets)

# Prefer GridDyn local SuiteSparse subproject targets when present.
foreach(_klu_tgt IN ITEMS klu btf amd colamd suitesparseconfig)
    if(TARGET ${_klu_tgt})
        list(APPEND _griddyn_klu_link_targets ${_klu_tgt})
    endif()
endforeach()

# Fall back to imported namespaces when available.
if(NOT _griddyn_klu_link_targets)
    foreach(_klu_tgt IN ITEMS Suitesparse::klu Suitesparse::btf Suitesparse::amd Suitesparse::colamd Suitesparse::suitesparseconfig)
        if(TARGET ${_klu_tgt})
            list(APPEND _griddyn_klu_link_targets ${_klu_tgt})
        endif()
    endforeach()
endif()

# Final fallback: variable-based library list.
if(NOT _griddyn_klu_link_targets AND KLU_LIBRARIES)
    list(APPEND _griddyn_klu_link_targets ${KLU_LIBRARIES})
endif()

if(NOT TARGET SUNDIALS::KLU)
    add_library(SUNDIALS::KLU INTERFACE IMPORTED)
endif()

if(_griddyn_klu_link_targets)
    list(REMOVE_DUPLICATES _griddyn_klu_link_targets)
    set_target_properties(SUNDIALS::KLU PROPERTIES
        INTERFACE_LINK_LIBRARIES "${_griddyn_klu_link_targets}"
    )
else()
    message(FATAL_ERROR
        "GridDyn SUNDIALS KLU shim could not locate KLU link targets. "
        "Ensure KLU/SuiteSparse is configured before addSundials."
    )
endif()

if(KLU_INCLUDE_DIR)
    set_target_properties(SUNDIALS::KLU PROPERTIES
        INTERFACE_INCLUDE_DIRECTORIES "${KLU_INCLUDE_DIR}"
    )
endif()

# SUNDIALS' KLU module also uses KLU_LIBRARIES/KLU_INCLUDE_DIR variables in
# status output and some checks.
if(NOT KLU_LIBRARIES AND _griddyn_klu_link_targets)
    set(KLU_LIBRARIES "${_griddyn_klu_link_targets}")
endif()
