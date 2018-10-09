cmake_minimum_required (VERSION 2.6)
project (egbbnn)
set (CMAKE_CXX_STANDARD 11)

#
# Add libraries
#

file(GLOB SOURCES "src/*.cpp")
add_library(egbbnn SHARED ${SOURCES})

#
# Options
#

option(TENSORFLOW  "Build with tensorflow lib." OFF)
option(TENSORFLOW_CC "Build with tensorflow_cc lib" OFF)
option(USE_SHARED "Build with shared tensorflow lib" ON)
option(TRT "Build with TensorRT backend" OFF)

#
# Tensorflow backend
#   option 1. Use tensorflow_cc library
#   option 2. Use manually built tensorflow library
#
if(TENSORFLOW OR TENSORFLOW_CC)

target_compile_definitions(egbbnn PRIVATE TENSORFLOW=1)

if(TENSORFLOW_CC)
    set(TF_DIR "/usr/local")
    set(TF_DIR_INC "${TF_DIR}/include/tensorflow")
    set(TF_DIR_LIB "${TF_DIR}/lib/tensorflow_cc")
else()
    set(TF_DIR "$ENV{HOME}/tensorflow")
    set(TF_DIR_INC "${TF_DIR}")
    set(TF_DIR_LIB "${TF_DIR}/bazel-bin/tensorflow")
endif()

target_include_directories(
    egbbnn PUBLIC
    "${TF_DIR_INC}"
    "${TF_DIR_INC}/tensorflow/contrib/makefile/downloads"
    "${TF_DIR_INC}/tensorflow/contrib/makefile/downloads/eigen"
    "${TF_DIR_INC}/tensorflow/contrib/makefile/downloads/gemmlowp"
    "${TF_DIR_INC}/tensorflow/contrib/makefile/downloads/nsync/public"
    "${TF_DIR_INC}/tensorflow/contrib/makefile/gen/protobuf-host/include"
)

if(USE_SHARED)
    target_include_directories(egbbnn PUBLIC "${TF_DIR_INC}/bazel-genfiles")

    find_library(TFCC tensorflow_cc HINTS "${TF_DIR_LIB}")
    target_link_libraries(egbbnn PUBLIC ${TFCC})
else()
    target_include_directories(
        egbbnn PUBLIC
        "${TF_DIR_INC}/tensorflow/contrib/makefile/gen/host_obj"
        "${TF_DIR_INC}/tensorflow/contrib/makefile/gen/proto"
        "${TF_DIR_INC}/tensorflow/contrib/makefile/gen/proto_text"
    )

    if(TENSORFLOW_CC)
        find_library(NSYNC nsync HINTS "${TF_DIR_LIB}")
        find_library(TFCORE tensorflow-core HINTS "${TF_DIR_LIB}")
    else()
        find_library(NSYNC nsync HINTS "${TF_DIR}/tensorflow/contrib/makefile/downloads/nsync/builds/default.linux.c++11")
        find_library(TFCORE tensorflow-core HINTS "${TF_DIR}/tensorflow/contrib/makefile/gen/lib")
    endif()
    target_link_libraries(egbbnn PUBLIC "${NSYNC}" )
    target_link_libraries(egbbnn PUBLIC "-Wl,--whole-archive ${TFCORE} -Wl,--no-whole-archive" )
endif()

if(TENSORFLOW_CC)
    find_library(PROTOBUF protobuf HINTS "${TF_DIR_LIB}")
    target_link_libraries(egbbnn PUBLIC "${PROTOBUF}" )
else()
    find_library(PROTOBUF protobuf HINTS "${TF_DIR}/tensorflow/contrib/makefile/gen/protobuf-host/lib")
    target_link_libraries(egbbnn PUBLIC "${PROTOBUF}" )
endif()

endif()

#
# TensorRT backend
#

if(TRT)
    set(TRT_DIR "$ENV{HOME}/TensorRT-5.0.0.10")
    set(CUDA_DIR "/usr/local/cuda")

    target_compile_definitions(egbbnn PRIVATE TRT=1)

    target_include_directories(
        egbbnn PUBLIC
        "${TRT_DIR}"
        "${CUDA_DIR}"
    )
    find_library(NVINFER nvinfer HINTS "${TRT_DIR}/lib")
    target_link_libraries(egbbnn PUBLIC "${NVINFER}" )
    find_library(NVPARSERS nvparsers HINTS "${TRT_DIR}/lib")
    target_link_libraries(egbbnn PUBLIC "${NVPARSERS}" )
endif()

