#pragma once

#include "ast.h"
#include "token.h"
#include <string>
#include <vector>

namespace dagger {

class Parser {
public:
    explicit Parser(std::vector<Token> tokens);
    std::unique_ptr<Program> parse();

private:
    const Token& peek() const;
    const Token& previous() const;
    bool isAtEnd() const;
    const Token& advance();
    const Token& peekNext() const;
    bool checkAt(size_t offset, TokenKind kind, std::string_view text = "") const;
    bool match(TokenKind kind, std::string_view text = "");
    bool check(TokenKind kind, std::string_view text = "") const;
    bool isShapeBracketSuffix() const;
    void consume(TokenKind kind, std::string_view text, const char* message);
    void consume(TokenKind kind, const char* message);

    std::unique_ptr<Statement> parseStatement();
    std::unique_ptr<Statement> parseUseDecl();
    std::unique_ptr<Statement> parseStreamDecl(bool isStatic = false);
    std::unique_ptr<Statement> parseShapeDecl();
    std::unique_ptr<Statement> parseGateDecl(std::vector<std::string> annotations);
    std::unique_ptr<Expression> parseExpression();
    std::unique_ptr<Expression> parseSingleExpression();
    std::unique_ptr<Expression> parseRoute();
    std::unique_ptr<Expression> parsePrefix();
    std::unique_ptr<Expression> parseProbeCompare();
    std::unique_ptr<Expression> parseCall();
    std::unique_ptr<Expression> finishCall(std::unique_ptr<Expression> callee);
    std::unique_ptr<Expression> parsePrimary();
    std::unique_ptr<Expression> parseFieldExpression();
    std::unique_ptr<Expression> parseFieldExpressionFromBracket();
    std::unique_ptr<Expression> parseStructLiteralFromBracket();
    std::unique_ptr<Expression> parseForkExpression();
    std::unique_ptr<Expression> parseLoopExpression();
    std::string parseShape();
    GateParam parseGateParam();
    ShapeField parseShapeField();
    std::vector<std::unique_ptr<Statement>> parseBlockStatements();

private:
    std::vector<Token> tokens_;
    size_t current_ = 0;
};

} // namespace dagger
