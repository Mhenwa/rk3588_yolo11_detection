cmake_minimum_required(VERSION 3.10)
project(demo)

set(CMAKE_CXX_STANDARD 14)

set(RKNNRT_FALLBACK_DIR "${CMAKE_CURRENT_SOURCE_DIR}/../3rdparty/rknpu2/${CMAKE_SYSTEM_NAME}/${CMAKE_SYSTEM_PROCESSOR}")
if(NOT EXISTS "${RKNNRT_FALLBACK_DIR}/librknnrt.so")
    set(RKNNRT_FALLBACK_DIR "${CMAKE_CURRENT_SOURCE_DIR}/../3rdparty/rknpu2/${CMAKE_SYSTEM_NAME}/aarch64")
endif()

find_library(RKNNRT_LIB
    NAMES rknnrt
    HINTS
        /usr/lib
        /usr/lib64
        /lib
        /lib64
        /usr/local/lib
        /usr/local/lib64
        "${RKNNRT_FALLBACK_DIR}"
)
if(NOT RKNNRT_LIB)
    message(FATAL_ERROR "Cannot find librknnrt.so in system paths or bundled 3rdparty path")
endif()

find_path(RKNNRT_INCLUDE_DIR
    NAMES rknn_api.h
    HINTS
        /usr/include
        /usr/local/include
        ${CMAKE_CURRENT_SOURCE_DIR}/../3rdparty/rknpu2/include
)
if(NOT RKNNRT_INCLUDE_DIR)
    message(FATAL_ERROR "Cannot find rknn_api.h in system paths or bundled 3rdparty path")
endif()

set(LIBRKNNRT_INCLUDES ${RKNNRT_INCLUDE_DIR})
message(STATUS "Using RKNN runtime library: ${RKNNRT_LIB}")
message(STATUS "Using RKNN include path: ${RKNNRT_INCLUDE_DIR}")

add_library(rknnrt SHARED IMPORTED GLOBAL)

set_target_properties(rknnrt PROPERTIES
    IMPORTED_LOCATION ${RKNNRT_LIB}
    INTERFACE_INCLUDE_DIRECTORIES ${RKNNRT_INCLUDE_DIR}
)

# ===== OpenCV =====
find_package(OpenCV REQUIRED)

# ===== easyeai api / gtk / gstreamer =====
set(EASYEAI_API_ROOT "${CMAKE_CURRENT_SOURCE_DIR}/../3rdparty/easyeai-api")
if(NOT EXISTS "${EASYEAI_API_ROOT}")
    message(FATAL_ERROR "Cannot find easyeai-api at: ${EASYEAI_API_ROOT}")
endif()

include("${EASYEAI_API_ROOT}/base64/api.cmake")
include("${EASYEAI_API_ROOT}/system_opt/api.cmake")
include("${EASYEAI_API_ROOT}/gst_opt/api.cmake")

set(EASYEAI_API_INCLUDES
    ${BASE64_INCLUDE_DIRS}
    ${SYSOPT_INCLUDE_DIRS}
    ${GSTOPT_INCLUDE_DIRS}
)

set(EASYEAI_API_SRCS
    ${BASE64_SOURCE_DIRS}
    ${SYSOPT_SOURCE_DIRS}
)

set(EASYEAI_API_LIBS
    ${BASE64_LIBS}
    ${SYSOPT_LIBS}
    ${GSTOPT_LIBS}
)

find_package(PkgConfig REQUIRED)
pkg_check_modules(GTK3 REQUIRED gtk+-3.0)

# # rk3576: temporary workaround, force CPU preprocessing path.
# if (TARGET_SOC STREQUAL "rk3576")
#     set(DISABLE_RGA ON CACHE BOOL "Disable RGA preprocessing on rk3576" FORCE)
#     set(FORCE_CPU_PREPROCESS ON)
#     message(WARNING "TARGET_SOC=rk3576: force CPU preprocessing (DISABLE_RGA)")
# endif()

# ===== Third-party =====
add_subdirectory(../3rdparty 3rdparty.out)

# 普通版本
add_executable(demo
    app/config/app_config.cc
    app/app_controller.cpp
    app/main.cpp
    app/utils/app_utils.cc
    core/log/app_log.cc
    core/utils/rga_debug_gate.cc
    core/queue/result_queue.cc
    core/pool/worker_pool.cc
    core/pool/infer_worker.cc
    core/pipeline/frame_pipeline.cc
    modules/source/file_source.cc
    modules/source/rtsp_source.cc
    modules/source/capturer.cpp
    modules/source/decChannel.cpp
    modules/source/usb_cam_source.cc
    modules/source/mipi_source.cc
    modules/source/source_factory.cc
    modules/compositor/grid_compositor.cpp
    modules/display/display.cpp
    modules/display/display_node.cc
    modules/decode/decode_node.cc
    modules/preprocess/preprocess_node.cc
    modules/preprocess/image_utils.c
    modules/inference/infer_node.cc
    modules/inference/infer_context.cc
    modules/postprocess/postprocess_node.cc
    ../3rdparty/allocator/dma/dma_alloc.cpp
    ${EASYEAI_API_SRCS}
)

