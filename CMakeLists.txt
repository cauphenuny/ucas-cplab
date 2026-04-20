cmake_minimum_required(VERSION 3.5)

project(Compiler LANGUAGES CXX)

set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

find_program(JAVA_EXECUTABLE java)
if(NOT JAVA_EXECUTABLE)
    message(FATAL_ERROR "Java runtime not found. It is required to run ANTLR.")
endif()

set(ANTLR_JAR "${CMAKE_CURRENT_SOURCE_DIR}/deps/antlr-4.13.1-complete.jar")
if(NOT EXISTS ${ANTLR_JAR})
    message(FATAL_ERROR "ANTLR jar not found: ${ANTLR_JAR}")
endif()

set(ANTLR_OUTPUT_DIR "${CMAKE_CURRENT_BINARY_DIR}/generated/antlr")

function(add_antlr_grammar GRAMMAR_FILE GRAMMAR_PREFIX)
    set(_generated_cpp
        "${ANTLR_OUTPUT_DIR}/${GRAMMAR_PREFIX}Lexer.cpp"
        "${ANTLR_OUTPUT_DIR}/${GRAMMAR_PREFIX}Parser.cpp"
        "${ANTLR_OUTPUT_DIR}/${GRAMMAR_PREFIX}BaseVisitor.cpp"
        "${ANTLR_OUTPUT_DIR}/${GRAMMAR_PREFIX}Visitor.cpp"
    )
    set(_generated_h
        "${ANTLR_OUTPUT_DIR}/${GRAMMAR_PREFIX}Lexer.h"
        "${ANTLR_OUTPUT_DIR}/${GRAMMAR_PREFIX}Parser.h"
        "${ANTLR_OUTPUT_DIR}/${GRAMMAR_PREFIX}BaseVisitor.h"
        "${ANTLR_OUTPUT_DIR}/${GRAMMAR_PREFIX}Visitor.h"
    )

    add_custom_command(
        OUTPUT ${_generated_cpp} ${_generated_h}
        COMMAND ${CMAKE_COMMAND} -E make_directory ${ANTLR_OUTPUT_DIR}
        COMMAND ${JAVA_EXECUTABLE} -jar ${ANTLR_JAR}
            -Dlanguage=Cpp
            -visitor
            -no-listener
            -o ${ANTLR_OUTPUT_DIR}
            ${GRAMMAR_FILE}
        DEPENDS ${GRAMMAR_FILE} ${ANTLR_JAR}
        COMMENT "Generating ANTLR parser for ${GRAMMAR_PREFIX}.g4"
        VERBATIM
    )

    set(ANTLR_GENERATED_SOURCES
        ${ANTLR_GENERATED_SOURCES}
        ${_generated_cpp}
        PARENT_SCOPE
    )
endfunction()

add_antlr_grammar("${CMAKE_CURRENT_SOURCE_DIR}/grammar/CACT.g4" "CACT")
add_antlr_grammar("${CMAKE_CURRENT_SOURCE_DIR}/grammar/IR.g4" "IR")
add_custom_target(generate_antlr DEPENDS ${ANTLR_GENERATED_SOURCES})

option(ENABLE_IWYU "Enable Include-What-You-Use (IWYU)" OFF)
if(ENABLE_IWYU)
    find_program(IWYU_EXECUTABLE include-what-you-use)
    if(IWYU_EXECUTABLE)
        set(CMAKE_CXX_INCLUDE_WHAT_YOU_USE ${IWYU_EXECUTABLE})
    else()
        message(FATAL_ERROR "include-what-you-use not found!")
    endif()
endif()

option(ENABLE_FORMAT "Run clang-format before building other targets" ON)

set(SANITIZER_TYPE "none" CACHE STRING "Comma-separated sanitizer types: address, thread, undefined, memory, leak")

find_program(BASH_EXECUTABLE bash)
if(NOT BASH_EXECUTABLE)
    message(FATAL_ERROR "bash not found. It is required for include checks.")
endif()

