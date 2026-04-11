cmake_minimum_required(VERSION 3.10)

project(minizip LANGUAGES C VERSION 2.0)

set(BUILD_SHARED_LIBS OFF)

cmake_policy(VERSION 3.10)

if (TARGET ZLIB::ZLIB)
	set(ZLIB_LINK ZLIB::ZLIB)
elseif (TARGET zlib::zlib)
	set(ZLIB_LINK zlib::zlib)
elseif (EXISTS ${ZLIB_LOCATION}/cmake/zlib-targets.cmake)
    include (${ZLIB_LOCATION}/cmake/zlib-targets.cmake)
	if (TARGET ZLIB::ZLIB)
		set(ZLIB_LINK ZLIB::ZLIB)
	else()
		set(ZLIB_LINK zlib::zlib)
	endif()

else()
    find_package(ZLIB)
    if (TARGET ZLIB::ZLIB)
        set(ZLIB_LINK ZLIB::ZLIB)
    else()
        set(ZLIB_LINK zlib::zlib)
    endif()
endif()

set(SOURCES
  argv.c
  ioapi.c
  miniunz.c
  minizip.c
  unzip.c
  zip.c
)

if(WIN32)
    set(SOURCES ${SOURCES} iowin32.c)
endif(WIN32)

set(HEADERS
  crypt.h
  ioapi.h
  miniunz.h
  minizip.h
  unzip.h
)

set(INTERNAL_HEADERS
  argv.h
 )

if(WIN32)
    set(HEADERS ${HEADERS} iowin32.h)
endif(WIN32)

if (NOT CMAKE_DEBUG_POSTFIX)
set(CMAKE_DEBUG_POSTFIX d)
endif()

add_library(minizip ${SOURCES} ${HEADERS})
add_library(minizip::minizip ALIAS minizip)

if(CMAKE_HOST_APPLE)
set(PLATFORM __APPLE__)
elseif(CMAKE_HOST_UNIX)
set(PLATFORM unix)
elseif(CMAKE_HOST_WIN32)
set(PLATFORM _WIN32)
else(CMAKE_HOST_APPLE)
message(FATAL_ERROR "Not supported Platform")
endif(CMAKE_HOST_APPLE)

target_compile_definitions(minizip PRIVATE ${PLATFORM})

if(CMAKE_COMPILER_IS_GNUCC)
  target_compile_options(minizip PRIVATE "-std=c99")
endif()

if(MSVC)
 target_compile_options(minizip PRIVATE /wd4131 /wd4013 /wd4127 /wd4100 /wd4189 /wd4456 -D_CRT_SECURE_NO_WARNINGS)
endif(MSVC)

target_link_libraries(minizip PRIVATE ${ZLIB_LINK})

if (NOT SKIP_INSTALL_LIBRARIES)
	install(TARGETS minizip EXPORT minizip-targets
       LIBRARY DESTINATION lib
       ARCHIVE DESTINATION lib
	)

	install (EXPORT minizip-targets NAMESPACE minizip:: DESTINATION cmake/)
	INSTALL(FILES ${HEADERS} DESTINATION ${CMAKE_INSTALL_PREFIX}/include/minizip)
endif()
