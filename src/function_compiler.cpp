#include "sharpscript/function_compiler.hpp"

#include <cctype>
#include <optional>

namespace sharpscript {
namespace {

const std::unordered_map<std::string, bool>& keywordTypes() {
    static const std::unordered_map<std::string, bool> names = {
        {"void", true}, {"bool", true}, {"byte", true}, {"sbyte", true}, {"short", true},
        {"ushort", true}, {"int", true}, {"uint", true}, {"long", true}, {"ulong", true},
        {"float", true}, {"double", true}, {"char", true}, {"string", true}, {"var", true}};
    return names;
}

bool isRelationalOp(const std::string& op) {
    return op == "<" || op == "<=" || op == ">" || op == ">=";
}
bool isEqualityOp(const std::string& op) { return op == "==" || op == "!="; }
bool isComparisonOp(const std::string& op) { return isRelationalOp(op) || isEqualityOp(op); }
bool isBitwiseOp(const std::string& op) {
    return op == "&" || op == "|" || op == "^" || op == "<<" || op == ">>";
}

void sortDelegateOverloads(std::vector<ScriptFunction*>& candidates, const ScriptDelegateType& type) {
    auto score = [&](const ScriptFunction* function) {
        int exact = 0;
        for (std::size_t i = 0; i < type.parameterTypes.size(); ++i) {
            if (function->parameters[i].type.sameAs(type.parameterTypes[i])) ++exact;
        }
        return exact;
    };
    std::stable_sort(candidates.begin(), candidates.end(),
                     [&](const ScriptFunction* a, const ScriptFunction* b) { return score(a) > score(b); });
}

bool expressionHasLambda(const Expression& expression);

bool statementHasLambda(const Statement& statement) {
    if (auto* block = dynamic_cast<const BlockStatement*>(&statement)) {
        for (const auto& child : block->statements) {
            if (statementHasLambda(*child)) return true;
        }
        return false;
    }
    if (auto* s = dynamic_cast<const IfStatement*>(&statement)) {
        return expressionHasLambda(*s->condition) || statementHasLambda(*s->thenBranch) ||
               (s->elseBranch && statementHasLambda(*s->elseBranch));
    }
    if (auto* s = dynamic_cast<const WhileStatement*>(&statement)) {
        return expressionHasLambda(*s->condition) || statementHasLambda(*s->body);
    }
    if (auto* s = dynamic_cast<const DoWhileStatement*>(&statement)) {
        return expressionHasLambda(*s->condition) || statementHasLambda(*s->body);
    }
    if (auto* s = dynamic_cast<const ForStatement*>(&statement)) {
        if (s->initializer && statementHasLambda(*s->initializer)) return true;
        if (s->condition && expressionHasLambda(*s->condition)) return true;
        if (s->iterator && expressionHasLambda(*s->iterator)) return true;
        return statementHasLambda(*s->body);
    }
    if (auto* s = dynamic_cast<const ForeachStatement*>(&statement)) {
        return expressionHasLambda(*s->collection) || statementHasLambda(*s->body);
    }
    if (auto* s = dynamic_cast<const SwitchStatement*>(&statement)) {
        if (expressionHasLambda(*s->value)) return true;
        for (const auto& clause : s->cases) {
            if (clause.label && expressionHasLambda(*clause.label)) return true;
            for (const auto& child : clause.body) {
                if (statementHasLambda(*child)) return true;
            }
        }
        return false;
    }
    if (auto* s = dynamic_cast<const TryStatement*>(&statement)) {
        if (statementHasLambda(*s->body)) return true;
        for (const auto& clause : s->catches) {
            if (clause->body && statementHasLambda(*clause->body)) return true;
        }
        return false;
    }
    if (auto* s = dynamic_cast<const ReturnStatement*>(&statement)) {
        return s->value && expressionHasLambda(*s->value);
    }
    if (auto* s = dynamic_cast<const ThrowStatement*>(&statement)) {
        return s->value && expressionHasLambda(*s->value);
    }
    if (auto* s = dynamic_cast<const ExpressionStatement*>(&statement)) {
        return expressionHasLambda(*s->expression);
    }
    if (auto* s = dynamic_cast<const LocalVariableDeclaration*>(&statement)) {
        for (const auto& initializer : s->initializers) {
            if (initializer.value && expressionHasLambda(*initializer.value)) return true;
        }
    }
    return false;
}

bool expressionHasLambda(const Expression& expression) {
    if (dynamic_cast<const LambdaExpression*>(&expression) != nullptr) return true;
    // ref/out arguments need their locals hoisted into cells as well.
    if (dynamic_cast<const RefArgumentExpression*>(&expression) != nullptr) return true;
    if (dynamic_cast<const OutDeclarationExpression*>(&expression) != nullptr) return true;
    if (auto* e = dynamic_cast<const BinaryExpression*>(&expression)) {
        return expressionHasLambda(*e->left) || expressionHasLambda(*e->right);
    }
    if (auto* e = dynamic_cast<const UnaryExpression*>(&expression)) return expressionHasLambda(*e->operand);
    if (auto* e = dynamic_cast<const AssignmentExpression*>(&expression)) {
        return expressionHasLambda(*e->target) || expressionHasLambda(*e->value);
    }
    if (auto* e = dynamic_cast<const MemberExpression*>(&expression)) return expressionHasLambda(*e->target);
    if (auto* e = dynamic_cast<const IndexExpression*>(&expression)) {
        return expressionHasLambda(*e->target) || expressionHasLambda(*e->index);
    }
    if (auto* e = dynamic_cast<const CallExpression*>(&expression)) {
        if (expressionHasLambda(*e->callee)) return true;
        for (const auto& argument : e->arguments) {
            if (expressionHasLambda(*argument)) return true;
        }
        return false;
    }
    if (auto* e = dynamic_cast<const NewObjectExpression*>(&expression)) {
        for (const auto& argument : e->arguments) {
            if (expressionHasLambda(*argument)) return true;
        }
        return false;
    }
    if (auto* e = dynamic_cast<const NewArrayExpression*>(&expression)) {
        if (e->size && expressionHasLambda(*e->size)) return true;
        for (const auto& item : e->initializer) {
            if (expressionHasLambda(*item)) return true;
        }
        return false;
    }
    if (auto* e = dynamic_cast<const CastExpression*>(&expression)) return expressionHasLambda(*e->operand);
    if (auto* e = dynamic_cast<const AsExpression*>(&expression)) return expressionHasLambda(*e->operand);
    if (auto* e = dynamic_cast<const TypeTestExpression*>(&expression)) return expressionHasLambda(*e->operand);
    if (auto* e = dynamic_cast<const ConditionalExpression*>(&expression)) {
        return expressionHasLambda(*e->condition) || expressionHasLambda(*e->whenTrue) ||
               expressionHasLambda(*e->whenFalse);
    }
    return false;
}

bool bodyHasLambda(const BlockStatement* body) {
    if (body == nullptr) return false;
    for (const auto& statement : body->statements) {
        if (statementHasLambda(*statement)) return true;
    }
    return false;
}

}  // namespace

FunctionCompiler::FunctionCompiler(ModuleCompiler& moduleCompiler, ScriptFunction* function,
                                   const FunctionDeclaration* declaration,
                                   const std::vector<CaptureBinding>* captures)
    : modules_(moduleCompiler), function_(function), declaration_(declaration) {
    namespaceScope_ = function_->owner != nullptr ? function_->owner->namespacePath : function_->namespacePath;
    useCells_ = declaration_ != nullptr && bodyHasLambda(declaration_->body.get());
    if (captures != nullptr) useCells_ = true;
    setupParameters();
    if (captures != nullptr) {
        captureBindings_ = *captures;
        for (const auto& binding : captureBindings_) {
            const int slot = allocateSlot(binding.second);
            declareLocal(binding.first, binding.second, slot, false, true);
        }
    }
}

void FunctionCompiler::setNamespaceScope(std::string scope) { namespaceScope_ = std::move(scope); }

const std::string& FunctionCompiler::namespaceScope() const {
    if (function_->owner != nullptr) return function_->owner->namespacePath;
    return namespaceScope_;
}

void FunctionCompiler::setupParameters() {
    pushScope();
    int slot = 0;
    if (function_->owner != nullptr && !function_->isStatic) slot = 1;
    for (const auto& parameter : function_->parameters) {
        const bool isRef = parameter.modifier != ParamModifier::None;
        declareLocal(parameter.name, parameter.type, slot++, false, useCells_ && !isRef);
        auto& local = scopes_.back()[parameter.name];
        local.isRef = isRef;
        local.isReadOnlyRef = parameter.modifier == ParamModifier::In;
        if (isRef) local.isCell = false;
    }
    nextSlot_ = slot;
}

void FunctionCompiler::pushScope() { scopes_.emplace_back(); }
void FunctionCompiler::popScope() {
    if (scopes_.size() > 1) scopes_.pop_back();
}

void FunctionCompiler::declareLocal(const std::string& name, const ScriptType& type, int slot,
                                    bool isConst, bool isCell) {
    scopes_.back()[name] = LocalVariable{name, type, slot, isConst, isCell};
}

void FunctionCompiler::emitReturnEpilogues() {
    if (returnSites_.empty()) return;
    Label over = builder_.newLabel();
    emitJump(OpCode::Jmp, over, 0);
    for (const auto& site : returnSites_) {
        builder_.markLabel(site.label);
        if (site.slot >= 0) {
            emit(OpCode::LoadLocal, site.slot, +1);
            emit(OpCode::Ret, 0, -1);
        } else {
            emit(OpCode::Ret);
        }
    }
    builder_.markLabel(over);
}

void FunctionCompiler::emitCellPrologue() {
    int slot = (function_->owner != nullptr && !function_->isStatic) ? 1 : 0;
    for (std::size_t i = 0; i < function_->parameters.size(); ++i) {
        if (function_->parameters[i].isRef()) continue;   // ref slots hold ScriptRef already
        emit(OpCode::LoadLocal, slot + static_cast<int>(i), +1);
        emit(OpCode::NewCell, 0, 0);
        emit(OpCode::StoreLocal, slot + static_cast<int>(i), -1);
    }
}

const FunctionCompiler::LocalVariable* FunctionCompiler::findLocal(const std::string& name) const {
    for (auto it = scopes_.rbegin(); it != scopes_.rend(); ++it) {
        auto found = it->find(name);
        if (found != it->end()) return &found->second;
    }
    return nullptr;
}

void FunctionCompiler::compile() {
    if (useCells_) emitCellPrologue();
    if (function_->isConstructor) emitConstructorPrologue();
    if (declaration_ != nullptr && declaration_->body != nullptr) {
        for (const auto& statement : declaration_->body->statements) compileStatement(*statement);
        if (!function_->returnType.isVoid() && !definitelyTerminates(*declaration_->body)) {
            modules_.error("E0301", "not all code paths return a value in '" + function_->name + "'",
                           *declaration_);
        }
    }
    emitReturnEpilogues();
    finish();
}

void FunctionCompiler::compileSynthetic(const std::function<void(FunctionCompiler&)>& body) {
    body(*this);
    emitReturnEpilogues();
    finish();
}

void FunctionCompiler::finish() {
    if (function_->isAbstract) return;
    if (function_->returnType.isVoid()) {
        emit(OpCode::Ret);
    } else {
        emitDefault(function_->returnType);
        emit(OpCode::Ret, 0, -1);
    }
    function_->localSlotCount = nextSlot_;
    builder_.complete(*function_);
}

void FunctionCompiler::emitConstructorPrologue() {
    auto* cls = dynamic_cast<ScriptClassType*>(function_->owner);
    if (cls == nullptr) return;

    ScriptFunction* baseConstructor = cls->base != nullptr ? cls->base->constructor : nullptr;
    if (baseConstructor != nullptr) {
        if (!baseConstructor->parameters.empty()) {
            modules_.error("E0302",
                           "base class constructor needs arguments; explicit base(...) is not supported",
                           declaration_ != nullptr ? static_cast<const Node&>(*declaration_) : Node{});
        } else {
            emit(OpCode::LoadThis, 0, +1);
            emitScriptCall(baseConstructor);
        }
    }

    for (const auto& initializer : modules_.fieldInitializers(cls)) {
        setLine(initializer.line);
        emit(OpCode::LoadThis, 0, +1);
        compileExpression(*initializer.initializer, &initializer.field->type);
        emitStoreField(initializer.field);
    }
}

void FunctionCompiler::emit(OpCode op, int arg, int stackDelta) {
    builder_.emit(op, arg, stackDelta);
}

void FunctionCompiler::emitDefault(const ScriptType& type) {
    if (type.isVoid()) return;
    if (!type.isArray() && type.object == nullptr && type.primitive == Primitive::Int32) {
        emit(OpCode::PushI32Imm, 0, +1);
        return;
    }
    if (!type.isArray() && type.object == nullptr && type.primitive == Primitive::Bool) {
        emit(OpCode::PushFalse, 0, +1);
        return;
    }
    if (type.isStruct()) {
        // A struct default is a fresh, zero-initialized box (never a shared constant).
        emit(OpCode::NewObj, builder_.addTypeRef(type), +1);
        return;
    }
    if (type.isReference() || type.isVar()) {
        emit(OpCode::PushNull, 0, +1);
        return;
    }
    pushConstant(defaultValue(type, modules_.engine().garbageCollector()));
}

void FunctionCompiler::compileStatement(const Statement& statement) {
    setLine(statement.line);

    if (auto* block = dynamic_cast<const BlockStatement*>(&statement)) {
        compileBlock(*block);
    } else if (auto* declaration = dynamic_cast<const LocalVariableDeclaration*>(&statement)) {
        compileLocalDeclaration(*declaration);
    } else if (auto* expression = dynamic_cast<const ExpressionStatement*>(&statement)) {
        auto type = compileExpression(*expression->expression);
        if (!type.isVoid()) emit(OpCode::Pop, 0, -1);
    } else if (auto* ifStatement = dynamic_cast<const IfStatement*>(&statement)) {
        compileIf(*ifStatement);
    } else if (auto* whileStatement = dynamic_cast<const WhileStatement*>(&statement)) {
        compileWhile(*whileStatement);
    } else if (auto* doStatement = dynamic_cast<const DoWhileStatement*>(&statement)) {
        compileDoWhile(*doStatement);
    } else if (auto* forStatement = dynamic_cast<const ForStatement*>(&statement)) {
        compileFor(*forStatement);
    } else if (auto* foreachStatement = dynamic_cast<const ForeachStatement*>(&statement)) {
        compileForeach(*foreachStatement);
    } else if (auto* returnStatement = dynamic_cast<const ReturnStatement*>(&statement)) {
        compileReturn(*returnStatement);
    } else if (dynamic_cast<const BreakStatement*>(&statement) != nullptr) {
        if (loops_.empty()) {
            modules_.error("E0303", "'break' outside of a loop or switch", statement);
        } else {
            if (finallyDepth_ > 0 && loops_.back().finallyDepth < finallyDepth_) {
                modules_.error("E0426", "control cannot leave the body of a 'finally' block", statement);
            }
            // Leave runs any finally blocks of the regions being exited.
            emitJump(OpCode::Leave, loops_.back().breakLabel, 0);
        }
    } else if (dynamic_cast<const ContinueStatement*>(&statement) != nullptr) {
        bool found = false;
        for (auto it = loops_.rbegin(); it != loops_.rend(); ++it) {
            if (it->hasContinue) {
                if (finallyDepth_ > 0 && it->finallyDepth < finallyDepth_) {
                    modules_.error("E0426", "control cannot leave the body of a 'finally' block", statement);
                }
                emitJump(OpCode::Leave, it->continueLabel, 0);
                found = true;
                break;
            }
        }
        if (!found) modules_.error("E0304", "'continue' outside of a loop", statement);
    } else if (auto* switchStatement = dynamic_cast<const SwitchStatement*>(&statement)) {
        compileSwitch(*switchStatement);
    } else if (auto* tryStatement = dynamic_cast<const TryStatement*>(&statement)) {
        compileTry(*tryStatement);
    } else if (auto* throwStatement = dynamic_cast<const ThrowStatement*>(&statement)) {
        compileThrow(*throwStatement);
    } else if (dynamic_cast<const EmptyStatement*>(&statement) != nullptr) {
        // nothing
    } else {
        modules_.error("E0305", "unsupported statement", statement);
    }
}

void FunctionCompiler::compileBlock(const BlockStatement& block) {
    pushScope();
    for (const auto& statement : block.statements) compileStatement(*statement);
    popScope();
}

void FunctionCompiler::compileLocalDeclaration(const LocalVariableDeclaration& declaration) {
    for (const auto& initializer : declaration.initializers) {
        ScriptType type;
        if (!declaration.type.has_value()) {
            if (!initializer.value) {
                modules_.error("E0306",
                               "'" + initializer.name + "' needs an initializer when declared with 'var'",
                               initializer);
                type = primI32();
                emitDefault(type);
            } else {
                type = compileExpression(*initializer.value);
            }
        } else {
            if (!modules_.resolveTypeRef(*declaration.type, initializer, namespaceScope(), type)) type = primI32();
            if (type.isVoid()) {
                modules_.error("E0387", "variable '" + initializer.name + "' cannot have type 'void'",
                               initializer);
                type = primI32();
            }
            if (initializer.value) {
                compileExpression(*initializer.value, &type);
            } else {
                emitDefault(type);
            }
        }

        // C# value semantics: a local receives its own copy of the struct.
        if (initializer.value && type.isStruct()) emit(OpCode::CloneStruct, 0, 0);
        // Functions containing lambdas keep every local in a heap cell so
        // closures can capture the variable by reference.
        const bool isCell = useCells_;
        if (isCell) emit(OpCode::NewCell, 0, 0);
        int slot = allocateSlot(type);
        emit(OpCode::StoreLocal, slot, -1);
        if (scopes_.back().count(initializer.name) > 0) {
            modules_.error("E0307",
                           "a variable named '" + initializer.name + "' is already declared in this scope",
                           initializer);
        }
        declareLocal(initializer.name, type, slot, declaration.isConst, isCell);
    }
}

void FunctionCompiler::compileIf(const IfStatement& statement) {
    compileExpression(*statement.condition, &primBool());
    Label elseLabel = builder_.newLabel();
    Label endLabel = builder_.newLabel();
    emitJump(OpCode::Jz, elseLabel, -1);
    compileStatement(*statement.thenBranch);
    if (statement.elseBranch) {
        emitJump(OpCode::Jmp, endLabel, 0);
        builder_.markLabel(elseLabel);
        compileStatement(*statement.elseBranch);
        builder_.markLabel(endLabel);
    } else {
        builder_.markLabel(elseLabel);
    }
}

void FunctionCompiler::compileWhile(const WhileStatement& statement) {
    Label conditionLabel = builder_.newLabel();
    Label endLabel = builder_.newLabel();
    builder_.markLabel(conditionLabel);
    compileExpression(*statement.condition, &primBool());
    emitJump(OpCode::Jz, endLabel, -1);
    loops_.push_back(LoopContext{endLabel, conditionLabel, true, finallyDepth_});
    compileStatement(*statement.body);
    loops_.pop_back();
    emitJump(OpCode::Jmp, conditionLabel, 0);
    builder_.markLabel(endLabel);
}

void FunctionCompiler::compileDoWhile(const DoWhileStatement& statement) {
    Label bodyLabel = builder_.newLabel();
    Label conditionLabel = builder_.newLabel();
    Label endLabel = builder_.newLabel();
    builder_.markLabel(bodyLabel);
    loops_.push_back(LoopContext{endLabel, conditionLabel, true, finallyDepth_});
    compileStatement(*statement.body);
    loops_.pop_back();
    builder_.markLabel(conditionLabel);
    compileExpression(*statement.condition, &primBool());
    emitJump(OpCode::Jnz, bodyLabel, -1);
    builder_.markLabel(endLabel);
}

void FunctionCompiler::compileFor(const ForStatement& statement) {
    pushScope();
    if (statement.initializer) compileStatement(*statement.initializer);

    Label conditionLabel = builder_.newLabel();
    Label continueLabel = builder_.newLabel();
    Label endLabel = builder_.newLabel();
    builder_.markLabel(conditionLabel);

    if (statement.condition) {
        compileExpression(*statement.condition, &primBool());
        emitJump(OpCode::Jz, endLabel, -1);
    }

    loops_.push_back(LoopContext{endLabel, continueLabel, true, finallyDepth_});
    compileStatement(*statement.body);
    loops_.pop_back();

    builder_.markLabel(continueLabel);
    if (statement.iterator) {
        auto type = compileExpression(*statement.iterator);
        if (!type.isVoid()) emit(OpCode::Pop, 0, -1);
    }
    emitJump(OpCode::Jmp, conditionLabel, 0);
    builder_.markLabel(endLabel);
    popScope();
}

void FunctionCompiler::compileForeach(const ForeachStatement& statement) {
    pushScope();
    auto collectionType = compileExpression(*statement.collection);
    ScriptType elementType;
    if (collectionType.isArray()) {
        elementType = *collectionType.element;
    } else if (collectionType.isString()) {
        elementType = primChar();
    } else {
        modules_.error("E0308",
                       "'foreach' requires an array or string, got '" + collectionType.name + "'",
                       statement);
        elementType = primI32();
    }

    if (statement.elementType.has_value()) {
        ScriptType declared;
        if (modules_.resolveTypeRef(*statement.elementType, statement, namespaceScope(), declared))
            elementType = declared;
    }

    int collectionSlot = allocateSlot(collectionType);
    emit(OpCode::StoreLocal, collectionSlot, -1);
    int indexSlot = allocateSlot(primI32());
    emit(OpCode::PushI32Imm, 0, +1);
    emit(OpCode::StoreLocal, indexSlot, -1);
    int elementSlot = allocateSlot(elementType);
    declareLocal(statement.variableName, elementType, elementSlot, false, useCells_);

    Label startLabel = builder_.newLabel();
    Label continueLabel = builder_.newLabel();
    Label endLabel = builder_.newLabel();
    builder_.markLabel(startLabel);

    emit(OpCode::LoadLocal, indexSlot, +1);
    emit(OpCode::LoadLocal, collectionSlot, +1);
    emit(OpCode::Length, 0, 0);
    emit(OpCode::CmpLT, 0, -1);
    emitJump(OpCode::Jz, endLabel, -1);

    emit(OpCode::LoadLocal, collectionSlot, +1);
    emit(OpCode::LoadLocal, indexSlot, +1);
    emit(OpCode::LoadElem, 0, -1);
    // foreach over an array of structs yields a copy, like C#.
    if (elementType.isStruct()) emit(OpCode::CloneStruct, 0, 0);
    // C# creates a fresh loop variable per iteration, so closures don't share it.
    if (useCells_) emit(OpCode::NewCell, 0, 0);
    emit(OpCode::StoreLocal, elementSlot, -1);

    loops_.push_back(LoopContext{endLabel, continueLabel, true, finallyDepth_});
    compileStatement(*statement.body);
    loops_.pop_back();

    builder_.markLabel(continueLabel);
    emit(OpCode::LoadLocal, indexSlot, +1);
    pushConstant(Value::i32(1));
    emit(OpCode::AddI32, 0, -1);
    emit(OpCode::StoreLocal, indexSlot, -1);
    emitJump(OpCode::Jmp, startLabel, 0);
    builder_.markLabel(endLabel);
    popScope();
}

void FunctionCompiler::compileReturn(const ReturnStatement& statement) {
    if (finallyDepth_ > 0) {
        modules_.error("E0426", "control cannot leave the body of a 'finally' block", statement);
    }
    // Leaving a protected region must run the enclosing finally blocks first.
    // The epilogue lives at the end of the function (outside every region).
    auto emitLeaveToEnd = [this]() {
        if (protectedDepth_ <= 0) return;
        ReturnSite site;
        site.label = builder_.newLabel();
        emitJump(OpCode::Leave, site.label, 0);
        returnSites_.push_back(site);
    };

    if (!statement.value) {
        if (!function_->returnType.isVoid()) {
            modules_.error("E0309",
                           "function '" + function_->name + "' must return a value of type '" +
                               function_->returnType.name + "'",
                           statement);
        }
        if (protectedDepth_ > 0) {
            emitLeaveToEnd();
            return;
        }
        emit(OpCode::Ret);
        return;
    }

    if (function_->returnType.isVoid()) {
        modules_.error("E0310", "void function '" + function_->name + "' cannot return a value", statement);
        auto type = compileExpression(*statement.value);
        if (!type.isVoid()) emit(OpCode::Pop, 0, -1);
        if (protectedDepth_ > 0) {
            emitLeaveToEnd();
            return;
        }
        emit(OpCode::Ret);
        return;
    }

    compileExpression(*statement.value, &function_->returnType);
    // Returning a struct hands the caller an independent copy (`return this;` included).
    if (function_->returnType.isStruct()) emit(OpCode::CloneStruct, 0, 0);
    if (protectedDepth_ > 0) {
        // Evaluate once, run finally blocks, then return the saved value.
        const int slot = allocateSlot(function_->returnType);
        emit(OpCode::StoreLocal, slot, -1);
        ReturnSite site;
        site.label = builder_.newLabel();
        site.slot = slot;
        emitJump(OpCode::Leave, site.label, 0);
        returnSites_.push_back(site);
        return;
    }
    emit(OpCode::Ret, 0, -1);
}

bool FunctionCompiler::isExceptionType(const ScriptType& type) const {
    auto* native = dynamic_cast<NativeObjectType*>(type.object);
    if (native == nullptr) return false;
    auto* base = modules_.engine().findNativeType("Exception");
    return base != nullptr && base->isAssignableFrom(native);
}

void FunctionCompiler::compileThrow(const ThrowStatement& statement) {
    if (!statement.value) {
        if (catchExceptionSlots_.empty()) {
            modules_.error("E0406", "'throw;' is only valid inside a catch block", statement);
            return;
        }
        emit(OpCode::LoadLocal, catchExceptionSlots_.back(), +1);
        emit(OpCode::Throw, 0, -1);
        return;
    }

    ScriptType exceptionType("Exception", Primitive::None, modules_.engine().findNativeType("Exception"));
    auto valueType = compileExpression(*statement.value, &exceptionType);
    if (!valueType.isNullType() && !isExceptionType(valueType)) {
        modules_.error("E0405", "'throw' requires an Exception value, got '" + valueType.name + "'", statement);
    }
    emit(OpCode::Throw, 0, -1);
}

void FunctionCompiler::compileTry(const TryStatement& statement) {
    const int stackDepth = builder_.stackDepth();
    const int tryStart = builder_.position();
    ++protectedDepth_;
    compileBlock(*statement.body);
    --protectedDepth_;
    const int tryEnd = builder_.position();

    const bool hasFinally = statement.finallyBody != nullptr;
    Label endLabel = builder_.newLabel();
    // Normal completion of the try body: Leave runs the finally blocks crossed.
    emitJump(hasFinally ? OpCode::Leave : OpCode::Jmp, endLabel, 0);

    ScriptType exceptionType("Exception", Primitive::None, modules_.engine().findNativeType("Exception"));

    for (const auto& clausePtr : statement.catches) {
        const auto& clause = *clausePtr;
        bool hasType = clause.type.has_value();
        ScriptType catchType = exceptionType;
        if (hasType) {
            if (!modules_.resolveTypeRef(*clause.type, clause, namespaceScope(), catchType)) {
                catchType = exceptionType;
            } else if (!isExceptionType(catchType)) {
                modules_.error("E0407", "'catch' requires an Exception type, got '" + catchType.name + "'",
                               clause);
                catchType = exceptionType;
            }
        }

        pushScope();
        const int exceptionSlot = allocateSlot(exceptionType);
        int variableSlot = -1;
        if (!clause.variable.empty()) {
            variableSlot = allocateSlot(catchType);
            declareLocal(clause.variable, catchType, variableSlot, false);
        }

        ExceptionHandler handler;
        handler.kind = HandlerKind::Catch;
        handler.tryStart = tryStart;
        handler.tryEnd = tryEnd;
        handler.handlerPc = builder_.position();
        handler.stackDepth = stackDepth;
        handler.exceptionSlot = exceptionSlot;
        handler.hasCatchType = hasType;
        handler.catchType = catchType;
        handler.line = clause.line;
        function_->handlers.push_back(handler);

        if (variableSlot >= 0) {
            emit(OpCode::LoadLocal, exceptionSlot, +1);
            emit(OpCode::StoreLocal, variableSlot, -1);
        }

        catchExceptionSlots_.push_back(exceptionSlot);
        ++protectedDepth_;
        compileBlock(*clause.body);
        --protectedDepth_;
        catchExceptionSlots_.pop_back();
        popScope();
        emitJump(hasFinally ? OpCode::Leave : OpCode::Jmp, endLabel, 0);
    }

    if (hasFinally) {
        // The finally region also covers the catch handlers, so their exceptions
        // still run the finally before propagating.
        ExceptionHandler handler;
        handler.kind = HandlerKind::Finally;
        handler.tryStart = tryStart;
        handler.tryEnd = builder_.position();
        handler.handlerPc = builder_.position();
        handler.stackDepth = stackDepth;
        handler.line = statement.line;
        const int index = static_cast<int>(function_->handlers.size());
        function_->handlers.push_back(handler);

        ++finallyDepth_;
        compileBlock(*statement.finallyBody);
        --finallyDepth_;
        function_->handlers[static_cast<std::size_t>(index)].handlerEnd = builder_.position();
        emit(OpCode::EndFinally, 0, 0);
    }

    builder_.markLabel(endLabel);
}

void FunctionCompiler::compileSwitch(const SwitchStatement& statement) {
    auto valueType = compileExpression(*statement.value);
    if (valueType.isStruct()) {
        modules_.error("E0404", "'switch' cannot be used with struct type '" + valueType.name + "'", statement);
    }
    int slot = allocateSlot(valueType);
    emit(OpCode::StoreLocal, slot, -1);

    Label endLabel = builder_.newLabel();
    std::vector<std::pair<const SwitchCase*, Label>> labels;
    std::optional<Label> defaultLabel;

    for (const auto& switchCase : statement.cases) {
        Label label = builder_.newLabel();
        labels.emplace_back(&switchCase, label);
        if (!switchCase.label) {
            defaultLabel = label;
            continue;
        }
        emit(OpCode::LoadLocal, slot, +1);
        compileExpression(*switchCase.label, &valueType);
        emit(OpCode::CmpEQ, 0, -1);
        emitJump(OpCode::Jnz, label, -1);
    }

    if (defaultLabel.has_value()) {
        emitJump(OpCode::Jmp, *defaultLabel, 0);
    } else {
        emitJump(OpCode::Jmp, endLabel, 0);
    }

    for (const auto& entry : labels) {
        builder_.markLabel(entry.second);
        loops_.push_back(LoopContext{endLabel, Label{}, false, finallyDepth_});
        for (const auto& child : entry.first->body) compileStatement(*child);
        loops_.pop_back();
        emitJump(OpCode::Jmp, endLabel, 0);
    }
    builder_.markLabel(endLabel);
}

// ---------------------------------------------------------------- expressions

ScriptType FunctionCompiler::compileExpression(const Expression& expression, const ScriptType* target) {
    if (target != nullptr && target->isDelegate()) {
        if (auto* lambda = dynamic_cast<const LambdaExpression*>(&expression)) {
            return compileLambdaExpression(*lambda, *target);
        }
        if (auto* member = dynamic_cast<const MemberExpression*>(&expression)) {
            if (tryCompileMethodGroup(*member, *target)) return *target;
        } else if (auto* identifier = dynamic_cast<const IdentifierExpression*>(&expression)) {
            if (tryCompileFunctionGroup(*identifier, *target)) return *target;
        }
    }
    if (auto* refArg = dynamic_cast<const RefArgumentExpression*>(&expression)) {
        modules_.error("E0425", "'ref'/'out'/'in' is only valid in an argument list", expression);
        return compileExpression(*refArg->operand);
    }
    if (dynamic_cast<const OutDeclarationExpression*>(&expression) != nullptr) {
        modules_.error("E0425", "'out var' is only valid in an argument list", expression);
        emitDefault(primI32());
        return primI32();
    }
    auto type = compileExpressionCore(expression);
    if (target != nullptr) {
        emitConversion(type, *target, expression);
        type = *target;
    }
    return type;
}

ScriptType FunctionCompiler::compileExpressionCore(const Expression& expression) {
    setLine(expression.line);

    if (auto* literal = dynamic_cast<const LiteralExpression*>(&expression)) return compileLiteral(*literal);
    if (auto* identifier = dynamic_cast<const IdentifierExpression*>(&expression))
        return compileIdentifier(*identifier);
    if (auto* thisExpr = dynamic_cast<const ThisExpression*>(&expression)) return compileThis(*thisExpr);
    if (auto* member = dynamic_cast<const MemberExpression*>(&expression)) return compileMember(*member);
    if (auto* index = dynamic_cast<const IndexExpression*>(&expression)) return compileIndex(*index);
    if (auto* call = dynamic_cast<const CallExpression*>(&expression)) return compileCall(*call);
    if (auto* newObject = dynamic_cast<const NewObjectExpression*>(&expression))
        return compileNewObject(*newObject);
    if (auto* newArray = dynamic_cast<const NewArrayExpression*>(&expression))
        return compileNewArray(*newArray);
    if (auto* cast = dynamic_cast<const CastExpression*>(&expression)) return compileCast(*cast);
    if (auto* asExpr = dynamic_cast<const AsExpression*>(&expression)) return compileAs(*asExpr);
    if (auto* typeTest = dynamic_cast<const TypeTestExpression*>(&expression))
        return compileTypeTest(*typeTest);
    if (auto* unary = dynamic_cast<const UnaryExpression*>(&expression)) return compileUnary(*unary);
    if (auto* binary = dynamic_cast<const BinaryExpression*>(&expression)) return compileBinary(*binary);
    if (auto* assignment = dynamic_cast<const AssignmentExpression*>(&expression))
        return compileAssignment(*assignment);
    if (auto* conditional = dynamic_cast<const ConditionalExpression*>(&expression))
        return compileConditional(*conditional);
    if (dynamic_cast<const LambdaExpression*>(&expression) != nullptr) {
        modules_.error("E0411", "a lambda expression requires a delegate target type", expression);
        emitDefault(primI32());
        return primI32();
    }

    modules_.error("E0311", "unsupported expression", expression);
    emitDefault(primI32());
    return primI32();
}

ScriptType FunctionCompiler::compileLiteral(const LiteralExpression& literal) {
    switch (literal.kind) {
        case LiteralKind::Bool:
            emit(literal.boolValue ? OpCode::PushTrue : OpCode::PushFalse, 0, +1);
            return primBool();
        case LiteralKind::String:
            pushConstant(Value::string(literal.stringValue));
            return primString();
        case LiteralKind::Char:
            pushConstant(Value::character(literal.charValue));
            return primChar();
        case LiteralKind::Null:
            emit(OpCode::PushNull, 0, +1);
            return primNull();
        case LiteralKind::Float:
            if (literal.suffix == NumericSuffix::Float) {
                pushConstant(Value::f32(static_cast<float>(literal.floatValue)));
                return primF32();
            }
            pushConstant(Value::f64(literal.floatValue));
            return primF64();
        case LiteralKind::Int: {
            std::int64_t value = literal.intValue;
            switch (literal.suffix) {
                case NumericSuffix::Unsigned:
                    pushConstant(Value::u32(static_cast<std::uint32_t>(value)));
                    return primU32();
                case NumericSuffix::Long:
                    pushConstant(Value::i64(value));
                    return primI64();
                case NumericSuffix::UnsignedLong:
                    pushConstant(Value::u64(static_cast<std::uint64_t>(value)));
                    return primU64();
                default:
                    if (value >= INT32_MIN && value <= INT32_MAX) {
                        pushConstant(Value::i32(static_cast<std::int32_t>(value)));
                        return primI32();
                    }
                    if (value >= 0 && value <= UINT32_MAX) {
                        pushConstant(Value::u32(static_cast<std::uint32_t>(value)));
                        return primU32();
                    }
                    pushConstant(Value::i64(value));
                    return primI64();
            }
        }
    }
    return primI32();
}

ScriptType FunctionCompiler::compileIdentifier(const IdentifierExpression& identifier) {
    if (auto* local = findLocal(identifier.name)) {
        emit(OpCode::LoadLocal, local->slot, +1);
        if (local->isRef) {
            emit(OpCode::LoadRef, 0, 0);
        } else if (local->isCell) {
            emit(OpCode::LoadCell, 0, 0);
        }
        return local->type;
    }

    auto& module = modules_.module();
    int globalIndex = 0;
    ScriptType globalType;
    if (lookupGlobal(identifier.name, globalIndex, globalType)) {
        emit(OpCode::LoadGlobal, globalIndex, +1);
        return globalType;
    }

    auto* owner = dynamic_cast<ScriptClassType*>(function_->owner);
    if (owner != nullptr) {
        std::string staticName = owner->name + "." + identifier.name;
        auto staticIt = module.globalsByName.find(staticName);
        if (staticIt != module.globalsByName.end()) {
            emit(OpCode::LoadGlobal, staticIt->second, +1);
            return module.globals[staticIt->second].type;
        }
    }

    if (auto* field = findInstanceField(identifier.name)) {
        emit(OpCode::LoadThis, 0, +1);
        emitLoadField(field);
        return field->type;
    }

    ScriptType typeReference;
    if (lookupType(identifier.name, typeReference)) {
        modules_.error("E0312", "'" + identifier.name + "' is a type, not a value", identifier);
        emitDefault(primI32());
        return primI32();
    }

    modules_.error("E0313", "unknown identifier '" + identifier.name + "'", identifier);
    emitDefault(primI32());
    return primI32();
}

ScriptType FunctionCompiler::compileThis(const ThisExpression& expression) {
    if (function_->owner == nullptr || function_->isStatic) {
        modules_.error("E0314", "'this' is only available in instance methods", expression);
        emitDefault(primI32());
        return primI32();
    }
    emit(OpCode::LoadThis, 0, +1);
    return ScriptType(function_->owner->name, Primitive::None, function_->owner);
}

bool FunctionCompiler::tryCompileFunctionGroup(const IdentifierExpression& identifier,
                                                const ScriptType& expected) {
    auto* delegateType = dynamic_cast<ScriptDelegateType*>(expected.object);
    if (delegateType == nullptr) return false;
    int variableIndex = 0;
    if (findLocal(identifier.name) != nullptr || lookupGlobal(identifier.name, variableIndex)) return false;

    const std::size_t arity = delegateType->parameterTypes.size();
    auto matches = [&](ScriptFunction* function) {
        if (function->parameters.size() != arity) return false;
        for (std::size_t i = 0; i < arity; ++i) {
            if (!function->parameters[i].type.identityOrReferenceAssignableFrom(delegateType->parameterTypes[i]) ||
                function->parameters[i].isRef()) {
                return false;
            }
        }
        return delegateType->returnType.identityOrReferenceAssignableFrom(function->returnType);
    };

    auto candidates = moduleFunctions(identifier.name, arity);
    if (candidates.empty()) {
        auto* owner = dynamic_cast<ScriptClassType*>(function_->owner);
        if (owner != nullptr) candidates = owner->findMethods(identifier.name, arity, true);
    }
    if (candidates.empty()) {
        // The name may still be a function with another signature; report that
        // instead of letting it fall through to "unknown identifier".
        bool nameExists = false;
        for (const auto& candidate : modules_.candidateNames(identifier.name, namespaceScope())) {
            auto it = modules_.module().functionsByName.find(candidate);
            if (it != modules_.module().functionsByName.end() && !it->second.empty()) {
                nameExists = true;
                break;
            }
        }
        if (!nameExists) return false;
    }
    sortDelegateOverloads(candidates, *delegateType);
    for (auto* function : candidates) {
        if (!matches(function)) continue;
        emit(OpCode::PushNull, 0, +1);
        emit(OpCode::NewDelegate, builder_.addDelegateInfo(DelegateInfo{function, delegateType, 0}), 0);
        return true;
    }
    modules_.error("E0409",
                   "no overload of '" + identifier.name + "' matches delegate '" + expected.name + "'",
                   identifier);
    emitDefault(expected);
    return true;
}

bool FunctionCompiler::tryCompileMethodGroup(const MemberExpression& member, const ScriptType& expected) {
    auto* delegateType = dynamic_cast<ScriptDelegateType*>(expected.object);
    if (delegateType == nullptr) return false;
    const std::size_t arity = delegateType->parameterTypes.size();
    auto matches = [&](const ScriptFunction* function) {
        if (function->parameters.size() != arity) return false;
        for (std::size_t i = 0; i < arity; ++i) {
            if (!function->parameters[i].type.identityOrReferenceAssignableFrom(delegateType->parameterTypes[i]) ||
                function->parameters[i].isRef()) {
                return false;
            }
        }
        return delegateType->returnType.identityOrReferenceAssignableFrom(function->returnType);
    };

    std::string chain;
    if (flattenNameChain(member, chain) && !firstSegmentIsValue(chain)) {
        auto dot = chain.rfind('.');
        if (dot != std::string::npos) {
            ScriptType prefixType;
            if (lookupType(chain.substr(0, dot), prefixType)) {
                auto* cls = dynamic_cast<ScriptClassType*>(prefixType.object);
                if (cls == nullptr) return false;   // native method groups are not supported yet
                auto candidates = cls->findMethods(member.memberName, arity, true);
                sortDelegateOverloads(candidates, *delegateType);
                for (auto* function : candidates) {
                    if (!matches(function)) continue;
                    emit(OpCode::PushNull, 0, +1);
                    emit(OpCode::NewDelegate,
                         builder_.addDelegateInfo(DelegateInfo{function, delegateType, 0}), 0);
                    return true;
                }
                modules_.error("E0409",
                               "no overload of '" + chain + "' matches delegate '" + expected.name + "'",
                               member);
                emitDefault(expected);
                return true;
            }
        }
    }

    auto targetType = inferType(*member.target);
    if (!targetType.has_value()) return false;
    auto* cls = dynamic_cast<ScriptClassType*>(targetType->object);
    if (cls == nullptr) return false;
    auto candidates = cls->findMethods(member.memberName, arity, false);
    sortDelegateOverloads(candidates, *delegateType);
    for (auto* function : candidates) {
        if (!matches(function)) continue;
        auto compiledTargetType = compileExpression(*member.target);
        // C# captures a *copy* when binding a delegate to a struct value.
        if (compiledTargetType.isStruct()) emit(OpCode::CloneStruct, 0, 0);
        emit(OpCode::NewDelegate, builder_.addDelegateInfo(DelegateInfo{function, delegateType, 0}), 0);
        return true;
    }
    if (!candidates.empty()) {
        modules_.error("E0409",
                       "no overload of '" + member.memberName + "' matches delegate '" + expected.name + "'",
                       member);
        emitDefault(expected);
        return true;
    }
    return false;
}

ScriptType FunctionCompiler::compileLambdaExpression(const LambdaExpression& lambda,
                                                     const ScriptType& expected) {
    auto* delegateType = dynamic_cast<ScriptDelegateType*>(expected.object);
    if (delegateType == nullptr) {
        modules_.error("E0411", "a lambda expression requires a delegate target type", lambda);
        emitDefault(expected);
        return expected;
    }
    // Capture every visible cell (the enclosing function hoists all locals when
    // it contains lambdas). Lambda parameters shadow outer names, so skip those.
    std::unordered_set<std::string> shadowed;
    if (lambda.declaration != nullptr) {
        for (const auto& parameter : lambda.declaration->parameters) shadowed.insert(parameter.name);
    }
    std::vector<CaptureBinding> captures;
    std::vector<int> captureSlots;
    std::unordered_set<std::string> seen;
    for (auto scopeIt = scopes_.rbegin(); scopeIt != scopes_.rend(); ++scopeIt) {
        for (const auto& entry : *scopeIt) {
            if (!entry.second.isCell) continue;
            if (shadowed.count(entry.first) > 0) continue;
            if (!seen.insert(entry.first).second) continue;
            captures.push_back({entry.first, entry.second.type});
            captureSlots.push_back(entry.second.slot);
        }
    }

    const bool instanceContext = function_->owner != nullptr && !function_->isStatic;
    auto* owner = instanceContext ? dynamic_cast<ScriptClassType*>(function_->owner) : nullptr;
    auto* function = modules_.compileLambda(lambda, *delegateType, namespaceScope(), owner,
                                           !instanceContext, captures);
    if (function == nullptr) {
        emitDefault(expected);
        return expected;
    }
    if (instanceContext) {
        emit(OpCode::LoadThis, 0, +1);   // captured `this` is the delegate target
    } else {
        emit(OpCode::PushNull, 0, +1);
    }
    for (int slot : captureSlots) emit(OpCode::LoadLocal, slot, +1);
    emit(OpCode::NewDelegate,
         builder_.addDelegateInfo(
             DelegateInfo{function, delegateType, static_cast<int>(captures.size())}),
         0);
    return expected;
}

ScriptType FunctionCompiler::compileDelegateCall(const Expression& callee, const CallExpression& call) {
    auto callableType = inferType(callee);
    auto* delegateType =
        callableType.has_value() ? dynamic_cast<ScriptDelegateType*>(callableType->object) : nullptr;
    if (delegateType == nullptr) {
        modules_.error("E0410", "expression is not a delegate", call);
        emitDefault(primI32());
        return primI32();
    }
    compileExpression(callee);
    compileArguments(call.arguments, delegateType->parameterTypes, call);
    const int argc = static_cast<int>(call.arguments.size());
    if (delegateType->returnType.isVoid()) {
        emit(OpCode::CallDelegate, argc, -(argc + 1));
    } else {
        emit(OpCode::CallDelegate, argc, -argc);
    }
    return delegateType->returnType;
}

ScriptType FunctionCompiler::compileMember(const MemberExpression& member) {
    std::string chain;
    if (flattenNameChain(member, chain) && !firstSegmentIsValue(chain)) {
        auto dot = chain.rfind('.');
        if (dot != std::string::npos) {
            const std::string prefix = chain.substr(0, dot);
            ScriptType type;
            if (lookupType(prefix, type)) return compileStaticMember(member, prefix, type);
        }
        int index = 0;
        ScriptType globalType;
        if (lookupGlobal(chain, index, globalType)) {
            emit(OpCode::LoadGlobal, index, +1);
            return globalType;
        }
        ScriptType typeOnly;
        if (lookupType(chain, typeOnly)) {
            modules_.error("E0312", "'" + chain + "' is a type, not a value", member);
            emitDefault(primI32());
            return primI32();
        }
    }
    auto targetType = compileExpression(*member.target);
    return emitMemberLoad(member, targetType);
}

ScriptType FunctionCompiler::compileStaticMember(const MemberExpression& member,
                                                 const std::string& typeName,
                                                 const ScriptType& type) {
    if (typeName == "string" && member.memberName == "Empty") {
        pushConstant(Value::string(u""));
        return primString();
    }

    if (auto* cls = dynamic_cast<ScriptClassType*>(type.object)) {
        std::string globalName = cls->name + "." + member.memberName;
        auto it = modules_.module().globalsByName.find(globalName);
        if (it != modules_.module().globalsByName.end()) {
            emit(OpCode::LoadGlobal, it->second, +1);
            return modules_.module().globals[it->second].type;
        }
        modules_.error("E0315", "'" + typeName + "' has no static field named '" + member.memberName + "'",
                       member);
        emitDefault(primI32());
        return primI32();
    }

    if (auto* native = dynamic_cast<NativeObjectType*>(type.object)) {
        if (auto* property = native->findProperty(member.memberName)) {
            if (property->getter) {
                emitNativeCall(property->getterFunction());
                return property->type;
            }
        }
        modules_.error("E0316", "'" + typeName + "' has no static property named '" + member.memberName + "'",
                       member);
        emitDefault(primI32());
        return primI32();
    }

    modules_.error("E0317", "'" + member.memberName + "' is not a static member of '" + typeName + "'", member);
    emitDefault(primI32());
    return primI32();
}

ScriptType FunctionCompiler::emitMemberLoad(const MemberExpression& member, const ScriptType& targetType) {
    if (auto* cls = dynamic_cast<ScriptClassType*>(targetType.object)) {
        if (auto* field = cls->findField(member.memberName)) {
            emitLoadField(field);
            return field->type;
        }
        modules_.error("E0318", "'" + cls->name + "' has no field named '" + member.memberName + "'", member);
        return primI32();
    }

    if (auto* native = dynamic_cast<NativeObjectType*>(targetType.object)) {
        if (auto* property = native->findProperty(member.memberName)) {
            if (property->getter) {
                emitNativeCall(property->getterFunction());
                return property->type;
            }
        }
        modules_.error("E0319", "'" + native->name + "' has no property named '" + member.memberName + "'",
                       member);
        return primI32();
    }

    if (targetType.isString() && (member.memberName == "length" || member.memberName == "Length")) {
        emit(OpCode::Length, 0, 0);
        return primI32();
    }
    if (targetType.isArray() && (member.memberName == "length" || member.memberName == "Length" ||
                                 member.memberName == "count" || member.memberName == "Count")) {
        emit(OpCode::Length, 0, 0);
        return primI32();
    }

    modules_.error("E0320", "'" + targetType.name + "' has no member named '" + member.memberName + "'",
                   member);
    return primI32();
}

ScriptType FunctionCompiler::compileIndex(const IndexExpression& expression) {
    auto targetType = compileExpression(*expression.target);
    compileExpression(*expression.index, &primI32());
    if (targetType.isArray()) {
        emit(OpCode::LoadElem, 0, -1);
        return *targetType.element;
    }
    if (targetType.isString()) {
        emit(OpCode::LoadElem, 0, -1);
        return primChar();
    }
    modules_.error("E0321", "type '" + targetType.name + "' cannot be indexed", expression);
    emit(OpCode::LoadElem, 0, -1);
    return primI32();
}

ScriptType FunctionCompiler::compileCall(const CallExpression& call) {
    if (auto* identifier = dynamic_cast<const IdentifierExpression*>(call.callee.get())) {
        auto calleeType = inferType(*identifier);
        if (calleeType && calleeType->isDelegate()) return compileDelegateCall(*identifier, call);
        return compileNamedCall(*identifier, call);
    }
    if (auto* member = dynamic_cast<const MemberExpression*>(call.callee.get())) {
        auto targetType = inferType(*member->target);
        if (targetType && targetType->isDelegate() && member->memberName == "Invoke") {
            return compileDelegateCall(*member->target, call);
        }
        auto memberType = inferType(*member);
        if (memberType && memberType->isDelegate()) return compileDelegateCall(*member, call);
        return compileMemberCall(*member, call);
    }
    auto calleeType = inferType(*call.callee);
    if (calleeType && calleeType->isDelegate()) return compileDelegateCall(*call.callee, call);
    modules_.error("E0322", "expression is not callable", call);
    return primI32();
}

bool FunctionCompiler::overloadAccepts(const std::vector<ScriptFunction*>& candidates,
                                       const std::vector<ExprPtr>& arguments) {
    for (auto* candidate : candidates) {
        if (candidate->parameters.size() != arguments.size()) continue;
        bool ok = true;
        for (std::size_t i = 0; i < arguments.size(); ++i) {
            auto argumentType = inferType(*arguments[i]);
            if (!argumentType.has_value()) continue;
            if (!canImplicitlyConvert(*argumentType, candidate->parameters[i].type)) {
                ok = false;
                break;
            }
        }
        if (ok) return true;
    }
    return false;
}

ScriptType FunctionCompiler::compileNamedCall(const IdentifierExpression& callee,
                                              const CallExpression& call) {
    int variableIndex = 0;
    if (findLocal(callee.name) != nullptr || lookupGlobal(callee.name, variableIndex)) {
        modules_.error("E0323", "'" + callee.name + "' is a variable, not a function", call);
        return primI32();
    }

    auto* owner = dynamic_cast<ScriptClassType*>(function_->owner);

    if (!call.typeArguments.empty()) {
        // Explicit type arguments always select the generic method.
        const bool wantStatic = owner == nullptr || function_->isStatic;
        if (auto generic = compileGenericCall(callee.name, owner, wantStatic, call, !wantStatic)) {
            return *generic;
        }
        return primI32();
    }

    if (owner != nullptr && function_->owner != nullptr && !function_->isStatic) {
        auto methods = instanceMethods(owner, callee.name, call.arguments.size());
        if (!methods.empty()) {
            std::vector<std::vector<ScriptType>> lists;
            for (auto* method : methods) {
                std::vector<ScriptType> types;
                for (const auto& parameter : method->parameters) types.push_back(parameter.type);
                lists.push_back(std::move(types));
            }
            int index = selectOverload(lists, std::vector<bool>(methods.size(), false), call.arguments, call);
            if (index >= 0) {
                auto* target = methods[static_cast<std::size_t>(index)];
                emit(OpCode::LoadThis, 0, +1);
                compileCallArguments(call.arguments, target->parameters, call);
                if (target->vtableSlot >= 0) {
                    emitVirtualCall(target);
                } else {
                    emitScriptCall(target);
                }
                return target->returnType;
            }
        }
    }

    if (owner != nullptr) {
        // Unqualified call to a generic method of the enclosing class.
        const bool wantStatic = function_->isStatic;
        if (auto generic = compileGenericCall(callee.name, owner, wantStatic, call, !wantStatic)) {
            return *generic;
        }
    }

    auto candidates = moduleFunctions(callee.name, call.arguments.size());
    if (candidates.empty() || !overloadAccepts(candidates, call.arguments)) {
        if (auto generic = compileGenericCall(callee.name, nullptr, true, call, false)) return *generic;
    }
    if (!candidates.empty()) return compileResolvedCall(candidates, call, callee.name);

    const auto* globals = modules_.engine().findGlobalFunctions(callee.name);
    if (globals != nullptr) {
        std::vector<NativeFunction*> nativeCandidates;
        for (auto* native : *globals) {
            if (native->matchesArity(call.arguments.size())) nativeCandidates.push_back(native);
        }
        if (!nativeCandidates.empty()) {
            std::vector<std::vector<ScriptType>> lists;
            std::vector<bool> variadic;
            for (auto* native : nativeCandidates) {
                lists.push_back(native->parameterTypes);
                variadic.push_back(native->isVariadic);
            }
            int index = selectOverload(lists, variadic, call.arguments, call);
            if (index < 0) {
                modules_.error("E0325",
                               "no overload of native function '" + callee.name + "' matches the arguments",
                               call);
                return primI32();
            }
            auto* target = nativeCandidates[static_cast<std::size_t>(index)];
            if (target->isVariadic) {
                for (const auto& argument : call.arguments) {
                    if (dynamic_cast<const RefArgumentExpression*>(argument.get()) != nullptr ||
                        dynamic_cast<const OutDeclarationExpression*>(argument.get()) != nullptr) {
                        modules_.error("E0424",
                                       "'ref'/'out'/'in' arguments are not supported by native functions",
                                       call);
                    }
                    compileExpression(*argument);
                }
                pushConstant(Value::i32(static_cast<std::int32_t>(call.arguments.size())));
                int delta = -static_cast<int>(call.arguments.size()) - 1 +
                            (target->returnType.isVoid() ? 0 : 1);
                emit(OpCode::CallNativeVariadic, builder_.addNativeRef(target), delta);
            } else {
                compileArguments(call.arguments, target->parameterTypes, call);
                emitNativeCall(target);
            }
            return target->returnType;
        }
    }

    if (isTypeReference(callee.name)) {
        modules_.error("E0326", "'" + callee.name + "' is a type; use 'new " + callee.name + "(...)'", call);
        return primI32();
    }

    modules_.error("E0327", "unknown function '" + callee.name + "'", call);
    return primI32();
}

std::optional<ScriptType> FunctionCompiler::compileGenericCall(const std::string& name,
                                                              ScriptClassType* owner, bool wantStatic,
                                                              const CallExpression& call,
                                                              bool pushThis) {
    std::vector<std::optional<ScriptType>> argumentTypes;
    argumentTypes.reserve(call.arguments.size());
    for (const auto& argument : call.arguments) argumentTypes.push_back(inferType(*argument));

    ScriptFunction* target = nullptr;
    auto status = modules_.resolveGenericMethod(name, namespaceScope(), owner, wantStatic,
                                                call.typeArguments, argumentTypes, call, target);
    if (status == ModuleCompiler::GenericCallStatus::NoMatch) {
        if (!call.typeArguments.empty()) {
            modules_.error("E0439", "explicit type arguments are not allowed on '" + name + "'", call);
            emitDefault(primI32());
            return primI32();
        }
        return std::nullopt;
    }
    if (status == ModuleCompiler::GenericCallStatus::Failed || target == nullptr) {
        emitDefault(primI32());
        return primI32();
    }
    if (pushThis) emit(OpCode::LoadThis, 0, +1);
    compileCallArguments(call.arguments, target->parameters, call);
    emitScriptCall(target);
    return target->returnType;
}

ScriptType FunctionCompiler::compileMemberCall(const MemberExpression& member, const CallExpression& call) {
    std::string chain;
    if (flattenNameChain(member, chain) && !firstSegmentIsValue(chain)) {
        auto dot = chain.rfind('.');
        if (dot != std::string::npos) {
            const std::string prefix = chain.substr(0, dot);
            ScriptType type;
            if (lookupType(prefix, type)) return compileStaticMemberCall(member, prefix, type, call);
        }
        auto candidates = moduleFunctions(chain, call.arguments.size());
        if (!candidates.empty()) return compileResolvedCall(candidates, call, chain);
        ScriptType typeOnly;
        if (lookupType(chain, typeOnly)) {
            modules_.error("E0326", "'" + chain + "' is a type; use 'new " + chain + "(...)'", call);
            return primI32();
        }
    }

    auto targetType = compileExpression(*member.target);

    if (auto* cls = dynamic_cast<ScriptClassType*>(targetType.object)) {
        auto candidates = instanceMethods(cls, member.memberName, call.arguments.size());
        if (candidates.empty()) {
            if (auto generic = compileGenericCall(member.memberName, cls, false, call, false)) {
                return *generic;
            }
            if (member.memberName == "ToString" && call.arguments.empty()) {
                emit(OpCode::ConvStr, 0, 0);
                return primString();
            }
            modules_.error("E0328",
                           "'" + cls->name + "' has no method named '" + member.memberName + "' taking " +
                               std::to_string(call.arguments.size()) + " argument(s)",
                           call);
            return primI32();
        }
        std::vector<std::vector<ScriptType>> lists;
        for (auto* method : candidates) {
            std::vector<ScriptType> types;
            for (const auto& parameter : method->parameters) types.push_back(parameter.type);
            lists.push_back(std::move(types));
        }
        int index = selectOverload(lists, std::vector<bool>(candidates.size(), false), call.arguments, call);
        if (index < 0) {
            if (auto generic = compileGenericCall(member.memberName, cls, false, call, false)) {
                return *generic;
            }
            modules_.error("E0329", "no overload of '" + member.memberName + "' matches the arguments", call);
            return primI32();
        }
        auto* target = candidates[static_cast<std::size_t>(index)];
        compileCallArguments(call.arguments, target->parameters, call);
        if (target->vtableSlot >= 0) {
            emitVirtualCall(target);
        } else {
            emitScriptCall(target);
        }
        return target->returnType;
    }

    if (auto* native = dynamic_cast<NativeObjectType*>(targetType.object)) {
        auto it = native->methods.find(member.memberName);
        if (it == native->methods.end() || it->second.empty()) {
            modules_.error("E0330", "'" + native->name + "' has no method named '" + member.memberName + "'",
                           call);
            return primI32();
        }
        std::vector<NativeFunction*> candidates;
        for (auto* method : it->second) {
            if (method->matchesArity(call.arguments.size())) candidates.push_back(method);
        }
        if (candidates.empty()) {
            modules_.error("E0331", "no overload takes the supplied argument count", call);
            return primI32();
        }
        std::vector<std::vector<ScriptType>> lists;
        std::vector<bool> variadic;
        for (auto* method : candidates) {
            lists.push_back(method->parameterTypes);
            variadic.push_back(method->isVariadic);
        }
        int index = selectOverload(lists, variadic, call.arguments, call);
        if (index < 0) {
            modules_.error("E0332", "no overload matches the supplied arguments", call);
            return primI32();
        }
        auto* target = candidates[static_cast<std::size_t>(index)];
        if (target->isVariadic) {
            for (const auto& argument : call.arguments) compileExpression(*argument);
            pushConstant(Value::i32(static_cast<std::int32_t>(call.arguments.size())));
            int delta = -static_cast<int>(call.arguments.size()) - 1 +
                        (target->returnType.isVoid() ? 0 : 1);
            emit(OpCode::CallNativeVariadic, builder_.addNativeRef(target), delta);
        } else {
            compileArguments(call.arguments, target->parameterTypes, call);
            emitNativeCall(target);
        }
        return target->returnType;
    }

    if (targetType.isString()) return compileStringMethod(member, call);
    if (targetType.isArray()) return compileArrayMethod(member, call, targetType);

    if (member.memberName == "ToString" && call.arguments.empty()) {
        emit(OpCode::ConvStr, 0, 0);
        return primString();
    }

    modules_.error("E0333", "type '" + targetType.name + "' has no method named '" + member.memberName + "'",
                   call);
    return primI32();
}

ScriptType FunctionCompiler::compileResolvedCall(const std::vector<ScriptFunction*>& candidates,
                                                 const CallExpression& call,
                                                 const std::string& displayName) {
    std::vector<std::vector<ScriptType>> lists;
    for (auto* function : candidates) {
        std::vector<ScriptType> types;
        for (const auto& parameter : function->parameters) types.push_back(parameter.type);
        lists.push_back(std::move(types));
    }
    int index = selectOverload(lists, std::vector<bool>(candidates.size(), false), call.arguments, call);
    if (index < 0) {
        modules_.error("E0324", "no overload of '" + displayName + "' matches the supplied arguments", call);
        return primI32();
    }
    auto* target = candidates[static_cast<std::size_t>(index)];
    compileCallArguments(call.arguments, target->parameters, call);
    emitScriptCall(target);
    return target->returnType;
}

ScriptType FunctionCompiler::compileStaticMemberCall(const MemberExpression& member,
                                                      const std::string& typeName,
                                                      const ScriptType& type,
                                                      const CallExpression& call) {
    if (typeName == "string" && member.memberName == "IsNullOrEmpty") {
        if (call.arguments.size() != 1) {
            modules_.error("E0396", "string.IsNullOrEmpty expects one argument", call);
            return primBool();
        }
        compileExpression(*call.arguments[0], &primString());
        Label trueLabel = builder_.newLabel();
        Label endLabel = builder_.newLabel();
        emit(OpCode::Dup, 0, +1);
        emit(OpCode::PushNull, 0, +1);
        emit(OpCode::CmpEQ, 0, -1);
        emitJump(OpCode::Jnz, trueLabel, -1);
        emit(OpCode::Dup, 0, +1);
        emit(OpCode::Length, 0, 0);
        emitJump(OpCode::Jz, trueLabel, -1);
        emit(OpCode::Pop, 0, -1);
        emit(OpCode::PushFalse, 0, +1);
        emitJump(OpCode::Jmp, endLabel, 0);
        builder_.markLabel(trueLabel);
        emit(OpCode::Pop, 0, -1);
        emit(OpCode::PushTrue, 0, +1);
        builder_.markLabel(endLabel);
        return primBool();
    }

    if (auto* cls = dynamic_cast<ScriptClassType*>(type.object)) {
        auto candidates = staticMethods(cls, member.memberName, call.arguments.size());
        if (candidates.empty()) {
            if (auto generic = compileGenericCall(member.memberName, cls, true, call, false)) {
                return *generic;
            }
            modules_.error("E0334", "'" + typeName + "' has no static method named '" + member.memberName + "'",
                           call);
            return primI32();
        }
        std::vector<std::vector<ScriptType>> lists;
        for (auto* method : candidates) {
            std::vector<ScriptType> types;
            for (const auto& parameter : method->parameters) types.push_back(parameter.type);
            lists.push_back(std::move(types));
        }
        int index = selectOverload(lists, std::vector<bool>(candidates.size(), false), call.arguments, call);
        if (index < 0) {
            if (auto generic = compileGenericCall(member.memberName, cls, true, call, false)) {
                return *generic;
            }
            modules_.error("E0335", "no overload of '" + typeName + "." + member.memberName + "' matches",
                           call);
            return primI32();
        }
        auto* target = candidates[static_cast<std::size_t>(index)];
        compileCallArguments(call.arguments, target->parameters, call);
        emitScriptCall(target);
        return target->returnType;
    }

    if (auto* native = dynamic_cast<NativeObjectType*>(type.object)) {
        auto it = native->methods.find(member.memberName);
        if (it == native->methods.end()) {
            modules_.error("E0336", "'" + typeName + "' has no method named '" + member.memberName + "'", call);
            return primI32();
        }
        std::vector<NativeFunction*> candidates;
        for (auto* method : it->second) {
            if (method->isStatic && method->matchesArity(call.arguments.size())) candidates.push_back(method);
        }
        if (candidates.empty()) {
            modules_.error("E0337", "'" + typeName + "." + member.memberName + "' is not a static method", call);
            return primI32();
        }
        std::vector<std::vector<ScriptType>> lists;
        std::vector<bool> variadic;
        for (auto* method : candidates) {
            lists.push_back(method->parameterTypes);
            variadic.push_back(method->isVariadic);
        }
        int index = selectOverload(lists, variadic, call.arguments, call);
        if (index < 0) {
            modules_.error("E0338", "no overload matches the supplied arguments", call);
            return primI32();
        }
        auto* target = candidates[static_cast<std::size_t>(index)];
        if (target->isVariadic) {
            for (const auto& argument : call.arguments) compileExpression(*argument);
            pushConstant(Value::i32(static_cast<std::int32_t>(call.arguments.size())));
            int delta = -static_cast<int>(call.arguments.size()) - 1 +
                        (target->returnType.isVoid() ? 0 : 1);
            emit(OpCode::CallNativeVariadic, builder_.addNativeRef(target), delta);
        } else {
            compileArguments(call.arguments, target->parameterTypes, call);
            emitNativeCall(target);
        }
        return target->returnType;
    }

    modules_.error("E0339", "'" + typeName + "' has no static method named '" + member.memberName + "'", call);
    return primI32();
}

std::vector<NativeFunction*>* FunctionCompiler::findStringMethods(const std::string& name) {
    auto* stringType = modules_.engine().stringMethods();
    auto it = stringType->methods.find(name);
    if (it != stringType->methods.end()) return &it->second;
    // C# naming is PascalCase; the native table is registered with camelCase.
    if (!name.empty()) {
        std::string alias = name;
        alias[0] = static_cast<char>(std::tolower(static_cast<unsigned char>(alias[0])));
        it = stringType->methods.find(alias);
        if (it != stringType->methods.end()) return &it->second;
    }
    return nullptr;
}

ScriptType FunctionCompiler::compileStringMethod(const MemberExpression& member,
                                                 const CallExpression& call) {
    auto* methods = findStringMethods(member.memberName);
    if (methods == nullptr) {
        modules_.error("E0340", "'string' has no method named '" + member.memberName + "'", call);
        return primI32();
    }

    std::vector<NativeFunction*> candidates;
    for (auto* method : *methods) {
        if (method->matchesArity(call.arguments.size())) candidates.push_back(method);
    }
    if (candidates.empty()) {
        modules_.error("E0341", "no overload of 'string." + member.memberName + "' takes the arguments", call);
        return primI32();
    }
    std::vector<std::vector<ScriptType>> lists;
    std::vector<bool> variadic;
    for (auto* method : candidates) {
        lists.push_back(method->parameterTypes);
        variadic.push_back(method->isVariadic);
    }
    int index = selectOverload(lists, variadic, call.arguments, call);
    if (index < 0) {
        modules_.error("E0342", "no overload of 'string." + member.memberName + "' matches", call);
        return primI32();
    }
    auto* target = candidates[static_cast<std::size_t>(index)];
    compileArguments(call.arguments, target->parameterTypes, call);
    emitNativeCall(target);
    return target->returnType;
}

ScriptType FunctionCompiler::compileArrayMethod(const MemberExpression& member, const CallExpression& call,
                                                const ScriptType& arrayType) {
    ScriptType elementType = arrayType.element ? *arrayType.element : primI32();

    if (member.memberName == "push" || member.memberName == "add") {
        if (call.arguments.size() != 1) {
            modules_.error("E0343", "'push' takes exactly one argument", call);
            return primVoid();
        }
        compileExpression(*call.arguments[0], &elementType);
        emit(OpCode::ArrayPush, 0, -2);
        return primVoid();
    }
    if (member.memberName == "pop") {
        if (!call.arguments.empty()) modules_.error("E0344", "'pop' takes no arguments", call);
        emit(OpCode::ArrayPop, 0, 0);
        return elementType;
    }
    if (member.memberName == "clear" || member.memberName == "indexOf" ||
        member.memberName == "contains" || member.memberName == "insert" ||
        member.memberName == "removeAt") {
        modules_.error("E0446",
                       "'" + member.memberName + "' is not available on 'T[]'; use the script library " +
                           "Array<T> (Add/Insert/RemoveAt/Clear/IndexOf/Contains)",
                       call);
        return primI32();
    }

    modules_.error("E0345", "array has no method named '" + member.memberName + "'", call);
    return primI32();
}

ScriptType FunctionCompiler::compileNewObject(const NewObjectExpression& expression) {
    ScriptType type;
    if (!modules_.resolveTypeRef(expression.type, expression, namespaceScope(), type)) return primI32();

    if (type.isDelegate()) {
        if (expression.arguments.size() != 1) {
            modules_.error("E0348", "delegate constructor of '" + type.name +
                                      "' expects one callable argument", expression);
            emitDefault(type);
            return type;
        }
        compileExpression(*expression.arguments[0], &type);
        return type;
    }
    if (auto* cls = dynamic_cast<ScriptClassType*>(type.object)) {
        if (cls->isAbstract) {
            modules_.error("E0346", "cannot instantiate abstract class '" + cls->name + "'", expression);
        }
        emit(OpCode::NewObj, builder_.addTypeRef(type), +1);
        auto* constructor = cls->constructor;
        if (constructor == nullptr) {
            if (!expression.arguments.empty()) {
                modules_.error("E0347", "class '" + cls->name + "' has no constructor taking arguments",
                               expression);
            }
            return type;
        }
        if (expression.arguments.size() != constructor->parameters.size()) {
            modules_.error("E0348",
                           "constructor of '" + cls->name + "' expects " +
                               std::to_string(constructor->parameters.size()) + " argument(s), got " +
                               std::to_string(expression.arguments.size()),
                           expression);
        }
        emit(OpCode::Dup, 0, +1);
        compileCallArguments(expression.arguments, constructor->parameters, expression);
        emitScriptCall(constructor);
        return type;
    }

    if (auto* native = dynamic_cast<NativeObjectType*>(type.object)) {
        if (native->isStatic) {
            modules_.error("E0349", "'" + native->name + "' is a static type and cannot be instantiated",
                           expression);
            return type;
        }
        auto* constructor = native->findConstructor(expression.arguments.size());
        if (constructor == nullptr) {
            modules_.error("E0350",
                           "native type '" + native->name + "' has no constructor taking " +
                               std::to_string(expression.arguments.size()) + " argument(s)",
                           expression);
            return type;
        }
        compileArguments(expression.arguments, constructor->parameterTypes, expression);
        emitNativeCall(constructor);
        return type;
    }

    modules_.error("E0351", "type '" + type.name + "' cannot be instantiated", expression);
    return type;
}

ScriptType FunctionCompiler::compileNewArray(const NewArrayExpression& expression) {
    ScriptType elementType;
    if (!modules_.resolveTypeRef(expression.elementType, expression, namespaceScope(), elementType))
        elementType = primI32();
    auto arrayType = modules_.engine().getArrayType(elementType);
    int typeRef = builder_.addTypeRef(elementType);

    if (expression.size) {
        compileExpression(*expression.size, &primI32());
        emit(OpCode::NewArray, typeRef, 0);
        return arrayType;
    }

    pushConstant(Value::i32(static_cast<std::int32_t>(expression.initializer.size())));
    emit(OpCode::NewArray, typeRef, 0);
    for (std::size_t i = 0; i < expression.initializer.size(); ++i) {
        emit(OpCode::Dup, 0, +1);
        pushConstant(Value::i32(static_cast<std::int32_t>(i)));
        compileExpression(*expression.initializer[i], &elementType);
        emit(OpCode::StoreElem, 0, -3);
    }
    return arrayType;
}

ScriptType FunctionCompiler::compileCast(const CastExpression& expression) {
    ScriptType targetType;
    if (!modules_.resolveTypeRef(expression.targetType, expression, namespaceScope(), targetType))
        return primI32();

    auto sourceType = compileExpression(*expression.operand);

    if (targetType.isStruct() || sourceType.isStruct()) {
        modules_.error("E0401", "cannot cast to or from struct type '" +
                                    (targetType.isStruct() ? targetType.name : sourceType.name) + "'",
                       expression);
        return targetType;
    }

    if (targetType.isNumeric() || targetType.isBool() || targetType.isChar()) {
        if (!sourceType.isNumeric() && !sourceType.isBool() && !sourceType.isChar()) {
            modules_.error("E0352", "cannot cast '" + sourceType.name + "' to '" + targetType.name + "'",
                           expression);
            return targetType;
        }
        emitExplicitConversion(sourceType, targetType, expression);
        return targetType;
    }

    emit(OpCode::Cast, builder_.addTypeRef(targetType), 0);
    return targetType;
}

ScriptType FunctionCompiler::compileAs(const AsExpression& expression) {
    ScriptType targetType;
    if (!modules_.resolveTypeRef(expression.targetType, expression, namespaceScope(), targetType))
        return primI32();
    compileExpression(*expression.operand);
    if (targetType.isStruct()) {
        modules_.error("E0402", "'as' cannot be used with struct type '" + targetType.name + "'", expression);
        return targetType;
    }
    emit(OpCode::Cast, builder_.addTypeRef(targetType), 0);
    return targetType;
}

ScriptType FunctionCompiler::compileTypeTest(const TypeTestExpression& expression) {
    auto operandType = compileExpression(*expression.operand);
    if (!expression.hasTargetType) {
        if (operandType.isStruct()) {
            modules_.error("E0403", "'is null' cannot be used with struct type '" + operandType.name + "'",
                           expression);
        }
        emit(OpCode::PushNull, 0, +1);
        emit(OpCode::CmpEQ, 0, -1);
    } else {
        ScriptType targetType;
        if (!modules_.resolveTypeRef(expression.targetType, expression, namespaceScope(), targetType))
            return primBool();
        if (targetType.isStruct() || operandType.isStruct()) {
            modules_.error("E0403", "'is' cannot be used with struct type '" +
                                        (targetType.isStruct() ? targetType.name : operandType.name) + "'",
                           expression);
        }
        emit(OpCode::IsType, builder_.addTypeRef(targetType), 0);
    }
    if (expression.negated) emit(OpCode::Not, 0, 0);
    return primBool();
}

ScriptType FunctionCompiler::compileUnary(const UnaryExpression& expression) {
    if (expression.op == "!") {
        compileExpression(*expression.operand, &primBool());
        emit(OpCode::Not, 0, 0);
        return primBool();
    }
    if (expression.op == "+") return compileExpression(*expression.operand);

    if (expression.op == "-") {
        auto type = compileExpression(*expression.operand);
        emitNegate(type, expression);
        return type;
    }

    if (expression.op == "~") {
        auto type = compileExpression(*expression.operand);
        if (!type.isIntegral() && !type.isChar()) {
            modules_.error("E0353", "operator '~' requires an integral operand", expression);
            return type;
        }
        if (type.primitive == Primitive::Int64 || type.primitive == Primitive::UInt64) {
            emit(OpCode::BNotI64, 0, 0);
            return primI64();
        }
        emit(OpCode::BNotI32, 0, 0);
        return primI32();
    }

    if (expression.op == "++" || expression.op == "--") {
        return compileIncrement(expression, expression.op == "++", expression.isPrefix);
    }

    modules_.error("E0354", "unsupported unary operator '" + expression.op + "'", expression);
    return compileExpression(*expression.operand);
}

ScriptType FunctionCompiler::compileIncrement(const UnaryExpression& expression, bool increment,
                                              bool prefix) {
    if (dynamic_cast<const IdentifierExpression*>(expression.operand.get()) == nullptr &&
        dynamic_cast<const MemberExpression*>(expression.operand.get()) == nullptr &&
        dynamic_cast<const IndexExpression*>(expression.operand.get()) == nullptr) {
        modules_.error("E0355", "increment/decrement requires a variable", expression);
        return primI32();
    }

    auto type = resolveLValueType(*expression.operand);
    if (!type.isNumeric()) {
        modules_.error("E0356", "increment/decrement requires a numeric variable", expression);
        return type;
    }

    ScriptType opType = type.primitive == Primitive::Int64 || type.primitive == Primitive::UInt64 ||
                                type.primitive == Primitive::UInt32
                            ? primI64()
                            : type.isFloat() ? type : primI32();

    int temp = tempSlot(type);
    emitLValueLoad(*expression.operand, type);

    if (prefix) {
        pushConstant(Value::i32(1));
        emitNumericOp(increment ? "+" : "-", opType);
        emitConversion(opType, type, expression);
        emit(OpCode::StoreLocal, temp, -1);
        emit(OpCode::LoadLocal, temp, +1);
        emitLValueStore(*expression.operand, type);
        emit(OpCode::LoadLocal, temp, +1);
    } else {
        emit(OpCode::StoreLocal, temp, -1);
        emit(OpCode::LoadLocal, temp, +1);
        pushConstant(Value::i32(1));
        emitNumericOp(increment ? "+" : "-", opType);
        emitConversion(opType, type, expression);
        emitLValueStore(*expression.operand, type);
        emit(OpCode::LoadLocal, temp, +1);
    }
    return type;
}

ScriptType FunctionCompiler::compileBinary(const BinaryExpression& expression) {
    if (expression.op == "&&") return compileLogicalAnd(expression);
    if (expression.op == "||") return compileLogicalOr(expression);
    if (isComparisonOp(expression.op)) return compileComparison(expression);

    if (expression.op == "+" && isStringConcat(expression)) {
        auto leftType = compileExpression(*expression.left);
        if (!leftType.isString()) emit(OpCode::ConvStr, 0, 0);
        auto rightType = compileExpression(*expression.right);
        if (!rightType.isString()) emit(OpCode::ConvStr, 0, 0);
        emit(OpCode::AddStr, 0, -1);
        return primString();
    }

    if (expression.op == "+" || expression.op == "-") {
        auto leftInferred = inferType(*expression.left);
        auto rightInferred = inferType(*expression.right);
        const ScriptType* delegateType = nullptr;
        if (leftInferred.has_value() && leftInferred->isDelegate()) delegateType = &*leftInferred;
        else if (rightInferred.has_value() && rightInferred->isDelegate()) delegateType = &*rightInferred;
        if (delegateType != nullptr) {
            compileExpression(*expression.left, delegateType);
            compileExpression(*expression.right, delegateType);
            emit(expression.op == "+" ? OpCode::DelegateAdd : OpCode::DelegateSub, 0, -1);
            return *delegateType;
        }
    }

    auto left = compileExpression(*expression.left);
    auto rightType = inferType(*expression.right);

    if (isBitwiseOp(expression.op)) {
        if ((!left.isIntegral() && !left.isChar()) ||
            (rightType.has_value() && !rightType->isIntegral() && !rightType->isChar())) {
            modules_.error("E0386", "operator '" + expression.op + "' requires integral operands", expression);
        }
        auto bitwiseType = (left.primitive == Primitive::Int64 || left.primitive == Primitive::UInt64 ||
                            (rightType.has_value() && (rightType->primitive == Primitive::Int64 ||
                                                       rightType->primitive == Primitive::UInt64)))
                               ? primI64()
                               : primI32();
        emitConversion(left, bitwiseType, *expression.left);
        compileExpression(*expression.right, &bitwiseType);
        emitNumericOp(expression.op, bitwiseType);
        return bitwiseType;
    }

    auto operationType = promoteNumeric(left, rightType.has_value() ? *rightType : left, expression);
    emitConversion(left, operationType, *expression.left);
    compileExpression(*expression.right, &operationType);
    emitNumericOp(expression.op, operationType);
    return operationType;
}

ScriptType FunctionCompiler::compileLogicalAnd(const BinaryExpression& expression) {
    compileExpression(*expression.left, &primBool());
    Label falseLabel = builder_.newLabel();
    Label endLabel = builder_.newLabel();
    emitJump(OpCode::Jz, falseLabel, -1);
    compileExpression(*expression.right, &primBool());
    emitJump(OpCode::Jz, falseLabel, -1);
    emit(OpCode::PushTrue, 0, +1);
    emitJump(OpCode::Jmp, endLabel, 0);
    builder_.markLabel(falseLabel);
    emit(OpCode::PushFalse, 0, +1);
    builder_.markLabel(endLabel);
    return primBool();
}

ScriptType FunctionCompiler::compileLogicalOr(const BinaryExpression& expression) {
    compileExpression(*expression.left, &primBool());
    Label trueLabel = builder_.newLabel();
    Label endLabel = builder_.newLabel();
    emitJump(OpCode::Jnz, trueLabel, -1);
    compileExpression(*expression.right, &primBool());
    emitJump(OpCode::Jnz, trueLabel, -1);
    emit(OpCode::PushFalse, 0, +1);
    emitJump(OpCode::Jmp, endLabel, 0);
    builder_.markLabel(trueLabel);
    emit(OpCode::PushTrue, 0, +1);
    builder_.markLabel(endLabel);
    return primBool();
}

ScriptType FunctionCompiler::compileComparison(const BinaryExpression& expression) {
    auto leftType = inferType(*expression.left);
    auto rightType = inferType(*expression.right);
    bool equality = isEqualityOp(expression.op);
    bool valid = true;

    if (leftType.has_value() && rightType.has_value()) {
        if (leftType->isNullType() || rightType->isNullType()) {
            valid = equality && (leftType->isReference() || rightType->isReference());
        } else if (leftType->isString() || rightType->isString()) {
            valid = equality;
        } else if (leftType->isNumeric() && rightType->isNumeric()) {
            valid = true;
        } else if (leftType->isBool() && rightType->isBool()) {
            valid = equality;
        } else if (equality && leftType->isStruct() && rightType->isStruct() &&
                   leftType->sameAs(*rightType)) {
            // Structs compare field by field through ValueOps::areEqual; only
            // equality is defined, ordering would need an explicit contract.
            valid = true;
        } else if (leftType->isReference() && rightType->isReference()) {
            valid = equality;
        } else {
            valid = false;
        }
    }

    if (!valid) {
        modules_.error("E0357",
                       "operator '" + expression.op + "' cannot be applied to '" +
                           (leftType ? leftType->name : "?") + "' and '" +
                           (rightType ? rightType->name : "?") + "'",
                       expression);
    }

    compileExpression(*expression.left);
    compileExpression(*expression.right);
    OpCode opcode = OpCode::CmpEQ;
    if (expression.op == "==") opcode = OpCode::CmpEQ;
    else if (expression.op == "!=") opcode = OpCode::CmpNE;
    else if (expression.op == "<") opcode = OpCode::CmpLT;
    else if (expression.op == "<=") opcode = OpCode::CmpLE;
    else if (expression.op == ">") opcode = OpCode::CmpGT;
    else opcode = OpCode::CmpGE;
    emit(opcode, 0, -1);
    return primBool();
}

ScriptType FunctionCompiler::compileAssignment(const AssignmentExpression& expression) {
    bool compound = expression.op != "=";
    auto targetType = resolveLValueType(*expression.target);
    if (targetType.isVoid()) return targetType;

    if (!compound) {
        compileLValueAddress(*expression.target);
        compileExpression(*expression.value, &targetType);
        emitLValueStore(*expression.target, targetType);
        return primVoid();
    }

    std::string op = expression.op.substr(0, expression.op.size() - 1);
    if (targetType.isDelegate() && (op == "+" || op == "-")) {
        emitLValueLoad(*expression.target, targetType);
        compileExpression(*expression.value, &targetType);
        emit(op == "+" ? OpCode::DelegateAdd : OpCode::DelegateSub, 0, -1);
        emitLValueStore(*expression.target, targetType);
        return primVoid();
    }
    auto valueType = inferType(*expression.value);
    bool stringConcat = op == "+" && (targetType.isString() || (valueType && valueType->isString()));
    auto operationType = stringConcat
                             ? primString()
                             : promoteNumeric(targetType, valueType ? *valueType : targetType, expression);

    emitLValueLoad(*expression.target, targetType);
    if (stringConcat) {
        if (!targetType.isString()) emit(OpCode::ConvStr, 0, 0);
        auto compiledValueType = compileExpression(*expression.value);
        if (!compiledValueType.isString()) emit(OpCode::ConvStr, 0, 0);
        emit(OpCode::AddStr, 0, -1);
    } else {
        emitConversion(targetType, operationType, expression);
        compileExpression(*expression.value, &operationType);
        emitNumericOp(op, operationType);
        emitConversion(operationType, targetType, expression);
    }
    emitLValueStore(*expression.target, targetType);
    return primVoid();
}

ScriptType FunctionCompiler::compileConditional(const ConditionalExpression& expression) {
    auto trueType = inferType(*expression.whenTrue);
    auto falseType = inferType(*expression.whenFalse);
    ScriptType resultType = primI32();

    if (trueType.has_value() && falseType.has_value()) {
        if (trueType->sameAs(*falseType)) {
            resultType = *trueType;
        } else if (trueType->isNullType()) {
            resultType = *falseType;
        } else if (falseType->isNullType()) {
            resultType = *trueType;
        } else if (trueType->isNumeric() && falseType->isNumeric()) {
            resultType = promoteNumeric(*trueType, *falseType, expression);
        } else if (canImplicitlyConvert(*falseType, *trueType)) {
            resultType = *trueType;
        } else if (canImplicitlyConvert(*trueType, *falseType)) {
            resultType = *falseType;
        } else {
            modules_.error("E0358", "the two branches of '?:' must have compatible types", expression);
        }
    } else {
        resultType = trueType.has_value() ? *trueType : falseType.has_value() ? *falseType : primI32();
    }

    compileExpression(*expression.condition, &primBool());
    Label falseLabel = builder_.newLabel();
    Label endLabel = builder_.newLabel();
    emitJump(OpCode::Jz, falseLabel, -1);
    compileExpression(*expression.whenTrue, &resultType);
    emitJump(OpCode::Jmp, endLabel, 0);
    builder_.markLabel(falseLabel);
    compileExpression(*expression.whenFalse, &resultType);
    builder_.markLabel(endLabel);
    return resultType;
}

// ------------------------------------------------------------------- lvalues

ScriptType FunctionCompiler::resolveLValueType(const Expression& target) {
    if (auto* identifier = dynamic_cast<const IdentifierExpression*>(&target)) {
        if (auto* local = findLocal(identifier->name)) return local->type;
        auto& module = modules_.module();
        int index = 0;
        if (lookupGlobal(identifier->name, index)) return module.globals[index].type;
        auto* owner = dynamic_cast<ScriptClassType*>(function_->owner);
        if (owner != nullptr) {
            auto staticIt = module.globalsByName.find(owner->name + "." + identifier->name);
            if (staticIt != module.globalsByName.end()) return module.globals[staticIt->second].type;
        }
        if (auto* field = findInstanceField(identifier->name)) return field->type;
        modules_.error("E0359", "unknown variable '" + identifier->name + "'", target);
        return primVoid();
    }

    if (auto* member = dynamic_cast<const MemberExpression*>(&target)) {
        int staticIndex = 0;
        ScriptType staticType;
        if (resolveStaticGlobal(target, staticIndex, staticType)) return staticType;

        std::string chain;
        if (flattenNameChain(target, chain)) {
            auto dot = chain.rfind('.');
            if (dot != std::string::npos) {
                ScriptType ownerType;
                if (lookupType(chain.substr(0, dot), ownerType)) {
                    if (auto* native = dynamic_cast<NativeObjectType*>(ownerType.object)) {
                        if (auto* property = native->findProperty(member->memberName)) {
                            if (!property->setter) {
                                modules_.error("E0364", "property '" + member->memberName + "' is read-only",
                                               target);
                            }
                            return property->type;
                        }
                    }
                    modules_.error("E0360",
                                   "'" + chain + "' is not an assignable static member", target);
                    return primVoid();
                }
            }
        }

        auto targetType = inferType(*member->target);
        if (!targetType.has_value()) {
            modules_.error("E0361", "cannot determine the type of the assignment target", target);
            return primVoid();
        }
        if (auto* cls = dynamic_cast<ScriptClassType*>(targetType->object)) {
            auto* field = cls->findField(member->memberName);
            if (field == nullptr) {
                modules_.error("E0362", "'" + cls->name + "' has no field named '" + member->memberName + "'",
                               target);
                return primVoid();
            }
            if (field->isReadOnly && !function_->isConstructor) {
                modules_.error("E0363", "field '" + member->memberName + "' is readonly", target);
            }
            return field->type;
        }
        if (auto* native = dynamic_cast<NativeObjectType*>(targetType->object)) {
            if (auto* property = native->findProperty(member->memberName)) {
                if (!property->setter) {
                    modules_.error("E0364", "property '" + member->memberName + "' is read-only", target);
                }
                return property->type;
            }
            modules_.error("E0365", "'" + native->name + "' has no field or property named '" +
                                        member->memberName + "'",
                           target);
            return primVoid();
        }
        modules_.error("E0366", "'" + targetType->name + "' has no assignable member '" +
                                    member->memberName + "'",
                       target);
        return primVoid();
    }

    if (auto* index = dynamic_cast<const IndexExpression*>(&target)) {
        auto targetType = inferType(*index->target);
        if (!targetType.has_value()) {
            modules_.error("E0367", "cannot determine the type of the indexed expression", target);
            return primVoid();
        }
        if (targetType->isArray()) return *targetType->element;
        modules_.error("E0368", "type '" + targetType->name + "' does not support index assignment", target);
        return primVoid();
    }

    modules_.error("E0369", "expression is not assignable", target);
    return primVoid();
}

void FunctionCompiler::compileLValueAddress(const Expression& target) {
    if (auto* identifier = dynamic_cast<const IdentifierExpression*>(&target)) {
        if (findLocal(identifier->name) != nullptr) return;
        auto& module = modules_.module();
        int index = 0;
        if (lookupGlobal(identifier->name, index)) return;
        auto* owner = dynamic_cast<ScriptClassType*>(function_->owner);
        if (owner != nullptr && module.globalsByName.count(owner->name + "." + identifier->name) > 0) return;
        if (findInstanceField(identifier->name) != nullptr) emit(OpCode::LoadThis, 0, +1);
        return;
    }

    if (auto* member = dynamic_cast<const MemberExpression*>(&target)) {
        int index = 0;
        ScriptType staticType;
        if (resolveStaticGlobal(target, index, staticType)) return;
        std::string chain;
        if (flattenNameChain(target, chain) && !firstSegmentIsValue(chain)) {
            auto dot = chain.rfind('.');
            if (dot != std::string::npos) {
                ScriptType ownerType;
                if (lookupType(chain.substr(0, dot), ownerType)) return;   // static property: no address
            }
        }
        compileExpression(*member->target);
        return;
    }

    if (auto* index = dynamic_cast<const IndexExpression*>(&target)) {
        compileExpression(*index->target);
        compileExpression(*index->index, &primI32());
        return;
    }
}

void FunctionCompiler::emitLValueLoad(const Expression& target, const ScriptType& type) {
    if (auto* identifier = dynamic_cast<const IdentifierExpression*>(&target)) {
        if (auto* local = findLocal(identifier->name)) {
            emit(OpCode::LoadLocal, local->slot, +1);
            if (local->isRef) {
                emit(OpCode::LoadRef, 0, 0);
            } else if (local->isCell) {
                emit(OpCode::LoadCell, 0, 0);
            }
            return;
        }
        auto& module = modules_.module();
        int globalIndex = 0;
        if (lookupGlobal(identifier->name, globalIndex)) {
            emit(OpCode::LoadGlobal, globalIndex, +1);
            return;
        }
        auto* owner = dynamic_cast<ScriptClassType*>(function_->owner);
        if (owner != nullptr) {
            auto staticIt = module.globalsByName.find(owner->name + "." + identifier->name);
            if (staticIt != module.globalsByName.end()) {
                emit(OpCode::LoadGlobal, staticIt->second, +1);
                return;
            }
        }
        if (auto* field = findInstanceField(identifier->name)) {
            emit(OpCode::LoadThis, 0, +1);
            emit(OpCode::Dup, 0, +1);
            emitLoadField(field);
            return;
        }
        emitDefault(type);
        return;
    }

    if (auto* member = dynamic_cast<const MemberExpression*>(&target)) {
        int staticIndex = 0;
        ScriptType staticType;
        if (resolveStaticGlobal(target, staticIndex, staticType)) {
            emit(OpCode::LoadGlobal, staticIndex, +1);
            return;
        }
        std::string chain;
        if (flattenNameChain(target, chain) && !firstSegmentIsValue(chain)) {
            auto dot = chain.rfind('.');
            if (dot != std::string::npos) {
                ScriptType ownerType;
                if (lookupType(chain.substr(0, dot), ownerType)) {
                    if (auto* native = dynamic_cast<NativeObjectType*>(ownerType.object)) {
                        if (auto* property = native->findProperty(member->memberName)) {
                            if (property->getter) {
                                emitNativeCall(property->getterFunction());
                                return;
                            }
                        }
                    }
                    emitDefault(type);
                    return;
                }
            }
        }

        auto addressType = compileExpression(*member->target);
        emit(OpCode::Dup, 0, +1);
        if (auto* cls = dynamic_cast<ScriptClassType*>(addressType.object)) {
            if (auto* field = cls->findField(member->memberName)) {
                emitLoadField(field);
                return;
            }
        }
        if (auto* native = dynamic_cast<NativeObjectType*>(addressType.object)) {
            if (auto* property = native->findProperty(member->memberName)) {
                if (property->getter) {
                    emitNativeCall(property->getterFunction());
                    return;
                }
            }
        }
        emit(OpCode::Pop, 0, -1);
        emit(OpCode::Pop, 0, -1);
        emitDefault(type);
        return;
    }

    if (auto* index = dynamic_cast<const IndexExpression*>(&target)) {
        compileExpression(*index->target);
        compileExpression(*index->index, &primI32());
        emit(OpCode::Dup2, 0, +2);
        emit(OpCode::LoadElem, 0, -1);
        return;
    }

    emitDefault(type);
}

void FunctionCompiler::emitLValueStore(const Expression& target, const ScriptType& type) {
    if (auto* identifier = dynamic_cast<const IdentifierExpression*>(&target)) {
        // Locals and globals are plain slots, so the value-semantics copy has to
        // be emitted here (fields/elements copy through convertArgument).
        if (type.isStruct()) emit(OpCode::CloneStruct, 0, 0);
        if (auto* local = findLocal(identifier->name)) {
            if (local->isConst) modules_.error("E0370", "variable '" + identifier->name + "' is const", target);
            if (local->isReadOnlyRef) {
                modules_.error("E0423", "cannot assign to 'in' parameter '" + identifier->name + "'", target);
            }
            if (local->isRef) {
                // Stack is [value]; push the reference and let StoreRef pop both.
                emit(OpCode::LoadLocal, local->slot, +1);
                emit(OpCode::StoreRef, 0, -2);
                return;
            }
            if (local->isCell) {
                // Stack is [value]; push the cell and let StoreCell pop both.
                emit(OpCode::LoadLocal, local->slot, +1);
                emit(OpCode::StoreCell, 0, -2);
                return;
            }
            emit(OpCode::StoreLocal, local->slot, -1);
            return;
        }
        auto& module = modules_.module();
        int globalIndex = 0;
        if (lookupGlobal(identifier->name, globalIndex)) {
            if (module.globals[globalIndex].isReadOnly)
                modules_.error("E0371", "global variable '" + identifier->name + "' is readonly", target);
            emit(OpCode::StoreGlobal, globalIndex, -1);
            return;
        }
        auto* owner = dynamic_cast<ScriptClassType*>(function_->owner);
        if (owner != nullptr) {
            auto staticIt = module.globalsByName.find(owner->name + "." + identifier->name);
            if (staticIt != module.globalsByName.end()) {
                emit(OpCode::StoreGlobal, staticIt->second, -1);
                return;
            }
        }
        if (auto* field = findInstanceField(identifier->name)) {
            emitStoreField(field);
            return;
        }
        emit(OpCode::Pop, 0, -1);
        return;
    }

    if (auto* member = dynamic_cast<const MemberExpression*>(&target)) {
        int staticIndex = 0;
        ScriptType staticType;
        if (resolveStaticGlobal(target, staticIndex, staticType)) {
            if (modules_.module().globals[staticIndex].isReadOnly)
                modules_.error("E0372", "static field '" + member->memberName + "' is readonly", target);
            emit(OpCode::StoreGlobal, staticIndex, -1);
            return;
        }
        std::string chain;
        if (flattenNameChain(target, chain) && !firstSegmentIsValue(chain)) {
            auto dot = chain.rfind('.');
            if (dot != std::string::npos) {
                ScriptType ownerType;
                if (lookupType(chain.substr(0, dot), ownerType)) {
                    if (auto* native = dynamic_cast<NativeObjectType*>(ownerType.object)) {
                        if (auto* property = native->findProperty(member->memberName)) {
                            if (property->setter) {
                                emitNativeCall(property->setterFunction());
                                return;
                            }
                        }
                    }
                    emit(OpCode::Pop, 0, -1);
                    return;
                }
            }
        }

        auto targetType = inferType(*member->target);
        if (targetType.has_value()) {
            if (auto* cls = dynamic_cast<ScriptClassType*>(targetType->object)) {
                if (auto* field = cls->findField(member->memberName)) {
                    emitStoreField(field);
                    return;
                }
            }
            if (auto* native = dynamic_cast<NativeObjectType*>(targetType->object)) {
                if (auto* property = native->findProperty(member->memberName)) {
                    if (property->setter) {
                        emitNativeCall(property->setterFunction());
                        return;
                    }
                }
            }
        }
        emit(OpCode::Pop, 0, -1);
        emit(OpCode::Pop, 0, -1);
        return;
    }

    if (dynamic_cast<const IndexExpression*>(&target) != nullptr) {
        emit(OpCode::StoreElem, 0, -3);
        return;
    }

    emit(OpCode::Pop, 0, -1);
}

// ------------------------------------------------------------------- helpers

int FunctionCompiler::allocateSlot(const ScriptType& type) {
    (void)type;
    return nextSlot_++;
}

int FunctionCompiler::tempSlot(const ScriptType& type) {
    (void)type;
    if (tempSlot_ < 0) tempSlot_ = nextSlot_++;
    return tempSlot_;
}

void FunctionCompiler::pushConstant(const Value& value) {
    if (value.kind() == ValueKind::Int32) {
        std::int64_t intValue = value.asI64();
        if (intValue >= -8388608 && intValue <= 8388607) {
            emit(OpCode::PushI32Imm, static_cast<int>(intValue) & 0xFFFFFF, +1);
            return;
        }
    }
    emit(OpCode::PushConst, builder_.addConstant(value), +1);
}

void FunctionCompiler::emitJump(OpCode op, const Label& label, int stackDelta) {
    builder_.emitJump(op, label, stackDelta);
}

void FunctionCompiler::emitLoadField(FieldInfo* field) {
    emit(OpCode::LoadField, builder_.addFieldRef(field), 0);
}

void FunctionCompiler::emitStoreField(FieldInfo* field) {
    emit(OpCode::StoreField, builder_.addFieldRef(field), -2);
}

void FunctionCompiler::compileArguments(const std::vector<ExprPtr>& arguments,
                                        const std::vector<ScriptType>& parameterTypes,
                                        const Node& node) {
    if (arguments.size() != parameterTypes.size()) {
        modules_.error("E0373",
                       "expected " + std::to_string(parameterTypes.size()) + " argument(s), got " +
                           std::to_string(arguments.size()),
                       node);
    }
    std::size_t count = std::min(arguments.size(), parameterTypes.size());
    for (std::size_t i = 0; i < count; ++i) {
        if (dynamic_cast<const RefArgumentExpression*>(arguments[i].get()) != nullptr ||
            dynamic_cast<const OutDeclarationExpression*>(arguments[i].get()) != nullptr) {
            modules_.error("E0424", "'ref'/'out'/'in' arguments are not supported by native functions", node);
        }
        compileExpression(*arguments[i], &parameterTypes[i]);
    }
    for (std::size_t i = count; i < arguments.size(); ++i) {
        compileExpression(*arguments[i]);
        emit(OpCode::Pop, 0, -1);
    }
}

const char* modifierName(ParamModifier modifier) {
    switch (modifier) {
        case ParamModifier::Ref: return "ref";
        case ParamModifier::Out: return "out";
        case ParamModifier::In: return "in";
        default: return "";
    }
}

FieldInfo* FunctionCompiler::resolveField(const MemberExpression& member) {
    auto targetType = inferType(*member.target);
    if (!targetType.has_value()) return nullptr;
    auto* cls = dynamic_cast<ScriptClassType*>(targetType->object);
    return cls != nullptr ? cls->findField(member.memberName) : nullptr;
}

void FunctionCompiler::compileRefArgument(const Expression& target, bool allowRvalueTemp,
                                          const Node& node) {
    const Expression* operand = &target;
    bool rvalueAllowed = allowRvalueTemp;
    if (auto* refArg = dynamic_cast<const RefArgumentExpression*>(&target)) {
        operand = refArg->operand.get();
        rvalueAllowed = allowRvalueTemp && refArg->modifier == ParamModifier::In;
    }

    if (auto* identifier = dynamic_cast<const IdentifierExpression*>(operand)) {
        if (auto* local = findLocal(identifier->name)) {
            if (local->isRef) {
                emit(OpCode::LoadLocal, local->slot, +1);   // forward the same reference
                return;
            }
            if (local->isCell) {
                emit(OpCode::LoadLocal, local->slot, +1);
                emit(OpCode::MakeRefCell, 0, 0);
                return;
            }
            modules_.error("E0420", "'" + identifier->name + "' cannot be passed by reference", node);
            emitDefault(local->type);
            emit(OpCode::NewCell, 0, 0);
            emit(OpCode::MakeRefCell, 0, 0);
            return;
        }
        int globalIndex = 0;
        if (lookupGlobal(identifier->name, globalIndex)) {
            emit(OpCode::MakeRefGlobal, globalIndex, +1);
            return;
        }
        auto* owner = dynamic_cast<ScriptClassType*>(function_->owner);
        if (owner != nullptr) {
            auto staticIt = modules_.module().globalsByName.find(owner->name + "." + identifier->name);
            if (staticIt != modules_.module().globalsByName.end()) {
                emit(OpCode::MakeRefGlobal, staticIt->second, +1);
                return;
            }
        }
        if (auto* field = findInstanceField(identifier->name)) {
            emit(OpCode::LoadThis, 0, +1);
            emit(OpCode::MakeRefField, builder_.addFieldRef(field), 0);
            return;
        }
        modules_.error("E0420", "'" + identifier->name + "' cannot be passed by reference", node);
        emitDefault(primI32());
        emit(OpCode::NewCell, 0, 0);
        emit(OpCode::MakeRefCell, 0, 0);
        return;
    }

    if (auto* member = dynamic_cast<const MemberExpression*>(operand)) {
        if (auto* field = resolveField(*member)) {
            compileExpression(*member->target);
            emit(OpCode::MakeRefField, builder_.addFieldRef(field), 0);
            return;
        }
    }

    if (auto* index = dynamic_cast<const IndexExpression*>(operand)) {
        compileExpression(*index->target);
        compileExpression(*index->index, &primI32());
        emit(OpCode::MakeRefElem, 0, -1);
        return;
    }

    if (!rvalueAllowed) {
        modules_.error("E0420", "expression cannot be passed by reference", node);
    }
    auto type = compileExpression(*operand);
    emit(OpCode::NewCell, 0, 0);
    emit(OpCode::MakeRefCell, 0, 0);
}

void FunctionCompiler::compileCallArguments(const std::vector<ExprPtr>& arguments,
                                           const std::vector<ScriptParameter>& parameters,
                                           const Node& node) {
    if (arguments.size() != parameters.size()) {
        modules_.error("E0373",
                       "expected " + std::to_string(parameters.size()) + " argument(s), got " +
                           std::to_string(arguments.size()),
                       node);
    }
    const std::size_t count = std::min(arguments.size(), parameters.size());
    for (std::size_t i = 0; i < count; ++i) {
        const auto& parameter = parameters[i];
        const ExprPtr& argument = arguments[i];
        auto* refArg = dynamic_cast<const RefArgumentExpression*>(argument.get());
        auto* outDecl = dynamic_cast<const OutDeclarationExpression*>(argument.get());

        if (parameter.modifier == ParamModifier::None) {
            if (refArg != nullptr) {
                modules_.error("E0421", "parameter '" + parameter.name + "' is not a " +
                                            std::string(modifierName(refArg->modifier)) + " parameter",
                               node);
                compileExpression(*refArg->operand);
            } else if (outDecl != nullptr) {
                modules_.error("E0422", "parameter '" + parameter.name + "' is not an out parameter", node);
                emitDefault(parameter.type);
            } else {
                compileExpression(*argument, &parameter.type);
            }
            continue;
        }

        if (parameter.modifier == ParamModifier::In) {
            if (refArg != nullptr && refArg->modifier == ParamModifier::In) {
                compileRefArgument(*argument, false, node);
            } else if (outDecl != nullptr) {
                modules_.error("E0422", "parameter '" + parameter.name + "' is not an out parameter", node);
                emitDefault(parameter.type);
                emit(OpCode::NewCell, 0, 0);
                emit(OpCode::MakeRefCell, 0, 0);
            } else {
                // `in` accepts rvalues by copying them into a temporary cell.
                compileExpression(*argument, &parameter.type);
                emit(OpCode::NewCell, 0, 0);
                emit(OpCode::MakeRefCell, 0, 0);
            }
            continue;
        }

        // ref / out
        if (outDecl != nullptr) {
            ScriptType type = parameter.type;
            if (outDecl->type.has_value()) {
                ScriptType declared;
                if (modules_.resolveTypeRef(*outDecl->type, *outDecl, namespaceScope(), declared)) {
                    type = declared;
                }
            }
            emitDefault(type);
            emit(OpCode::NewCell, 0, 0);
            const int slot = allocateSlot(type);
            emit(OpCode::StoreLocal, slot, -1);
            declareLocal(outDecl->name, type, slot, false, true);
            emit(OpCode::LoadLocal, slot, +1);
            emit(OpCode::MakeRefCell, 0, 0);
            continue;
        }

        if (refArg == nullptr || refArg->modifier != parameter.modifier) {
            modules_.error("E0421", "argument for '" + parameter.name + "' must be passed with '" +
                                        std::string(modifierName(parameter.modifier)) + "'",
                           node);
            if (refArg != nullptr) {
                compileRefArgument(*refArg, false, node);
            } else {
                compileExpression(*argument, &parameter.type);
                emit(OpCode::NewCell, 0, 0);
                emit(OpCode::MakeRefCell, 0, 0);
            }
            continue;
        }
        compileRefArgument(*refArg, false, node);
    }
    for (std::size_t i = count; i < arguments.size(); ++i) {
        compileExpression(*arguments[i]);
        emit(OpCode::Pop, 0, -1);
    }
}

void FunctionCompiler::emitScriptCall(ScriptFunction* target) {
    int reference = builder_.addFunctionRef(target);
    int pops = static_cast<int>(target->parameters.size()) + (target->isStatic ? 0 : 1);
    int pushes = target->returnType.isVoid() ? 0 : 1;
    emit(OpCode::Call, reference, pushes - pops);
}

void FunctionCompiler::emitVirtualCall(ScriptFunction* target) {
    int reference = builder_.addVirtualRef(target);
    int pops = static_cast<int>(target->parameters.size()) + 1;
    int pushes = target->returnType.isVoid() ? 0 : 1;
    emit(OpCode::CallVirt, reference, pushes - pops);
}

void FunctionCompiler::emitNativeCall(NativeFunction* target) {
    int reference = builder_.addNativeRef(target);
    int pops = static_cast<int>(target->parameterTypes.size()) + (target->isStatic ? 0 : 1);
    int pushes = target->returnType.isVoid() ? 0 : 1;
    emit(OpCode::CallNative, reference, pushes - pops);
}

int FunctionCompiler::selectOverload(const std::vector<std::vector<ScriptType>>& parameterLists,
                                     const std::vector<bool>& variadic,
                                     const std::vector<ExprPtr>& arguments, const Node& node) {
    (void)node;
    if (parameterLists.size() == 1) return 0;

    int best = -1;
    int bestScore = -1;
    for (std::size_t c = 0; c < parameterLists.size(); ++c) {
        const auto& parameters = parameterLists[c];
        bool isVariadic = c < variadic.size() && variadic[c];
        if (!isVariadic && parameters.size() != arguments.size()) continue;

        int score = 0;
        bool compatible = true;
        for (std::size_t i = 0; i < arguments.size(); ++i) {
            auto argumentType = inferType(*arguments[i]);
            if (i >= parameters.size()) {
                score += 1;
                continue;
            }
            if (!argumentType.has_value()) {
                score += 1;
                continue;
            }
            if (argumentType->sameAs(parameters[i])) {
                score += 4;
            } else if (canImplicitlyConvert(*argumentType, parameters[i])) {
                score += 2;
            } else {
                compatible = false;
                break;
            }
        }
        if (compatible && score > bestScore) {
            best = static_cast<int>(c);
            bestScore = score;
        }
    }
    return best;
}

bool FunctionCompiler::canImplicitlyConvert(const ScriptType& from, const ScriptType& to) const {
    if (to.isVar() || from.sameAs(to) || from.isVar()) return true;
    if (from.isNullType()) return to.isReference();
    if (to.isString()) return from.isString();
    if (from.isArray() || to.isArray()) {
        return from.isArray() && to.isArray() && from.element->sameAs(*to.element);
    }
    if (from.isNumeric() && to.isNumeric()) {
        return ScriptType::canImplicitNumeric(from.primitive, to.primitive);
    }
    if (from.object != nullptr && to.object != nullptr) {
        return to.object->isAssignableFrom(from.object);
    }
    return false;
}

void FunctionCompiler::emitConversion(const ScriptType& from, const ScriptType& to, const Node& node,
                                      bool explicitCast) {
    if (to.isVar() || from.sameAs(to) || to.isVoid()) return;

    if (from.isNullType()) {
        if (!to.isReference()) modules_.error("E0374", "cannot convert 'null' to '" + to.name + "'", node);
        return;
    }
    if (to.isString()) {
        if (!from.isString()) modules_.error("E0375", "cannot convert '" + from.name + "' to 'string'", node);
        return;
    }
    if (from.isString()) {
        modules_.error("E0376", "cannot convert 'string' to '" + to.name + "'", node);
        return;
    }
    if (from.isArray() || to.isArray()) {
        if (!(from.isArray() && to.isArray() && from.element->sameAs(*to.element))) {
            modules_.error("E0377", "cannot convert '" + from.name + "' to '" + to.name + "'", node);
        }
        return;
    }
    if (to.isNumeric() || to.isChar() || to.isBool()) {
        if (from.isBool() && !to.isBool()) {
            modules_.error("E0378", "cannot convert 'bool' to '" + to.name + "'", node);
            return;
        }
        if (from.isNumeric() || from.isChar()) {
            if (!explicitCast && !from.sameAs(to) &&
                !ScriptType::canImplicitNumeric(from.primitive, to.primitive)) {
                modules_.error("E0379",
                               "cannot implicitly convert '" + from.name + "' to '" + to.name +
                                   "'; use an explicit cast",
                               node);
            }
            emitConvertOpcode(to);
            return;
        }
        if (from.isBool() && to.isBool()) return;
        modules_.error("E0380", "cannot convert '" + from.name + "' to '" + to.name + "'", node);
        return;
    }
    if (from.object != nullptr && to.object != nullptr) {
        if (to.object->isAssignableFrom(from.object)) return;
        if (explicitCast) {
            emit(OpCode::Cast, builder_.addTypeRef(to), 0);
            return;
        }
        modules_.error("E0381", "cannot convert '" + from.name + "' to '" + to.name + "'", node);
        return;
    }
    modules_.error("E0382", "cannot convert '" + from.name + "' to '" + to.name + "'", node);
}

void FunctionCompiler::emitExplicitConversion(const ScriptType& from, const ScriptType& to,
                                               const Node& node) {
    if (from.sameAs(to)) return;
    if (to.isBool() && from.isNumeric()) {
        emit(OpCode::ConvBool, 0, 0);
        return;
    }
    if ((to.isNumeric() || to.isChar()) && (from.isNumeric() || from.isChar() || from.isBool())) {
        emitConvertOpcode(to);
        return;
    }
    emitConversion(from, to, node, true);
}

void FunctionCompiler::emitConvertOpcode(const ScriptType& target) {
    OpCode opcode = OpCode::Nop;
    switch (target.primitive) {
        case Primitive::Int8: opcode = OpCode::ConvI8; break;
        case Primitive::Int16: opcode = OpCode::ConvI16; break;
        case Primitive::Int32: opcode = OpCode::ConvI32; break;
        case Primitive::Int64: opcode = OpCode::ConvI64; break;
        case Primitive::UInt8: opcode = OpCode::ConvU8; break;
        case Primitive::UInt16: opcode = OpCode::ConvU16; break;
        case Primitive::UInt32: opcode = OpCode::ConvU32; break;
        case Primitive::UInt64: opcode = OpCode::ConvU64; break;
        case Primitive::Float32: opcode = OpCode::ConvF32; break;
        case Primitive::Float64: opcode = OpCode::ConvF64; break;
        case Primitive::Char: opcode = OpCode::ConvChar; break;
        case Primitive::Bool: opcode = OpCode::ConvBool; break;
        default: break;
    }
    if (opcode != OpCode::Nop) emit(opcode, 0, 0);
}

ScriptType FunctionCompiler::promoteNumeric(const ScriptType& left, const ScriptType& right,
                                             const Node& node) {
    if ((!left.isNumeric() && !left.isChar()) || (!right.isNumeric() && !right.isChar())) {
        modules_.error("E0383",
                       "arithmetic requires numeric operands, got '" + left.name + "' and '" + right.name + "'",
                       node);
        return primI32();
    }
    if (left.primitive == Primitive::Float64 || right.primitive == Primitive::Float64) return primF64();
    if (left.primitive == Primitive::Float32 || right.primitive == Primitive::Float32) return primF32();
    if (left.primitive == Primitive::Int64 || left.primitive == Primitive::UInt64 ||
        right.primitive == Primitive::Int64 || right.primitive == Primitive::UInt64 ||
        left.primitive == Primitive::UInt32 || right.primitive == Primitive::UInt32) {
        return primI64();
    }
    return primI32();
}

void FunctionCompiler::emitNumericOp(const std::string& op, const ScriptType& type) {
    if (type.isString() && op == "+") {
        emit(OpCode::AddStr, 0, -1);
        return;
    }

    bool is64 = type.primitive == Primitive::Int64 || type.primitive == Primitive::UInt64;
    bool isF32 = type.primitive == Primitive::Float32;
    bool isF64 = type.primitive == Primitive::Float64;

    OpCode opcode = OpCode::Nop;
    if (op == "+") opcode = isF64 ? OpCode::AddF64 : isF32 ? OpCode::AddF32 : is64 ? OpCode::AddI64 : OpCode::AddI32;
    else if (op == "-") opcode = isF64 ? OpCode::SubF64 : isF32 ? OpCode::SubF32 : is64 ? OpCode::SubI64 : OpCode::SubI32;
    else if (op == "*") opcode = isF64 ? OpCode::MulF64 : isF32 ? OpCode::MulF32 : is64 ? OpCode::MulI64 : OpCode::MulI32;
    else if (op == "/") opcode = isF64 ? OpCode::DivF64 : isF32 ? OpCode::DivF32 : is64 ? OpCode::DivI64 : OpCode::DivI32;
    else if (op == "%") opcode = isF64 ? OpCode::ModF64 : isF32 ? OpCode::ModF32 : is64 ? OpCode::ModI64 : OpCode::ModI32;
    else if (op == "&") opcode = is64 ? OpCode::BAndI64 : OpCode::BAndI32;
    else if (op == "|") opcode = is64 ? OpCode::BOrI64 : OpCode::BOrI32;
    else if (op == "^") opcode = is64 ? OpCode::BXorI64 : OpCode::BXorI32;
    else if (op == "<<") opcode = is64 ? OpCode::BShlI64 : OpCode::BShlI32;
    else if (op == ">>") opcode = is64 ? OpCode::BShrI64 : OpCode::BShrI32;

    if (opcode == OpCode::Nop) {
        modules_.error("E0384", "unsupported operator '" + op + "'", Node{});
        return;
    }
    emit(opcode, 0, -1);
}

void FunctionCompiler::emitNegate(const ScriptType& type, const Node& node) {
    if (!type.isNumeric() && !type.isChar()) {
        modules_.error("E0385", "unary '-' requires a numeric operand", node);
        return;
    }
    if (type.primitive == Primitive::Float64) emit(OpCode::NegF64, 0, 0);
    else if (type.primitive == Primitive::Float32) emit(OpCode::NegF32, 0, 0);
    else if (type.primitive == Primitive::Int64 || type.primitive == Primitive::UInt64 ||
             type.primitive == Primitive::UInt32)
        emit(OpCode::NegI64, 0, 0);
    else
        emit(OpCode::NegI32, 0, 0);
}

bool FunctionCompiler::isStringConcat(const BinaryExpression& expression) {
    auto left = inferType(*expression.left);
    auto right = inferType(*expression.right);
    return (left.has_value() && left->isString()) || (right.has_value() && right->isString());
}

bool FunctionCompiler::lookupGlobal(const std::string& rawName, int& index) const {
    return modules_.findGlobalInScope(rawName, namespaceScope(), index);
}

bool FunctionCompiler::lookupGlobal(const std::string& rawName, int& index, ScriptType& type) const {
    if (!lookupGlobal(rawName, index)) return false;
    type = modules_.module().globals[index].type;
    return true;
}

bool FunctionCompiler::lookupType(const std::string& rawName, ScriptType& type) const {
    return modules_.resolveTypeNameInScope(rawName, namespaceScope(), type);
}

ScriptClassType* FunctionCompiler::lookupClass(const std::string& rawName) const {
    ScriptType type;
    if (!lookupType(rawName, type)) return nullptr;
    return dynamic_cast<ScriptClassType*>(type.object);
}

bool FunctionCompiler::flattenNameChain(const Expression& expression, std::string& name) {
    if (auto* identifier = dynamic_cast<const IdentifierExpression*>(&expression)) {
        name = identifier->name;
        return true;
    }
    if (auto* member = dynamic_cast<const MemberExpression*>(&expression)) {
        std::string target;
        if (!flattenNameChain(*member->target, target)) return false;
        name = target + "." + member->memberName;
        return true;
    }
    return false;
}

bool FunctionCompiler::firstSegmentIsValue(const std::string& rawName) const {
    auto dot = rawName.find('.');
    const std::string first = dot == std::string::npos ? rawName : rawName.substr(0, dot);
    if (findLocal(first) != nullptr) return true;
    int index = 0;
    if (lookupGlobal(first, index)) return true;
    auto* owner = dynamic_cast<ScriptClassType*>(function_->owner);
    if (owner != nullptr && modules_.module().globalsByName.count(owner->name + "." + first) > 0) return true;
    return findInstanceField(first) != nullptr;
}

bool FunctionCompiler::resolveStaticGlobal(const Expression& target, int& index, ScriptType& type) const {
    std::string chain;
    if (!flattenNameChain(target, chain)) return false;
    if (firstSegmentIsValue(chain)) return false;
    if (lookupGlobal(chain, index, type)) return true;

    auto dot = chain.rfind('.');
    if (dot == std::string::npos) return false;
    ScriptType ownerType;
    if (!lookupType(chain.substr(0, dot), ownerType)) return false;
    auto* cls = dynamic_cast<ScriptClassType*>(ownerType.object);
    if (cls == nullptr) return false;
    auto it = modules_.module().globalsByName.find(cls->name + "." + chain.substr(dot + 1));
    if (it == modules_.module().globalsByName.end()) return false;
    index = it->second;
    type = modules_.module().globals[index].type;
    return true;
}

bool FunctionCompiler::isTypeReference(const std::string& name) {
    int index = 0;
    if (findLocal(name) != nullptr || lookupGlobal(name, index)) return false;
    ScriptType type;
    return lookupType(name, type);
}

std::vector<ScriptFunction*> FunctionCompiler::moduleFunctions(const std::string& name,
                                                               std::size_t arity) const {
    return modules_.findFunctionsInScope(name, namespaceScope(), arity);
}

std::vector<ScriptFunction*> FunctionCompiler::instanceMethods(ScriptClassType* cls,
                                                               const std::string& name,
                                                               std::size_t arity) const {
    return cls == nullptr ? std::vector<ScriptFunction*>{} : cls->findMethods(name, arity, false);
}

std::vector<ScriptFunction*> FunctionCompiler::staticMethods(ScriptClassType* cls, const std::string& name,
                                                             std::size_t arity) const {
    return cls == nullptr ? std::vector<ScriptFunction*>{} : cls->findMethods(name, arity, true);
}

FieldInfo* FunctionCompiler::findInstanceField(const std::string& name) const {
    if (function_->owner == nullptr || function_->isStatic) return nullptr;
    auto* cls = dynamic_cast<ScriptClassType*>(function_->owner);
    return cls != nullptr ? cls->findField(name) : nullptr;
}

// ------------------------------------------------------------- type inference

std::optional<ScriptType> FunctionCompiler::inferType(const Expression& expression) {
    if (auto* literal = dynamic_cast<const LiteralExpression*>(&expression)) {
        switch (literal->kind) {
            case LiteralKind::Bool: return primBool();
            case LiteralKind::Char: return primChar();
            case LiteralKind::Float:
                return literal->suffix == NumericSuffix::Float ? primF32() : primF64();
            case LiteralKind::Int:
                switch (literal->suffix) {
                    case NumericSuffix::Unsigned: return primU32();
                    case NumericSuffix::Long: return primI64();
                    case NumericSuffix::UnsignedLong: return primU64();
                    default:
                        return literal->intValue >= INT32_MIN && literal->intValue <= INT32_MAX ? primI32()
                                                                                                : primI64();
                }
            case LiteralKind::Null: return primNull();
            default: return primString();
        }
    }
    if (auto* identifier = dynamic_cast<const IdentifierExpression*>(&expression)) {
        if (auto* local = findLocal(identifier->name)) return local->type;
        auto& module = modules_.module();
        int index = 0;
        if (lookupGlobal(identifier->name, index)) return module.globals[index].type;
        auto* owner = dynamic_cast<ScriptClassType*>(function_->owner);
        if (owner != nullptr) {
            auto staticIt = module.globalsByName.find(owner->name + "." + identifier->name);
            if (staticIt != module.globalsByName.end()) return module.globals[staticIt->second].type;
        }
        if (auto* field = findInstanceField(identifier->name)) return field->type;
        return std::nullopt;
    }
    if (dynamic_cast<const ThisExpression*>(&expression) != nullptr) {
        if (function_->owner == nullptr || function_->isStatic) return std::nullopt;
        return ScriptType(function_->owner->name, Primitive::None, function_->owner);
    }
    if (auto* reference = dynamic_cast<const RefArgumentExpression*>(&expression)) {
        return inferType(*reference->operand);
    }
    if (auto* outDeclaration = dynamic_cast<const OutDeclarationExpression*>(&expression)) {
        if (!outDeclaration->type.has_value()) return std::nullopt;
        ScriptType type;
        if (modules_.resolveTypeRef(*outDeclaration->type, *outDeclaration, namespaceScope(), type)) {
            return type;
        }
        return std::nullopt;
    }
    if (auto* cast = dynamic_cast<const CastExpression*>(&expression)) {
        ScriptType type;
        if (modules_.resolveTypeRef(cast->targetType, *cast, namespaceScope(), type)) return type;
        return std::nullopt;
    }
    if (auto* asExpr = dynamic_cast<const AsExpression*>(&expression)) {
        ScriptType type;
        if (modules_.resolveTypeRef(asExpr->targetType, *asExpr, namespaceScope(), type)) return type;
        return std::nullopt;
    }
    if (auto* newObject = dynamic_cast<const NewObjectExpression*>(&expression)) {
        ScriptType type;
        if (modules_.resolveTypeRef(newObject->type, *newObject, namespaceScope(), type)) return type;
        return std::nullopt;
    }
    if (auto* newArray = dynamic_cast<const NewArrayExpression*>(&expression)) {
        ScriptType element;
        if (modules_.resolveTypeRef(newArray->elementType, *newArray, namespaceScope(), element)) {
            return modules_.engine().getArrayType(element);
        }
        return std::nullopt;
    }
    if (auto* member = dynamic_cast<const MemberExpression*>(&expression)) {
        std::string chain;
        if (flattenNameChain(*member, chain) && !firstSegmentIsValue(chain)) {
            auto dot = chain.rfind('.');
            if (dot != std::string::npos) {
                const std::string prefix = chain.substr(0, dot);
                if (prefix == "string" && member->memberName == "Empty") return primString();
                ScriptType type;
                if (lookupType(prefix, type)) {
                    if (auto* cls = dynamic_cast<ScriptClassType*>(type.object)) {
                        auto it = modules_.module().globalsByName.find(cls->name + "." + member->memberName);
                        if (it != modules_.module().globalsByName.end())
                            return modules_.module().globals[it->second].type;
                    }
                    if (auto* native = dynamic_cast<NativeObjectType*>(type.object)) {
                        if (auto* property = native->findProperty(member->memberName))
                            return property->type;
                    }
                }
            }
            int index = 0;
            ScriptType globalType;
            if (lookupGlobal(chain, index, globalType)) return globalType;
        }
        auto targetType = inferType(*member->target);
        if (!targetType.has_value()) return std::nullopt;
        if (auto* cls = dynamic_cast<ScriptClassType*>(targetType->object)) {
            auto* field = cls->findField(member->memberName);
            return field != nullptr ? std::optional<ScriptType>(field->type) : std::nullopt;
        }
        if (auto* native = dynamic_cast<NativeObjectType*>(targetType->object)) {
            if (auto* property = native->findProperty(member->memberName)) return property->type;
        }
        if (targetType->isString() && (member->memberName == "length" || member->memberName == "Length"))
            return primI32();
        if (targetType->isArray() && (member->memberName == "length" || member->memberName == "Length" ||
                                      member->memberName == "count" || member->memberName == "Count"))
            return primI32();
        return std::nullopt;
    }
    if (auto* index = dynamic_cast<const IndexExpression*>(&expression)) {
        auto targetType = inferType(*index->target);
        if (!targetType.has_value()) return std::nullopt;
        if (targetType->isArray()) return *targetType->element;
        if (targetType->isString()) return primChar();
        return std::nullopt;
    }
    if (auto* unary = dynamic_cast<const UnaryExpression*>(&expression)) {
        if (unary->op == "!") return primBool();
        if (unary->op == "~") return primI32();
        return inferType(*unary->operand);
    }
    if (auto* binary = dynamic_cast<const BinaryExpression*>(&expression)) {
        if (isComparisonOp(binary->op) || binary->op == "&&" || binary->op == "||") return primBool();
        auto left = inferType(*binary->left);
        auto right = inferType(*binary->right);
        if (binary->op == "+" && ((left.has_value() && left->isString()) ||
                                  (right.has_value() && right->isString()))) {
            return primString();
        }
        if (left.has_value() && right.has_value() && left->isNumeric() && right->isNumeric()) {
            return promoteNumeric(*left, *right, *binary);
        }
        return left;
    }
    if (auto* conditional = dynamic_cast<const ConditionalExpression*>(&expression)) {
        auto whenTrue = inferType(*conditional->whenTrue);
        if (whenTrue.has_value()) return whenTrue;
        return inferType(*conditional->whenFalse);
    }
    if (auto* assignment = dynamic_cast<const AssignmentExpression*>(&expression)) {
        return resolveLValueType(*assignment->target);
    }
    if (dynamic_cast<const TypeTestExpression*>(&expression) != nullptr) return primBool();
    if (auto* call = dynamic_cast<const CallExpression*>(&expression)) return inferCallType(*call);
    return std::nullopt;
}

std::optional<ScriptType> FunctionCompiler::inferCallType(const CallExpression& call) {
    auto callableType = inferType(*call.callee);
    if (callableType && callableType->isDelegate()) {
        return static_cast<ScriptDelegateType*>(callableType->object)->returnType;
    }
    if (auto* member = dynamic_cast<const MemberExpression*>(call.callee.get())) {
        if (member->memberName == "Invoke") {
            auto targetType = inferType(*member->target);
            if (targetType && targetType->isDelegate()) {
                return static_cast<ScriptDelegateType*>(targetType->object)->returnType;
            }
        }
    }
    if (auto* identifier = dynamic_cast<const IdentifierExpression*>(call.callee.get())) {
        auto* owner = dynamic_cast<ScriptClassType*>(function_->owner);
        if (owner != nullptr && !function_->isStatic) {
            auto methods = instanceMethods(owner, identifier->name, call.arguments.size());
            if (!methods.empty()) return methods[0]->returnType;
        }
        auto candidates = moduleFunctions(identifier->name, call.arguments.size());
        if (!candidates.empty()) return candidates[0]->returnType;
        const auto* globals = modules_.engine().findGlobalFunctions(identifier->name);
        if (globals != nullptr) {
            for (auto* native : *globals) {
                if (native->matchesArity(call.arguments.size())) return native->returnType;
            }
        }
        return std::nullopt;
    }
    if (auto* member = dynamic_cast<const MemberExpression*>(call.callee.get())) {
        std::string chain;
        if (flattenNameChain(*member, chain) && !firstSegmentIsValue(chain)) {
            auto dot = chain.rfind('.');
            if (dot != std::string::npos) {
                const std::string prefix = chain.substr(0, dot);
                if (prefix == "string" && member->memberName == "IsNullOrEmpty") return primBool();
                ScriptType type;
                if (lookupType(prefix, type)) {
                    if (auto* cls = dynamic_cast<ScriptClassType*>(type.object)) {
                        auto methods = staticMethods(cls, member->memberName, call.arguments.size());
                        if (!methods.empty()) return methods[0]->returnType;
                    }
                    if (auto* native = dynamic_cast<NativeObjectType*>(type.object)) {
                        auto* method = native->findMethod(member->memberName, call.arguments.size());
                        if (method != nullptr) return method->returnType;
                    }
                    return std::nullopt;
                }
            }
            auto candidates = moduleFunctions(chain, call.arguments.size());
            if (!candidates.empty()) return candidates[0]->returnType;
        }
        auto targetType = inferType(*member->target);
        if (!targetType.has_value()) return std::nullopt;
        if (auto* cls = dynamic_cast<ScriptClassType*>(targetType->object)) {
            auto methods = instanceMethods(cls, member->memberName, call.arguments.size());
            if (!methods.empty()) return methods[0]->returnType;
        }
        if (auto* native = dynamic_cast<NativeObjectType*>(targetType->object)) {
            auto* method = native->findMethod(member->memberName, call.arguments.size());
            if (method != nullptr) return method->returnType;
        }
        if (targetType->isString()) {
            auto* methods = findStringMethods(member->memberName);
            if (methods != nullptr) {
                for (auto* method : *methods) {
                    if (method->matchesArity(call.arguments.size())) return method->returnType;
                }
            }
        }
        if (targetType->isArray()) {
            if (member->memberName == "pop") return *targetType->element;
            if (member->memberName == "indexOf") return primI32();
            if (member->memberName == "contains") return primBool();
            if (member->memberName == "push" || member->memberName == "add" ||
                member->memberName == "clear" || member->memberName == "insert" ||
                member->memberName == "removeAt")
                return primVoid();
        }
        if (member->memberName == "ToString" && call.arguments.empty()) return primString();
    }
    return std::nullopt;
}

bool FunctionCompiler::definitelyTerminates(const Statement& statement) const {
    if (dynamic_cast<const ReturnStatement*>(&statement) != nullptr) return true;
    if (dynamic_cast<const BreakStatement*>(&statement) != nullptr) return true;
    if (dynamic_cast<const ContinueStatement*>(&statement) != nullptr) return true;
    if (dynamic_cast<const ThrowStatement*>(&statement) != nullptr) return true;
    if (auto* tryStatement = dynamic_cast<const TryStatement*>(&statement)) {
        if (!definitelyTerminates(*tryStatement->body)) return false;
        for (const auto& clause : tryStatement->catches) {
            if (!definitelyTerminates(*clause->body)) return false;
        }
        return true;
    }
    if (auto* block = dynamic_cast<const BlockStatement*>(&statement)) {
        for (const auto& child : block->statements) {
            if (definitelyTerminates(*child)) return true;
        }
        return false;
    }
    if (auto* ifStatement = dynamic_cast<const IfStatement*>(&statement)) {
        return ifStatement->elseBranch != nullptr && definitelyTerminates(*ifStatement->thenBranch) &&
               definitelyTerminates(*ifStatement->elseBranch);
    }
    if (auto* switchStatement = dynamic_cast<const SwitchStatement*>(&statement)) {
        bool hasDefault = false;
        for (const auto& switchCase : switchStatement->cases) {
            if (!switchCase.label) hasDefault = true;
            bool terminates = false;
            for (const auto& child : switchCase.body) {
                if (definitelyTerminates(*child)) {
                    terminates = true;
                    break;
                }
            }
            if (!terminates) return false;
        }
        return hasDefault;
    }
    return false;
}

}  // namespace sharpscript
