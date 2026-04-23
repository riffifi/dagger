#pragma once

#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <variant>
#include <vector>

namespace dagger {

struct AstNode {
    virtual ~AstNode() = default;
};

struct Statement : AstNode {
};

struct Expression : AstNode {
};

struct Program : AstNode {
    std::vector<std::unique_ptr<Statement>> statements;
};

struct LiteralExpr : Expression {
    std::variant<std::monostate, int64_t, double, bool, char, std::string> value;
};

struct IdentifierExpr : Expression {
    std::string name;
};

struct PrefixExpr : Expression {
    std::string op;
    std::unique_ptr<Expression> right;
};

struct ProbeCompareExpr : Expression {
    std::string op;
    std::unique_ptr<Expression> right;
};

struct ListExpr : Expression {
    std::vector<std::unique_ptr<Expression>> items;
};

struct StructFieldInit {
    std::string name;
    std::unique_ptr<Expression> value;
};

struct StructLiteralExpr : Expression {
    std::vector<StructFieldInit> fields;
};

struct CallExpr : Expression {
    std::unique_ptr<Expression> callee;
    std::vector<std::unique_ptr<Expression>> arguments;
};

struct CaptureExpr : Expression {
    std::string name;
    std::optional<std::string> typeName;
};

struct RouteExpr : Expression {
    std::unique_ptr<Expression> source;
    std::vector<std::unique_ptr<Expression>> stages;
};

struct FieldExpr : Expression {
    std::optional<std::string> inputName;
    std::vector<std::unique_ptr<Statement>> body;
};

struct ForkArm {
    std::unique_ptr<Expression> condition;
    std::unique_ptr<Expression> body;
};

struct ForkExpr : Expression {
    std::vector<ForkArm> arms;
};

struct LoopExpr : Expression {
    std::unique_ptr<Expression> condition;
    std::unique_ptr<Expression> body;
};

struct WildcardExpr : Expression {
};

struct StreamDecl : Statement {
    bool isStatic = false;
    std::vector<std::string> names;
    std::optional<std::string> typeName;
    std::vector<std::unique_ptr<Expression>> initializers;
};

struct GateParam {
    std::string name;
    std::optional<std::string> typeName;
};

struct ShapeField {
    std::string name;
    std::optional<std::string> typeName;
};

struct ShapeDecl : Statement {
    std::string name;
    std::vector<ShapeField> fields;
};

struct GateDecl : Statement {
    std::string name;
    std::vector<std::string> annotations;
    std::vector<GateParam> params;
    std::optional<std::string> resultType;
    std::unique_ptr<Expression> body;
};

struct ExprStmt : Statement {
    std::unique_ptr<Expression> expression;
};

} // namespace dagger
