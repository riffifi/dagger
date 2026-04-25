#include "parser.h"
#include <iostream>
#include <stdexcept>
#include <utility>

namespace dagger {

Parser::Parser(std::vector<Token> tokens) : tokens_(std::move(tokens)) {}

std::unique_ptr<Program> Parser::parse() {
    auto program = std::make_unique<Program>();
    while (!isAtEnd()) {
        if (check(TokenKind::EndOfFile)) {
            break;
        }
        auto statement = parseStatement();
        if (statement) {
            program->statements.push_back(std::move(statement));
        } else {
            advance();
        }
    }
    return program;
}

const Token& Parser::peek() const {
    return tokens_.at(current_);
}

const Token& Parser::previous() const {
    return tokens_.at(current_ - 1);
}

const Token& Parser::peekNext() const {
    if (current_ + 1 >= tokens_.size()) {
        return tokens_.back();
    }
    return tokens_.at(current_ + 1);
}

bool Parser::isAtEnd() const {
    return peek().kind == TokenKind::EndOfFile;
}

const Token& Parser::advance() {
    if (!isAtEnd()) {
        current_++;
    }
    return previous();
}

bool Parser::checkAt(size_t offset, TokenKind kind, std::string_view text) const {
    if (current_ + offset >= tokens_.size()) {
        return false;
    }
    const Token& token = tokens_.at(current_ + offset);
    if (token.kind != kind) {
        return false;
    }
    if (text.empty()) {
        return true;
    }
    return token.text == text;
}

bool Parser::match(TokenKind kind, std::string_view text) {
    if (check(kind, text)) {
        advance();
        return true;
    }
    return false;
}

bool Parser::check(TokenKind kind, std::string_view text) const {
    if (isAtEnd()) {
        return false;
    }
    if (peek().kind != kind) {
        return false;
    }
    if (text.empty()) {
        return true;
    }
    return peek().text == text;
}

bool Parser::isShapeBracketSuffix() const {
    if (!check(TokenKind::Punctuation, "[")) {
        return false;
    }
    if (current_ == 0) {
        return false;
    }
    const Token& prior = tokens_.at(current_ - 1);
    return peek().line == prior.line &&
           peek().column == prior.column + static_cast<int>(prior.text.size());
}

void Parser::consume(TokenKind kind, std::string_view text, const char* message) {
    if (check(kind, text)) {
        advance();
        return;
    }
    std::cerr << "parser error: " << message << " at line " << peek().line << ", column " << peek().column << "\n";
}

void Parser::consume(TokenKind kind, const char* message) {
    if (check(kind)) {
        advance();
        return;
    }
    std::cerr << "parser error: " << message << " at line " << peek().line << ", column " << peek().column << "\n";
}

std::unique_ptr<Statement> Parser::parseStatement() {
    std::vector<std::string> annotations;
    while (check(TokenKind::Identifier) && !peek().text.empty() && peek().text[0] == '@' &&
           peek().text != "@gate" && peek().text != "@fn" && peek().text != "@shape" && peek().text != "@type" &&
           peek().text != "@use") {
        annotations.push_back(advance().text);
    }

    if (match(TokenKind::Identifier, "@gate") || match(TokenKind::Identifier, "@fn")) {
        return parseGateDecl(std::move(annotations));
    }

    if (match(TokenKind::Identifier, "@use")) {
        return parseUseDecl();
    }

    if (match(TokenKind::Identifier, "@shape") || match(TokenKind::Identifier, "@type")) {
        return parseShapeDecl();
    }

    if (annotations.size() == 1 && annotations[0] == "@static" && match(TokenKind::Operator, "~")) {
        return parseStreamDecl(true);
    }

    if (check(TokenKind::Operator, "~")) {
        advance();
        return parseStreamDecl(false);
    }

    auto expression = parseExpression();
    auto statement = std::make_unique<ExprStmt>();
    statement->expression = std::move(expression);
    return statement;
}

std::unique_ptr<Statement> Parser::parseUseDecl() {
    auto decl = std::make_unique<UseDecl>();
    consume(TokenKind::Identifier, "expected module name after @use");
    decl->moduleName = previous().text;

    if (match(TokenKind::Punctuation, "[")) {
        if (!check(TokenKind::Punctuation, "]")) {
            do {
                consume(TokenKind::Identifier, "expected imported name");
                decl->importedNames.push_back(previous().text);
            } while (match(TokenKind::Punctuation, ","));
        }
        consume(TokenKind::Punctuation, "]", "expected ']' after import list");
    }

    return decl;
}

std::unique_ptr<Statement> Parser::parseStreamDecl(bool isStatic) {
    auto decl = std::make_unique<StreamDecl>();
    decl->isStatic = isStatic;

    do {
        if (!check(TokenKind::Identifier)) {
            consume(TokenKind::Identifier, "expected stream name");
            return nullptr;
        }
        decl->names.push_back(advance().text);
    } while (match(TokenKind::Punctuation, ","));

    if (match(TokenKind::Operator, "::")) {
        decl->typeName = parseShape();
    }

    if (match(TokenKind::Operator, "=")) {
        do {
            decl->initializers.push_back(parseSingleExpression());
        } while (match(TokenKind::Punctuation, ","));
    }

    return decl;
}

std::unique_ptr<Statement> Parser::parseShapeDecl() {
    auto decl = std::make_unique<ShapeDecl>();
    consume(TokenKind::Identifier, "expected shape name");
    decl->name = previous().text;
    consume(TokenKind::Punctuation, "[", "expected '[' after shape name");
    while (!check(TokenKind::Punctuation, "]") && !isAtEnd()) {
        decl->fields.push_back(parseShapeField());
        match(TokenKind::Punctuation, ",");
    }
    consume(TokenKind::Punctuation, "]", "expected ']' after shape body");
    return decl;
}

std::unique_ptr<Statement> Parser::parseGateDecl(std::vector<std::string> annotations) {
    auto decl = std::make_unique<GateDecl>();
    decl->annotations = std::move(annotations);

    if (!check(TokenKind::Identifier)) {
        consume(TokenKind::Identifier, "expected gate name");
        return nullptr;
    }
    decl->name = advance().text;

    consume(TokenKind::Punctuation, "[", "expected '[' after gate name");
    if (!check(TokenKind::Punctuation, "]")) {
        do {
            decl->params.push_back(parseGateParam());
        } while (match(TokenKind::Punctuation, ","));
    }
    consume(TokenKind::Punctuation, "]", "expected ']' after gate parameters");

    if (match(TokenKind::Operator, "=>")) {
        decl->resultType = parseShape();
    }

    decl->body = parseFieldExpression();
    return decl;
}

GateParam Parser::parseGateParam() {
    GateParam param;
    consume(TokenKind::Operator, "~", "expected '~' before param name");
    consume(TokenKind::Identifier, "expected parameter name");
    param.name = previous().text;
    if (match(TokenKind::Operator, "::")) {
        param.typeName = parseShape();
    }
    return param;
}

ShapeField Parser::parseShapeField() {
    ShapeField field;
    consume(TokenKind::Identifier, "expected field name");
    field.name = previous().text;
    if (match(TokenKind::Operator, "::")) {
        field.typeName = parseShape();
    }
    return field;
}

std::string Parser::parseShape() {
    std::string shape;
    if (check(TokenKind::Identifier) || check(TokenKind::Keyword)) {
        shape = advance().text;
    } else {
        consume(TokenKind::Identifier, "expected shape name");
        return std::string();
    }

    while (isShapeBracketSuffix()) {
        advance();
        shape.push_back('[');
        while (!check(TokenKind::Punctuation, "]") && !isAtEnd()) {
            shape += advance().text;
        }
        consume(TokenKind::Punctuation, "]", "expected ']' after shape argument");
        shape.push_back(']');
    }

    return shape;
}

std::unique_ptr<Expression> Parser::parseExpression() {
    return parseSingleExpression();
}

std::unique_ptr<Expression> Parser::parseSingleExpression() {
    return parseRoute();
}

std::unique_ptr<Expression> Parser::parseRoute() {
    auto expression = parsePrefix();
    if (!expression) {
        return nullptr;
    }

    const size_t afterFirstExpression = current_;
    if (match(TokenKind::Punctuation, ",")) {
        std::vector<std::unique_ptr<Expression>> items;
        items.push_back(std::move(expression));
        do {
            items.push_back(parsePrefix());
        } while (match(TokenKind::Punctuation, ","));

        if (check(TokenKind::Operator, "->")) {
            auto list = std::make_unique<ListExpr>();
            list->items = std::move(items);
            expression = std::move(list);
        } else {
            current_ = afterFirstExpression;
            expression = std::move(items.front());
        }
    }

    std::vector<std::unique_ptr<Expression>> stages;
    while (match(TokenKind::Operator, "->")) {
        stages.push_back(parsePrefix());
    }

    if (stages.empty()) {
        return expression;
    }

    auto route = std::make_unique<RouteExpr>();
    route->source = std::move(expression);
    route->stages = std::move(stages);
    return route;
}

std::unique_ptr<Expression> Parser::parsePrefix() {
    if (check(TokenKind::Operator) &&
        (peek().text == "?=" || peek().text == "?!=" || peek().text == "?<" || peek().text == "?<=" ||
         peek().text == "?>" || peek().text == "?>=")) {
        return parseProbeCompare();
    }

    if (match(TokenKind::Operator, "?") || match(TokenKind::Operator, "!")) {
        auto expr = std::make_unique<PrefixExpr>();
        expr->op = previous().text;
        expr->right = parsePrefix();
        return expr;
    }
    return parseCall();
}

std::unique_ptr<Expression> Parser::parseProbeCompare() {
    auto expr = std::make_unique<ProbeCompareExpr>();
    expr->op = advance().text;
    expr->right = parsePrefix();
    return expr;
}

std::unique_ptr<Expression> Parser::parseCall() {
    auto expression = parsePrimary();
    while (match(TokenKind::Punctuation, "(")) {
        expression = finishCall(std::move(expression));
    }
    return expression;
}

std::unique_ptr<Expression> Parser::finishCall(std::unique_ptr<Expression> callee) {
    auto call = std::make_unique<CallExpr>();
    call->callee = std::move(callee);
    if (!check(TokenKind::Punctuation, ")")) {
        do {
            call->arguments.push_back(parseSingleExpression());
        } while (match(TokenKind::Punctuation, ","));
    }
    consume(TokenKind::Punctuation, ")", "expected ')' after call arguments");
    return call;
}

static int64_t parseIntegerLiteral(std::string_view text) {
    bool negative = !text.empty() && text.front() == '-';
    size_t start = negative ? 1 : 0;
    int base = 10;

    if (text.substr(start, 2) == "0x" || text.substr(start, 2) == "0X") {
        base = 16;
        start += 2;
    } else if (text.substr(start, 2) == "0b" || text.substr(start, 2) == "0B") {
        base = 2;
        start += 2;
    } else if (text.substr(start, 2) == "0o" || text.substr(start, 2) == "0O") {
        base = 8;
        start += 2;
    }

    int64_t value = 0;
    for (char c : text.substr(start)) {
        int digit = 0;
        if (c >= '0' && c <= '9') {
            digit = c - '0';
        } else if (c >= 'a' && c <= 'f') {
            digit = 10 + (c - 'a');
        } else if (c >= 'A' && c <= 'F') {
            digit = 10 + (c - 'A');
        } else {
            throw std::runtime_error("invalid integer literal");
        }
        if (digit >= base) {
            throw std::runtime_error("invalid digit for integer base");
        }
        value = (value * base) + digit;
    }

    return negative ? -value : value;
}

std::unique_ptr<Expression> Parser::parsePrimary() {
    if (match(TokenKind::IntegerLiteral)) {
        auto literal = std::make_unique<LiteralExpr>();
        literal->value = parseIntegerLiteral(previous().text);
        return literal;
    }

    if (match(TokenKind::FloatLiteral)) {
        auto literal = std::make_unique<LiteralExpr>();
        literal->value = std::stod(previous().text);
        return literal;
    }

    if (match(TokenKind::StringLiteral)) {
        auto literal = std::make_unique<LiteralExpr>();
        literal->value = previous().text;
        return literal;
    }

    if (match(TokenKind::CharLiteral)) {
        auto literal = std::make_unique<LiteralExpr>();
        if (!previous().text.empty()) {
            literal->value = previous().text[0];
        } else {
            literal->value = '\0';
        }
        return literal;
    }

    if (match(TokenKind::Keyword, "true")) {
        auto literal = std::make_unique<LiteralExpr>();
        literal->value = true;
        return literal;
    }

    if (match(TokenKind::Keyword, "false")) {
        auto literal = std::make_unique<LiteralExpr>();
        literal->value = false;
        return literal;
    }

    if (match(TokenKind::Keyword, "null")) {
        return std::make_unique<LiteralExpr>();
    }

    if (match(TokenKind::Identifier, "field") || match(TokenKind::Keyword, "field") ||
        match(TokenKind::Identifier, "block") || match(TokenKind::Keyword, "block")) {
        return parseFieldExpression();
    }

    if (match(TokenKind::Identifier, "fork") || match(TokenKind::Keyword, "fork")) {
        return parseForkExpression();
    }

    if (match(TokenKind::Identifier, "loop") || match(TokenKind::Keyword, "loop")) {
        return parseLoopExpression();
    }

    if (match(TokenKind::Punctuation, "(")) {
        auto expression = parseExpression();
        consume(TokenKind::Punctuation, ")", "expected ')' after expression");
        return expression;
    }

    if (match(TokenKind::Punctuation, "[")) {
        if (check(TokenKind::Identifier) && checkAt(1, TokenKind::Operator, "=")) {
            return parseStructLiteralFromBracket();
        }
        return parseFieldExpressionFromBracket();
    }

    if (match(TokenKind::Operator, "~")) {
        auto capture = std::make_unique<CaptureExpr>();
        consume(TokenKind::Identifier, "expected stream name after '~'");
        capture->name = previous().text;
        if (match(TokenKind::Operator, "::")) {
            capture->typeName = parseShape();
        }
        return capture;
    }

    if (check(TokenKind::Identifier)) {
        if (peek().text == "_") {
            advance();
            return std::make_unique<WildcardExpr>();
        }
        auto identifier = std::make_unique<IdentifierExpr>();
        identifier->name = advance().text;
        return identifier;
    }

    std::cerr << "parser error: unexpected token '" << peek().text << "' at line " << peek().line << ", column " << peek().column << "\n";
    advance();
    return nullptr;
}

std::unique_ptr<Expression> Parser::parseFieldExpression() {
    if (match(TokenKind::Punctuation, "[")) {
        return parseFieldExpressionFromBracket();
    }
    std::cerr << "parser error: expected field expression\n";
    return nullptr;
}

std::unique_ptr<Expression> Parser::parseFieldExpressionFromBracket() {
    auto field = std::make_unique<FieldExpr>();
    bool hasExplicitParam = false;

    if (check(TokenKind::Operator, "~")) {
        hasExplicitParam = true;
        advance();
        if (!check(TokenKind::Identifier)) {
            consume(TokenKind::Identifier, "expected input name after '~' in field");
            return nullptr;
        }
        field->inputName = advance().text;
        if (match(TokenKind::Operator, "::")) {
            field->inputTypeName = parseShape();
        }
        consume(TokenKind::Punctuation, "]", "expected ']' after field parameter");
        consume(TokenKind::Punctuation, "[", "expected '[' before field body");
    }

    if (!hasExplicitParam) {
        field->inputName = "__input";
    }

    field->body = parseBlockStatements();
    consume(TokenKind::Punctuation, "]", "expected ']' after field body");
    return field;
}

std::unique_ptr<Expression> Parser::parseStructLiteralFromBracket() {
    auto literal = std::make_unique<StructLiteralExpr>();
    while (!check(TokenKind::Punctuation, "]") && !isAtEnd()) {
        StructFieldInit field;
        consume(TokenKind::Identifier, "expected field name in struct literal");
        field.name = previous().text;
        consume(TokenKind::Operator, "=", "expected '=' after struct field name");
        field.value = parseSingleExpression();
        literal->fields.push_back(std::move(field));
        if (!check(TokenKind::Punctuation, "]")) {
            match(TokenKind::Punctuation, ",");
        }
    }
    consume(TokenKind::Punctuation, "]", "expected ']' after struct literal");
    return literal;
}

std::unique_ptr<Expression> Parser::parseForkExpression() {
    auto fork = std::make_unique<ForkExpr>();
    consume(TokenKind::Punctuation, "[", "expected '[' after fork");
    while (!check(TokenKind::Punctuation, "]") && !isAtEnd()) {
        auto arm = ForkArm();
        arm.condition = parsePrefix();
        consume(TokenKind::Operator, "->", "expected '->' in fork arm");
        arm.body = parseExpression();
        fork->arms.push_back(std::move(arm));
        if (!check(TokenKind::Punctuation, "]")) {
            match(TokenKind::Punctuation, ",");
        }
    }
    consume(TokenKind::Punctuation, "]", "expected ']' after fork");
    return fork;
}

std::unique_ptr<Expression> Parser::parseLoopExpression() {
    consume(TokenKind::Punctuation, "[", "expected '[' after loop");
    auto condition = parseExpression();
    consume(TokenKind::Punctuation, "]", "expected ']' after loop condition");
    auto body = parseExpression();
    auto loop = std::make_unique<LoopExpr>();
    loop->condition = std::move(condition);
    loop->body = std::move(body);
    return loop;
}

std::vector<std::unique_ptr<Statement>> Parser::parseBlockStatements() {
    std::vector<std::unique_ptr<Statement>> statements;
    while (!check(TokenKind::Punctuation, "]") && !isAtEnd()) {
        if (match(TokenKind::Punctuation, ";")) {
            continue;
        }
        statements.push_back(parseStatement());
    }
    return statements;
}

} // namespace dagger
