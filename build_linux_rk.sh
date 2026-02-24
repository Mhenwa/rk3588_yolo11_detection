#!/usr/bin/env bash
set -euo pipefail

# Defaults
TARGET_SOC=""
TARGET_ARCH="aarch64"
BUILD_TYPE="Release"
PROJECT_NAME="demo"
SUBPROJECT_NAME="rtspMulitPlayer"
DO_RUN=1
DO_CLEAN=1
JOBS="$(nproc)"
USE_NATIVE=0

ROOT_DIR="$(cd "$(dirname "$0")" && pwd)"
SRC_DIR="${ROOT_DIR}/src"
BUILD_DIR="${ROOT_DIR}/build"

usage() {
  cat <<EOF
Usage:
  $0 --soc rk3588|rk3576 [options]

Options:
  --soc <rk3588|rk3576>     Target SoC (required unless auto-detect works)
  --type <Release|Debug>    Build type (default: Release)
  --target <demo|demo_zero_copy>  Binary name to run (default: demo)
  --run | --no-run          Run after build (default: --run)
  --clean | --no-clean      Clean build dir (default: --clean)
  -j, --jobs <N>            Parallel jobs (default: nproc)
  --native                  Enable -mcpu=native -mtune=native (safe for on-device build&run)
  -h, --help                Show this help

Examples:
  $0 --soc rk3588 --native
  $0 --soc rk3576 --type Release --target demo --no-run
EOF
}

die() { echo "ERROR: $*" >&2; exit 1; }

# Args parse
while [[ $# -gt 0 ]]; do
  case "$1" in
    --soc) TARGET_SOC="${2:-}"; shift 2;;
    --type) BUILD_TYPE="${2:-}"; shift 2;;
    --target) PROJECT_NAME="${2:-}"; shift 2;;
    --run) DO_RUN=1; shift;;
    --no-run) DO_RUN=0; shift;;
    --clean) DO_CLEAN=1; shift;;
    --no-clean) DO_CLEAN=0; shift;;
    -j|--jobs) JOBS="${2:-}"; shift 2;;
    --native) USE_NATIVE=1; shift;;
    -h|--help) usage; exit 0;;
    *) die "Unknown argument: $1";;
  esac
done

# Auto-detect SoC if not provided
if [[ -z "${TARGET_SOC}" ]]; then
  # /proc/device-tree/compatible is the most reliable when present
  if [[ -r /proc/device-tree/compatible ]]; then
    compat="$(tr -d '\0' </proc/device-tree/compatible | tr ' ' '\n')"
    if echo "${compat}" | grep -qi 'rk3588'; then
      TARGET_SOC="rk3588"
    elif echo "${compat}" | grep -qi 'rk3576'; then
      TARGET_SOC="rk3576"
    fi
  fi
fi

if [[ -z "${TARGET_SOC}" ]]; then
  # fallback: cpuinfo heuristic
  if grep -qi 'rk3588' /proc/cpuinfo 2>/dev/null; then
    TARGET_SOC="rk3588"
  elif grep -qi 'rk3576' /proc/cpuinfo 2>/dev/null; then
    TARGET_SOC="rk3576"
  else
    echo "WARN: Cannot auto-detect SoC. Defaulting to rk3588." >&2
    TARGET_SOC="rk3588"
  fi
fi

[[ "${TARGET_SOC}" == "rk3588" || "${TARGET_SOC}" == "rk3576" ]] || die "--soc must be rk3588 or rk3576"

# Toolchain: on-device native compile
export CC="gcc"
export CXX="g++"

# Flags
COMMON_CFLAGS="-DNDEBUG"
COMMON_CXXFLAGS="-DNDEBUG"

# Release/Debug base
if [[ "${BUILD_TYPE}" == "Release" ]]; then
  OPT_FLAGS="-O3"
else
  OPT_FLAGS="-O0 -g"
  COMMON_CFLAGS=""
  COMMON_CXXFLAGS=""
fi

# Optional native tuning (recommended for on-device build+run)
NATIVE_FLAGS=""
if [[ "${USE_NATIVE}" -eq 1 && "${BUILD_TYPE}" == "Release" ]]; then
  # gcc supports -mcpu=native for aarch64; if toolchain doesn't, it'll fail fast.
  NATIVE_FLAGS="-mcpu=native -mtune=native"
fi

C_FLAGS="${OPT_FLAGS} ${COMMON_CFLAGS} ${NATIVE_FLAGS}"
CXX_FLAGS="${OPT_FLAGS} ${COMMON_CXXFLAGS} ${NATIVE_FLAGS}"

# Print config
echo "==================================="
echo "PROJECT_NAME=${PROJECT_NAME}"
echo "SUBPROJECT_NAME=${SUBPROJECT_NAME}"
echo "TARGET_SOC=${TARGET_SOC}"
echo "TARGET_ARCH=${TARGET_ARCH}"
echo "BUILD_TYPE=${BUILD_TYPE}"
echo "CC=${CC}"
echo "CXX=${CXX}"
echo "USE_NATIVE=${USE_NATIVE}"
echo "JOBS=${JOBS}"
echo "BUILD_DIR=${BUILD_DIR}"
echo "==================================="

# Build
if [[ "${DO_CLEAN}" -eq 1 ]]; then
  rm -rf "${BUILD_DIR}"
fi
mkdir -p "${BUILD_DIR}"
cd "${BUILD_DIR}"

cmake "${SRC_DIR}" \
  -DTARGET_SOC="${TARGET_SOC}" \
  -DCMAKE_SYSTEM_NAME=Linux \
  -DCMAKE_SYSTEM_PROCESSOR="${TARGET_ARCH}" \
  -DCMAKE_BUILD_TYPE="${BUILD_TYPE}" \
  -DCMAKE_C_COMPILER="${CC}" \
  -DCMAKE_CXX_COMPILER="${CXX}" \
  -DCMAKE_C_FLAGS_RELEASE="${C_FLAGS}" \
  -DCMAKE_CXX_FLAGS_RELEASE="${CXX_FLAGS}" \
  -DCMAKE_C_FLAGS_DEBUG="${C_FLAGS}" \
  -DCMAKE_CXX_FLAGS_DEBUG="${CXX_FLAGS}"

cmake --build . --target "${PROJECT_NAME}" "${SUBPROJECT_NAME}" -j"${JOBS}"

cd "${ROOT_DIR}"

# Run
if [[ "${DO_RUN}" -eq 1 ]]; then
  BIN_PATH="${BUILD_DIR}/${PROJECT_NAME}"
  [[ -x "${BIN_PATH}" ]] || die "Binary not found or not executable: ${BIN_PATH}"
  "${BIN_PATH}"
fi
