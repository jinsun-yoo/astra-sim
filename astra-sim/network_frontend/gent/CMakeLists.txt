# CMake Requirement
cmake_minimum_required(VERSION 3.15)

# C++ requirement
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Set the build type to Release if not specified
if (NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE Release)
endif ()

# Setup project
project(AstraSim_Gent)

# Compilation target

# Include src files to compile
file(GLOB srcs
        ${CMAKE_CURRENT_SOURCE_DIR}/*.cc
        ${CMAKE_CURRENT_SOURCE_DIR}/gent_network/*.cc
)

# Compile Congestion Unaware Backend
add_executable(AstraSim_Gent ${srcs})
target_sources(AstraSim_Gent PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/main.cc)
# Link libraries
target_link_libraries(AstraSim_Gent PRIVATE AstraSim)
target_link_libraries(AstraSim_Gent PRIVATE gloo)

# Include directories
target_include_directories(AstraSim_Gent PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/include/)
target_include_directories(AstraSim_Gent PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/../../../extern/)
target_include_directories(AstraSim_Gent PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/../../../extern/helper)
target_include_directories(AstraSim_Gent PRIVATE ${GLOO_DIR})

if(USE_MPI)
  #set (MPI_USE_STATIC_LIBS ON)
  find_package(MPI REQUIRED)
  if(MPI_C_FOUND)
    #if(DEFINED ENV{CUSTOM_MPI_INCLUDE_PATH})
    #    message(STATUS "DEFINED CUSTOM MPI INCLUDE PATH AS " $ENV{CUSTOM_MPI_INCLUDE_PATH})
    #    set(MPI_INCLUDE_PATH $ENV{CUSTOM_MPI_INCLUDE_PATH})
    #endif()
    message(STATUS "MPI include path: " ${MPI_INCLUDE_PATH})
    message(STATUS "MPI libraries: " ${MPI_LIBRARIES})
    target_include_directories(AstraSim_Gent PRIVATE ${MPI_INCLUDE_PATH})
    target_link_libraries(AstraSim_Gent PRIVATE ${MPI_LIBRARIES})
    #list(APPEND gloo_DEPENDENCY_LIBS ${MPI_CXX_LIBRARIES})
    add_definitions(-DGLOO_USE_MPI=1)
  else()
    message(WARNING "Not compiling with MPI support. Suppress this warning with -DUSE_MPI=OFF.")
    set(USE_MPI OFF)
  endif()
endif()

if (USE_REDIS)
        target_include_directories(AstraSim_Gent PRIVATE ${HIREDIS_INCLUDE_DIR_HIREDIS})
endif ()


# Properties
# TODO: Switch to OFF after binary_function deprecation has been resolved
set_target_properties(AstraSim_Gent PROPERTIES COMPILE_WARNING_AS_ERROR OFF)
set_target_properties(AstraSim_Gent
        PROPERTIES
        RUNTIME_OUTPUT_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}/../bin/
        LIBRARY_OUTPUT_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}/../lib/
        ARCHIVE_OUTPUT_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}/../lib/
)

