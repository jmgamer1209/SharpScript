#pragma once

#include "type.hpp"

namespace sharpscript {

struct ScriptParameter {
    std::string name;
    ScriptType type;
    ParamModifier modifier = ParamModifier::None;

    bool isRef() const { return modifier != ParamModifier::None; }
};

struct VirtualCallSite {
    int slot = -1;
    int parameterCount = 0;
};

enum class HandlerKind : std::uint8_t { Catch, Finally };

/// One protected region of a `try` statement. The protected range is half-open:
/// `tryStart <= pc < tryEnd`. For `Finally` clauses `handlerPc..handlerEnd` is
/// the handler body, which is not protected by the clause itself.
struct ExceptionHandler {
    HandlerKind kind = HandlerKind::Catch;
    int tryStart = 0;
    int tryEnd = 0;
    int handlerPc = 0;
    /// End of a `finally` body (used to detect exceptions escaping it).
    int handlerEnd = 0;
    /// Operand stack height to restore before entering the handler.
    int stackDepth = 0;
    /// Local slot that receives the caught exception.
    int exceptionSlot = -1;
    /// `catch` target type; unused when `hasCatchType` is false (catch-all).
    ScriptType catchType;
    bool hasCatchType = false;
    int line = 0;
};

/// Compile-time description of one `NewDelegate` site.
struct DelegateInfo {
    ScriptFunction* function = nullptr;
    ScriptDelegateType* type = nullptr;
    /// Captured closure cells passed after the declared parameters.
    int captureCount = 0;
};

/// A compiled function: bytecode, constant pool and all link-time references.
class ScriptFunction {
public:
    std::string name;
    /// Enclosing namespace for free functions ("Game.AI" or empty).
    std::string namespacePath;
    ScriptModule* module = nullptr;
    ScriptClassType* owner = nullptr;
    ScriptType returnType;
    std::vector<ScriptParameter> parameters;

    int localSlotCount = 0;
    int maxStack = 0;

    std::vector<std::uint32_t> code;
    std::vector<Value> constants;
    std::vector<ScriptFunction*> functionRefs;
    std::vector<VirtualCallSite> virtualRefs;
    std::vector<NativeFunction*> nativeRefs;
    std::vector<ScriptType> typeRefs;
    std::vector<FieldInfo*> fieldRefs;
    std::vector<DelegateInfo> delegateInfos;
    std::vector<int> lineMap;
    /// Catch clauses, in source order (one entry per `catch`).
    std::vector<ExceptionHandler> handlers;

    std::string file;
    int declLine = 0;

    bool isStatic = false;
    bool isConstructor = false;
    bool isAbstract = false;
    NativeFunction* native = nullptr;
    int vtableSlot = -1;

    bool isNative() const { return native != nullptr; }
    bool hasBody() const { return native == nullptr && !isAbstract && !code.empty(); }

    std::string qualifiedName() const {
        if (owner != nullptr) return isConstructor ? owner->name : owner->name + "." + name;
        return namespacePath.empty() ? name : namespacePath + "." + name;
    }

    std::string signature() const;

    int lineAt(int instruction) const {
        if (instruction >= 0 && instruction < static_cast<int>(lineMap.size())) return lineMap[instruction];
        return declLine;
    }
};

struct GlobalVariable {
    std::string name;
    ScriptType type;
    Value value;
    bool isReadOnly = false;
    bool isStatic = false;
    int index = 0;
    int declLine = 0;
};

}  // namespace sharpscript
