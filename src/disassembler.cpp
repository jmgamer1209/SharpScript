#include "sharpscript/disassembler.hpp"

#include <iomanip>

#include "sharpscript/bytecode.hpp"
#include "sharpscript/unicode.hpp"

namespace sharpscript {

#define SS_OPCODE_NAMES(X)          \
    X(Nop) X(PushNull) X(PushTrue) X(PushFalse) X(PushI32Imm) X(PushConst) X(Pop) X(Dup) X(Dup2) X(Swap) \
    X(LoadLocal) X(StoreLocal) X(LoadGlobal) X(StoreGlobal) X(LoadField) X(StoreField) X(LoadThis) \
    X(NewCell) X(LoadCell) X(StoreCell) X(MakeRefCell) X(MakeRefGlobal) X(MakeRefField) \
    X(MakeRefElem) X(LoadRef) X(StoreRef) X(LoadElem) X(StoreElem) X(Length) \
    X(AddI32) X(SubI32) X(MulI32) X(DivI32) X(ModI32) X(BAndI32) X(BOrI32) X(BXorI32) X(BShlI32) X(BShrI32) \
    X(BShrUI32) X(NegI32) X(BNotI32) \
    X(AddI64) X(SubI64) X(MulI64) X(DivI64) X(ModI64) X(BAndI64) X(BOrI64) X(BXorI64) X(BShlI64) X(BShrI64) \
    X(BShrUI64) X(NegI64) X(BNotI64) \
    X(AddF32) X(SubF32) X(MulF32) X(DivF32) X(ModF32) X(NegF32) \
    X(AddF64) X(SubF64) X(MulF64) X(DivF64) X(ModF64) X(NegF64) \
    X(AddStr) X(Not) \
    X(IncI32) X(DecI32) X(IncI64) X(DecI64) X(IncF32) X(DecF32) X(IncF64) X(DecF64) \
    X(CmpEQ) X(CmpNE) X(CmpLT) X(CmpLE) X(CmpGT) X(CmpGE) \
    X(Jmp) X(Jz) X(Jnz) \
    X(ConvI8) X(ConvI16) X(ConvI32) X(ConvI64) X(ConvU8) X(ConvU16) X(ConvChar) X(ConvU32) X(ConvU64) \
    X(ConvF32) X(ConvF64) X(ConvBool) X(ConvStr) \
    X(Call) X(CallVirt) X(CallNative) X(CallNativeVariadic) \
    X(CallDelegate) X(NewDelegate) X(DelegateAdd) X(DelegateSub) \
    X(NewObj) X(NewArray) X(CloneStruct) X(Cast) X(IsType) \
    X(Throw) X(Leave) X(EndFinally) X(Ret)

const char* opCodeName(OpCode op) {
    switch (op) {
#define SS_CASE(name) case OpCode::name: return #name;
        SS_OPCODE_NAMES(SS_CASE)
#undef SS_CASE
    }
    return "<unknown>";
}

namespace {

std::string describeLocal(const ScriptFunction& function, int slot) {
    int index = slot;
    if (!function.isStatic) {
        if (slot == 0) return "this";
        index = slot - 1;
    }
    if (index >= 0 && index < static_cast<int>(function.parameters.size())) {
        return "arg '" + function.parameters[static_cast<std::size_t>(index)].name + "'";
    }
    return "local #" + std::to_string(slot);
}

std::string formatConstant(const Value& value) {
    switch (value.kind()) {
        case ValueKind::String: return "\"" + utf16ToUtf8(value.asString()) + "\"";
        case ValueKind::Char: return "'" + utf16ToUtf8(std::u16string(1, value.asChar())) + "'";
        case ValueKind::Null: return "null";
        default: return value.display() + " (" + value.typeName() + ")";
    }
}

}  // namespace

std::string Disassembler::disassemble(const ScriptFunction& function, bool includeHeader) {
    std::ostringstream out;
    if (includeHeader) {
        out << "// " << function.signature() << "\n";
        out << "// code=" << function.code.size() << " words, locals=" << function.localSlotCount
            << ", stack=" << function.maxStack << ", constants=" << function.constants.size() << "\n";
    }

    for (std::size_t pc = 0; pc < function.code.size(); ++pc) {
        auto decoded = decodeInstruction(function.code[pc]);
        out << "  " << std::right << std::setw(5) << std::setfill('0') << pc << std::setfill(' ')
            << "  " << std::left << std::setw(20) << opCodeName(decoded.op) << std::right;

        std::string comment;
        switch (decoded.op) {
            case OpCode::PushI32Imm: comment = std::to_string(signExtend24(decoded.arg)); break;
            case OpCode::PushConst:
                comment = decoded.arg < static_cast<int>(function.constants.size())
                              ? formatConstant(function.constants[static_cast<std::size_t>(decoded.arg)])
                              : "?";
                break;
            case OpCode::LoadGlobal:
            case OpCode::StoreGlobal:
                if (decoded.arg < static_cast<int>(function.module->globals.size())) {
                    const auto& variable = function.module->globals[static_cast<std::size_t>(decoded.arg)];
                    comment = variable.name + " : " + variable.type.name;
                } else {
                    comment = "?";
                }
                break;
            case OpCode::LoadLocal:
            case OpCode::StoreLocal: comment = describeLocal(function, decoded.arg); break;
            case OpCode::LoadField:
            case OpCode::StoreField:
                if (decoded.arg < static_cast<int>(function.fieldRefs.size())) {
                    const auto* field = function.fieldRefs[static_cast<std::size_t>(decoded.arg)];
                    comment = field->type.name + " " + field->name;
                } else {
                    comment = "?";
                }
                break;
            case OpCode::Call:
                if (decoded.arg < static_cast<int>(function.functionRefs.size())) {
                    comment = function.functionRefs[static_cast<std::size_t>(decoded.arg)]->qualifiedName();
                } else {
                    comment = "?";
                }
                break;
            case OpCode::CallVirt:
                if (decoded.arg < static_cast<int>(function.virtualRefs.size())) {
                    const auto& site = function.virtualRefs[static_cast<std::size_t>(decoded.arg)];
                    comment = "slot " + std::to_string(site.slot) + " (" + std::to_string(site.parameterCount) +
                              " args)";
                } else {
                    comment = "?";
                }
                break;
            case OpCode::CallNative:
            case OpCode::CallNativeVariadic:
                if (decoded.arg < static_cast<int>(function.nativeRefs.size())) {
                    comment = function.nativeRefs[static_cast<std::size_t>(decoded.arg)]->toString();
                } else {
                    comment = "?";
                }
                break;
            case OpCode::NewObj:
            case OpCode::NewArray:
            case OpCode::Cast:
            case OpCode::IsType:
                if (decoded.arg < static_cast<int>(function.typeRefs.size())) {
                    comment = function.typeRefs[static_cast<std::size_t>(decoded.arg)].name;
                } else {
                    comment = "?";
                }
                break;
            case OpCode::Jmp:
            case OpCode::Jz:
            case OpCode::Jnz: comment = "-> " + std::to_string(decoded.arg); break;
            default: break;
        }

        if (!comment.empty()) out << "  ; " << comment;
        out << "\n";
    }
    return out.str();
}

std::string Disassembler::disassembleModule(const ScriptModule& module) {
    std::ostringstream out;
    out << "module " << module.name << " (generation " << module.generation << ")\n";
    if (!module.globals.empty()) {
        out << "globals:\n";
        for (const auto& global : module.globals) {
            out << "  " << std::setw(3) << global.index << "  " << std::left << std::setw(10)
                << global.type.name << global.name << " = " << global.value.display() << "\n";
        }
    }
    for (const auto& cls : module.classes) {
        out << (cls->isStruct ? "struct " : "class ") << cls->name;
        if (cls->base != nullptr) out << " : " << cls->base->name;
        out << "\n";
        for (const auto& field : cls->fields) {
            out << "  field " << field.type.name << " " << field.name << " @" << field.index << "\n";
        }
        for (auto* slot : cls->vtable) {
            out << "  vtable[" << slot->vtableSlot << "] " << slot->qualifiedName() << "("
                << slot->parameters.size() << ")\n";
        }
    }
    for (const auto& function : module.functions) {
        out << "\n" << disassemble(*function);
    }
    return out.str();
}

}  // namespace sharpscript
