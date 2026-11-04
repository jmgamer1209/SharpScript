#pragma once

#include <deque>

#include "gc.hpp"
#include "module.hpp"

namespace sharpscript {

class HotReloadManager;

/// How `reloadModule` applies a new generation.
enum class ReloadStrategy {
    /// Always compile a new module and migrate state.
    FullModule,
    /// Only patch method bodies in place; fail when the module structure changed.
    MethodBody,
    /// Try method-body patching first and fall back to a full reload.
    Auto
};

struct EngineSettings {
    int maxCallDepth = 512;
    /// Safety net for the recursive interpreter: abort before the host thread's
    /// native stack is exhausted (each script call consumes C++ stack space).
    int maxStackBytes = 512 * 1024;
    std::int64_t maxInstructionsPerExecution = 0;
    int maxArrayLength = 1000000;
    std::function<void(const std::string&)> output;
    bool emitDebugInfo = true;
    bool printReloadMessages = false;

    /// Compile the script-side standard library (`Array<T>`) into every module.
    /// Turn it off when the host provides its own definitions, or to keep
    /// generated modules free of library declarations.
    bool useScriptStandardLibrary = true;

    /// Hot reload strategy; `Auto` patches method bodies and falls back to a
    /// full module reload when the module structure changed.
    ReloadStrategy reloadStrategy = ReloadStrategy::Auto;

    /// Automatic cycle collection. Reference counting always destroys acyclic
    /// objects immediately; this only decides when cycles are looked for.
    bool autoGarbageCollect = true;
    /// Allocations since the last cycle that trigger an automatic collection.
    std::size_t gcAutoThreshold = 1024;
    /// Objects processed per step when running the collector incrementally.
    std::size_t gcStepSize = 128;
};

struct ReloadResult {
    bool success = false;
    std::shared_ptr<ScriptModule> module;
    std::shared_ptr<ScriptModule> previous;
    std::vector<Diagnostic> diagnostics;
    std::vector<std::string> migratedGlobals;
    std::vector<std::string> resetGlobals;
    double durationMs = 0.0;
    /// Generation the previous module was at, kept because an in-place patch
    /// bumps the live module's generation in position.
    int previousGeneration = 0;
    /// "method-body" when bodies were patched in place, "module" for a full reload.
    std::string strategy;
    /// Number of bodies replaced in place (0 for a full reload).
    std::size_t patchedFunctions = 0;
    /// Why in-place patching was rejected (empty when it succeeded).
    std::vector<std::string> fallbackReasons;

    bool patchedInPlace() const { return strategy == "method-body"; }
    std::string summary() const;
};

/// Owns native bindings, modules and the reload pipeline. Roughly the equivalent
/// of AngelScript's asCScriptEngine.
class ScriptEngine {
public:
    ScriptEngine();
    ~ScriptEngine();

    ScriptEngine(const ScriptEngine&) = delete;
    ScriptEngine& operator=(const ScriptEngine&) = delete;

    EngineSettings settings;
    HotReloadManager& hotReload();

    // ------------------------------------------------------------ registration
    NativeObjectType& createNativeType(const std::string& typeName, bool isStatic,
                                      const std::string& baseName = "");
    NativeObjectType* findNativeType(const std::string& typeName) const;

    /// Creates a script exception value of `typeName` (must be `Exception` or a
    /// built-in subclass). Host code can throw it with
    /// `ExecutionContext::throwException`.
    Value createException(const std::string& typeName, const std::string& message);

    NativeFunction* addGlobalFunction(const std::string& functionName, const ScriptType& returnType,
                                      std::vector<ScriptType> parameterTypes, NativeCall call,
                                      bool isVariadic = false);
    NativeFunction* addMethod(NativeObjectType& type, const std::string& methodName,
                              const ScriptType& returnType, std::vector<ScriptType> parameterTypes,
                              NativeCall call, bool isStatic = false);
    NativeFunction* addConstructor(NativeObjectType& type, std::vector<ScriptType> parameterTypes,
                                   NativeCall call);
    NativeProperty* addProperty(NativeObjectType& type, const std::string& propertyName,
                                const ScriptType& propertyType, std::function<Value(const Value&)> getter,
                                std::function<void(const Value&, const Value&)> setter,
                                bool isStatic = false);

    const std::vector<NativeFunction*>* findGlobalFunctions(const std::string& functionName) const;

    ScriptType getArrayType(const ScriptType& elementType);
    bool resolveTypeName(const std::string& typeName, ScriptModule* module, ScriptType& result);
    NativeObjectType* stringMethods() const { return stringMethods_; }

