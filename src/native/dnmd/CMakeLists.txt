cmake_minimum_required(VERSION 3.10)
project(dnmd
    LANGUAGES C CXX)

# Always include our public headers
include_directories(src/inc)

# Include the metadata API headers provided by CoreCLR
include_directories(${CMAKE_CURRENT_SOURCE_DIR}/../../coreclr/inc ${CMAKE_CURRENT_SOURCE_DIR}/../../coreclr/pal/prebuilt/inc)

if (CMAKE_C_COMPILER_ID STREQUAL "GNU")
  add_compile_options(-Wno-error=unknown-pragmas)
endif()

add_subdirectory(../dncp dncp)

add_subdirectory(src/)

option(DNMD_BUILD_TESTS "Build tests for dnmd" ON)
option(DNMD_INSTALL "Install dnmd targets" ON)

if (DNMD_BUILD_TESTS)
    enable_testing()

    add_subdirectory(test/)
endif()