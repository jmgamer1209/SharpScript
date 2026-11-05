#include "sharpscript/method_reload.hpp"

#include <algorithm>

namespace sharpscript {
namespace {

constexpr const char* kLambdaPrefix = "__lambda_";

bool isLambdaName(const std::string& name) {
    return name.compare(0, std::char_traits<char>::length(kLambdaPrefix), kLambdaPrefix) == 0;
}

/// Passing mode suffix, so `F(ref int)` / `F(out int)` / `F(in int)` keep
/// distinct identities: overloads only differing by mode are invalid C#, but the
/// key must not silently pair two different declarations.
const char* parameterModeKey(ParamModifier modifier) {
    switch (modifier) {
        case ParamModifier::Ref: return "&ref";
        case ParamModifier::Out: return "&out";
        case ParamModifier::In: return "&in";
        default: return "";
    }
}

/// Identity of a function across generations: owner + name + parameter types +
/// return type. Lambdas are excluded because their names are positional and
/// drift as soon as an unrelated lambda is added or removed.
std::string functionKey(const ScriptFunction& function) {
    if (isLambdaName(function.name)) return {};
    std::string key = function.isConstructor ? "ctor:" : "";
    key += function.qualifiedName();
    key += "(";
    for (const auto& parameter : function.parameters) {
        key += parameter.type.name;
        key += parameterModeKey(parameter.modifier);
        key += ",";
    }
    key += ")->";
    key += function.returnType.name;
    if (function.isStatic) key += " static";
    return key;
}

std::string signatureOf(const ScriptFunction& function) {
    std::string result = function.name + "(";
    for (std::size_t i = 0; i < function.parameters.size(); ++i) {
        if (i > 0) result += ", ";
        result += function.parameters[i].type.name;
    }
    result += ")";
    return result;
}

std::string vtableKey(const ScriptClassType& cls, std::size_t slot) {
    if (slot >= cls.vtable.size() || cls.vtable[slot] == nullptr) return "<empty>";
    return cls.vtable[slot]->name + "/" + signatureOf(*cls.vtable[slot]);
}

bool sameClassLayout(const ScriptClassType& previous, const ScriptClassType& next, std::string& reason) {
    if (previous.isStruct != next.isStruct) {
        reason = "struct/class kind changed";
        return false;
    }
    // A class that starts (or stops) being finalizable changes the lifetime of
    // every existing instance, so it cannot be patched in place.
    if ((previous.finalizer != nullptr) != (next.finalizer != nullptr)) {
        reason = next.finalizer != nullptr ? "a finalizer was added" : "the finalizer was removed";
        return false;
    }
    const std::string previousBase = previous.base != nullptr ? previous.base->name : std::string();
    const std::string nextBase = next.base != nullptr ? next.base->name : std::string();
    if (previousBase != nextBase) {
        reason = "base type changed from '" + previousBase + "' to '" + nextBase + "'";
        return false;
    }
    if (previous.fields.size() != next.fields.size()) {
        reason = "field count changed (" + std::to_string(previous.fields.size()) + " -> " +
                 std::to_string(next.fields.size()) + ")";
        return false;
    }
    for (std::size_t i = 0; i < previous.fields.size(); ++i) {
        const auto& before = previous.fields[i];
        const auto& after = next.fields[i];
        if (before.name != after.name) {
            reason = "field #" + std::to_string(i) + " renamed from '" + before.name + "' to '" + after.name + "'";
            return false;
        }
        if (!before.type.sameAs(after.type)) {
            reason = "field '" + before.name + "' changed type from '" + before.type.name + "' to '" +
                     after.type.name + "'";
            return false;
        }
        if (before.isReadOnly != after.isReadOnly) {
            reason = "field '" + before.name + "' changed readonly-ness";
            return false;
        }
    }
    if (previous.vtable.size() != next.vtable.size()) {
        reason = "virtual method table changed size (" + std::to_string(previous.vtable.size()) +
                 " -> " + std::to_string(next.vtable.size()) + ")";
        return false;
    }
    for (std::size_t slot = 0; slot < previous.vtable.size(); ++slot) {
        if (vtableKey(previous, slot) != vtableKey(next, slot)) {
            reason = "virtual slot " + std::to_string(slot) + " changed from '" + vtableKey(previous, slot) +
                     "' to '" + vtableKey(next, slot) + "'";
            return false;
        }
    }
    return true;
}

bool sameDelegateSignature(const ScriptDelegateType& previous, const ScriptDelegateType& next) {
    if (!previous.returnType.sameAs(next.returnType)) return false;
    if (previous.parameterTypes.size() != next.parameterTypes.size()) return false;
    for (std::size_t i = 0; i < previous.parameterTypes.size(); ++i) {
        if (!previous.parameterTypes[i].sameAs(next.parameterTypes[i])) return false;
    }
    return true;
}

bool sameGlobal(const GlobalVariable& previous, const GlobalVariable& next) {
    return previous.name == next.name && previous.type.sameAs(next.type) &&
           previous.isReadOnly == next.isReadOnly && previous.isStatic == next.isStatic;
}

bool sameCode(const ScriptFunction* previous, const ScriptFunction* next) {
    if (previous == nullptr || next == nullptr) return previous == next;
    return previous->code == next->code;
}

/// Maps every type/function referenced by a patched body back onto the previous
/// generation so that live objects and call sites keep their identity.
struct PatchContext {
    ScriptModule& previous;
    ScriptModule& next;
    std::unordered_map<std::string, ScriptFunction*> previousFunctions;

