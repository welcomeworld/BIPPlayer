cmake_minimum_required(VERSION 3.10.2)

project("bipplayer")
add_library(
        native-lib
        SHARED
        native-lib.cpp)

find_library( # Sets the name of the path variable.
        log-lib
        # Specifies the name of the NDK library that
        # you want CMake to locate.
        log)

set(DIR ${CMAKE_SOURCE_DIR}/../../../libs)
include_directories(${DIR}/include)
add_library(avcodec
        SHARED
        IMPORTED)
set_target_properties(avcodec
        PROPERTIES IMPORTED_LOCATION
        ${DIR}/arm64-v8a/libavcodec.so)

add_library(avdevice
        SHARED
        IMPORTED)
set_target_properties(avdevice
        PROPERTIES IMPORTED_LOCATION
        ${DIR}/arm64-v8a/libavdevice.so)
add_library(avfilter
        SHARED
        IMPORTED)
set_target_properties(avfilter
        PROPERTIES IMPORTED_LOCATION
        ${DIR}/arm64-v8a/libavfilter.so)
add_library(avformat
        SHARED
        IMPORTED)
set_target_properties(avformat
        PROPERTIES IMPORTED_LOCATION
        ${DIR}/arm64-v8a/libavformat.so)
add_library(avutil
        SHARED
        IMPORTED)
set_target_properties(avutil
        PROPERTIES IMPORTED_LOCATION
        ${DIR}/arm64-v8a/libavutil.so)
add_library(postproc
        SHARED
        IMPORTED)
set_target_properties(postproc
        PROPERTIES IMPORTED_LOCATION
        ${DIR}/arm64-v8a/libpostproc.so)
add_library(swresample
        SHARED
        IMPORTED)
set_target_properties(swresample
        PROPERTIES IMPORTED_LOCATION
        ${DIR}/arm64-v8a/libswresample.so)
add_library(swscale
        SHARED
        IMPORTED)
set_target_properties(swscale
        PROPERTIES IMPORTED_LOCATION
        ${DIR}/arm64-v8a/libswscale.so)

target_link_libraries( # Specifies the target library.
        native-lib
        avcodec
        avdevice
        avfilter
        avformat
        avutil
        postproc
        swresample
        swscale
        # Links the target library to the log library
        # included in the NDK.
        ${log-lib})