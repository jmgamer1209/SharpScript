#include "sharpscript/stdlib.hpp"

// Generated from lib/*.cs at configure time (see CMakeLists.txt).
#include "stdlib_source.hpp"

namespace sharpscript {

const std::vector<StandardLibraryFile>& scriptStandardLibrary() {
    static const std::vector<StandardLibraryFile> files = [] {
        std::vector<StandardLibraryFile> result;
        for (const auto& entry : detail::kScriptStandardLibraryFiles) {
            result.push_back(StandardLibraryFile{entry.file, entry.source});
        }
        return result;
    }();
    return files;
}

}  // namespace sharpscript
