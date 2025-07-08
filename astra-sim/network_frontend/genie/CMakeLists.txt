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
project(AstraSim_Genie)

# Compilation target

# Include src files to compile
file(GLOB srcs
        ${CMAKE_CURRENT_SOURCE_DIR}/*.cc
        ${CMAKE_CURRENT_SOURCE_DIR}/genie_network/*.cc
)

# Compile Genie's main executable
add_executable(AstraSim_Genie ${srcs})
target_sources(AstraSim_Genie PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/main.cc)
# Link libraries
target_link_libraries(AstraSim_Genie PRIVATE AstraSim)
target_link_libraries(AstraSim_Genie PRIVATE gloo)

# Include directories
target_include_directories(AstraSim_Genie PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/../../../extern/)
target_include_directories(AstraSim_Genie PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/../../../extern/helper)
target_include_directories(AstraSim_Genie PRIVATE ${GLOO_DIR})

# Link MPI/Redis/Filestore libraries to Genie
if(USE_MPI)
  find_package(MPI REQUIRED)
  if(MPI_C_FOUND)
    message(STATUS "MPI include path: " ${MPI_INCLUDE_PATH})
    message(STATUS "MPI libraries: " ${MPI_LIBRARIES})
    target_include_directories(AstraSim_Genie PRIVATE ${MPI_INCLUDE_PATH})
    target_link_libraries(AstraSim_Genie PRIVATE ${MPI_LIBRARIES})
    add_definitions(-DGLOO_USE_MPI=1)
  else()
    message(WARNING "USE_MPI was set to True, but could not find MPI. End compilation.")
    return()
  endif()
endif()

if (USE_REDIS)
        target_include_directories(AstraSim_Genie PRIVATE ${HIREDIS_INCLUDE_DIR_HIREDIS})
endif ()

if (USE_FILESTORE)
  add_definitions(-DGLOO_USE_FILESTORE=1)
endif()


# Properties
# TODO: Switch to OFF after binary_function deprecation has been resolved
set_target_properties(AstraSim_Genie PROPERTIES COMPILE_WARNING_AS_ERROR OFF)
set_target_properties(AstraSim_Genie
        PROPERTIES
        RUNTIME_OUTPUT_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}/../bin/
        LIBRARY_OUTPUT_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}/../lib/
        ARCHIVE_OUTPUT_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}/../lib/
)

