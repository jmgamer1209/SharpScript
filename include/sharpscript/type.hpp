#pragma once

#include <deque>

#include "value.hpp"

namespace sharpscript {

enum class Primitive {
    None,
    Void,
    Null,
    Bool,
    Int8,
    Int16,
    Int32,
    Int64,
    UInt8,
    UInt16,
    UInt32,
    UInt64,
    Float32,
    Float64,
    Char,
    String,
    Var
};

class ScriptType {
public:
    std::string name;
    Primitive primitive = Primitive::None;
    ScriptObjectType* object = nullptr;
    std::shared_ptr<const ScriptType> element;
    /// Concrete type arguments of an instantiated generic type (`Box<int>`).
    std::vector<ScriptType> typeArguments;
    /// Template name for instantiated generic types (`Box`), empty otherwise.
    std::string templateName;

    ScriptType() = default;
    ScriptType(std::string name, Primitive primitive, ScriptObjectType* object = nullptr)
        : name(std::move(name)), primitive(primitive), object(object) {}

    bool isArray() const { return element != nullptr; }
    bool isVoid() const { return primitive == Primitive::Void; }
    bool isNullType() const { return primitive == Primitive::Null; }
    bool isVar() const { return primitive == Primitive::Var; }
    bool isPrimitive() const { return object == nullptr && !isArray(); }
    bool isBool() const { return primitive == Primitive::Bool; }
    bool isString() const { return primitive == Primitive::String; }
    bool isChar() const { return primitive == Primitive::Char; }
    bool isIntegral() const {
        switch (primitive) {
            case Primitive::Int8:
            case Primitive::Int16:
            case Primitive::Int32:
            case Primitive::Int64:
            case Primitive::UInt8:
            case Primitive::UInt16:
            case Primitive::UInt32:
            case Primitive::UInt64:
            case Primitive::Char: return true;
            default: return false;
        }
    }
    bool isFloat() const { return primitive == Primitive::Float32 || primitive == Primitive::Float64; }
    bool isNumeric() const { return isIntegral() || isFloat(); }
    bool isReference() const {
        return isArray() || isString() || (object != nullptr && !isStruct()) || isNullType();
    }
    bool isClass() const;
    /// Script `struct`: a value type (copy semantics, non-nullable, no inheritance).
    bool isStruct() const;
    /// Script `delegate` type (reference type holding callable entries).
    bool isDelegate() const;

    bool sameAs(const ScriptType& other) const;
    bool assignableFrom(const ScriptType& other) const;
    bool identityOrReferenceAssignableFrom(const ScriptType& other) const;

    static bool canImplicitNumeric(Primitive from, Primitive to);
    static bool canExplicitNumeric(Primitive from, Primitive to);
};

const ScriptType& primVoid();
const ScriptType& primNull();
const ScriptType& primBool();
const ScriptType& primI8();
const ScriptType& primI16();
const ScriptType& primI32();
const ScriptType& primI64();
const ScriptType& primU8();
const ScriptType& primU16();
const ScriptType& primU32();
const ScriptType& primU64();
const ScriptType& primF32();
const ScriptType& primF64();
const ScriptType& primChar();
const ScriptType& primString();
const ScriptType& primVar();

class ScriptObjectType {
public:
    std::string name;
    ScriptModule* module = nullptr;

    virtual ~ScriptObjectType() = default;
    virtual bool isAssignableFrom(const ScriptObjectType* other) const = 0;
};

struct FieldInfo {
    std::string name;
    ScriptType type;
    int index = 0;
    bool isReadOnly = false;
    ScriptClassType* owner = nullptr;
};

class ScriptClassType : public ScriptObjectType {
public:
    /// Enclosing namespace ("Game.AI" or empty); `name` is fully qualified.
    std::string namespacePath;
    ScriptClassType* base = nullptr;
    bool isAbstract = false;
    /// Value type (`struct`): no base, no vtable, copied by value at every
    /// assignment/argument/return boundary.
    bool isStruct = false;
    std::deque<FieldInfo> fields;
    std::unordered_map<std::string, int> fieldIndex;
    std::vector<ScriptFunction*> vtable;
    std::unordered_map<std::string, std::vector<ScriptFunction*>> methods;
    ScriptFunction* constructor = nullptr;
    /// `~Type()` declared by this class (nullptr when it has none).
    ScriptFunction* finalizer = nullptr;

