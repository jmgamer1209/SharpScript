#pragma once

#include "common.hpp"
#include "gc.hpp"

namespace sharpscript {

class ScriptFunction;
class ScriptModule;

enum class ValueKind : std::uint8_t {
    Void,
    Null,
    Bool,
    Int8,
    UInt8,
    Int16,
    UInt16,
    Int32,
    UInt32,
    Int64,
    UInt64,
    Float32,
    Float64,
    Char,
    String,
    Object,
    Array,
    Native,
    /// Script `struct` value (boxed, copied at every value-semantics boundary).
    Struct,
    /// Script `delegate` value (immutable invocation list).
    Delegate,
    /// Captured-variable cell (internal, produced by the closure transform).
    Cell,
    /// Reference to assignable storage (`ref`/`out`/`in` argument).
    Ref
};

class ScriptDelegateType;
class ScriptDelegate;
class ScriptCell;
class ScriptRef;

bool isNumericKind(ValueKind kind);
bool isIntegralKind(ValueKind kind);

/// Tagged value used on the operand stack, in locals, fields and globals.
/// The numeric payload is always 64 bit; reference payloads live in ref_.
class Value {
public:
    Value() : kind_(ValueKind::Null), bits_(0) {}

    static Value voidValue() { return Value(ValueKind::Void, 0, nullptr); }
    static Value null() { return Value(ValueKind::Null, 0, nullptr); }
    static Value boolean(bool value) { return Value(ValueKind::Bool, value ? 1 : 0, nullptr); }
    static Value i8(std::int8_t value) { return Value(ValueKind::Int8, value, nullptr); }
    static Value u8(std::uint8_t value) { return Value(ValueKind::UInt8, value, nullptr); }
    static Value i16(std::int16_t value) { return Value(ValueKind::Int16, value, nullptr); }
    static Value u16(std::uint16_t value) { return Value(ValueKind::UInt16, value, nullptr); }
    static Value i32(std::int32_t value) { return Value(ValueKind::Int32, value, nullptr); }
    static Value u32(std::uint32_t value) { return Value(ValueKind::UInt32, value, nullptr); }
    static Value i64(std::int64_t value) { return Value(ValueKind::Int64, value, nullptr); }
    static Value u64(std::uint64_t value) {
        return Value(ValueKind::UInt64, static_cast<std::int64_t>(value), nullptr);
    }
    static Value f32(float value) {
        return Value(ValueKind::Float32, static_cast<std::int64_t>(bitsOf(static_cast<double>(value))), nullptr);
    }
    static Value f64(double value) {
        return Value(ValueKind::Float64, static_cast<std::int64_t>(bitsOf(value)), nullptr);
    }
    static Value character(char16_t value) {
        return Value(ValueKind::Char, static_cast<std::uint16_t>(value), nullptr);
    }
    static Value string(std::u16string value) {
        return Value(ValueKind::String, 0, std::make_shared<std::u16string>(std::move(value)));
    }
    /// Convenience overload for hosts whose text is UTF-8.
    static Value stringUtf8(const std::string& value);
    static Value instance(std::shared_ptr<ScriptInstance> value) {
        if (!value) return null();
        return Value(ValueKind::Object, 0, std::static_pointer_cast<void>(value));
    }
    static Value array(std::shared_ptr<ScriptArray> value) {
        if (!value) return null();
        return Value(ValueKind::Array, 0, std::static_pointer_cast<void>(value));
    }
    static Value structValue(std::shared_ptr<ScriptStructValue> value) {
        if (!value) return null();
        return Value(ValueKind::Struct, 0, std::static_pointer_cast<void>(value));
    }
    static Value delegateValue(std::shared_ptr<ScriptDelegate> value) {
        if (!value) return null();
        return Value(ValueKind::Delegate, 0, std::static_pointer_cast<void>(value));
    }
    static Value cellValue(std::shared_ptr<ScriptCell> value) {
        if (!value) return null();
        return Value(ValueKind::Cell, 0, std::static_pointer_cast<void>(value));
    }
    static Value refValue(std::shared_ptr<ScriptRef> value) {
        if (!value) return null();
        return Value(ValueKind::Ref, 0, std::static_pointer_cast<void>(value));
    }

    template <typename T>
    static Value native(std::shared_ptr<T> value) {
        if (!value) return null();
        return Value(ValueKind::Native, 0, std::static_pointer_cast<void>(value));
    }

