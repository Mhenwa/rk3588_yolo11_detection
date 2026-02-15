cmake_minimum_required(VERSION 3.15)

project(rknn_model_zoo_3rdparty_libs)

if (CMAKE_SYSTEM_NAME STREQUAL "Android")
    set (TARGET_LIB_ARCH ${CMAKE_ANDROID_ARCH_ABI})
else()
    if(CMAKE_SIZEOF_VOID_P EQUAL 8)
        set (TARGET_LIB_ARCH aarch64)
    else()
        set (TARGET_LIB_ARCH armhf)
    endif()
    if (CMAKE_C_COMPILER MATCHES "uclibc")
        set (TARGET_LIB_ARCH ${TARGET_LIB_ARCH}_uclibc)
    endif()
endif()

# stb image
set(STB_PATH ${CMAKE_CURRENT_SOURCE_DIR}/stb_image)
set(STB_INCLUDES ${STB_PATH} PARENT_SCOPE)

# jpeg turbo
set(JPEG_PATH ${CMAKE_CURRENT_SOURCE_DIR}/jpeg_turbo)
find_library(SYS_LIBJPEG
    NAMES turbojpeg
    HINTS
        /usr/lib
        /usr/lib64
        /lib
        /lib64
        /usr/local/lib
        /usr/local/lib64
)
find_path(SYS_LIBJPEG_INCLUDES
    NAMES turbojpeg.h
    HINTS
        /usr/include
        /usr/local/include
)
if(SYS_LIBJPEG AND SYS_LIBJPEG_INCLUDES)
    set(LIBJPEG ${SYS_LIBJPEG} PARENT_SCOPE)
    set(LIBJPEG_INCLUDES ${SYS_LIBJPEG_INCLUDES} PARENT_SCOPE)
    message(STATUS "Using system turbojpeg: ${SYS_LIBJPEG}")
else()
    set(LIBJPEG ${JPEG_PATH}/${CMAKE_SYSTEM_NAME}/${TARGET_LIB_ARCH}/libturbojpeg.a PARENT_SCOPE)
    set(LIBJPEG_INCLUDES ${JPEG_PATH}/include PARENT_SCOPE)
    message(STATUS "Using bundled turbojpeg: ${JPEG_PATH}/${CMAKE_SYSTEM_NAME}/${TARGET_LIB_ARCH}/libturbojpeg.a")
endif()

# rga
set(RGA_PATH ${CMAKE_CURRENT_SOURCE_DIR}/librga)
find_library(SYS_LIBRGA
    NAMES rga
    HINTS
        /usr/lib
        /usr/lib64
        /lib
        /lib64
        /usr/local/lib
        /usr/local/lib64
)
find_path(SYS_LIBRGA_INCLUDES
    NAMES im2d.h
    HINTS
        /usr/include
        /usr/local/include
    PATH_SUFFIXES
        rga
)
if(SYS_LIBRGA AND SYS_LIBRGA_INCLUDES)
    set(LIBRGA ${SYS_LIBRGA} PARENT_SCOPE)
    set(LIBRGA_INCLUDES ${SYS_LIBRGA_INCLUDES} PARENT_SCOPE)
    message(STATUS "Using system RGA: ${SYS_LIBRGA}")
else()
    set(LIBRGA ${RGA_PATH}/${CMAKE_SYSTEM_NAME}/${TARGET_LIB_ARCH}/librga.a PARENT_SCOPE)
    set(LIBRGA_INCLUDES ${RGA_PATH}/include PARENT_SCOPE)
    install(PROGRAMS ${RGA_PATH}/${CMAKE_SYSTEM_NAME}/${TARGET_LIB_ARCH}/librga.so DESTINATION lib)
    message(STATUS "Using bundled RGA: ${RGA_PATH}/${CMAKE_SYSTEM_NAME}/${TARGET_LIB_ARCH}/librga.a")
endif()

# timer
set(TIMER_PATH ${CMAKE_CURRENT_SOURCE_DIR}/timer)
set(LIBTIMER_INCLUDES ${TIMER_PATH} PARENT_SCOPE)

# libsndfile
set(LIBSNDFILE_PATH ${CMAKE_CURRENT_SOURCE_DIR}/libsndfile)
set(LIBSNDFILE_INCLUDES ${LIBSNDFILE_PATH}/include PARENT_SCOPE)
set(LIBSNDFILE ${LIBSNDFILE_PATH}/${CMAKE_SYSTEM_NAME}/${TARGET_LIB_ARCH}/libsndfile.a PARENT_SCOPE)