    /// True when this class or any base class declares a finalizer.
    bool hasFinalizer() const;
    FieldInfo* findField(const std::string& fieldName);
    std::vector<ScriptFunction*> findMethods(const std::string& methodName, std::size_t arity, bool isStatic) const;
    bool isAssignableFrom(const ScriptObjectType* other) const override;
    int fieldCount() const { return static_cast<int>(fields.size()); }
};

/// Callable signature declared with `delegate R Name(params);`.
class ScriptDelegateType : public ScriptObjectType {
public:
    std::string namespacePath;
    std::vector<ScriptType> parameterTypes;
    ScriptType returnType;
    /// Variance declared on each type parameter of a generic delegate template.
    std::vector<Variance> parameterVariances;
    /// Template name of an instantiated generic delegate (`Factory`); empty otherwise.
    std::string templateName;
    /// Concrete type arguments of an instantiated generic delegate.
    std::vector<ScriptType> typeArguments;

    /// Delegates are assignable only through their own template: `Factory<Dog>`
    /// converts to `Factory<Animal>` when the parameter is declared `out` (and
    /// the other way around for `in`). Unrelated delegate types stay invariant.
    bool isAssignableFrom(const ScriptObjectType* other) const override;
};

using NativeCall =
    std::function<Value(ExecutionContext&, const Value&, const std::vector<Value>&)>;

class NativeFunction {
public:
    std::string name;
    ScriptType returnType;
    std::vector<ScriptType> parameterTypes;
    bool isStatic = false;
    bool isVariadic = false;
    bool isConstructor = false;
    NativeCall invoke;

    bool matchesArity(std::size_t count) const {
        return isVariadic || parameterTypes.size() == count;
    }
    std::string toString() const;
};

class NativeProperty {
public:
    std::string name;
    ScriptType type;
    bool isStatic = false;
    std::function<Value(const Value& self)> getter;
    std::function<void(const Value& self, const Value& value)> setter;

    NativeFunction* getterFunction();
    NativeFunction* setterFunction();

private:
    std::unique_ptr<NativeFunction> getterFunction_;
    std::unique_ptr<NativeFunction> setterFunction_;
};

class NativeObjectType : public ScriptObjectType {
public:
    bool isStatic = false;
    bool isAbstract = false;
    /// Native base type (e.g. exception subclasses derive from `Exception`).
    NativeObjectType* base = nullptr;
    std::deque<NativeFunction> methodStorage_;
    std::unordered_map<std::string, std::vector<NativeFunction*>> methods;
    std::unordered_map<std::string, std::unique_ptr<NativeProperty>> properties;
    /// All registered constructors (overloads are matched by arity).
    std::vector<NativeFunction*> constructors;
    NativeFunction* constructor = nullptr;

    NativeFunction* addMethod(const std::string& methodName, const ScriptType& returnType,
                              std::vector<ScriptType> parameterTypes, NativeCall call, bool isStatic);
    NativeFunction* addConstructor(std::vector<ScriptType> parameterTypes, NativeCall call);
    NativeProperty* addProperty(const std::string& propertyName, const ScriptType& propertyType,
                                std::function<Value(const Value&)> getter,
                                std::function<void(const Value&, const Value&)> setter, bool isStatic);

    NativeFunction* findMethod(const std::string& methodName, std::size_t arity);
    NativeFunction* findConstructor(std::size_t arity) const;
    /// Property lookup that walks the native base chain (getters/setters are inherited).
    NativeProperty* findProperty(const std::string& propertyName);
    bool isAssignableFrom(const ScriptObjectType* other) const override;
};

std::string primitiveName(Primitive primitive);

}  // namespace sharpscript
