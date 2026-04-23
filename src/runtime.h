#pragma once

#include "ast.h"
#include <functional>
#include <map>
#include <memory>
#include <optional>
#include <string>
#include <vector>

namespace dagger {

struct ShapeDecl;

struct Value {
    enum class Kind { Null, Int, Float, Bool, Text, Char, List, Object } kind = Kind::Null;
    int64_t intValue = 0;
    double floatValue = 0.0;
    bool boolValue = false;
    char charValue = '\0';
    std::string textValue;
    std::vector<Value> listValue;
    std::map<std::string, Value> objectValue;
    std::optional<std::string> shapeName;

    Value() = default;
    explicit Value(int64_t value);
    explicit Value(double value);
    explicit Value(bool value);
    explicit Value(char value);
    explicit Value(std::string value);
    explicit Value(std::vector<Value> value);
    explicit Value(std::map<std::string, Value> value, std::optional<std::string> shape = std::nullopt);

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
    std::map<std::string, GateDecl*> gates;
    std::map<std::string, ShapeDecl*> shapes;

    std::optional<Value> lookupVariable(std::string_view name) const;
    GateDecl* lookupGate(std::string_view name) const;
    ShapeDecl* lookupShape(std::string_view name) const;
    void defineVariable(std::string name, Value value);
    bool assignVariable(std::string_view name, Value value);
    void defineGate(std::string name, GateDecl* gate);
    void defineShape(std::string name, ShapeDecl* shape);
};

class Interpreter {
public:
    Interpreter();
    Value execute(const Program& program, bool printResult = false);

private:
    using BuiltinFn = std::function<Value(const std::vector<Value>&)>;

    EvalContext rootContext_;
    std::map<std::string, BuiltinFn> builtins_;

    void registerBuiltins();
    Value evalStatement(const Statement& statement, EvalContext& context);
    Value evalExpression(const Expression& expression, EvalContext& context);
    Value evalRoute(const RouteExpr& route, EvalContext& context);
    Value evalRouteStage(const Expression& stage, const Value& current, EvalContext& context);
    Value evalField(const FieldExpr& field, const Value& input, EvalContext& context);
    Value evalFork(const ForkExpr& fork, const Value& input, EvalContext& context);
    Value evalLoop(const LoopExpr& loop, const Value& input, EvalContext& context);
    Value callFunction(std::string_view name, const std::vector<Value>& args, EvalContext& context);
    Value callUserGate(const GateDecl& gate, const std::vector<Value>& args, EvalContext& context);
};

} // namespace dagger
