#pragma once

#include "common.hpp"

namespace sharpscript {

enum class TokenType {
    EndOfFile,
    Identifier,
    IntLiteral,
    FloatLiteral,
    StringLiteral,
    CharLiteral,

    Plus,
    Minus,
    Star,
    Slash,
    Percent,
    Amp,
    Pipe,
    Caret,
    Tilde,
    Bang,
    Assign,
    Less,
    Greater,

    PlusPlus,
    MinusMinus,
    PlusAssign,
    MinusAssign,
    StarAssign,
    SlashAssign,
    PercentAssign,
    AmpAssign,
    PipeAssign,
    CaretAssign,
    ShiftLeftAssign,
    ShiftRightAssign,

    AmpAmp,
    PipePipe,
    ShiftLeft,
    ShiftRight,

    EqualEqual,
    BangEqual,
    LessEqual,
    GreaterEqual,

    /// `=>` used by lambda expressions.
    Arrow,

    Dot,
    Comma,
    Semicolon,
    Colon,
    Question,

    LParen,
    RParen,
    LBrace,
    RBrace,
    LBracket,
    RBracket
};

enum class NumericSuffix { None, Float, Double, Unsigned, Long, UnsignedLong, Decimal };

struct Token {
    TokenType type = TokenType::EndOfFile;
    std::string text;
    int line = 1;
    int column = 1;
    int offset = 0;
    std::int64_t longValue = 0;
    double doubleValue = 0.0;
    char16_t charValue = u'\0';
    NumericSuffix suffix = NumericSuffix::None;
    std::u16string stringValue;

    bool is(const std::string& word) const { return type == TokenType::Identifier && text == word; }
    bool isIdentifier() const { return type == TokenType::Identifier; }
    std::string toString() const;
};

class Tokenizer {
public:
    static std::vector<Token> tokenize(const std::string& source, DiagnosticBag& diagnostics);

private:
    Tokenizer(const std::string& source, DiagnosticBag& diagnostics)
        : source_(source), diagnostics_(diagnostics) {}

    std::vector<Token> run();
    Token nextToken();
    void skipWhitespaceAndComments();
    Token readIdentifier(int line, int column, int offset);
    Token readNumber(int line, int column, int offset);
    Token readString(int line, int column, int offset);
    Token readChar(int line, int column, int offset);
    bool readEscape(std::u16string& out, int line, int column);
    char32_t readCodePoint();

    char current() const { return position_ < source_.size() ? source_[position_] : '\0'; }
    char peek(int offset = 1) const {
        return position_ + offset < source_.size() ? source_[position_ + offset] : '\0';
    }
    bool atEnd() const { return position_ >= source_.size(); }
    void advance();
    bool match(char expected);

    const std::string& source_;
    DiagnosticBag& diagnostics_;
    std::size_t position_ = 0;
    int line_ = 1;
    int column_ = 1;
};

}  // namespace sharpscript
