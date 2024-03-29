cmake_minimum_required(VERSION 3.10.2)

project("bipplayer")
aux_source_directory(. DIR_SRC)
aux_source_directory(./soundtouch DIR_SRC_SOUND_TOUCH)
add_library(
        native-lib
        SHARED
        ${DIR_SRC} ${DIR_SRC_SOUND_TOUCH})

find_library( # Sets the name of the path variable.
        log-lib
        # Specifies the name of the NDK library that
        # you want CMake to locate.
        log)

set(DIR ${CMAKE_SOURCE_DIR}/../../../libs)
include_directories(${DIR}/include)
include_directories(${DIR}/include/soundtouch)
add_library(avcodec
        SHARED
        IMPORTED)
set_target_properties(avcodec
        PROPERTIES IMPORTED_LOCATION
        ${DIR}/${CMAKE_ANDROID_ARCH_ABI}/libavcodec.so)

add_library(avdevice
        SHARED
        IMPORTED)
set_target_properties(avdevice
        PROPERTIES IMPORTED_LOCATION
        ${DIR}/${CMAKE_ANDROID_ARCH_ABI}/libavdevice.so)
add_library(avfilter
        SHARED
        IMPORTED)
set_target_properties(avfilter
        PROPERTIES IMPORTED_LOCATION
        ${DIR}/${CMAKE_ANDROID_ARCH_ABI}/libavfilter.so)
add_library(avformat
        SHARED
        IMPORTED)
set_target_properties(avformat
        PROPERTIES IMPORTED_LOCATION
        ${DIR}/${CMAKE_ANDROID_ARCH_ABI}/libavformat.so)
add_library(avutil
        SHARED
        IMPORTED)
set_target_properties(avutil
        PROPERTIES IMPORTED_LOCATION
        ${DIR}/${CMAKE_ANDROID_ARCH_ABI}/libavutil.so)
add_library(postproc
        SHARED
        IMPORTED)
set_target_properties(postproc
        PROPERTIES IMPORTED_LOCATION
        ${DIR}/${CMAKE_ANDROID_ARCH_ABI}/libpostproc.so)
add_library(swresample
        SHARED
        IMPORTED)
set_target_properties(swresample
        PROPERTIES IMPORTED_LOCATION
        ${DIR}/${CMAKE_ANDROID_ARCH_ABI}/libswresample.so)
add_library(swscale
        SHARED
        IMPORTED)
set_target_properties(swscale
        PROPERTIES IMPORTED_LOCATION
        ${DIR}/${CMAKE_ANDROID_ARCH_ABI}/libswscale.so)
add_library(yuv
        SHARED
        IMPORTED)
set_target_properties(yuv
        PROPERTIES IMPORTED_LOCATION
        ${DIR}/${CMAKE_ANDROID_ARCH_ABI}/libyuv.so)

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
        android
        OpenSLES
        # Links the target library to the log library
        # included in the NDK.
        ${log-lib}
        yuv)