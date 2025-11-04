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

set(CHROMETRACE WORKLOAD)
# Allow overriding CHROMETRACE via environment variable
if(DEFINED ENV{CHROMETRACE})
  message(STATUS "Overriding CHROMETRACE from environment: $ENV{CHROMETRACE}")
  set(CHROMETRACE "$ENV{CHROMETRACE}")
endif()
if(CHROMETRACE STREQUAL "WORKLOAD")
  message(STATUS "Enabling Chrome Tracing for Workload")
  target_compile_definitions(AstraSim_Genie PRIVATE GENIE_CHROMETRACE_WORKLOAD=1)
  target_compile_definitions(AstraSim_Genie PRIVATE GENIE_CHROMETRACE_EVENT=1)
endif()
if(CHROMETRACE STREQUAL "EVENT")
  message(STATUS "Enabling Chrome Tracing for Events")
  target_compile_definitions(AstraSim_Genie PRIVATE GENIE_CHROMETRACE_EVENT=1)
endif()


# Allow overriding the ChromeTracer queue size via environment variable
# If CHROMETRACE_QUEUE_SIZE is set in the environment it will be forwarded
# as a compile-time macro CHROME_TRACER_MAX_QUEUE_SIZE so the header can
# pick it up and adjust the static queue sizes.
set (CHROMETRACE_QUEUE_SIZE 2097152) # Default: 2^21.
if(DEFINED ENV{CHROMETRACE_QUEUE_SIZE})
  message(STATUS "Overriding CHROMETRACE_QUEUE_SIZE from environment: $ENV{CHROMETRACE_QUEUE_SIZE}")
  # Expose as a preprocessor macro; value should be an integer literal
  set(CHROMETRACE_QUEUE_SIZE "$ENV{CHROMETRACE_QUEUE_SIZE}")
  target_compile_definitions(AstraSim_Genie PRIVATE CHROMETRACE_QUEUE_SIZE=${CHROMETRACE_QUEUE_SIZE})
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