target_include_directories(demo PRIVATE
    ${LIBRKNNRT_INCLUDES}
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_CURRENT_SOURCE_DIR}/app/config
    ${CMAKE_CURRENT_SOURCE_DIR}/app/utils
    ${CMAKE_CURRENT_SOURCE_DIR}/core
    ${CMAKE_CURRENT_SOURCE_DIR}/core/types
    ${CMAKE_CURRENT_SOURCE_DIR}/core/queue
    ${CMAKE_CURRENT_SOURCE_DIR}/core/pool
    ${CMAKE_CURRENT_SOURCE_DIR}/core/pipeline
    ${CMAKE_CURRENT_SOURCE_DIR}/core/log
    ${CMAKE_CURRENT_SOURCE_DIR}/modules
    ${CMAKE_CURRENT_SOURCE_DIR}/modules/source
    ${CMAKE_CURRENT_SOURCE_DIR}/modules/compositor
    ${CMAKE_CURRENT_SOURCE_DIR}/modules/display
    ${CMAKE_CURRENT_SOURCE_DIR}/modules/decode
    ${CMAKE_CURRENT_SOURCE_DIR}/modules/preprocess
    ${CMAKE_CURRENT_SOURCE_DIR}/modules/inference
    ${CMAKE_CURRENT_SOURCE_DIR}/modules/postprocess
    ${CMAKE_CURRENT_SOURCE_DIR}/app
    ${CMAKE_CURRENT_SOURCE_DIR}/app/config
    ${CMAKE_CURRENT_SOURCE_DIR}/../3rdparty/json/include
    ${CMAKE_CURRENT_SOURCE_DIR}/../3rdparty/allocator/dma
    ${CMAKE_CURRENT_SOURCE_DIR}/../3rdparty/spdlog/include
    ${EASYEAI_API_INCLUDES}
    ${GTK3_INCLUDE_DIRS}
    ${LIBJPEG_INCLUDES}
    ${LIBRGA_INCLUDES}
    ${OpenCV_INCLUDE_DIRS}
)

target_link_directories(demo PRIVATE
    ${GTK3_LIBRARY_DIRS}
)

target_compile_options(demo PRIVATE
    ${GTK3_CFLAGS_OTHER}
)

if (FORCE_CPU_PREPROCESS)
    target_compile_definitions(demo PRIVATE DISABLE_RGA)
endif()
if (TARGET_SOC STREQUAL "rk3588")
    target_compile_definitions(demo PRIVATE LIBRGA_IM2D_HANDLE)
endif()
if (TARGET_SOC STREQUAL "rk3576")
    target_compile_definitions(demo PRIVATE TARGET_SOC_RK3576)
endif()

target_link_libraries(demo
    PRIVATE
    ${OpenCV_LIBS}
    ${LIBJPEG}
    ${LIBRGA}
    ${GTK3_LIBRARIES}
    ${EASYEAI_API_LIBS}
    rknnrt
    dl
    Threads::Threads
)


# 
# # zero-copy 
# add_executable(demo_zero_copy
#     app/config/app_config.cc
#     app/modes/mode_image.cc
#     app/modes/mode_video.cc
#     app/modes/mode_camera.cc
#     app/modes/mode_rtsp.cc
#     app/app_utils.cc
#     app/app_worker.cc
#     app/worker/worker_pool.cc
#     app/pipeline/frame_pipeline.cc
#     app/runtime/video_camera_runtime.cc
#     main.cc
#     modules/inference/infer_context.cc
# )

# target_compile_definitions(demo_zero_copy PRIVATE ZERO_COPY)

# target_include_directories(demo_zero_copy PRIVATE
#     ${CMAKE_CURRENT_SOURCE_DIR}
#     ${CMAKE_CURRENT_SOURCE_DIR}/app
#     ${CMAKE_CURRENT_SOURCE_DIR}/app/config
#     ${CMAKE_CURRENT_SOURCE_DIR}/app/pipeline
#     ${CMAKE_CURRENT_SOURCE_DIR}/app/runtime
#     ${CMAKE_CURRENT_SOURCE_DIR}/modules/postprocess
#     ${CMAKE_CURRENT_SOURCE_DIR}/modules/source
#     ${CMAKE_CURRENT_SOURCE_DIR}/modules/inference
#     ${LIBRKNNRT_INCLUDES}
#     ${LIBRGA_INCLUDES}
#     ${OpenCV_INCLUDE_DIRS}
# )

# target_link_libraries(demo_zero_copy
#     ${OpenCV_LIBS}
#     ${LIBRGA}
#     ${LIBRKNNRT}
#     dl
#     Threads::Threads
# )

# ===== pthread =====
set(THREADS_PREFER_PTHREAD_FLAG ON)
find_package(Threads REQUIRED)
