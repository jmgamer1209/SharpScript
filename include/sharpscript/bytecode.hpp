#pragma once

#include "common.hpp"

namespace sharpscript {

/// Every instruction is one 32-bit word: high 8 bits opcode, low 24 bits operand.
enum class OpCode : std::uint8_t {
    Nop = 0,

    PushNull,
    PushTrue,
    PushFalse,
    PushI32Imm,
    PushConst,

    Pop,
    Dup,
    Dup2,
    Swap,

    LoadLocal,
    StoreLocal,
    LoadGlobal,
    StoreGlobal,
    LoadField,
    StoreField,
    LoadThis,
    /// Captured-variable cells: wrap a value, read it back, write it back.
    NewCell,
    LoadCell,
    StoreCell,
    /// `ref`/`out`/`in` storage references.
    MakeRefCell,
    MakeRefGlobal,
    MakeRefField,
    MakeRefElem,
    LoadRef,
    StoreRef,
    LoadElem,
    StoreElem,
    Length,

    AddI32,
    SubI32,
    MulI32,
    DivI32,
    ModI32,
    BAndI32,
    BOrI32,
    BXorI32,
    BShlI32,
    BShrI32,
    BShrUI32,
    NegI32,
    BNotI32,

    AddI64,
    SubI64,
    MulI64,
    DivI64,
    ModI64,
    BAndI64,
    BOrI64,
    BXorI64,
    BShlI64,
    BShrI64,
    BShrUI64,
    NegI64,
    BNotI64,

    AddF32,
    SubF32,
    MulF32,
    DivF32,
    ModF32,
    NegF32,
    AddF64,
    SubF64,
    MulF64,
    DivF64,
    ModF64,
    NegF64,

    AddStr,
    Not,

    IncI32,
    DecI32,
    IncI64,
    DecI64,
    IncF32,
    DecF32,
    IncF64,
    DecF64,

    CmpEQ,
    CmpNE,
    CmpLT,
    CmpLE,
    CmpGT,
    CmpGE,

    Jmp,
    Jz,
    Jnz,

    ConvI8,
    ConvI16,
    ConvI32,
    ConvI64,
    ConvU8,
    ConvU16,
    ConvChar,
    ConvU32,
    ConvU64,
    ConvF32,
    ConvF64,
    ConvBool,
    ConvStr,

    Call,
    CallVirt,
    CallNative,
    CallNativeVariadic,
    /// Pops a delegate (plus arguments) and invokes every entry; operand = argc.
    CallDelegate,
    /// Builds a delegate from a target and captured cells; operand = DelegateInfo index.
    NewDelegate,
    /// C# multicast combine/remove (2 operands -> 1).
    DelegateAdd,
    DelegateSub,

    NewObj,
    NewArray,
    /// Replaces the struct on top of the stack with an independent copy.
    CloneStruct,
    Cast,
    IsType,


    /// Pops an Exception value and starts stack unwinding.
    Throw,
    /// Leaves protected regions, running any crossed `finally` handlers first.
    Leave,
    /// End of a `finally` handler: resumes the pending control transfer.
    EndFinally,

    Ret
};

struct DecodedInstruction {
    OpCode op = OpCode::Nop;
    int arg = 0;
};

inline std::uint32_t encodeInstruction(OpCode op, int arg = 0) {
    if (arg < 0 || arg > 0xFFFFFF) throw std::out_of_range("instruction operand does not fit in 24 bits");
    return (static_cast<std::uint32_t>(op) << 24) | static_cast<std::uint32_t>(arg);
}

inline DecodedInstruction decodeInstruction(std::uint32_t word) {
    return DecodedInstruction{static_cast<OpCode>((word >> 24) & 0xFF), static_cast<int>(word & 0xFFFFFF)};
}

inline int signExtend24(int arg) { return (arg << 8) >> 8; }

const char* opCodeName(OpCode op);

}  // namespace sharpscript
