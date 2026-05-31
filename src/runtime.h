#pragma once

#include "ast.h"
#include <functional>
#include <map>
#include <memory>
#include <optional>
#include <string>
#include <variant>
#include <vector>

namespace dagger {

struct Value {
    enum class Kind {
        Null,
        Int,
        Float,
        Bool,
        Char,
        Text,
        List,
        Object,
    };

    Kind kind = Kind::Null;
    int64_t intValue = 0;
    double floatValue = 0.0;
    bool boolValue = false;
    char charValue = '\0';
    std::string textValue;
    std::vector<Value> listValue;
    std::map<std::string, Value> objectValue;
    std::optional<std::string> typeName;

    Value() = default;
    explicit Value(int64_t value);
    explicit Value(double value);
    explicit Value(bool value);
    explicit Value(char value);
    explicit Value(std::string value);
    explicit Value(std::vector<Value> value);
    Value(std::map<std::string, Value> value, std::optional<std::string> typeName);

    bool isNull() const;
    bool isTruthy() const;
    std::string toString() const;

    int64_t asInt() const;
    double asFloat() const;
    std::string asText() const;
};

struct EvalContext {
    EvalContext* parent = nullptr;
    std::map<std::string, Value> variables;
    std::map<std::string, std::optional<std::string>> variableTypes;
    std::map<std::string, FunctionDecl*> functions;
    std::map<std::string, TypeDecl*> types;

    EvalContext() = default;
    EvalContext(EvalContext* p) : parent(p) {}

    std::optional<Value> lookupVariable(std::string_view name) const;
    std::optional<std::string> lookupVariableType(std::string_view name) const;
    FunctionDecl* lookupFunction(std::string_view name) const;
    TypeDecl* lookupType(std::string_view name) const;

    void defineVariable(std::string name, Value value, std::optional<std::string> typeName = std::nullopt);
    bool assignVariable(std::string_view name, Value value);
    void defineFunction(std::string name, FunctionDecl* func);
    void defineType(std::string name, TypeDecl* type);
};

class Interpreter {
public:
    Interpreter();
    Value execute(const Program& program, bool printResult = false);

    Value evalStatement(const Statement& statement, EvalContext& context);
    Value evalExpression(const Expression& expression, EvalContext& context);

private:
    Value evalRoute(const RouteExpr& route, EvalContext& context);
    Value evalRouteStage(const Expression& stage, const Value& current, EvalContext& context);
    Value evalBlock(const BlockExpr& block, const Value& input, EvalContext& context);
    Value evalFork(const ForkExpr& fork, const Value& input, EvalContext& context);
    Value evalLoop(const LoopExpr& loop, const Value& input, EvalContext& context);
    Value evalEach(const EachExpr& each, const Value& input, EvalContext& context);

    Value callFunction(std::string_view name, const std::vector<Value>& args, EvalContext& context);
    Value callUserFunction(const FunctionDecl& func, const std::vector<Value>& args, EvalContext& context);

    void registerBuiltins();

    EvalContext rootContext_;
    std::map<std::string, std::function<Value(const std::vector<Value>&)>> builtins_;
};

} // namespace dagger