add_custom_target(check_includes ALL
    COMMAND ${BASH_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/scripts/check_include.sh ${CMAKE_CURRENT_SOURCE_DIR}/src
    WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}
    COMMENT "Checking missing mapped standard includes under src/**"
    VERBATIM
)

find_program(PYTHON_EXECUTABLE python3)
if(NOT PYTHON_EXECUTABLE)
    message(WARNING "python3 not found. Skipping gen_tree target.")
else()
    add_custom_target(gen_tree ALL
        COMMAND ${PYTHON_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/scripts/generate_code_tree.py
        WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}
        COMMENT "Generating code tree with briefs"
        VERBATIM
    )

    # Add fetch_usage target after executables are defined
    # But since this is inside if(PYTHON_EXECUTABLE), and executables are outside, we need to defer
endif()

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(THREADS_PREFER_PTHREAD_FLAG ON)

include_directories(deps/antlr4-runtime/)
include_directories(grammar)
include_directories(${ANTLR_OUTPUT_DIR})
include_directories(src)
include_directories(deps)

enable_testing()

# Project sources excluding tests and src/*.cpp (directly in src/)
FILE(GLOB_RECURSE ALL_SOURCES CONFIGURE_DEPENDS
    "src/*.cpp"
)

# Filter out tests and .cpp files directly in src/ from the core library sources
set(LIB_SOURCES ${ALL_SOURCES})
list(FILTER LIB_SOURCES EXCLUDE REGEX "src/tests/.*\\.cpp")
list(FILTER LIB_SOURCES EXCLUDE REGEX "src/[^/]*\\.cpp$") # Exclude .cpp files directly in src/ (they are executables)

# ANTLR Lib
FILE(GLOB_RECURSE ANTLR_LIB_FILES "deps/antlr4-runtime/*.cpp")

# Source files to format under src/**
FILE(GLOB_RECURSE FORMAT_FILES CONFIGURE_DEPENDS
    "src/*.h"
    "src/*.hpp"
    "src/*.c"
    "src/*.cc"
    "src/*.cpp"
)

find_program(CLANG_FORMAT_EXECUTABLE clang-format)
if(NOT CLANG_FORMAT_EXECUTABLE)
    message(WARNING "clang-format not found, 'format' target will be unavailable.")
endif()

if(CLANG_FORMAT_EXECUTABLE)
    if(FORMAT_FILES)
        add_custom_target(format
            COMMAND ${CLANG_FORMAT_EXECUTABLE} -i ${FORMAT_FILES}
            COMMENT "Formatting src/** with clang-format"
            VERBATIM
        )
    else()
        add_custom_target(format
            COMMAND ${CMAKE_COMMAND} -E echo "No files found under src/** to format."
            VERBATIM
        )
    endif()
endif()

# add_definitions(-w -g)
add_definitions(-g -Wall) # Do not ignore warnings!

if(NOT SANITIZER_TYPE STREQUAL "none")
    string(REPLACE "," ";" SANITIZER_LIST ${SANITIZER_TYPE})
    foreach(SAN IN LISTS SANITIZER_LIST)
        string(STRIP ${SAN} SAN)
        if(SAN STREQUAL "address")
            add_compile_options(-fsanitize=address)
            set(CMAKE_EXE_LINKER_FLAGS "${CMAKE_EXE_LINKER_FLAGS} -fsanitize=address")
            set(CMAKE_SHARED_LINKER_FLAGS "${CMAKE_SHARED_LINKER_FLAGS} -fsanitize=address")
        elseif(SAN STREQUAL "thread")
            add_compile_options(-fsanitize=thread)
            set(CMAKE_EXE_LINKER_FLAGS "${CMAKE_EXE_LINKER_FLAGS} -fsanitize=thread")
            set(CMAKE_SHARED_LINKER_FLAGS "${CMAKE_SHARED_LINKER_FLAGS} -fsanitize=thread")
        elseif(SAN STREQUAL "undefined")
            add_compile_options(-fsanitize=undefined)
            set(CMAKE_EXE_LINKER_FLAGS "${CMAKE_EXE_LINKER_FLAGS} -fsanitize=undefined")
            set(CMAKE_SHARED_LINKER_FLAGS "${CMAKE_SHARED_LINKER_FLAGS} -fsanitize=undefined")
        elseif(SAN STREQUAL "memory")
            add_compile_options(-fsanitize=memory)
            set(CMAKE_EXE_LINKER_FLAGS "${CMAKE_EXE_LINKER_FLAGS} -fsanitize=memory")
            set(CMAKE_SHARED_LINKER_FLAGS "${CMAKE_SHARED_LINKER_FLAGS} -fsanitize=memory")
        elseif(SAN STREQUAL "leak")
            add_compile_options(-fsanitize=leak)
            set(CMAKE_EXE_LINKER_FLAGS "${CMAKE_EXE_LINKER_FLAGS} -fsanitize=leak")
            set(CMAKE_SHARED_LINKER_FLAGS "${CMAKE_SHARED_LINKER_FLAGS} -fsanitize=leak")
        else()
            message(FATAL_ERROR "Unknown sanitizer: ${SAN}")
        endif()
    endforeach()
    # Add common options for better sanitizer performance
    add_compile_options(-fno-omit-frame-pointer)
