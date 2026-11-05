cmake_minimum_required(VERSION 3.20)
project(SharpScript LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

if(MSVC)
    add_compile_options(/W4 /permissive- /utf-8 /EHsc)
else()
    add_compile_options(-Wall -Wextra -Wpedantic)
endif()

# The script standard library lives in lib/*.cs so it can be edited, highlighted
# and validated by the CLI. It is embedded at configure time to keep the binary
# self-contained (the AngelScript analogue: source in the repo, add-on in the SDK).
set(SHARPSCRIPT_STDLIB_FILES
    ${CMAKE_CURRENT_SOURCE_DIR}/lib/array.cs
)
set(SHARPSCRIPT_GENERATED_DIR ${CMAKE_CURRENT_BINARY_DIR}/generated)
set(SHARPSCRIPT_STDLIB_SOURCE "")
foreach(stdlib_file IN LISTS SHARPSCRIPT_STDLIB_FILES)
    file(READ ${stdlib_file} stdlib_part)
    string(APPEND SHARPSCRIPT_STDLIB_SOURCE "${stdlib_part}\n")
endforeach()
file(WRITE ${SHARPSCRIPT_GENERATED_DIR}/stdlib_source.hpp
"// Generated from lib/*.cs -- do not edit.
#pragma once

namespace sharpscript {
namespace detail {

inline const char* const kScriptStandardLibrary = R\"SS(
${SHARPSCRIPT_STDLIB_SOURCE}
)SS\";

}  // namespace detail
}  // namespace sharpscript
")
# Re-run CMake when the library sources change so the embedded copy stays fresh.
set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS ${SHARPSCRIPT_STDLIB_FILES})

add_library(sharpscript STATIC
    src/value.cpp
    src/gc.cpp
    src/type.cpp
    src/unicode.cpp
    src/tokenizer.cpp
    src/parser.cpp
    src/builder.cpp
    src/compiler.cpp
    src/function_compiler.cpp
    src/module.cpp
    src/engine.cpp
    src/context.cpp
    src/native.cpp
    src/stdlib.cpp
    src/method_reload.cpp
    src/hot_reload.cpp
    src/disassembler.cpp
)

target_include_directories(sharpscript PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include)
target_include_directories(sharpscript PRIVATE ${SHARPSCRIPT_GENERATED_DIR})
set_source_files_properties(src/stdlib.cpp PROPERTIES
    OBJECT_DEPENDS ${SHARPSCRIPT_GENERATED_DIR}/stdlib_source.hpp)

add_executable(sharpscript_cli cli/main.cpp)
target_link_libraries(sharpscript_cli PRIVATE sharpscript)

add_executable(sharpscript_tests tests/main.cpp)
target_link_libraries(sharpscript_tests PRIVATE sharpscript)

enable_testing()
add_test(NAME sharpscript_tests COMMAND sharpscript_tests)

# ------------------------------------------------------------ C# compatibility
# The standard library and the samples are meant to be valid C# as well. When a
# .NET SDK is available, compile them with Roslyn as part of the test suite.
find_program(SHARPSCRIPT_DOTNET_EXECUTABLE NAMES dotnet)
if(SHARPSCRIPT_DOTNET_EXECUTABLE)
    execute_process(COMMAND ${SHARPSCRIPT_DOTNET_EXECUTABLE} --version
                    OUTPUT_VARIABLE SHARPSCRIPT_DOTNET_VERSION
                    OUTPUT_STRIP_TRAILING_WHITESPACE
                    ERROR_QUIET)
    if(SHARPSCRIPT_DOTNET_VERSION MATCHES "^([0-9]+)\\.")
        set(SHARPSCRIPT_DOTNET_TFM "net${CMAKE_MATCH_1}.0")
        add_test(NAME csharp_compat
                 COMMAND ${CMAKE_COMMAND}
                         -DDOTNET=${SHARPSCRIPT_DOTNET_EXECUTABLE}
                         -DTFM=${SHARPSCRIPT_DOTNET_TFM}
                         -DSOURCE_DIR=${CMAKE_CURRENT_SOURCE_DIR}
                         -DWORK_DIR=${CMAKE_CURRENT_BINARY_DIR}/csharp_compat
                         -P ${CMAKE_CURRENT_SOURCE_DIR}/cmake/csharp_compat.cmake)
        message(STATUS "C# compatibility check enabled (${SHARPSCRIPT_DOTNET_TFM})")
    endif()
else()
    message(STATUS "C# compatibility check disabled (no dotnet found)")
endif()
