#include "sharpscript/parser.hpp"

namespace sharpscript {
namespace {

const std::unordered_map<std::string, bool>& primitiveNames() {
    static const std::unordered_map<std::string, bool> names = {
        {"void", true},   {"bool", true},   {"byte", true},   {"sbyte", true},  {"short", true},
        {"ushort", true}, {"int", true},    {"uint", true},   {"long", true},   {"ulong", true},
        {"float", true},  {"double", true}, {"char", true},   {"string", true}, {"var", true},
        {"object", true}};
    return names;
}

const std::unordered_map<std::string, bool>& reservedWords() {
    static const std::unordered_map<std::string, bool> words = {
        {"if", true},       {"else", true},     {"while", true},    {"do", true},
        {"for", true},      {"foreach", true},  {"switch", true},   {"case", true},
        {"default", true},  {"break", true},    {"continue", true}, {"return", true},
        {"class", true},    {"new", true},      {"this", true},     {"base", true},
        {"null", true},     {"true", true},     {"false", true},    {"static", true},
        {"const", true},    {"readonly", true}, {"public", true},   {"private", true},
        {"protected", true},{"internal", true}, {"virtual", true},  {"override", true},
        {"abstract", true}, {"sealed", true},   {"extern", true},   {"is", true},
        {"as", true},       {"in", true},       {"void", true},     {"struct", true},
        {"try", true},      {"catch", true},    {"finally", true},  {"throw", true}};
    return words;
}

bool isModifier(const std::string& word) {
    return word == "public" || word == "private" || word == "protected" || word == "internal" ||
           word == "static" || word == "const" || word == "readonly" || word == "virtual" ||
           word == "override" || word == "abstract" || word == "sealed" || word == "extern";
}

}  // namespace

std::unique_ptr<CompilationUnit> Parser::parse(const std::string& source, DiagnosticBag& diagnostics) {
    auto tokens = Tokenizer::tokenize(source, diagnostics);
    Parser parser(std::move(tokens), diagnostics);
    return parser.parseCompilationUnit();
}

Token Parser::advance() {
    Token token = current();
    if (!atEnd()) ++position_;
    return token;
}

bool Parser::match(TokenType type) {
    if (current().type != type) return false;
    advance();
    return true;
}

bool Parser::matchWord(const std::string& word) {
    if (!current().is(word)) return false;
    advance();
    return true;
}

Token Parser::expect(TokenType type, const std::string& expectation) {
    if (current().type != type) {
        report("expected " + expectation + " but found " + current().toString(), current());
        throw ParseAbort{};
    }
    return advance();
}

Token Parser::expectWord(const std::string& word) {
    if (!current().is(word)) {
        report("expected '" + word + "' but found " + current().toString(), current());
        throw ParseAbort{};
    }
    return advance();
}

std::string Parser::expectIdentifier(const std::string& what) {
    if (current().type != TokenType::Identifier || reservedWords().count(current().text) > 0) {
        report("expected " + what + " but found " + current().toString(), current());
        throw ParseAbort{};
    }
    return advance().text;
}

void Parser::report(const std::string& message, const Token& token, const std::string& code) {
    diagnostics_.error(code, message, token.line, token.column);
}

void Parser::synchronize() {
    while (!atEnd()) {
        if (current().type == TokenType::Semicolon) {
            advance();
            return;
        }
        if (current().type == TokenType::RBrace) return;
        advance();
    }
}

void Parser::synchronizeInBlock() {
    while (!atEnd() && current().type != TokenType::RBrace) {
        if (current().type == TokenType::Semicolon) {
            advance();
            return;
        }
        advance();
    }
}

std::unique_ptr<CompilationUnit> Parser::parseCompilationUnit() {
    auto unit = std::make_unique<CompilationUnit>();
    parseDeclarationList(*unit, true);
    return unit;
}

/// Parses declarations until EOF (top level) or the closing brace of a
/// namespace block. Namespace blocks are flattened: members land directly in
/// `unit.declarations` with their qualified namespace recorded on the node.
void Parser::parseDeclarationList(CompilationUnit& unit, bool topLevel) {
    while (!atEnd() && (topLevel || current().type != TokenType::RBrace)) {
        std::size_t start = position_;
        try {
            if (current().is("namespace")) {
                parseNamespace(unit, topLevel);
            } else if (current().is("using")) {
                parseUsing(unit);
            } else {
                auto declaration = parseTopLevelDeclaration();
                if (declaration != nullptr) unit.declarations.push_back(std::move(declaration));
            }
        } catch (const ParseAbort&) {
            if (topLevel) {
                synchronize();
            } else {
                synchronizeInBlock();
            }
        }
        if (position_ == start) advance();
    }
}

void Parser::parseNamespace(CompilationUnit& unit, bool topLevel) {
    Token keyword = expectWord("namespace");
    std::string path = parseDottedName();

    // C# 10 file-scoped namespace: namespace Game.AI;  (rest of the file)
    if (match(TokenType::Semicolon)) {
        if (!topLevel) {
            report("a file-scoped namespace must be declared at the top level", keyword);
            throw ParseAbort{};
        }
        if (!fileScopedNamespace_.empty()) {
            report("only one file-scoped namespace is allowed per module", keyword);
            throw ParseAbort{};
        }
        fileScopedNamespace_ = path;
        return;
    }

    expect(TokenType::LBrace, "'{'");
    namespaceStack_.push_back(path);
    parseDeclarationList(unit, false);
    expect(TokenType::RBrace, "'}'");
    namespaceStack_.pop_back();
}

void Parser::parseUsing(CompilationUnit& unit) {
    Token keyword = expectWord("using");
    if (current().is("static")) {
        report("'using static' is not supported", current());
        throw ParseAbort{};
    }
    if (current().type == TokenType::Identifier && peek(1).type == TokenType::Assign) {
        report("using aliases are not supported", current());
        throw ParseAbort{};
    }

    UsingDirective directive;
    directive.path = parseDottedName();
    directive.namespacePath = currentNamespace();
    directive.line = keyword.line;
    directive.column = keyword.column;
    expect(TokenType::Semicolon, "';'");
    unit.usings.push_back(std::move(directive));
}

std::string Parser::parseDottedName() {
    std::string name = expectIdentifier("identifier");
    while (current().type == TokenType::Dot && peek(1).type == TokenType::Identifier) {
        advance();
        name += ".";
        name += advance().text;
    }
    return name;
}

std::string Parser::currentNamespace() const {
    std::string result = fileScopedNamespace_;
    for (const auto& part : namespaceStack_) {
        if (!result.empty()) result += ".";
        result += part;
    }
    return result;
}

std::size_t Parser::skipQualifiedName(std::size_t index) const {
    while (index < tokens_.size()) {
        if (tokens_[index].type == TokenType::Dot && index + 1 < tokens_.size() &&
            tokens_[index + 1].type == TokenType::Identifier) {
            index += 2;
            continue;
        }
        if (tokens_[index].type == TokenType::LBracket && index + 1 < tokens_.size() &&
            tokens_[index + 1].type == TokenType::RBracket) {
            index += 2;
            continue;
        }
        break;
    }
    return index;
}

std::unique_ptr<Declaration> Parser::parseTopLevelDeclaration() {
    bool isStatic = false;
    bool isConst = false;
    bool isReadOnly = false;
    bool isAbstract = false;
    bool isOverride = false;
    while (current().type == TokenType::Identifier && isModifier(current().text)) {
        const std::string modifier = advance().text;
        if (modifier == "static") isStatic = true;
        else if (modifier == "const") { isConst = true; isReadOnly = true; }
        else if (modifier == "readonly") isReadOnly = true;
        else if (modifier == "abstract") isAbstract = true;
        else if (modifier == "override") isOverride = true;
    }

    const std::string namespacePath = currentNamespace();
    if (current().is("delegate")) {
        auto declaration = parseDelegateDeclaration();
        declaration->namespacePath = namespacePath;
        return declaration;
    }

    if (current().is("class") || current().is("struct")) {
        const bool isStruct = current().is("struct");
        if (isStruct && isAbstract) report("a struct cannot be abstract", current());
        auto cls = parseClass(isStruct);
        cls->namespacePath = namespacePath;
        cls->isAbstract = isAbstract && !isStruct;
        return cls;
    }

    const Token typeStart = current();
    TypeRef type = parseType();
    Token nameToken = current();
    std::string name = expectIdentifier("declaration name");
    std::vector<TypeParameter> typeParameters = parseTypeParameters();

    if (current().type == TokenType::LParen) {
        auto function = std::make_unique<FunctionDeclaration>();
        function->name = name;
        function->returnType = type;
        function->isStatic = true;
        function->line = nameToken.line;
        function->column = nameToken.column;
        function->namespacePath = namespacePath;
        function->typeParameters = std::move(typeParameters);
        function->sourceStart = typeStart.offset;
        function->parameters = parseParameters();
        parseConstraints(function->typeParameters);
        if (match(TokenType::Semicolon)) {
            function->isAbstract = true;
            function->sourceEnd = current().offset;
            return function;
        }
        function->body = parseBlock();
        function->sourceEnd = current().offset;
        return function;
    }

    if (!typeParameters.empty()) {
        report("generic type arguments are only allowed on methods", nameToken);
    }

    auto declaration = std::make_unique<GlobalVariableDeclaration>();
    declaration->name = name;
    declaration->type = type;
    declaration->isConst = isConst || isReadOnly;
    declaration->line = nameToken.line;
    declaration->column = nameToken.column;
    declaration->namespacePath = namespacePath;

    do {
        VariableInitializer initializer;
        initializer.line = current().line;
        initializer.column = current().column;
        if (declaration->initializers.empty()) {
            initializer.name = name;
        } else {
            initializer.line = current().line;
            initializer.column = current().column;
            initializer.name = expectIdentifier("variable name");
        }
        if (match(TokenType::Assign)) initializer.value = parseExpression();
        declaration->initializers.push_back(std::move(initializer));
    } while (match(TokenType::Comma));

    expect(TokenType::Semicolon, "';'");
    (void)isStatic;
    (void)isAbstract;
    (void)isOverride;
    return declaration;
}

std::unique_ptr<ClassDeclaration> Parser::parseClass(bool isStruct) {
    Token classToken = expectWord(isStruct ? "struct" : "class");
    std::string name = expectIdentifier(isStruct ? "struct name" : "class name");
    std::vector<TypeParameter> typeParameters = parseTypeParameters();
    std::optional<std::string> baseName;
    if (match(TokenType::Colon)) {
        baseName = parseDottedName();
        if (isStruct) report("a struct cannot have a base type", classToken);
    }
    parseConstraints(typeParameters);

    expect(TokenType::LBrace, "'{'");
    auto declaration = std::make_unique<ClassDeclaration>();
    declaration->name = name;
    declaration->baseName = isStruct ? std::nullopt : baseName;
    declaration->isStruct = isStruct;
    declaration->typeParameters = std::move(typeParameters);
    declaration->sourceStart = classToken.offset;
    declaration->line = classToken.line;
    declaration->column = classToken.column;

    while (!atEnd() && current().type != TokenType::RBrace) {
        std::size_t start = position_;
        try {
            auto member = parseClassMember(name, isStruct);
            if (member != nullptr) declaration->members.push_back(std::move(member));
        } catch (const ParseAbort&) {
            synchronizeInBlock();
        }
        if (position_ == start) advance();
    }
    expect(TokenType::RBrace, "'}'");
    declaration->sourceEnd = current().offset;
    return declaration;
}

std::unique_ptr<Declaration> Parser::parseDelegateDeclaration() {
    Token keyword = advance();  // 'delegate'
    auto declaration = std::make_unique<DelegateDeclaration>();
    declaration->line = keyword.line;
    declaration->column = keyword.column;
    declaration->returnType = parseType();
    Token nameToken = current();
    declaration->name = expectIdentifier("delegate name");
    declaration->line = nameToken.line;
    declaration->column = nameToken.column;
    declaration->typeParameters = parseTypeParameters();
    declaration->parameters = parseParameters();
    parseConstraints(declaration->typeParameters);
    expect(TokenType::Semicolon, "';'");
    return declaration;
}

std::unique_ptr<Declaration> Parser::parseClassMember(const std::string& className, bool inStruct) {
    // `~Type() { ... }`: the finalizer, mirroring C#'s destructor syntax.
    if (current().type == TokenType::Tilde) {
        const Token tilde = advance();
        const Token nameToken = current();
        const std::string name = expectIdentifier("finalizer name");
        if (name != className) {
            report("finalizer must be named after its class ('~" + className + "')", nameToken);
        }

        auto method = std::make_unique<FunctionDeclaration>();
        method->name = className;
        method->isFinalizer = true;
        method->line = tilde.line;
        method->column = tilde.column;
        method->sourceStart = tilde.offset;
        if (current().type == TokenType::LParen) {
            method->parameters = parseParameters();
            if (!method->parameters.empty()) {
                report("a finalizer cannot declare parameters", nameToken);
                method->parameters.clear();
            }
        }
        method->body = parseBlock();
        method->sourceEnd = current().offset;
        return method;
    }

    bool isStatic = false;
    bool isConst = false;
    bool isReadOnly = false;
    bool isAbstract = false;
    bool isOverride = false;
    while (current().type == TokenType::Identifier && isModifier(current().text)) {
        const std::string modifier = advance().text;
        if (modifier == "static") isStatic = true;
        else if (modifier == "const") { isConst = true; isReadOnly = true; }
        else if (modifier == "readonly") isReadOnly = true;
        else if (modifier == "abstract") isAbstract = true;
        else if (modifier == "override") isOverride = true;
    }

    if (current().is("class") || current().is("struct")) {
        report("nested types are not supported", current());
        throw ParseAbort{};
    }

    if (current().is("delegate")) {
        report("delegate declarations must be at namespace scope", current());
        throw ParseAbort{};
    }

    if (inStruct && (isAbstract || isOverride)) {
        report("a struct cannot declare abstract or override members", current());
    }

    if (current().is(className) && peek(1).type == TokenType::LParen) {
        Token ctorToken = advance();
        auto constructor = std::make_unique<FunctionDeclaration>();
        constructor->name = className;
        constructor->isConstructor = true;
        constructor->line = ctorToken.line;
        constructor->column = ctorToken.column;
        constructor->parameters = parseParameters();
        constructor->body = parseBlock();
        return constructor;
    }

    const Token typeStart = current();
    TypeRef type = parseType();
    Token nameToken = current();
    std::string name = expectIdentifier("member name");
    std::vector<TypeParameter> typeParameters = parseTypeParameters();

    if (current().type == TokenType::LParen) {
        auto method = std::make_unique<FunctionDeclaration>();
        method->name = name;
        method->returnType = type;
        method->isStatic = isStatic;
        method->isAbstract = isAbstract;
        method->isOverride = isOverride;
        method->line = nameToken.line;
        method->column = nameToken.column;
        method->typeParameters = std::move(typeParameters);
        method->sourceStart = typeStart.offset;
        method->parameters = parseParameters();
        parseConstraints(method->typeParameters);
        if (match(TokenType::Semicolon)) {
            if (!isAbstract) report("method '" + name + "' must have a body", nameToken);
            method->sourceEnd = current().offset;
        } else {
            method->body = parseBlock();
            method->sourceEnd = current().offset;
        }
        return method;
    }

    if (!typeParameters.empty()) {
        report("generic type arguments are only allowed on methods", nameToken);
    }

    auto field = std::make_unique<FieldDeclaration>();
    field->name = name;
    field->type = type;
    field->isStatic = isStatic;
    field->isReadOnly = isReadOnly || isConst;
    field->line = nameToken.line;
    field->column = nameToken.column;

    do {
        VariableInitializer initializer;
        if (field->initializers.empty()) {
            initializer.name = name;
            initializer.line = nameToken.line;
            initializer.column = nameToken.column;
        } else {
            initializer.line = current().line;
            initializer.column = current().column;
            initializer.name = expectIdentifier("field name");
        }
        if (match(TokenType::Assign)) initializer.value = parseExpression();
        field->initializers.push_back(std::move(initializer));
    } while (match(TokenType::Comma));

    expect(TokenType::Semicolon, "';'");
    return field;
}

std::vector<ParameterDeclaration> Parser::parseParameters() {
    expect(TokenType::LParen, "'('");
    std::vector<ParameterDeclaration> parameters;
    if (current().type != TokenType::RParen) {
        do {
            ParameterDeclaration parameter;
            if (current().is("ref")) {
                advance();
                parameter.modifier = ParamModifier::Ref;
            } else if (current().is("out")) {
                advance();
                parameter.modifier = ParamModifier::Out;
            } else if (current().is("in")) {
                advance();
                parameter.modifier = ParamModifier::In;
            }
            parameter.type = parseType();
            Token nameToken = current();
            parameter.name = expectIdentifier("parameter name");
            parameter.line = nameToken.line;
            parameter.column = nameToken.column;
            parameters.push_back(std::move(parameter));
        } while (match(TokenType::Comma));
    }
    expect(TokenType::RParen, "')'");
    return parameters;
}

std::vector<TypeParameter> Parser::parseTypeParameters() {
    std::vector<TypeParameter> parameters;
    if (!match(TokenType::Less)) return parameters;
    do {
        TypeParameter parameter;
        parameter.line = current().line;
        parameter.column = current().column;
        if (matchWord("out")) {
            parameter.variance = Variance::Out;
        } else if (matchWord("in")) {
            parameter.variance = Variance::In;
        }
        parameter.name = expectIdentifier("type parameter name");
        parameters.push_back(std::move(parameter));
    } while (match(TokenType::Comma));
    expect(TokenType::Greater, "'>'");
    return parameters;
}

void Parser::parseConstraints(std::vector<TypeParameter>& parameters) {
    while (current().is("where")) {
        Token whereToken = advance();
        if (parameters.empty()) {
            report("a 'where' clause is only allowed on a generic declaration", whereToken);
        }
        Token nameToken = current();
        const std::string name = expectIdentifier("type parameter name");
        TypeParameter* parameter = nullptr;
        for (auto& candidate : parameters) {
            if (candidate.name == name) {
                parameter = &candidate;
                break;
            }
        }
        if (parameter == nullptr) {
            report("'where' constraint refers to unknown type parameter '" + name + "'", nameToken);
        }
        expect(TokenType::Colon, "':'");
        do {
            TypeConstraint constraint;
            constraint.line = current().line;
            constraint.column = current().column;
            if (current().is("class")) {
                advance();
                constraint.kind = ConstraintKind::Class;
            } else if (current().is("struct")) {
                advance();
                constraint.kind = ConstraintKind::Struct;
            } else if (current().is("new")) {
                advance();
                expect(TokenType::LParen, "'('");
                expect(TokenType::RParen, "')'");
                constraint.kind = ConstraintKind::New;
            } else if (isTypeStart()) {
                constraint.kind = ConstraintKind::BaseType;
                constraint.type = parseType();
            } else {
                report("expected a type constraint but found " + current().toString(), current());
                throw ParseAbort{};
            }
            if (parameter != nullptr) parameter->constraints.push_back(std::move(constraint));
        } while (match(TokenType::Comma));
    }
}

bool Parser::looksLikeGenericArguments() const {
    if (current().type != TokenType::Less) return false;
    std::size_t index = position_;
    int depth = 0;
    while (index < tokens_.size()) {
        const TokenType type = tokens_[index].type;
        if (type == TokenType::EndOfFile) return false;
        if (type == TokenType::Less) {
            ++depth;
            ++index;
            continue;
        }
        if (type == TokenType::Greater) {
            ++index;
            if (--depth == 0) {
                return index < tokens_.size() && tokens_[index].type == TokenType::LParen;
            }
            continue;
        }
        // `>>` cannot close a nested type argument list (write `> >`).
        if (type == TokenType::ShiftRight || type == TokenType::GreaterEqual) return false;
        if (type == TokenType::Identifier || type == TokenType::Comma || type == TokenType::Dot ||
            type == TokenType::LBracket || type == TokenType::RBracket || type == TokenType::Star ||
            type == TokenType::Question) {
            ++index;
            continue;
        }
        return false;
    }
    return false;
}

std::vector<TypeRef> Parser::parseTypeArgumentList() {
    std::vector<TypeRef> arguments;
    expect(TokenType::Less, "'<'");
    do {
        arguments.push_back(parseType());
    } while (match(TokenType::Comma));
    expect(TokenType::Greater, "'>'");
    return arguments;
}

bool Parser::isTypeStart() const {
    if (current().type != TokenType::Identifier) return false;
    return primitiveNames().count(current().text) > 0 || reservedWords().count(current().text) == 0;
}

TypeRef Parser::parseType() {
    if (current().type != TokenType::Identifier) {
        report("expected a type but found " + current().toString(), current());
        throw ParseAbort{};
    }
    Token token = advance();
    std::string name = token.text;
    // C# style qualified type: Game.AI.Player
    while (current().type == TokenType::Dot && peek(1).type == TokenType::Identifier) {
        advance();
        name += ".";
        name += advance().text;
    }
    TypeRef type(std::move(name), 0, token.line, token.column);
    if (current().type == TokenType::Less) {
        advance();
        do {
            type.typeArguments.push_back(parseType());
        } while (match(TokenType::Comma));
        if (current().type == TokenType::Greater) {
            advance();
        } else {
            report("expected '>' to close the generic type (write '> >' for nested generics)", current());
        }
    }
    while (current().type == TokenType::LBracket && peek(1).type == TokenType::RBracket) {
        advance();
        advance();
        ++type.arrayDepth;
    }
    return type;
}

std::unique_ptr<BlockStatement> Parser::parseBlock() {
    Token open = expect(TokenType::LBrace, "'{'");
    auto block = std::make_unique<BlockStatement>();
    block->line = open.line;
    block->column = open.column;
    while (!atEnd() && current().type != TokenType::RBrace) {
        std::size_t start = position_;
        try {
            block->statements.push_back(parseStatement());
        } catch (const ParseAbort&) {
            synchronizeInBlock();
        }
        if (position_ == start) advance();
    }
    expect(TokenType::RBrace, "'}'");
    return block;
}

bool Parser::isLocalDeclarationStart() const {
    if (current().type != TokenType::Identifier) return false;
    if (current().is("const")) return true;
    if (!isTypeStart()) return false;
    if (primitiveNames().count(current().text) > 0) return true;

    std::size_t index = skipQualifiedName(position_ + 1);
    // Generic type arguments: `Box<int> box`.
    if (index < tokens_.size() && tokens_[index].type == TokenType::Less) {
        int depth = 0;
        bool closed = false;
        while (index < tokens_.size()) {
            const auto type = tokens_[index].type;
            if (type == TokenType::Less) {
                ++depth;
            } else if (type == TokenType::Greater) {
                if (--depth == 0) {
                    ++index;
                    closed = true;
                    break;
                }
            } else if (type == TokenType::Semicolon || type == TokenType::LBrace ||
                       type == TokenType::RParen) {
                return false;
            }
            ++index;
        }
        if (!closed) return false;
    }
    // Array suffix: `Box<int>[] boxes`.
    while (index + 1 < tokens_.size() && tokens_[index].type == TokenType::LBracket &&
           tokens_[index + 1].type == TokenType::RBracket) {
        index += 2;
    }
    return index < tokens_.size() && tokens_[index].type == TokenType::Identifier &&
           reservedWords().count(tokens_[index].text) == 0;
}

std::unique_ptr<LocalVariableDeclaration> Parser::parseLocalDeclaration(bool expectSemicolon) {
    Token start = current();
    bool isConst = matchWord("const");
    auto declaration = std::make_unique<LocalVariableDeclaration>();
    declaration->isConst = isConst;
    declaration->line = start.line;
    declaration->column = start.column;

    if (current().is("var")) {
        advance();
    } else {
        declaration->type = parseType();
    }

    do {
        VariableInitializer initializer;
        initializer.line = current().line;
        initializer.column = current().column;
        initializer.name = expectIdentifier("variable name");
        if (match(TokenType::Assign)) initializer.value = parseExpression();
        declaration->initializers.push_back(std::move(initializer));
    } while (match(TokenType::Comma));

    if (expectSemicolon) expect(TokenType::Semicolon, "';'");
    return declaration;
}

StmtPtr Parser::parseStatement() {
    Token token = current();

    if (token.type == TokenType::LBrace) return parseBlock();
    if (match(TokenType::Semicolon)) {
        auto empty = std::make_unique<EmptyStatement>();
        empty->line = token.line;
        empty->column = token.column;
        return empty;
    }

    if (token.is("if")) {
        advance();
        expect(TokenType::LParen, "'('");
        auto condition = parseExpression();
        expect(TokenType::RParen, "')'");
        auto thenBranch = parseStatement();
        StmtPtr elseBranch;
        if (matchWord("else")) elseBranch = parseStatement();
        auto statement = std::make_unique<IfStatement>();
        statement->line = token.line;
        statement->column = token.column;
        statement->condition = std::move(condition);
        statement->thenBranch = std::move(thenBranch);
        statement->elseBranch = std::move(elseBranch);
        return statement;
    }

    if (token.is("while")) {
        advance();
        expect(TokenType::LParen, "'('");
        auto condition = parseExpression();
        expect(TokenType::RParen, "')'");
        auto body = parseStatement();
        auto statement = std::make_unique<WhileStatement>();
        statement->line = token.line;
        statement->column = token.column;
        statement->condition = std::move(condition);
        statement->body = std::move(body);
        return statement;
    }

    if (token.is("do")) {
        advance();
        auto body = parseStatement();
        expectWord("while");
        expect(TokenType::LParen, "'('");
        auto condition = parseExpression();
        expect(TokenType::RParen, "')'");
        expect(TokenType::Semicolon, "';'");
        auto statement = std::make_unique<DoWhileStatement>();
        statement->line = token.line;
        statement->column = token.column;
        statement->body = std::move(body);
        statement->condition = std::move(condition);
        return statement;
    }

    if (token.is("for")) {
        advance();
        expect(TokenType::LParen, "'('");
        auto statement = std::make_unique<ForStatement>();
        statement->line = token.line;
        statement->column = token.column;

        if (current().type != TokenType::Semicolon) {
            if (isLocalDeclarationStart()) {
                statement->initializer = parseLocalDeclaration(true);
            } else {
                auto expression = parseExpression();
                expect(TokenType::Semicolon, "';'");
                auto initializer = std::make_unique<ExpressionStatement>();
                initializer->line = expression->line;
                initializer->column = expression->column;
                initializer->expression = std::move(expression);
                statement->initializer = std::move(initializer);
            }
        } else {
            advance();
        }

        if (current().type != TokenType::Semicolon) statement->condition = parseExpression();
        expect(TokenType::Semicolon, "';'");
        if (current().type != TokenType::RParen) statement->iterator = parseExpression();
        expect(TokenType::RParen, "')'");
        statement->body = parseStatement();
        return statement;
    }

    if (token.is("foreach")) {
        advance();
        expect(TokenType::LParen, "'('");
        std::optional<TypeRef> elementType;
        if (current().is("var")) {
            advance();
        } else {
            elementType = parseType();
        }
        Token nameToken = current();
        std::string name = expectIdentifier("loop variable name");
        expectWord("in");
        auto collection = parseExpression();
        expect(TokenType::RParen, "')'");
        auto statement = std::make_unique<ForeachStatement>();
        statement->line = nameToken.line;
        statement->column = nameToken.column;
        statement->elementType = elementType;
        statement->variableName = name;
        statement->collection = std::move(collection);
        statement->body = parseStatement();
        return statement;
    }

    if (token.is("switch")) {
        advance();
        expect(TokenType::LParen, "'('");
        auto value = parseExpression();
        expect(TokenType::RParen, "')'");
        expect(TokenType::LBrace, "'{'");
        auto statement = std::make_unique<SwitchStatement>();
        statement->line = token.line;
        statement->column = token.column;
        statement->value = std::move(value);

        while (!atEnd() && current().type != TokenType::RBrace) {
            SwitchCase switchCase;
            if (matchWord("case")) {
                switchCase.line = current().line;
                switchCase.column = current().column;
                switchCase.label = parseExpression();
                expect(TokenType::Colon, "':'");
            } else if (matchWord("default")) {
                switchCase.line = tokens_[position_ - 1].line;
                switchCase.column = tokens_[position_ - 1].column;
                expect(TokenType::Colon, "':'");
            } else {
                report("expected 'case' or 'default' but found " + current().toString(), current());
                throw ParseAbort{};
            }

            while (!atEnd() && current().type != TokenType::RBrace && !current().is("case") &&
                   !current().is("default")) {
                switchCase.body.push_back(parseStatement());
            }
            statement->cases.push_back(std::move(switchCase));
        }
        expect(TokenType::RBrace, "'}'");
        return statement;
    }

    if (token.is("return")) {
        advance();
        auto statement = std::make_unique<ReturnStatement>();
        statement->line = token.line;
        statement->column = token.column;
        if (current().type != TokenType::Semicolon) statement->value = parseExpression();
        expect(TokenType::Semicolon, "';'");
        return statement;
    }

    if (token.is("try")) return parseTry(token);

    if (token.is("throw")) return parseThrow(token);

    if (token.is("break")) {
        advance();
        expect(TokenType::Semicolon, "';'");
        auto statement = std::make_unique<BreakStatement>();
        statement->line = token.line;
        statement->column = token.column;
        return statement;
    }

    if (token.is("continue")) {
        advance();
        expect(TokenType::Semicolon, "';'");
        auto statement = std::make_unique<ContinueStatement>();
        statement->line = token.line;
        statement->column = token.column;
        return statement;
    }

    if (isLocalDeclarationStart()) return parseLocalDeclaration(true);

    auto expression = parseExpression();
    expect(TokenType::Semicolon, "';'");
    auto statement = std::make_unique<ExpressionStatement>();
    statement->line = expression->line;
    statement->column = expression->column;
    statement->expression = std::move(expression);
    return statement;
}

StmtPtr Parser::parseTry(const Token& keyword) {
    advance();  // 'try'
    auto statement = std::make_unique<TryStatement>();
    statement->line = keyword.line;
    statement->column = keyword.column;
    statement->body = parseBlock();

    while (current().is("catch")) {
        Token catchToken = advance();
        auto clause = std::make_unique<CatchClause>();
        clause->line = catchToken.line;
        clause->column = catchToken.column;
        if (match(TokenType::LParen)) {
            clause->type = parseType();
            if (current().type == TokenType::Identifier) {
                clause->variable = expectIdentifier("exception variable name");
            }
            expect(TokenType::RParen, "')'");
        }
        clause->body = parseBlock();
        statement->catches.push_back(std::move(clause));
    }

    if (current().is("finally")) {
        advance();
        statement->finallyBody = parseBlock();
    }

    if (statement->catches.empty() && statement->finallyBody == nullptr) {
        report("'try' requires at least one catch or finally block", keyword);
    }
    return statement;
}

StmtPtr Parser::parseThrow(const Token& keyword) {
    advance();  // 'throw'
    auto statement = std::make_unique<ThrowStatement>();
    statement->line = keyword.line;
    statement->column = keyword.column;
    if (current().type != TokenType::Semicolon) statement->value = parseExpression();
    expect(TokenType::Semicolon, "';'");
    return statement;
}

ExprPtr Parser::parseArgument() {
    Token token = current();
    if (token.is("ref") || token.is("out") || token.is("in")) {
        advance();
        const ParamModifier modifier = token.is("ref")   ? ParamModifier::Ref
                                       : token.is("out") ? ParamModifier::Out
                                                         : ParamModifier::In;
        if (modifier == ParamModifier::Out) {
            if (current().is("var")) {
                Token varToken = advance();
                auto node = std::make_unique<OutDeclarationExpression>();
                node->line = varToken.line;
                node->column = varToken.column;
                node->name = expectIdentifier("variable name");
                return node;
            }
            if (isLocalDeclarationStart()) {
                auto node = std::make_unique<OutDeclarationExpression>();
                node->line = current().line;
                node->column = current().column;
                node->type = parseType();
                node->name = expectIdentifier("variable name");
                return node;
            }
        }
        auto node = std::make_unique<RefArgumentExpression>();
        node->line = token.line;
        node->column = token.column;
        node->modifier = modifier;
        node->operand = parseExpression();
        return node;
    }
    return parseExpression();
}

ExprPtr Parser::parseExpression() { return parseAssignment(); }

ExprPtr Parser::parseAssignment() {
    auto left = parseConditional();
    switch (current().type) {
        case TokenType::Assign:
        case TokenType::PlusAssign:
        case TokenType::MinusAssign:
        case TokenType::StarAssign:
        case TokenType::SlashAssign:
        case TokenType::PercentAssign:
        case TokenType::AmpAssign:
        case TokenType::PipeAssign:
        case TokenType::CaretAssign:
        case TokenType::ShiftLeftAssign:
        case TokenType::ShiftRightAssign: break;
        default: return left;
    }

    Token opToken = advance();
    if (dynamic_cast<IdentifierExpression*>(left.get()) == nullptr &&
        dynamic_cast<MemberExpression*>(left.get()) == nullptr &&
        dynamic_cast<IndexExpression*>(left.get()) == nullptr) {
        report("the left-hand side of '" + opToken.text + "' is not assignable", opToken);
        throw ParseAbort{};
    }
    auto right = parseAssignment();
    auto node = std::make_unique<AssignmentExpression>();
    node->line = opToken.line;
    node->column = opToken.column;
    node->op = opToken.text;
    node->target = std::move(left);
    node->value = std::move(right);
    return node;
}

ExprPtr Parser::parseConditional() {
    auto condition = parseLogicalOr();
    if (!match(TokenType::Question)) return condition;
    auto whenTrue = parseAssignment();
    expect(TokenType::Colon, "':'");
    auto whenFalse = parseAssignment();
    auto node = std::make_unique<ConditionalExpression>();
    node->line = condition->line;
    node->column = condition->column;
    node->condition = std::move(condition);
    node->whenTrue = std::move(whenTrue);
    node->whenFalse = std::move(whenFalse);
    return node;
}

#define SS_BINARY_LEVEL(FUNCTION, NEXT, ...)                                     \
    ExprPtr Parser::FUNCTION() {                                                 \
        auto left = NEXT();                                                      \
        while (true) {                                                           \
            bool matched = false;                                                \
            (void)matched;                                                       \
            __VA_ARGS__                                                          \
            if (!matched) break;                                                 \
        }                                                                        \
        return left;                                                             \
    }

ExprPtr Parser::parseLogicalOr() {
    auto left = parseLogicalAnd();
    while (match(TokenType::PipePipe)) {
        Token op = tokens_[position_ - 1];
        auto right = parseLogicalAnd();
        auto node = std::make_unique<BinaryExpression>();
        node->line = op.line;
        node->column = op.column;
        node->op = op.text;
        node->left = std::move(left);
        node->right = std::move(right);
        left = std::move(node);
    }
    return left;
}

ExprPtr Parser::parseLogicalAnd() {
    auto left = parseBitwiseOr();
    while (match(TokenType::AmpAmp)) {
        Token op = tokens_[position_ - 1];
        auto right = parseBitwiseOr();
        auto node = std::make_unique<BinaryExpression>();
        node->line = op.line;
        node->column = op.column;
        node->op = op.text;
        node->left = std::move(left);
        node->right = std::move(right);
        left = std::move(node);
    }
    return left;
}

ExprPtr Parser::parseBitwiseOr() {
    auto left = parseBitwiseXor();
    while (match(TokenType::Pipe)) {
        Token op = tokens_[position_ - 1];
        auto right = parseBitwiseXor();
        auto node = std::make_unique<BinaryExpression>();
        node->line = op.line;
        node->column = op.column;
        node->op = op.text;
        node->left = std::move(left);
        node->right = std::move(right);
        left = std::move(node);
    }
    return left;
}

ExprPtr Parser::parseBitwiseXor() {
    auto left = parseBitwiseAnd();
    while (match(TokenType::Caret)) {
        Token op = tokens_[position_ - 1];
        auto right = parseBitwiseAnd();
        auto node = std::make_unique<BinaryExpression>();
        node->line = op.line;
        node->column = op.column;
        node->op = op.text;
        node->left = std::move(left);
        node->right = std::move(right);
        left = std::move(node);
    }
    return left;
}

ExprPtr Parser::parseBitwiseAnd() {
    auto left = parseEquality();
    while (match(TokenType::Amp)) {
        Token op = tokens_[position_ - 1];
        auto right = parseEquality();
        auto node = std::make_unique<BinaryExpression>();
        node->line = op.line;
        node->column = op.column;
        node->op = op.text;
        node->left = std::move(left);
        node->right = std::move(right);
        left = std::move(node);
    }
    return left;
}

ExprPtr Parser::parseEquality() {
    auto left = parseRelational();
    while (current().type == TokenType::EqualEqual || current().type == TokenType::BangEqual) {
        Token op = advance();
        auto right = parseRelational();
        auto node = std::make_unique<BinaryExpression>();
        node->line = op.line;
        node->column = op.column;
        node->op = op.text;
        node->left = std::move(left);
        node->right = std::move(right);
        left = std::move(node);
    }
    return left;
}

ExprPtr Parser::parseRelational() {
    auto left = parseShift();
    while (true) {
        if (current().type == TokenType::Less || current().type == TokenType::LessEqual ||
            current().type == TokenType::Greater || current().type == TokenType::GreaterEqual) {
            Token op = advance();
            auto right = parseShift();
            auto node = std::make_unique<BinaryExpression>();
            node->line = op.line;
            node->column = op.column;
            node->op = op.text;
            node->left = std::move(left);
            node->right = std::move(right);
            left = std::move(node);
            continue;
        }
        if (current().is("is")) {
            Token op = advance();
            bool negated = matchWord("not");
            if (current().is("null")) {
                Token nullToken = advance();
                auto node = std::make_unique<TypeTestExpression>();
                node->line = nullToken.line;
                node->column = nullToken.column;
                node->operand = std::move(left);
                node->negated = negated;
                left = std::move(node);
                continue;
            }
            TypeRef type = parseType();
            auto node = std::make_unique<TypeTestExpression>();
            node->line = op.line;
            node->column = op.column;
            node->hasTargetType = true;
            node->targetType = type;
            node->operand = std::move(left);
            node->negated = negated;
            left = std::move(node);
            continue;
        }
        if (current().is("as")) {
            Token op = advance();
            TypeRef type = parseType();
            auto node = std::make_unique<AsExpression>();
            node->line = op.line;
            node->column = op.column;
            node->targetType = type;
            node->operand = std::move(left);
            left = std::move(node);
            continue;
        }
        break;
    }
    return left;
}

ExprPtr Parser::parseShift() {
    auto left = parseAdditive();
    while (current().type == TokenType::ShiftLeft || current().type == TokenType::ShiftRight) {
        Token op = advance();
        auto right = parseAdditive();
        auto node = std::make_unique<BinaryExpression>();
        node->line = op.line;
        node->column = op.column;
        node->op = op.text;
        node->left = std::move(left);
        node->right = std::move(right);
        left = std::move(node);
    }
    return left;
}

ExprPtr Parser::parseAdditive() {
    auto left = parseMultiplicative();
    while (current().type == TokenType::Plus || current().type == TokenType::Minus) {
        Token op = advance();
        auto right = parseMultiplicative();
        auto node = std::make_unique<BinaryExpression>();
        node->line = op.line;
        node->column = op.column;
        node->op = op.text;
        node->left = std::move(left);
        node->right = std::move(right);
        left = std::move(node);
    }
    return left;
}

ExprPtr Parser::parseMultiplicative() {
    auto left = parseUnary();
    while (current().type == TokenType::Star || current().type == TokenType::Slash ||
           current().type == TokenType::Percent) {
        Token op = advance();
        auto right = parseUnary();
        auto node = std::make_unique<BinaryExpression>();
        node->line = op.line;
        node->column = op.column;
        node->op = op.text;
        node->left = std::move(left);
        node->right = std::move(right);
        left = std::move(node);
    }
    return left;
}

bool Parser::isCastStart() const {
    if (current().type != TokenType::LParen || peek(1).type != TokenType::Identifier) return false;
    bool isPrimitive = primitiveNames().count(peek(1).text) > 0 && peek(1).text != "var";
    std::size_t index = isPrimitive ? 2 : skipQualifiedName(2);
    if (peek(static_cast<int>(index)).type != TokenType::RParen) return false;

    const Token& after = peek(static_cast<int>(index + 1));
    if (isPrimitive) {
        return after.type == TokenType::Identifier || after.type == TokenType::IntLiteral ||
               after.type == TokenType::FloatLiteral || after.type == TokenType::StringLiteral ||
               after.type == TokenType::CharLiteral || after.type == TokenType::LParen ||
               after.type == TokenType::Tilde || after.type == TokenType::Bang ||
               after.type == TokenType::Plus || after.type == TokenType::Minus ||
               after.type == TokenType::PlusPlus || after.type == TokenType::MinusMinus;
    }
    if (reservedWords().count(peek(1).text) > 0) return false;
    return after.type == TokenType::Identifier || after.type == TokenType::IntLiteral ||
           after.type == TokenType::FloatLiteral || after.type == TokenType::StringLiteral ||
           after.type == TokenType::CharLiteral || after.type == TokenType::LParen ||
           after.type == TokenType::Tilde || after.type == TokenType::Bang || after.is("new") ||
           after.is("this") || after.is("null") || after.is("true") || after.is("false");
}

ExprPtr Parser::parseUnary() {
    if (current().type == TokenType::Plus || current().type == TokenType::Minus ||
        current().type == TokenType::Bang || current().type == TokenType::Tilde ||
        current().type == TokenType::PlusPlus || current().type == TokenType::MinusMinus) {
        Token op = advance();
        auto operand = parseUnary();
        auto node = std::make_unique<UnaryExpression>();
        node->line = op.line;
        node->column = op.column;
        node->op = op.text;
        node->operand = std::move(operand);
        node->isPrefix = true;
        return node;
    }

    if (isCastStart()) {
        Token open = advance();
        TypeRef type = parseType();
        expect(TokenType::RParen, "')'");
        auto operand = parseUnary();
        auto node = std::make_unique<CastExpression>();
        node->line = open.line;
        node->column = open.column;
        node->targetType = type;
        node->operand = std::move(operand);
        return node;
    }

    return parsePostfix();
}

ExprPtr Parser::parsePostfix() {
    auto expression = parsePrimary();
    std::vector<TypeRef> pendingTypeArguments;
    while (true) {
        // Call-site type arguments: `Max<int>(1, 2)`, `Box<int>.Of<string>(x)`.
        if (current().type == TokenType::Less && looksLikeGenericArguments()) {
            pendingTypeArguments = parseTypeArgumentList();
            continue;
        }
        if (match(TokenType::Dot)) {
            Token nameToken = current();
            std::string name = expectIdentifier("member name");
            auto node = std::make_unique<MemberExpression>();
            node->line = nameToken.line;
            node->column = nameToken.column;
            node->target = std::move(expression);
            node->memberName = name;
            expression = std::move(node);
            continue;
        }
        if (match(TokenType::LBracket)) {
            auto index = parseExpression();
            expect(TokenType::RBracket, "']'");
            auto node = std::make_unique<IndexExpression>();
            node->line = expression->line;
            node->column = expression->column;
            node->target = std::move(expression);
            node->index = std::move(index);
            expression = std::move(node);
            continue;
        }
        if (match(TokenType::LParen)) {
            auto node = std::make_unique<CallExpression>();
            node->line = expression->line;
            node->column = expression->column;
            node->callee = std::move(expression);
            node->typeArguments = std::move(pendingTypeArguments);
            pendingTypeArguments.clear();
            if (current().type != TokenType::RParen) {
                do {
                    node->arguments.push_back(parseArgument());
                } while (match(TokenType::Comma));
            }
            expect(TokenType::RParen, "')'");
            expression = std::move(node);
            continue;
        }
        if (current().type == TokenType::PlusPlus || current().type == TokenType::MinusMinus) {
            Token op = advance();
            auto node = std::make_unique<UnaryExpression>();
            node->line = op.line;
            node->column = op.column;
            node->op = op.text;
            node->operand = std::move(expression);
            node->isPrefix = false;
            expression = std::move(node);
            continue;
        }
        break;
    }
    return expression;
}

bool Parser::looksLikeLambda() const {
    if (current().type == TokenType::Identifier && position_ + 1 < tokens_.size() &&
        tokens_[position_ + 1].type == TokenType::Arrow) {
        return true;
    }
    if (current().type != TokenType::LParen) return false;
    int depth = 0;
    for (std::size_t i = position_; i < tokens_.size(); ++i) {
        if (tokens_[i].type == TokenType::LParen) {
            ++depth;
        } else if (tokens_[i].type == TokenType::RParen) {
            if (--depth == 0) {
                return i + 1 < tokens_.size() && tokens_[i + 1].type == TokenType::Arrow;
            }
        }
    }
    return false;
}

ExprPtr Parser::parseLambdaExpression() {
    Token start = current();
    auto lambda = std::make_unique<LambdaExpression>();
    lambda->line = start.line;
    lambda->column = start.column;

    auto declaration = std::make_unique<FunctionDeclaration>();
    declaration->line = start.line;
    declaration->column = start.column;
    declaration->isStatic = true;

    if (current().type == TokenType::Identifier) {
        ParameterDeclaration parameter;
        parameter.name = advance().text;
        declaration->parameters.push_back(std::move(parameter));
    } else {
        expect(TokenType::LParen, "'('");
        if (current().type != TokenType::RParen) {
            do {
                ParameterDeclaration parameter;
                const bool untyped =
                    current().type == TokenType::Identifier && position_ + 1 < tokens_.size() &&
                    (tokens_[position_ + 1].type == TokenType::Comma ||
                     tokens_[position_ + 1].type == TokenType::RParen);
                if (untyped) {
                    parameter.name = advance().text;
                } else {
                    parameter.type = parseType();
                    parameter.name = expectIdentifier("parameter name");
                }
                declaration->parameters.push_back(std::move(parameter));
            } while (match(TokenType::Comma));
        }
        expect(TokenType::RParen, "')'");
    }
    expect(TokenType::Arrow, "'=>'");

    if (current().type == TokenType::LBrace) {
        declaration->body = parseBlock();
    } else {
        lambda->expressionBody = true;
        auto expression = parseExpression();
        auto block = std::make_unique<BlockStatement>();
        block->line = expression->line;
        block->column = expression->column;
        auto statement = std::make_unique<ReturnStatement>();
        statement->line = expression->line;
        statement->column = expression->column;
        statement->value = std::move(expression);
        block->statements.push_back(std::move(statement));
        declaration->body = std::move(block);
    }
    lambda->declaration = std::move(declaration);
    return lambda;
}

ExprPtr Parser::parsePrimary() {
    Token token = current();
    if (looksLikeLambda()) return parseLambdaExpression();
    switch (token.type) {
        case TokenType::IntLiteral: {
            advance();
            auto node = std::make_unique<LiteralExpression>();
            node->line = token.line;
            node->column = token.column;
            node->kind = LiteralKind::Int;
            node->intValue = token.longValue;
            node->suffix = token.suffix;
            return node;
        }
        case TokenType::FloatLiteral: {
            advance();
            auto node = std::make_unique<LiteralExpression>();
            node->line = token.line;
            node->column = token.column;
            node->kind = LiteralKind::Float;
            node->floatValue = token.doubleValue;
            node->suffix = token.suffix;
            return node;
        }
        case TokenType::StringLiteral: {
            advance();
            auto node = std::make_unique<LiteralExpression>();
            node->line = token.line;
            node->column = token.column;
            node->kind = LiteralKind::String;
            node->stringValue = token.stringValue;
            return node;
        }
        case TokenType::CharLiteral: {
            advance();
            auto node = std::make_unique<LiteralExpression>();
            node->line = token.line;
            node->column = token.column;
            node->kind = LiteralKind::Char;
            node->charValue = token.charValue;
            return node;
        }
        case TokenType::LParen: {
            advance();
            auto expression = parseExpression();
            expect(TokenType::RParen, "')'");
            return expression;
        }
        default: break;
    }

    if (token.is("true") || token.is("false")) {
        advance();
        auto node = std::make_unique<LiteralExpression>();
        node->line = token.line;
        node->column = token.column;
        node->kind = LiteralKind::Bool;
        node->boolValue = token.is("true");
        return node;
    }
    if (token.is("null")) {
        advance();
        auto node = std::make_unique<LiteralExpression>();
        node->line = token.line;
        node->column = token.column;
        node->kind = LiteralKind::Null;
        return node;
    }
    if (token.is("this")) {
        advance();
        auto node = std::make_unique<ThisExpression>();
        node->line = token.line;
        node->column = token.column;
        return node;
    }
    if (token.is("new")) return parseNew();

    if (token.type == TokenType::Identifier) {
        advance();
        auto node = std::make_unique<IdentifierExpression>();
        node->line = token.line;
        node->column = token.column;
        node->name = token.text;
        return node;
    }

    report("unexpected " + token.toString() + " in expression", token);
    throw ParseAbort{};
}

ExprPtr Parser::parseNew() {
    Token newToken = expectWord("new");
    TypeRef type = parseType();

    if (type.arrayDepth > 0) {
        expect(TokenType::LBrace, "'{'");
        auto node = std::make_unique<NewArrayExpression>();
        node->line = newToken.line;
        node->column = newToken.column;
        node->elementType = TypeRef(type.name, type.arrayDepth - 1, type.line, type.column);
        node->hasInitializer = true;
        if (current().type != TokenType::RBrace) {
            do {
                node->initializer.push_back(parseExpression());
            } while (match(TokenType::Comma));
        }
        expect(TokenType::RBrace, "'}'");
        return node;
    }

    if (match(TokenType::LBracket)) {
        auto node = std::make_unique<NewArrayExpression>();
        node->line = newToken.line;
        node->column = newToken.column;
        node->elementType = type;
        node->size = parseExpression();
        expect(TokenType::RBracket, "']'");
        return node;
    }

    expect(TokenType::LParen, "'('");
    auto node = std::make_unique<NewObjectExpression>();
    node->line = newToken.line;
    node->column = newToken.column;
    node->type = type;
    if (current().type != TokenType::RParen) {
        do {
            node->arguments.push_back(parseArgument());
        } while (match(TokenType::Comma));
    }
    expect(TokenType::RParen, "')'");
    return node;
}

}  // namespace sharpscript
