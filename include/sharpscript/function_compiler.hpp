#pragma once

#include "builder.hpp"
#include "compiler.hpp"

namespace sharpscript {

/// Compiles a single function body from AST to bytecode.
class FunctionCompiler {
public:
    using CaptureBinding = std::pair<std::string, ScriptType>;
    FunctionCompiler(ModuleCompiler& moduleCompiler, ScriptFunction* function,
                     const FunctionDeclaration* declaration,
                     const std::vector<CaptureBinding>* captures = nullptr);

    void compile();
    void compileSynthetic(const std::function<void(FunctionCompiler&)>& body);
    void emitConstructorPrologue();
    /// Overrides the namespace used for name lookup. Needed for the synthetic
    /// global initializer, which compiles expressions from several namespaces.
    void setNamespaceScope(std::string scope);

    ScriptType compileExpression(const Expression& expression, const ScriptType* target = nullptr);
    void emitDefault(const ScriptType& type);
    void emit(OpCode op, int arg = 0, int stackDelta = 0);
    void setLine(int line) { builder_.setLine(line); }

private:
    struct LocalVariable {
        std::string name;
        ScriptType type;
        int slot = 0;
        bool isConst = false;
        /// The slot holds a `ScriptCell` (the function contains lambdas, so its
        /// locals are heap cells and can be captured by reference).
        bool isCell = false;
        /// The slot holds a `ScriptRef` (a `ref`/`out`/`in` parameter).
        bool isRef = false;
        /// `in` parameter: assignments are rejected at compile time.
        bool isReadOnlyRef = false;
    };

    struct LoopContext {
        Label breakLabel;
        Label continueLabel;
        bool hasContinue = false;
        /// `finally` nesting at the point the loop/switch was created.
        int finallyDepth = 0;
    };

    struct TempLocal {
        int slot = -1;
    };

    void setupParameters();
    void finish();
    void pushScope();
    void popScope();
    void declareLocal(const std::string& name, const ScriptType& type, int slot, bool isConst,
                      bool isCell = false);
    /// Wraps every parameter in a cell when the function contains lambdas.
    void emitCellPrologue();
    /// Emits the epilogues of `return` statements that must leave protected
    /// regions (their value is saved in a slot, then loaded after the finallys).
    void emitReturnEpilogues();
    const LocalVariable* findLocal(const std::string& name) const;

    void compileStatement(const Statement& statement);
    void compileBlock(const BlockStatement& block);
    void compileLocalDeclaration(const LocalVariableDeclaration& declaration);
    void compileIf(const IfStatement& statement);
    void compileWhile(const WhileStatement& statement);
    void compileDoWhile(const DoWhileStatement& statement);
    void compileFor(const ForStatement& statement);
    void compileForeach(const ForeachStatement& statement);
    void compileReturn(const ReturnStatement& statement);
    void compileSwitch(const SwitchStatement& statement);
    void compileTry(const TryStatement& statement);
    void compileThrow(const ThrowStatement& statement);
    bool isExceptionType(const ScriptType& type) const;

    ScriptType compileExpressionCore(const Expression& expression);
    ScriptType compileLiteral(const LiteralExpression& literal);
    ScriptType compileIdentifier(const IdentifierExpression& identifier);
    ScriptType compileThis(const ThisExpression& expression);
    ScriptType compileMember(const MemberExpression& member);
    ScriptType compileStaticMember(const MemberExpression& member, const std::string& typeName,
                                   const ScriptType& type);
    ScriptType emitMemberLoad(const MemberExpression& member, const ScriptType& targetType);
    ScriptType compileIndex(const IndexExpression& expression);
    ScriptType compileCall(const CallExpression& call);
    ScriptType compileNamedCall(const IdentifierExpression& callee, const CallExpression& call);
    ScriptType compileMemberCall(const MemberExpression& member, const CallExpression& call);
    ScriptType compileStaticMemberCall(const MemberExpression& member, const std::string& typeName,
                                       const ScriptType& type, const CallExpression& call);
    ScriptType compileResolvedCall(const std::vector<ScriptFunction*>& candidates,
                                   const CallExpression& call, const std::string& displayName);
    /// Routes a call to a monomorphized generic method. Returns nullopt when no
    /// generic template matches the name and arity (the caller then reports the
    /// ordinary "unknown function" diagnostic).
    std::optional<ScriptType> compileGenericCall(const std::string& name, ScriptClassType* owner,
                                                 bool wantStatic, const CallExpression& call,
                                                 bool pushThis);
    /// True when at least one candidate accepts the arguments. Unknown argument
    /// types are treated as compatible, so this never rejects a valid call.
    bool overloadAccepts(const std::vector<ScriptFunction*>& candidates,
                         const std::vector<ExprPtr>& arguments);
    /// Converts `obj.Method` / `Type.Method` into a delegate when the expected
    /// type is a delegate. Returns false when the member is not a method group.
    bool tryCompileMethodGroup(const MemberExpression& member, const ScriptType& expected);
    /// Converts a bare function name (`Op f = Handler;`) into a delegate.
    bool tryCompileFunctionGroup(const IdentifierExpression& identifier, const ScriptType& expected);
    /// `d(args)` / `d.Invoke(args)`.
    ScriptType compileDelegateCall(const Expression& callee, const CallExpression& call);
    /// Compiles a lambda and creates a delegate from it.
    ScriptType compileLambdaExpression(const LambdaExpression& lambda, const ScriptType& expected);
    ScriptType compileStringMethod(const MemberExpression& member, const CallExpression& call);
    ScriptType compileArrayMethod(const MemberExpression& member, const CallExpression& call,
                                  const ScriptType& arrayType);
    ScriptType compileNewObject(const NewObjectExpression& expression);
    ScriptType compileNewArray(const NewArrayExpression& expression);
    ScriptType compileCast(const CastExpression& expression);
    ScriptType compileAs(const AsExpression& expression);
    ScriptType compileTypeTest(const TypeTestExpression& expression);
    ScriptType compileUnary(const UnaryExpression& expression);
    ScriptType compileIncrement(const UnaryExpression& expression, bool increment, bool prefix);
    ScriptType compileBinary(const BinaryExpression& expression);
    ScriptType compileLogicalAnd(const BinaryExpression& expression);
    ScriptType compileLogicalOr(const BinaryExpression& expression);
    ScriptType compileComparison(const BinaryExpression& expression);
    ScriptType compileAssignment(const AssignmentExpression& expression);
    ScriptType compileConditional(const ConditionalExpression& expression);

