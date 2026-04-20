cmake_minimum_required(VERSION 3.16)
project(VeloxTradingEngine VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# Build options
option(BUILD_TESTS "Build tests" ON)
option(BUILD_BENCHMARKS "Build benchmarks" OFF)
option(CI_BUILD "CI build with reduced tests" OFF)
option(ENABLE_ASAN "Enable AddressSanitizer" OFF)
option(ENABLE_TSAN "Enable ThreadSanitizer" OFF)
option(ENABLE_COVERAGE "Enable code coverage" OFF)

# Fetch Google Benchmark only if building benchmarks
if(BUILD_BENCHMARKS)
    include(FetchContent)
    FetchContent_Declare(
        benchmark
        GIT_REPOSITORY https://github.com/google/benchmark.git
        GIT_TAG v1.8.3
    )
    set(BENCHMARK_ENABLE_TESTING OFF CACHE BOOL "" FORCE)
    FetchContent_MakeAvailable(benchmark)
endif()

# Compiler specific flags
if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
    set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -Wall -Wextra -Wpedantic -Wconversion -Wsign-conversion")
    
    if(CMAKE_BUILD_TYPE STREQUAL "Release")
        set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -O3 -march=native -mtune=native")
        set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -flto -ffast-math")
    endif()
    
    if(ENABLE_ASAN)
        set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -fsanitize=address -fno-omit-frame-pointer")
    endif()
    
    if(ENABLE_TSAN)
        set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -fsanitize=thread -fno-omit-frame-pointer")
    endif()
    
    if(ENABLE_COVERAGE)
        set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -fprofile-arcs -ftest-coverage")
    endif()
endif()

# CPU affinity for benchmarks (Linux only)
if(CMAKE_SYSTEM_NAME STREQUAL "Linux")
    set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -D_GNU_SOURCE")
endif()

# Include directories
include_directories(include)
include_directories(third_party/whirlpool/include)

# GoogleTest (must be added before any targets that use it)
set(INSTALL_GTEST OFF CACHE BOOL "" FORCE)
include(FetchContent)
if(EXISTS "${CMAKE_SOURCE_DIR}/third_party/googletest/CMakeLists.txt")
    add_subdirectory(third_party/googletest)
else()
    FetchContent_Declare(
        googletest
        GIT_REPOSITORY https://github.com/google/googletest.git
        GIT_TAG release-1.12.1
    )
    FetchContent_MakeAvailable(googletest)
endif()

# Temporarily disable benchmarks for whirlpool only
set(_SAVED_BUILD_BENCHMARKS ${BUILD_BENCHMARKS})
set(BUILD_BENCHMARKS OFF)
if(EXISTS "${CMAKE_SOURCE_DIR}/third_party/whirlpool/CMakeLists.txt")
    add_subdirectory(third_party/whirlpool)
else()
    # Fetch whirlpool if submodule not present (fallback)
    FetchContent_Declare(
        whirlpool
        GIT_REPOSITORY https://github.com/matthewyeo1/whirl-pool.git
        GIT_TAG master
    )
    FetchContent_MakeAvailable(whirlpool)
endif()
set(BUILD_BENCHMARKS ${_SAVED_BUILD_BENCHMARKS})

# Subdirectories
add_subdirectory(src)

if(BUILD_TESTS)
    add_subdirectory(tests)
endif()

if(BUILD_BENCHMARKS)
    add_subdirectory(benchmarks)
endif()

if(CI_BUILD)
    add_subdirectory(tools)
endif()

# Installation
install(DIRECTORY include/ DESTINATION include)