    PatchContext(ScriptModule& oldModule, ScriptModule& newModule)
        : previous(oldModule), next(newModule) {
        for (auto& function : previous.functions) {
            const std::string key = functionKey(*function);
            if (!key.empty()) previousFunctions.emplace(key, function.get());
        }
    }

    ScriptFunction* remapFunction(ScriptFunction* function) {
        if (function == nullptr) return nullptr;
        const std::string key = functionKey(*function);
        if (!key.empty()) {
            auto it = previousFunctions.find(key);
            if (it != previousFunctions.end()) return it->second;
        }
        return function;   // newly added function: owned by `next`
    }

    ScriptType remapType(const ScriptType& type) {
        ScriptType result = type;
        if (type.isArray() && type.element) {
            result.element = std::make_shared<const ScriptType>(remapType(*type.element));
            return result;
        }
        if (!type.name.empty() && type.object != nullptr) {
            if (auto* cls = dynamic_cast<ScriptClassType*>(type.object)) {
                if (auto* mapped = previous.findClass(cls->name)) result.object = mapped;
            } else if (auto* delegate = dynamic_cast<ScriptDelegateType*>(type.object)) {
                auto it = previous.delegatesByName.find(delegate->name);
                if (it != previous.delegatesByName.end()) result.object = it->second;
            }
        }
        return result;
    }

    FieldInfo* remapField(FieldInfo* field) {
        if (field == nullptr || field->owner == nullptr) return field;
        auto* cls = previous.findClass(field->owner->name);
        if (cls == nullptr) return field;
        auto* mapped = cls->findField(field->name);
        return mapped != nullptr ? mapped : field;
    }

