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

# ===== Third-party & utils =====
add_subdirectory(../3rdparty 3rdparty.out)
add_subdirectory(../utils utils.out)

# 普通版本
add_executable(demo
    app/config/app_config.cc
    app/modes/mode_image.cc
    app/modes/mode_video.cc
    app/modes/mode_camera.cc
    app/log/app_log.cc
    app/utils/app_utils.cc
    app/worker/app_worker.cc
    app/worker/worker_pool.cc
    app/pipeline/frame_pipeline.cc
    app/runtime/video_camera_runtime.cc
    main.cc
    postprocess/postprocess.cc
    v4l2_camera/v4l2_camera.cpp
    rknpu2/yolo11.cc
    ../3rdparty/allocator/dma/dma_alloc.cpp
)

target_include_directories(demo PRIVATE
    ${LIBRKNNRT_INCLUDES}
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_CURRENT_SOURCE_DIR}/app
    ${CMAKE_CURRENT_SOURCE_DIR}/app/modes
    ${CMAKE_CURRENT_SOURCE_DIR}/app/config
    ${CMAKE_CURRENT_SOURCE_DIR}/app/pipeline
    ${CMAKE_CURRENT_SOURCE_DIR}/app/runtime
    ${CMAKE_CURRENT_SOURCE_DIR}/app/utils
    ${CMAKE_CURRENT_SOURCE_DIR}/app/worker
    ${CMAKE_CURRENT_SOURCE_DIR}/app/log
    ${CMAKE_CURRENT_SOURCE_DIR}/postprocess
    ${CMAKE_CURRENT_SOURCE_DIR}/v4l2_camera
    ${CMAKE_CURRENT_SOURCE_DIR}/rknpu2/
    ${CMAKE_CURRENT_SOURCE_DIR}/../3rdparty/json/include
    ${CMAKE_CURRENT_SOURCE_DIR}/../3rdparty/allocator/dma
    ${CMAKE_CURRENT_SOURCE_DIR}/../3rdparty/spdlog/include
    ${LIBJPEG_INCLUDES}
    ${LIBRGA_INCLUDES}
    ${OpenCV_INCLUDE_DIRS}
)

target_compile_definitions(demo PRIVATE SPDLOG_HEADER_ONLY)

target_link_libraries(demo
    PRIVATE
    ${OpenCV_LIBS}
    ${LIBJPEG}
    imageutils
    fileutils
    imagedrawing
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
#     app/app_utils.cc
#     app/app_worker.cc
#     app/worker/worker_pool.cc
#     app/pipeline/frame_pipeline.cc
#     app/runtime/video_camera_runtime.cc
#     main.cc
#     postprocess/postprocess.cc
#     v4l2_camera/v4l2_camera.cpp
#     rknpu2/yolo11_zero_copy.cc
# )

# target_compile_definitions(demo_zero_copy PRIVATE ZERO_COPY)

# target_include_directories(demo_zero_copy PRIVATE
#     ${CMAKE_CURRENT_SOURCE_DIR}
#     ${CMAKE_CURRENT_SOURCE_DIR}/app
#     ${CMAKE_CURRENT_SOURCE_DIR}/app/config
#     ${CMAKE_CURRENT_SOURCE_DIR}/app/pipeline
#     ${CMAKE_CURRENT_SOURCE_DIR}/app/runtime
#     ${CMAKE_CURRENT_SOURCE_DIR}/postprocess
#     ${CMAKE_CURRENT_SOURCE_DIR}/v4l2_camera
#     ${CMAKE_CURRENT_SOURCE_DIR}/rknpu2
#     ${LIBRKNNRT_INCLUDES}
#     ${LIBRGA_INCLUDES}
#     ${OpenCV_INCLUDE_DIRS}
# )

# target_link_libraries(demo_zero_copy
#     ${OpenCV_LIBS}
#     imageutils
#     fileutils
#     imagedrawing
#     ${LIBRKNNRT}
#     dl
#     Threads::Threads
# )

# ===== pthread =====
set(THREADS_PREFER_PTHREAD_FLAG ON)
find_package(Threads REQUIRED)

enable_testing()

add_executable(app_config_tests
    tests/test_app_config.cc
    app/config/app_config.cc
)

target_include_directories(app_config_tests PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_CURRENT_SOURCE_DIR}/app
    ${CMAKE_CURRENT_SOURCE_DIR}/app/config
    ${CMAKE_CURRENT_SOURCE_DIR}/../3rdparty/json/include
)

add_test(NAME app_config_tests COMMAND app_config_tests)
