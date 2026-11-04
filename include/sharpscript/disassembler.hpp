#pragma once

#include "module.hpp"

namespace sharpscript {

class Disassembler {
public:
    static std::string disassemble(const ScriptFunction& function, bool includeHeader = true);
    static std::string disassembleModule(const ScriptModule& module);
};

}  // namespace sharpscript