    ValueKind kind() const { return kind_; }
    bool isVoid() const { return kind_ == ValueKind::Void; }
    bool isNull() const {
        if (kind_ == ValueKind::Null) return true;
        if (kind_ == ValueKind::String || kind_ == ValueKind::Object || kind_ == ValueKind::Array ||
            kind_ == ValueKind::Native || kind_ == ValueKind::Delegate) {
            return ref_ == nullptr;
        }
        return false;
    }
    bool isReference() const {
        return kind_ == ValueKind::String || kind_ == ValueKind::Object || kind_ == ValueKind::Array ||
               kind_ == ValueKind::Native || kind_ == ValueKind::Delegate || kind_ == ValueKind::Null;
    }

    bool asBool() const {
        switch (kind_) {
            case ValueKind::Bool: return bits_ != 0;
            case ValueKind::Null: return false;
            case ValueKind::Object:
            case ValueKind::Array:
            case ValueKind::Native:
            case ValueKind::Struct:
            case ValueKind::Delegate:
            case ValueKind::String: return ref_ != nullptr;
            default: return asF64() != 0.0;
        }
    }

    std::int64_t asI64() const {
        switch (kind_) {
            case ValueKind::Int8:
            case ValueKind::UInt8:
            case ValueKind::Int16:
            case ValueKind::UInt16:
            case ValueKind::Int32:
            case ValueKind::UInt32:
            case ValueKind::Int64:
            case ValueKind::UInt64: return bits_;
            case ValueKind::Char: return static_cast<std::uint16_t>(bits_);
            case ValueKind::Bool: return bits_;
            case ValueKind::Float32:
            case ValueKind::Float64: return static_cast<std::int64_t>(asF64());
            default: return 0;
        }
    }

    std::uint64_t asU64() const {
        switch (kind_) {
            case ValueKind::UInt64: return static_cast<std::uint64_t>(bits_);
            case ValueKind::Char: return static_cast<std::uint16_t>(bits_);
            case ValueKind::Bool: return static_cast<std::uint64_t>(bits_);
            case ValueKind::Float32:
            case ValueKind::Float64: return static_cast<std::uint64_t>(asF64());
            default: return static_cast<std::uint64_t>(asI64());
        }
    }

    std::int32_t asI32() const { return static_cast<std::int32_t>(asI64()); }

    double asF64() const {
        switch (kind_) {
            case ValueKind::Float32:
            case ValueKind::Float64: return doubleFromBits(static_cast<std::uint64_t>(bits_));
            case ValueKind::Char: return static_cast<std::uint16_t>(bits_);
            case ValueKind::Bool: return static_cast<double>(bits_);
            case ValueKind::UInt64: return static_cast<double>(static_cast<std::uint64_t>(bits_));
            default: return static_cast<double>(bits_);
        }
    }

    char16_t asChar() const {
        if (kind_ == ValueKind::Char) return static_cast<char16_t>(bits_);
        return static_cast<char16_t>(asI64());
    }

    const std::u16string& asString() const {
        static const std::u16string empty;
        if (kind_ != ValueKind::String || !ref_) return empty;
        return *std::static_pointer_cast<std::u16string>(ref_);
    }

    std::shared_ptr<ScriptInstance> asInstance() const {
        if (kind_ != ValueKind::Object) return nullptr;
        return std::static_pointer_cast<ScriptInstance>(ref_);
    }

    std::shared_ptr<ScriptArray> asArray() const {
        if (kind_ != ValueKind::Array) return nullptr;
        return std::static_pointer_cast<ScriptArray>(ref_);
    }

    std::shared_ptr<ScriptStructValue> asStruct() const {
        if (kind_ != ValueKind::Struct) return nullptr;
        return std::static_pointer_cast<ScriptStructValue>(ref_);
    }

    std::shared_ptr<ScriptDelegate> asDelegate() const {
        if (kind_ != ValueKind::Delegate) return nullptr;
        return std::static_pointer_cast<ScriptDelegate>(ref_);
    }

    std::shared_ptr<ScriptCell> asCell() const {
        if (kind_ != ValueKind::Cell) return nullptr;
        return std::static_pointer_cast<ScriptCell>(ref_);
    }