    ScriptDelegateType* remapDelegateType(ScriptDelegateType* type) {
        if (type == nullptr) return nullptr;
        auto it = previous.delegatesByName.find(type->name);
        return it != previous.delegatesByName.end() ? it->second : type;
    }
};

void patchBody(PatchContext& context, ScriptFunction& target, const ScriptFunction& source) {
    target.code = source.code;
    target.constants = source.constants;
    target.lineMap = source.lineMap;
    target.localSlotCount = source.localSlotCount;
    target.maxStack = source.maxStack;
    target.file = source.file;
    target.declLine = source.declLine;
    target.nativeRefs = source.nativeRefs;
    target.virtualRefs = source.virtualRefs;

    target.handlers = source.handlers;
    for (auto& handler : target.handlers) handler.catchType = context.remapType(handler.catchType);

    target.functionRefs.clear();
    target.functionRefs.reserve(source.functionRefs.size());
    for (auto* function : source.functionRefs) target.functionRefs.push_back(context.remapFunction(function));

    target.typeRefs.clear();
    target.typeRefs.reserve(source.typeRefs.size());
    for (const auto& type : source.typeRefs) target.typeRefs.push_back(context.remapType(type));

    target.fieldRefs.clear();
    target.fieldRefs.reserve(source.fieldRefs.size());
    for (auto* field : source.fieldRefs) target.fieldRefs.push_back(context.remapField(field));

    target.delegateInfos.clear();
    target.delegateInfos.reserve(source.delegateInfos.size());
    for (const auto& info : source.delegateInfos) {
        DelegateInfo copy = info;
        copy.function = context.remapFunction(info.function);
        copy.type = context.remapDelegateType(info.type);
        target.delegateInfos.push_back(copy);
    }
}

/// Adds a function that only exists in the new generation so that lookups and
/// call sites resolved later keep working after the swap.
void attachFunction(ScriptModule& previous, ScriptFunction& function) {
    previous.functionsByName[function.qualifiedName()].push_back(&function);
    if (function.owner == nullptr) return;
    auto* cls = previous.findClass(function.owner->name);
    if (cls == nullptr) return;
    cls->methods[function.name].push_back(&function);
}

}  // namespace

MethodReloadPlan planMethodReload(const ScriptModule& previous, const ScriptModule& next) {
    MethodReloadPlan plan;
    auto block = [&plan](std::string reason) { plan.blockingReasons.push_back(std::move(reason)); };

    // 1. Class layouts: field slots and vtable slots are baked into bytecode.
    for (const auto& entry : previous.classesByName) {
        auto* nextClass = next.findClass(entry.first);
        if (nextClass == nullptr) continue;   // no longer instantiated; live objects keep the old type
        std::string reason;
        if (!sameClassLayout(*entry.second, *nextClass, reason)) {
            block("class '" + entry.first + "' changed layout: " + reason);
        }
    }

    // 2. Types that only exist in the new generation would be owned by a module
    //    that is otherwise discarded, so they force a full reload.
    for (const auto& entry : next.classesByName) {
        if (previous.classesByName.find(entry.first) == previous.classesByName.end()) {
            block("type '" + entry.first + "' is new");
        }
    }
    for (const auto& entry : next.delegatesByName) {
        if (previous.delegatesByName.find(entry.first) == previous.delegatesByName.end()) {
            block("delegate type '" + entry.first + "' is new");
        }
    }

    // 3. Delegate signatures are copied into delegate values.
    for (const auto& entry : previous.delegatesByName) {
        auto it = next.delegatesByName.find(entry.first);
        if (it == next.delegatesByName.end()) continue;
        if (!sameDelegateSignature(*entry.second, *it->second)) {
            block("delegate '" + entry.first + "' changed signature");
        }
    }

    // 4. Globals are addressed by index in `LoadGlobal`/`StoreGlobal`.
    if (previous.globals.size() != next.globals.size()) {
        block("global variable table changed size (" + std::to_string(previous.globals.size()) + " -> " +
              std::to_string(next.globals.size()) + ")");
    } else {
        for (std::size_t i = 0; i < previous.globals.size(); ++i) {
            if (sameGlobal(previous.globals[i], next.globals[i])) continue;
            block("global '" + previous.globals[i].name + "' changed declaration");
            break;
        }
    }

    // 5. Global initializers only run on a full reload, so editing one must not
    //    silently keep the old value.
    if (!sameCode(previous.globalInit, next.globalInit)) block("a global initializer changed");

    // 6. Match functions by signature; every match must keep its vtable slot.
    std::unordered_map<std::string, ScriptFunction*> previousFunctions;
    for (auto& function : previous.functions) {
        const std::string key = functionKey(*function);
        if (!key.empty()) previousFunctions.emplace(key, function.get());
    }
    for (auto& function : next.functions) {
        const std::string key = functionKey(*function);
        if (key.empty()) continue;   // lambdas are regenerated, never patched
        auto it = previousFunctions.find(key);
        if (it == previousFunctions.end()) {
            plan.added.push_back(function.get());
            continue;
        }
        auto* previousFunction = it->second;
        if (previousFunction->vtableSlot != function->vtableSlot) {
            block("'" + function->qualifiedName() + "' moved to a different virtual slot");
            continue;
        }
        if (previousFunction->isAbstract != function->isAbstract) {
            block("'" + function->qualifiedName() + "' changed abstractness");
            continue;
        }
        if (!function->hasBody()) continue;   // native/abstract declarations need no patch
        plan.patchPairs.push_back({previousFunction, function.get()});
    }

    plan.canPatchInline = plan.blockingReasons.empty();
    return plan;
}

void applyMethodReload(ScriptModule& previous, ScriptModule& next, const MethodReloadPlan& plan) {
    if (!plan.canPatchInline) return;

    PatchContext context(previous, next);

    // Attach new functions first: patched bodies may call them.
    for (auto* function : plan.added) {
        if (function == nullptr) continue;
        attachFunction(previous, *function);
    }
    for (auto& pair : plan.patchPairs) patchBody(context, *pair.first, *pair.second);

    // Copied tables may point at functions owned by `next` (new lambdas, new
    // functions), so the discarded generation has to stay alive with this one.
    previous.patchedDependency = next.shared_from_this();
    previous.source = next.source;
    previous.sourceFile = next.sourceFile;
    previous.generation = next.generation;
    previous.compileTimeMs = next.compileTimeMs;
}

}  // namespace sharpscript
