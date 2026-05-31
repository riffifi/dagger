#pragma once

#include <string>
#include <string_view>
#include <set>

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
    static const std::set<std::string, std::less<>> keywords = {
        "loop", "fork", "block", "each", "tee", "null", "true", "false"
    };
    return keywords.contains(std::string(text));
}

inline bool isDirective(std::string_view text) {
    static const std::set<std::string, std::less<>> directives = {
        "@fn", "@type", "@burst", "@static", "@extern", "@inline", 
        "@comptime", "@private", "@use", "@syscall", "@error", 
        "@bubble", "@or", "@ok", "@err", "@break", "@skip"
    };
    return directives.contains(std::string(text));
}

} // namespace dagger
