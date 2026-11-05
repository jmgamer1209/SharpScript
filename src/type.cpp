#include "sharpscript/type.hpp"

#include "sharpscript/function.hpp"

namespace sharpscript {
namespace {

const ScriptType kVar("var", Primitive::Var);

}  // namespace

const ScriptType& primVoid() {
    static const ScriptType type("void", Primitive::Void);
    return type;
}
const ScriptType& primNull() {
    static const ScriptType type("null", Primitive::Null);
    return type;
}
const ScriptType& primBool() {
    static const ScriptType type("bool", Primitive::Bool);
    return type;
}
const ScriptType& primI8() {
    static const ScriptType type("sbyte", Primitive::Int8);
    return type;
}
const ScriptType& primI16() {
    static const ScriptType type("short", Primitive::Int16);
    return type;
}
const ScriptType& primI32() {
    static const ScriptType type("int", Primitive::Int32);
    return type;
}
const ScriptType& primI64() {
    static const ScriptType type("long", Primitive::Int64);
    return type;
}
const ScriptType& primU8() {
    static const ScriptType type("byte", Primitive::UInt8);
    return type;
}
const ScriptType& primU16() {
    static const ScriptType type("ushort", Primitive::UInt16);
    return type;
}
const ScriptType& primU32() {
    static const ScriptType type("uint", Primitive::UInt32);
    return type;
}
const ScriptType& primU64() {
    static const ScriptType type("ulong", Primitive::UInt64);
    return type;
}
const ScriptType& primF32() {
    static const ScriptType type("float", Primitive::Float32);
    return type;
}
const ScriptType& primF64() {
    static const ScriptType type("double", Primitive::Float64);
    return type;
}
const ScriptType& primChar() {
    static const ScriptType type("char", Primitive::Char);
    return type;
}
const ScriptType& primString() {
    static const ScriptType type("string", Primitive::String);
    return type;
}
const ScriptType& primVar() { return kVar; }

const ScriptType& primError() {
    static const ScriptType type = [] {
        ScriptType value("<error>", Primitive::None);
        value.error = true;
        return value;
    }();
    return type;
}

bool ScriptType::isClass() const {
    return object != nullptr && dynamic_cast<ScriptClassType*>(object) != nullptr;
}

bool ScriptType::isStruct() const {
    auto* cls = dynamic_cast<ScriptClassType*>(object);
    return cls != nullptr && cls->isStruct;
}

bool ScriptType::isDelegate() const {
    return dynamic_cast<ScriptDelegateType*>(object) != nullptr;
}

bool ScriptType::sameAs(const ScriptType& other) const {
    if (this == &other) return true;
    if (name != other.name) return false;
    if (isArray() != other.isArray()) return false;
    if (!isArray()) return true;
    return element->sameAs(*other.element);
}

bool ScriptType::identityOrReferenceAssignableFrom(const ScriptType& other) const {
    if (sameAs(other)) return true;
    return isReference() && other.isReference() && !isNullType() && !other.isNullType() &&
           assignableFrom(other);
}

bool ScriptType::assignableFrom(const ScriptType& other) const {
    if (sameAs(other)) return true;
    // After a type error the value is "any type": never report conversions.
    if (isErrorType() || other.isErrorType()) return true;
    if (isVar() || other.isVar()) return true;
    if (other.isNullType()) return isReference();
    if (isArray() || other.isArray()) {
        return isArray() && other.isArray() && element->sameAs(*other.element);
    }
    if (isNumeric() && other.isNumeric()) {
        return canImplicitNumeric(other.primitive, primitive);
    }
    if (object != nullptr && other.object != nullptr) {
        return object->isAssignableFrom(other.object);
    }
    return false;
}

bool ScriptType::canImplicitNumeric(Primitive from, Primitive to) {
    if (from == to) return true;
    switch (to) {
        case Primitive::Int16:
            return from == Primitive::Int8 || from == Primitive::UInt8;
        case Primitive::Int32:
            return from == Primitive::Int8 || from == Primitive::UInt8 || from == Primitive::Int16 ||
                   from == Primitive::UInt16 || from == Primitive::Char;
        case Primitive::Int64:
            return from == Primitive::Int8 || from == Primitive::UInt8 || from == Primitive::Int16 ||
                   from == Primitive::UInt16 || from == Primitive::Int32 || from == Primitive::UInt32 ||
                   from == Primitive::Char;
        case Primitive::UInt16:
            return from == Primitive::UInt8 || from == Primitive::Char;
        case Primitive::UInt32:
            return from == Primitive::UInt8 || from == Primitive::UInt16 || from == Primitive::Char;
        case Primitive::UInt64:
            return from == Primitive::UInt8 || from == Primitive::UInt16 || from == Primitive::UInt32 ||
                   from == Primitive::Char;
        case Primitive::Float32:
            return from == Primitive::Int8 || from == Primitive::UInt8 || from == Primitive::Int16 ||
                   from == Primitive::UInt16 || from == Primitive::Int32 || from == Primitive::UInt32 ||
                   from == Primitive::Int64 || from == Primitive::UInt64 || from == Primitive::Char;
        case Primitive::Float64:
            return from == Primitive::Int8 || from == Primitive::UInt8 || from == Primitive::Int16 ||
                   from == Primitive::UInt16 || from == Primitive::Int32 || from == Primitive::UInt32 ||
                   from == Primitive::Int64 || from == Primitive::UInt64 || from == Primitive::Char ||
                   from == Primitive::Float32;
        default: return false;
    }
}

bool ScriptType::canExplicitNumeric(Primitive from, Primitive to) {
    auto excluded = [](Primitive p) {
        return p == Primitive::None || p == Primitive::Void || p == Primitive::Null ||
               p == Primitive::String || p == Primitive::Var;
    };
    return !excluded(from) && !excluded(to);
}

const ScriptClassType::PropertyInfo* ScriptClassType::findProperty(const std::string& propertyName) const {
    for (const ScriptClassType* current = this; current != nullptr; current = current->base) {
        auto it = current->properties.find(propertyName);
        if (it != current->properties.end()) return &it->second;
    }
    return nullptr;
}

bool ScriptClassType::hasFinalizer() const {
    for (const ScriptClassType* current = this; current != nullptr; current = current->base) {
        if (current->finalizer != nullptr) return true;
    }
    return false;
}

FieldInfo* ScriptClassType::findField(const std::string& fieldName) {
    for (ScriptClassType* current = this; current != nullptr; current = current->base) {
        auto it = current->fieldIndex.find(fieldName);
        if (it != current->fieldIndex.end()) return &current->fields[it->second];
    }
    return nullptr;
}

std::vector<ScriptFunction*> ScriptClassType::findMethods(const std::string& methodName, std::size_t arity,
                                                          bool wantStatic) const {
    for (const ScriptClassType* current = this; current != nullptr; current = current->base) {
        auto it = current->methods.find(methodName);
        if (it == current->methods.end()) continue;
        std::vector<ScriptFunction*> found;
        for (auto* method : it->second) {
            if (method->isStatic == wantStatic && method->parameters.size() == arity) found.push_back(method);
        }
        if (!found.empty()) return found;
    }
    return {};
}

bool ScriptDelegateType::isAssignableFrom(const ScriptObjectType* other) const {
    if (other == this) return true;
    const auto* source = dynamic_cast<const ScriptDelegateType*>(other);
    if (source == nullptr) return false;
    if (templateName.empty() || source->templateName != templateName) return false;
    if (source->typeArguments.size() != typeArguments.size()) return false;
    if (parameterVariances.size() != typeArguments.size()) return false;
    for (std::size_t i = 0; i < typeArguments.size(); ++i) {
        switch (parameterVariances[i]) {
            case Variance::None:
                if (!typeArguments[i].sameAs(source->typeArguments[i])) return false;
                break;
            case Variance::Out:
                // `Factory<Animal>` accepts `Factory<Dog>`.
                if (!typeArguments[i].identityOrReferenceAssignableFrom(source->typeArguments[i])) return false;
                break;
            case Variance::In:
                // `Consumer<Dog>` accepts `Consumer<Animal>`.
                if (!source->typeArguments[i].identityOrReferenceAssignableFrom(typeArguments[i])) return false;
                break;
        }
    }
    return true;
}

bool ScriptClassType::isAssignableFrom(const ScriptObjectType* other) const {
    if (other == this) return true;
    auto* otherClass = dynamic_cast<const ScriptClassType*>(other);
    if (otherClass == nullptr) return false;
    for (auto* current = otherClass; current != nullptr; current = current->base) {
        if (current == this) return true;
    }
    return false;
}

std::string NativeFunction::toString() const {
    std::ostringstream out;
    out << name << "(";
    for (std::size_t i = 0; i < parameterTypes.size(); ++i) {
        if (i > 0) out << ", ";
        out << parameterTypes[i].name;
    }
    if (isVariadic) out << (parameterTypes.empty() ? "..." : ", ...");
    out << ")";
    return out.str();
}

NativeFunction* NativeProperty::getterFunction() {
    if (!getter) return nullptr;
    if (!getterFunction_) {
        getterFunction_ = std::make_unique<NativeFunction>();
        getterFunction_->name = "get_" + name;
        getterFunction_->returnType = type;
        getterFunction_->isStatic = isStatic;
        getterFunction_->invoke = [this](ExecutionContext&, const Value& self, const std::vector<Value>&) {
            return getter(self);
        };
    }
    return getterFunction_.get();
}

NativeFunction* NativeProperty::setterFunction() {
    if (!setter) return nullptr;
    if (!setterFunction_) {
        setterFunction_ = std::make_unique<NativeFunction>();
        setterFunction_->name = "set_" + name;
        setterFunction_->returnType = primVoid();
        setterFunction_->parameterTypes = {type};
        setterFunction_->isStatic = isStatic;
        setterFunction_->invoke = [this](ExecutionContext&, const Value& self, const std::vector<Value>& args) {
            setter(self, args.empty() ? Value::null() : args[0]);
            return Value::voidValue();
        };
    }
    return setterFunction_.get();
}

NativeFunction* NativeObjectType::addMethod(const std::string& methodName, const ScriptType& returnType,
                                            std::vector<ScriptType> parameterTypes, NativeCall call,
                                            bool isStaticMethod) {
    methodStorage_.emplace_back();
    auto& method = methodStorage_.back();
    method.name = methodName;
    method.returnType = returnType;
    method.parameterTypes = std::move(parameterTypes);
    method.isStatic = isStaticMethod;
    method.invoke = std::move(call);
    methods[methodName].push_back(&method);
    return &method;
}

NativeFunction* NativeObjectType::addConstructor(std::vector<ScriptType> parameterTypes, NativeCall call) {
    methodStorage_.emplace_back();
    auto& method = methodStorage_.back();
    method.name = name;
    method.returnType = ScriptType(name, Primitive::None, this);
    method.parameterTypes = std::move(parameterTypes);
    method.isStatic = true;
    method.isConstructor = true;
    method.invoke = std::move(call);
    constructors.push_back(&method);
    if (constructor == nullptr) constructor = &method;
    return &method;
}

NativeProperty* NativeObjectType::addProperty(const std::string& propertyName,
                                               const ScriptType& propertyType,
                                               std::function<Value(const Value&)> getter,
                                               std::function<void(const Value&, const Value&)> setter,
                                               bool isStaticProperty) {
    auto property = std::make_unique<NativeProperty>();
    property->name = propertyName;
    property->type = propertyType;
    property->isStatic = isStaticProperty;
    property->getter = std::move(getter);
    property->setter = std::move(setter);
    properties[propertyName] = std::move(property);
    return properties[propertyName].get();
}

NativeFunction* NativeObjectType::findMethod(const std::string& methodName, std::size_t arity) {
    for (auto* type = this; type != nullptr; type = type->base) {
        auto it = type->methods.find(methodName);
        if (it == type->methods.end()) continue;
        for (auto* method : it->second) {
            if (method->matchesArity(arity)) return method;
        }
    }
    return nullptr;
}

NativeFunction* NativeObjectType::findConstructor(std::size_t arity) const {
    for (auto* constructorFunction : constructors) {
        if (constructorFunction->parameterTypes.size() == arity) return constructorFunction;
    }
    return nullptr;
}

NativeProperty* NativeObjectType::findProperty(const std::string& propertyName) {
    for (auto* type = this; type != nullptr; type = type->base) {
        auto it = type->properties.find(propertyName);
        if (it != type->properties.end()) return it->second.get();
    }
    return nullptr;
}

bool NativeObjectType::isAssignableFrom(const ScriptObjectType* other) const {
    auto* native = dynamic_cast<const NativeObjectType*>(other);
    for (auto* type = native; type != nullptr; type = type->base) {
        if (type == this) return true;
    }
    return false;
}

std::string primitiveName(Primitive primitive) {
    switch (primitive) {
        case Primitive::None: return "<none>";
        case Primitive::Void: return "void";
        case Primitive::Null: return "null";
        case Primitive::Bool: return "bool";
        case Primitive::Int8: return "sbyte";
        case Primitive::Int16: return "short";
        case Primitive::Int32: return "int";
        case Primitive::Int64: return "long";
        case Primitive::UInt8: return "byte";
        case Primitive::UInt16: return "ushort";
        case Primitive::UInt32: return "uint";
        case Primitive::UInt64: return "ulong";
        case Primitive::Float32: return "float";
        case Primitive::Float64: return "double";
        case Primitive::Char: return "char";
        case Primitive::String: return "string";
        case Primitive::Var: return "var";
    }
    return "<unknown>";
}

Value defaultValue(const ScriptType& type, GcCollector& collector) {
    if (type.isVoid()) return Value::voidValue();
    if (type.isStruct()) {
        auto* cls = dynamic_cast<ScriptClassType*>(type.object);
        if (cls == nullptr) return Value::null();
        return makeDefaultStructValue(*cls, collector);
    }
    if (type.isArray() || type.isString() || type.object != nullptr || type.isNullType() ||
        type.isVar()) {
        return Value::null();
    }
    switch (type.primitive) {
        case Primitive::Bool: return Value::boolean(false);
        case Primitive::Int8: return Value::i8(0);
        case Primitive::Int16: return Value::i16(0);
        case Primitive::Int32: return Value::i32(0);
        case Primitive::Int64: return Value::i64(0);
        case Primitive::UInt8: return Value::u8(0);
        case Primitive::UInt16: return Value::u16(0);
        case Primitive::UInt32: return Value::u32(0);
        case Primitive::UInt64: return Value::u64(0);
        case Primitive::Float32: return Value::f32(0.0f);
        case Primitive::Float64: return Value::f64(0.0);
        case Primitive::Char: return Value::character(u'\0');
        default: return Value::null();
    }
}

}  // namespace sharpscript