    // ---------------------------------------------------------------- modules
    /// Builds a module from several sections that share one global namespace
    /// (AngelScript's `AddScriptSection` + `Build`). Diagnostics report the
    /// section name, so multi-file scripts locate errors correctly.
    std::shared_ptr<ScriptModule> loadModule(const std::string& moduleName,
                                             const std::vector<ScriptSection>& sections,
                                             const std::string& sourceFile = "<memory>");
    std::shared_ptr<ScriptModule> tryLoadModule(const std::string& moduleName,
                                                const std::vector<ScriptSection>& sections,
                                                const std::string& sourceFile,
                                                std::vector<Diagnostic>& diagnostics);
    ReloadResult reloadModule(const std::string& moduleName, const std::vector<ScriptSection>& sections,
                              const std::string& sourceFile = "");

    std::shared_ptr<ScriptModule> loadModule(const std::string& moduleName, const std::string& source,
                                             const std::string& sourceFile = "<memory>");
    std::shared_ptr<ScriptModule> tryLoadModule(const std::string& moduleName, const std::string& source,
                                                const std::string& sourceFile,
                                                std::vector<Diagnostic>& diagnostics);
    ReloadResult reloadModule(const std::string& moduleName, const std::string& source,
                              const std::string& sourceFile = "");
    bool unloadModule(const std::string& moduleName);

    /// Runs the finalizers of objects whose last reference was dropped. Every
    /// `invoke`/`invokeEntryPoint` drains the queue automatically, mirroring the
    /// non-deterministic timing of C# finalization; hosts may also call it
    /// explicitly (e.g. before tearing down a scene).
    std::size_t runFinalizers();

    std::shared_ptr<ScriptModule> getModule(const std::string& moduleName) const;
    bool hasModule(const std::string& moduleName) const;
    std::vector<std::shared_ptr<ScriptModule>> modules() const;

    // --------------------------------------------------------------- invoking
    ScriptFunction* findFunction(const std::string& functionName, const std::string& moduleName = "",
                                 int arity = 0);
    Value invoke(ScriptFunction* function, std::vector<Value> args = {});
    Value invoke(const std::string& moduleName, const std::string& functionName,
                 std::vector<Value> args = {});
    Value invokeEntryPoint(const std::string& moduleName, std::vector<Value> args = {});
    std::string evaluate(const std::string& expression);

    // ------------------------------------------------------------ collection
    GcCollector& garbageCollector() { return garbageCollector_; }
    /// Current collector counters (AngelScript's GetGCStatistics).
    GcStatistics gcStatistics() const { return garbageCollector_.statistics(); }
    /// Runs or advances a collection cycle. `FullCycle` finishes in one call,
    /// `OneStep` processes at most `stepSize` objects and returns false while
    /// more work remains.
    bool garbageCollect(GcFlag flag = GcFlag::FullCycle, std::size_t stepSize = 0);
    /// Triggers a full cycle when the allocation threshold has been reached.
    /// Only called at safe points (after script execution), never reentrantly.
    void maybeGarbageCollect();

    // ---------------------------------------------------------------- events
    std::function<void(const std::shared_ptr<ScriptModule>&)> onModuleLoaded;
    std::function<void(const ReloadResult&)> onModuleReloaded;
    std::function<void(const std::shared_ptr<ScriptModule>&, const std::vector<Diagnostic>&)>
        onModuleCompileFailed;

    void executeGlobalInitializers(const std::shared_ptr<ScriptModule>& module);

private:
    std::shared_ptr<ScriptModule> tryLoadModuleLocked(const std::string& moduleName,
                                                      const std::vector<ScriptSection>& sections,
                                                      const std::string& sourceFile,
                                                      std::vector<Diagnostic>& diagnostics,
                                                      bool reloading);
    void initializeStringMethods();
    void initializeExceptionTypes();

    /// Declared first so it outlives modules (and the script objects they own).
    GcCollector garbageCollector_;
    std::unordered_map<std::string, std::shared_ptr<ScriptModule>> modules_;
    std::unordered_map<std::string, NativeObjectType*> nativeTypes_;
    std::vector<std::unique_ptr<NativeObjectType>> nativeTypeStorage_;
    std::deque<NativeFunction> globalFunctionStorage_;
    std::unordered_map<std::string, std::vector<NativeFunction*>> globalFunctionIndex_;
    /// Objects revived by the collector because their finalizer still has to run.
    std::vector<std::shared_ptr<ScriptInstance>> pendingFinalizers_;
    bool runningFinalizers_ = false;

    void queueFinalizer(std::shared_ptr<GcObject> object);
    void invokeFinalizer(ScriptFunction* function, const std::shared_ptr<ScriptInstance>& instance);

    std::unordered_map<std::string, ScriptType> arrayTypes_;
    NativeObjectType* stringMethods_ = nullptr;
    std::unique_ptr<HotReloadManager> hotReload_;
    std::recursive_mutex reloadMutex_;
};

}  // namespace sharpscript
