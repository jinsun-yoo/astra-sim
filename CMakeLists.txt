# # *****************************************************************************
# # This source code is licensed under the MIT license found in the
# # LICENSE file in the root directory of this source tree.
# #
# # Copyright (c) 2024 Georgia Institute of Technology
# # *****************************************************************************

# CMake Requirement
cmake_minimum_required(VERSION 3.22)

# C++ requirement
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Set the build type to Release if not specified
if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE Release)
endif()

# Setup project
project(AstraSim)

# Add libraries.
set(BUILD_SHARED_LIBS ON CACHE BOOL "Build shared libraries" FORCE)
set(SPDLOG_BUILD_SHARED ON CACHE BOOL "Build spdlog as a shared library" FORCE)
add_subdirectory("${CMAKE_CURRENT_SOURCE_DIR}/extern/helper/fmt")
option(SPDLOG_FMT_EXTERNAL ON) # override default option for spdlog.
add_subdirectory("${CMAKE_CURRENT_SOURCE_DIR}/extern/helper/spdlog")

# Find Protobuf
# TODO: We want to use the advanced semantics 'protobuf CONFIG REQUIRED'.
# However, this requires protobuf to be compiled from source,
# which is enabled only in Docker containers. (Our documentation makes users install protobuc from apt-get)
# Therefore, enable 'protobuf CONFIG REQUIRED' only when we are building in Docker.
# TODO: Find a better way to detect if protobuf has been compiled by source, OR
# find a way to enable 'protobuf CONFIG REQUIRED' regardless of how protubuf was installed.
if(DEFINED ENV{PROTOBUF_FROM_SOURCE} AND "$ENV{PROTOBUF_FROM_SOURCE}" STREQUAL "True")
    find_package(protobuf CONFIG REQUIRED)
else()
    find_package(Protobuf REQUIRED)
endif()

# Files to compile
file(GLOB srcs
    "${CMAKE_CURRENT_SOURCE_DIR}/astra-sim/system/*.cc"
    "${CMAKE_CURRENT_SOURCE_DIR}/astra-sim/system/statistics/*.cc"
    "${CMAKE_CURRENT_SOURCE_DIR}/astra-sim/workload/*.cc"
    "${CMAKE_CURRENT_SOURCE_DIR}/astra-sim/system/astraccl/*.cc"
    "${CMAKE_CURRENT_SOURCE_DIR}/astra-sim/system/astraccl/native_collectives/*.cc"
    "${CMAKE_CURRENT_SOURCE_DIR}/astra-sim/system/astraccl/native_collectives/logical_topology/*.cc"
    "${CMAKE_CURRENT_SOURCE_DIR}/astra-sim/system/astraccl/native_collectives/collective_algorithm/*.cc"
    "${CMAKE_CURRENT_SOURCE_DIR}/astra-sim/system/astraccl/custom_collectives/*.cc"
    "${CMAKE_CURRENT_SOURCE_DIR}/astra-sim/system/memory/*.cc"
    "${CMAKE_CURRENT_SOURCE_DIR}/astra-sim/system/scheduling/*.cc"
    "${CMAKE_CURRENT_SOURCE_DIR}/astra-sim/common/*.cc"
    "${CMAKE_CURRENT_SOURCE_DIR}/extern/graph_frontend/chakra/src/third_party/utils/*.cc"
    "${CMAKE_CURRENT_SOURCE_DIR}/extern/graph_frontend/chakra/schema/protobuf/*.cc"
    "${CMAKE_CURRENT_SOURCE_DIR}/extern/graph_frontend/chakra/src/feeder/*.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/extern/remote_memory_backend/analytical/*.cc")

# Compile AstraSim Library
add_library(AstraSim STATIC ${srcs})

# Link libraries
target_link_libraries(AstraSim PUBLIC fmt::fmt)
target_link_libraries(AstraSim PUBLIC spdlog::spdlog)

# Same as above.
if(DEFINED ENV{PROTOBUF_FROM_SOURCE} AND "$ENV{PROTOBUF_FROM_SOURCE}" STREQUAL "True")
    target_link_libraries(AstraSim PUBLIC protobuf::libprotobuf)
else()
    target_link_libraries(AstraSim PUBLIC ${Protobuf_LIBRARIES})
endif()

