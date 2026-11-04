#include "sharpscript/compiler.hpp"

#include "sharpscript/function_compiler.hpp"
#include "sharpscript/parser.hpp"

namespace sharpscript {
namespace {

using TypeEnvMap = std::unordered_map<std::string, ScriptType>;

TypeRef typeRefFromType(const ScriptType& type) {
    TypeRef ref;
    int depth = 0;
    const ScriptType* current = &type;
    while (current->element) {
        ++depth;
        current = current->element.get();
    }
    ref.name = current->templateName.empty() ? current->name : current->templateName;
    ref.arrayDepth = depth;
    for (const auto& argument : current->typeArguments) {
        ref.typeArguments.push_back(typeRefFromType(argument));
    }
    return ref;
}

void substituteTypeRef(TypeRef& ref, const TypeEnvMap& env) {
    auto it = env.find(ref.name);
    if (it != env.end()) {
        const int extraDepth = ref.arrayDepth;
        ref = typeRefFromType(it->second);
        ref.arrayDepth += extraDepth;
        return;
    }
    for (auto& argument : ref.typeArguments) substituteTypeRef(argument, env);
}

void substituteExpression(ExprPtr& expression, const TypeEnvMap& env);
void substituteStatement(StmtPtr& statement, const TypeEnvMap& env);

void substituteBlock(BlockStatement& block, const TypeEnvMap& env) {
    for (auto& statement : block.statements) substituteStatement(statement, env);
}

void substituteExpression(ExprPtr& expression, const TypeEnvMap& env) {
    if (!expression) return;
    if (auto* e = dynamic_cast<BinaryExpression*>(expression.get())) {
        substituteExpression(e->left, env);
        substituteExpression(e->right, env);
    } else if (auto* e = dynamic_cast<UnaryExpression*>(expression.get())) {
        substituteExpression(e->operand, env);
    } else if (auto* e = dynamic_cast<AssignmentExpression*>(expression.get())) {
        substituteExpression(e->target, env);
        substituteExpression(e->value, env);
    } else if (auto* e = dynamic_cast<MemberExpression*>(expression.get())) {
        substituteExpression(e->target, env);
    } else if (auto* e = dynamic_cast<IndexExpression*>(expression.get())) {
        substituteExpression(e->target, env);
        substituteExpression(e->index, env);
    } else if (auto* e = dynamic_cast<CallExpression*>(expression.get())) {
        substituteExpression(e->callee, env);
        for (auto& argument : e->arguments) substituteExpression(argument, env);
        for (auto& typeArgument : e->typeArguments) substituteTypeRef(typeArgument, env);
    } else if (auto* e = dynamic_cast<NewObjectExpression*>(expression.get())) {
        substituteTypeRef(e->type, env);
        for (auto& argument : e->arguments) substituteExpression(argument, env);
    } else if (auto* e = dynamic_cast<NewArrayExpression*>(expression.get())) {
        substituteTypeRef(e->elementType, env);
        substituteExpression(e->size, env);
        for (auto& item : e->initializer) substituteExpression(item, env);
    } else if (auto* e = dynamic_cast<CastExpression*>(expression.get())) {
        substituteTypeRef(e->targetType, env);
        substituteExpression(e->operand, env);
    } else if (auto* e = dynamic_cast<AsExpression*>(expression.get())) {
        substituteTypeRef(e->targetType, env);
        substituteExpression(e->operand, env);
    } else if (auto* e = dynamic_cast<TypeTestExpression*>(expression.get())) {
        if (e->hasTargetType) substituteTypeRef(e->targetType, env);
        substituteExpression(e->operand, env);
    } else if (auto* e = dynamic_cast<ConditionalExpression*>(expression.get())) {
        substituteExpression(e->condition, env);
        substituteExpression(e->whenTrue, env);
        substituteExpression(e->whenFalse, env);
    } else if (auto* e = dynamic_cast<LambdaExpression*>(expression.get())) {
        if (e->declaration != nullptr) {
            for (auto& parameter : e->declaration->parameters) substituteTypeRef(parameter.type, env);
            if (e->declaration->returnType) substituteTypeRef(*e->declaration->returnType, env);
            if (e->declaration->body) substituteBlock(*e->declaration->body, env);
        }
    }
}

void substituteStatement(StmtPtr& statement, const TypeEnvMap& env) {
    if (!statement) return;
    if (auto* s = dynamic_cast<BlockStatement*>(statement.get())) {
        substituteBlock(*s, env);
    } else if (auto* s = dynamic_cast<LocalVariableDeclaration*>(statement.get())) {
        if (s->type) substituteTypeRef(*s->type, env);
        for (auto& initializer : s->initializers) substituteExpression(initializer.value, env);
    } else if (auto* s = dynamic_cast<ExpressionStatement*>(statement.get())) {
        substituteExpression(s->expression, env);
    } else if (auto* s = dynamic_cast<IfStatement*>(statement.get())) {
        substituteExpression(s->condition, env);
        substituteStatement(s->thenBranch, env);
        substituteStatement(s->elseBranch, env);
    } else if (auto* s = dynamic_cast<WhileStatement*>(statement.get())) {
        substituteExpression(s->condition, env);
        substituteStatement(s->body, env);
    } else if (auto* s = dynamic_cast<DoWhileStatement*>(statement.get())) {
        substituteStatement(s->body, env);
        substituteExpression(s->condition, env);
    } else if (auto* s = dynamic_cast<ForStatement*>(statement.get())) {
        substituteStatement(s->initializer, env);
        substituteExpression(s->condition, env);
        substituteExpression(s->iterator, env);
        substituteStatement(s->body, env);
    } else if (auto* s = dynamic_cast<ForeachStatement*>(statement.get())) {
        if (s->elementType) substituteTypeRef(*s->elementType, env);
        substituteExpression(s->collection, env);
        substituteStatement(s->body, env);
    } else if (auto* s = dynamic_cast<SwitchStatement*>(statement.get())) {
        substituteExpression(s->value, env);
        for (auto& clause : s->cases) {
            substituteExpression(clause.label, env);
            for (auto& child : clause.body) substituteStatement(child, env);
        }
    } else if (auto* s = dynamic_cast<TryStatement*>(statement.get())) {
        if (s->body) substituteBlock(*s->body, env);
        for (auto& clause : s->catches) {
            if (clause->type) substituteTypeRef(*clause->type, env);
            if (clause->body) substituteBlock(*clause->body, env);
        }
        if (s->finallyBody) substituteBlock(*s->finallyBody, env);
    } else if (auto* s = dynamic_cast<ReturnStatement*>(statement.get())) {
        substituteExpression(s->value, env);
    } else if (auto* s = dynamic_cast<ThrowStatement*>(statement.get())) {
        substituteExpression(s->value, env);
    }
}

bool typeRefMentions(const TypeRef& ref, const std::string& name) {
    if (ref.name == name) return true;
    for (const auto& argument : ref.typeArguments) {
        if (typeRefMentions(argument, name)) return true;
    }
    return false;
}

std::string joinTypeNames(const std::vector<ScriptType>& types) {
    std::string result;
    for (std::size_t i = 0; i < types.size(); ++i) {
        if (i > 0) result += ", ";
        result += types[i].name;
    }
    return result;
}

std::string joinNames(const std::vector<std::string>& names) {
    std::string result;
    for (std::size_t i = 0; i < names.size(); ++i) {
        if (i > 0) result += ", ";
        result += names[i];
    }
    return result;
}

/// `Box<T, U>`: the template name with its type parameters, for diagnostics.
std::string templateDisplayName(const std::string& qualifiedName, const std::vector<std::string>& parameters) {
    if (parameters.empty()) return qualifiedName;
    return qualifiedName + "<" + joinNames(parameters) + ">";
}

/// Prefixes blank lines so that a re-parsed fragment reports the absolute line
/// numbers of the original source instead of positions inside the fragment.
std::string padFragmentText(std::string text, int line) {
    if (line > 1) text.insert(0, static_cast<std::size_t>(line - 1), '\n');
    return text;
}

void substituteDeclarationTypes(Declaration& declaration, const TypeEnvMap& env) {
    if (auto* field = dynamic_cast<FieldDeclaration*>(&declaration)) {
        substituteTypeRef(field->type, env);
        for (auto& initializer : field->initializers) substituteExpression(initializer.value, env);
    } else if (auto* function = dynamic_cast<FunctionDeclaration*>(&declaration)) {
        if (function->returnType) substituteTypeRef(*function->returnType, env);
        for (auto& parameter : function->parameters) substituteTypeRef(parameter.type, env);
        if (function->body) substituteBlock(*function->body, env);
    } else if (auto* cls = dynamic_cast<ClassDeclaration*>(&declaration)) {
        for (auto& member : cls->members) substituteDeclarationTypes(*member, env);
    }
}

}  // namespace

CompileResult compileModule(ScriptEngine& engine, const std::string& moduleName,
                            const std::vector<ScriptSection>& sections, const std::string& sourceFile,
                            int generation) {
    auto start = std::chrono::steady_clock::now();
    DiagnosticBag diagnostics(sourceFile);

    // Sections are concatenated into one text so the parser sees a single global
    // namespace, exactly like AngelScript's multiple `AddScriptSection` calls.
    std::string combined;
    auto sectionMap = std::make_shared<SectionMap>();
    std::vector<int> startLines;
    combined.reserve(1024);
    int nextLine = 1;
    for (const auto& section : sections) {
        startLines.push_back(nextLine);
        SectionRange range;
        range.startLine = nextLine;
        range.file = section.file.empty() ? sourceFile : section.file;
        range.lineOffset = section.lineOffset;
        sectionMap->push_back(std::move(range));

        int newlines = static_cast<int>(std::count(section.source.begin(), section.source.end(), '\n'));
        combined += section.source;
        if (combined.empty() || combined.back() != '\n') {
            combined += '\n';
            ++newlines;
        }
        nextLine += newlines;
    }
    if (!sectionMap->empty()) diagnostics.setSections(sectionMap);

    auto module = std::make_shared<ScriptModule>(&engine, moduleName, combined, sourceFile, generation);
    module->sections = sections;
    module->sectionStartLines = startLines;

    std::unique_ptr<CompilationUnit> unit;
    try {
        unit = Parser::parse(module->source, diagnostics);
    } catch (const std::exception& ex) {
        diagnostics.error("E0901", std::string("internal parser error: ") + ex.what(), 1, 1);
        return CompileResult{nullptr, diagnostics.items()};
    } catch (...) {
        diagnostics.error("E0901", "internal parser error", 1, 1);
        return CompileResult{nullptr, diagnostics.items()};
    }

    if (!diagnostics.hasErrors()) {
        try {
            ModuleCompiler compiler(engine, module, std::move(unit), diagnostics);
            compiler.compile();
        } catch (const std::exception& ex) {
            diagnostics.error("E0902", std::string("internal compiler error: ") + ex.what(), 1, 1);
        } catch (...) {
            diagnostics.error("E0902", "internal compiler error", 1, 1);
        }
    }

    module->compileTimeMs = millisecondsSince(start);
    return CompileResult{diagnostics.hasErrors() ? nullptr : module, diagnostics.items()};
}

ModuleCompiler::ModuleCompiler(ScriptEngine& engine, std::shared_ptr<ScriptModule> module,
                               std::unique_ptr<CompilationUnit> unit, DiagnosticBag& diagnostics)
    : engine_(engine), module_(std::move(module)), diagnostics_(diagnostics), unit_(std::move(unit)) {}

ModuleCompiler::~ModuleCompiler() = default;

void ModuleCompiler::error(const std::string& code, const std::string& message, const Node& node) {
    diagnostics_.error(code, message, node.line, node.column);
}

void ModuleCompiler::warning(const std::string& code, const std::string& message, const Node& node) {
    diagnostics_.warning(code, message, node.line, node.column);
}

void ModuleCompiler::compile() {
    registerDeclarationSources(*unit_, &module_->source);
    declareClasses();
    declareDelegates();
    for (auto* declaration : classOrder_) layoutClass(*declaration);
    validateStructCycles();
    for (auto& variable : module_->globals) {
        if (variable.isStatic && variable.type.isStruct() && variable.value.isNull()) {
            variable.value = defaultValue(variable.type, engine_.garbageCollector());
        }
    }
    declareTopLevelFunctions();
    declareTopLevelGlobals();
    resolveEntryPoint();

    std::unordered_map<ScriptClassType*, bool> visited;
    for (auto& cls : module_->classes) ensureConstructor(*cls, visited);

    compilingBodies_ = true;
    compilePendingBodies();

    compileGlobalInitializer();

    // Global initializers may instantiate generic methods and lambdas too.
    compilePendingBodies();
    compilingBodies_ = false;
}

void ModuleCompiler::compilePendingBodies() {
    // Index loop: generic instantiation and lambdas may append while compiling.
    for (; bodyCursor_ < pendingBodies_.size(); ++bodyCursor_) {
        const auto* declaration = pendingBodies_[bodyCursor_].first;
        auto* function = pendingBodies_[bodyCursor_].second;
        // Lambda bodies are compiled on demand by compileLambda (with captures).
        if (lambdaDeclarations_.count(declaration) > 0) continue;
        if (declaration->body == nullptr) continue;
        if (function->isAbstract) {
            error("E0211", "abstract method '" + function->qualifiedName() + "' cannot have a body",
                  *declaration);
            continue;
        }
        FunctionCompiler compiler(*this, function, declaration);
        compiler.compile();
    }
}

void ModuleCompiler::registerDeclarationSources(CompilationUnit& unit, const std::string* source) {
    std::function<void(Declaration&)> visit = [&](Declaration& declaration) {
        declarationSources_[&declaration] = source;
        if (auto* cls = dynamic_cast<ClassDeclaration*>(&declaration)) {
            for (auto& member : cls->members) visit(*member);
        }
    };
    for (auto& declaration : unit.declarations) visit(*declaration);
}

const std::string* ModuleCompiler::sourceFor(const void* declaration) const {
    auto it = declarationSources_.find(declaration);
    return it == declarationSources_.end() ? &module_->source : it->second;
}

void ModuleCompiler::declareDelegates() {
    std::vector<DelegateDeclaration*> declarations;
    for (auto& declarationPtr : unit_->declarations) {
        auto* declaration = dynamic_cast<DelegateDeclaration*>(declarationPtr.get());
        if (declaration == nullptr) continue;

        const std::string qualifiedName = qualify(declaration->namespacePath, declaration->name);
        const bool taken = qualifiedName == "System.Func" || qualifiedName == "System.Action" ||
                           module_->delegatesByName.count(qualifiedName) > 0 ||
                           module_->classesByName.count(qualifiedName) > 0 ||
                           genericClasses_.count(qualifiedName) > 0 ||
                           genericDelegates_.count(qualifiedName) > 0 ||
                           engine_.findNativeType(qualifiedName) != nullptr;
        if (taken) {
            error("E0408", "a type named '" + qualifiedName + "' is already declared", *declaration);
            continue;
        }

        if (!declaration->typeParameters.empty()) {
            // Generic delegates are instantiated on demand like generic classes.
            GenericDelegateTemplate tmpl;
            tmpl.declaration = declaration;
            tmpl.qualifiedName = qualifiedName;
            for (const auto& parameter : declaration->typeParameters) {
                tmpl.parameters.push_back(parameter.name);
                tmpl.variances.push_back(parameter.variance);
            }
            validateDelegateVariance(*declaration);
            genericDelegates_[qualifiedName] = std::move(tmpl);
            continue;
        }

        declarations.push_back(declaration);
        auto type = std::make_unique<ScriptDelegateType>();
        type->name = qualifiedName;
        type->namespacePath = declaration->namespacePath;
        type->module = module_.get();
        module_->delegatesByName[qualifiedName] = type.get();
        module_->delegates.push_back(std::move(type));
    }

    // Resolve signatures after every delegate shell exists, so delegates may
    // reference each other regardless of declaration order.
    for (auto* declaration : declarations) {
        auto it = module_->delegatesByName.find(qualify(declaration->namespacePath, declaration->name));
        if (it == module_->delegatesByName.end()) continue;
        auto* type = it->second;
        type->returnType = primVoid();
        resolveTypeRef(declaration->returnType, *declaration, declaration->namespacePath, type->returnType);
        for (const auto& parameter : declaration->parameters) {
            if (parameter.modifier != ParamModifier::None) {
                error("E0424", "delegate parameters cannot be 'ref'/'out'/'in'", parameter);
            }
            ScriptType parameterType;
            if (!resolveTypeRef(parameter.type, parameter, declaration->namespacePath, parameterType)) {
                parameterType = primI32();
            }
            type->parameterTypes.push_back(parameterType);
        }
    }
}

void ModuleCompiler::declareClasses() {
    for (auto& declarationPtr : unit_->declarations) {
        auto* declaration = dynamic_cast<ClassDeclaration*>(declarationPtr.get());
        if (declaration == nullptr) continue;
        const std::string qualifiedName = qualify(declaration->namespacePath, declaration->name);
        if (qualifiedName == "System.Func" || qualifiedName == "System.Action") {
            error("E0201", "a type named '" + qualifiedName + "' is already declared", *declaration);
            continue;
        }
        if (!declaration->typeParameters.empty()) {
            // Generic templates are instantiated on demand and never laid out directly.
            if (genericClasses_.count(qualifiedName) > 0 || genericDelegates_.count(qualifiedName) > 0) {
                error("E0201", "a type named '" + qualifiedName + "' is already declared", *declaration);
                continue;
            }
            GenericTemplate tmpl;
            tmpl.declaration = declaration;
            tmpl.qualifiedName = qualifiedName;
            for (const auto& parameter : declaration->typeParameters) {
                if (parameter.variance != Variance::None) {
                    error("E0440",
                          "variance annotations are only allowed on delegate type parameters ('" +
                              parameter.name + "')",
                          parameter);
                }
                tmpl.parameters.push_back(parameter.name);
            }
            genericClasses_[qualifiedName] = std::move(tmpl);
            continue;
        }
        if (module_->classesByName.count(qualifiedName) > 0 ||
            engine_.findNativeType(qualifiedName) != nullptr) {
            error("E0201", "a type named '" + qualifiedName + "' is already declared", *declaration);
            continue;
        }
        auto cls = std::make_unique<ScriptClassType>();
        cls->name = qualifiedName;
        cls->namespacePath = declaration->namespacePath;
        cls->module = module_.get();
        cls->isAbstract = declaration->isAbstract && !declaration->isStruct;
        cls->isStruct = declaration->isStruct;
        auto* raw = cls.get();
        module_->classesByName[cls->name] = raw;
        module_->classes.push_back(std::move(cls));
        classDeclarations_[declaration] = raw;
        classOrder_.push_back(declaration);
    }
}

void ModuleCompiler::layoutClass(const ClassDeclaration& declaration) {
    if (laidOut_.count(&declaration) > 0) return;
    auto it = classDeclarations_.find(&declaration);
    if (it == classDeclarations_.end()) return;
    ScriptClassType& cls = *it->second;

    if (layoutInProgress_.count(&declaration) > 0) {
        error("E0202", "cyclic inheritance detected for class '" + declaration.name + "'", declaration);
        return;
    }
    layoutInProgress_.insert(&declaration);

    if (declaration.baseName.has_value()) {
        ScriptType baseType;
        if (!resolveTypeNameInScope(*declaration.baseName, declaration.namespacePath, baseType) ||
            !baseType.isClass() || baseType.isStruct()) {
            error("E0203", "base type '" + *declaration.baseName + "' is not a script class", declaration);
        } else if (cls.isStruct) {
            error("E0399", "a struct cannot have a base type", declaration);
        } else {
            auto* baseClass = dynamic_cast<ScriptClassType*>(baseType.object);
            const ClassDeclaration* baseDeclaration = nullptr;
            for (const auto& entry : classDeclarations_) {
                if (entry.second == baseClass) {
                    baseDeclaration = entry.first;
                    break;
                }
            }
            if (baseClass == &cls) {
                error("E0204", "class '" + declaration.name + "' cannot inherit from itself", declaration);
            } else if (baseDeclaration == nullptr) {
                error("E0205", "base class '" + baseClass->name + "' must be declared in the same module",
                      declaration);
            } else {
                layoutClass(*baseDeclaration);
                cls.base = baseClass;
            }
        }
    }

    if (cls.base != nullptr) {
        for (const auto& field : cls.base->fields) {
            cls.fieldIndex[field.name] = static_cast<int>(cls.fields.size());
            cls.fields.push_back(field);
        }
        cls.vtable = cls.base->vtable;
    }

    for (auto& memberPtr : declaration.members) {
        if (auto* field = dynamic_cast<FieldDeclaration*>(memberPtr.get())) {
            layoutField(cls, *field);
        } else if (auto* function = dynamic_cast<FunctionDeclaration*>(memberPtr.get())) {
            layoutMethod(cls, *function);
        } else if (auto* nested = dynamic_cast<ClassDeclaration*>(memberPtr.get())) {
            error("E0206", "nested classes are not supported ('" + nested->name + "')", *nested);
        }
    }

    layoutInProgress_.erase(&declaration);
    laidOut_.insert(&declaration);
}

void ModuleCompiler::layoutField(ScriptClassType& cls, const FieldDeclaration& declaration) {
    ScriptType fieldType;
    if (!resolveTypeRef(declaration.type, declaration, cls.namespacePath, fieldType)) fieldType = primI32();
    if (fieldType.isVoid()) {
        error("E0387", "field cannot have type 'void'", declaration);
        fieldType = primI32();
    }

    for (const auto& initializer : declaration.initializers) {
        if (declaration.isStatic) {
            std::string globalName = cls.name + "." + initializer.name;
            if (module_->globalsByName.count(globalName) > 0) {
                error("E0207", "static field '" + globalName + "' is already declared", initializer);
                continue;
            }
            GlobalVariable variable;
            variable.name = globalName;
            variable.type = fieldType;
            variable.isReadOnly = declaration.isReadOnly;
            variable.isStatic = true;
            variable.index = static_cast<int>(module_->globals.size());
            // A struct field may refer to a struct that has not been laid out
            // yet, so its zero value is materialized after all layouts.
            variable.value = fieldType.isStruct() ? Value::null()
                                                  : defaultValue(fieldType, engine_.garbageCollector());
            variable.declLine = initializer.line;
            module_->globalsByName[globalName] = variable.index;
            module_->globals.push_back(variable);
            if (initializer.value) {
                globalInitializers_.push_back(GlobalInitializer{variable.index, initializer.value.get(),
                                                                initializer.line, cls.namespacePath});
            }
            continue;
        }

        if (cls.fieldIndex.count(initializer.name) > 0) {
            error("E0208", "field '" + initializer.name + "' is already declared in '" + cls.name + "'",
                  initializer);
            continue;
        }
        FieldInfo field;
        field.name = initializer.name;
        field.type = fieldType;
        field.index = static_cast<int>(cls.fields.size());
        field.isReadOnly = declaration.isReadOnly;
        field.owner = &cls;
        cls.fieldIndex[field.name] = field.index;
        cls.fields.push_back(field);
        if (initializer.value) {
            fieldInitializers_[&cls].push_back(
                FieldInitializer{&cls.fields.back(), initializer.value.get(), initializer.line});
        }
    }
}

void ModuleCompiler::layoutMethod(ScriptClassType& cls, const FunctionDeclaration& declaration) {
    if (declaration.isFinalizer) {
        if (cls.isStruct) {
            error("E0451", "struct '" + cls.name + "' cannot declare a finalizer", declaration);
            return;
        }
        if (cls.finalizer != nullptr) {
            error("E0453", "class '" + cls.name + "' declares more than one finalizer", declaration);
            return;
        }
        auto* function = createFunctionStub(declaration, &cls);
        if (function == nullptr) return;
        function->name = "~" + declaration.name;
        function->vtableSlot = -1;
        // Kept out of `methods`: a finalizer cannot be called explicitly.
        cls.finalizer = function;
        return;
    }
    if (!declaration.typeParameters.empty()) {
        // Generic methods are monomorphized at each call site: no stub is created
        // here and they never take part in the vtable.
        if (declaration.isAbstract || declaration.isOverride) {
            error("E0444",
                  "generic method '" + declaration.name +
                      "' cannot be abstract, virtual or override: it is dispatched statically",
                  declaration);
        }
        registerGenericMethod(declaration, &cls, cls.namespacePath);
        return;
    }
    auto* function = createFunctionStub(declaration, &cls);
    if (function == nullptr) return;
    cls.methods[function->name].push_back(function);

    if (declaration.isConstructor) {
        if (cls.constructor != nullptr) {
            error("E0209", "class '" + cls.name + "' declares more than one constructor", declaration);
        } else {
            cls.constructor = function;
        }
        return;
    }
    if (function->isStatic) return;
    // Struct methods are never virtual: they are always dispatched statically.
    if (cls.isStruct) {
        function->vtableSlot = -1;
        return;
    }

    int slot = findVtableSlot(cls.vtable, *function);
    if (slot >= 0) {
        cls.vtable[slot] = function;
        function->vtableSlot = slot;
    } else {
        cls.vtable.push_back(function);
        function->vtableSlot = static_cast<int>(cls.vtable.size() - 1);
    }
}

void ModuleCompiler::validateStructCycles() {
    std::unordered_map<ScriptClassType*, int> state;
    for (const auto& entry : classDeclarations_) {
        auto* cls = entry.second;
        if (cls == nullptr || !cls->isStruct) continue;
        state.clear();
        std::function<bool(ScriptClassType&)> visit = [&](ScriptClassType& current) -> bool {
            int& mark = state[&current];
            if (mark == 1) return false;
            if (mark == 2) return true;
            mark = 1;
            for (auto& field : current.fields) {
                auto* inner = dynamic_cast<ScriptClassType*>(field.type.object);
                if (inner == nullptr || !inner->isStruct) continue;
                if (!visit(*inner)) return false;
            }
            mark = 2;
            return true;
        };
        if (!visit(*cls)) {
            error("E0400", "struct '" + cls->name + "' cannot contain a value of itself", *entry.first);
        }
    }
}

int ModuleCompiler::findVtableSlot(const std::vector<ScriptFunction*>& vtable,
                                   const ScriptFunction& function) {
    for (std::size_t i = 0; i < vtable.size(); ++i) {
        const auto* candidate = vtable[i];
        if (candidate->name != function.name ||
            candidate->parameters.size() != function.parameters.size()) {
            continue;
        }
        bool match = true;
        for (std::size_t p = 0; p < candidate->parameters.size(); ++p) {
            if (!candidate->parameters[p].type.sameAs(function.parameters[p].type) ||
                candidate->parameters[p].modifier != function.parameters[p].modifier) {
                match = false;
                break;
            }
        }
        if (match) return static_cast<int>(i);
    }
    return -1;
}

void ModuleCompiler::declareTopLevelFunctions() {
    for (auto& declarationPtr : unit_->declarations) {
        auto* declaration = dynamic_cast<FunctionDeclaration*>(declarationPtr.get());
        if (declaration == nullptr) continue;
        if (!declaration->typeParameters.empty()) {
            registerGenericMethod(*declaration, nullptr, declaration->namespacePath);
            continue;
        }
        auto* function = createFunctionStub(*declaration, nullptr);
        if (function == nullptr) continue;
        function->isStatic = true;
        module_->functionsByName[qualify(declaration->namespacePath, function->name)].push_back(function);
    }
}

ScriptFunction* ModuleCompiler::createFunctionStub(const FunctionDeclaration& declaration,
                                                   ScriptClassType* owner) {
    auto existing = functionDeclarations_.find(&declaration);
    if (existing != functionDeclarations_.end()) return existing->second;

    auto functionPtr = std::make_unique<ScriptFunction>();
    auto* function = functionPtr.get();
    function->name = declaration.name;
    function->namespacePath = declaration.namespacePath;
    function->module = module_.get();
    function->owner = owner;
    function->isStatic = declaration.isStatic;
    function->isConstructor = declaration.isConstructor;
    function->isAbstract = declaration.isAbstract;
    function->file = module_->sourceFile;
    function->declLine = declaration.line;

    const std::string scope = owner != nullptr ? owner->namespacePath : declaration.namespacePath;
    if (declaration.isConstructor || !declaration.returnType.has_value()) {
        function->returnType = primVoid();
    } else {
        ScriptType returnType;
        function->returnType =
            resolveTypeRef(*declaration.returnType, declaration, scope, returnType) ? returnType : primVoid();
    }

    for (const auto& parameter : declaration.parameters) {
        ScriptType parameterType;
        if (!resolveTypeRef(parameter.type, parameter, scope, parameterType)) parameterType = primI32();
        if (parameterType.isVoid()) {
            error("E0394", "parameter cannot have type 'void'", parameter);
            parameterType = primI32();
        }
        function->parameters.push_back(
            ScriptParameter{parameter.name, parameterType, parameter.modifier});
    }

    if (owner != nullptr && !declaration.isConstructor && !declaration.isStatic &&
        !declaration.isAbstract && declaration.body == nullptr) {
        error("E0210", "method '" + declaration.name + "' has no body", declaration);
    }

    functionDeclarations_[&declaration] = function;
    pendingBodies_.push_back({&declaration, function});
    module_->functions.push_back(std::move(functionPtr));
    return function;
}

ScriptFunction* ModuleCompiler::compileLambda(
    const LambdaExpression& lambda, ScriptDelegateType& type, const std::string& scope,
    ScriptClassType* owner, bool isStatic,
    const std::vector<std::pair<std::string, ScriptType>>& captures) {
    auto* declaration = const_cast<FunctionDeclaration*>(lambda.declaration.get());
    if (declaration == nullptr) return nullptr;
    auto existing = functionDeclarations_.find(declaration);
    if (existing != functionDeclarations_.end()) return existing->second;

    if (declaration->parameters.size() != type.parameterTypes.size()) {
        error("E0412",
              "lambda has " + std::to_string(declaration->parameters.size()) + " parameter(s) but delegate '" +
                  type.name + "' expects " + std::to_string(type.parameterTypes.size()),
              lambda);
        return nullptr;
    }

    for (const auto& parameter : declaration->parameters) {
        if (parameter.modifier != ParamModifier::None) {
            error("E0424", "lambda parameters cannot be 'ref'/'out'/'in'", parameter);
        }
    }

    for (std::size_t i = 0; i < declaration->parameters.size(); ++i) {
        auto& parameter = declaration->parameters[i];
        if (parameter.type.name.empty()) {
            parameter.type = typeRefFromType(type.parameterTypes[i]);
        } else {
            ScriptType declaredType;
            if (!resolveTypeRef(parameter.type, parameter, scope, declaredType)) return nullptr;
            if (!declaredType.sameAs(type.parameterTypes[i])) {
                error("E0413", "lambda parameter '" + parameter.name + "' has type '" + declaredType.name +
                                   "' but delegate '" + type.name + "' requires '" +
                                   type.parameterTypes[i].name + "'", parameter);
                return nullptr;
            }
        }
        if (parameter.name.empty()) parameter.name = "arg" + std::to_string(i);
    }
    declaration->returnType = typeRefFromType(type.returnType);
    if (type.returnType.isVoid() && lambda.expressionBody && declaration->body != nullptr &&
        declaration->body->statements.size() == 1) {
        // `m => print(m)` for a void delegate is an expression *statement*.
        if (auto* ret = dynamic_cast<ReturnStatement*>(declaration->body->statements[0].get())) {
            const auto* unary = dynamic_cast<UnaryExpression*>(ret->value.get());
            if (dynamic_cast<CallExpression*>(ret->value.get()) == nullptr &&
                dynamic_cast<AssignmentExpression*>(ret->value.get()) == nullptr &&
                dynamic_cast<NewObjectExpression*>(ret->value.get()) == nullptr &&
                !(unary != nullptr && (unary->op == "++" || unary->op == "--"))) {
                error("E0414", "a void lambda expression body must be a statement expression", lambda);
                return nullptr;
            }
            auto statement = std::make_unique<ExpressionStatement>();
            statement->line = ret->line;
            statement->column = ret->column;
            statement->expression = std::move(ret->value);
            declaration->body->statements[0] = std::move(statement);
        }
    }
    declaration->name = "__lambda_" + std::to_string(++lambdaCounter_);
    lambdaDeclarations_.insert(declaration);
    declaration->namespacePath = scope;
    declaration->isStatic = isStatic;

    auto* function = createFunctionStub(*declaration, owner);
    if (function == nullptr) return nullptr;
    function->isStatic = isStatic;
    FunctionCompiler compiler(*this, function, declaration, &captures);
    compiler.compile();
    return function;
}

void ModuleCompiler::declareTopLevelGlobals() {
    for (auto& declarationPtr : unit_->declarations) {
        auto* declaration = dynamic_cast<GlobalVariableDeclaration*>(declarationPtr.get());
        if (declaration == nullptr) continue;
        ScriptType type;
        if (!resolveTypeRef(declaration->type, *declaration, declaration->namespacePath, type)) type = primI32();
        if (type.isVoid()) {
            error("E0395", "global variable cannot have type 'void'", *declaration);
            type = primI32();
        }
        for (const auto& initializer : declaration->initializers) {
            const std::string qualifiedName = qualify(declaration->namespacePath, initializer.name);
            if (module_->globalsByName.count(qualifiedName) > 0) {
                error("E0213", "global variable '" + qualifiedName + "' is already declared", initializer);
                continue;
            }
            GlobalVariable variable;
            variable.name = qualifiedName;
            variable.type = type;
            variable.isReadOnly = declaration->isConst;
            variable.isStatic = false;
            variable.index = static_cast<int>(module_->globals.size());
            variable.value = defaultValue(type, engine_.garbageCollector());
            variable.declLine = initializer.line;
            module_->globalsByName[variable.name] = variable.index;
            module_->globals.push_back(variable);
            if (initializer.value) {
                globalInitializers_.push_back(GlobalInitializer{variable.index, initializer.value.get(),
                                                                initializer.line, declaration->namespacePath});
            } else if (declaration->isConst) {
                error("E0214", "const variable '" + initializer.name + "' requires an initializer", initializer);
            }
        }
    }
}

void ModuleCompiler::resolveEntryPoint() {
    auto it = module_->functionsByName.find("main");
    if (it == module_->functionsByName.end()) return;
    for (auto* function : it->second) {
        if (!function->parameters.empty()) continue;
        if (!function->returnType.isVoid() && !function->returnType.sameAs(primI32())) {
            error("E0212", "the entry point 'main' must return void or int", Node{});  // line 1
            return;
        }
        module_->entryPoint = function;
        return;
    }
}

void ModuleCompiler::compileFunctionBodies() {}

void ModuleCompiler::ensureConstructor(ScriptClassType& cls,
                                       std::unordered_map<ScriptClassType*, bool>& visited) {
    if (visited[&cls]) return;
    visited[&cls] = true;
    if (cls.base != nullptr) ensureConstructor(*cls.base, visited);
    if (cls.constructor != nullptr) return;

    auto functionPtr = std::make_unique<ScriptFunction>();
    auto* function = functionPtr.get();
    function->name = unqualified(cls.name);
    function->namespacePath = cls.namespacePath;
    function->module = module_.get();
    function->owner = &cls;
    function->isConstructor = true;
    function->returnType = primVoid();
    function->file = module_->sourceFile;
    cls.constructor = function;
    module_->functions.push_back(std::move(functionPtr));

    FunctionCompiler compiler(*this, function, nullptr);
    compiler.compileSynthetic([](FunctionCompiler& self) { self.emitConstructorPrologue(); });
}

void ModuleCompiler::compileGlobalInitializer() {
    if (globalInitializers_.empty()) return;

    auto functionPtr = std::make_unique<ScriptFunction>();
    auto* function = functionPtr.get();
    function->name = "__global_init";
    function->module = module_.get();
    function->isStatic = true;
    function->returnType = primVoid();
    function->file = module_->sourceFile;
    module_->globalInit = function;
    module_->functions.push_back(std::move(functionPtr));

    FunctionCompiler compiler(*this, function, nullptr);
    compiler.compileSynthetic([this](FunctionCompiler& self) {
        for (const auto& initializer : globalInitializers_) {
            if (initializer.globalIndex >= static_cast<int>(module_->globals.size())) continue;
            auto& variable = module_->globals[initializer.globalIndex];
            self.setLine(initializer.line);
            self.setNamespaceScope(initializer.namespacePath);
            if (initializer.initializer != nullptr) {
                self.compileExpression(*initializer.initializer, &variable.type);
                if (variable.type.isStruct()) self.emit(OpCode::CloneStruct, 0, 0);
            } else {
                self.emitDefault(variable.type);
            }
            self.emit(OpCode::StoreGlobal, variable.index, -1);
        }
    });
}

std::string ModuleCompiler::qualify(const std::string& scope, const std::string& name) {
    if (scope.empty()) return name;
    if (name.empty()) return scope;
    return scope + "." + name;
}

std::string ModuleCompiler::unqualified(const std::string& name) {
    auto dot = name.rfind('.');
    return dot == std::string::npos ? name : name.substr(dot + 1);
}

std::vector<std::string> ModuleCompiler::candidateNames(const std::string& rawName,
                                                        const std::string& scope) const {
    std::vector<std::string> result;
    auto push = [&result](std::string name) {
        if (name.empty()) return;
        if (std::find(result.begin(), result.end(), name) == result.end()) {
            result.push_back(std::move(name));
        }
    };

    // Innermost namespace first, then each enclosing one, then the global scope.
    std::vector<std::string> scopes;
    std::string current = scope;
    while (true) {
        scopes.push_back(current);
        auto dot = current.rfind('.');
        if (dot == std::string::npos) break;
        current.erase(dot);
    }

    for (const auto& level : scopes) {
        push(qualify(level, rawName));
        // `using` directives declared in this namespace apply here; a using at
        // the global scope applies everywhere.
        for (const auto& directive : unit_->usings) {
            if (directive.namespacePath == level) push(qualify(directive.path, rawName));
        }
    }
    push(rawName);
    return result;
}

bool ModuleCompiler::resolveTypeNameInScope(const std::string& rawName, const std::string& scope,
                                            ScriptType& result) const {
    // Built-in primitives and host registered types are not namespaced.
    if (engine_.resolveTypeName(rawName, nullptr, result)) return true;

    for (const auto& candidate : candidateNames(rawName, scope)) {
        auto it = module_->classesByName.find(candidate);
        if (it != module_->classesByName.end()) {
            result = ScriptType(candidate, Primitive::None, it->second);
            return true;
        }
        auto delegateIt = module_->delegatesByName.find(candidate);
        if (delegateIt != module_->delegatesByName.end()) {
            result = ScriptType(candidate, Primitive::None, delegateIt->second);
            return true;
        }
    }
    return false;
}

bool ModuleCompiler::findGlobalInScope(const std::string& rawName, const std::string& scope,
                                       int& index) const {
    for (const auto& candidate : candidateNames(rawName, scope)) {
        auto it = module_->globalsByName.find(candidate);
        if (it != module_->globalsByName.end()) {
            index = it->second;
            return true;
        }
    }
    return false;
}

std::vector<ScriptFunction*> ModuleCompiler::findFunctionsInScope(const std::string& rawName,
                                                                 const std::string& scope,
                                                                 std::size_t arity) const {
    for (const auto& candidate : candidateNames(rawName, scope)) {
        auto it = module_->functionsByName.find(candidate);
        if (it == module_->functionsByName.end()) continue;
        std::vector<ScriptFunction*> matches;
        for (auto* function : it->second) {
            if (function->parameters.size() == arity) matches.push_back(function);
        }
        if (!matches.empty()) return matches;
    }
    return {};
}

std::string ModuleCompiler::builtinDelegateName(const std::string& rawName,
                                               const std::string& scope) const {
    for (const auto& candidate : candidateNames(rawName, scope)) {
        if (candidate == "System.Func" || candidate == "System.Action") return candidate;
        if (genericClasses_.count(candidate) > 0 || genericDelegates_.count(candidate) > 0 ||
            module_->classesByName.count(candidate) > 0 || module_->delegatesByName.count(candidate) > 0 ||
            engine_.findNativeType(candidate) != nullptr) {
            return {};
        }
        if (candidate == "Func" || candidate == "Action") return "System." + candidate;
    }
    return {};
}

ScriptDelegateType* ModuleCompiler::instantiateBuiltinDelegate(
    const std::string& name, const std::vector<ScriptType>& arguments, const Node& node) {
    const bool isFunc = name == "System.Func";
    if ((isFunc && arguments.empty()) || arguments.size() > (isFunc ? 17u : 16u)) {
        error("E0432", "'" + name + "' accepts " + (isFunc ? "1 to 17" : "0 to 16") +
                           " type argument(s), got " + std::to_string(arguments.size()), node);
        return nullptr;
    }
    for (const auto& argument : arguments) {
        if (argument.isVoid() || argument.isVar() || argument.isNullType()) {
            error("E0445", "'" + argument.name + "' cannot be a type argument of '" + name + "'", node);
            return nullptr;
        }
    }
    const std::string key = arguments.empty() ? name : name + "<" + joinTypeNames(arguments) + ">";
    auto cached = delegateInstances_.find(key);
    if (cached != delegateInstances_.end()) return cached->second;
    auto type = std::make_unique<ScriptDelegateType>();
    auto* result = type.get();
    type->name = key;
    type->namespacePath = "System";
    type->module = module_.get();
    type->returnType = isFunc ? arguments.back() : primVoid();
    type->parameterTypes.assign(arguments.begin(), isFunc ? arguments.end() - 1 : arguments.end());
    type->typeArguments = arguments;
    if (!arguments.empty()) type->templateName = name;
    type->parameterVariances.assign(arguments.size(), Variance::In);
    if (isFunc) type->parameterVariances.back() = Variance::Out;
    module_->delegatesByName[key] = result;
    module_->delegates.push_back(std::move(type));
    delegateInstances_[key] = result;
    return result;
}

bool ModuleCompiler::resolveTypeRef(const TypeRef& typeRef, const Node& node, const std::string& scope,
                                    ScriptType& result) {
    const std::string builtinName = builtinDelegateName(typeRef.name, scope);
    if (!builtinName.empty()) {
        if (builtinName == "System.Func" && typeRef.typeArguments.empty()) {
            error("E0431", "generic type 'System.Func' requires a return type argument", node);
            return false;
        }
        std::vector<ScriptType> arguments;
        for (const auto& argumentRef : typeRef.typeArguments) {
            ScriptType argumentType;
            if (!resolveTypeRef(argumentRef, node, scope, argumentType)) return false;
            arguments.push_back(argumentType);
        }
        auto* delegateType = instantiateBuiltinDelegate(builtinName, arguments, node);
        if (delegateType == nullptr) return false;
        result = ScriptType(delegateType->name, Primitive::None, delegateType);
        result.typeArguments = delegateType->typeArguments;
        result.templateName = delegateType->templateName;
    } else if (!typeRef.typeArguments.empty()) {
        GenericTemplate tmpl;
        GenericDelegateTemplate delegateTmpl;
        if (!findGenericTemplate(typeRef.name, scope, tmpl) &&
            !findGenericDelegateTemplate(typeRef.name, scope, delegateTmpl)) {
            error("E0430", "unknown generic type '" + typeRef.name + "'", node);
            return false;
        }
        std::vector<ScriptType> arguments;
        for (const auto& argumentRef : typeRef.typeArguments) {
            ScriptType argumentType;
            if (!resolveTypeRef(argumentRef, node, scope, argumentType)) argumentType = primI32();
            arguments.push_back(argumentType);
        }
        if (tmpl.declaration != nullptr) {
            auto* cls = instantiateGenericClass(tmpl, arguments, node);
            if (cls == nullptr) return false;
            result = ScriptType(cls->name, Primitive::None, cls);
            result.typeArguments = std::move(arguments);
            result.templateName = tmpl.qualifiedName;
        } else {
            auto* delegateType = instantiateGenericDelegate(delegateTmpl, arguments, node);
            if (delegateType == nullptr) return false;
            result = ScriptType(delegateType->name, Primitive::None, delegateType);
            result.typeArguments = std::move(arguments);
            result.templateName = delegateTmpl.qualifiedName;
        }
    } else {
        GenericTemplate tmpl;
        GenericDelegateTemplate delegateTmpl;
        if (findGenericTemplate(typeRef.name, scope, tmpl) ||
            findGenericDelegateTemplate(typeRef.name, scope, delegateTmpl)) {
            error("E0431", "generic type '" + typeRef.name + "' requires type arguments", node);
            return false;
        }
        if (!resolveTypeNameInScope(typeRef.name, scope, result)) {
            error("E0215", "unknown type '" + typeRef.name + "'", node);
            return false;
        }
    }
    for (int i = 0; i < typeRef.arrayDepth; ++i) {
        if (result.isVoid()) {
            error("E0216", "arrays of void are not allowed", node);
            result = primI32();
            return true;
        }
        result = engine_.getArrayType(result);
    }
    return true;
}

bool ModuleCompiler::findGenericTemplate(const std::string& rawName, const std::string& scope,
                                         GenericTemplate& result) const {
    for (const auto& candidate : candidateNames(rawName, scope)) {
        auto it = genericClasses_.find(candidate);
        if (it != genericClasses_.end()) {
            result = it->second;
            return true;
        }
    }
    return false;
}

bool ModuleCompiler::findGenericDelegateTemplate(const std::string& rawName, const std::string& scope,
                                                 GenericDelegateTemplate& result) const {
    for (const auto& candidate : candidateNames(rawName, scope)) {
        auto it = genericDelegates_.find(candidate);
        if (it != genericDelegates_.end()) {
            result = it->second;
            return true;
        }
    }
    return false;
}

bool ModuleCompiler::checkTypeConstraints(const std::vector<TypeParameter>& parameters,
                                          const std::vector<ScriptType>& arguments,
                                          const std::string& displayName, const std::string& scope,
                                          const Node& node) {
    for (std::size_t i = 0; i < parameters.size() && i < arguments.size(); ++i) {
        const auto& parameter = parameters[i];
        const ScriptType& argument = arguments[i];
        for (const auto& constraint : parameter.constraints) {
            bool satisfied = true;
            std::string description;
            switch (constraint.kind) {
                case ConstraintKind::Class:
                    description = "'class'";
                    satisfied = argument.isReference();
                    break;
                case ConstraintKind::Struct:
                    description = "'struct'";
                    // Non-nullable value types: script structs and numeric/bool/char.
                    satisfied = argument.isStruct() || (argument.isPrimitive() && !argument.isVoid() &&
                                                        !argument.isNullType() && !argument.isVar() &&
                                                        !argument.isString());
                    break;
                case ConstraintKind::New:
                    description = "'new()'";
                    if (auto* cls = dynamic_cast<ScriptClassType*>(argument.object)) {
                        satisfied = cls->constructor == nullptr || cls->constructor->parameters.empty();
                    } else {
                        satisfied = false;
                    }
                    break;
                case ConstraintKind::BaseType: {
                    description = "'" + constraint.type.displayName() + "'";
                    ScriptType baseType;
                    if (resolveTypeRef(constraint.type, node, scope, baseType)) {
                        satisfied = baseType.sameAs(argument) || baseType.assignableFrom(argument);
                    } else {
                        satisfied = false;
                    }
                    break;
                }
            }
            if (!satisfied) {
                error("E0435",
                      "type argument '" + argument.name + "' does not satisfy the " + description +
                          " constraint on type parameter '" + parameter.name + "' of '" + displayName + "'",
                      node);
                return false;
            }
        }
    }
    return true;
}

void ModuleCompiler::validateDelegateVariance(const DelegateDeclaration& declaration) {
    for (const auto& parameter : declaration.typeParameters) {
        if (parameter.variance == Variance::None) continue;
        if (parameter.variance == Variance::Out) {
            for (const auto& input : declaration.parameters) {
                if (typeRefMentions(input.type, parameter.name)) {
                    error("E0441",
                          "covariant type parameter '" + parameter.name +
                              "' cannot be used in an input position",
                          parameter);
                    break;
                }
            }
        } else if (typeRefMentions(declaration.returnType, parameter.name)) {
            error("E0442",
                  "contravariant type parameter '" + parameter.name +
                      "' cannot be used in an output position",
                  parameter);
        }
    }
}

ScriptDelegateType* ModuleCompiler::instantiateGenericDelegate(const GenericDelegateTemplate& tmpl,
                                                              const std::vector<ScriptType>& arguments,
                                                              const Node& node) {
    if (arguments.size() != tmpl.parameters.size()) {
        error("E0432",
              "generic type '" + tmpl.qualifiedName + "' expects " +
                  std::to_string(tmpl.parameters.size()) + " type argument(s), got " +
                  std::to_string(arguments.size()),
              *tmpl.declaration);
        return nullptr;
    }

    const std::string key = tmpl.qualifiedName + "<" + joinTypeNames(arguments) + ">";
    auto cached = delegateInstances_.find(key);
    if (cached != delegateInstances_.end()) return cached->second;
    if (!checkTypeConstraints(tmpl.declaration->typeParameters, arguments,
                              templateDisplayName(tmpl.qualifiedName, tmpl.parameters),
                              tmpl.declaration->namespacePath, node)) {
        return nullptr;
    }

    // Register the shell first so a delegate may refer to itself.
    auto typePtr = std::make_unique<ScriptDelegateType>();
    auto* type = typePtr.get();
    type->name = key;
    type->namespacePath = tmpl.declaration->namespacePath;
    type->module = module_.get();
    type->templateName = tmpl.qualifiedName;
    type->typeArguments = arguments;
    type->parameterVariances = tmpl.variances;
    module_->delegatesByName[key] = type;
    module_->delegates.push_back(std::move(typePtr));
    delegateInstances_[key] = type;

    std::unordered_map<std::string, ScriptType> typeEnv;
    for (std::size_t i = 0; i < tmpl.parameters.size(); ++i) typeEnv[tmpl.parameters[i]] = arguments[i];

    TypeRef returnRef = tmpl.declaration->returnType;
    substituteTypeRef(returnRef, typeEnv);
    type->returnType = primVoid();
    resolveTypeRef(returnRef, *tmpl.declaration, tmpl.declaration->namespacePath, type->returnType);

    for (const auto& parameter : tmpl.declaration->parameters) {
        TypeRef parameterRef = parameter.type;
        substituteTypeRef(parameterRef, typeEnv);
        ScriptType parameterType;
        if (!resolveTypeRef(parameterRef, parameter, tmpl.declaration->namespacePath, parameterType)) {
            parameterType = primI32();
        }
        type->parameterTypes.push_back(parameterType);
    }
    return type;
}

ScriptClassType* ModuleCompiler::instantiateGenericClass(const GenericTemplate& tmpl,
                                                         const std::vector<ScriptType>& arguments,
                                                         const Node& node) {
    if (arguments.size() != tmpl.parameters.size()) {
        error("E0432",
              "generic type '" + tmpl.qualifiedName + "' expects " +
                  std::to_string(tmpl.parameters.size()) + " type argument(s), got " +
                  std::to_string(arguments.size()),
              *tmpl.declaration);
        return nullptr;
    }

    const std::string key = tmpl.qualifiedName + "<" + joinTypeNames(arguments) + ">";
    auto cached = genericInstances_.find(key);
    if (cached != genericInstances_.end()) return cached->second;
    if (!checkTypeConstraints(tmpl.declaration->typeParameters, arguments,
                              templateDisplayName(tmpl.qualifiedName, tmpl.parameters),
                              tmpl.declaration->namespacePath, node)) {
        return nullptr;
    }

    // Register the shell before laying out the body so recursive generics work.
    auto classPtr = std::make_unique<ScriptClassType>();
    auto* cls = classPtr.get();
    cls->name = key;
    cls->namespacePath = tmpl.declaration->namespacePath;
    cls->module = module_.get();
    cls->isStruct = tmpl.declaration->isStruct;
    cls->isAbstract = tmpl.declaration->isAbstract;
    module_->classesByName[key] = cls;
    module_->classes.push_back(std::move(classPtr));
    genericInstances_[key] = cls;

    const std::string& source = *sourceFor(tmpl.declaration);
    const int start = tmpl.declaration->sourceStart;
    const int end = tmpl.declaration->sourceEnd;
    if (start < 0 || end <= start || end > static_cast<int>(source.size())) {
        error("E0433", "cannot re-instantiate generic type '" + tmpl.qualifiedName + "'", *tmpl.declaration);
        return cls;
    }
    // The fragment text must outlive the instantiated method declarations,
    // which refer back to their own offsets into it.
    fragmentSources_.push_back(std::make_unique<std::string>(padFragmentText(
        source.substr(static_cast<std::size_t>(start), static_cast<std::size_t>(end - start)),
        tmpl.declaration->line)));
    const std::string* fragmentSource = fragmentSources_.back().get();

    auto fragment = Parser::parse(*fragmentSource, diagnostics_);
    registerDeclarationSources(*fragment, fragmentSource);
    ClassDeclaration* declaration = nullptr;
    for (auto& fragmentNode : fragment->declarations) {
        if (auto* candidate = dynamic_cast<ClassDeclaration*>(fragmentNode.get())) {
            declaration = candidate;
            instantiatedDeclarations_.emplace_back(static_cast<ClassDeclaration*>(fragmentNode.release()));
            break;
        }
    }
    if (declaration == nullptr) {
        error("E0434", "failed to instantiate generic type '" + tmpl.qualifiedName + "'", *tmpl.declaration);
        return cls;
    }

    std::unordered_map<std::string, ScriptType> typeEnv;
    for (std::size_t i = 0; i < tmpl.parameters.size(); ++i) {
        typeEnv[tmpl.parameters[i]] = arguments[i];
    }
    substituteDeclarationTypes(*declaration, typeEnv);

    declaration->name = key;
    declaration->namespacePath = tmpl.declaration->namespacePath;
    declaration->typeParameters.clear();
    declaration->sourceStart = -1;
    declaration->sourceEnd = -1;

    classDeclarations_[declaration] = cls;
    layoutClass(*declaration);
    std::unordered_map<ScriptClassType*, bool> visited;
    ensureConstructor(*cls, visited);
    return cls;
}

void ModuleCompiler::registerGenericMethod(const FunctionDeclaration& declaration,
                                           ScriptClassType* owner, const std::string& namespacePath) {
    GenericMethodTemplate tmpl;
    tmpl.declaration = const_cast<FunctionDeclaration*>(&declaration);
    tmpl.owner = owner;
    tmpl.namespacePath = namespacePath;
    tmpl.qualifiedName = owner != nullptr ? owner->name + "." + declaration.name
                                          : qualify(namespacePath, declaration.name);
    tmpl.source = sourceFor(&declaration);
    for (const auto& parameter : declaration.typeParameters) tmpl.parameters.push_back(parameter.name);
    genericMethods_[tmpl.qualifiedName].push_back(std::move(tmpl));
}

bool ModuleCompiler::unifyTypeArgument(const TypeRef& pattern, const ScriptType& actual,
                                       const std::vector<std::string>& parameters,
                                       std::unordered_map<std::string, ScriptType>& bindings) {
    const ScriptType* element = &actual;
    for (int i = 0; i < pattern.arrayDepth; ++i) {
        if (!element->isArray()) return false;
        element = element->element.get();
    }
    const bool isParameter =
        std::find(parameters.begin(), parameters.end(), pattern.name) != parameters.end();
    if (isParameter) {
        auto inserted = bindings.emplace(pattern.name, *element);
        if (!inserted.second) return inserted.first->second.sameAs(*element);
        return true;
    }
    if (pattern.typeArguments.empty()) return true;
    if (element->typeArguments.size() != pattern.typeArguments.size()) return false;
    bool ok = true;
    for (std::size_t i = 0; i < pattern.typeArguments.size(); ++i) {
        if (!unifyTypeArgument(pattern.typeArguments[i], element->typeArguments[i], parameters,
                               bindings)) {
            ok = false;
        }
    }
    return ok;
}

ScriptFunction* ModuleCompiler::instantiateGenericMethod(const GenericMethodTemplate& tmpl,
                                                         const std::vector<ScriptType>& arguments,
                                                         const Node& node) {
    const std::string key = tmpl.qualifiedName + "<" + joinTypeNames(arguments) + ">";
    auto cached = genericMethodInstances_.find(key);
    if (cached != genericMethodInstances_.end()) return cached->second;

    if (arguments.size() != tmpl.parameters.size()) {
        error("E0438",
              "generic method '" + tmpl.qualifiedName + "' expects " +
                  std::to_string(tmpl.parameters.size()) + " type argument(s), got " +
                  std::to_string(arguments.size()),
              node);
        return nullptr;
    }
    if (genericInstantiationCount_ >= kMaxGenericInstantiations) {
        error("E0437",
              "generic instantiation limit exceeded while instantiating '" + tmpl.qualifiedName + "'", node);
        return nullptr;
    }
    if (!checkTypeConstraints(tmpl.declaration->typeParameters, arguments,
                              templateDisplayName(tmpl.qualifiedName, tmpl.parameters),
                              tmpl.namespacePath, node)) {
        return nullptr;
    }
    ++genericInstantiationCount_;

    const std::string* source = tmpl.source != nullptr ? tmpl.source : &module_->source;
    const int start = tmpl.declaration->sourceStart;
    const int end = tmpl.declaration->sourceEnd;
    if (start < 0 || end <= start || end > static_cast<int>(source->size())) {
        error("E0433", "cannot re-instantiate generic method '" + tmpl.qualifiedName + "'",
              *tmpl.declaration);
        return nullptr;
    }

    auto fragment = Parser::parse(padFragmentText(source->substr(static_cast<std::size_t>(start),
                                                                static_cast<std::size_t>(end - start)),
                                                  tmpl.declaration->line),
                                  diagnostics_);
    FunctionDeclaration* declaration = nullptr;
    for (auto& fragmentNode : fragment->declarations) {
        if (auto* candidate = dynamic_cast<FunctionDeclaration*>(fragmentNode.get())) {
            declaration = candidate;
            instantiatedFunctions_.emplace_back(static_cast<FunctionDeclaration*>(fragmentNode.release()));
            break;
        }
    }
    if (declaration == nullptr) {
        error("E0434", "failed to instantiate generic method '" + tmpl.qualifiedName + "'",
              *tmpl.declaration);
        return nullptr;
    }

    std::unordered_map<std::string, ScriptType> typeEnv;
    for (std::size_t i = 0; i < tmpl.parameters.size(); ++i) {
        typeEnv[tmpl.parameters[i]] = arguments[i];
    }
    substituteDeclarationTypes(*declaration, typeEnv);

    // Suffix the instantiation so stack traces and the disassembler show the
    // concrete signature (`Map<int>`); the name is never used for lookup.
    declaration->name = tmpl.declaration->name + "<" + joinTypeNames(arguments) + ">";
    declaration->namespacePath = tmpl.namespacePath;
    declaration->typeParameters.clear();
    declaration->isStatic = tmpl.declaration->isStatic || tmpl.owner == nullptr;
    declaration->isOverride = false;
    declaration->isAbstract = false;
    declaration->isConstructor = false;
    declaration->sourceStart = -1;
    declaration->sourceEnd = -1;
    declaration->line = tmpl.declaration->line;
    declaration->column = tmpl.declaration->column;

    // The body is compiled later, so recursion resolves through this cache entry.
    genericMethodInstances_[key] = nullptr;
    auto* function = createFunctionStub(*declaration, tmpl.owner);
    if (function == nullptr) {
        genericMethodInstances_.erase(key);
        return nullptr;
    }
    function->isStatic = declaration->isStatic;
    // Generic methods are statically dispatched: they never take a vtable slot.
    function->vtableSlot = -1;
    genericMethodInstances_[key] = function;
    return function;
}

ModuleCompiler::GenericCallStatus ModuleCompiler::resolveGenericMethod(
    const std::string& rawName, const std::string& scope, ScriptClassType* owner, bool wantStatic,
    const std::vector<TypeRef>& explicitArguments,
    const std::vector<std::optional<ScriptType>>& argumentTypes, const Node& node,
    ScriptFunction*& result) {
    result = nullptr;

    std::vector<GenericMethodTemplate*> candidates;
    if (owner != nullptr) {
        for (ScriptClassType* current = owner; current != nullptr; current = current->base) {
            auto it = genericMethods_.find(current->name + "." + rawName);
            if (it == genericMethods_.end()) continue;
            for (auto& tmpl : it->second) {
                if (tmpl.owner != current) continue;
                if (tmpl.declaration->isStatic != wantStatic) continue;
                if (tmpl.declaration->parameters.size() != argumentTypes.size()) continue;
                candidates.push_back(&tmpl);
            }
            if (!candidates.empty()) break;
        }
    } else {
        for (const auto& candidate : candidateNames(rawName, scope)) {
            auto it = genericMethods_.find(candidate);
            if (it == genericMethods_.end()) continue;
            for (auto& tmpl : it->second) {
                if (tmpl.declaration->parameters.size() != argumentTypes.size()) continue;
                candidates.push_back(&tmpl);
            }
            if (!candidates.empty()) break;
        }
    }
    if (candidates.empty()) return GenericCallStatus::NoMatch;

    for (auto* tmpl : candidates) {
        std::vector<ScriptType> arguments;
        if (!explicitArguments.empty()) {
            if (explicitArguments.size() != tmpl->parameters.size()) {
                error("E0438",
                      "generic method '" + tmpl->qualifiedName + "' expects " +
                          std::to_string(tmpl->parameters.size()) + " type argument(s), got " +
                          std::to_string(explicitArguments.size()),
                      node);
                return GenericCallStatus::Failed;
            }
            for (const auto& reference : explicitArguments) {
                ScriptType resolved;
                if (!resolveTypeRef(reference, node, scope, resolved)) resolved = primI32();
                arguments.push_back(resolved);
            }
        } else {
            std::unordered_map<std::string, ScriptType> bindings;
            for (std::size_t i = 0; i < argumentTypes.size(); ++i) {
                if (!argumentTypes[i].has_value()) continue;
                unifyTypeArgument(tmpl->declaration->parameters[i].type, *argumentTypes[i],
                                  tmpl->parameters, bindings);
            }
            bool complete = true;
            for (const auto& name : tmpl->parameters) {
                auto it = bindings.find(name);
                if (it == bindings.end()) {
                    complete = false;
                    break;
                }
                arguments.push_back(it->second);
            }
            if (!complete) {
                error("E0436",
                      "cannot infer type arguments for generic method '" + tmpl->qualifiedName + "'", node);
                return GenericCallStatus::Failed;
            }
        }

        auto* function = instantiateGenericMethod(*tmpl, arguments, node);
        if (function != nullptr) {
            result = function;
            return GenericCallStatus::Resolved;
        }
    }
    return GenericCallStatus::Failed;
}

const std::vector<FieldInitializer>& ModuleCompiler::fieldInitializers(ScriptClassType* cls) const {
    static const std::vector<FieldInitializer> empty;
    auto it = fieldInitializers_.find(cls);
    return it == fieldInitializers_.end() ? empty : it->second;
}

}  // namespace sharpscript
