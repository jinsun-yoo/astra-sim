#!/bin/bash
set -e

# set paths
SCRIPT_DIR=$(dirname "$(realpath "$0")")
BUILD_DIR="${SCRIPT_DIR:?}"/build
CHAKRA_ET_DIR="${SCRIPT_DIR:?}"/../../extern/graph_frontend/chakra/schema/protobuf
PROTOC_BIN=""

function add_ld_library_path() {
  local dir="$1"
  if [[ -d "${dir}" ]]; then
    if [[ -z "${LD_LIBRARY_PATH:-}" ]]; then
      export LD_LIBRARY_PATH="${dir}"
    elif [[ ":${LD_LIBRARY_PATH}:" != *":${dir}:"* ]]; then
      export LD_LIBRARY_PATH="${dir}:${LD_LIBRARY_PATH}"
    fi
  fi
}

function setup_protoc() {
  PROTOC_BIN="$(command -v protoc || true)"
  if [[ -z "${PROTOC_BIN}" ]]; then
    echo "Error: protoc is not in PATH" >&2
    exit 1
  fi

  # Ensure protobuf's own runtime libs are discoverable.
  local protoc_prefix
  protoc_prefix="$(dirname "${PROTOC_BIN}")/.."
  add_ld_library_path "${protoc_prefix}/lib64"
  add_ld_library_path "${protoc_prefix}/lib"

  # Newer protobuf may depend on abseil from a separate prefix.
  if ldd "${PROTOC_BIN}" 2>/dev/null | grep -q "libabsl_.*=> not found"; then
    local absl_dir
    absl_dir="$(ls -d "${HOME}"/local/abseil-cpp-*/install/lib64 2>/dev/null | tail -n 1 || true)"
    if [[ -n "${absl_dir}" ]]; then
      add_ld_library_path "${absl_dir}"
    fi
  fi

  if ldd "${PROTOC_BIN}" 2>/dev/null | grep -q "=> not found"; then
    echo "Error: protoc has unresolved shared libraries:" >&2
    ldd "${PROTOC_BIN}" 2>/dev/null | grep "=> not found" >&2
    echo "Hint: add the appropriate runtime lib directory to LD_LIBRARY_PATH." >&2
    exit 1
  fi
}

# set functions
function compile_chakra_et() {
  # compile et_def.proto if one doesn't exist
  if [[ ! -f "${CHAKRA_ET_DIR:?}"/et_def.pb.h || ! -f "${CHAKRA_ET_DIR:?}"/et_def.pb.cc ]]; then
    "${PROTOC_BIN}" et_def.proto \
      --proto_path="${CHAKRA_ET_DIR:?}" \
      --cpp_out="${CHAKRA_ET_DIR:?}"
  fi

  if [[ ! -f "${CHAKRA_ET_DIR:?}"/et_def_pb2.py ]]; then
    "${PROTOC_BIN}" et_def.proto \
      --proto_path="${CHAKRA_ET_DIR:?}" \
      --python_out="${CHAKRA_ET_DIR:?}"
  fi
}

function setup() {
  # make build directory if one doesn't exist
  if [[ ! -d "${BUILD_DIR:?}" ]]; then
    mkdir -p "${BUILD_DIR:?}"
  fi

  # set concurrent build threads, capped at 16
  NUM_THREADS=$(nproc)
  if [[ ${NUM_THREADS} -ge 16 ]]; then
    NUM_THREADS=16
  fi
}

function maybe_reset_cmake_cache() {
  local cache_file="${BUILD_DIR:?}/CMakeCache.txt"
  if [[ ! -f "${cache_file}" ]]; then
    return
  fi

  local cached_cxx
  local cached_c
  local desired_cxx="${CMAKE_CXX_COMPILER:-${CXX:-}}"
  local desired_c="${CMAKE_C_COMPILER:-${CC:-}}"
  local should_reset=false

  cached_cxx="$(grep -E '^CMAKE_CXX_COMPILER(:|=)' "${cache_file}" | head -n 1 | cut -d= -f2-)"
  cached_c="$(grep -E '^CMAKE_C_COMPILER(:|=)' "${cache_file}" | head -n 1 | cut -d= -f2-)"

  if [[ -n "${desired_cxx}" && -n "${cached_cxx}" && "${cached_cxx}" != "${desired_cxx}" ]]; then
    should_reset=true
  fi
  if [[ -n "${desired_c}" && -n "${cached_c}" && "${cached_c}" != "${desired_c}" ]]; then
    should_reset=true
  fi

  if [[ "${cached_cxx}" == "CC" && -z "$(command -v CC || true)" ]]; then
    should_reset=true
  fi
  if [[ "${cached_c}" == "cc" && -z "$(command -v cc || true)" ]]; then
    should_reset=true
  fi

  if [[ "${should_reset}" == true ]]; then
    rm -f "${BUILD_DIR:?}/CMakeCache.txt"
    rm -rf "${BUILD_DIR:?}/CMakeFiles"
  fi
}

