#include "sharpscript/builder.hpp"

#include "sharpscript/unicode.hpp"

namespace sharpscript {

int ScriptBuilder::emit(OpCode op, int arg, int stackDelta) {
    int position = static_cast<int>(code_.size());
    code_.push_back(encodeInstruction(op, arg));
    lines_.push_back(currentLine_);
    stackDepth_ += stackDelta;
    if (stackDepth_ > maxStack_) maxStack_ = stackDepth_;
    return position;
}

Label ScriptBuilder::newLabel() { return Label(nextLabel_++); }

void ScriptBuilder::markLabel(const Label& label) {
    labelPositions_[label.id()] = static_cast<int>(code_.size());
}

void ScriptBuilder::emitJump(OpCode op, const Label& label, int stackDelta) {
    int position = emit(op, 0, stackDelta);
    fixups_.emplace_back(position, label);
}

int ScriptBuilder::addConstant(const Value& value) {
    ConstantKey key;
    key.kind = value.kind();
    // Structs are normally materialized with CloneStruct, never as constants;
    // key by identity so two different values can never be deduplicated.
    key.bits = value.kind() == ValueKind::Struct
                   ? static_cast<std::int64_t>(reinterpret_cast<std::uintptr_t>(value.gcObject()))
                   : (value.isReference() ? 0 : value.rawBits());
    key.text = value.kind() == ValueKind::String ? utf16ToUtf8(value.asString()) : std::string();
    auto it = constantLookup_.find(key);
    if (it != constantLookup_.end()) return it->second;
    int index = static_cast<int>(constants_.size());
    constants_.push_back(value);
    constantLookup_[key] = index;
    return index;
}

int ScriptBuilder::addFunctionRef(ScriptFunction* function) {
    for (std::size_t i = 0; i < functionRefs_.size(); ++i) {
        if (functionRefs_[i] == function) return static_cast<int>(i);
    }
    functionRefs_.push_back(function);
    return static_cast<int>(functionRefs_.size() - 1);
}

int ScriptBuilder::addVirtualRef(ScriptFunction* function) {
    for (std::size_t i = 0; i < virtualRefs_.size(); ++i) {
        if (virtualRefs_[i].slot == function->vtableSlot &&
            virtualRefs_[i].parameterCount == static_cast<int>(function->parameters.size())) {
            return static_cast<int>(i);
        }
    }
    virtualRefs_.push_back(VirtualCallSite{function->vtableSlot,
                                           static_cast<int>(function->parameters.size())});
    return static_cast<int>(virtualRefs_.size() - 1);
}

int ScriptBuilder::addNativeRef(NativeFunction* function) {
    for (std::size_t i = 0; i < nativeRefs_.size(); ++i) {
        if (nativeRefs_[i] == function) return static_cast<int>(i);
    }
    nativeRefs_.push_back(function);
    return static_cast<int>(nativeRefs_.size() - 1);
}

int ScriptBuilder::addTypeRef(const ScriptType& type) {
    for (std::size_t i = 0; i < typeRefs_.size(); ++i) {
        if (typeRefs_[i].sameAs(type)) return static_cast<int>(i);
    }
    typeRefs_.push_back(type);
    return static_cast<int>(typeRefs_.size() - 1);
}

int ScriptBuilder::addFieldRef(FieldInfo* field) {
    for (std::size_t i = 0; i < fieldRefs_.size(); ++i) {
        if (fieldRefs_[i] == field) return static_cast<int>(i);
        if (fieldRefs_[i]->index == field->index && fieldRefs_[i]->owner == field->owner) {
            return static_cast<int>(i);
        }
    }
    fieldRefs_.push_back(field);
    return static_cast<int>(fieldRefs_.size() - 1);
}

int ScriptBuilder::addDelegateInfo(const DelegateInfo& info) {
    delegateInfos_.push_back(info);
    return static_cast<int>(delegateInfos_.size() - 1);
}

void ScriptBuilder::complete(ScriptFunction& function) {
    for (const auto& fixup : fixups_) {
        int target = static_cast<int>(code_.size());
        auto it = labelPositions_.find(fixup.second.id());
        if (it != labelPositions_.end()) target = it->second;
        std::uint32_t word = code_[fixup.first];
        code_[fixup.first] = (word & 0xFF000000u) | static_cast<std::uint32_t>(target & 0xFFFFFF);
    }

    function.code = code_;
    function.constants = constants_;
    function.functionRefs = functionRefs_;
    function.virtualRefs = virtualRefs_;
    function.nativeRefs = nativeRefs_;
    function.typeRefs = typeRefs_;
    function.fieldRefs = fieldRefs_;
    function.delegateInfos = delegateInfos_;
    function.lineMap = lines_;
    function.maxStack = std::max(maxStack_, 4);
}

}  // namespace sharpscript
