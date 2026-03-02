#!/usr/bin/env bash
set -euo pipefail

# Defaults
TARGET_SOC=""
TARGET_ARCH="aarch64"
BUILD_TYPE="Release"
PROJECT_NAME="demo"
DO_RUN=1
DO_CLEAN=1
JOBS=4
USE_NATIVE=0
ENABLE_ASAN=0
ENABLE_UBSAN=0
ENABLE_TSAN=0
ENABLE_DEBUG_SYMBOLS=0
ENABLE_MALLOC_CHECK=0
DISABLE_PREPROCESS_RGA=0
DISABLE_DISPLAY_RGA=0
ENABLE_RGA_GLOBAL_LOCK=0
ENABLE_RGA_GUARD_CHECK=0

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
  --asan                    Enable AddressSanitizer (for heap/UAF/double-free定位)
  --ubsan                   Enable UndefinedBehaviorSanitizer
  --tsan                    Enable ThreadSanitizer (for data-race/并发内存破坏定位)
  --debug-symbols           Keep current optimization level, but add debug symbols (-g3)
  --malloc-check            Enable glibc malloc checks at runtime (MALLOC_CHECK_=3)
  --disable-preprocess-rga  Force preprocess path to CPU (DISABLE_PREPROCESS_RGA=1)
  --disable-display-rga     Disable RGA display compositor, use OpenCV multi-window show
  --rga-global-lock         Serialize RGA calls across modules (RGA_GLOBAL_LOCK=1)
  --rga-guard-check         Enable guard-canary checks around key RGA buffers
  -h, --help                Show this help

Examples:
  $0 --soc rk3588 --native
  $0 --soc rk3576 --type Release --target demo --no-run
  $0 --soc rk3588 --type Debug --asan --ubsan --malloc-check
  $0 --soc rk3576 --type Debug --disable-preprocess-rga
  $0 --soc rk3576 --type Debug --disable-display-rga
  $0 --soc rk3576 --type Debug --rga-global-lock
  $0 --soc rk3576 --type Debug --rga-guard-check
  $0 --soc rk3576 --type Release --tsan --no-run
  $0 --soc rk3576 --type Release --debug-symbols --no-run
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
    --asan) ENABLE_ASAN=1; shift;;
    --ubsan) ENABLE_UBSAN=1; shift;;
    --tsan) ENABLE_TSAN=1; shift;;
    --debug-symbols) ENABLE_DEBUG_SYMBOLS=1; shift;;
    --malloc-check) ENABLE_MALLOC_CHECK=1; shift;;
    --disable-preprocess-rga) DISABLE_PREPROCESS_RGA=1; shift;;
    --disable-display-rga) DISABLE_DISPLAY_RGA=1; shift;;
    --rga-global-lock) ENABLE_RGA_GLOBAL_LOCK=1; shift;;
    --rga-guard-check) ENABLE_RGA_GUARD_CHECK=1; shift;;
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

# Sanitizer compatibility checks
if [[ "${ENABLE_TSAN}" -eq 1 && "${ENABLE_ASAN}" -eq 1 ]]; then
  die "--tsan cannot be combined with --asan"
fi

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

SAN_FLAGS=""
if [[ "${ENABLE_ASAN}" -eq 1 || "${ENABLE_UBSAN}" -eq 1 || "${ENABLE_TSAN}" -eq 1 ]]; then
  SAN_FLAGS="-fno-omit-frame-pointer -g3"
  if [[ "${ENABLE_TSAN}" -eq 1 ]]; then
    SAN_FLAGS="${SAN_FLAGS} -fsanitize=thread"
  fi
  if [[ "${ENABLE_ASAN}" -eq 1 ]]; then
    SAN_FLAGS="${SAN_FLAGS} -fsanitize=address"
  fi
  if [[ "${ENABLE_UBSAN}" -eq 1 ]]; then
    SAN_FLAGS="${SAN_FLAGS} -fsanitize=undefined"
  fi
  if [[ "${BUILD_TYPE}" == "Release" ]]; then
    OPT_FLAGS="-O1"
  fi
fi

DBG_FLAGS=""
if [[ "${ENABLE_DEBUG_SYMBOLS}" -eq 1 ]]; then
  DBG_FLAGS="-g3 -fno-omit-frame-pointer"
fi

