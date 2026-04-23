#pragma once

#include "token.h"
#include <string>
#include <string_view>
#include <vector>

namespace dagger {

class Lexer {
public:
    explicit Lexer(std::string_view source);
    std::vector<Token> tokenize();

private:
    char peek() const;
    char peekNext() const;
    char advance();
    bool isAtEnd() const;
    void skipWhitespace();
    Token makeToken(TokenKind kind, std::string text) const;
    Token scanToken();
    Token scanIdentifier();
    Token scanAtIdentifier();
    Token scanNumber();
    Token scanString();
    Token scanChar();
    Token scanOperator(char first);

private:
    std::string_view source_;
    size_t start_ = 0;
    size_t current_ = 0;
    int line_ = 1;
    int column_ = 1;
};

} // namespace dagger
