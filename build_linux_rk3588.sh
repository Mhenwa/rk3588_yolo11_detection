#!/bin/bash
set -e

# ====== 可配置参数 ======
TARGET_SOC=rk3588
TARGET_ARCH=aarch64
BUILD_TYPE=Release
PROJECT_NAME=demo

# ====== 编译器 ======
GCC_COMPILER=aarch64-linux-gnu
export CC=${GCC_COMPILER}-gcc
export CXX=${GCC_COMPILER}-g++

# ====== 路径 ======
ROOT_DIR=$(cd "$(dirname "$0")" && pwd)
BUILD_DIR=${ROOT_DIR}/build

echo "==================================="
echo "PROJECT_NAME=${PROJECT_NAME}"
echo "TARGET_SOC=${TARGET_SOC}"
echo "TARGET_ARCH=${TARGET_ARCH}"
echo "BUILD_TYPE=${BUILD_TYPE}"
echo "CC=${CC}"
echo "CXX=${CXX}"
echo "BUILD_DIR=${BUILD_DIR}"
echo "==================================="

rm -rf ${BUILD_DIR}

mkdir -p ${BUILD_DIR}

cd ${BUILD_DIR}

cmake ../src \
    -DTARGET_SOC=${TARGET_SOC} \
    -DCMAKE_SYSTEM_NAME=Linux \
    -DCMAKE_SYSTEM_PROCESSOR=${TARGET_ARCH} \
    -DCMAKE_BUILD_TYPE=Release \
    -DCMAKE_C_COMPILER=${CC} \
    -DCMAKE_CXX_COMPILER=${CXX} \
    -DCMAKE_C_FLAGS_RELEASE="-O3 -march=armv8.2-a -mtune=cortex-a76 -DNDEBUG" \
    -DCMAKE_CXX_FLAGS_RELEASE="-O3 -march=armv8.2-a -mtune=cortex-a76 -DNDEBUG"

make -j$(nproc)

cd ..
# 运行普通程序
./build/demo

# 运行zero_copy程序
# ./build/demo_zero_copy