function run_cmake_configure() {
  local build_type_arg="$1"

  # Respect external CMAKE_ARGS but allow explicit compiler overrides to win.
  local cmake_cmd="cmake .. -DCMAKE_EXPORT_COMPILE_COMMANDS=ON"
  if [[ -n "${build_type_arg}" ]]; then
    cmake_cmd+=" ${build_type_arg}"
  fi
  if [[ -n "${CMAKE_ARGS:-}" ]]; then
    cmake_cmd+=" ${CMAKE_ARGS}"
  fi
  if [[ -n "${CMAKE_C_COMPILER:-}" ]]; then
    cmake_cmd+=" -DCMAKE_C_COMPILER=${CMAKE_C_COMPILER}"
  elif [[ -n "${CC:-}" ]]; then
    cmake_cmd+=" -DCMAKE_C_COMPILER=${CC}"
  fi
  if [[ -n "${CMAKE_CXX_COMPILER:-}" ]]; then
    cmake_cmd+=" -DCMAKE_CXX_COMPILER=${CMAKE_CXX_COMPILER}"
  elif [[ -n "${CXX:-}" ]]; then
    cmake_cmd+=" -DCMAKE_CXX_COMPILER=${CXX}"
  fi

  eval "${cmake_cmd}"
}

function compile_astrasim_genie() {
  # compile AstraSim
  cd "${BUILD_DIR:?}" || exit
  run_cmake_configure ""
  cp "${SCRIPT_DIR:?}"/build/Gloo/gloo/config.h "${SCRIPT_DIR:?}"/../../extern/network_backend/gloo/gloo/
  cmake --build . -j "${NUM_THREADS:?}" 
}

function compile_astrasim_genie_as_debug() {
  # compile AstraSim
  cd "${BUILD_DIR:?}" || exit
  run_cmake_configure "-DCMAKE_BUILD_TYPE=Debug"
  cp "${SCRIPT_DIR:?}"/build/Gloo/gloo/config.h "${SCRIPT_DIR:?}"/../../extern/network_backend/gloo/gloo/
  cmake --build . --config=Debug -j "${NUM_THREADS:?}"
}

function cleanup() {
  rm -rf "${BUILD_DIR:?}"
  rm -f "${CHAKRA_ET_DIR}/et_def.pb.cc"
  rm -f "${CHAKRA_ET_DIR}/et_def.pb.h"
  rm -f "${CHAKRA_ET_DIR}/et_def_pb2.py"
}

function print_usage() {
  echo "print usage"
}

# set default option values
build_target="all"
build_as_debug=false
should_clean=false

# Process command-line options
while getopts "t:ld" OPT; do
  case "${OPT:?}" in
  t)
    build_target="${OPTARG:?}"
    ;;
  l)
    should_clean=true
    ;;
  d)
    build_as_debug=true
    ;;
  *)
    exit 1
    ;;
  esac
done

# check the validity of build target
if [[ ${build_target:?} != "all" &&
  ${build_target:?} != "congestion_unaware" &&
  ${build_target:?} != "congestion_aware" ]]; then
  echo "Invalid build target: ${build_target:?}" >&2
  exit 1
fi

# run operations as required
if [[ ${should_clean:?} == true ]]; then
  cleanup
else
  # setup ASTRA-sim build
  setup
  maybe_reset_cmake_cache
  setup_protoc
  compile_chakra_et

  # compile ASTRA-sim
  if [[ ${build_as_debug:?} == true ]]; then
    compile_astrasim_genie_as_debug "${build_target:?}"
  else
    compile_astrasim_genie "${build_target:?}"
  fi

fi
