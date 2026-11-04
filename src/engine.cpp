#include "sharpscript/engine.hpp"

#include "sharpscript/compiler.hpp"
#include "sharpscript/context.hpp"
#include "sharpscript/exception.hpp"
#include "sharpscript/hot_reload.hpp"
#include "sharpscript/method_reload.hpp"
#include "sharpscript/stdlib.hpp"
#include "sharpscript/unicode.hpp"

namespace sharpscript {
namespace {

/// Appends the script standard library as its own section: declarations are
/// visible everywhere in the module while diagnostics still point at the user's
/// file (the library reports `<stdlib>`).
std::vector<ScriptSection> withStandardLibrary(const EngineSettings& settings,
                                               const std::vector<ScriptSection>& sections) {
    std::vector<ScriptSection> result = sections;
    if (settings.useScriptStandardLibrary) {
        result.emplace_back("lib/array.ss", scriptStandardLibrary());
    }
    return result;
}

}  // namespace

std::string ReloadResult::summary() const {
    std::ostringstream out;
    if (success) {
        const int before = previousGeneration > 0
                               ? previousGeneration
                               : (previous ? previous->generation : 0);
        out << "reloaded '" << (module ? module->name : "") << "' gen " << before << "->"
            << (module ? module->generation : 0) << " in " << durationMs << " ms";
        if (patchedInPlace()) {
            out << " via " << patchedFunctions << " method body/bodies (state untouched)";
        } else {
            out << " (preserved " << migratedGlobals.size() << " globals, reset " << resetGlobals.size()
                << ")";
            if (!fallbackReasons.empty()) out << " [reason: " << fallbackReasons.front() << "]";
        }
    } else {
        std::size_t errors = 0;
        for (const auto& diagnostic : diagnostics) {
            if (diagnostic.severity == DiagnosticSeverity::Error) ++errors;
        }
        out << "reload failed with " << errors << " error(s)";
        if (!fallbackReasons.empty()) out << ": " << fallbackReasons.front();
    }
    return out.str();
}

ScriptEngine::ScriptEngine() : hotReload_(std::make_unique<HotReloadManager>(*this)) {
    settings.output = [](const std::string& line) { std::cout << line << std::endl; };
    // The collector revives objects that still need a finalizer and hands them
    // over; they run at the next safe point (see runFinalizers).
    garbageCollector_.finalizerSink = [this](std::shared_ptr<GcObject> object) {
        queueFinalizer(std::move(object));
    };
    initializeStringMethods();
    initializeExceptionTypes();

    addGlobalFunction(
        "print", primVoid(), {},
        [this](ExecutionContext&, const Value&, const std::vector<Value>& args) {
            std::string line;
            for (std::size_t i = 0; i < args.size(); ++i) {
                if (i > 0) line += " ";
                line += args[i].display();
            }
            if (settings.output) {
                settings.output(line);
            } else {
                std::cout << line << std::endl;
            }
            return Value::voidValue();
        },
        true);

    addGlobalFunction(
        "assert", primVoid(), {},
        [](ExecutionContext&, const Value&, const std::vector<Value>& args) {
            bool condition = !args.empty() && args[0].asBool();
            if (!condition) {
                std::string message = args.size() > 1 ? args[1].display() : "assertion failed";
                throw std::runtime_error(message);
            }
            return Value::voidValue();
        },
        true);
}

ScriptEngine::~ScriptEngine() {
    // Drop the modules first so their globals stop rooting script objects,
    // then let the collector reclaim whatever cycles are left, and finally
    // detach any object still held by the host.
    modules_.clear();
    hotReload_.reset();
    // Finish a partially advanced incremental cycle, then run one fresh cycle
    // for everything that became unreachable when the modules were released.
    while (!garbageCollector_.collect(GcFlag::FullCycle)) {
    }
    garbageCollector_.collect(GcFlag::FullCycle);
    garbageCollector_.shutdown();
}

bool ScriptEngine::garbageCollect(GcFlag flag, std::size_t stepSize) {
    bool finished = garbageCollector_.collect(flag, stepSize == 0 ? settings.gcStepSize : stepSize);
    if (finished) runFinalizers();
    return finished;
}

void ScriptEngine::maybeGarbageCollect() {
    if (!settings.autoGarbageCollect || settings.gcAutoThreshold == 0) return;
    if (garbageCollector_.statistics().allocationsSinceLastCycle < settings.gcAutoThreshold) return;
    garbageCollector_.collect(GcFlag::FullCycle);
}

HotReloadManager& ScriptEngine::hotReload() { return *hotReload_; }

NativeObjectType& ScriptEngine::createNativeType(const std::string& typeName, bool isStatic,
                                                 const std::string& baseName) {
    auto type = std::make_unique<NativeObjectType>();
    type->name = typeName;
    type->isStatic = isStatic;
    if (!baseName.empty()) {
        auto* base = findNativeType(baseName);
        if (base == nullptr) {
            throw std::invalid_argument("unknown native base type '" + baseName + "'");
        }
        type->base = base;
    }
    auto* raw = type.get();
    nativeTypeStorage_.push_back(std::move(type));
    nativeTypes_[typeName] = raw;
    return *raw;
}

void ScriptEngine::initializeExceptionTypes() {
    auto& exception = createNativeType("Exception", false);

    auto addExceptionConstructors = [this](NativeObjectType& type) {
        NativeObjectType* typePointer = &type;
        addConstructor(type, {},
                       [typePointer](ExecutionContext&, const Value&, const std::vector<Value>&) {
                           auto info = std::make_shared<ScriptExceptionInfo>();
                           info->type = typePointer;
                           return Value::native(info);
                       });
        addConstructor(type, {primString()},
                       [typePointer](ExecutionContext&, const Value&, const std::vector<Value>& args) {
                           auto info = std::make_shared<ScriptExceptionInfo>();
                           info->type = typePointer;
                           if (!args.empty()) info->message = args[0].asString();
                           return Value::native(info);
                       });
    };

    addExceptionConstructors(exception);
    addProperty(
        exception, "Message", primString(),
        [](const Value& self) {
            auto info = self.asNative<ScriptExceptionInfo>();
            return Value::string(info ? info->message : std::u16string());
        },
        {}, false);
    addProperty(
        exception, "StackTrace", primString(),
        [](const Value& self) {
            auto info = self.asNative<ScriptExceptionInfo>();
            return Value::string(info ? info->stackTrace : std::u16string());
        },
        {}, false);
    addMethod(
        exception, "ToString", primString(), {},
        [](ExecutionContext&, const Value& self, const std::vector<Value>&) {
            auto info = self.asNative<ScriptExceptionInfo>();
            std::u16string typeName = utf8ToUtf16(info && info->type ? info->type->name : "Exception");
            std::u16string message = info ? info->message : std::u16string();
            return Value::string(typeName + u": " + message);
        },
        false);

    for (const char* name : {"NullReferenceException", "IndexOutOfRangeException",
                             "DivideByZeroException", "ArgumentException", "InvalidCastException",
                             "InvalidOperationException", "StackOverflowException"}) {
        auto& type = createNativeType(name, false, "Exception");
        addExceptionConstructors(type);
    }
}

Value ScriptEngine::createException(const std::string& typeName, const std::string& message) {
    auto* type = findNativeType(typeName);
    if (type == nullptr) type = findNativeType("Exception");
    auto info = std::make_shared<ScriptExceptionInfo>();
    info->type = type;
    info->message = utf8ToUtf16(message);
    return Value::native(info);
}

NativeObjectType* ScriptEngine::findNativeType(const std::string& typeName) const {
    auto it = nativeTypes_.find(typeName);
    return it == nativeTypes_.end() ? nullptr : it->second;
}

NativeFunction* ScriptEngine::addGlobalFunction(const std::string& functionName,
                                                const ScriptType& returnType,
                                                std::vector<ScriptType> parameterTypes, NativeCall call,
                                                bool isVariadic) {
    globalFunctionStorage_.emplace_back();
    auto& function = globalFunctionStorage_.back();
    function.name = functionName;
    function.returnType = returnType;
    function.parameterTypes = std::move(parameterTypes);
    function.isStatic = true;
    function.isVariadic = isVariadic;
    function.invoke = std::move(call);
    globalFunctionIndex_[functionName].push_back(&function);
    return &function;
}

NativeFunction* ScriptEngine::addMethod(NativeObjectType& type, const std::string& methodName,
                                        const ScriptType& returnType,
                                        std::vector<ScriptType> parameterTypes, NativeCall call,
                                        bool isStatic) {
    return type.addMethod(methodName, returnType, std::move(parameterTypes), std::move(call), isStatic);
}

NativeFunction* ScriptEngine::addConstructor(NativeObjectType& type,
                                             std::vector<ScriptType> parameterTypes, NativeCall call) {
    return type.addConstructor(std::move(parameterTypes), std::move(call));
}

NativeProperty* ScriptEngine::addProperty(NativeObjectType& type, const std::string& propertyName,
                                          const ScriptType& propertyType,
                                          std::function<Value(const Value&)> getter,
                                          std::function<void(const Value&, const Value&)> setter,
                                          bool isStatic) {
    return type.addProperty(propertyName, propertyType, std::move(getter), std::move(setter), isStatic);
}

const std::vector<NativeFunction*>* ScriptEngine::findGlobalFunctions(
    const std::string& functionName) const {
    auto it = globalFunctionIndex_.find(functionName);
    return it == globalFunctionIndex_.end() ? nullptr : &it->second;
}

ScriptType ScriptEngine::getArrayType(const ScriptType& elementType) {
    std::string key = elementType.name + "[]";
    auto it = arrayTypes_.find(key);
    if (it != arrayTypes_.end()) return it->second;
    ScriptType type(key, Primitive::None);
    type.element = std::make_shared<ScriptType>(elementType);
    arrayTypes_[key] = type;
    return type;
}

bool ScriptEngine::resolveTypeName(const std::string& typeName, ScriptModule* module,
                                   ScriptType& result) {
    if (typeName == "void") { result = primVoid(); return true; }
    if (typeName == "bool") { result = primBool(); return true; }
    if (typeName == "sbyte") { result = primI8(); return true; }
    if (typeName == "short") { result = primI16(); return true; }
    if (typeName == "int") { result = primI32(); return true; }
    if (typeName == "long") { result = primI64(); return true; }
    if (typeName == "byte") { result = primU8(); return true; }
    if (typeName == "ushort") { result = primU16(); return true; }
    if (typeName == "uint") { result = primU32(); return true; }
    if (typeName == "ulong") { result = primU64(); return true; }
    if (typeName == "float") { result = primF32(); return true; }
    if (typeName == "double") { result = primF64(); return true; }
    if (typeName == "char") { result = primChar(); return true; }
    if (typeName == "string") { result = primString(); return true; }
    if (typeName == "var") { result = primVar(); return true; }
    if (typeName == "null") { result = primNull(); return true; }

    if (module != nullptr) {
        auto it = module->classesByName.find(typeName);
        if (it != module->classesByName.end()) {
            result = ScriptType(typeName, Primitive::None, it->second);
            return true;
        }
    }

    auto nativeIt = nativeTypes_.find(typeName);
    if (nativeIt != nativeTypes_.end()) {
        result = ScriptType(typeName, Primitive::None, nativeIt->second);
        return true;
    }

    return false;
}

std::shared_ptr<ScriptModule> ScriptEngine::tryLoadModuleLocked(const std::string& moduleName,
                                                                const std::vector<ScriptSection>& sections,
                                                                const std::string& sourceFile,
                                                                std::vector<Diagnostic>& diagnostics,
                                                                bool reloading) {
    (void)reloading;
    int generation = 1;
    auto existing = modules_.find(moduleName);
    if (existing != modules_.end()) generation = existing->second->generation + 1;

    auto result = compileModule(*this, moduleName, withStandardLibrary(settings, sections), sourceFile,
                                generation);
    diagnostics = result.diagnostics;
    if (!result.success()) {
        if (onModuleCompileFailed) {
            auto dummy = std::make_shared<ScriptModule>(this, moduleName, sourceFile, sourceFile, generation);
            onModuleCompileFailed(dummy, diagnostics);
        }
        return nullptr;
    }

    auto module = result.module;
    executeGlobalInitializers(module);
    hotReload_->captureInitialValues(*module);

    auto old = modules_.find(moduleName);
    if (old != modules_.end()) old->second->obsolete = true;
    modules_[moduleName] = module;
    if (onModuleLoaded) onModuleLoaded(module);
    return module;
}

std::shared_ptr<ScriptModule> ScriptEngine::loadModule(const std::string& moduleName,
                                                       const std::vector<ScriptSection>& sections,
                                                       const std::string& sourceFile) {
    std::vector<Diagnostic> diagnostics;
    auto module = tryLoadModule(moduleName, sections, sourceFile, diagnostics);
    if (!module) throw ScriptCompileException(diagnostics);
    return module;
}

std::shared_ptr<ScriptModule> ScriptEngine::loadModule(const std::string& moduleName,
                                                       const std::string& source,
                                                       const std::string& sourceFile) {
    return loadModule(moduleName, {ScriptSection(sourceFile, source)}, sourceFile);
}

std::shared_ptr<ScriptModule> ScriptEngine::tryLoadModule(const std::string& moduleName,
                                                          const std::vector<ScriptSection>& sections,
                                                          const std::string& sourceFile,
                                                          std::vector<Diagnostic>& diagnostics) {
    std::lock_guard<std::recursive_mutex> lock(reloadMutex_);
    return tryLoadModuleLocked(moduleName, sections, sourceFile, diagnostics, false);
}

std::shared_ptr<ScriptModule> ScriptEngine::tryLoadModule(const std::string& moduleName,
                                                          const std::string& source,
                                                          const std::string& sourceFile,
                                                          std::vector<Diagnostic>& diagnostics) {
    return tryLoadModule(moduleName, {ScriptSection(sourceFile, source)}, sourceFile, diagnostics);
}

ReloadResult ScriptEngine::reloadModule(const std::string& moduleName, const std::string& source,
                                        const std::string& sourceFile) {
    return reloadModule(moduleName, {ScriptSection(sourceFile, source)}, sourceFile);
}

ReloadResult ScriptEngine::reloadModule(const std::string& moduleName,
                                        const std::vector<ScriptSection>& sections,
                                        const std::string& sourceFile) {
    std::lock_guard<std::recursive_mutex> lock(reloadMutex_);
    auto start = std::chrono::steady_clock::now();

    auto previousIt = modules_.find(moduleName);
    if (previousIt == modules_.end()) {
        std::vector<Diagnostic> diagnostics;
        auto loaded = tryLoadModuleLocked(moduleName, sections, sourceFile, diagnostics, true);
        ReloadResult result;
        result.success = loaded != nullptr;
        result.module = loaded;
        result.diagnostics = diagnostics;
        result.durationMs = millisecondsSince(start);
        return result;
    }

    auto previous = previousIt->second;
    auto compileResult = compileModule(*this, moduleName, withStandardLibrary(settings, sections), sourceFile,
                                       previous->generation + 1);
    if (!compileResult.success()) {
        if (onModuleCompileFailed) onModuleCompileFailed(previous, compileResult.diagnostics);
        ReloadResult result;
        result.success = false;
        result.previous = previous;
        result.diagnostics = compileResult.diagnostics;
        result.durationMs = millisecondsSince(start);
        return result;
    }

    auto module = compileResult.module;

    std::vector<std::string> fallbackReasons;
    if (settings.reloadStrategy != ReloadStrategy::FullModule) {
        auto plan = planMethodReload(*previous, *module);
        if (plan.canPatchInline) {
            applyMethodReload(*previous, *module, plan);
            ReloadResult result;
            result.success = true;
            result.module = previous;
            result.previous = previous;
            result.diagnostics = compileResult.diagnostics;
            result.strategy = "method-body";
            result.patchedFunctions = plan.patchedCount();
            result.previousGeneration = previous->generation - 1;
            result.durationMs = millisecondsSince(start);
            if (settings.printReloadMessages && settings.output) settings.output(result.summary());
            if (onModuleReloaded) onModuleReloaded(result);
            maybeGarbageCollect();
            return result;
        }
        fallbackReasons = plan.blockingReasons;
        if (settings.reloadStrategy == ReloadStrategy::MethodBody) {
            ReloadResult result;
            result.success = false;
            result.module = previous;
            result.previous = previous;
            result.strategy = "method-body";
            result.fallbackReasons = std::move(fallbackReasons);
            result.durationMs = millisecondsSince(start);
            if (settings.printReloadMessages && settings.output) settings.output(result.summary());
            return result;
        }
    }

    executeGlobalInitializers(module);
    hotReload_->captureInitialValues(*module);
    auto migration = hotReload_->migrate(*previous, *module);

    previous->obsolete = true;
    modules_[moduleName] = module;

    ReloadResult result;
    result.success = true;
    result.module = module;
    result.previous = previous;
    result.diagnostics = compileResult.diagnostics;
    result.migratedGlobals = migration.migrated;
    result.resetGlobals = migration.reset;
    result.durationMs = millisecondsSince(start);
    result.strategy = "module";
    result.fallbackReasons = std::move(fallbackReasons);

    if (settings.printReloadMessages && settings.output) settings.output(result.summary());
    if (onModuleReloaded) onModuleReloaded(result);
    maybeGarbageCollect();
    return result;
}

bool ScriptEngine::unloadModule(const std::string& moduleName) {
    std::lock_guard<std::recursive_mutex> lock(reloadMutex_);
    auto it = modules_.find(moduleName);
    if (it == modules_.end()) return false;
    it->second->obsolete = true;
    modules_.erase(it);
    maybeGarbageCollect();
    return true;
}

std::shared_ptr<ScriptModule> ScriptEngine::getModule(const std::string& moduleName) const {
    auto it = modules_.find(moduleName);
    return it == modules_.end() ? nullptr : it->second;
}

bool ScriptEngine::hasModule(const std::string& moduleName) const {
    return modules_.count(moduleName) > 0;
}

std::vector<std::shared_ptr<ScriptModule>> ScriptEngine::modules() const {
    std::vector<std::shared_ptr<ScriptModule>> result;
    result.reserve(modules_.size());
    for (const auto& entry : modules_) result.push_back(entry.second);
    return result;
}

ScriptFunction* ScriptEngine::findFunction(const std::string& functionName,
                                           const std::string& moduleName, int arity) {
    if (!moduleName.empty()) {
        auto module = getModule(moduleName);
        return module ? module->findFunction(functionName, static_cast<std::size_t>(arity)) : nullptr;
    }
    for (const auto& entry : modules_) {
        if (auto* function = entry.second->findFunction(functionName, static_cast<std::size_t>(arity))) {
            return function;
        }
    }
    return nullptr;
}

void ScriptEngine::queueFinalizer(std::shared_ptr<GcObject> object) {
    auto instance = std::dynamic_pointer_cast<ScriptInstance>(object);
    if (!instance) return;   // only script instances carry finalizers
    pendingFinalizers_.push_back(std::move(instance));
}

void ScriptEngine::invokeFinalizer(ScriptFunction* function,
                                   const std::shared_ptr<ScriptInstance>& instance) {
    if (function == nullptr || !instance) return;
    // A partially advanced cycle must not observe the references this touches.
    if (garbageCollector_.busy()) garbageCollector_.collect(GcFlag::FullCycle);
    try {
        ExecutionContext context(*this);
        context.execute(function, {}, Value::instance(instance));
    } catch (const std::exception&) {
        // Finalizers never propagate exceptions (same rule as C#).
    } catch (...) {
    }
}

std::size_t ScriptEngine::runFinalizers() {
    if (runningFinalizers_) return 0;
    runningFinalizers_ = true;
    std::size_t count = 0;
    while (!pendingFinalizers_.empty()) {
        auto instance = pendingFinalizers_.front();
        pendingFinalizers_.erase(pendingFinalizers_.begin());
        // Mark first: if the finalizer resurrects the object it must not be
        // finalized (and destroyed) a second time.
        instance->markFinalized();
        // Derived finalizers run before base ones, matching C#.
        for (auto* cls = instance->cls(); cls != nullptr; cls = cls->base) {
            if (cls->finalizer != nullptr) invokeFinalizer(cls->finalizer, instance);
        }
        // A cycle-collected object still holds its outgoing references (they were
        // kept so the finalizer could read its fields); break them now so the
        // graph is actually reclaimed.
        instance->gcReleaseReferences();
        ++count;
    }
    runningFinalizers_ = false;
    return count;
}

Value ScriptEngine::invoke(ScriptFunction* function, std::vector<Value> args) {
    if (function == nullptr) throw std::invalid_argument("function is null");
    // A partially advanced incremental cycle must not observe reference-count
    // changes made by script code: finish it before executing anything.
    if (garbageCollector_.busy()) garbageCollector_.collect(GcFlag::FullCycle);
    ExecutionContext context(*this);
    Value result = context.execute(function, args);
    maybeGarbageCollect();
    runFinalizers();
    return result;
}

Value ScriptEngine::invoke(const std::string& moduleName, const std::string& functionName,
                           std::vector<Value> args) {
    auto module = getModule(moduleName);
    if (!module) throw std::invalid_argument("module '" + moduleName + "' is not loaded");
    auto* function = module->findFunction(functionName, args.size());
    if (function == nullptr) {
        throw std::invalid_argument("function '" + functionName + "' taking " +
                                    std::to_string(args.size()) + " argument(s) not found in module '" +
                                    moduleName + "'");
    }
    return invoke(function, std::move(args));
}

Value ScriptEngine::invokeEntryPoint(const std::string& moduleName, std::vector<Value> args) {
    auto module = getModule(moduleName);
    if (!module) throw std::invalid_argument("module '" + moduleName + "' is not loaded");
    if (module->entryPoint == nullptr) {
        throw std::invalid_argument("module '" + moduleName + "' does not declare 'void main()'");
    }
    return invoke(module->entryPoint, std::move(args));
}

std::string ScriptEngine::evaluate(const std::string& expression) {
    const std::string source = "string __result = \"\";\nvoid __eval() { __result = \"\" + (" + expression +
                               "); }\n";
    auto module = loadModule("__eval", source, "<eval>");
    auto* function = module->findFunction("__eval", 0);
    if (function == nullptr) throw std::runtime_error("failed to compile expression");
    invoke(function);
    auto it = module->globalsByName.find("__result");
    return it == module->globalsByName.end()
               ? std::string()
               : utf16ToUtf8(module->globals[it->second].value.asString());
}

void ScriptEngine::executeGlobalInitializers(const std::shared_ptr<ScriptModule>& module) {
    if (module->globalInit != nullptr && module->globalInit->hasBody()) {
        if (garbageCollector_.busy()) garbageCollector_.collect(GcFlag::FullCycle);
        ExecutionContext context(*this);
        context.execute(module->globalInit, {});
        maybeGarbageCollect();
    }
}

}  // namespace sharpscript
