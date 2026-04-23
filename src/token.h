#pragma once

#include <string>
#include <string_view>

namespace dagger {

enum class TokenKind {
    EndOfFile,
    Identifier,
    IntegerLiteral,
    FloatLiteral,
    StringLiteral,
    CharLiteral,
    Operator,
    Punctuation,
    Keyword,
};

struct Token {
    TokenKind kind;
    std::string text;
    int line = 1;
    int column = 1;
};

inline bool isKeyword(std::string_view text) {
    return text == "true" || text == "false" || text == "null" || text == "loop" || text == "fork" ||
           text == "field" || text == "block" || text == "loop.range";
}

} // namespace dagger