endif()
add_library(antlr STATIC ${ANTLR_LIB_FILES})

if(ENABLE_FORMAT AND CLANG_FORMAT_EXECUTABLE)
    add_dependencies(antlr format)
endif()

message("LIB_SOURCES: ${LIB_SOURCES}")

# Compiler Core Library
if(LIB_SOURCES)
    add_library(compiler_core STATIC ${LIB_SOURCES} ${ANTLR_GENERATED_SOURCES})
else()
    # Some older CMake versions error when given an empty source list.
    # Create a tiny dummy source file in the build dir so the target is valid.
    file(WRITE ${CMAKE_CURRENT_BINARY_DIR}/empty_compiler_core.cpp "// dummy source for compiler_core\n")
    add_library(compiler_core STATIC ${CMAKE_CURRENT_BINARY_DIR}/empty_compiler_core.cpp)
endif()

add_dependencies(compiler_core generate_antlr)
add_dependencies(compiler_core check_includes)

if(TARGET gen_tree)
    add_dependencies(compiler_core gen_tree)
endif()

if(ENABLE_FORMAT AND CLANG_FORMAT_EXECUTABLE)
    add_dependencies(compiler_core format)
endif()

find_package(Threads REQUIRED)

# Add targets for each test in src/tests
FILE(GLOB TEST_FILES "src/tests/*.cpp")
foreach(TEST_SOURCE ${TEST_FILES})
    get_filename_component(TEST_NAME ${TEST_SOURCE} NAME_WE)
    add_executable(${TEST_NAME} ${TEST_SOURCE})
    target_link_libraries(${TEST_NAME} compiler_core antlr Threads::Threads)
    if(ENABLE_FORMAT AND CLANG_FORMAT_EXECUTABLE)
        add_dependencies(${TEST_NAME} format)
    endif()
    add_test(NAME ${TEST_NAME} COMMAND ${TEST_NAME})
endforeach()

# Add targets for each .cpp file directly in src/
FILE(GLOB SRC_EXECUTABLES "src/*.cpp")
foreach(EXEC_SOURCE ${SRC_EXECUTABLES})
    get_filename_component(EXEC_NAME ${EXEC_SOURCE} NAME_WE)
    add_executable(${EXEC_NAME} ${EXEC_SOURCE})
    target_link_libraries(${EXEC_NAME} compiler_core antlr Threads::Threads)
    if(ENABLE_FORMAT AND CLANG_FORMAT_EXECUTABLE)
        add_dependencies(${EXEC_NAME} format)
    endif()
endforeach()

if(PYTHON_EXECUTABLE AND TARGET compiler AND TARGET interpreter)
    add_custom_target(update_usage ALL
        COMMAND ${PYTHON_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/scripts/fetch_usage.py $<TARGET_FILE:compiler> $<TARGET_FILE:interpreter>
        WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}
        COMMENT "Updating README with usage information"
        VERBATIM
    )
    add_dependencies(update_usage compiler interpreter)
endif()