    std::shared_ptr<ScriptRef> asRef() const {
        if (kind_ != ValueKind::Ref) return nullptr;
        return std::static_pointer_cast<ScriptRef>(ref_);
    }

    template <typename T>
    std::shared_ptr<T> asNative() const {
        if (kind_ != ValueKind::Native) return nullptr;
        return std::static_pointer_cast<T>(ref_);
    }

    std::int64_t rawBits() const { return bits_; }
    /// Borrowed handle: no reference count change, which the collector relies on.
    const std::shared_ptr<void>& rawRef() const { return ref_; }
    /// Script heap object held by this value (instance/array), or null.
    /// Performs no reference count change so it is safe inside the collector.
    GcObject* gcObject() const;

    std::string typeName() const;
    /// UTF-8 representation used for diagnostics and print().
    std::string display() const;
    /// C# `object.ToString()` semantics used by string concatenation and ConvStr:
    /// null -> empty, bool -> True/False, numbers -> invariant C# formatting.
    std::u16string toScriptString() const;

    static std::uint64_t bitsOf(double value) {
        std::uint64_t bits = 0;
        std::memcpy(&bits, &value, sizeof(bits));
        return bits;
    }
    static double doubleFromBits(std::uint64_t bits) {
        double value = 0;
        std::memcpy(&value, &bits, sizeof(value));
        return value;
    }

private:
    Value(ValueKind kind, std::int64_t bits, std::shared_ptr<void> ref)
        : kind_(kind), bits_(bits), ref_(std::move(ref)) {}

    ValueKind kind_;
    std::int64_t bits_;
    std::shared_ptr<void> ref_;
};

class ScriptInstance : public GcObject {
public:
    ScriptInstance(ScriptClassType* cls, GcCollector& collector);
    ScriptClassType* cls() const { return cls_; }
    std::vector<Value>& fields() { return fields_; }
    const std::vector<Value>& fields() const { return fields_; }

    void gcEnumerateReferences(GcCollector& collector) override;
    void gcReleaseReferences() override;
    bool gcNeedsFinalizer() const override;
    /// Marks the finalizer as scheduled so the object is destroyed next time.
    void markFinalized() { finalized_ = true; }
    bool finalized() const { return finalized_; }

private:
    ScriptClassType* cls_;
    std::vector<Value> fields_;
    bool finalized_ = false;
};

class ScriptArray : public GcObject {
public:
    explicit ScriptArray(std::shared_ptr<const ScriptType> elementType, std::size_t capacity = 0)
        : elementType_(std::move(elementType)) {
        if (capacity > 0) items_.reserve(capacity);
    }

    const std::shared_ptr<const ScriptType>& elementType() const { return elementType_; }
    std::vector<Value>& items() { return items_; }
    const std::vector<Value>& items() const { return items_; }
    std::size_t size() const { return items_.size(); }

    void gcEnumerateReferences(GcCollector& collector) override;
    void gcReleaseReferences() override;

private:
    std::shared_ptr<const ScriptType> elementType_;
    std::vector<Value> items_;
};

/// Boxed storage for a script `struct`. Value semantics are implemented by
/// copying this box (recursively for nested struct fields) at every assignment,
/// argument passing, array store and return; class/array/string references
/// inside the struct stay shared, exactly like C#.
class ScriptStructValue : public GcObject {
public:
    explicit ScriptStructValue(ScriptClassType* type) : type_(type) {}
    ScriptClassType* type() const { return type_; }
    std::vector<Value>& fields() { return fields_; }
    const std::vector<Value>& fields() const { return fields_; }

    void gcEnumerateReferences(GcCollector& collector) override;
    void gcReleaseReferences() override;

private:
    ScriptClassType* type_;
    std::vector<Value> fields_;
};

/// Immutable invocation list of a script delegate (C# multicast semantics).
class ScriptDelegate : public GcObject {
public:
    struct Entry {
        Value target;
        ScriptFunction* function = nullptr;
        /// Closure cells captured by a lambda (appended after the parameters).
        std::vector<Value> captures;
    };

    explicit ScriptDelegate(ScriptDelegateType* type) : type_(type) {}
    ScriptDelegateType* type() const { return type_; }
    std::vector<Entry>& entries() { return entries_; }
    const std::vector<Entry>& entries() const { return entries_; }
    /// Keeps the owning module generation alive for as long as the delegate is.
    void setModuleOwner(std::shared_ptr<ScriptModule> module) { moduleOwner_ = std::move(module); }

