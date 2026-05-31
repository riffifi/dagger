#pragma once

#include "ast.h"
#include "token.h"
#include <memory>
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
    const Token& peekNext() const;
    bool isAtEnd() const;
    const Token& advance();
    bool check(TokenKind kind, std::string_view text = "") const;
    bool checkAt(size_t offset, TokenKind kind, std::string_view text = "") const;
    bool match(TokenKind kind, std::string_view text = "");
    void consume(TokenKind kind, const char* message);
    void consume(TokenKind kind, std::string_view text, const char* message);

    std::unique_ptr<Statement> parseStatement();
    std::unique_ptr<Statement> parseUseDecl();
    std::unique_ptr<Statement> parseStreamDecl(bool isStatic);
    std::unique_ptr<Statement> parseTypeDecl();
    std::unique_ptr<Statement> parseFunctionDecl(std::vector<std::string> annotations);
    std::unique_ptr<Statement> parseDirectiveStmt();

    std::unique_ptr<Expression> parseExpression();
    std::unique_ptr<Expression> parseSingleExpression();
    std::unique_ptr<Expression> parseRoute();
    std::unique_ptr<Expression> parsePrefix();
    std::unique_ptr<Expression> parseProbeCompare();
    std::unique_ptr<Expression> parseCall();
    std::unique_ptr<Expression> finishCall(std::unique_ptr<Expression> callee);
    std::unique_ptr<Expression> parsePrimary();
    std::unique_ptr<Expression> parseBlockExpression();
    std::unique_ptr<Expression> parseBlockExpressionFromBracket(std::string name = "");
    std::unique_ptr<Expression> parseStructLiteralFromBracket();
    std::unique_ptr<Expression> parseForkExpression();
    std::unique_ptr<Expression> parseLoopExpression();
    std::unique_ptr<Expression> parseEachExpression();

    std::vector<std::unique_ptr<Statement>> parseBlockStatements();
    std::string parseType();
    FunctionParam parseFunctionParam();
    TypeField parseTypeField();

    bool isTypeBracketSuffix() const;

private:
    std::vector<Token> tokens_;
    size_t current_ = 0;
};

} // namespace dagger
