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

bool Parser::isTypeBracketSuffix() const {
    if (!check(TokenKind::Punctuation, "[")) {
        return false;
    }
    if (current_ == 0) {
        return false;
    }
    const Token& prior = tokens_.at(current_ - 1);
    // In Dagger, type arguments like block[256] must be immediately adjacent
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
    while (check(TokenKind::Keyword) && peek().text.size() > 0 && peek().text[0] == '@' &&
           peek().text != "@fn" && peek().text != "@type" && peek().text != "@use") {
        annotations.push_back(advance().text);
    }

    if (match(TokenKind::Keyword, "@fn")) {
        return parseFunctionDecl(std::move(annotations));
    }

    if (match(TokenKind::Keyword, "@use")) {
        return parseUseDecl();
    }

    if (match(TokenKind::Keyword, "@type")) {
        return parseTypeDecl();
    }

    if (annotations.size() == 1 && annotations[0] == "@static" && check(TokenKind::Operator, "~")) {
        advance();
        return parseStreamDecl(true);
    }
    
    if (check(TokenKind::Operator, "~")) {
        advance();
        return parseStreamDecl(false);
    }

    if (check(TokenKind::Keyword) && peek().text.size() > 0 && peek().text[0] == '@') {
        return parseDirectiveStmt();
    }

    auto expression = parseExpression();
    auto statement = std::make_unique<ExprStmt>();
    statement->expression = std::move(expression);
    return statement;
}

std::unique_ptr<Statement> Parser::parseDirectiveStmt() {
    auto decl = std::make_unique<DirectiveStmt>();
    decl->directive = advance().text;
    
    // Some directives might take arguments in parentheses, like @error("tag")
    if (match(TokenKind::Punctuation, "(")) {
        if (!check(TokenKind::Punctuation, ")")) {
            do {
                decl->arguments.push_back(parseSingleExpression());
            } while (match(TokenKind::Punctuation, ","));
        }
        consume(TokenKind::Punctuation, ")", "expected ')' after directive arguments");
    }
    
    return decl;
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
        decl->typeName = parseType();
        if ((check(TokenKind::Keyword) || check(TokenKind::Identifier)) && peek().text.size() > 1 && peek().text[0] == '@') {
             decl->registerPin = advance().text;
        }
    }

    if (match(TokenKind::Operator, "=")) {
        do {
            decl->initializers.push_back(parseSingleExpression());
        } while (match(TokenKind::Punctuation, ","));
    }

    return decl;
}

std::unique_ptr<Statement> Parser::parseTypeDecl() {
    auto decl = std::make_unique<TypeDecl>();
    consume(TokenKind::Identifier, "expected type name");
    decl->name = previous().text;
    consume(TokenKind::Punctuation, "[", "expected '[' after type name");
    
    if (check(TokenKind::Operator, "|")) {
        decl->isUnion = true;
        while (match(TokenKind::Operator, "|")) {
            TypeField field;
            field.typeName = parseType();
            decl->fields.push_back(std::move(field));
            // Optional newline/comma handling can be added here
            match(TokenKind::Punctuation, ",");
        }
    } else {
        while (!check(TokenKind::Punctuation, "]") && !isAtEnd()) {
            decl->fields.push_back(parseTypeField());
            match(TokenKind::Punctuation, ",");
        }
    }
    
    consume(TokenKind::Punctuation, "]", "expected ']' after type body");
    return decl;
}

std::unique_ptr<Statement> Parser::parseFunctionDecl(std::vector<std::string> annotations) {
    auto decl = std::make_unique<FunctionDecl>();
    decl->annotations = std::move(annotations);

    if (!check(TokenKind::Identifier)) {
        consume(TokenKind::Identifier, "expected function name");
        return nullptr;
    }
    decl->name = advance().text;

    consume(TokenKind::Punctuation, "[", "expected '[' after function name");
    if (!check(TokenKind::Punctuation, "]")) {
        do {
            decl->params.push_back(parseFunctionParam());
        } while (match(TokenKind::Punctuation, ","));
    }
    consume(TokenKind::Punctuation, "]", "expected ']' after function parameters");

    if (match(TokenKind::Operator, "=>")) {
        decl->resultType = parseType();
    }

    if (match(TokenKind::Punctuation, "[")) {
        auto body = std::make_unique<BlockExpr>();
        body->body = parseBlockStatements();
        consume(TokenKind::Punctuation, "]", "expected ']' after function body");
        decl->body = std::move(body);
    } else {
        bool isExtern = false;
        for (const auto& ann : decl->annotations) {
            if (ann == "@extern" || ann == "@syscall") {
                isExtern = true;
                break;
            }
        }
        if (!isExtern) {
            consume(TokenKind::Punctuation, "[", "expected '[' before function body");
        }
    }

    return decl;
    }

FunctionParam Parser::parseFunctionParam() {
    FunctionParam param;
    consume(TokenKind::Operator, "~", "expected '~' before param name");
    consume(TokenKind::Identifier, "expected parameter name");
    param.name = previous().text;
    if (match(TokenKind::Operator, "::")) {
        param.typeName = parseType();
    }
    return param;
}

TypeField Parser::parseTypeField() {
    TypeField field;
    consume(TokenKind::Identifier, "expected field name");
    field.name = previous().text;
    if (match(TokenKind::Operator, "::")) {
        field.typeName = parseType();
    }
    return field;
}

