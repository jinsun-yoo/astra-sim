#!/bin/bash

################################################################################
# Abseil and Protobuf Installation Script
################################################################################
#
# This script extracts and compiles abseil and protobuf from source.
# It is idempotent and skips installation if libraries are already present.
#
# Prerequisites:
#   - cmake
#   - wget
#   - g++ / gcc
#   - make
#
# Usage:
#   ./install-abseil-protobuf.sh [install_root]
#
# Arguments:
#   install_root  Installation root directory (default: ~/scratch/local/)
#
# Environment Variables Set:
#   absl_DIR      Path to abseil installation
#   protobuf_DIR  Path to protobuf installation
#   PATH          Updated to include protobuf bin directory
#
# Example:
#   ./install-abseil-protobuf.sh ~/scratch/local
#
################################################################################

set -euo pipefail

# Configuration
ABSL_VER=${ABSL_VER:-20240722.0}
PROTOBUF_VER=${PROTOBUF_VER:-29.0}
INSTALL_ROOT="${1:-$HOME/scratch/local}"

# Ensure install root exists
mkdir -p "$INSTALL_ROOT"

# Color codes for output
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
NC='\033[0m' # No Color

################################################################################
# Helper Functions
################################################################################
log_empty() {
    echo -e "$1"
}

log_info() {
    echo -e "${GREEN}[INFO]${NC} $1"
}

log_warn() {
    echo -e "${YELLOW}[WARN]${NC} $1"
}

log_error() {
    echo -e "${RED}[ERROR]${NC} $1" >&2
}

check_command() {
    if ! command -v "$1" &> /dev/null; then
        log_error "Required command not found: $1"
        exit 1
    fi
}

check_prerequisites() {
    log_info "Checking prerequisites..."
    check_command cmake
    check_command wget
    check_command g++
    check_command make
    log_info "All prerequisites found."
}

is_installed() {
    local install_path="$1"
    local check_file="$2"

    if [[ -f "$install_path/$check_file" ]]; then
        return 0
    else
        return 1
    fi
}

cleanup_on_error() {
    log_warn "Interrupted or error occurred. Partial installation may remain."
}

trap cleanup_on_error EXIT

################################################################################
# Abseil Installation
################################################################################

install_abseil() {
    log_info "================================"
    log_info "Abseil Installation (v$ABSL_VER)"
    log_info "================================"

    local absl_install_path="$INSTALL_ROOT/abseil-cpp-${ABSL_VER}/install"
    local absl_check_file="lib/libabsl_base.a"

    # Check if already installed
    if is_installed "$absl_install_path" "$absl_check_file"; then
        log_warn "Abseil v$ABSL_VER already installed at $absl_install_path"
        log_info "Skipping abseil installation."
        export absl_DIR="$absl_install_path"
        return 0
    fi

    # Download
    log_info "Downloading abseil-cpp-${ABSL_VER}..."
    cd "$INSTALL_ROOT"

    if [[ ! -f "abseil-cpp-${ABSL_VER}.tar.gz" ]]; then
        wget -q "https://github.com/abseil/abseil-cpp/releases/download/${ABSL_VER}/abseil-cpp-${ABSL_VER}.tar.gz"
        log_info "Downloaded abseil-cpp-${ABSL_VER}.tar.gz"
    else
        log_warn "abseil-cpp-${ABSL_VER}.tar.gz already exists, skipping download"
    fi

    # Extract
    if [[ ! -d "abseil-cpp-${ABSL_VER}" ]]; then
        log_info "Extracting abseil-cpp-${ABSL_VER}.tar.gz..."
        tar -xf "abseil-cpp-${ABSL_VER}.tar.gz"
        log_info "Extracted to abseil-cpp-${ABSL_VER}/"
    else
        log_warn "abseil-cpp-${ABSL_VER}/ already exists, skipping extraction"
    fi

    # Compile
    mkdir -p "abseil-cpp-${ABSL_VER}/build"
    cd "abseil-cpp-${ABSL_VER}/build"

    log_info "Configuring abseil with CMake..."
    cmake .. \
        -DCMAKE_CXX_STANDARD=14 \
        -DCMAKE_BUILD_TYPE=Release \
        -DCMAKE_INSTALL_PREFIX="$absl_install_path"

    log_info "Building abseil (using $(nproc) parallel jobs)..."
    cmake --build . --target install --config Release --parallel "$(nproc)"

    log_info "Abseil v$ABSL_VER installed successfully at $absl_install_path"

    # Set environment variable
    export absl_DIR="$absl_install_path"
}