C_FLAGS="${OPT_FLAGS} ${COMMON_CFLAGS} ${NATIVE_FLAGS} ${SAN_FLAGS} ${DBG_FLAGS}"
CXX_FLAGS="${OPT_FLAGS} ${COMMON_CXXFLAGS} ${NATIVE_FLAGS} ${SAN_FLAGS} ${DBG_FLAGS}"
LD_FLAGS="${SAN_FLAGS}"

# Print config
echo "==================================="
echo "PROJECT_NAME=${PROJECT_NAME}"
echo "TARGET_SOC=${TARGET_SOC}"
echo "TARGET_ARCH=${TARGET_ARCH}"
echo "BUILD_TYPE=${BUILD_TYPE}"
echo "CC=${CC}"
echo "CXX=${CXX}"
echo "USE_NATIVE=${USE_NATIVE}"
echo "ENABLE_ASAN=${ENABLE_ASAN}"
echo "ENABLE_UBSAN=${ENABLE_UBSAN}"
echo "ENABLE_TSAN=${ENABLE_TSAN}"
echo "ENABLE_DEBUG_SYMBOLS=${ENABLE_DEBUG_SYMBOLS}"
echo "ENABLE_MALLOC_CHECK=${ENABLE_MALLOC_CHECK}"
echo "DISABLE_PREPROCESS_RGA=${DISABLE_PREPROCESS_RGA}"
echo "DISABLE_DISPLAY_RGA=${DISABLE_DISPLAY_RGA}"
echo "ENABLE_RGA_GLOBAL_LOCK=${ENABLE_RGA_GLOBAL_LOCK}"
echo "ENABLE_RGA_GUARD_CHECK=${ENABLE_RGA_GUARD_CHECK}"
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
  -DCMAKE_CXX_FLAGS_DEBUG="${CXX_FLAGS}" \
  -DCMAKE_EXE_LINKER_FLAGS="${LD_FLAGS}"

cmake --build . --target "${PROJECT_NAME}" -j"${JOBS}"

cd "${ROOT_DIR}"

# Run
if [[ "${DO_RUN}" -eq 1 ]]; then
  BIN_PATH="${BUILD_DIR}/${PROJECT_NAME}"
  [[ -x "${BIN_PATH}" ]] || die "Binary not found or not executable: ${BIN_PATH}"
  RUN_ENV=()
  if [[ "${ENABLE_ASAN}" -eq 1 ]]; then
    RUN_ENV+=("ASAN_OPTIONS=abort_on_error=1:symbolize=1:detect_leaks=0:fast_unwind_on_malloc=0")
  fi
  if [[ "${ENABLE_UBSAN}" -eq 1 ]]; then
    RUN_ENV+=("UBSAN_OPTIONS=print_stacktrace=1:halt_on_error=1")
  fi
  if [[ "${ENABLE_TSAN}" -eq 1 ]]; then
    TSAN_SUPP_FILE="${ROOT_DIR}/tools/tsan.supp"
    TSAN_BASE_OPTIONS="halt_on_error=1:history_size=7:second_deadlock_stack=1:ignore_noninstrumented_modules=1"
    if [[ -f "${TSAN_SUPP_FILE}" ]]; then
      TSAN_BASE_OPTIONS="${TSAN_BASE_OPTIONS}:suppressions=${TSAN_SUPP_FILE}"
    fi
    RUN_ENV+=("TSAN_OPTIONS=${TSAN_BASE_OPTIONS}")
  fi
  if [[ "${ENABLE_MALLOC_CHECK}" -eq 1 ]]; then
    RUN_ENV+=("MALLOC_CHECK_=3" "MALLOC_PERTURB_=165")
  fi
  if [[ "${DISABLE_PREPROCESS_RGA}" -eq 1 ]]; then
    RUN_ENV+=("DISABLE_PREPROCESS_RGA=1")
  fi
  if [[ "${DISABLE_DISPLAY_RGA}" -eq 1 ]]; then
    RUN_ENV+=("DISABLE_DISPLAY_RGA=1")
  fi
  if [[ "${ENABLE_RGA_GLOBAL_LOCK}" -eq 1 ]]; then
    RUN_ENV+=("RGA_GLOBAL_LOCK=1")
  fi
  if [[ "${ENABLE_RGA_GUARD_CHECK}" -eq 1 ]]; then
    RUN_ENV+=("RGA_GUARD_CHECK=1")
  fi

  if [[ "${#RUN_ENV[@]}" -gt 0 ]]; then
    env "${RUN_ENV[@]}" "${BIN_PATH}"
  else
    "${BIN_PATH}"
  fi
fi
