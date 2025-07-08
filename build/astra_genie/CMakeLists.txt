# CMake Requirement
cmake_minimum_required(VERSION 3.15)

# C++ requirement
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)
# Set the build type to Release if not specified
if (NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE Release)
endif ()
set(CMAKE_CXX_FLAGS_DEBUG "-O0 -Wall -Wextra -Wuninitialized -Wunused-function -Wno-unused-parameter -Wno-empty-body -g")#-fsanitize=thread")# -fsanitize=address")#,undefined,leak -g")
set(CMAKE_CXX_FLAGS_RELEASE "-O3")


# TODO: Remove this once binary_function deprecation is resolved.
set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -Wno-error")

# Setup project
project(AstraSim_Genie)

# Compile AstraSim library
add_subdirectory(${CMAKE_CURRENT_SOURCE_DIR}/../../ AstraSim)

# Set variables needed to build Gloo as a library.
set(GLOO_DIR "${CMAKE_CURRENT_SOURCE_DIR}/../../extern/network_backend/gloo")
set(USE_IBVERBS 1 CACHE BOOL "Enable ibverbs transport")
# Switch between REDIS or MPI depending on deployment environment
set(USE_REDIS 0 CACHE BOOL "Enable redis transport")
set(USE_MPI 1 CACHE BOOL "Enable MPI support")
set(USE_FILESTORE 0 CACHE BOOL "Enable filestore support")

if (USE_REDIS)
        message(STATUS "Redis support is enabled")
	message(STATUS "Environment variable 'INSTALL_HIREDIS_LOCALLY'. Set to true if installed from source under ~/.local: "ENV{INSTALL_HIREDIS_LOCALLY})
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

if (USE_FILESTORE)
        message(STATUS "Filestore support is enabled")
endif()

set(BUILD_SHARED_LIBS ON)
# Build Gloo as a library
add_subdirectory(${GLOO_DIR} Gloo)
# Build the Genie target.
add_subdirectory(${CMAKE_CURRENT_SOURCE_DIR}/../../astra-sim/network_frontend/genie/ AstraSim_Genie)
