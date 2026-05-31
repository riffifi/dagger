#include "runtime.h"
#include <cmath>
#include <functional>
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
Value::Value(std::map<std::string, Value> value, std::optional<std::string> typeName)
    : kind(Kind::Object), objectValue(std::move(value)), typeName(std::move(typeName)) {}

bool Value::isNull() const {
    return kind == Kind::Null;
}

bool Value::isTruthy() const {
    switch (kind) {
        case Kind::Null: return false;
        case Kind::Bool: return boolValue;
        case Kind::Int: return intValue != 0;
        case Kind::Float: return floatValue != 0.0;
        case Kind::Text: return !textValue.empty();
        case Kind::Char: return charValue != '\0';
        case Kind::List: return !listValue.empty();
        case Kind::Object: return true;
    }
    return false;
}

std::string Value::toString() const {
    switch (kind) {
        case Kind::Null: return "null";
        case Kind::Bool: return boolValue ? "true" : "false";
        case Kind::Int: return std::to_string(intValue);
        case Kind::Float: {
            std::ostringstream out;
            out << floatValue;
            return out.str();
        }
        case Kind::Text: return textValue;
        case Kind::Char: return std::string(1, charValue);
        case Kind::List: {
            std::ostringstream out;
            out << "[";
            for (size_t i = 0; i < listValue.size(); ++i) {
                if (i > 0) out << ", ";
                out << listValue[i].toString();
            }
            out << "]";
            return out.str();
        }
        case Kind::Object: {
            std::ostringstream out;
            if (typeName) out << *typeName << " ";
            out << "[";
            bool first = true;
            for (const auto& [name, value] : objectValue) {
                if (!first) out << ", ";
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
        case Kind::Int: return intValue;
        case Kind::Float: return static_cast<int64_t>(floatValue);
        case Kind::Bool: return boolValue ? 1 : 0;
        case Kind::Text: return std::stoll(textValue);
        case Kind::Char: return static_cast<int64_t>(charValue);
        case Kind::List: if (listValue.size() == 1) return listValue.front().asInt(); break;
        default: break;
    }
    throw std::runtime_error("cannot convert " + toString() + " to int");
}

double Value::asFloat() const {
    switch (kind) {
        case Kind::Float: return floatValue;
        case Kind::Int: return static_cast<double>(intValue);
        case Kind::Bool: return boolValue ? 1.0 : 0.0;
        case Kind::Text: return std::stod(textValue);
        case Kind::Char: return static_cast<double>(charValue);
        case Kind::List: if (listValue.size() == 1) return listValue.front().asFloat(); break;
        default: break;
    }
    throw std::runtime_error("cannot convert " + toString() + " to float");
}

std::string Value::asText() const {
    return toString();
}

static std::vector<Value> flattenValue(const Value& value) {
    if (value.kind == Value::Kind::List) return value.listValue;
    if (value.isNull()) return {};
    return {value};
}

static bool isBuiltinTypeName(std::string_view typeName) {
    static const std::set<std::string, std::less<>> builtinTypes{
        "int", "int8", "int16", "int32", "int64",
        "uint", "uint8", "uint16", "uint32", "uint64",
        "byte", "float", "float32", "float64", "bool",
        "text", "char", "null",
    };
    return builtinTypes.contains(std::string(typeName));
}

static bool isIntegerTypeName(std::string_view typeName) {
    static const std::set<std::string, std::less<>> integerTypes{
        "int", "int8", "int16", "int32", "int64",
        "uint", "uint8", "uint16", "uint32", "uint64", "byte",
    };
    return integerTypes.contains(std::string(typeName));
}

static bool isFloatTypeName(std::string_view typeName) {
    return typeName == "float" || typeName == "float32" || typeName == "float64";
}

static std::string valueKindName(const Value& value) {
    switch (value.kind) {
        case Value::Kind::Null: return "null";
        case Value::Kind::Int: return "int";
        case Value::Kind::Float: return "float";
        case Value::Kind::Bool: return "bool";
        case Value::Kind::Text: return "text";
        case Value::Kind::Char: return "char";
        case Value::Kind::List: return "list";
        case Value::Kind::Object: return value.typeName.value_or("object");
    }
    return "unknown";
}

static void requireKnownType(const EvalContext& context, std::string_view typeName) {
    if (isBuiltinTypeName(typeName) || context.lookupType(typeName)) return;
    if (typeName.starts_with("block[") || typeName.starts_with("slice[")) return;
    throw std::runtime_error("unknown type: " + std::string(typeName));
}

static void validateValueAgainstType(const Value& value, std::string_view typeName, const EvalContext& context);

static void validateObjectAgainstType(const Value& value, const TypeDecl& type, const EvalContext& context) {
    if (value.kind != Value::Kind::Object) {
        throw std::runtime_error("type mismatch: expected " + type.name + ", got " + valueKindName(value));
    }

    for (const auto& field : type.fields) {
        auto it = value.objectValue.find(field.name);
        if (it == value.objectValue.end()) {
            throw std::runtime_error("type mismatch: missing field '" + field.name + "' for " + type.name);
        }
        if (field.typeName) validateValueAgainstType(it->second, *field.typeName, context);
    }
}

static void validateValueAgainstType(const Value& value, std::string_view typeName, const EvalContext& context) {
    requireKnownType(context, typeName);
    if (isIntegerTypeName(typeName)) {
        if (value.kind != Value::Kind::Int) throw std::runtime_error("type mismatch");
        return;
    }
    if (isFloatTypeName(typeName)) {
        if (value.kind != Value::Kind::Float) throw std::runtime_error("type mismatch");
        return;
    }
    if (typeName == "bool" && value.kind != Value::Kind::Bool) throw std::runtime_error("type mismatch");
    if (typeName == "text" && value.kind != Value::Kind::Text) throw std::runtime_error("type mismatch");
    if (typeName == "char" && value.kind != Value::Kind::Char) throw std::runtime_error("type mismatch");
    if (typeName == "null" && value.kind != Value::Kind::Null) throw std::runtime_error("type mismatch");

    if (const auto* type = context.lookupType(typeName)) {
        validateObjectAgainstType(value, *type, context);
    }
}

std::optional<Value> EvalContext::lookupVariable(std::string_view name) const {
    auto it = variables.find(std::string(name));
    if (it != variables.end()) return it->second;
    if (parent) return parent->lookupVariable(name);
    return std::nullopt;
}

std::optional<std::string> EvalContext::lookupVariableType(std::string_view name) const {
    auto it = variableTypes.find(std::string(name));
    if (it != variableTypes.end()) return it->second;
    if (parent) return parent->lookupVariableType(name);
    return std::nullopt;
}

FunctionDecl* EvalContext::lookupFunction(std::string_view name) const {
    auto it = functions.find(std::string(name));
    if (it != functions.end()) return it->second;
    if (parent) return parent->lookupFunction(name);
    return nullptr;
}

TypeDecl* EvalContext::lookupType(std::string_view name) const {
    auto it = types.find(std::string(name));
    if (it != types.end()) return it->second;
    if (parent) return parent->lookupType(name);
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
    if (parent) return parent->assignVariable(name, std::move(value));
    return false;
}

void EvalContext::defineFunction(std::string name, FunctionDecl* func) { functions[std::move(name)] = func; }
void EvalContext::defineType(std::string name, TypeDecl* type) { types[std::move(name)] = type; }

static std::optional<Value> lookupDottedField(const EvalContext& context, std::string_view name) {
    const size_t firstDot = name.find('.');
    if (firstDot == std::string_view::npos) return std::nullopt;
    auto current = context.lookupVariable(name.substr(0, firstDot));
    if (!current) return std::nullopt;
    size_t offset = firstDot + 1;
    while (offset <= name.size()) {
        const size_t nextDot = name.find('.', offset);
        const std::string_view part = name.substr(offset, nextDot == std::string_view::npos ? name.size() - offset : nextDot - offset);
        if (current->kind != Value::Kind::Object) throw std::runtime_error("field access on non-object");
        auto it = current->objectValue.find(std::string(part));
        if (it == current->objectValue.end()) throw std::runtime_error("unknown field: " + std::string(part));
        current = it->second;
        if (nextDot == std::string_view::npos) break;
        offset = nextDot + 1;
    }
    return current;
}

static bool valuesEqual(const Value& left, const Value& right) {
    if (left.kind != right.kind) return false;
    switch (left.kind) {
        case Value::Kind::Null: return true;
        case Value::Kind::Bool: return left.boolValue == right.boolValue;
        case Value::Kind::Int: return left.intValue == right.intValue;
        case Value::Kind::Float: return left.floatValue == right.floatValue;
        case Value::Kind::Char: return left.charValue == right.charValue;
        case Value::Kind::Text: return left.textValue == right.textValue;
        case Value::Kind::List:
            if (left.listValue.size() != right.listValue.size()) return false;
            for (size_t i = 0; i < left.listValue.size(); ++i) if (!valuesEqual(left.listValue[i], right.listValue[i])) return false;
            return true;
        case Value::Kind::Object:
            if (left.objectValue.size() != right.objectValue.size()) return false;
            for (const auto& [name, value] : left.objectValue) {
                auto it = right.objectValue.find(name);
                if (it == right.objectValue.end() || !valuesEqual(value, it->second)) return false;
            }
            return true;
    }
    return false;
}

Interpreter::Interpreter() { registerBuiltins(); }

Value Interpreter::execute(const Program& program, bool printResult) {
    Value lastValue;
    for (const auto& statement : program.statements) {
        lastValue = evalStatement(*statement, rootContext_);
        if (printResult && !lastValue.isNull()) std::cout << lastValue.toString() << "\n";
    }
    return lastValue;
}

Value Interpreter::evalStatement(const Statement& statement, EvalContext& context) {
    if (auto decl = dynamic_cast<const StreamDecl*>(&statement)) {
        for (size_t i = 0; i < decl->names.size(); ++i) {
            Value value;
            if (i < decl->initializers.size()) value = evalExpression(*decl->initializers[i], context);
            if (decl->typeName) validateValueAgainstType(value, *decl->typeName, context);
            context.defineVariable(decl->names[i], value, decl->typeName);
        }
        return Value();
    }
    if (auto typeDecl = dynamic_cast<const TypeDecl*>(&statement)) {
        context.defineType(typeDecl->name, const_cast<TypeDecl*>(typeDecl));
        return Value();
    }
    if (auto funcDecl = dynamic_cast<const FunctionDecl*>(&statement)) {
        context.defineFunction(funcDecl->name, const_cast<FunctionDecl*>(funcDecl));
        return Value();
    }
    if (auto exprStmt = dynamic_cast<const ExprStmt*>(&statement)) {
        return evalExpression(*exprStmt->expression, context);
    }
    if (auto dirStmt = dynamic_cast<const DirectiveStmt*>(&statement)) {
        if (dirStmt->directive == "@error") {
             throw std::runtime_error(evalExpression(*dirStmt->arguments[0], context).toString());
        }
        return Value();
    }
    throw std::runtime_error("unsupported statement");
}

Value Interpreter::evalExpression(const Expression& expression, EvalContext& context) {
    if (auto literal = dynamic_cast<const LiteralExpr*>(&expression)) {
        if (literal->value.index() == 0) return Value();
        if (std::holds_alternative<int64_t>(literal->value)) return Value(std::get<int64_t>(literal->value));
        if (std::holds_alternative<double>(literal->value)) return Value(std::get<double>(literal->value));
        if (std::holds_alternative<bool>(literal->value)) return Value(std::get<bool>(literal->value));
        if (std::holds_alternative<char>(literal->value)) return Value(std::get<char>(literal->value));
        if (std::holds_alternative<std::string>(literal->value)) return Value(std::get<std::string>(literal->value));
    }
    if (auto identifier = dynamic_cast<const IdentifierExpr*>(&expression)) {
        if (auto value = context.lookupVariable(identifier->name)) return *value;
        if (auto value = lookupDottedField(context, identifier->name)) return *value;
        if (identifier->name == "_") return Value();
        throw std::runtime_error("undefined variable: " + identifier->name);
    }
    if (auto prefix = dynamic_cast<const PrefixExpr*>(&expression)) {
        auto value = evalExpression(*prefix->right, context);
        if (prefix->op == "-") {
            if (value.kind == Value::Kind::Int) return Value(-value.intValue);
            if (value.kind == Value::Kind::Float) return Value(-value.floatValue);
        }
        return value;
    }
    if (auto compare = dynamic_cast<const ProbeCompareExpr*>(&expression)) {
        auto input = context.lookupVariable("__input");
        if (!input) throw std::runtime_error("no routed input");
        auto value = evalExpression(*compare->right, context);
        if (compare->op == "?=") return Value(valuesEqual(*input, value));
        if (compare->op == "?!=") return Value(!valuesEqual(*input, value));
        if (compare->op == "?>") return Value(input->asFloat() > value.asFloat());
        if (compare->op == "?>=") return Value(input->asFloat() >= value.asFloat());
        if (compare->op == "?<") return Value(input->asFloat() < value.asFloat());
        if (compare->op == "?<=") return Value(input->asFloat() <= value.asFloat());
    }
    if (auto list = dynamic_cast<const ListExpr*>(&expression)) {
        std::vector<Value> values;
        for (const auto& item : list->items) values.push_back(evalExpression(*item, context));
        return Value(values);
    }
    if (auto object = dynamic_cast<const StructLiteralExpr*>(&expression)) {
        std::map<std::string, Value> fields;
        for (const auto& field : object->fields) fields[field.name] = evalExpression(*field.value, context);
        return Value(fields, std::nullopt);
    }
    if (auto call = dynamic_cast<const CallExpr*>(&expression)) {
        if (auto callee = dynamic_cast<const IdentifierExpr*>(call->callee.get())) {
            std::vector<Value> args;
            for (const auto& arg : call->arguments) {
                auto flat = flattenValue(evalExpression(*arg, context));
                args.insert(args.end(), flat.begin(), flat.end());
            }
            return callFunction(callee->name, args, context);
        }
    }
    if (auto route = dynamic_cast<const RouteExpr*>(&expression)) return evalRoute(*route, context);
    if (auto block = dynamic_cast<const BlockExpr*>(&expression)) return evalBlock(*block, Value(), context);
    if (auto fork = dynamic_cast<const ForkExpr*>(&expression)) return evalFork(*fork, Value(), context);
    if (auto loop = dynamic_cast<const LoopExpr*>(&expression)) return evalLoop(*loop, Value(), context);
    if (auto each = dynamic_cast<const EachExpr*>(&expression)) return evalEach(*each, Value(), context);
    throw std::runtime_error("unsupported expression");
}

Value Interpreter::evalRoute(const RouteExpr& route, EvalContext& context) {
    Value current = evalExpression(*route.source, context);
    for (const auto& stage : route.stages) current = evalRouteStage(*stage, current, context);
    return current;
}

Value Interpreter::evalRouteStage(const Expression& stage, const Value& current, EvalContext& context) {
    if (auto call = dynamic_cast<const CallExpr*>(&stage)) {
        if (auto callee = dynamic_cast<const IdentifierExpr*>(call->callee.get())) {
            std::vector<Value> args = flattenValue(current);
            for (const auto& arg : call->arguments) {
                auto flat = flattenValue(evalExpression(*arg, context));
                args.insert(args.end(), flat.begin(), flat.end());
            }
            return callFunction(callee->name, args, context);
        }
    }
    if (auto capture = dynamic_cast<const CaptureExpr*>(&stage)) {
        context.defineVariable(capture->name, current, capture->typeName);
        return current;
    }
    if (auto identifier = dynamic_cast<const IdentifierExpr*>(&stage)) {
        if (!current.isNull()) {
            if (context.assignVariable(identifier->name, current)) return current;
            return callFunction(identifier->name, flattenValue(current), context);
        }
    }
    if (auto block = dynamic_cast<const BlockExpr*>(&stage)) return evalBlock(*block, current, context);
    if (auto fork = dynamic_cast<const ForkExpr*>(&stage)) return evalFork(*fork, current, context);
    if (auto loop = dynamic_cast<const LoopExpr*>(&stage)) return evalLoop(*loop, current, context);
    return evalExpression(stage, context);
}

Value Interpreter::evalBlock(const BlockExpr& block, const Value& input, EvalContext& context) {
    EvalContext local{&context};
    local.defineVariable(block.inputName.value_or("__input"), input, block.inputTypeName);
    Value result;
    for (const auto& statement : block.body) result = evalStatement(*statement, local);
    return result;
}

Value Interpreter::evalFork(const ForkExpr& fork, const Value& input, EvalContext& context) {
    EvalContext local{&context};
    local.defineVariable("__input", input);
    for (const auto& arm : fork.arms) {
        if (dynamic_cast<const WildcardExpr*>(arm.condition.get())) return evalExpression(*arm.body, local);
        if (auto ident = dynamic_cast<const IdentifierExpr*>(arm.condition.get())) {
             if (ident->name == "@ok" && input.kind != Value::Kind::Null) return evalExpression(*arm.body, local);
             if (ident->name == "@err" && input.kind == Value::Kind::Null) return evalExpression(*arm.body, local);
        }
        if (evalExpression(*arm.condition, local).isTruthy()) return evalExpression(*arm.body, local);
    }
    return Value();
}

Value Interpreter::evalLoop(const LoopExpr& loop, const Value& input, EvalContext& context) {
    EvalContext local{&context};
    local.defineVariable("__input", input);
    Value result;
    while (evalExpression(*loop.condition, local).isTruthy()) result = evalExpression(*loop.body, local);
    return result;
}

Value Interpreter::evalEach(const EachExpr& each, const Value& input, EvalContext& context) {
    if (input.kind != Value::Kind::List) throw std::runtime_error("each requires a list");
    Value result;
    for (const auto& item : input.listValue) {
        EvalContext local{&context};
        local.defineVariable(each.itemName, item);
        result = evalExpression(*each.body, local);
    }
    return result;
}

Value Interpreter::callFunction(std::string_view name, const std::vector<Value>& args, EvalContext& context) {
    auto builtin = builtins_.find(std::string(name));
    if (builtin != builtins_.end()) return builtin->second(args);
    if (auto func = context.lookupFunction(name)) return callUserFunction(*func, args, context);
    throw std::runtime_error("unknown function: " + std::string(name));
}

Value Interpreter::callUserFunction(const FunctionDecl& func, const std::vector<Value>& args, EvalContext& context) {
    if (!func.body) {
        throw std::runtime_error("cannot call @extern function in interpreter: " + func.name);
    }
    EvalContext local{&context};
    for (size_t i = 0; i < func.params.size(); ++i) {
        Value val = (i < args.size()) ? args[i] : Value();
        local.defineVariable(func.params[i].name, val, func.params[i].typeName);
    }
    return evalExpression(*func.body, local);
}

void Interpreter::registerBuiltins() {
    builtins_["add"] = [](const std::vector<Value>& args) { return Value(args[0].asFloat() + args[1].asFloat()); };
    builtins_["sub"] = [](const std::vector<Value>& args) { return Value(args[0].asFloat() - args[1].asFloat()); };
    builtins_["mul"] = [](const std::vector<Value>& args) { return Value(args[0].asFloat() * args[1].asFloat()); };
    builtins_["div"] = [](const std::vector<Value>& args) { return Value(args[0].asFloat() / args[1].asFloat()); };
    builtins_["mod"] = [](const std::vector<Value>& args) { return Value(args[0].asInt() % args[1].asInt()); };
    builtins_["neg"] = [](const std::vector<Value>& args) { return Value(-args[0].asFloat()); };
    builtins_["eq"] = [](const std::vector<Value>& args) { return Value(valuesEqual(args[0], args[1])); };
    builtins_["neq"] = [](const std::vector<Value>& args) { return Value(!valuesEqual(args[0], args[1])); };
    builtins_["gt"] = [](const std::vector<Value>& args) { return Value(args[0].asFloat() > args[1].asFloat()); };
    builtins_["lt"] = [](const std::vector<Value>& args) { return Value(args[0].asFloat() < args[1].asFloat()); };
    builtins_["gte"] = [](const std::vector<Value>& args) { return Value(args[0].asFloat() >= args[1].asFloat()); };
    builtins_["lte"] = [](const std::vector<Value>& args) { return Value(args[0].asFloat() <= args[1].asFloat()); };
    builtins_["and"] = [](const std::vector<Value>& args) { return Value(args[0].isTruthy() && args[1].isTruthy()); };
    builtins_["or"] = [](const std::vector<Value>& args) { return Value(args[0].isTruthy() || args[1].isTruthy()); };
    builtins_["not"] = [](const std::vector<Value>& args) { return Value(!args[0].isTruthy()); };
    builtins_["out.write"] = [](const std::vector<Value>& args) { if (!args.empty()) std::cout << args[0].toString(); return Value(); };
    builtins_["out.writeln"] = [](const std::vector<Value>& args) { if (!args.empty()) std::cout << args[0].toString(); std::cout << "\n"; return Value(); };
    builtins_["text.from"] = [](const std::vector<Value>& args) { return Value(args[0].toString()); };
    builtins_["text.len"] = [](const std::vector<Value>& args) { return Value((int64_t)args[0].asText().size()); };
    builtins_["text.join"] = [](const std::vector<Value>& args) { return Value(args[0].asText() + args[1].asText()); };
}

} // namespace dagger
