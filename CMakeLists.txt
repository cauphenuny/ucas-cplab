cmake_minimum_required(VERSION 3.5)

project(Compiler LANGUAGES CXX)

set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(THREADS_PREFER_PTHREAD_FLAG ON)

include_directories(deps/antlr4-runtime/)
include_directories(grammar)
include_directories(src)
include_directories(deps)

# Project sources excluding tests and main.cpp
FILE(GLOB_RECURSE ALL_SOURCES "src/*.cpp" "grammar/*.cpp")

# Filter out tests and main.cpp from the core library sources
set(LIB_SOURCES ${ALL_SOURCES})
list(FILTER LIB_SOURCES EXCLUDE REGEX "src/tests/.*\\.cpp")
list(FILTER LIB_SOURCES EXCLUDE REGEX "src/main\\.cpp$") # Match end of string

# ANTLR Lib
FILE(GLOB_RECURSE ANTLR_LIB_FILES "deps/antlr4-runtime/*.cpp")

# add_definitions(-w -g)
add_definitions(-g -Wall) # Do not ignore warnings!
add_library(antlr STATIC ${ANTLR_LIB_FILES})

# Compiler Core Library
if(LIB_SOURCES)
    add_library(compiler_core STATIC ${LIB_SOURCES})
else()
    # Some older CMake versions error when given an empty source list.
    # Create a tiny dummy source file in the build dir so the target is valid.
    file(WRITE ${CMAKE_CURRENT_BINARY_DIR}/empty_compiler_core.cpp "// dummy source for compiler_core\n")
    add_library(compiler_core STATIC ${CMAKE_CURRENT_BINARY_DIR}/empty_compiler_core.cpp)
endif()

# Main executable
add_executable(compiler src/main.cpp)
find_package(Threads REQUIRED)
target_link_libraries(compiler compiler_core antlr Threads::Threads)

# Add targets for each test in src/tests
FILE(GLOB TEST_FILES "src/tests/*.cpp")
foreach(TEST_SOURCE ${TEST_FILES})
    get_filename_component(TEST_NAME ${TEST_SOURCE} NAME_WE)
    add_executable(${TEST_NAME} ${TEST_SOURCE})
    target_link_libraries(${TEST_NAME} compiler_core antlr Threads::Threads)
endforeach()
