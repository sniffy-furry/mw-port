# Android native build for the NFS MW port.
#
# HONESTY NOTE: this file was written from documented raylib + Android NDK
# patterns, but never actually build-tested (the sandbox that produced this
# project has no network access, so raylib could not be fetched locally).
# GitHub Actions -- which does have network access -- is the first real
# build attempt. Expect this to need a debugging pass; the parts most
# likely to need adjustment are the raylib CMake Android detection and the
# native_app_glue path below.

cmake_minimum_required(VERSION 3.22)
project(nfsmw_port_android LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

include(FetchContent)
set(BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
set(BUILD_GAMES OFF CACHE BOOL "" FORCE)
set(BUILD_SHARED_LIBS OFF CACHE BOOL "" FORCE)

FetchContent_Declare(
    raylib
    GIT_REPOSITORY https://github.com/raysan5/raylib.git
    GIT_TAG        5.0
)
FetchContent_MakeAvailable(raylib)

# native_app_glue ships with the NDK itself.
add_library(native_app_glue STATIC
    ${ANDROID_NDK}/sources/android/native_app_glue/android_native_app_glue.c)
target_include_directories(native_app_glue PUBLIC
    ${ANDROID_NDK}/sources/android/native_app_glue)

# Repo root is 5 directories up from android/app/src/main/cpp/
set(REPO_ROOT ${CMAKE_CURRENT_SOURCE_DIR}/../../../../..)

add_library(nfsmw_port SHARED
    ${REPO_ROOT}/src/main.cpp)

target_include_directories(nfsmw_port PRIVATE ${REPO_ROOT}/src)
target_link_libraries(nfsmw_port PRIVATE raylib native_app_glue log android EGL GLESv2 OpenSLES)

# Required so the dynamic linker can find NativeActivity's entry point.
target_link_options(nfsmw_port PRIVATE "-u" "ANativeActivity_onCreate")
