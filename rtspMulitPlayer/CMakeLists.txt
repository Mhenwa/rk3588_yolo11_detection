cmake_minimum_required(VERSION 3.10)
project(rtspMulitPlayer LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 14)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

add_definitions(-Wall -Wpointer-arith)

# ======================== easyeai_api =========================
set(EASYEAI_API_ROOT "${CMAKE_CURRENT_SOURCE_DIR}/../3rdparty/easyeai-api")
if(NOT EXISTS "${EASYEAI_API_ROOT}")
    message(FATAL_ERROR "Cannot find easyeai-api at: ${EASYEAI_API_ROOT}")
endif()

include("${EASYEAI_API_ROOT}/base64/api.cmake")
include("${EASYEAI_API_ROOT}/system_opt/api.cmake")
include("${EASYEAI_API_ROOT}/gst_opt/api.cmake")

set(api_inc
    ${BASE64_INCLUDE_DIRS}
    ${SYSOPT_INCLUDE_DIRS}
    ${GSTOPT_INCLUDE_DIRS}
)

set(api_srcs
    ${BASE64_SOURCE_DIRS}
    ${SYSOPT_SOURCE_DIRS}
)

set(sysLib_list
    ${BASE64_LIBS}
    ${SYSOPT_LIBS}
    ${GSTOPT_LIBS}
)
# ======================== easyeai_api =========================

find_package(PkgConfig REQUIRED)
pkg_check_modules(GTK3 REQUIRED gtk+-3.0)

set(project_srcs
    ${CMAKE_CURRENT_SOURCE_DIR}/main.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/capturer/capturer.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/capturer/decChannel.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/analyzer/analyzer.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/analyzer/algoProcess.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/player/display.cpp
)

add_executable(rtspMulitPlayer ${project_srcs} ${api_srcs})

target_include_directories(rtspMulitPlayer PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${GTK3_INCLUDE_DIRS}
    ${api_inc}
)

target_link_directories(rtspMulitPlayer PRIVATE
    ${GTK3_LIBRARY_DIRS}
)

target_compile_options(rtspMulitPlayer PRIVATE
    ${GTK3_CFLAGS_OTHER}
)

target_link_libraries(rtspMulitPlayer PRIVATE
    rga
    ${GTK3_LIBRARIES}
    ${sysLib_list}
)
