#include "semantics.h"
#include "stdlib.h"
#include <set>
#include <stdexcept>

namespace dagger {

namespace {

bool isBuiltinTypeName(std::string_view typeName) {
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

bool isIntegerTypeName(std::string_view typeName) {
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

bool isFloatTypeName(std::string_view typeName) {
    return typeName == "float" || typeName == "float32" || typeName == "float64";
}

struct SemanticContext {
    struct VariableInfo {
        TypeInfo type;
        bool initialized = false;
    };

    SemanticContext* parent = nullptr;
    std::map<std::string, VariableInfo> variables;
    std::map<std::string, const GateDecl*> gates;
    std::map<std::string, const ShapeDecl*> shapes;

    std::optional<VariableInfo> lookupVariable(std::string_view name) const {
        auto it = variables.find(std::string(name));
        if (it != variables.end()) {
            return it->second;
        }
        if (parent) {
            return parent->lookupVariable(name);
        }
        return std::nullopt;
    }

    const GateDecl* lookupGate(std::string_view name) const {
        auto it = gates.find(std::string(name));
        if (it != gates.end()) {
            return it->second;
        }
        if (parent) {
            return parent->lookupGate(name);
        }
        return nullptr;
    }

    const ShapeDecl* lookupShape(std::string_view name) const {
        auto it = shapes.find(std::string(name));
        if (it != shapes.end()) {
            return it->second;
        }
        if (parent) {
            return parent->lookupShape(name);
        }
        return nullptr;
    }

    void defineVariable(std::string name, TypeInfo type, bool initialized) {
        variables[std::move(name)] = VariableInfo{std::move(type), initialized};
    }

    bool assignVariable(std::string_view name, TypeInfo type, bool initialized) {
        auto it = variables.find(std::string(name));
        if (it != variables.end()) {
            it->second = VariableInfo{std::move(type), initialized};
            return true;
        }
        if (parent) {
            return parent->assignVariable(name, std::move(type), initialized);
        }
        return false;
    }
};

void requireKnownType(const SemanticContext& context, std::string_view typeName) {
    if (isBuiltinTypeName(typeName) || context.lookupShape(typeName)) {
        return;
    }
    throw std::runtime_error("unknown type: " + std::string(typeName));
}

TypeInfo typeFromAnnotation(const SemanticContext& context, const std::optional<std::string>& typeName) {
    if (!typeName) {
        return TypeInfo::unknown();
    }
    requireKnownType(context, *typeName);
    return TypeInfo::named(*typeName);
}

bool isNumericType(const TypeInfo& type) {
    return type.kind == TypeInfo::Kind::Named && (isIntegerTypeName(type.name) || isFloatTypeName(type.name));
}

TypeInfo resolveFieldType(const SemanticContext& context, const TypeInfo& base, std::string_view fieldName) {
    if (base.kind == TypeInfo::Kind::ObjectLiteral) {
        auto it = base.fields.find(std::string(fieldName));
        if (it == base.fields.end()) {
            throw std::runtime_error("unknown field: " + std::string(fieldName));
        }
        return it->second;
    }

    if (base.kind == TypeInfo::Kind::Named) {
        if (base.name == "text") {
            throw std::runtime_error("field access on non-object value");
        }
        if (const auto* shape = context.lookupShape(base.name)) {
            for (const auto& field : shape->fields) {
                if (field.name == fieldName) {
                    return typeFromAnnotation(context, field.typeName);
                }
            }
            throw std::runtime_error("unknown field: " + std::string(fieldName));
        }
    }

    if (!base.isUnknown()) {
        throw std::runtime_error("field access on non-object value");
    }

    return TypeInfo::unknown();
}

void requireAssignable(const SemanticContext& context, const TypeInfo& actual, const TypeInfo& expected) {
    if (expected.isUnknown() || actual.isUnknown()) {
        return;
    }

    if (expected.kind == TypeInfo::Kind::Named && actual.kind == TypeInfo::Kind::Named) {
        const bool compatibleIntegers = isIntegerTypeName(expected.name) && isIntegerTypeName(actual.name);
        const bool compatibleFloats = isFloatTypeName(expected.name) && isFloatTypeName(actual.name);
        if (expected.name != actual.name && !compatibleIntegers && !compatibleFloats) {
            throw std::runtime_error("type mismatch: expected " + expected.describe() + ", got " + actual.describe());
        }
        return;
    }

    if (expected.kind == TypeInfo::Kind::Named) {
        if (const auto* shape = context.lookupShape(expected.name)) {
            if (actual.kind != TypeInfo::Kind::ObjectLiteral) {
                throw std::runtime_error("type mismatch: expected " + expected.describe() + ", got " + actual.describe());
            }
            for (const auto& field : shape->fields) {
                auto it = actual.fields.find(field.name);
                if (it == actual.fields.end()) {
                    throw std::runtime_error("type mismatch: missing field '" + field.name + "' for " + expected.name);
                }
                requireAssignable(context, it->second, typeFromAnnotation(context, field.typeName));
            }
            for (const auto& [fieldName, _] : actual.fields) {
                bool found = false;
                for (const auto& field : shape->fields) {
                    if (field.name == fieldName) {
                        found = true;
                        break;
                    }
                }
                if (!found) {
                    throw std::runtime_error("type mismatch: unknown field '" + fieldName + "' for " + expected.name);
                }
            }
            return;
        }
    }

    if (expected.kind == TypeInfo::Kind::ObjectLiteral && actual.kind == TypeInfo::Kind::ObjectLiteral) {
        for (const auto& [fieldName, expectedField] : expected.fields) {
            auto it = actual.fields.find(fieldName);
            if (it == actual.fields.end()) {
                throw std::runtime_error("type mismatch: missing field '" + fieldName + "'");
            }
            requireAssignable(context, it->second, expectedField);
        }
        return;
    }

    if (expected.kind == TypeInfo::Kind::List && actual.kind == TypeInfo::Kind::List) {
        if (expected.elements.size() != actual.elements.size()) {
            throw std::runtime_error("type mismatch: expected " + expected.describe() + ", got " + actual.describe());
        }
        for (size_t i = 0; i < expected.elements.size(); ++i) {
            requireAssignable(context, actual.elements[i], expected.elements[i]);
        }
        return;
    }

    throw std::runtime_error("type mismatch: expected " + expected.describe() + ", got " + actual.describe());
}

TypeInfo inferExpression(const Expression& expression, SemanticContext& context);

std::vector<TypeInfo> flattenType(const TypeInfo& type) {
    if (type.kind == TypeInfo::Kind::List) {
        return type.elements;
    }
    if (type.isNamed("null")) {
        return {};
    }
    return {type};
}

TypeInfo analyzeCall(std::string_view name, const std::vector<TypeInfo>& args, SemanticContext& context) {
    auto requireArgCount = [&](size_t count) {
        if (args.size() != count) {
            throw std::runtime_error(std::string(name) + " expects " + std::to_string(count) + " arguments");
        }
    };
    auto requireAtLeast = [&](size_t count) {
        if (args.size() < count) {
            throw std::runtime_error(std::string(name) + " expects at least " + std::to_string(count) + " arguments");
        }
    };

    if (name == "add" || name == "sub" || name == "mul" || name == "div") {
        requireArgCount(2);
        if (!args[0].isUnknown() && !isNumericType(args[0])) {
            throw std::runtime_error(std::string(name) + " expects numeric arguments");
        }
        if (!args[1].isUnknown() && !isNumericType(args[1])) {
            throw std::runtime_error(std::string(name) + " expects numeric arguments");
        }
        if (args[0].kind == TypeInfo::Kind::Named && isFloatTypeName(args[0].name)) {
            return args[0];
        }
        if (args[1].kind == TypeInfo::Kind::Named && isFloatTypeName(args[1].name)) {
            return args[1];
        }
        if (args[0].kind == TypeInfo::Kind::Named && isIntegerTypeName(args[0].name)) {
            return args[0];
        }
        if (args[1].kind == TypeInfo::Kind::Named && isIntegerTypeName(args[1].name)) {
            return args[1];
        }
        return TypeInfo::unknown();
    }

    if (name == "mod") {
        requireArgCount(2);
        requireAssignable(context, args[0], TypeInfo::named("int64"));
        requireAssignable(context, args[1], TypeInfo::named("int64"));
        if (args[0].kind == TypeInfo::Kind::Named && isIntegerTypeName(args[0].name)) {
            return args[0];
        }
        return TypeInfo::named("int");
    }

    if (name == "neg") {
        requireArgCount(1);
        if (!args[0].isUnknown() && !isNumericType(args[0])) {
            throw std::runtime_error("neg expects a numeric argument");
        }
        return args[0];
    }

    if (name == "eq" || name == "neq") {
        requireArgCount(2);
        return TypeInfo::named("bool");
    }

    if (name == "gt" || name == "lt" || name == "gte" || name == "lte") {
        requireArgCount(2);
        if (!args[0].isUnknown() && !isNumericType(args[0])) {
            throw std::runtime_error(std::string(name) + " expects numeric arguments");
        }
        if (!args[1].isUnknown() && !isNumericType(args[1])) {
            throw std::runtime_error(std::string(name) + " expects numeric arguments");
        }
        return TypeInfo::named("bool");
    }

    if (name == "and" || name == "or") {
        requireArgCount(2);
        requireAssignable(context, args[0], TypeInfo::named("bool"));
        requireAssignable(context, args[1], TypeInfo::named("bool"));
        return TypeInfo::named("bool");
    }

    if (name == "not") {
        requireArgCount(1);
        requireAssignable(context, args[0], TypeInfo::named("bool"));
        return TypeInfo::named("bool");
    }

    if (name == "out.write" || name == "out.writeln" || name == "out.write_err") {
        return TypeInfo::named("null");
    }

    if (name == "text.from") {
        requireArgCount(1);
        return TypeInfo::named("text");
    }

    if (name == "text.len") {
        requireArgCount(1);
        requireAssignable(context, args[0], TypeInfo::named("text"));
        return TypeInfo::named("int");
    }

    if (name == "text.join") {
        requireArgCount(2);
        requireAssignable(context, args[0], TypeInfo::named("text"));
        requireAssignable(context, args[1], TypeInfo::named("text"));
        return TypeInfo::named("text");
    }

    if (name == "tee" || name == "id") {
        requireAtLeast(1);
        return args[0];
    }

    if (name == "const") {
        requireAtLeast(1);
        return args.size() == 1 ? args[0] : args[1];
    }

    if (name == "assert") {
        requireAtLeast(1);
        requireAssignable(context, args[0], TypeInfo::named("bool"));
        return TypeInfo::named("null");
    }

    if (name == "@error") {
        requireAtLeast(1);
        return TypeInfo::named("null");
    }

    if (hasReservedNamespaceRoot(name) && !isStdlibFunctionName(name)) {
        throw std::runtime_error("unknown stdlib symbol: " + std::string(name));
    }

    if (const auto* gate = context.lookupGate(name)) {
        if (args.size() != gate->params.size()) {
            throw std::runtime_error(std::string(name) + " expects " + std::to_string(gate->params.size()) + " arguments");
        }
        for (size_t i = 0; i < gate->params.size(); ++i) {
            requireAssignable(context, args[i], typeFromAnnotation(context, gate->params[i].typeName));
        }
        return typeFromAnnotation(context, gate->resultType);
    }

    throw std::runtime_error("unknown gate or function: " + std::string(name));
}

TypeInfo inferIdentifierType(std::string_view name, SemanticContext& context) {
    if (auto variable = context.lookupVariable(name)) {
        if (!variable->initialized) {
            throw std::runtime_error("use of uninitialized variable: " + std::string(name));
        }
        return variable->type;
    }

    const size_t firstDot = name.find('.');
    if (firstDot == std::string_view::npos) {
        if (isReservedNamespaceRoot(name)) {
            throw std::runtime_error("namespace '" + std::string(name) + "' is not a value");
        }
        if (name == "_") {
            return TypeInfo::named("null");
        }
        throw std::runtime_error("undefined variable: " + std::string(name));
    }

    if (hasReservedNamespaceRoot(name) && !isStdlibFunctionName(name)) {
        throw std::runtime_error("unknown stdlib symbol: " + std::string(name));
    }

    auto current = inferIdentifierType(name.substr(0, firstDot), context);
    size_t offset = firstDot + 1;
    while (offset <= name.size()) {
        const size_t nextDot = name.find('.', offset);
        const std::string_view part =
            name.substr(offset, nextDot == std::string_view::npos ? name.size() - offset : nextDot - offset);
        current = resolveFieldType(context, current, part);
        if (nextDot == std::string_view::npos) {
            break;
        }
        offset = nextDot + 1;
    }
    return current;
}

TypeInfo analyzeField(const FieldExpr& field, const TypeInfo& input, SemanticContext& context);
TypeInfo analyzeFork(const ForkExpr& fork, const TypeInfo& input, SemanticContext& context);
TypeInfo analyzeLoop(const LoopExpr& loop, const TypeInfo& input, SemanticContext& context);
TypeInfo analyzeStatement(const Statement& statement, SemanticContext& context);

TypeInfo inferExpression(const Expression& expression, SemanticContext& context) {
    if (auto literal = dynamic_cast<const LiteralExpr*>(&expression)) {
        if (literal->value.index() == 0) {
            return TypeInfo::named("null");
        }
        if (std::holds_alternative<int64_t>(literal->value)) {
            return TypeInfo::named("int");
        }
        if (std::holds_alternative<double>(literal->value)) {
            return TypeInfo::named("float");
        }
        if (std::holds_alternative<bool>(literal->value)) {
            return TypeInfo::named("bool");
        }
        if (std::holds_alternative<char>(literal->value)) {
            return TypeInfo::named("char");
        }
        if (std::holds_alternative<std::string>(literal->value)) {
            return TypeInfo::named("text");
        }
    }

    if (auto identifier = dynamic_cast<const IdentifierExpr*>(&expression)) {
        return inferIdentifierType(identifier->name, context);
    }

    if (auto prefix = dynamic_cast<const PrefixExpr*>(&expression)) {
        TypeInfo type = inferExpression(*prefix->right, context);
        if (prefix->op == "-") {
            if (!type.isUnknown() && !isNumericType(type)) {
                throw std::runtime_error("unexpected operand for unary '-'");
            }
        }
        return type;
    }

    if (auto compare = dynamic_cast<const ProbeCompareExpr*>(&expression)) {
        if (!context.lookupVariable("__input")) {
            throw std::runtime_error("probe comparison requires a routed input");
        }
        inferExpression(*compare->right, context);
        return TypeInfo::named("bool");
    }

    if (auto list = dynamic_cast<const ListExpr*>(&expression)) {
        std::vector<TypeInfo> items;
        items.reserve(list->items.size());
        for (const auto& item : list->items) {
            items.push_back(inferExpression(*item, context));
        }
        return TypeInfo::list(std::move(items));
    }

    if (auto object = dynamic_cast<const StructLiteralExpr*>(&expression)) {
        std::map<std::string, TypeInfo> fields;
        for (const auto& field : object->fields) {
            fields[field.name] = inferExpression(*field.value, context);
        }
        return TypeInfo::object(std::move(fields));
    }

    if (auto call = dynamic_cast<const CallExpr*>(&expression)) {
        auto* callee = dynamic_cast<const IdentifierExpr*>(call->callee.get());
        if (!callee) {
            throw std::runtime_error("unsupported call target");
        }
        std::vector<TypeInfo> args;
        for (const auto& arg : call->arguments) {
            auto flat = flattenType(inferExpression(*arg, context));
            args.insert(args.end(), flat.begin(), flat.end());
        }
        return analyzeCall(callee->name, args, context);
    }

    if (auto route = dynamic_cast<const RouteExpr*>(&expression)) {
        TypeInfo current = inferExpression(*route->source, context);
        for (const auto& stage : route->stages) {
            if (auto call = dynamic_cast<const CallExpr*>(stage.get())) {
                auto* callee = dynamic_cast<const IdentifierExpr*>(call->callee.get());
                if (!callee) {
                    throw std::runtime_error("unsupported call target");
                }
                std::vector<TypeInfo> args = flattenType(current);
                for (const auto& arg : call->arguments) {
                    auto flat = flattenType(inferExpression(*arg, context));
                    args.insert(args.end(), flat.begin(), flat.end());
                }
                current = analyzeCall(callee->name, args, context);
                continue;
            }

            if (auto capture = dynamic_cast<const CaptureExpr*>(stage.get())) {
                TypeInfo expected = typeFromAnnotation(context, capture->typeName);
                requireAssignable(context, current, expected);
                context.defineVariable(capture->name, capture->typeName ? expected : current, true);
                continue;
            }

            if (auto identifier = dynamic_cast<const IdentifierExpr*>(stage.get())) {
                if (auto existing = context.lookupVariable(identifier->name)) {
                    requireAssignable(context, current, existing->type);
                    context.assignVariable(identifier->name, existing->type.isUnknown() ? current : existing->type, true);
                    continue;
                }
                current = analyzeCall(identifier->name, flattenType(current), context);
                continue;
            }

            if (auto field = dynamic_cast<const FieldExpr*>(stage.get())) {
                current = analyzeField(*field, current, context);
                continue;
            }

            if (auto fork = dynamic_cast<const ForkExpr*>(stage.get())) {
                current = analyzeFork(*fork, current, context);
                continue;
            }

            if (auto loop = dynamic_cast<const LoopExpr*>(stage.get())) {
                current = analyzeLoop(*loop, current, context);
                continue;
            }

            current = inferExpression(*stage, context);
        }
        return current;
    }

    if (auto field = dynamic_cast<const FieldExpr*>(&expression)) {
        return analyzeField(*field, TypeInfo::named("null"), context);
    }

    if (auto fork = dynamic_cast<const ForkExpr*>(&expression)) {
        return analyzeFork(*fork, TypeInfo::named("null"), context);
    }

    if (auto loop = dynamic_cast<const LoopExpr*>(&expression)) {
        return analyzeLoop(*loop, TypeInfo::named("null"), context);
    }

    if (auto capture = dynamic_cast<const CaptureExpr*>(&expression)) {
        if (auto variable = context.lookupVariable(capture->name)) {
            return variable->type;
        }
        return typeFromAnnotation(context, capture->typeName);
    }

    if (dynamic_cast<const WildcardExpr*>(&expression)) {
        return TypeInfo::unknown();
    }

    throw std::runtime_error("unsupported expression type");
}

TypeInfo analyzeField(const FieldExpr& field, const TypeInfo& input, SemanticContext& context) {
    SemanticContext local{&context, {}, {}, {}};
    TypeInfo boundType = typeFromAnnotation(context, field.inputTypeName);
    requireAssignable(context, input, boundType);
    local.defineVariable(field.inputName.value_or("__input"), field.inputTypeName ? boundType : input, true);
    TypeInfo result = TypeInfo::named("null");
    for (const auto& statement : field.body) {
        result = analyzeStatement(*statement, local);
    }
    return result;
}

TypeInfo analyzeFork(const ForkExpr& fork, const TypeInfo& input, SemanticContext& context) {
    SemanticContext local{&context, {}, {}, {}};
    local.defineVariable("__input", input, true);
    TypeInfo result = TypeInfo::unknown();
    bool first = true;
    for (const auto& arm : fork.arms) {
        if (!dynamic_cast<const WildcardExpr*>(arm.condition.get())) {
            requireAssignable(local, inferExpression(*arm.condition, local), TypeInfo::named("bool"));
        }
        TypeInfo armType = inferExpression(*arm.body, local);
        if (first) {
            result = armType;
            first = false;
        } else if (!result.isUnknown() && !armType.isUnknown()) {
            requireAssignable(local, armType, result);
        }
    }
    return result;
}

TypeInfo analyzeLoop(const LoopExpr& loop, const TypeInfo& input, SemanticContext& context) {
    SemanticContext local{&context, {}, {}, {}};
    local.defineVariable("__input", input, true);
    requireAssignable(local, inferExpression(*loop.condition, local), TypeInfo::named("bool"));
    return inferExpression(*loop.body, local);
}

TypeInfo analyzeStatement(const Statement& statement, SemanticContext& context) {
    if (auto decl = dynamic_cast<const StreamDecl*>(&statement)) {
        TypeInfo declaredType = typeFromAnnotation(context, decl->typeName);
        if (decl->names.size() > 1 && decl->initializers.size() == 1) {
            TypeInfo grouped = inferExpression(*decl->initializers.front(), context);
            if (grouped.kind == TypeInfo::Kind::List && grouped.elements.size() == decl->names.size()) {
                for (size_t i = 0; i < decl->names.size(); ++i) {
                    if (context.variables.contains(decl->names[i])) {
                        throw std::runtime_error("duplicate variable: " + decl->names[i]);
                    }
                    if (isReservedNamespaceRoot(decl->names[i])) {
                        throw std::runtime_error("reserved stdlib namespace: " + decl->names[i]);
                    }
                    requireAssignable(context, grouped.elements[i], declaredType);
                    context.defineVariable(decl->names[i], decl->typeName ? declaredType : grouped.elements[i], true);
                }
                return TypeInfo::named("null");
            }
        }
        for (size_t i = 0; i < decl->names.size(); ++i) {
            if (context.variables.contains(decl->names[i])) {
                throw std::runtime_error("duplicate variable: " + decl->names[i]);
            }
            if (isReservedNamespaceRoot(decl->names[i])) {
                throw std::runtime_error("reserved stdlib namespace: " + decl->names[i]);
            }
            TypeInfo valueType = TypeInfo::unknown();
            bool initialized = false;
            if (i < decl->initializers.size()) {
                valueType = inferExpression(*decl->initializers[i], context);
                requireAssignable(context, valueType, declaredType);
                initialized = true;
            }
            context.defineVariable(decl->names[i], decl->typeName ? declaredType : valueType, initialized);
        }
        return TypeInfo::named("null");
    }

    if (auto shape = dynamic_cast<const ShapeDecl*>(&statement)) {
        for (const auto& field : shape->fields) {
            if (field.typeName) {
                requireKnownType(context, *field.typeName);
            }
        }
        return TypeInfo::named("null");
    }

    if (auto gate = dynamic_cast<const GateDecl*>(&statement)) {
        SemanticContext local{&context, {}, {}, {}};
        for (const auto& param : gate->params) {
            local.defineVariable(param.name, typeFromAnnotation(context, param.typeName), true);
        }
        TypeInfo bodyType = inferExpression(*gate->body, local);
        requireAssignable(context, bodyType, typeFromAnnotation(context, gate->resultType));
        return TypeInfo::named("null");
    }

    if (auto exprStmt = dynamic_cast<const ExprStmt*>(&statement)) {
        return inferExpression(*exprStmt->expression, context);
    }

    if (dynamic_cast<const UseDecl*>(&statement)) {
        throw std::runtime_error("@use requires file-based module loading");
    }

    throw std::runtime_error("unknown statement type");
}

} // namespace

TypeInfo TypeInfo::unknown() {
    return TypeInfo{};
}

TypeInfo TypeInfo::named(std::string value) {
    TypeInfo type;
    type.kind = Kind::Named;
    type.name = std::move(value);
    return type;
}

TypeInfo TypeInfo::object(std::map<std::string, TypeInfo> value) {
    TypeInfo type;
    type.kind = Kind::ObjectLiteral;
    type.fields = std::move(value);
    return type;
}

TypeInfo TypeInfo::list(std::vector<TypeInfo> value) {
    TypeInfo type;
    type.kind = Kind::List;
    type.elements = std::move(value);
    return type;
}

bool TypeInfo::isUnknown() const {
    return kind == Kind::Unknown;
}

bool TypeInfo::isNamed(std::string_view expected) const {
    return kind == Kind::Named && name == expected;
}

std::string TypeInfo::describe() const {
    switch (kind) {
        case Kind::Unknown:
            return "unknown";
        case Kind::Named:
            return name;
        case Kind::ObjectLiteral:
            return "object";
        case Kind::List:
            return "list";
    }
    return "unknown";
}

void analyzeProgram(const Program& program) {
    SemanticContext root;

    for (const auto& statement : program.statements) {
        if (auto shape = dynamic_cast<const ShapeDecl*>(statement.get())) {
            if (root.shapes.contains(shape->name)) {
                throw std::runtime_error("duplicate shape: " + shape->name);
            }
            if (isReservedNamespaceRoot(shape->name)) {
                throw std::runtime_error("reserved stdlib namespace: " + shape->name);
            }
            root.shapes[shape->name] = shape;
        } else if (auto gate = dynamic_cast<const GateDecl*>(statement.get())) {
            if (root.gates.contains(gate->name)) {
                throw std::runtime_error("duplicate gate: " + gate->name);
            }
            if (isReservedNamespaceRoot(gate->name)) {
                throw std::runtime_error("reserved stdlib namespace: " + gate->name);
            }
            root.gates[gate->name] = gate;
        }
    }

    for (const auto& statement : program.statements) {
        analyzeStatement(*statement, root);
    }
}

} // namespace dagger
