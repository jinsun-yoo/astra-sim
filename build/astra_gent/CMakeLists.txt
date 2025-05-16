# CMake Requirement
cmake_minimum_required(VERSION 3.15)

# C++ requirement
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Set the build type to Release if not specified
if (NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE Release)
endif ()
set(CMAKE_CXX_FLAGS_DEBUG "-O0 -Wall -Wextra -Wuninitialized -Wunused-function -Wno-unused-parameter -Wno-empty-body -g")#-fsanitize=thread")# -fsanitize=address")#,undefined,leak -g")
#set(CMAKE_CXX_FLAGS_DEBUG "-O3 -Wall -Wextra -Wuninitialized -Wunused-function -Wno-unused-parameter -Wno-empty-body -g") #-fsanitize=address,undefined,leak -g")
set(CMAKE_CXX_FLAGS_RELEASE "-O3")


# TODO: Remove this once binary_function deprecation is resolved.
set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -Wno-error")
##set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -static")

# Setup project
project(AstraSim_Gent)

# Compilation target
set(BUILDTARGET "all" CACHE STRING "Compilation target ([all]/congestion_unaware/congestion_aware)")

# Compile AstraSim library
add_subdirectory(${CMAKE_CURRENT_SOURCE_DIR}/../../ AstraSim)

# Build gloo as a library
set(GLOO_DIR "${CMAKE_CURRENT_SOURCE_DIR}/../../extern/network_backend/gloo")
set(USE_IBVERBS 1 CACHE BOOL "Enable ibverbs transport")
set(USE_REDIS 1 CACHE BOOL "Enable redis transport")
if (USE_REDIS)
        message(STATUS "Redis support is enabled")
	message(STATUS ENV{INSTALL_HIREDIS_LOCALLY})
        # Set variables needed for gloo's CMakeLists.txt
        if (DEFINED ENV{INSTALL_HIREDIS_LOCALLY})
		message(STATUS "HIREDIS IS INSTALLED LOCALLY")
                set(HIREDIS_INCLUDE_DIR_HIREDIS "$ENV{HOME}/.local/include/hiredis")
                set(HIREDIS_LIB_DIR "$ENV{HOME}/.local/lib")
                set(HIREDIS_INCLUDE_DIR "$ENV{HOME}/.local/include")
        else ()
                set(HIREDIS_INCLUDE_DIR_HIREDIS "/usr/include/hiredis")
        endif ()
endif ()

if (USE_MPI)
        message(STATUS "MPI support is enabled")
endif()

set(BUILD_SHARED_LIBS ON)
add_subdirectory(${GLOO_DIR} Gloo)

add_subdirectory(${CMAKE_CURRENT_SOURCE_DIR}/../../astra-sim/network_frontend/gent/ AstraSim_Gent)