std::string Parser::parseType() {
    std::string type;
    if (check(TokenKind::Identifier) || check(TokenKind::Keyword)) {
        type = advance().text;
    } else {
        consume(TokenKind::Identifier, "expected type name");
        return std::string();
    }

    while (isTypeBracketSuffix()) {
        advance();
        type.push_back('[');
        while (!check(TokenKind::Punctuation, "]") && !isAtEnd()) {
            type += advance().text;
        }
        consume(TokenKind::Punctuation, "]", "expected ']' after type argument");
        type.push_back(']');
    }
    
    if (match(TokenKind::Operator, "?")) {
        type.push_back('?');
    }
    if (match(TokenKind::Operator, "!")) {
        type.push_back('!');
        consume(TokenKind::Identifier, "expected error tag after '!'");
        type += previous().text;
    }

    return type;
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
    
    if (match(TokenKind::Operator, "&") || match(TokenKind::Operator, "*")) {
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

    if (text.size() > start + 2) {
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
            continue; // Skip separators if any, or handle errors
        }
        if (digit >= base) continue;
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

    if (check(TokenKind::Keyword, "block")) {
        return parseBlockExpression();
    }

    if (match(TokenKind::Keyword, "fork")) {
        return parseForkExpression();
    }

    if (match(TokenKind::Keyword, "loop")) {
        if (checkAt(0, TokenKind::Identifier, "range")) {
             return parseLoopExpression(); // Special handling for loop.range
        }
        return parseLoopExpression();
    }
    
    if (match(TokenKind::Identifier, "loop.range")) {
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
        return parseBlockExpressionFromBracket();
    }

    if (match(TokenKind::Operator, "~")) {
        auto capture = std::make_unique<CaptureExpr>();
        consume(TokenKind::Identifier, "expected stream name after '~'");
        capture->name = previous().text;
        if (match(TokenKind::Operator, "::")) {
            capture->typeName = parseType();
        }
        return capture;
    }

    if (check(TokenKind::Identifier)) {
        if (peek().text == "_") {
            advance();
            return std::make_unique<WildcardExpr>();
        }
        if (peek().text == "each") {
            return parseEachExpression();
        }
        auto identifier = std::make_unique<IdentifierExpr>();
        identifier->name = advance().text;
        return identifier;
    }

    std::cerr << "parser error: unexpected token '" << peek().text << "' at line " << peek().line << ", column " << peek().column << "\n";
    advance();
    return nullptr;
}

std::unique_ptr<Expression> Parser::parseBlockExpression() {
    consume(TokenKind::Keyword, "block", "expected 'block'");
    std::string name;
    if (check(TokenKind::Identifier) && peek().text.size() > 0 && peek().text[0] == '.') {
        name = advance().text;
    }
    consume(TokenKind::Punctuation, "[", "expected '[' after block");
    return parseBlockExpressionFromBracket(name);
}

std::unique_ptr<Expression> Parser::parseBlockExpressionFromBracket(std::string name) {
    auto block = std::make_unique<BlockExpr>();
    block->name = std::move(name);

    bool hasParam = false;
    if (check(TokenKind::Operator, "~")) {
        // Look ahead to see if this is followed by ] then [
        size_t offset = 1;
        while (current_ + offset < tokens_.size() && tokens_[current_ + offset].kind != TokenKind::Punctuation) {
            offset++;
        }
        if (current_ + offset + 1 < tokens_.size() && 
            tokens_[current_ + offset].kind == TokenKind::Punctuation && tokens_[current_ + offset].text == "]" &&
            tokens_[current_ + offset + 1].kind == TokenKind::Punctuation && tokens_[current_ + offset + 1].text == "[") {
            hasParam = true;
        }
    }

    if (hasParam) {
        advance(); // ~
        consume(TokenKind::Identifier, "expected input name after '~' in block");
        block->inputName = previous().text;
        if (match(TokenKind::Operator, "::")) {
            block->inputTypeName = parseType();
        }
        consume(TokenKind::Punctuation, "]", "expected ']' after block parameter");
        consume(TokenKind::Punctuation, "[", "expected '[' before block body");
    }
    block->body = parseBlockStatements();
    consume(TokenKind::Punctuation, "]", "expected ']' after block body");

    return block;
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
        if (match(TokenKind::Keyword, "@ok") || match(TokenKind::Keyword, "@err")) {
             auto ident = std::make_unique<IdentifierExpr>();
             ident->name = previous().text;
             arm.condition = std::move(ident);
        } else {
             arm.condition = parsePrefix();
        }
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
    auto loop = std::make_unique<LoopExpr>();
    consume(TokenKind::Punctuation, "[", "expected '[' after loop condition");
    loop->condition = parseExpression();
    consume(TokenKind::Punctuation, "]", "expected ']' after loop condition");
    consume(TokenKind::Punctuation, "[", "expected '[' before loop body");
    auto body = std::make_unique<BlockExpr>();
    body->body = parseBlockStatements();
    consume(TokenKind::Punctuation, "]", "expected ']' after loop body");
    loop->body = std::move(body);
    return loop;
}

std::unique_ptr<Expression> Parser::parseEachExpression() {
    auto each = std::make_unique<EachExpr>();
    consume(TokenKind::Identifier, "each", "expected 'each'");
    consume(TokenKind::Operator, "->", "expected '->' after each");
    consume(TokenKind::Punctuation, "[", "expected '[' for item binding");
    consume(TokenKind::Operator, "~", "expected '~' before item name");
    consume(TokenKind::Identifier, "expected item name");
    each->itemName = previous().text;
    consume(TokenKind::Punctuation, "]", "expected ']' after item binding");
    
    consume(TokenKind::Punctuation, "[", "expected '[' before each body");
    auto body = std::make_unique<BlockExpr>();
    body->body = parseBlockStatements();
    consume(TokenKind::Punctuation, "]", "expected ']' after each body");
    each->body = std::move(body);
    
    return each;
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
