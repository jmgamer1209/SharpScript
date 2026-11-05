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
# and validated by the CLI. Each file is embedded separately and becomes its own
# module section, so diagnostics name the real file and `--no-stdlib` can validate
# one file at a time. The binary stays self-contained (the AngelScript analogue:
# source in the repo, add-on in the SDK).
set(SHARPSCRIPT_STDLIB_FILES
    ${CMAKE_CURRENT_SOURCE_DIR}/lib/array.cs
    ${CMAKE_CURRENT_SOURCE_DIR}/lib/list.cs
    ${CMAKE_CURRENT_SOURCE_DIR}/lib/dictionary.cs
)
set(SHARPSCRIPT_GENERATED_DIR ${CMAKE_CURRENT_BINARY_DIR}/generated)
set(SHARPSCRIPT_STDLIB_TABLE "")
foreach(stdlib_file IN LISTS SHARPSCRIPT_STDLIB_FILES)
    file(READ ${stdlib_file} stdlib_part)
    # Section names are repo-relative: `lib/array.cs`, `lib/list.cs`, ...
    file(RELATIVE_PATH stdlib_name ${CMAKE_CURRENT_SOURCE_DIR} ${stdlib_file})
    string(APPEND SHARPSCRIPT_STDLIB_TABLE
"    {\"${stdlib_name}\", R\"SS(${stdlib_part})SS\"},
")
endforeach()
file(WRITE ${SHARPSCRIPT_GENERATED_DIR}/stdlib_source.hpp
"// Generated from lib/*.cs -- do not edit.
#pragma once

namespace sharpscript {
namespace detail {

struct StandardLibraryFile {
    const char* file;
    const char* source;
};

inline const StandardLibraryFile kScriptStandardLibraryFiles[] = {
${SHARPSCRIPT_STDLIB_TABLE}};

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

# --------------------------------------------------------------- sample smoke
# Every runnable sample is also a smoke test: it must compile and exit cleanly, so
# the examples cannot rot as the language evolves. `module_main.cs` needs its
# second section, which the last entry supplies.
set(SHARPSCRIPT_SAMPLES
    hello oop native interfaces dispose collections structs delegates generics
    exceptions refs properties controlflow
)
foreach(sample IN LISTS SHARPSCRIPT_SAMPLES)
    add_test(NAME sample_${sample}
             COMMAND sharpscript_cli run ${CMAKE_CURRENT_SOURCE_DIR}/samples/${sample}.cs)
endforeach()
add_test(NAME sample_multifile
         COMMAND sharpscript_cli run
                 ${CMAKE_CURRENT_SOURCE_DIR}/samples/module_main.cs
                 ${CMAKE_CURRENT_SOURCE_DIR}/samples/module_player.cs)

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
