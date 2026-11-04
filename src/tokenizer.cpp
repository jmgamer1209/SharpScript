#include "sharpscript/tokenizer.hpp"

#include <cstdlib>

#include "sharpscript/unicode.hpp"

namespace sharpscript {

std::string Token::toString() const {
    if (type == TokenType::EndOfFile) return "end of file";
    return "'" + text + "'";
}

std::vector<Token> Tokenizer::tokenize(const std::string& source, DiagnosticBag& diagnostics) {
    Tokenizer tokenizer(source, diagnostics);
    return tokenizer.run();
}

std::vector<Token> Tokenizer::run() {
    std::vector<Token> tokens;
    // Skip a UTF-8 BOM if present (editors and stdin redirection both produce one).
    if (source_.size() >= 3 && static_cast<unsigned char>(source_[0]) == 0xEF &&
        static_cast<unsigned char>(source_[1]) == 0xBB && static_cast<unsigned char>(source_[2]) == 0xBF) {
        position_ = 3;
    }
    while (true) {
        Token token = nextToken();
        tokens.push_back(token);
        if (token.type == TokenType::EndOfFile) break;
    }
    return tokens;
}

void Tokenizer::advance() {
    if (atEnd()) return;
    if (source_[position_] == '\n') {
        ++line_;
        column_ = 1;
    } else {
        ++column_;
    }
    ++position_;
}

bool Tokenizer::match(char expected) {
    if (current() != expected) return false;
    advance();
    return true;
}

void Tokenizer::skipWhitespaceAndComments() {
    while (!atEnd()) {
        char c = current();
        if (c == ' ' || c == '\t' || c == '\r' || c == '\n') {
            advance();
            continue;
        }
        // Tolerate UTF-8 BOM sequences appearing mid-source (e.g. piped REPL input).
        if (static_cast<unsigned char>(c) == 0xEF && static_cast<unsigned char>(peek()) == 0xBB &&
            static_cast<unsigned char>(peek(2)) == 0xBF) {
            advance();
            advance();
            advance();
            continue;
        }
        if (c == '/' && peek() == '/') {
            while (!atEnd() && current() != '\n') advance();
            continue;
        }
        if (c == '/' && peek() == '*') {
            int startLine = line_;
            int startColumn = column_;
            advance();
            advance();
            bool terminated = false;
            while (!atEnd()) {
                if (current() == '*' && peek() == '/') {
                    advance();
                    advance();
                    terminated = true;
                    break;
                }
                advance();
            }
            if (!terminated) diagnostics_.error("E0001", "unterminated block comment", startLine, startColumn);
            continue;
        }
        break;
    }
}

Token Tokenizer::nextToken() {
    skipWhitespaceAndComments();
    int line = line_;
    int column = column_;
    int offset = static_cast<int>(position_);

    if (atEnd()) return Token{TokenType::EndOfFile, "", line, column, offset};

    char c = current();
    if ((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || c == '_') {
        return readIdentifier(line, column, offset);
    }
    if (c >= '0' && c <= '9') return readNumber(line, column, offset);
    if (c == '"') return readString(line, column, offset);
    if (c == '\'') return readChar(line, column, offset);

    advance();
    switch (c) {
        case '+':
            if (match('+')) return {TokenType::PlusPlus, "++", line, column, offset};
            if (match('=')) return {TokenType::PlusAssign, "+=", line, column, offset};
            return {TokenType::Plus, "+", line, column, offset};
        case '-':
            if (match('-')) return {TokenType::MinusMinus, "--", line, column, offset};
            if (match('=')) return {TokenType::MinusAssign, "-=", line, column, offset};
            return {TokenType::Minus, "-", line, column, offset};
        case '*':
            if (match('=')) return {TokenType::StarAssign, "*=", line, column, offset};
            return {TokenType::Star, "*", line, column, offset};
        case '/':
            if (match('=')) return {TokenType::SlashAssign, "/=", line, column, offset};
            return {TokenType::Slash, "/", line, column, offset};
        case '%':
            if (match('=')) return {TokenType::PercentAssign, "%=", line, column, offset};
            return {TokenType::Percent, "%", line, column, offset};
        case '&':
            if (match('&')) return {TokenType::AmpAmp, "&&", line, column, offset};
            if (match('=')) return {TokenType::AmpAssign, "&=", line, column, offset};
            return {TokenType::Amp, "&", line, column, offset};
        case '|':
            if (match('|')) return {TokenType::PipePipe, "||", line, column, offset};
            if (match('=')) return {TokenType::PipeAssign, "|=", line, column, offset};
            return {TokenType::Pipe, "|", line, column, offset};
        case '^':
            if (match('=')) return {TokenType::CaretAssign, "^=", line, column, offset};
            return {TokenType::Caret, "^", line, column, offset};
        case '~': return {TokenType::Tilde, "~", line, column, offset};
        case '!':
            if (match('=')) return {TokenType::BangEqual, "!=", line, column, offset};
            return {TokenType::Bang, "!", line, column, offset};
        case '=':
            if (match('=')) return {TokenType::EqualEqual, "==", line, column, offset};
            if (match('>')) return {TokenType::Arrow, "=>", line, column, offset};
            return {TokenType::Assign, "=", line, column, offset};
        case '<':
            if (match('<')) {
                if (match('=')) return {TokenType::ShiftLeftAssign, "<<=", line, column, offset};
                return {TokenType::ShiftLeft, "<<", line, column, offset};
            }
            if (match('=')) return {TokenType::LessEqual, "<=", line, column, offset};
            return {TokenType::Less, "<", line, column, offset};
        case '>':
            if (match('>')) {
                if (match('=')) return {TokenType::ShiftRightAssign, ">>=", line, column, offset};
                return {TokenType::ShiftRight, ">>", line, column, offset};
            }
            if (match('=')) return {TokenType::GreaterEqual, ">=", line, column, offset};
            return {TokenType::Greater, ">", line, column, offset};
        case '.': return {TokenType::Dot, ".", line, column, offset};
        case ',': return {TokenType::Comma, ",", line, column, offset};
        case ';': return {TokenType::Semicolon, ";", line, column, offset};
        case ':': return {TokenType::Colon, ":", line, column, offset};
        case '?': return {TokenType::Question, "?", line, column, offset};
        case '(': return {TokenType::LParen, "(", line, column, offset};
        case ')': return {TokenType::RParen, ")", line, column, offset};
        case '{': return {TokenType::LBrace, "{", line, column, offset};
        case '}': return {TokenType::RBrace, "}", line, column, offset};
        case '[': return {TokenType::LBracket, "[", line, column, offset};
        case ']': return {TokenType::RBracket, "]", line, column, offset};
        default: break;
    }

    diagnostics_.error("E0002", std::string("unexpected character '") + c + "'", line, column);
    return nextToken();
}

Token Tokenizer::readIdentifier(int line, int column, int offset) {
    std::string text;
    while (!atEnd()) {
        char c = current();
        if ((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '_') {
            text.push_back(c);
            advance();
        } else {
            break;
        }
    }
    Token token;
    token.type = TokenType::Identifier;
    token.text = std::move(text);
    token.line = line;
    token.column = column;
    token.offset = offset;
    return token;
}

Token Tokenizer::readNumber(int line, int column, int offset) {
    std::string text;
    bool isFloat = false;
    NumericSuffix suffix = NumericSuffix::None;

    if (current() == '0' && (peek() == 'x' || peek() == 'X')) {
        text.push_back(current());
        advance();
        text.push_back(current());
        advance();
        while (!atEnd()) {
            char c = current();
            bool hex = (c >= '0' && c <= '9') || (c >= 'a' && c <= 'f') || (c >= 'A' && c <= 'F');
            if (!hex) break;
            text.push_back(c);
            advance();
        }
        if (current() == 'u' || current() == 'U') {
            suffix = NumericSuffix::Unsigned;
            advance();
        } else if (current() == 'l' || current() == 'L') {
            suffix = NumericSuffix::Long;
            advance();
            if (current() == 'u' || current() == 'U') {
                suffix = NumericSuffix::UnsignedLong;
                advance();
            }
        }
        std::int64_t value = 0;
        try {
            value = static_cast<std::int64_t>(std::stoull(text.substr(2), nullptr, 16));
        } catch (...) {
            diagnostics_.error("E0003", "invalid hexadecimal literal '" + text + "'", line, column);
        }
        Token token;
        token.type = TokenType::IntLiteral;
        token.text = std::move(text);
        token.line = line;
        token.column = column;
        token.offset = offset;
        token.longValue = value;
        token.suffix = suffix;
        return token;
    }

    while (!atEnd() && current() >= '0' && current() <= '9') {
        text.push_back(current());
        advance();
    }
    if (current() == '.' && peek() >= '0' && peek() <= '9') {
        isFloat = true;
        text.push_back(current());
        advance();
        while (!atEnd() && current() >= '0' && current() <= '9') {
            text.push_back(current());
            advance();
        }
    }
    if (current() == 'e' || current() == 'E') {
        isFloat = true;
        text.push_back(current());
        advance();
        if (current() == '+' || current() == '-') {
            text.push_back(current());
            advance();
        }
        while (!atEnd() && current() >= '0' && current() <= '9') {
            text.push_back(current());
            advance();
        }
    }
    if (current() == 'f' || current() == 'F') {
        isFloat = true;
        suffix = NumericSuffix::Float;
        advance();
    } else if (current() == 'd' || current() == 'D' || current() == 'm' || current() == 'M') {
        isFloat = true;
        suffix = (current() == 'm' || current() == 'M') ? NumericSuffix::Decimal : NumericSuffix::Double;
        advance();
    } else if (current() == 'u' || current() == 'U') {
        suffix = NumericSuffix::Unsigned;
        advance();
        if (current() == 'l' || current() == 'L') {
            suffix = NumericSuffix::UnsignedLong;
            advance();
        }
    } else if (current() == 'l' || current() == 'L') {
        suffix = NumericSuffix::Long;
        advance();
        if (current() == 'u' || current() == 'U') {
            suffix = NumericSuffix::UnsignedLong;
            advance();
        }
    }

    if (!atEnd() && ((current() >= 'a' && current() <= 'z') || (current() >= 'A' && current() <= 'Z'))) {
        diagnostics_.error("E0004", std::string("invalid numeric suffix '") + current() + "'", line, column);
        while (!atEnd()) {
            char c = current();
            if (!((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9'))) break;
            advance();
        }
    }

    Token token;
    token.text = std::move(text);
    token.line = line;
    token.column = column;
    token.offset = offset;
    token.suffix = suffix;
    if (isFloat || suffix == NumericSuffix::Float || suffix == NumericSuffix::Double ||
        suffix == NumericSuffix::Decimal) {
        token.type = TokenType::FloatLiteral;
        try {
            token.doubleValue = std::stod(token.text);
        } catch (...) {
            diagnostics_.error("E0005", "invalid floating point literal '" + token.text + "'", line, column);
        }
    } else {
        token.type = TokenType::IntLiteral;
        try {
            token.longValue = std::stoll(token.text);
        } catch (...) {
            diagnostics_.error("E0006", "integer literal '" + token.text + "' is out of range", line,
                               column);
        }
    }
    return token;
}

char32_t Tokenizer::readCodePoint() {
    std::size_t index = position_;
    char32_t codePoint = 0xFFFD;
    decodeUtf8(source_, index, codePoint);
    if (index <= position_) {
        advance();
        return 0xFFFD;
    }
    while (position_ < index && !atEnd()) advance();
    return codePoint;
}

Token Tokenizer::readString(int line, int column, int offset) {
    advance();  // opening quote
    std::u16string value;
    while (!atEnd() && current() != '"') {
        if (current() == '\n' || current() == '\r') {
            diagnostics_.error("E0007", "unterminated string literal", line, column);
            break;
        }
        if (current() == '\\') {
            advance();
            readEscape(value, line, column);
            continue;
        }
        appendCodePoint(value, readCodePoint());
    }
    if (current() == '"') {
        advance();
    } else {
        diagnostics_.error("E0008", "unterminated string literal", line, column);
    }
    Token token;
    token.type = TokenType::StringLiteral;
    token.text = utf16ToUtf8(value);
    token.stringValue = std::move(value);
    token.line = line;
    token.column = column;
    token.offset = offset;
    return token;
}

Token Tokenizer::readChar(int line, int column, int offset) {
    advance();  // opening quote
    if (atEnd() || current() == '\n' || current() == '\r') {
        diagnostics_.error("E0009", "unterminated character literal", line, column);
        return {TokenType::CharLiteral, "\\0", line, column, offset};
    }

    char16_t value = u'\0';
    if (current() == '\\') {
        advance();
        std::u16string escaped;
        readEscape(escaped, line, column);
        if (escaped.size() == 1) {
            value = escaped[0];
        } else {
            diagnostics_.error("E0010", "character literal must be a single UTF-16 code unit", line,
                               column);
        }
    } else {
        char32_t codePoint = readCodePoint();
        if (codePoint > 0xFFFF || (codePoint >= 0xD800 && codePoint <= 0xDFFF)) {
            diagnostics_.error("E0014",
                               "character literal must be a single UTF-16 code unit (BMP character)",
                               line, column);
        } else {
            value = static_cast<char16_t>(codePoint);
        }
    }

    if (current() == '\'') {
        advance();
    } else {
        diagnostics_.error("E0010", "character literal must contain exactly one character", line, column);
    }

    Token token;
    token.type = TokenType::CharLiteral;
    token.text = utf16ToUtf8(std::u16string(1, value));
    token.stringValue = std::u16string(1, value);
    token.line = line;
    token.column = column;
    token.offset = offset;
    token.charValue = value;
    return token;
}

bool Tokenizer::readEscape(std::u16string& out, int line, int column) {
    if (atEnd()) {
        diagnostics_.error("E0011", "unterminated escape sequence", line, column);
        return false;
    }
    char escape = current();
    advance();
    switch (escape) {
        case 'n': out.push_back(u'\n'); return true;
        case 'r': out.push_back(u'\r'); return true;
        case 't': out.push_back(u'\t'); return true;
        case '0': out.push_back(u'\0'); return true;
        case '\\': out.push_back(u'\\'); return true;
        case '"': out.push_back(u'"'); return true;
        case '\'': out.push_back(u'\''); return true;
        case 'a': out.push_back(u'\a'); return true;
        case 'b': out.push_back(u'\b'); return true;
        case 'f': out.push_back(u'\f'); return true;
        case 'v': out.push_back(u'\v'); return true;
        case 'u': {
            std::string hex;
            for (int i = 0; i < 4; ++i) {
                if (atEnd()) {
                    diagnostics_.error("E0012", "invalid unicode escape sequence", line, column);
                    return false;
                }
                char c = current();
                bool valid = (c >= '0' && c <= '9') || (c >= 'a' && c <= 'f') || (c >= 'A' && c <= 'F');
                if (!valid) {
                    diagnostics_.error("E0012", "invalid unicode escape sequence", line, column);
                    return false;
                }
                hex.push_back(c);
                advance();
            }
            auto code = static_cast<std::uint32_t>(std::stoul(hex, nullptr, 16));
            // \uXXXX is one UTF-16 code unit, exactly like C#.
            out.push_back(static_cast<char16_t>(code));
            return true;
        }
        default:
            diagnostics_.error("E0013", std::string("unknown escape sequence '\\") + escape + "'", line,
                               column);
            return false;
    }
}

}  // namespace sharpscript
