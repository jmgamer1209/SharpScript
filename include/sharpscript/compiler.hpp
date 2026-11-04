#pragma once

#include <optional>

#include "ast.hpp"
#include "engine.hpp"

namespace sharpscript {

class FunctionCompiler;

struct CompileResult {
    std::shared_ptr<ScriptModule> module;
    std::vector<Diagnostic> diagnostics;
    bool success() const { return module != nullptr; }
};

/// Compiles one module out of one or more source sections. Sections share a
/// single global namespace (AngelScript's `AddScriptSection` model); diagnostics
/// are mapped back to the section that produced them.
CompileResult compileModule(ScriptEngine& engine, const std::string& moduleName,
                            const std::vector<ScriptSection>& sections, const std::string& sourceFile,
                            int generation);

struct FieldInitializer {
    FieldInfo* field = nullptr;
    const Expression* initializer = nullptr;
    int line = 1;
};

struct GlobalInitializer {
    int globalIndex = 0;
    const Expression* initializer = nullptr;
    int line = 1;
    /// Namespace of the declaration, used for name resolution in the expression.
    std::string namespacePath;
};

/// Resolves declarations, lays out classes/vtables and compiles every body of one module.
class ModuleCompiler {
public:
    ModuleCompiler(ScriptEngine& engine, std::shared_ptr<ScriptModule> module,
                   std::unique_ptr<CompilationUnit> unit, DiagnosticBag& diagnostics);
    ~ModuleCompiler();

    void compile();

    ScriptEngine& engine() { return engine_; }
    ScriptModule& module() { return *module_; }
    DiagnosticBag& diagnostics() { return diagnostics_; }

    void error(const std::string& code, const std::string& message, const Node& node);
    void warning(const std::string& code, const std::string& message, const Node& node);

    bool resolveTypeRef(const TypeRef& typeRef, const Node& node, const std::string& scope,
                        ScriptType& result);

    /// C# style name lookup: `scope` is the current namespace ("Game.AI").
    /// Returns candidate fully-qualified names, innermost scope first, followed
    /// by the namespaces imported with `using` in that scope and finally the
    /// global namespace.
    std::vector<std::string> candidateNames(const std::string& rawName, const std::string& scope) const;
    bool resolveTypeNameInScope(const std::string& rawName, const std::string& scope,
                                ScriptType& result) const;
    bool findGlobalInScope(const std::string& rawName, const std::string& scope, int& index) const;
    std::vector<ScriptFunction*> findFunctionsInScope(const std::string& rawName, const std::string& scope,
                                                      std::size_t arity) const;

    /// Compiles (once) the hidden function behind a lambda expression.
    /// `captures` are the enclosing cells the closure refers to, in slot order.
    ScriptFunction* compileLambda(const LambdaExpression& lambda, ScriptDelegateType& type,
                                  const std::string& scope, ScriptClassType* owner, bool isStatic,
                                  const std::vector<std::pair<std::string, ScriptType>>& captures);

    enum class GenericCallStatus { NoMatch, Resolved, Failed };

    /// Resolves a call to a generic method template (monomorphization).
    /// `NoMatch` means no template has this name/arity: the caller falls back to
    /// the ordinary overload path. `Failed` means a template matched but could
    /// not be instantiated; the diagnostic has already been reported.
    GenericCallStatus resolveGenericMethod(const std::string& rawName, const std::string& scope,
                                           ScriptClassType* owner, bool wantStatic,
                                           const std::vector<TypeRef>& explicitArguments,
                                           const std::vector<std::optional<ScriptType>>& argumentTypes,
                                           const Node& node, ScriptFunction*& result);

    static std::string qualify(const std::string& scope, const std::string& name);
    static std::string unqualified(const std::string& name);

    const std::vector<FieldInitializer>& fieldInitializers(ScriptClassType* cls) const;

private:
    void declareClasses();
    void declareDelegates();
    void layoutClass(const ClassDeclaration& declaration);
    void layoutField(ScriptClassType& cls, const FieldDeclaration& declaration);
    void layoutMethod(ScriptClassType& cls, const FunctionDeclaration& declaration);
    /// Rejects recursive value containment (`struct S { S inner; }`).
    void validateStructCycles();
    void declareTopLevelFunctions();
    void declareTopLevelGlobals();
    void resolveEntryPoint();
    void compileFunctionBodies();
    void compileGlobalInitializer();
    void compilePendingBodies();
    void ensureConstructor(ScriptClassType& cls, std::unordered_map<ScriptClassType*, bool>& visited);
    ScriptFunction* createFunctionStub(const FunctionDeclaration& declaration, ScriptClassType* owner);

    static int findVtableSlot(const std::vector<ScriptFunction*>& vtable, const ScriptFunction& function);

    ScriptEngine& engine_;
    std::shared_ptr<ScriptModule> module_;
    DiagnosticBag& diagnostics_;
    std::unique_ptr<CompilationUnit> unit_;

