#include "sharpscript/value.hpp"

#include <charconv>

#include "sharpscript/type.hpp"
#include "sharpscript/unicode.hpp"

namespace sharpscript {
namespace {

template <typename T>
std::string numberToString(T value) {
    char buffer[64];
    auto result = std::to_chars(buffer, buffer + sizeof(buffer), value);
    return std::string(buffer, result.ptr);
}

/// C# ToString() uses uppercase E, an explicit exponent sign and at least two
/// exponent digits: 1E+20, 1E-05. std::to_chars emits 1e+20 / 1e-05.
std::string normalizeCSharpExponent(std::string text) {
    auto exponent = text.find('e');
    if (exponent == std::string::npos) return text;
    text[exponent] = 'E';
    std::size_t position = exponent + 1;
    if (position >= text.size() || (text[position] != '+' && text[position] != '-')) {
        text.insert(position, "+");
    }
    position = exponent + 2;
    std::size_t digits = text.size() - position;
    if (digits < 2) text.insert(position, 2 - digits, '0');
    return text;
}

std::string formatDoubleCSharp(double value) {
    if (std::isnan(value)) return "NaN";
    if (std::isinf(value)) return value < 0 ? "-Infinity" : "Infinity";
    char buffer[64];
    auto result = std::to_chars(buffer, buffer + sizeof(buffer), value);
    return normalizeCSharpExponent(std::string(buffer, result.ptr));
}

std::string formatFloatCSharp(float value) {
    if (std::isnan(value)) return "NaN";
    if (std::isinf(value)) return value < 0 ? "-Infinity" : "Infinity";
    char buffer[64];
    auto result = std::to_chars(buffer, buffer + sizeof(buffer), value);
    return normalizeCSharpExponent(std::string(buffer, result.ptr));
}

std::u16string asciiToUtf16(const std::string& text) { return utf8ToUtf16(text); }

/// Field-wise comparison used by array helpers. Operator == remains a compile
/// error for structs, matching C# (no user-defined operators in this language).
bool structValuesEqual(const Value& a, const Value& b) {
    auto left = a.asStruct();
    auto right = b.asStruct();
    if (!left || !right) return left == right;
    if (left->type() != right->type()) return false;
    if (left->fields().size() != right->fields().size()) return false;
    for (std::size_t i = 0; i < left->fields().size(); ++i) {
        const Value& l = left->fields()[i];
        const Value& r = right->fields()[i];
        if (l.kind() == ValueKind::Struct || r.kind() == ValueKind::Struct) {
            if (!structValuesEqual(l, r)) return false;
        } else if (!ValueOps::areEqual(l, r)) {
            return false;
        }
    }
    return true;
}

/// FNV-1a over UTF-16 code units: stable, allocation free and independent of
/// the C++ standard library's string hash implementation.
std::uint64_t hashText(const std::u16string& text) {
    std::uint64_t result = 1469598103934665603ull;
    for (char16_t unit : text) {
        result ^= static_cast<std::uint64_t>(unit);
        result *= 1099511628211ull;
    }
    return result;
}

std::uint64_t hashValue(const Value& value, int depth) {
    switch (value.kind()) {
        case ValueKind::Void:
        case ValueKind::Null: return 0;
        case ValueKind::String: return hashText(value.asString());
        case ValueKind::Bool:
        case ValueKind::Char:
        case ValueKind::Int8:
        case ValueKind::UInt8:
        case ValueKind::Int16:
        case ValueKind::UInt16:
        case ValueKind::Int32:
        case ValueKind::UInt32:
        case ValueKind::Int64:
        case ValueKind::UInt64: return static_cast<std::uint64_t>(value.asI64());
        case ValueKind::Float32:
        case ValueKind::Float64: {
            // `compare` treats -0.0 and 0.0 as equal, so they must hash alike.
            const double number = value.asF64();
            if (number == 0.0) return 0;
            return Value::bitsOf(number);
        }
        case ValueKind::Struct: {
            auto structValue = value.asStruct();
            if (!structValue) return 0;
            std::uint64_t result = reinterpret_cast<std::uintptr_t>(structValue->type());
            if (depth > 16) return result;
            for (const Value& field : structValue->fields()) {
                result = (result ^ hashValue(field, depth + 1)) * 1099511628211ull;
            }
            return result;
        }
        case ValueKind::Delegate:
            // Delegates compare structurally; a constant keeps the "equal values
            // hash equally" contract and only costs collisions.
            return 0x9e3779b97f4a7c15ull;
        case ValueKind::Object:
        case ValueKind::Array:
        case ValueKind::Native:
        case ValueKind::Cell:
        case ValueKind::Ref:
        default: return reinterpret_cast<std::uintptr_t>(value.rawRef().get());
    }
}

}  // namespace

bool isNumericKind(ValueKind kind) {
    switch (kind) {
        case ValueKind::Int8:
        case ValueKind::UInt8:
        case ValueKind::Int16:
        case ValueKind::UInt16:
        case ValueKind::Int32:
        case ValueKind::UInt32:
        case ValueKind::Int64:
        case ValueKind::UInt64:
        case ValueKind::Float32:
        case ValueKind::Float64:
        case ValueKind::Char: return true;
        default: return false;
    }
}

bool isIntegralKind(ValueKind kind) {
    switch (kind) {
        case ValueKind::Int8:
        case ValueKind::UInt8:
        case ValueKind::Int16:
        case ValueKind::UInt16:
        case ValueKind::Int32:
        case ValueKind::UInt32:
        case ValueKind::Int64:
        case ValueKind::UInt64:
        case ValueKind::Char: return true;
        default: return false;
    }
}

ScriptInstance::ScriptInstance(ScriptClassType* cls, GcCollector& collector) : cls_(cls) {
    fields_.resize(cls->fields.size());
    for (std::size_t i = 0; i < cls->fields.size(); ++i) {
        fields_[i] = defaultValue(cls->fields[i].type, collector);
    }
}

GcObject* Value::gcObject() const {
    if (kind_ == ValueKind::Object) return static_cast<ScriptInstance*>(ref_.get());
    if (kind_ == ValueKind::Array) return static_cast<ScriptArray*>(ref_.get());
    if (kind_ == ValueKind::Struct) return static_cast<ScriptStructValue*>(ref_.get());
    if (kind_ == ValueKind::Delegate) return static_cast<ScriptDelegate*>(ref_.get());
    if (kind_ == ValueKind::Cell) return static_cast<ScriptCell*>(ref_.get());
    if (kind_ == ValueKind::Ref) return static_cast<ScriptRef*>(ref_.get());
    return nullptr;
}

Value ScriptRef::load() const {
    switch (refKind) {
        case RefKind::Cell: return cell ? cell->value : Value::null();
        case RefKind::Global: return global != nullptr ? *global : Value::null();
        case RefKind::Field: {
            if (auto instance = owner.asInstance()) {
                if (index >= 0 && index < static_cast<int>(instance->fields().size()))
                    return instance->fields()[static_cast<std::size_t>(index)];
            } else if (auto structValue = owner.asStruct()) {
                if (index >= 0 && index < static_cast<int>(structValue->fields().size()))
                    return structValue->fields()[static_cast<std::size_t>(index)];
            }
            return Value::null();
        }
        case RefKind::Elem: {
            auto array = owner.asArray();
            if (array && index >= 0 && index < static_cast<int>(array->items().size()))
                return array->items()[static_cast<std::size_t>(index)];
            return Value::null();
        }
    }
    return Value::null();
}

void ScriptRef::store(const Value& value) {
    switch (refKind) {
        case RefKind::Cell:
            if (cell) cell->value = value;
            break;
        case RefKind::Global:
            if (global != nullptr) *global = value;
            break;
        case RefKind::Field: {
            if (auto instance = owner.asInstance()) {
                if (index >= 0 && index < static_cast<int>(instance->fields().size()))
                    instance->fields()[static_cast<std::size_t>(index)] = value;
            } else if (auto structValue = owner.asStruct()) {
                if (index >= 0 && index < static_cast<int>(structValue->fields().size()))
                    structValue->fields()[static_cast<std::size_t>(index)] = value;
            }
            break;
        }
        case RefKind::Elem: {
            auto array = owner.asArray();
            if (array && index >= 0 && index < static_cast<int>(array->items().size()))
                array->items()[static_cast<std::size_t>(index)] = value;
            break;
        }
    }
}

void ScriptDelegate::gcEnumerateReferences(GcCollector& collector) {
    for (const auto& entry : entries_) {
        collector.enumerateValue(entry.target);
        for (const auto& capture : entry.captures) collector.enumerateValue(capture);
    }
}

void ScriptDelegate::gcReleaseReferences() { entries_.clear(); }

void ScriptStructValue::gcEnumerateReferences(GcCollector& collector) {
    for (const auto& field : fields_) collector.enumerateValue(field);
}

void ScriptStructValue::gcReleaseReferences() { fields_.clear(); }

Value makeDefaultStructValue(ScriptClassType& type, GcCollector& collector) {
    auto value = collector.make<ScriptStructValue>(&type);
    value->fields().resize(type.fields.size());
    for (std::size_t i = 0; i < type.fields.size(); ++i) {
        value->fields()[i] = defaultValue(type.fields[i].type, collector);
    }
    return Value::structValue(std::move(value));
}

Value cloneStructValue(const Value& value, GcCollector& collector) {
    auto source = value.asStruct();
    if (!source || source->type() == nullptr) return value;
    auto* type = source->type();
    auto copy = collector.make<ScriptStructValue>(type);
    copy->fields().resize(source->fields().size());
    for (std::size_t i = 0; i < source->fields().size(); ++i) {
        const auto& field = source->fields()[i];
        if (i < type->fields.size() && type->fields[i].type.isStruct()) {
            copy->fields()[i] = cloneStructValue(field, collector);
        } else {
            copy->fields()[i] = field;
        }
    }
    return Value::structValue(std::move(copy));
}

void ScriptInstance::gcEnumerateReferences(GcCollector& collector) {
    for (const auto& field : fields_) collector.enumerateValue(field);
}

void ScriptInstance::gcReleaseReferences() { fields_.clear(); }

bool ScriptInstance::gcNeedsFinalizer() const {
    return !finalized_ && cls_ != nullptr && cls_->hasFinalizer();
}

void ScriptArray::gcEnumerateReferences(GcCollector& collector) {
    for (const auto& item : items_) collector.enumerateValue(item);
}

void ScriptArray::gcReleaseReferences() { items_.clear(); }

std::string Value::typeName() const {
    switch (kind_) {
        case ValueKind::Void: return "void";
        case ValueKind::Null: return "null";
        case ValueKind::Bool: return "bool";
        case ValueKind::Int8: return "sbyte";
        case ValueKind::UInt8: return "byte";
        case ValueKind::Int16: return "short";
        case ValueKind::UInt16: return "ushort";
        case ValueKind::Int32: return "int";
        case ValueKind::UInt32: return "uint";
        case ValueKind::Int64: return "long";
        case ValueKind::UInt64: return "ulong";
        case ValueKind::Float32: return "float";
        case ValueKind::Float64: return "double";
        case ValueKind::Char: return "char";
        case ValueKind::String: return "string";
        case ValueKind::Object: {
            auto instance = asInstance();
            return instance ? instance->cls()->name : "object";
        }
        case ValueKind::Array: {
            auto array = asArray();
            return array && array->elementType() ? array->elementType()->name + "[]" : "array";
        }
        case ValueKind::Struct: {
            auto value = asStruct();
            return value && value->type() != nullptr ? value->type()->name : "struct";
        }
        case ValueKind::Delegate: {
            auto value = asDelegate();
            return value && value->type() != nullptr ? value->type()->name : "delegate";
        }
        case ValueKind::Cell: return "<cell>";
        case ValueKind::Ref: return "<ref>";
        case ValueKind::Native: return "<native>";
    }
    return "<unknown>";
}

std::string Value::display() const {
    switch (kind_) {
        case ValueKind::Void: return "";
        case ValueKind::Null: return "null";
        case ValueKind::Bool: return bits_ != 0 ? "true" : "false";
        case ValueKind::String: return utf16ToUtf8(asString());
        case ValueKind::Char: return utf16ToUtf8(std::u16string(1, asChar()));
        case ValueKind::Float32: return numberToString(static_cast<float>(asF64()));
        case ValueKind::Float64: return numberToString(asF64());
        case ValueKind::UInt64: return numberToString(static_cast<std::uint64_t>(bits_));
        case ValueKind::Object: {
            auto instance = asInstance();
            if (!instance) return "null";
            std::ostringstream out;
            out << instance->cls()->name << "@" << instance.get();
            return out.str();
        }
        case ValueKind::Array: {
            auto array = asArray();
            if (!array) return "null";
            return (array->elementType() ? array->elementType()->name : std::string("array")) + "[" +
                   std::to_string(array->size()) + "]";
        }
        case ValueKind::Struct: {
            auto value = asStruct();
            return value && value->type() != nullptr ? value->type()->name : "struct";
        }
        case ValueKind::Delegate: {
            auto value = asDelegate();
            return value && value->type() != nullptr ? value->type()->name : "delegate";
        }
        case ValueKind::Cell: return "<cell>";
        case ValueKind::Ref: return "<ref>";
        case ValueKind::Native: return "<native>";
        default: return numberToString(static_cast<long long>(bits_));
    }
}

Value Value::stringUtf8(const std::string& value) { return Value::string(utf8ToUtf16(value)); }

std::u16string Value::toScriptString() const {
    switch (kind_) {
        case ValueKind::Void:
        case ValueKind::Null: return u"";
        case ValueKind::Bool: return bits_ != 0 ? u"True" : u"False";
        case ValueKind::String: return asString();
        case ValueKind::Char: return std::u16string(1, asChar());
        case ValueKind::Float32: return asciiToUtf16(formatFloatCSharp(static_cast<float>(asF64())));
        case ValueKind::Float64: return asciiToUtf16(formatDoubleCSharp(asF64()));
        case ValueKind::UInt64: return asciiToUtf16(numberToString(static_cast<std::uint64_t>(bits_)));
        case ValueKind::Object: {
            auto instance = asInstance();
            return instance ? asciiToUtf16(instance->cls()->name) : u"";
        }
        case ValueKind::Array: return asciiToUtf16(typeName());
        case ValueKind::Struct: {
            auto value = asStruct();
            return asciiToUtf16(value && value->type() != nullptr ? value->type()->name : "struct");
        }
        case ValueKind::Delegate: {
            auto value = asDelegate();
            return asciiToUtf16(value && value->type() != nullptr ? value->type()->name : "delegate");
        }
        case ValueKind::Cell: return u"<cell>";
        case ValueKind::Ref: return u"<ref>";
        case ValueKind::Native: return u"<native>";
        default: return asciiToUtf16(numberToString(static_cast<long long>(bits_)));
    }
}

namespace ValueOps {

Value addI32(const Value& a, const Value& b) {
    return Value::i32(static_cast<std::int32_t>(a.asI64() + b.asI64()));
}
Value subI32(const Value& a, const Value& b) {
    return Value::i32(static_cast<std::int32_t>(a.asI64() - b.asI64()));
}
Value mulI32(const Value& a, const Value& b) {
    return Value::i32(static_cast<std::int32_t>(a.asI64() * b.asI64()));
}
Value divI32(const Value& a, const Value& b) {
    if (b.asI64() == 0) throw std::runtime_error("integer division by zero");
    return Value::i32(static_cast<std::int32_t>(a.asI64() / b.asI64()));
}
Value modI32(const Value& a, const Value& b) {
    if (b.asI64() == 0) throw std::runtime_error("integer modulo by zero");
    return Value::i32(static_cast<std::int32_t>(a.asI64() % b.asI64()));
}
Value addI64(const Value& a, const Value& b) { return Value::i64(a.asI64() + b.asI64()); }
Value subI64(const Value& a, const Value& b) { return Value::i64(a.asI64() - b.asI64()); }
Value mulI64(const Value& a, const Value& b) { return Value::i64(a.asI64() * b.asI64()); }
Value divI64(const Value& a, const Value& b) {
    if (b.asI64() == 0) throw std::runtime_error("integer division by zero");
    return Value::i64(a.asI64() / b.asI64());
}
Value modI64(const Value& a, const Value& b) {
    if (b.asI64() == 0) throw std::runtime_error("integer modulo by zero");
    return Value::i64(a.asI64() % b.asI64());
}

Value addF32(const Value& a, const Value& b) { return Value::f32(static_cast<float>(a.asF64() + b.asF64())); }
Value subF32(const Value& a, const Value& b) { return Value::f32(static_cast<float>(a.asF64() - b.asF64())); }
Value mulF32(const Value& a, const Value& b) { return Value::f32(static_cast<float>(a.asF64() * b.asF64())); }
Value divF32(const Value& a, const Value& b) { return Value::f32(static_cast<float>(a.asF64() / b.asF64())); }
Value modF32(const Value& a, const Value& b) { return Value::f32(static_cast<float>(std::fmod(a.asF64(), b.asF64()))); }
Value addF64(const Value& a, const Value& b) { return Value::f64(a.asF64() + b.asF64()); }
Value subF64(const Value& a, const Value& b) { return Value::f64(a.asF64() - b.asF64()); }
Value mulF64(const Value& a, const Value& b) { return Value::f64(a.asF64() * b.asF64()); }
Value divF64(const Value& a, const Value& b) { return Value::f64(a.asF64() / b.asF64()); }
Value modF64(const Value& a, const Value& b) { return Value::f64(std::fmod(a.asF64(), b.asF64())); }

Value addString(const Value& a, const Value& b) {
    // C# string.Concat / operator+ treats null operands as empty strings and
    // uses object.ToString() for non-string operands.
    return Value::string(a.toScriptString() + b.toScriptString());
}

Value bAndI32(const Value& a, const Value& b) {
    return Value::i32(static_cast<std::int32_t>(a.asI64() & b.asI64()));
}
Value bOrI32(const Value& a, const Value& b) {
    return Value::i32(static_cast<std::int32_t>(a.asI64() | b.asI64()));
}
Value bXorI32(const Value& a, const Value& b) {
    return Value::i32(static_cast<std::int32_t>(a.asI64() ^ b.asI64()));
}
Value bShlI32(const Value& a, const Value& b) {
    return Value::i32(static_cast<std::int32_t>(static_cast<std::uint32_t>(a.asI64())
                                               << (b.asI64() & 31)));
}
Value bShrI32(const Value& a, const Value& b) {
    return Value::i32(static_cast<std::int32_t>(a.asI32() >> (b.asI64() & 31)));
}
Value bShrUI32(const Value& a, const Value& b) {
    return Value::i32(static_cast<std::int32_t>(static_cast<std::uint32_t>(a.asI32()) >>
                                               (b.asI64() & 31)));
}
Value bAndI64(const Value& a, const Value& b) { return Value::i64(a.asI64() & b.asI64()); }
Value bOrI64(const Value& a, const Value& b) { return Value::i64(a.asI64() | b.asI64()); }
Value bXorI64(const Value& a, const Value& b) { return Value::i64(a.asI64() ^ b.asI64()); }
Value bShlI64(const Value& a, const Value& b) {
    return Value::i64(static_cast<std::int64_t>(static_cast<std::uint64_t>(a.asI64())
                                                << (b.asI64() & 63)));
}
Value bShrI64(const Value& a, const Value& b) { return Value::i64(a.asI64() >> (b.asI64() & 63)); }
Value bShrUI64(const Value& a, const Value& b) {
    return Value::u64(a.asU64() >> (b.asI64() & 63));
}

Value negI32(const Value& a) { return Value::i32(static_cast<std::int32_t>(-a.asI32())); }
Value negI64(const Value& a) { return Value::i64(-a.asI64()); }
Value negF32(const Value& a) { return Value::f32(-static_cast<float>(a.asF64())); }
Value negF64(const Value& a) { return Value::f64(-a.asF64()); }
Value logicalNot(const Value& a) { return Value::boolean(!a.asBool()); }
Value bNotI32(const Value& a) { return Value::i32(~a.asI32()); }
Value bNotI64(const Value& a) { return Value::i64(~a.asI64()); }

int compare(const Value& a, const Value& b) {
    if (a.kind() == ValueKind::String || b.kind() == ValueKind::String) {
        std::u16string left = a.kind() == ValueKind::String ? a.asString() : a.toScriptString();
        std::u16string right = b.kind() == ValueKind::String ? b.asString() : b.toScriptString();
        int result = left.compare(right);
        return result < 0 ? -1 : result > 0 ? 1 : 0;
    }
    if (a.kind() == ValueKind::Bool && b.kind() == ValueKind::Bool) {
        return static_cast<int>(a.asBool()) - static_cast<int>(b.asBool());
    }
    if (a.kind() == ValueKind::Struct || b.kind() == ValueKind::Struct) {
        return structValuesEqual(a, b) ? 0 : 1;
    }
    if (a.isReference() || b.isReference()) {
        if (a.isNull() && b.isNull()) return 0;
        if (a.isNull()) return -1;
        if (b.isNull()) return 1;
        return a.rawRef() == b.rawRef() ? 0 : 1;
    }
    if (isIntegralKind(a.kind()) && isIntegralKind(b.kind())) {
        bool aUnsigned = a.kind() == ValueKind::UInt64 || a.kind() == ValueKind::UInt32 ||
                         a.kind() == ValueKind::UInt16 || a.kind() == ValueKind::UInt8;
        bool bUnsigned = b.kind() == ValueKind::UInt64 || b.kind() == ValueKind::UInt32 ||
                         b.kind() == ValueKind::UInt16 || b.kind() == ValueKind::UInt8;
        if (aUnsigned && bUnsigned) {
            std::uint64_t left = a.asU64();
            std::uint64_t right = b.asU64();
            return left < right ? -1 : left > right ? 1 : 0;
        }
        std::int64_t left = a.asI64();
        std::int64_t right = b.asI64();
        return left < right ? -1 : left > right ? 1 : 0;
    }
    double left = a.asF64();
    double right = b.asF64();
    return left < right ? -1 : left > right ? 1 : 0;
}

bool areEqual(const Value& a, const Value& b) {
    if (a.isNull() && b.isNull()) return true;
    if (a.isNull() || b.isNull()) return false;
    if (a.kind() == ValueKind::String || b.kind() == ValueKind::String) {
        return a.kind() == ValueKind::String && b.kind() == ValueKind::String &&
               a.asString() == b.asString();
    }
    if (a.kind() == ValueKind::Struct || b.kind() == ValueKind::Struct) {
        return structValuesEqual(a, b);
    }
    if (a.kind() == ValueKind::Delegate || b.kind() == ValueKind::Delegate) {
        auto left = a.asDelegate();
        auto right = b.asDelegate();
        if (!left || !right) return false;
        if (left->type() != right->type()) return false;
        if (left->entries().size() != right->entries().size()) return false;
        for (std::size_t i = 0; i < left->entries().size(); ++i) {
            const auto& le = left->entries()[i];
            const auto& re = right->entries()[i];
            if (le.function != re.function || !areEqual(le.target, re.target)) return false;
            if (le.captures.size() != re.captures.size()) return false;
            for (std::size_t c = 0; c < le.captures.size(); ++c) {
                if (!areEqual(le.captures[c], re.captures[c])) return false;
            }
        }
        return true;
    }
    if (a.kind() == ValueKind::Cell || b.kind() == ValueKind::Cell ||
        a.kind() == ValueKind::Ref || b.kind() == ValueKind::Ref) {
        return a.rawRef() == b.rawRef();
    }
    if (a.isReference() || b.isReference()) {
        return a.rawRef() == b.rawRef();
    }
    return compare(a, b) == 0;
}

std::uint64_t hash(const Value& value) { return hashValue(value, 0); }

}  // namespace ValueOps

}  // namespace sharpscript
