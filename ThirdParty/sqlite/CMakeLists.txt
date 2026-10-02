# ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
#
# SQLite amalgamation
#
# SQLite is public-domain software. The source files in this directory are
# distributed by SQLite as an amalgamation and are kept here so GridDyn can
# build without requiring a separately installed SQLite package.
#
# ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

set(griddyn_sqlite_sources
    sqlite3.c
    sqlite3.h
    sqlite3ext.h
)

add_library(griddyn_sqlite STATIC ${griddyn_sqlite_sources})
add_library(GridDyn::sqlite ALIAS griddyn_sqlite)

target_include_directories(
    griddyn_sqlite
    SYSTEM PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}>
        $<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}/griddyn/sqlite>
)

set_target_properties(
    griddyn_sqlite
    PROPERTIES
        C_STANDARD 11
        C_STANDARD_REQUIRED ON
        FOLDER Extern
)

# GridDyn only needs SQLite as an embedded, read-only database engine. Do not
# compile the loadable-extension entry points into the bundled library.
target_compile_definitions(
    griddyn_sqlite
    PRIVATE
        SQLITE_THREADSAFE=1
        SQLITE_OMIT_LOAD_EXTENSION
)