    std::unordered_map<const ClassDeclaration*, ScriptClassType*> classDeclarations_;
    std::unordered_map<const FunctionDeclaration*, ScriptFunction*> functionDeclarations_;
    std::vector<const ClassDeclaration*> classOrder_;
    std::vector<GlobalInitializer> globalInitializers_;
    std::unordered_map<ScriptClassType*, std::vector<FieldInitializer>> fieldInitializers_;
    std::unordered_set<const ClassDeclaration*> laidOut_;
    std::unordered_set<const ClassDeclaration*> layoutInProgress_;
    std::unordered_set<const LambdaExpression*> compiledLambdas_;
    std::unordered_set<const FunctionDeclaration*> lambdaDeclarations_;

    // ---------------------------------------------------------- generics
    struct GenericTemplate {
        ClassDeclaration* declaration = nullptr;
        std::string qualifiedName;
        std::vector<std::string> parameters;
    };
    struct GenericDelegateTemplate {
        DelegateDeclaration* declaration = nullptr;
        std::string qualifiedName;
        std::vector<std::string> parameters;
        std::vector<Variance> variances;
    };
    struct GenericMethodTemplate {
        FunctionDeclaration* declaration = nullptr;
        std::string qualifiedName;
        std::string namespacePath;
        ScriptClassType* owner = nullptr;
        std::vector<std::string> parameters;
        /// Text the declaration offsets refer to (module source or a fragment).
        const std::string* source = nullptr;
    };

    std::unordered_map<std::string, GenericTemplate> genericClasses_;
    std::unordered_map<std::string, ScriptClassType*> genericInstances_;
    std::unordered_map<std::string, GenericDelegateTemplate> genericDelegates_;
    std::unordered_map<std::string, ScriptDelegateType*> delegateInstances_;
    std::unordered_map<std::string, std::vector<GenericMethodTemplate>> genericMethods_;
    std::unordered_map<std::string, ScriptFunction*> genericMethodInstances_;
    std::vector<std::unique_ptr<ClassDeclaration>> instantiatedDeclarations_;
    std::vector<std::unique_ptr<FunctionDeclaration>> instantiatedFunctions_;
    /// Keeps the source text of re-parsed generic templates alive.
    std::vector<std::unique_ptr<std::string>> fragmentSources_;
    /// Source text each declaration was parsed from (needed to re-instantiate).
    std::unordered_map<const void*, const std::string*> declarationSources_;
    int genericInstantiationCount_ = 0;
    static constexpr int kMaxGenericInstantiations = 512;

    /// Function stubs in creation order; instantiation may append while compiling.
    std::vector<std::pair<const FunctionDeclaration*, ScriptFunction*>> pendingBodies_;
    bool findGenericTemplate(const std::string& rawName, const std::string& scope,
                             GenericTemplate& result) const;
    bool findGenericDelegateTemplate(const std::string& rawName, const std::string& scope,
                                     GenericDelegateTemplate& result) const;
    ScriptClassType* instantiateGenericClass(const GenericTemplate& tmpl,
                                             const std::vector<ScriptType>& arguments,
                                             const Node& node);
    ScriptDelegateType* instantiateGenericDelegate(const GenericDelegateTemplate& tmpl,
                                                   const std::vector<ScriptType>& arguments,
                                                   const Node& node);
    std::string builtinDelegateName(const std::string& rawName, const std::string& scope) const;
    ScriptDelegateType* instantiateBuiltinDelegate(const std::string& name,
                                                    const std::vector<ScriptType>& arguments,
                                                    const Node& node);
    ScriptFunction* instantiateGenericMethod(const GenericMethodTemplate& tmpl,
                                             const std::vector<ScriptType>& arguments, const Node& node);
    void registerGenericMethod(const FunctionDeclaration& declaration, ScriptClassType* owner,
                               const std::string& namespacePath);
    /// Validates `where` constraints of a generic declaration against concrete
    /// type arguments. Reports E0435 and returns false when one is violated.
    bool checkTypeConstraints(const std::vector<TypeParameter>& parameters,
                              const std::vector<ScriptType>& arguments, const std::string& displayName,
                              const std::string& scope, const Node& node);
    /// Variance rules: `out T` may only appear in output positions, `in T` only
    /// in input positions.
    void validateDelegateVariance(const DelegateDeclaration& declaration);
    void registerDeclarationSources(CompilationUnit& unit, const std::string* source);
    const std::string* sourceFor(const void* declaration) const;
    /// Binds generic type parameters from a parameter pattern and an argument type.
    static bool unifyTypeArgument(const TypeRef& pattern, const ScriptType& actual,
                                  const std::vector<std::string>& parameters,
                                  std::unordered_map<std::string, ScriptType>& bindings);
    int lambdaCounter_ = 0;
    bool compilingBodies_ = false;
    std::size_t bodyCursor_ = 0;
};

}  // namespace sharpscript
