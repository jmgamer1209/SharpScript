#pragma once

#include "bytecode.hpp"
#include "function.hpp"

namespace sharpscript {

class Label {
public:
    Label() = default;
    int id() const { return id_; }

private:
    explicit Label(int id) : id_(id) {}
    int id_ = 0;
    friend class ScriptBuilder;
};

/// Emits the packed instruction stream for one function, tracking the constant
/// pool, label fixups and link-time reference tables.
class ScriptBuilder {
public:
    int emit(OpCode op, int arg = 0, int stackDelta = 0);
    Label newLabel();
    void markLabel(const Label& label);
    void emitJump(OpCode op, const Label& label, int stackDelta = 0);

    int addConstant(const Value& value);
    int addFunctionRef(ScriptFunction* function);
    int addVirtualRef(ScriptFunction* function);
    int addNativeRef(NativeFunction* function);
    int addTypeRef(const ScriptType& type);
    int addFieldRef(FieldInfo* field);
    int addDelegateInfo(const DelegateInfo& info);

    void complete(ScriptFunction& function);

    int position() const { return static_cast<int>(code_.size()); }
    int stackDepth() const { return stackDepth_; }
    int maxStack() const { return maxStack_; }
    void setLine(int line) { currentLine_ = line; }
    int currentLine() const { return currentLine_; }
    const std::vector<Value>& constants() const { return constants_; }

private:
    struct ConstantKey {
        ValueKind kind = ValueKind::Null;
        std::int64_t bits = 0;
        std::string text;
        bool operator==(const ConstantKey& other) const {
            return kind == other.kind && bits == other.bits && text == other.text;
        }
    };
    struct ConstantKeyHash {
        std::size_t operator()(const ConstantKey& key) const {
            std::size_t h1 = std::hash<int>()(static_cast<int>(key.kind));
            std::size_t h2 = std::hash<std::int64_t>()(key.bits);
            std::size_t h3 = std::hash<std::string>()(key.text);
            return h1 ^ (h2 << 1) ^ (h3 << 2);
        }
    };

    std::vector<std::uint32_t> code_;
    std::vector<int> lines_;
    std::vector<Value> constants_;
    std::unordered_map<ConstantKey, int, ConstantKeyHash> constantLookup_;
    std::vector<ScriptFunction*> functionRefs_;
    std::vector<VirtualCallSite> virtualRefs_;
    std::vector<NativeFunction*> nativeRefs_;
    std::vector<ScriptType> typeRefs_;
    std::vector<FieldInfo*> fieldRefs_;
    std::vector<DelegateInfo> delegateInfos_;
    std::unordered_map<int, int> labelPositions_;
    std::vector<std::pair<int, Label>> fixups_;
    int nextLabel_ = 0;
    int stackDepth_ = 0;
    int maxStack_ = 0;
    int currentLine_ = 1;
};

}  // namespace sharpscript