    void gcEnumerateReferences(GcCollector& collector) override;
    void gcReleaseReferences() override;

private:
    ScriptDelegateType* type_ = nullptr;
    std::vector<Entry> entries_;
    std::shared_ptr<ScriptModule> moduleOwner_;
};

/// Heap cell holding one captured variable (shared by every closure that
/// captures the same local, giving C# by-reference capture semantics).
class ScriptCell : public GcObject {
public:
    explicit ScriptCell(Value initial) : value(std::move(initial)) {}
    Value value;

    void gcEnumerateReferences(GcCollector& collector) override { collector.enumerateValue(value); }
    void gcReleaseReferences() override { value = Value::null(); }
};

enum class RefKind : std::uint8_t { Cell, Global, Field, Elem };

/// Reference to assignable storage used by `ref` / `out` / `in` parameters.
/// References only live for the duration of a call (no ref returns/locals).
class ScriptRef : public GcObject {
public:
    RefKind refKind = RefKind::Cell;
    std::shared_ptr<ScriptCell> cell;           ///< kind == Cell
    Value owner;                                ///< field owner / array (Field/Elem)
    int index = 0;                              ///< field slot / element index
    Value* global = nullptr;                    ///< kind == Global (module global slot)
    std::shared_ptr<ScriptModule> moduleOwner;  ///< keeps the global storage alive

    Value load() const;
    void store(const Value& value);

    void gcEnumerateReferences(GcCollector& collector) override {
        if (refKind == RefKind::Cell) {
            if (cell) collector.enumerateValue(Value::cellValue(cell));
        } else if (refKind != RefKind::Global) {
            collector.enumerateValue(owner);
        }
    }
    void gcReleaseReferences() override {
        cell.reset();
        owner = Value::null();
        global = nullptr;
    }
};

/// Allocates a struct with every field zero-initialized.
Value makeDefaultStructValue(ScriptClassType& type, GcCollector& collector);
/// Deep-copies a struct value (nested structs are copied, references shared).
Value cloneStructValue(const Value& value, GcCollector& collector);

Value defaultValue(const ScriptType& type, GcCollector& collector);

namespace ValueOps {

Value addI32(const Value& a, const Value& b);
Value subI32(const Value& a, const Value& b);
Value mulI32(const Value& a, const Value& b);
Value divI32(const Value& a, const Value& b);
Value modI32(const Value& a, const Value& b);
Value addI64(const Value& a, const Value& b);
Value subI64(const Value& a, const Value& b);
Value mulI64(const Value& a, const Value& b);
Value divI64(const Value& a, const Value& b);
Value modI64(const Value& a, const Value& b);
Value addF32(const Value& a, const Value& b);
Value subF32(const Value& a, const Value& b);
Value mulF32(const Value& a, const Value& b);
Value divF32(const Value& a, const Value& b);
Value modF32(const Value& a, const Value& b);
Value addF64(const Value& a, const Value& b);
Value subF64(const Value& a, const Value& b);
Value mulF64(const Value& a, const Value& b);
Value divF64(const Value& a, const Value& b);
Value modF64(const Value& a, const Value& b);
Value addString(const Value& a, const Value& b);

Value bAndI32(const Value& a, const Value& b);
Value bOrI32(const Value& a, const Value& b);
Value bXorI32(const Value& a, const Value& b);
Value bShlI32(const Value& a, const Value& b);
Value bShrI32(const Value& a, const Value& b);
Value bShrUI32(const Value& a, const Value& b);
Value bAndI64(const Value& a, const Value& b);
Value bOrI64(const Value& a, const Value& b);
Value bXorI64(const Value& a, const Value& b);
Value bShlI64(const Value& a, const Value& b);
Value bShrI64(const Value& a, const Value& b);
Value bShrUI64(const Value& a, const Value& b);

Value negI32(const Value& a);
Value negI64(const Value& a);
Value negF32(const Value& a);
Value negF64(const Value& a);
Value logicalNot(const Value& a);
Value bNotI32(const Value& a);
Value bNotI64(const Value& a);

int compare(const Value& a, const Value& b);
bool areEqual(const Value& a, const Value& b);

}  // namespace ValueOps

}  // namespace sharpscript
