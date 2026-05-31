#include "lexer.h"
#include <cctype>
#include <stdexcept>

namespace dagger {

Lexer::Lexer(std::string_view source) : source_(source) {}

std::vector<Token> Lexer::tokenize() {
    std::vector<Token> tokens;
    while (!isAtEnd()) {
        start_ = current_;
        Token token = scanToken();
        if (token.kind != TokenKind::EndOfFile) {
            tokens.push_back(std::move(token));
        }
    }
    tokens.push_back(Token{TokenKind::EndOfFile, "", line_, column_});
    return tokens;
}

char Lexer::peek() const {
    return isAtEnd() ? '\0' : source_[current_];
}

char Lexer::peekNext() const {
    size_t next = current_ + 1;
    return next >= source_.size() ? '\0' : source_[next];
}

char Lexer::advance() {
    char c = peek();
    current_++;
    if (c == '\n') {
        line_++;
        column_ = 1;
    } else {
        column_++;
    }
    return c;
}

bool Lexer::isAtEnd() const {
    return current_ >= source_.size();
}

void Lexer::skipWhitespace() {
    while (!isAtEnd()) {
        char c = peek();
        if (c == ' ' || c == '\r' || c == '\t' || c == '\n') {
            advance();
            continue;
        }
        if (c == '#') {
            while (!isAtEnd() && peek() != '\n') {
                advance();
            }
            continue;
        }
        break;
    }
}

Token Lexer::makeToken(TokenKind kind, std::string text) const {
    return Token{kind, std::move(text), line_, column_ - static_cast<int>(text.size())};
}

Token Lexer::scanToken() {
    skipWhitespace();
    if (isAtEnd()) {
        return Token{TokenKind::EndOfFile, "", line_, column_};
    }

    char c = advance();
    if (c == '@') {
        if (std::isalpha(peek()) || peek() == '_') {
            return scanAtIdentifier();
        }
        return makeToken(TokenKind::Operator, "@");
    }

    if (std::isalpha(static_cast<unsigned char>(c)) || c == '_') {
        current_--;
        return scanIdentifier();
    }

    if (std::isdigit(static_cast<unsigned char>(c)) || (c == '-' && std::isdigit(static_cast<unsigned char>(peek())))) {
        current_--;
        return scanNumber();
    }

    if (c == '"') {
        return scanString();
    }

    if (c == '\'') {
        return scanChar();
    }

    if (std::ispunct(static_cast<unsigned char>(c))) {
        if (c == ',' || c == ';' || c == '(' || c == ')' || c == '[' || c == ']' || c == '{' || c == '}') {
            return makeToken(TokenKind::Punctuation, std::string(1, c));
        }
        return scanOperator(c);
    }

    return makeToken(TokenKind::Operator, std::string(1, c));
}

Token Lexer::scanIdentifier() {
    size_t start = current_;
    while (!isAtEnd() && (std::isalnum(static_cast<unsigned char>(peek())) || peek() == '_' || peek() == '.')) {
        advance();
    }
    std::string text(source_.substr(start, current_ - start));
    TokenKind kind = TokenKind::Identifier;
    if (isKeyword(text)) {
        kind = TokenKind::Keyword;
    } else if (isDirective(text)) {
        kind = TokenKind::Keyword;
    }
    return makeToken(kind, std::move(text));
}

Token Lexer::scanAtIdentifier() {
    size_t start = current_ - 1;
    while (!isAtEnd() && (std::isalnum(static_cast<unsigned char>(peek())) || peek() == '_' || peek() == '.')) {
        advance();
    }
    std::string text(source_.substr(start, current_ - start));
    TokenKind kind = TokenKind::Identifier;
    if (isDirective(text)) {
        kind = TokenKind::Keyword;
    }
    return makeToken(kind, std::move(text));
}

Token Lexer::scanNumber() {
    size_t start = current_;
    bool isFloat = false;

    if (peek() == '-') {
        advance();
    }

    if (peek() == '0' && (peekNext() == 'x' || peekNext() == 'b' || peekNext() == 'o')) {
        char prefix = peekNext();
        advance();
        advance();
        while (!isAtEnd()) {
            char c = peek();
            bool valid = false;
            if (prefix == 'x') {
                valid = std::isxdigit(static_cast<unsigned char>(c));
            } else if (prefix == 'b') {
                valid = c == '0' || c == '1';
            } else if (prefix == 'o') {
                valid = c >= '0' && c <= '7';
            }
            if (!valid) {
                break;
            }
            advance();
        }
    } else {
        while (!isAtEnd() && std::isdigit(static_cast<unsigned char>(peek()))) {
            advance();
        }
        if (peek() == '.' && std::isdigit(static_cast<unsigned char>(peekNext()))) {
            isFloat = true;
            advance();
            while (!isAtEnd() && std::isdigit(static_cast<unsigned char>(peek()))) {
                advance();
            }
        }
        if (peek() == 'e' || peek() == 'E') {
            isFloat = true;
            advance();
            if (peek() == '+' || peek() == '-') {
                advance();
            }
            while (!isAtEnd() && std::isdigit(static_cast<unsigned char>(peek()))) {
                advance();
            }
        }
    }

    std::string text(source_.substr(start, current_ - start));
    return makeToken(isFloat ? TokenKind::FloatLiteral : TokenKind::IntegerLiteral, std::move(text));
}

Token Lexer::scanString() {
    std::string value;
    while (!isAtEnd() && peek() != '"') {
        char c = advance();
        if (c == '\\' && !isAtEnd()) {
            char next = advance();
            switch (next) {
                case 'n': value.push_back('\n'); break;
                case 'r': value.push_back('\r'); break;
                case 't': value.push_back('\t'); break;
                case '\\': value.push_back('\\'); break;
                case '"': value.push_back('"'); break;
                default: value.push_back(next); break;
            }
        } else {
            value.push_back(c);
        }
    }
    if (!isAtEnd()) {
        advance();
    }
    return makeToken(TokenKind::StringLiteral, std::move(value));
}

Token Lexer::scanChar() {
    std::string value;
    if (!isAtEnd()) {
        char c = advance();
        if (c == '\\' && !isAtEnd()) {
            char next = advance();
            switch (next) {
                case 'n': value.push_back('\n'); break;
                case 'r': value.push_back('\r'); break;
                case 't': value.push_back('\t'); break;
                case '\\': value.push_back('\\'); break;
                case '\'': value.push_back('\''); break;
                default: value.push_back(next); break;
            }
        } else {
            value.push_back(c);
        }
    }
    if (!isAtEnd() && peek() == '\'') {
        advance();
    }
    return makeToken(TokenKind::CharLiteral, std::move(value));
}

Token Lexer::scanOperator(char first) {
    std::string text(1, first);
    char second = peek();
    if ((first == '-' && second == '>') || (first == '=' && second == '>') || (first == ':' && second == ':') ||
        (first == '<' && second == '=') || (first == '>' && second == '=') || (first == '!' && second == '=') ||
        (first == '?' && (second == '=' || second == '<' || second == '>' || second == '!')) ||
        (first == '.' && second == '.')) {
        advance();
        text.push_back(second);
        if ((first == '?' && (second == '<' || second == '>' || second == '!')) && peek() == '=') {
            text.push_back(advance());
        }
    }
    return makeToken(TokenKind::Operator, std::move(text));
}

} // namespace dagger
