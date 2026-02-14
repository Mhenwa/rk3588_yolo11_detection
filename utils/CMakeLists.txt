cmake_minimum_required(VERSION 3.15)

add_library(fileutils STATIC
    file_utils.c
)
target_include_directories(fileutils PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_CURRENT_SOURCE_DIR}/../src/app/log
)

add_library(imagedrawing STATIC
    image_drawing.c
)
target_include_directories(imagedrawing PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_CURRENT_SOURCE_DIR}/../src/app/log
)

if(DISABLE_RGA AND NOT (TARGET_SOC STREQUAL "rv1106" OR TARGET_SOC STREQUAL "rv1103" OR TARGET_SOC STREQUAL "rv1103b"))
    add_definitions(-DDISABLE_RGA)
endif ()

# only RGA on rv1106 and rk3588 support handle
if (TARGET_SOC STREQUAL "rv1106" OR TARGET_SOC STREQUAL "rk3588")
    add_definitions(-DLIBRGA_IM2D_HANDLE)
endif()

add_library(imageutils STATIC
    image_utils.c
)

target_include_directories(imageutils PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_CURRENT_SOURCE_DIR}/../src/app/log
    ${STB_INCLUDES}
    ${LIBRGA_INCLUDES}
)

target_link_libraries(imageutils
    ${LIBRGA}
)

if (DISABLE_LIBJPEG)
    add_definitions(-DDISABLE_LIBJPEG)
else()
    target_link_libraries(imageutils
        ${LIBJPEG}
    )
    target_include_directories(imageutils PUBLIC
        ${LIBJPEG_INCLUDES}
    )
endif()
