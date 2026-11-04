#pragma once

#include <string>

namespace sharpscript {

/// Source of the script-side standard library that is compiled into every
/// module (currently `Array<T>`). Written in the scripting language itself so
/// no bytecode instructions are needed for container algorithms.
const std::string& scriptStandardLibrary();

}  // namespace sharpscript
