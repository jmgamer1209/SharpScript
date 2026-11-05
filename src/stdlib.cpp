#include "sharpscript/stdlib.hpp"

// Generated from lib/array.cs at configure time (see CMakeLists.txt).
#include "stdlib_source.hpp"

namespace sharpscript {

const std::string& scriptStandardLibrary() {
    static const std::string source = detail::kScriptStandardLibrary;
    return source;
}

}  // namespace sharpscript
