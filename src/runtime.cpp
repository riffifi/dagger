#include "runtime.h"
#include <cmath>
#include <iostream>
#include <set>
#include <sstream>
#include <stdexcept>

namespace dagger {

Value::Value(int64_t value) : kind(Kind::Int), intValue(value) {}
Value::Value(double value) : kind(Kind::Float), floatValue(value) {}
Value::Value(bool value) : kind(Kind::Bool), boolValue(value) {}
Value::Value(char value) : kind(Kind::Char), charValue(value) {}
Value::Value(std::string value) : kind(Kind::Text), textValue(std::move(value)) {}
Value::Value(std::vector<Value> value) : kind(Kind::List), listValue(std::move(value)) {}
Value::Value(std::map<std::string, Value> value, std::optional<std::string> shape)
    : kind(Kind::Object), objectValue(std::move(value)), shapeName(std::move(shape)) {}

bool Value::isNull() const {
    return kind == Kind::Null;
}

bool Value::isTruthy() const {
    switch (kind) {
        case Kind::Null:
            return false;
        case Kind::Bool:
            return boolValue;
        case Kind::Int:
            return intValue != 0;
        case Kind::Float:
            return floatValue != 0.0;
        case Kind::Text:
            return !textValue.empty();
        case Kind::Char:
            return charValue != '\0';
        case Kind::List:
            return !listValue.empty();
        case Kind::Object:
            return true;
    }
    return false;
}

std::string Value::toString() const {
    switch (kind) {
        case Kind::Null:
            return "null";
        case Kind::Bool:
            return boolValue ? "true" : "false";
        case Kind::Int:
            return std::to_string(intValue);
        case Kind::Float: {
            std::ostringstream out;
            out << floatValue;
            return out.str();
        }
        case Kind::Text:
            return textValue;
        case Kind::Char:
            return std::string(1, charValue);
        case Kind::List: {
            std::ostringstream out;
            out << "[";
            for (size_t i = 0; i < listValue.size(); ++i) {
                if (i > 0) {
                    out << ", ";
                }
                out << listValue[i].toString();
            }
            out << "]";
            return out.str();
        }
        case Kind::Object: {
            std::ostringstream out;
            if (shapeName) {
                out << *shapeName << " ";
            }
            out << "[";
            bool first = true;
            for (const auto& [name, value] : objectValue) {
                if (!first) {
                    out << ", ";
                }
                first = false;
                out << name << " = " << value.toString();
            }
            out << "]";
            return out.str();
        }
    }
    return "";
}

int64_t Value::asInt() const {
    switch (kind) {
        case Kind::Int:
            return intValue;
        case Kind::Float:
            return static_cast<int64_t>(floatValue);
        case Kind::Bool:
            return boolValue ? 1 : 0;
        case Kind::Text:
            return std::stoll(textValue);
        case Kind::Char:
            return static_cast<int64_t>(charValue);
        case Kind::List:
            if (listValue.size() == 1) {
                return listValue.front().asInt();
            }
            break;
        case Kind::Object:
            break;
        default:
            throw std::runtime_error("cannot convert null to int");
    }
    throw std::runtime_error("cannot convert list to int");
}

double Value::asFloat() const {
    switch (kind) {
        case Kind::Float:
            return floatValue;
        case Kind::Int:
            return static_cast<double>(intValue);
        case Kind::Bool:
            return boolValue ? 1.0 : 0.0;
        case Kind::Text:
            return std::stod(textValue);
        case Kind::Char:
            return static_cast<double>(charValue);
        case Kind::List:
            if (listValue.size() == 1) {
                return listValue.front().asFloat();
            }
            break;
        case Kind::Object:
            break;
        default:
            throw std::runtime_error("cannot convert null to float");
    }
    throw std::runtime_error("cannot convert list to float");
}

std::string Value::asText() const {
    switch (kind) {
        case Kind::Text:
            return textValue;
        case Kind::Char:
            return std::string(1, charValue);
        case Kind::List:
            if (listValue.size() == 1) {
                return listValue.front().asText();
            }
            return toString();
        case Kind::Object:
            return toString();
        default:
            return toString();
    }
}

static std::vector<Value> flattenValue(const Value& value) {
    if (value.kind == Value::Kind::List) {
        return value.listValue;
    }
    if (value.isNull()) {
        return {};
    }
    return {value};
}

static bool isBuiltinTypeName(std::string_view typeName) {
    static const std::set<std::string, std::less<>> builtinTypes{
        "int",
        "int8",
        "int16",
        "int32",
        "int64",
        "uint",
        "uint8",
        "uint16",
        "uint32",
        "uint64",
        "byte",
        "float",
        "float32",
        "float64",
        "bool",
        "text",
        "char",
        "null",
    };
    return builtinTypes.contains(typeName);
}

static bool isIntegerTypeName(std::string_view typeName) {
    static const std::set<std::string, std::less<>> integerTypes{
        "int",
        "int8",
        "int16",
        "int32",
        "int64",
        "uint",
        "uint8",
        "uint16",
        "uint32",
        "uint64",
        "byte",
    };
    return integerTypes.contains(typeName);
}

static bool isFloatTypeName(std::string_view typeName) {
    return typeName == "float" || typeName == "float32" || typeName == "float64";
}

static std::string valueKindName(const Value& value) {
    switch (value.kind) {
        case Value::Kind::Null:
            return "null";
        case Value::Kind::Int:
            return "int";
        case Value::Kind::Float:
            return "float";
        case Value::Kind::Bool:
            return "bool";
        case Value::Kind::Text:
            return "text";
        case Value::Kind::Char:
            return "char";
        case Value::Kind::List:
            return "list";
        case Value::Kind::Object:
            return value.shapeName.value_or("object");
    }
    return "unknown";
}

static void requireKnownType(const EvalContext& context, std::string_view typeName) {
    if (isBuiltinTypeName(typeName) || context.lookupShape(typeName)) {
        return;
    }
    throw std::runtime_error("unknown type: " + std::string(typeName));
}

static void validateValueAgainstType(const Value& value, std::string_view typeName, const EvalContext& context);

static void validateObjectAgainstShape(const Value& value, const ShapeDecl& shape, const EvalContext& context) {
    if (value.kind != Value::Kind::Object) {
        throw std::runtime_error("type mismatch: expected " + shape.name + ", got " + valueKindName(value));
    }

    for (const auto& field : shape.fields) {
        auto it = value.objectValue.find(field.name);
        if (it == value.objectValue.end()) {
            throw std::runtime_error("type mismatch: missing field '" + field.name + "' for " + shape.name);
        }
        if (field.typeName) {
            validateValueAgainstType(it->second, *field.typeName, context);
        }
    }

    for (const auto& [fieldName, _] : value.objectValue) {
        bool known = false;
        for (const auto& field : shape.fields) {
            if (field.name == fieldName) {
                known = true;
                break;
            }
        }
        if (!known) {
            throw std::runtime_error("type mismatch: unknown field '" + fieldName + "' for " + shape.name);
        }
    }
}

static void validateValueAgainstType(const Value& value, std::string_view typeName, const EvalContext& context) {
    requireKnownType(context, typeName);

    if (isIntegerTypeName(typeName)) {
        if (value.kind != Value::Kind::Int) {
            throw std::runtime_error("type mismatch: expected " + std::string(typeName) + ", got " + valueKindName(value));
        }
        return;
    }
    if (isFloatTypeName(typeName)) {
        if (value.kind != Value::Kind::Float) {
            throw std::runtime_error("type mismatch: expected " + std::string(typeName) + ", got " + valueKindName(value));
        }
        return;
    }
    if (typeName == "bool") {
        if (value.kind != Value::Kind::Bool) {
            throw std::runtime_error("type mismatch: expected bool, got " + valueKindName(value));
        }
        return;
    }
    if (typeName == "text") {
        if (value.kind != Value::Kind::Text) {
            throw std::runtime_error("type mismatch: expected text, got " + valueKindName(value));
        }
        return;
    }
    if (typeName == "char") {
        if (value.kind != Value::Kind::Char) {
            throw std::runtime_error("type mismatch: expected char, got " + valueKindName(value));
        }
        return;
    }
    if (typeName == "null") {
        if (value.kind != Value::Kind::Null) {
            throw std::runtime_error("type mismatch: expected null, got " + valueKindName(value));
        }
        return;
    }

    auto* shape = context.lookupShape(typeName);
    if (!shape) {
        throw std::runtime_error("unknown type: " + std::string(typeName));
    }
    validateObjectAgainstShape(value, *shape, context);
}

std::optional<Value> EvalContext::lookupVariable(std::string_view name) const {
    auto it = variables.find(std::string(name));
    if (it != variables.end()) {
        return it->second;
    }
    if (parent) {
        return parent->lookupVariable(name);
    }
    return std::nullopt;
}

std::optional<std::string> EvalContext::lookupVariableType(std::string_view name) const {
    auto it = variableTypes.find(std::string(name));
    if (it != variableTypes.end()) {
        return it->second;
    }
    if (parent) {
        return parent->lookupVariableType(name);
    }
    return std::nullopt;
}

GateDecl* EvalContext::lookupGate(std::string_view name) const {
    auto it = gates.find(std::string(name));
    if (it != gates.end()) {
        return it->second;
    }
    if (parent) {
        return parent->lookupGate(name);
    }
    return nullptr;
}

ShapeDecl* EvalContext::lookupShape(std::string_view name) const {
    auto it = shapes.find(std::string(name));
    if (it != shapes.end()) {
        return it->second;
    }
    if (parent) {
        return parent->lookupShape(name);
    }
    return nullptr;
}

void EvalContext::defineVariable(std::string name, Value value, std::optional<std::string> typeName) {
    variableTypes[name] = typeName;
    variables[std::move(name)] = std::move(value);
}

bool EvalContext::assignVariable(std::string_view name, Value value) {
    auto it = variables.find(std::string(name));
    if (it != variables.end()) {
        it->second = std::move(value);
        return true;
    }
    if (parent) {
        return parent->assignVariable(name, std::move(value));
    }
    return false;
}

void EvalContext::defineGate(std::string name, GateDecl* gate) {
    gates[std::move(name)] = gate;
}

void EvalContext::defineShape(std::string name, ShapeDecl* shape) {
    shapes[std::move(name)] = shape;
}

static std::optional<Value> lookupDottedField(const EvalContext& context, std::string_view name) {
    const size_t firstDot = name.find('.');
    if (firstDot == std::string_view::npos) {
        return std::nullopt;
    }

    auto current = context.lookupVariable(name.substr(0, firstDot));
    if (!current) {
        return std::nullopt;
    }

    size_t offset = firstDot + 1;
    while (offset <= name.size()) {
        const size_t nextDot = name.find('.', offset);
        const std::string_view part = name.substr(offset, nextDot == std::string_view::npos ? name.size() - offset : nextDot - offset);
        if (current->kind != Value::Kind::Object) {
            throw std::runtime_error("field access on non-object value");
        }
        auto it = current->objectValue.find(std::string(part));
        if (it == current->objectValue.end()) {
            throw std::runtime_error("unknown field: " + std::string(part));
        }
        current = it->second;
        if (nextDot == std::string_view::npos) {
            break;
        }
        offset = nextDot + 1;
    }

    return current;
}

static bool valuesEqual(const Value& left, const Value& right) {
    if (left.kind == Value::Kind::Null || right.kind == Value::Kind::Null) {
        return left.kind == right.kind;
    }
    if (left.kind == Value::Kind::List || right.kind == Value::Kind::List) {
        if (left.kind != right.kind || left.listValue.size() != right.listValue.size()) {
            return false;
        }
        for (size_t i = 0; i < left.listValue.size(); ++i) {
            if (!valuesEqual(left.listValue[i], right.listValue[i])) {
                return false;
            }
        }
        return true;
    }
    if (left.kind == Value::Kind::Object || right.kind == Value::Kind::Object) {
        if (left.kind != right.kind || left.objectValue.size() != right.objectValue.size()) {
            return false;
        }
        for (const auto& [name, value] : left.objectValue) {
            auto it = right.objectValue.find(name);
            if (it == right.objectValue.end() || !valuesEqual(value, it->second)) {
                return false;
            }
        }
        return true;
    }
    if (left.kind == Value::Kind::Text || right.kind == Value::Kind::Text) {
        return left.asText() == right.asText();
    }
    if (left.kind == Value::Kind::Char && right.kind == Value::Kind::Char) {
        return left.charValue == right.charValue;
    }
    if (left.kind == Value::Kind::Bool && right.kind == Value::Kind::Bool) {
        return left.boolValue == right.boolValue;
    }
    if (left.kind == Value::Kind::Float || right.kind == Value::Kind::Float) {
        return left.asFloat() == right.asFloat();
    }
    return left.asInt() == right.asInt();
}

Interpreter::Interpreter() {
    registerBuiltins();
}

Value Interpreter::execute(const Program& program, bool printResult) {
    Value lastValue;
    for (const auto& statement : program.statements) {
        lastValue = evalStatement(*statement, rootContext_);
        if (printResult && !lastValue.isNull()) {
            std::cout << lastValue.toString() << "\n";
        }
    }
    return lastValue;
}

Value Interpreter::evalStatement(const Statement& statement, EvalContext& context) {
    if (auto decl = dynamic_cast<const StreamDecl*>(&statement)) {
        size_t count = decl->names.size();
        if (count > 1 && decl->initializers.size() == 1) {
            Value grouped = evalExpression(*decl->initializers.front(), context);
            if (grouped.kind == Value::Kind::List && grouped.listValue.size() == count) {
                for (size_t i = 0; i < count; ++i) {
                    if (decl->typeName) {
                        validateValueAgainstType(grouped.listValue[i], *decl->typeName, context);
                    }
                    context.defineVariable(decl->names[i], grouped.listValue[i], decl->typeName);
                }
                return Value();
            }
        }
        for (size_t i = 0; i < count; ++i) {
            Value value;
            bool hasInitializer = i < decl->initializers.size();
            if (i < decl->initializers.size()) {
                value = evalExpression(*decl->initializers[i], context);
            }
            if (decl->typeName) {
                requireKnownType(context, *decl->typeName);
                if (hasInitializer) {
                    validateValueAgainstType(value, *decl->typeName, context);
                }
                if (hasInitializer) {
                    if (auto* shape = context.lookupShape(*decl->typeName);
                        shape && value.kind == Value::Kind::Object && !value.shapeName) {
                        value.shapeName = shape->name;
                    }
                }
            }
            context.defineVariable(decl->names[i], std::move(value), decl->typeName);
        }
        return Value();
    }

    if (auto shape = dynamic_cast<const ShapeDecl*>(&statement)) {
        context.defineShape(shape->name, const_cast<ShapeDecl*>(shape));
        return Value();
    }

    if (auto gate = dynamic_cast<const GateDecl*>(&statement)) {
        context.defineGate(gate->name, const_cast<GateDecl*>(gate));
        return Value();
    }

    if (auto exprStmt = dynamic_cast<const ExprStmt*>(&statement)) {
        return evalExpression(*exprStmt->expression, context);
    }

    if (dynamic_cast<const UseDecl*>(&statement)) {
        throw std::runtime_error("@use requires file-based module loading");
    }

    throw std::runtime_error("unknown statement type");
}

Value Interpreter::evalExpression(const Expression& expression, EvalContext& context) {
    if (auto literal = dynamic_cast<const LiteralExpr*>(&expression)) {
        if (literal->value.index() == 0) {
            return Value();
        }
        if (std::holds_alternative<int64_t>(literal->value)) {
            return Value(std::get<int64_t>(literal->value));
        }
        if (std::holds_alternative<double>(literal->value)) {
            return Value(std::get<double>(literal->value));
        }
        if (std::holds_alternative<bool>(literal->value)) {
            return Value(std::get<bool>(literal->value));
        }
        if (std::holds_alternative<char>(literal->value)) {
            return Value(std::get<char>(literal->value));
        }
        if (std::holds_alternative<std::string>(literal->value)) {
            return Value(std::get<std::string>(literal->value));
        }
    }

    if (auto identifier = dynamic_cast<const IdentifierExpr*>(&expression)) {
        if (auto value = context.lookupVariable(identifier->name)) {
            return *value;
        }
        if (auto value = lookupDottedField(context, identifier->name)) {
            return *value;
        }
        if (identifier->name == "_" ) {
            return Value();
        }
        throw std::runtime_error("undefined variable: " + identifier->name);
    }

    if (auto prefix = dynamic_cast<const PrefixExpr*>(&expression)) {
        auto value = evalExpression(*prefix->right, context);
        if (prefix->op == "?" || prefix->op == "!") {
            return value;
        }
        if (prefix->op == "-") {
            if (value.kind == Value::Kind::Int) {
                return Value(-value.intValue);
            }
            if (value.kind == Value::Kind::Float) {
                return Value(-value.floatValue);
            }
            throw std::runtime_error("unexpected operand for unary '-'");
        }
        return value;
    }

    if (auto compare = dynamic_cast<const ProbeCompareExpr*>(&expression)) {
        auto input = context.lookupVariable("__input");
        if (!input) {
            throw std::runtime_error("probe comparison requires a routed input");
        }
        auto value = evalExpression(*compare->right, context);
        if (compare->op == "?=") {
            return Value(valuesEqual(*input, value));
        }
        if (compare->op == "?!=") {
            return Value(!valuesEqual(*input, value));
        }
        if (compare->op == "?>") {
            return Value(input->asFloat() > value.asFloat());
        }
        if (compare->op == "?>=") {
            return Value(input->asFloat() >= value.asFloat());
        }
        if (compare->op == "?<") {
            return Value(input->asFloat() < value.asFloat());
        }
        if (compare->op == "?<=") {
            return Value(input->asFloat() <= value.asFloat());
        }
        throw std::runtime_error("unknown probe comparison operator: " + compare->op);
    }

    if (auto list = dynamic_cast<const ListExpr*>(&expression)) {
        std::vector<Value> values;
        values.reserve(list->items.size());
        for (const auto& item : list->items) {
            values.push_back(evalExpression(*item, context));
        }
        return Value(std::move(values));
    }

    if (auto object = dynamic_cast<const StructLiteralExpr*>(&expression)) {
        std::map<std::string, Value> fields;
        for (const auto& field : object->fields) {
            fields[field.name] = evalExpression(*field.value, context);
        }
        return Value(std::move(fields));
    }

    if (auto call = dynamic_cast<const CallExpr*>(&expression)) {
        if (auto callee = dynamic_cast<const IdentifierExpr*>(call->callee.get())) {
            std::vector<Value> args;
            for (const auto& arg : call->arguments) {
                Value value = evalExpression(*arg, context);
                auto flattened = flattenValue(value);
                args.insert(args.end(), flattened.begin(), flattened.end());
            }
            return callFunction(callee->name, args, context);
        }
        throw std::runtime_error("unsupported call target");
    }

    if (auto route = dynamic_cast<const RouteExpr*>(&expression)) {
        return evalRoute(*route, context);
    }

    if (auto field = dynamic_cast<const FieldExpr*>(&expression)) {
        return evalField(*field, Value(), context);
    }

    if (auto fork = dynamic_cast<const ForkExpr*>(&expression)) {
        return evalFork(*fork, Value(), context);
    }

    if (auto loop = dynamic_cast<const LoopExpr*>(&expression)) {
        return evalLoop(*loop, Value(), context);
    }

    if (auto capture = dynamic_cast<const CaptureExpr*>(&expression)) {
        if (auto value = context.lookupVariable(capture->name)) {
            return *value;
        }
        return Value();
    }

    if (dynamic_cast<const WildcardExpr*>(&expression)) {
        return Value();
    }

    throw std::runtime_error("unsupported expression type");
}

Value Interpreter::evalRoute(const RouteExpr& route, EvalContext& context) {
    Value current = evalExpression(*route.source, context);
    for (const auto& stage : route.stages) {
        current = evalRouteStage(*stage, current, context);
    }
    return current;
}

Value Interpreter::evalRouteStage(const Expression& stage, const Value& current, EvalContext& context) {
    if (auto call = dynamic_cast<const CallExpr*>(&stage)) {
        if (auto callee = dynamic_cast<const IdentifierExpr*>(call->callee.get())) {
            std::vector<Value> args;
            auto currentArgs = flattenValue(current);
            args.insert(args.end(), currentArgs.begin(), currentArgs.end());
            for (const auto& arg : call->arguments) {
                Value value = evalExpression(*arg, context);
                auto flattened = flattenValue(value);
                args.insert(args.end(), flattened.begin(), flattened.end());
            }
            return callFunction(callee->name, args, context);
        }
    }

    if (auto capture = dynamic_cast<const CaptureExpr*>(&stage)) {
        Value captured = current;
        if (capture->typeName) {
            validateValueAgainstType(captured, *capture->typeName, context);
            if (auto* shape = context.lookupShape(*capture->typeName);
                shape && captured.kind == Value::Kind::Object && !captured.shapeName) {
                captured.shapeName = shape->name;
            }
        }
        context.defineVariable(capture->name, captured, capture->typeName);
        return current;
    }

    if (auto identifier = dynamic_cast<const IdentifierExpr*>(&stage)) {
        if (!current.isNull()) {
            if (auto typeName = context.lookupVariableType(identifier->name)) {
                validateValueAgainstType(current, *typeName, context);
            }
            if (context.assignVariable(identifier->name, current)) {
                return current;
            }
            return callFunction(identifier->name, flattenValue(current), context);
        }
        return evalExpression(stage, context);
    }

    if (auto field = dynamic_cast<const FieldExpr*>(&stage)) {
        return evalField(*field, current, context);
    }

    if (auto fork = dynamic_cast<const ForkExpr*>(&stage)) {
        return evalFork(*fork, current, context);
    }

    if (auto loop = dynamic_cast<const LoopExpr*>(&stage)) {
        return evalLoop(*loop, current, context);
    }

    return evalExpression(stage, context);
}

Value Interpreter::evalField(const FieldExpr& field, const Value& input, EvalContext& context) {
    EvalContext local{&context, {}, {}, {}};
    if (field.inputTypeName) {
        validateValueAgainstType(input, *field.inputTypeName, context);
    }
    local.defineVariable(field.inputName.value_or("__input"), input, field.inputTypeName);
    Value result;
    for (const auto& statement : field.body) {
        result = evalStatement(*statement, local);
    }
    return result;
}

Value Interpreter::evalFork(const ForkExpr& fork, const Value& input, EvalContext& context) {
    EvalContext local{&context, {}, {}, {}};
    local.defineVariable("__input", input);
    for (const auto& arm : fork.arms) {
        if (dynamic_cast<const WildcardExpr*>(arm.condition.get())) {
            return evalExpression(*arm.body, local);
        }
        Value cond = evalExpression(*arm.condition, local);
        if (cond.isTruthy()) {
            return evalExpression(*arm.body, local);
        }
    }
    return Value();
}

Value Interpreter::evalLoop(const LoopExpr& loop, const Value& input, EvalContext& context) {
    EvalContext local{&context, {}, {}, {}};
    local.defineVariable("__input", input);
    Value result;
    while (evalExpression(*loop.condition, local).isTruthy()) {
        result = evalExpression(*loop.body, local);
    }
    return result;
}

Value Interpreter::callFunction(std::string_view name, const std::vector<Value>& args, EvalContext& context) {
    std::string key(name);
    auto builtin = builtins_.find(key);
    if (builtin != builtins_.end()) {
        return builtin->second(args);
    }

    if (auto gate = context.lookupGate(name)) {
        return callUserGate(*gate, args, context);
    }

    throw std::runtime_error("unknown gate or function: " + key);
}

Value Interpreter::callUserGate(const GateDecl& gate, const std::vector<Value>& args, EvalContext& context) {
    EvalContext local{&context, {}, {}, {}};
    for (size_t i = 0; i < gate.params.size(); ++i) {
        Value value;
        if (i < args.size()) {
            value = args[i];
        }
        if (gate.params[i].typeName) {
            validateValueAgainstType(value, *gate.params[i].typeName, context);
            if (auto* shape = context.lookupShape(*gate.params[i].typeName);
                shape && value.kind == Value::Kind::Object && !value.shapeName) {
                value.shapeName = shape->name;
            }
        }
        local.defineVariable(gate.params[i].name, std::move(value), gate.params[i].typeName);
    }
    Value result = evalExpression(*gate.body, local);
    if (gate.resultType) {
        validateValueAgainstType(result, *gate.resultType, context);
        if (auto* shape = context.lookupShape(*gate.resultType);
            shape && result.kind == Value::Kind::Object && !result.shapeName) {
            result.shapeName = shape->name;
        }
    }
    return result;
}

static Value requireArg(const std::vector<Value>& args, size_t index, std::string_view name) {
    if (index >= args.size()) {
        throw std::runtime_error(std::string(name) + " expected argument at position " + std::to_string(index));
    }
    return args[index];
}

void Interpreter::registerBuiltins() {
    builtins_["add"] = [](const std::vector<Value>& args) {
        if (args.size() < 2) {
            throw std::runtime_error("add requires two values");
        }
        if (args[0].kind == Value::Kind::Float || args[1].kind == Value::Kind::Float) {
            return Value(args[0].asFloat() + args[1].asFloat());
        }
        return Value(args[0].asInt() + args[1].asInt());
    };
    builtins_["sub"] = [](const std::vector<Value>& args) {
        if (args.size() < 2) {
            throw std::runtime_error("sub requires two values");
        }
        if (args[0].kind == Value::Kind::Float || args[1].kind == Value::Kind::Float) {
            return Value(args[0].asFloat() - args[1].asFloat());
        }
        return Value(args[0].asInt() - args[1].asInt());
    };
    builtins_["mul"] = [](const std::vector<Value>& args) {
        if (args.size() < 2) {
            throw std::runtime_error("mul requires two values");
        }
        if (args[0].kind == Value::Kind::Float || args[1].kind == Value::Kind::Float) {
            return Value(args[0].asFloat() * args[1].asFloat());
        }
        return Value(args[0].asInt() * args[1].asInt());
    };
    builtins_["div"] = [](const std::vector<Value>& args) {
        if (args.size() < 2) {
            throw std::runtime_error("div requires two values");
        }
        if (args[1].asFloat() == 0.0) {
            throw std::runtime_error("division by zero");
        }
        if (args[0].kind == Value::Kind::Float || args[1].kind == Value::Kind::Float) {
            return Value(args[0].asFloat() / args[1].asFloat());
        }
        return Value(args[0].asInt() / args[1].asInt());
    };
    builtins_["mod"] = [](const std::vector<Value>& args) {
        if (args.size() < 2) {
            throw std::runtime_error("mod requires two values");
        }
        return Value(args[0].asInt() % args[1].asInt());
    };
    builtins_["neg"] = [](const std::vector<Value>& args) {
        if (args.empty()) {
            throw std::runtime_error("neg requires one value");
        }
        if (args[0].kind == Value::Kind::Float) {
            return Value(-args[0].floatValue);
        }
        return Value(-args[0].asInt());
    };
    builtins_["eq"] = [](const std::vector<Value>& args) {
        if (args.size() < 2) {
            throw std::runtime_error("eq requires two values");
        }
        return Value(args[0].toString() == args[1].toString());
    };
    builtins_["neq"] = [](const std::vector<Value>& args) {
        if (args.size() < 2) {
            throw std::runtime_error("neq requires two values");
        }
        return Value(args[0].toString() != args[1].toString());
    };
    builtins_["gt"] = [](const std::vector<Value>& args) {
        if (args.size() < 2) {
            throw std::runtime_error("gt requires two values");
        }
        return Value(args[0].asFloat() > args[1].asFloat());
    };
    builtins_["lt"] = [](const std::vector<Value>& args) {
        if (args.size() < 2) {
            throw std::runtime_error("lt requires two values");
        }
        return Value(args[0].asFloat() < args[1].asFloat());
    };
    builtins_["gte"] = [](const std::vector<Value>& args) {
        if (args.size() < 2) {
            throw std::runtime_error("gte requires two values");
        }
        return Value(args[0].asFloat() >= args[1].asFloat());
    };
    builtins_["lte"] = [](const std::vector<Value>& args) {
        if (args.size() < 2) {
            throw std::runtime_error("lte requires two values");
        }
        return Value(args[0].asFloat() <= args[1].asFloat());
    };
    builtins_["and"] = [](const std::vector<Value>& args) {
        if (args.size() < 2) {
            throw std::runtime_error("and requires two values");
        }
        return Value(args[0].isTruthy() && args[1].isTruthy());
    };
    builtins_["or"] = [](const std::vector<Value>& args) {
        if (args.size() < 2) {
            throw std::runtime_error("or requires two values");
        }
        return Value(args[0].isTruthy() || args[1].isTruthy());
    };
    builtins_["not"] = [](const std::vector<Value>& args) {
        if (args.empty()) {
            throw std::runtime_error("not requires one value");
        }
        return Value(!args[0].isTruthy());
    };
    builtins_["out.write"] = [](const std::vector<Value>& args) {
        if (args.empty()) {
            return Value();
        }
        std::cout << args[0].toString();
        return Value();
    };
    builtins_["out.writeln"] = [](const std::vector<Value>& args) {
        if (!args.empty()) {
            std::cout << args[0].toString();
        }
        std::cout << '\n';
        return Value();
    };
    builtins_["out.write_err"] = [](const std::vector<Value>& args) {
        if (!args.empty()) {
            std::cerr << args[0].toString();
        }
        return Value();
    };
    builtins_["text.from"] = [](const std::vector<Value>& args) {
        if (args.empty()) {
            throw std::runtime_error("text.from requires one value");
        }
        return Value(args[0].toString());
    };
    builtins_["text.len"] = [](const std::vector<Value>& args) {
        if (args.empty()) {
            throw std::runtime_error("text.len requires one value");
        }
        return Value(static_cast<int64_t>(args[0].asText().size()));
    };
    builtins_["text.join"] = [](const std::vector<Value>& args) {
        if (args.size() < 2) {
            throw std::runtime_error("text.join requires two values");
        }
        return Value(args[0].asText() + args[1].asText());
    };
    builtins_["tee"] = [](const std::vector<Value>& args) {
        if (args.empty()) {
            return Value();
        }
        return args[0];
    };
    builtins_["id"] = [](const std::vector<Value>& args) {
        if (args.empty()) {
            return Value();
        }
        return args[0];
    };
    builtins_["const"] = [](const std::vector<Value>& args) {
        if (args.size() < 1) {
            throw std::runtime_error("const requires at least one value");
        }
        if (args.size() == 1) {
            return args[0];
        }
        return args[1];
    };
    builtins_["assert"] = [](const std::vector<Value>& args) {
        if (args.empty()) {
            throw std::runtime_error("assert requires one condition");
        }
        if (!args[0].isTruthy()) {
            throw std::runtime_error("assert failed");
        }
        return Value();
    };
    builtins_["@error"] = [](const std::vector<Value>& args) -> Value {
        if (args.empty()) {
            throw std::runtime_error("error requires a message");
        }
        throw std::runtime_error(args[0].toString());
    };
}

} // namespace dagger
