cmake_minimum_required(VERSION 3.16)
project(nfsmw_port LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

include(FetchContent)

# raylib -- fetched at configure time. Requires internet access, which CI
# runners have (this repo's sandbox build environment does not, so this
# step is untested locally -- GitHub Actions is the first real build).
set(BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
set(BUILD_GAMES OFF CACHE BOOL "" FORCE)
FetchContent_Declare(
    raylib
    GIT_REPOSITORY https://github.com/raysan5/raylib.git
    GIT_TAG        5.0
)
FetchContent_MakeAvailable(raylib)

add_executable(nfsmw_port
    src/main.cpp
)
target_link_libraries(nfsmw_port PRIVATE raylib)
target_include_directories(nfsmw_port PRIVATE src)

if (APPLE)
    target_link_libraries(nfsmw_port PRIVATE "-framework IOKit" "-framework Cocoa" "-framework OpenGL")
endif()

# --- standalone parser test (no raylib dependency, mirrors tests/test_main.cpp) ---
add_executable(nfsmw_tests EXCLUDE_FROM_ALL tests/test_main.cpp)
target_include_directories(nfsmw_tests PRIVATE src)