################################################################################
# Protobuf Installation
################################################################################

install_protobuf() {
    log_info "================================"
    log_info "Protobuf Installation (v$PROTOBUF_VER)"
    log_info "================================"

    local protobuf_install_path="$INSTALL_ROOT/protobuf-${PROTOBUF_VER}/install"
    local protobuf_check_file="bin/protoc"

    # Check if already installed
    if is_installed "$protobuf_install_path" "$protobuf_check_file"; then
        log_warn "Protobuf v$PROTOBUF_VER already installed at $protobuf_install_path"
        log_info "Skipping protobuf installation."
        export protobuf_DIR="$protobuf_install_path"
        export PATH="$protobuf_install_path/bin:$PATH"
        return 0
    fi

    # Download
    log_info "Downloading protobuf-${PROTOBUF_VER}..."
    cd "$INSTALL_ROOT"

    if [[ ! -f "protobuf-${PROTOBUF_VER}.tar.gz" ]]; then
        wget -q "https://github.com/protocolbuffers/protobuf/releases/download/v${PROTOBUF_VER}/protobuf-${PROTOBUF_VER}.tar.gz"
        log_info "Downloaded protobuf-${PROTOBUF_VER}.tar.gz"
    else
        log_warn "protobuf-${PROTOBUF_VER}.tar.gz already exists, skipping download"
    fi

    # Extract
    if [[ ! -d "protobuf-${PROTOBUF_VER}" ]]; then
        log_info "Extracting protobuf-${PROTOBUF_VER}.tar.gz..."
        tar -xf "protobuf-${PROTOBUF_VER}.tar.gz"
        log_info "Extracted to protobuf-${PROTOBUF_VER}/"
    else
        log_warn "protobuf-${PROTOBUF_VER}/ already exists, skipping extraction"
    fi

    # Compile
    mkdir -p "protobuf-${PROTOBUF_VER}/build"
    cd "protobuf-${PROTOBUF_VER}/build"

    log_info "Configuring protobuf with CMake..."
    cmake .. \
        -DCMAKE_CXX_STANDARD=14 \
        -DCMAKE_BUILD_TYPE=Release \
        -Dprotobuf_BUILD_TESTS=OFF \
        -Dprotobuf_ABSL_PROVIDER=package \
        -DCMAKE_PREFIX_PATH="$absl_DIR" \
        -DCMAKE_INSTALL_PREFIX="$protobuf_install_path"

    log_info "Building protobuf (using $(nproc) parallel jobs)..."
    cmake --build . --target install --config Release --parallel "$(nproc)"

    log_info "Protobuf v$PROTOBUF_VER installed successfully at $protobuf_install_path"

    # Set environment variables
    export protobuf_DIR="$protobuf_install_path"
    export PATH="$protobuf_install_path/bin:$PATH"
}

################################################################################
# Cleanup
################################################################################

cleanup_tarballs() {
    log_info "Cleaning up downloaded tarballs..."
    cd "$INSTALL_ROOT"
    rm -f "abseil-cpp-${ABSL_VER}.tar.gz" "protobuf-${PROTOBUF_VER}.tar.gz"
    log_info "Cleanup complete."
}

################################################################################
# Main Execution
################################################################################

main() {
    log_info "Starting abseil and protobuf installation..."
    log_info "Installation root: $INSTALL_ROOT"

    check_prerequisites
    install_abseil
    install_protobuf
    cleanup_tarballs

    log_info "================================"
    log_info "Installation Complete!"
    log_info "================================"
    log_info ""
    log_info "To use these libraries, set the following environment variables:"
    log_info "  export absl_DIR=\"$absl_DIR\""
    log_info "  export protobuf_DIR=\"$protobuf_DIR\""
    log_info "  export PATH=\"$protobuf_DIR/bin:\$PATH\""
    log_info ""
    log_info "You may want to add these to your shell configuration (.bashrc, .zshrc, etc.)"
}

main "$@"
