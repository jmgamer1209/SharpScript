#pragma once

#include <string>
#include <vector>

namespace sharpscript {

/// One embedded standard library file (`lib/*.cs`).
struct StandardLibraryFile {
    /// Section name used in diagnostics, e.g. `lib/array.cs`.
    std::string file;
    std::string source;
};

/// The script-side standard library that is compiled into every module
/// (`Array<T>`, `List<T>`, `Dictionary<K,V>`). Written in the scripting language
/// itself so no bytecode instructions are needed for container algorithms; each
/// file becomes its own module section.
const std::vector<StandardLibraryFile>& scriptStandardLibrary();

}  // namespace sharpscript