# Include Directories
target_include_directories(AstraSim PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_include_directories(AstraSim PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/extern/graph_frontend/chakra/)
target_include_directories(AstraSim PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/extern/graph_frontend/chakra/schema/protobuf)
target_include_directories(AstraSim PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/extern/graph_frontend/chakra/src/third_party/utils)
target_include_directories(AstraSim PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/extern/helper/)

# Link MPI/Redis/Filestore libraries to Genie
set(USE_MPI ON)
if(USE_MPI)
  find_package(MPI REQUIRED)
  if(MPI_C_FOUND)
    message(STATUS "MPI include path: " ${MPI_INCLUDE_PATH})
    message(STATUS "MPI libraries: " ${MPI_LIBRARIES})
    target_include_directories(AstraSim PRIVATE ${MPI_INCLUDE_PATH})
    target_link_libraries(AstraSim PRIVATE ${MPI_LIBRARIES})
    add_definitions(-DGLOO_USE_MPI=1)
  else()
    message(WARNING "USE_MPI was set to True, but could not find MPI. End compilation.")
    return()
  endif()
endif()


# Allow overriding the scale-up group size via environment variable
# If SCALE_UP_GROUP_SIZE is set in the environment it will be forwarded
# as a compile-time macro so the header can pick it up.
set(SCALE_UP_GROUP_SIZE 8) # Default: 4.
if(DEFINED ENV{SCALE_UP_GROUP_SIZE})
  message(STATUS "Overriding SCALE_UP_GROUP_SIZE from environment: $ENV{SCALE_UP_GROUP_SIZE}")
  set(SCALE_UP_GROUP_SIZE "$ENV{SCALE_UP_GROUP_SIZE}")
endif()
target_compile_definitions(AstraSim PUBLIC SCALE_UP_GROUP_SIZE=${SCALE_UP_GROUP_SIZE})

# Allow overriding the ChromeTracer queue size via environment variable
# If CHROMETRACE_QUEUE_SIZE is set in the environment it will be forwarded
# as a compile-time macro CHROME_TRACER_MAX_QUEUE_SIZE so the header can
# pick it up and adjust the static queue sizes.
set (CHROMETRACE_QUEUE_SIZE 2097152) # Default: 2^21.
if(DEFINED ENV{CHROMETRACE_QUEUE_SIZE})
  message(STATUS "Overriding CHROMETRACE_QUEUE_SIZE from environment: $ENV{CHROMETRACE_QUEUE_SIZE}")
  # Expose as a preprocessor macro; value should be an integer literal
  set(CHROMETRACE_QUEUE_SIZE "$ENV{CHROMETRACE_QUEUE_SIZE}")
endif()
target_compile_definitions(AstraSim PUBLIC CHROMETRACE_QUEUE_SIZE=${CHROMETRACE_QUEUE_SIZE})

# set(CHROMETRACE WORKLOAD)
# Allow overriding CHROMETRACE via environment variable
if(DEFINED ENV{CHROMETRACE})
  message(STATUS "Overriding CHROMETRACE from environment: $ENV{CHROMETRACE}")
  set(CHROMETRACE "$ENV{CHROMETRACE}")
endif()
if(CHROMETRACE STREQUAL "WORKLOAD")
  message(STATUS "Enabling Chrome Tracing for Workload")
  target_compile_definitions(AstraSim PUBLIC GENIE_CHROMETRACE_WORKLOAD=1)
endif()
if(CHROMETRACE STREQUAL "EVENT")
  message(STATUS "Enabling Chrome Tracing for Events")
  target_compile_definitions(AstraSim PUBLIC GENIE_CHROMETRACE_WORKLOAD=1)
  target_compile_definitions(AstraSim PUBLIC GENIE_CHROMETRACE_EVENT=1)
endif()

if(DEFINED ENV{TRACE_SIMPLERING})
  set(_trace_simplering_env "$ENV{TRACE_SIMPLERING}")
  string(TOUPPER "$ENV{TRACE_SIMPLERING}" _trace_simplering_env_upper)
  if(_trace_simplering_env_upper STREQUAL "1" OR
     _trace_simplering_env_upper STREQUAL "TRUE" OR
     _trace_simplering_env_upper STREQUAL "ON" OR
     _trace_simplering_env_upper STREQUAL "YES")
    message(STATUS "Enabling TRACE_SIMPLERING from environment: ${_trace_simplering_env}")
    target_compile_definitions(AstraSim PUBLIC TRACE_SIMPLERING=1)
  else()
    message(STATUS "TRACE_SIMPLERING is defined but not truthy ('${_trace_simplering_env}'); TRACE_SIMPLERING macro is disabled")
  endif()
endif()

if(DEFINED ENV{TRACE_SIMPLETREE})
  set(_trace_simpletree_env "$ENV{TRACE_SIMPLETREE}")
  string(TOUPPER "$ENV{TRACE_SIMPLETREE}" _trace_simpletree_env_upper)
  if(_trace_simpletree_env_upper STREQUAL "1" OR
     _trace_simpletree_env_upper STREQUAL "TRUE" OR
     _trace_simpletree_env_upper STREQUAL "ON" OR
     _trace_simpletree_env_upper STREQUAL "YES")
    message(STATUS "Enabling TRACE_SIMPLETREE from environment: ${_trace_simpletree_env}")
    target_compile_definitions(AstraSim PUBLIC TRACE_SIMPLETREE=1)
  else()
    message(STATUS "TRACE_SIMPLETREE is defined but not truthy ('${_trace_simpletree_env}'); TRACE_SIMPLETREE macro is disabled")
  endif()
endif()

if(DEFINED ENV{TRACE_WORKLOAD_DISPATCH})
  set(_trace_workload_dispatch_env "$ENV{TRACE_WORKLOAD_DISPATCH}")
  string(TOUPPER "$ENV{TRACE_WORKLOAD_DISPATCH}" _trace_workload_dispatch_env_upper)
  if(_trace_workload_dispatch_env_upper STREQUAL "1" OR
     _trace_workload_dispatch_env_upper STREQUAL "TRUE" OR
     _trace_workload_dispatch_env_upper STREQUAL "ON" OR
     _trace_workload_dispatch_env_upper STREQUAL "YES")
    message(STATUS "Enabling TRACE_WORKLOAD_DISPATCH from environment: ${_trace_workload_dispatch_env}")
    target_compile_definitions(AstraSim PUBLIC TRACE_WORKLOAD_DISPATCH=1)
  else()
    message(STATUS "TRACE_WORKLOAD_DISPATCH is defined but not truthy ('${_trace_workload_dispatch_env}'); TRACE_WORKLOAD_DISPATCH macro is disabled")
  endif()
endif()

if(DEFINED ENV{TRACE_COMMGROUP})
  set(_trace_commgroup_env "$ENV{TRACE_COMMGROUP}")
  string(TOUPPER "$ENV{TRACE_COMMGROUP}" _trace_commgroup_env_upper)
  if(_trace_commgroup_env_upper STREQUAL "1" OR
     _trace_commgroup_env_upper STREQUAL "TRUE" OR
     _trace_commgroup_env_upper STREQUAL "ON" OR
     _trace_commgroup_env_upper STREQUAL "YES")
    message(STATUS "Enabling TRACE_COMMGROUP from environment: ${_trace_commgroup_env}")
    target_compile_definitions(AstraSim PUBLIC TRACE_COMMGROUP=1)
  else()
    message(STATUS "TRACE_COMMGROUP is defined but not truthy ('${_trace_commgroup_env}'); TRACE_COMMGROUP macro is disabled")
  endif()
endif()

if(DEFINED ENV{GENIE_TIMEOUT})
  set(_genie_timeout_env "$ENV{GENIE_TIMEOUT}")
  string(TOUPPER "$ENV{GENIE_TIMEOUT}" _genie_timeout_env_upper)
  if(_genie_timeout_env_upper STREQUAL "1" OR
     _genie_timeout_env_upper STREQUAL "TRUE" OR
     _genie_timeout_env_upper STREQUAL "ON" OR
     _genie_timeout_env_upper STREQUAL "YES")
    message(STATUS "Enabling GENIE_TIMEOUT from environment: ${_genie_timeout_env}")
    target_compile_definitions(AstraSim PUBLIC GENIE_TIMEOUT=1)
  else()
    message(STATUS "GENIE_TIMEOUT is defined but not truthy ('${_genie_timeout_env}'); GENIE_TIMEOUT macro is disabled")
  endif()
endif()

set (GENIE_TIMEOUT_SECONDS 30) # Default: 30 seconds.
if(DEFINED ENV{GENIE_TIMEOUT_SECONDS})
  message(STATUS "Setting GENIE_TIMEOUT_SECONDS from environment: $ENV{GENIE_TIMEOUT_SECONDS}")
  set(GENIE_TIMEOUT_SECONDS "$ENV{GENIE_TIMEOUT_SECONDS}")
endif()
target_compile_definitions(AstraSim PUBLIC GENIE_TIMEOUT_SECONDS=${GENIE_TIMEOUT_SECONDS})

# Same as above.
if(DEFINED ENV{PROTOBUF_FROM_SOURCE} AND "$ENV{PROTOBUF_FROM_SOURCE}" STREQUAL "True")
    target_include_directories(AstraSim PUBLIC ${Protobuf_INCLUDE_DIRS})
endif()

# Properties
set_target_properties(AstraSim PROPERTIES COMPILE_WARNING_AS_ERROR OFF)
set_target_properties(AstraSim
    PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}/../bin/
    LIBRARY_OUTPUT_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}/../lib/
    ARCHIVE_OUTPUT_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}/../lib/
)
