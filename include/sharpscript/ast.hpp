#pragma once

#include <optional>

#include "tokenizer.hpp"

namespace sharpscript {

struct Node {
    int line = 1;
    int column = 1;
    virtual ~Node() = default;
};

struct TypeRef : Node {
    std::string name;
    int arrayDepth = 0;
    /// `Box<int>` -> [int]; empty for non-generic types.
    std::vector<TypeRef> typeArguments;

    TypeRef() = default;
    TypeRef(std::string name, int arrayDepth, int line, int column)
        : name(std::move(name)), arrayDepth(arrayDepth) {
        this->line = line;
        this->column = column;
    }

    std::string displayName() const {
        std::string result = name;
        if (!typeArguments.empty()) {
            result += "<";
            for (std::size_t i = 0; i < typeArguments.size(); ++i) {
                if (i > 0) result += ", ";
                result += typeArguments[i].displayName();
            }
            result += ">";
        }
        for (int i = 0; i < arrayDepth; ++i) result += "[]";
        return result;
    }
};

/// One entry of a `where T : ...` clause.
enum class ConstraintKind { Class, Struct, New, BaseType };

struct TypeConstraint : Node {
    ConstraintKind kind = ConstraintKind::BaseType;
    /// Base type of a `where T : Player` constraint; unused by the other kinds.
    TypeRef type;
};

/// A generic type parameter (`class Box<T>`, `delegate T F<out T>()`).
struct TypeParameter : Node {
    std::string name;
    Variance variance = Variance::None;
    std::vector<TypeConstraint> constraints;
};

struct Expression;
struct Statement;
struct BlockStatement;
using ExprPtr = std::unique_ptr<Expression>;
using StmtPtr = std::unique_ptr<Statement>;

enum class LiteralKind { Int, Float, String, Char, Bool, Null };

struct Expression : Node {
    ~Expression() override = default;
};

struct LiteralExpression : Expression {
    LiteralKind kind = LiteralKind::Int;
    std::int64_t intValue = 0;
    double floatValue = 0.0;
    std::u16string stringValue;
    char16_t charValue = u'\0';
    bool boolValue = false;
    NumericSuffix suffix = NumericSuffix::None;
};

struct IdentifierExpression : Expression {
    std::string name;
};

struct ThisExpression : Expression {};

struct BinaryExpression : Expression {
    std::string op;
    ExprPtr left;
    ExprPtr right;
};

struct UnaryExpression : Expression {
    std::string op;
    ExprPtr operand;
    bool isPrefix = true;
};

struct AssignmentExpression : Expression {
    std::string op;
    ExprPtr target;
    ExprPtr value;
};

struct MemberExpression : Expression {
    ExprPtr target;
    std::string memberName;
};

struct IndexExpression : Expression {
    ExprPtr target;
    ExprPtr index;
};

/// `ref expr` / `out expr` / `in expr` argument.
struct RefArgumentExpression : Expression {
    ParamModifier modifier = ParamModifier::None;
    ExprPtr operand;
};

/// `out var name` / `out Type name` argument: declares a local at the call site.
struct OutDeclarationExpression : Expression {
    std::optional<TypeRef> type;
    std::string name;
};

struct CallExpression : Expression {
    ExprPtr callee;
    std::vector<ExprPtr> arguments;
    /// Explicit type arguments of a generic call (`Max<int>(a, b)`); empty otherwise.
    std::vector<TypeRef> typeArguments;
};

struct NewObjectExpression : Expression {
    TypeRef type;
    std::vector<ExprPtr> arguments;
};

struct NewArrayExpression : Expression {
    TypeRef elementType;
    ExprPtr size;                     // optionally null
    bool hasInitializer = false;
    std::vector<ExprPtr> initializer;
};

struct CastExpression : Expression {
    TypeRef targetType;
    ExprPtr operand;
};

struct TypeTestExpression : Expression {
    bool hasTargetType = false;
    TypeRef targetType;
    ExprPtr operand;
    bool negated = false;
};

struct AsExpression : Expression {
    TypeRef targetType;
    ExprPtr operand;
};

struct ConditionalExpression : Expression {
    ExprPtr condition;
    ExprPtr whenTrue;
    ExprPtr whenFalse;
};

// --------------------------------------------------------------- statements

struct Statement : Node {
    ~Statement() override = default;
};

struct BlockStatement : Statement {
    std::vector<StmtPtr> statements;
};

struct VariableInitializer : Node {
    std::string name;
    ExprPtr value;
};

struct LocalVariableDeclaration : Statement {
    std::optional<TypeRef> type;
    bool isConst = false;
    std::vector<VariableInitializer> initializers;
};

struct ExpressionStatement : Statement {
    ExprPtr expression;
};

struct IfStatement : Statement {
    ExprPtr condition;
    StmtPtr thenBranch;
    StmtPtr elseBranch;
};

struct WhileStatement : Statement {
    ExprPtr condition;
    StmtPtr body;
};

struct DoWhileStatement : Statement {
    StmtPtr body;
    ExprPtr condition;
};

struct ForStatement : Statement {
    StmtPtr initializer;
    ExprPtr condition;
    ExprPtr iterator;
    StmtPtr body;
};

struct ForeachStatement : Statement {
    std::optional<TypeRef> elementType;
    std::string variableName;
    ExprPtr collection;
    StmtPtr body;
};

struct ReturnStatement : Statement {
    ExprPtr value;
};

struct BreakStatement : Statement {};
struct ContinueStatement : Statement {};
struct EmptyStatement : Statement {};

struct SwitchCase : Node {
    ExprPtr label;  // null => default
    std::vector<StmtPtr> body;
};

struct SwitchStatement : Statement {
    ExprPtr value;
    std::vector<SwitchCase> cases;
};

struct CatchClause : Node {
    /// Null for a catch-all clause (`catch { }`).
    std::optional<TypeRef> type;
    /// Empty when the clause does not bind a variable (`catch (Exception) { }`).
    std::string variable;
    std::unique_ptr<BlockStatement> body;
};

struct TryStatement : Statement {
    std::unique_ptr<BlockStatement> body;
    std::vector<std::unique_ptr<CatchClause>> catches;
    /// Optional `finally` block (may be present without any `catch`).
    std::unique_ptr<BlockStatement> finallyBody;
};

struct ThrowStatement : Statement {
    /// Null for `throw;` (rethrow the exception of the enclosing catch).
    ExprPtr value;
};

// -------------------------------------------------------------- declarations

struct Declaration : Node {
    std::string name;
    /// Enclosing namespace ("Game.AI" or empty for the global namespace).
    std::string namespacePath;
    ~Declaration() override = default;
};

/// `using Game.AI;` — imports the members of a namespace into `namespacePath`.
struct UsingDirective : Node {
    std::string path;
    std::string namespacePath;
};

struct ParameterDeclaration : Node {
    std::string name;
    TypeRef type;
    ParamModifier modifier = ParamModifier::None;
};

struct FunctionDeclaration : Declaration {
    std::optional<TypeRef> returnType;
    std::vector<ParameterDeclaration> parameters;
    std::unique_ptr<BlockStatement> body;
    bool isStatic = false;
    bool isConstructor = false;
    bool isAbstract = false;
    bool isOverride = false;
    /// `~Type()`: runs when the last reference to an instance goes away.
    bool isFinalizer = false;
    /// Generic method type parameters (`T Max<T>(T a, T b)`), empty otherwise.
    std::vector<TypeParameter> typeParameters;
    /// Source range of the declaration, used to re-instantiate a generic method.
    int sourceStart = -1;
    int sourceEnd = -1;
};

/// `delegate int Op(int a, int b);`
struct DelegateDeclaration : Declaration {
    TypeRef returnType;
    std::vector<ParameterDeclaration> parameters;
    /// Generic delegate type parameters (`delegate T F<out T>()`), empty otherwise.
    std::vector<TypeParameter> typeParameters;
};

struct FieldDeclaration : Declaration {
    TypeRef type;
    bool isStatic = false;
    bool isReadOnly = false;
    std::vector<VariableInitializer> initializers;
};

/// `public int Health { get; set; }` / `{ get { ... } set { ... } }`.
///
/// The parser lowers every property to ordinary members: the accessor methods
/// `get_<Name>` / `set_<Name>` (and, for auto-properties, a backing field
/// `__<Name>`), so dispatch, virtual slots, generics and hot reload all reuse
/// the existing method machinery. This node only records which functions back
/// the property and is read again when the class is laid out.
struct PropertyDeclaration : Declaration {
    TypeRef type;
    bool isStatic = false;
    bool hasGetter = false;
    bool hasSetter = false;
    /// True when at least one accessor was declared with `;`.
    bool isAuto = false;
};

struct ClassDeclaration : Declaration {
    std::optional<std::string> baseName;
    /// Interfaces after the base class (`class A : B, IFoo, IBar`).
    std::vector<std::string> interfaceNames;
    bool isAbstract = false;
    /// C# style value type: no inheritance, no virtual dispatch, copied by value.
    bool isStruct = false;
    /// `interface IFoo { ... }`: same node shape as a class, but it only holds
    /// abstract signatures and never gets fields or a constructor.
    bool isInterface = false;
    /// Generic template parameters (`class Box<T>` -> ["T"]), empty otherwise.
    std::vector<TypeParameter> typeParameters;
    /// Source range of the declaration, used to re-instantiate the template.
    int sourceStart = -1;
    int sourceEnd = -1;
    std::vector<std::unique_ptr<Declaration>> members;
};

struct GlobalVariableDeclaration : Declaration {
    TypeRef type;
    bool isConst = false;
    std::vector<VariableInitializer> initializers;
};

/// `(int x) => x + 1`, `x => { return x; }`
struct LambdaExpression : Expression {
    /// Parsed as a function declaration; parameter types may be empty and are
    /// filled from the target delegate type during compilation.
    std::unique_ptr<FunctionDeclaration> declaration;
    /// True for `x => expr`; false for `x => { ... }`.
    bool expressionBody = false;
};

struct CompilationUnit {
    /// Namespace blocks are flattened by the parser; every declaration carries
    /// its namespace in Declaration::namespacePath.
    std::vector<std::unique_ptr<Declaration>> declarations;
    std::vector<UsingDirective> usings;
};

}  // namespace sharpscript