    ScriptType resolveLValueType(const Expression& target);
    void compileLValueAddress(const Expression& target);
    void emitLValueLoad(const Expression& target, const ScriptType& type);
    void emitLValueStore(const Expression& target, const ScriptType& type);

    int allocateSlot(const ScriptType& type);
    int tempSlot(const ScriptType& type);
    void pushConstant(const Value& value);
    void emitJump(OpCode op, const Label& label, int stackDelta);
    void emitLoadField(FieldInfo* field);
    void emitStoreField(FieldInfo* field);
    void compileArguments(const std::vector<ExprPtr>& arguments,
                          const std::vector<ScriptType>& parameterTypes, const Node& node);
    /// Argument compilation for script functions, honouring ref/out/in.
    void compileCallArguments(const std::vector<ExprPtr>& arguments,
                              const std::vector<ScriptParameter>& parameters, const Node& node);
    /// Pushes a `ScriptRef` for an assignable expression (or a temp for rvalues).
    void compileRefArgument(const Expression& target, bool allowRvalueTemp, const Node& node);
    FieldInfo* resolveField(const MemberExpression& member);
    void emitScriptCall(ScriptFunction* target);
    void emitVirtualCall(ScriptFunction* target);
    void emitNativeCall(NativeFunction* target);

    int selectOverload(const std::vector<std::vector<ScriptType>>& parameterLists,
                       const std::vector<bool>& variadic, const std::vector<ExprPtr>& arguments,
                       const Node& node);
    bool canImplicitlyConvert(const ScriptType& from, const ScriptType& to) const;
    void emitConversion(const ScriptType& from, const ScriptType& to, const Node& node,
                        bool explicitCast = false);
    void emitExplicitConversion(const ScriptType& from, const ScriptType& to, const Node& node);
    void emitConvertOpcode(const ScriptType& target);
    ScriptType promoteNumeric(const ScriptType& left, const ScriptType& right, const Node& node);
    void emitNumericOp(const std::string& op, const ScriptType& type);
    void emitNegate(const ScriptType& type, const Node& node);
    bool isStringConcat(const BinaryExpression& expression);
    bool isTypeReference(const std::string& name);

    // -------------------------------------------------- namespace aware lookup
    const std::string& namespaceScope() const;
    bool lookupGlobal(const std::string& rawName, int& index, ScriptType& type) const;
    bool lookupGlobal(const std::string& rawName, int& index) const;
    bool lookupType(const std::string& rawName, ScriptType& type) const;
    ScriptClassType* lookupClass(const std::string& rawName) const;
    /// Recognizes `Type.StaticField` and `Namespace.Global` lvalues.
    bool resolveStaticGlobal(const Expression& target, int& index, ScriptType& type) const;
    /// True when `firstSegment` refers to a local/global/field, which must take
    /// precedence over a namespace or type with the same name (C# rules).
    bool firstSegmentIsValue(const std::string& rawName) const;
    static bool flattenNameChain(const Expression& expression, std::string& name);

    std::vector<NativeFunction*>* findStringMethods(const std::string& name);
    std::vector<ScriptFunction*> moduleFunctions(const std::string& name, std::size_t arity) const;
    std::vector<ScriptFunction*> instanceMethods(ScriptClassType* cls, const std::string& name,
                                                 std::size_t arity) const;
    std::vector<ScriptFunction*> staticMethods(ScriptClassType* cls, const std::string& name,
                                               std::size_t arity) const;
    FieldInfo* findInstanceField(const std::string& name) const;
    /// Property of the enclosing class, for bare `Name` inside its methods.
    const ScriptClassType::PropertyInfo* findOwnerProperty(const std::string& name) const;

    std::optional<ScriptType> inferType(const Expression& expression);
    std::optional<ScriptType> inferCallType(const CallExpression& call);
    bool definitelyTerminates(const Statement& statement) const;

    ModuleCompiler& modules_;
    ScriptFunction* function_;
    const FunctionDeclaration* declaration_;
    ScriptBuilder builder_;

    std::vector<std::unordered_map<std::string, LocalVariable>> scopes_;
    std::vector<LoopContext> loops_;
    /// Namespace used for unqualified/relative name resolution.
    std::string namespaceScope_;
    /// Capture names/types of a lambda function (empty for ordinary functions).
    std::vector<CaptureBinding> captureBindings_;
    /// True when the body contains a lambda: all locals become heap cells.
    bool useCells_ = false;
    /// Nesting depth of `try` bodies / `catch` bodies (any protected region).
    int protectedDepth_ = 0;
    /// Nesting depth of `finally` bodies (`return`/`break`/`continue` are illegal).
    int finallyDepth_ = 0;
    struct ReturnSite {
        Label label;
        int slot = -1;
    };
    std::vector<ReturnSite> returnSites_;
    /// Hidden slots holding the exception of the catch blocks being compiled.
    std::vector<int> catchExceptionSlots_;
    int nextSlot_ = 0;
    int tempSlot_ = -1;
};

}  // namespace sharpscript
