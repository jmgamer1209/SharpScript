#pragma once

#include "ast.hpp"

namespace sharpscript {

class Parser {
public:
    static std::unique_ptr<CompilationUnit> parse(const std::string& source, DiagnosticBag& diagnostics);

private:
    Parser(std::vector<Token> tokens, DiagnosticBag& diagnostics)
        : tokens_(std::move(tokens)), diagnostics_(diagnostics) {}

    struct ParseAbort {};

    std::unique_ptr<CompilationUnit> parseCompilationUnit();
    void parseDeclarationList(CompilationUnit& unit, bool topLevel);
    void parseNamespace(CompilationUnit& unit, bool topLevel);
    void parseUsing(CompilationUnit& unit);
    std::unique_ptr<Declaration> parseTopLevelDeclaration();
    std::unique_ptr<Declaration> parseDelegateDeclaration();
    std::unique_ptr<ClassDeclaration> parseClass(bool isStruct);
    std::unique_ptr<Declaration> parseClassMember(const std::string& className, bool inStruct,
                                                  std::vector<std::unique_ptr<Declaration>>* extra = nullptr);
    /// Parses a property and appends the lowered field/accessors to `extra`.
    std::unique_ptr<Declaration> parsePropertyMember(const TypeRef& type, const std::string& name,
                                                     bool isStatic, bool inStruct, const Token& nameToken,
                                                     std::vector<std::unique_ptr<Declaration>>* extra);
    std::vector<ParameterDeclaration> parseParameters();
    TypeRef parseType();
    /// `<T, in U, out V>`; returns an empty vector when no `<` is present.
    std::vector<TypeParameter> parseTypeParameters();
    /// Zero or more `where T : class, new()` clauses, attached to `parameters`.
    void parseConstraints(std::vector<TypeParameter>& parameters);
    /// True when the `<` at the current position opens a call-site type
    /// argument list (`Max<int>(...)`) rather than a comparison.
    bool looksLikeGenericArguments() const;
    std::vector<TypeRef> parseTypeArgumentList();
    bool looksLikeLambda() const;
    ExprPtr parseLambdaExpression();
    std::string parseDottedName();
    std::string currentNamespace() const;
    std::size_t skipQualifiedName(std::size_t index) const;

    std::unique_ptr<BlockStatement> parseBlock();
    StmtPtr parseStatement();
    StmtPtr parseTry(const Token& keyword);
    StmtPtr parseThrow(const Token& keyword);
    std::unique_ptr<LocalVariableDeclaration> parseLocalDeclaration(bool expectSemicolon);
    ExprPtr parseArgument();
    ExprPtr parseExpression();
    ExprPtr parseAssignment();
    ExprPtr parseConditional();
    ExprPtr parseLogicalOr();
    ExprPtr parseLogicalAnd();
    ExprPtr parseBitwiseOr();
    ExprPtr parseBitwiseXor();
    ExprPtr parseBitwiseAnd();
    ExprPtr parseEquality();
    ExprPtr parseRelational();
    ExprPtr parseShift();
    ExprPtr parseAdditive();
    ExprPtr parseMultiplicative();
    ExprPtr parseUnary();
    ExprPtr parsePostfix();
    ExprPtr parsePrimary();
    ExprPtr parseNew();

    const Token& current() const { return tokens_[std::min(position_, tokens_.size() - 1)]; }
    const Token& peek(int offset) const {
        std::size_t index = position_ + static_cast<std::size_t>(offset);
        return index < tokens_.size() ? tokens_[index] : tokens_.back();
    }
    bool atEnd() const { return current().type == TokenType::EndOfFile; }
    Token advance();
    bool match(TokenType type);
    bool matchWord(const std::string& word);
    Token expect(TokenType type, const std::string& expectation);
    Token expectWord(const std::string& word);
    std::string expectIdentifier(const std::string& what = "identifier");
    void report(const std::string& message, const Token& token, const std::string& code = "E0100");
    void synchronize();
    void synchronizeInBlock();
    /// Runs a parenthesized header (`if (...)`, `for (...;...;...)`, ...) and, on
    /// a syntax error, skips to the end of the header plus an optional block body
    /// so one broken header yields one diagnostic.
    bool parseHeaderSafely(const std::function<void()>& parse);
    void skipHeaderRemainder();
    bool isTypeStart() const;
    bool isLocalDeclarationStart() const;
    bool isCastStart() const;

    std::vector<Token> tokens_;
    DiagnosticBag& diagnostics_;
    std::size_t position_ = 0;
    /// Namespace blocks currently open, e.g. {"Game", "AI"}.
    std::vector<std::string> namespaceStack_;
    /// C# 10 file-scoped namespace: `namespace Game.AI;`
    std::string fileScopedNamespace_;
};

}  // namespace sharpscript
