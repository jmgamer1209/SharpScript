#pragma once

#include <ostream>

#include "module.hpp"

namespace sharpscript {

class Disassembler {
public:
    /// Bytecode listing returned as text (what `sharpscript_cli disasm` prints).
    static std::string disassemble(const ScriptFunction& function, bool includeHeader = true);
    static std::string disassembleModule(const ScriptModule& module);

    /// Same listing written to any sink, so hosts and the CLI can dump it
    /// straight into a file/stream without materializing a string first.
    static void write(const ScriptFunction& function, std::ostream& out, bool includeHeader = true);
    static void writeModule(const ScriptModule& module, std::ostream& out);
};

}  // namespace sharpscript
