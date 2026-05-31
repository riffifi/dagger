#include "semantics.h"
#include "stdlib.h"
#include <set>
#include <stdexcept>
#include <map>

namespace dagger {

namespace {

bool isBuiltinTypeName(std::string_view typeName) {
    static const std::set<std::string, std::less<>> builtinTypes{
        "int", "int8", "int16", "int32", "int64",
        "uint", "uint8", "uint16", "uint32", "uint64",
        "byte", "float", "float32", "float64", "bool",
        "text", "char", "null",
    };
    return builtinTypes.contains(std::string(typeName));
}

bool isIntegerTypeName(std::string_view typeName) {
    static const std::set<std::string, std::less<>> integerTypes{
        "int", "int8", "int16", "int32", "int64",
        "uint", "uint8", "uint16", "uint32", "uint64", "byte",
    };
    return integerTypes.contains(std::string(typeName));
}

bool isFloatTypeName(std::string_view typeName) {
    return typeName == "float" || typeName == "float32" || typeName == "float64";
}

struct SemanticContext {
    struct VariableInfo {
        TypeInfo type;
        bool initialized = false;
        bool burst = false;
    };

    SemanticContext* parent = nullptr;
    std::map<std::string, VariableInfo> variables;
    std::map<std::string, FunctionDecl*> functions;
    std::map<std::string, TypeDecl*> types;

    std::optional<VariableInfo*> lookupVariable(std::string_view name) {
        auto it = variables.find(std::string(name));
        if (it != variables.end()) {
            return &it->second;
        }
        if (parent) {
            return parent->lookupVariable(name);
        }
        return std::nullopt;
    }

    FunctionDecl* lookupFunction(std::string_view name) const {
        auto it = functions.find(std::string(name));
        if (it != functions.end()) {
            return it->second;
        }
        if (parent) {
            return parent->lookupFunction(name);
        }
        return nullptr;
    }

    TypeDecl* lookupType(std::string_view name) const {
        auto it = types.find(std::string(name));
        if (it != types.end()) {
            return it->second;
        }
        if (parent) {
            return parent->lookupType(name);
        }
        return nullptr;
    }

    void defineVariable(std::string name, TypeInfo type, bool initialized) {
        if (variables.contains(name)) {
            throw std::runtime_error("duplicate variable: " + name);
        }
        variables[std::move(name)] = VariableInfo{std::move(type), initialized};
    }

    bool assignVariable(std::string_view name, TypeInfo type, bool initialized) {
        auto it = variables.find(std::string(name));
        if (it != variables.end()) {
            if (it->second.burst) {
                throw std::runtime_error("assigning to burst variable: " + std::string(name));
            }
            it->second = VariableInfo{std::move(type), initialized, false};
            return true;
        }
        if (parent) {
            return parent->assignVariable(name, std::move(type), initialized);
        }
        return false;
    }
};

void requireKnownType(const SemanticContext& context, std::string_view typeName) {
    if (isBuiltinTypeName(typeName) || context.lookupType(typeName)) {
        return;
    }
    // Handle pointer types like &int or &Point
    if (typeName.size() > 1 && typeName[0] == '&') {
        requireKnownType(context, typeName.substr(1));
        return;
    }
    // Handle parameterized types like block[N] or slice[T]
    if (typeName.starts_with("block[") || typeName.starts_with("slice[")) {
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
    TypeInfo current = base;
    size_t offset = 0;
    while (offset < fieldName.size()) {
        const size_t nextDot = fieldName.find('.', offset);
        const std::string_view part = fieldName.substr(offset, nextDot == std::string_view::npos ? fieldName.size() - offset : nextDot - offset);
        
        if (current.kind == TypeInfo::Kind::ObjectLiteral) {
            auto it = current.fields.find(std::string(part));
            if (it == current.fields.end()) {
                throw std::runtime_error("unknown field: " + std::string(fieldName));
            }
            current = it->second;
        } else if (current.kind == TypeInfo::Kind::Named) {
            if (current.name == "text") {
                throw std::runtime_error("field access on non-object value");
            }
            if (auto* type = context.lookupType(current.name)) {
                bool found = false;
                for (const auto& field : type->fields) {
                    if (field.name == part) {
                        current = typeFromAnnotation(context, field.typeName);
                        found = true;
                        break;
                    }
                }
                if (!found) {
                    throw std::runtime_error("unknown field: " + std::string(fieldName));
                }
            } else {
                throw std::runtime_error("field access on non-object value");
            }
        } else if (!current.isUnknown()) {
            throw std::runtime_error("field access on non-object value");
        } else {
            return TypeInfo::unknown();
        }

        if (nextDot == std::string_view::npos) break;
        offset = nextDot + 1;
    }
    return current;
}

void requireAssignable(const SemanticContext& context, const TypeInfo& actual, const TypeInfo& expected) {
    if (expected.isUnknown() || actual.isUnknown()) {
        return;
    }

    if (expected.kind == TypeInfo::Kind::Named && actual.kind == TypeInfo::Kind::Named) {
        const bool compatibleIntegers = isIntegerTypeName(expected.name) && isIntegerTypeName(actual.name);
        const bool compatibleFloats = isFloatTypeName(expected.name) && isFloatTypeName(actual.name);
        
        // Handle pointer compatibility
        if (expected.name.size() > 1 && expected.name[0] == '&' && actual.name.size() > 1 && actual.name[0] == '&') {
             // For now, pointers must match exactly or be compatible with generic &byte
             if (expected.name == actual.name || expected.name == "&byte") return;
        }

        // text to &byte compatibility (for FFI)
        if (expected.name == "&byte" && actual.name == "text") return;

        if (expected.name != actual.name && !compatibleIntegers && !compatibleFloats) {
            throw std::runtime_error("type mismatch: expected " + expected.describe() + ", got " + actual.describe());
        }
        return;
    }

    if (expected.kind == TypeInfo::Kind::Named) {
        if (auto* type = context.lookupType(expected.name)) {
            if (actual.kind != TypeInfo::Kind::ObjectLiteral) {
                throw std::runtime_error("type mismatch: expected " + expected.describe() + ", got " + actual.describe());
            }
            for (const auto& field : type->fields) {
                auto it = actual.fields.find(field.name);
                if (it == actual.fields.end()) {
                    throw std::runtime_error("type mismatch: missing field '" + field.name + "' for " + expected.name);
                }
                requireAssignable(context, it->second, typeFromAnnotation(context, field.typeName));
            }
            for (const auto& [fieldName, _] : actual.fields) {
                bool found = false;
                for (const auto& field : type->fields) {
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

TypeInfo inferExpression(Expression& expression, SemanticContext& context);

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

    if (name == "add" || name == "sub" || name == "mul" || name == "div") {
        requireArgCount(2);
        if (!args[0].isUnknown() && !isNumericType(args[0])) {
            throw std::runtime_error(std::string(name) + " expects numeric arguments");
        }
        if (!args[1].isUnknown() && !isNumericType(args[1])) {
            throw std::runtime_error(std::string(name) + " expects numeric arguments");
        }
        if (args[0].kind == TypeInfo::Kind::Named && isFloatTypeName(args[0].name)) return args[0];
        if (args[1].kind == TypeInfo::Kind::Named && isFloatTypeName(args[1].name)) return args[1];
        if (args[0].kind == TypeInfo::Kind::Named && isIntegerTypeName(args[0].name)) return args[0];
        if (args[1].kind == TypeInfo::Kind::Named && isIntegerTypeName(args[1].name)) return args[1];
        return TypeInfo::unknown();
    }

    if (name == "mod") {
        requireArgCount(2);
        requireAssignable(context, args[0], TypeInfo::named("int"));
        requireAssignable(context, args[1], TypeInfo::named("int"));
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

    if (name == "math.sqrt") {
        requireArgCount(1);
        if (!args[0].isUnknown() && !isNumericType(args[0])) {
            throw std::runtime_error("math.sqrt expects a numeric argument");
        }
        return TypeInfo::named("float");
    }

    if (name == "id") {
        requireArgCount(1);
        return args[0];
    }

    if (name == "const") {
        requireArgCount(2);
        return args[0];
    }

    if (name == "assert") {
        requireArgCount(1);
        requireAssignable(context, args[0], TypeInfo::named("bool"));
        return TypeInfo::named("null");
    }

    if (auto* func = context.lookupFunction(name)) {
        requireArgCount(func->params.size());
        for (size_t i = 0; i < args.size(); ++i) {
            requireAssignable(context, args[i], typeFromAnnotation(context, func->params[i].typeName));
        }
        return typeFromAnnotation(context, func->resultType);
    }

    if (hasReservedNamespaceRoot(name)) {
        throw std::runtime_error("unknown stdlib symbol: " + std::string(name));
    }

    return TypeInfo::unknown();
}

TypeInfo inferIdentifierType(std::string_view name, SemanticContext& context) {
    if (auto info = context.lookupVariable(name)) {
        if ((*info)->burst) {
             throw std::runtime_error("use of burst variable: " + std::string(name));
        }
        if (!(*info)->initialized) {
            throw std::runtime_error("use of uninitialized variable: " + std::string(name));
        }
        return (*info)->type;
    }

    if (context.lookupFunction(name)) {
        return TypeInfo::named("fn");
    }

    if (name == "_") return TypeInfo::named("null");

    if (isReservedNamespaceRoot(name)) {
        throw std::runtime_error("namespace '" + std::string(name) + "' is not a value");
    }

    if (const size_t dot = name.find('.'); dot != std::string_view::npos) {
        std::string baseName(name.substr(0, dot));
        std::string fieldName(name.substr(dot + 1));
        if (auto baseInfo = context.lookupVariable(baseName)) {
            if ((*baseInfo)->burst) {
                throw std::runtime_error("field access on burst variable: " + baseName);
            }
            return resolveFieldType(context, (*baseInfo)->type, fieldName);
        }
        if (isReservedNamespaceRoot(baseName)) {
            if (isStdlibFunctionName(name)) {
                return TypeInfo::named("fn");
            }
            throw std::runtime_error("unknown stdlib symbol: " + std::string(name));
        }
    }

    return TypeInfo::unknown();
}

TypeInfo analyzeBlock(BlockExpr& block, const TypeInfo& input, SemanticContext& context);
TypeInfo analyzeFork(ForkExpr& fork, const TypeInfo& input, SemanticContext& context);
TypeInfo analyzeLoop(LoopExpr& loop, const TypeInfo& input, SemanticContext& context);
TypeInfo analyzeStatement(Statement& statement, SemanticContext& context);

TypeInfo inferExpression(Expression& expression, SemanticContext& context) {
    auto inferInternal = [&](Expression& expr) -> TypeInfo {
        if (auto* literal = dynamic_cast<LiteralExpr*>(&expr)) {
            if (literal->value.index() == 0) return TypeInfo::named("null");
            if (std::holds_alternative<int64_t>(literal->value)) return TypeInfo::named("int");
            if (std::holds_alternative<double>(literal->value)) return TypeInfo::named("float");
            if (std::holds_alternative<bool>(literal->value)) return TypeInfo::named("bool");
            if (std::holds_alternative<char>(literal->value)) return TypeInfo::named("char");
            if (std::holds_alternative<std::string>(literal->value)) return TypeInfo::named("text");
        }

        if (auto* identifier = dynamic_cast<IdentifierExpr*>(&expr)) {
            return inferIdentifierType(identifier->name, context);
        }

        if (auto* prefix = dynamic_cast<PrefixExpr*>(&expr)) {
            if (prefix->op == "?" || prefix->op == "!") {
                auto* id = dynamic_cast<IdentifierExpr*>(prefix->right.get());
                if (!id) throw std::runtime_error("probe/burst requires an identifier");
                auto info = context.lookupVariable(id->name);
                if (!info) throw std::runtime_error("undefined variable: " + id->name);
                if ((*info)->burst) throw std::runtime_error("use of burst variable: " + id->name);
                if (prefix->op == "!") (*info)->burst = true;
                return (*info)->type;
            }

            TypeInfo type = inferExpression(*prefix->right, context);
            if (prefix->op == "-") {
                if (!type.isUnknown() && !isNumericType(type)) {
                    throw std::runtime_error("unexpected operand for unary '-'");
                }
            }
            return type;
        }

        if (auto* compare = dynamic_cast<ProbeCompareExpr*>(&expr)) {
            if (!context.lookupVariable("__input")) {
                throw std::runtime_error("probe comparison requires a routed input");
            }
            inferExpression(*compare->right, context);
            return TypeInfo::named("bool");
        }

        if (auto* list = dynamic_cast<ListExpr*>(&expr)) {
            std::vector<TypeInfo> items;
            for (auto& item : list->items) {
                items.push_back(inferExpression(*item, context));
            }
            return TypeInfo::list(std::move(items));
        }

        if (auto* object = dynamic_cast<StructLiteralExpr*>(&expr)) {
            std::map<std::string, TypeInfo> fields;
            for (auto& field : object->fields) {
                fields[field.name] = inferExpression(*field.value, context);
            }
            return TypeInfo::object(std::move(fields));
        }

        if (auto* call = dynamic_cast<CallExpr*>(&expr)) {
            auto* callee = dynamic_cast<IdentifierExpr*>(call->callee.get());
            if (!callee) throw std::runtime_error("unsupported call target");
            std::vector<TypeInfo> args;
            for (auto& arg : call->arguments) {
                auto flat = flattenType(inferExpression(*arg, context));
                args.insert(args.end(), flat.begin(), flat.end());
            }
            return analyzeCall(callee->name, args, context);
        }

        if (auto* route = dynamic_cast<RouteExpr*>(&expr)) {
            TypeInfo current = inferExpression(*route->source, context);
            for (auto& stage : route->stages) {
                if (auto* call = dynamic_cast<CallExpr*>(stage.get())) {
                    auto* callee = dynamic_cast<IdentifierExpr*>(call->callee.get());
                    if (!callee) throw std::runtime_error("unsupported call target");
                    std::vector<TypeInfo> args = flattenType(current);
                    for (auto& arg : call->arguments) {
                        auto flat = flattenType(inferExpression(*arg, context));
                        args.insert(args.end(), flat.begin(), flat.end());
                    }
                    current = analyzeCall(callee->name, args, context);
                } else if (auto* capture = dynamic_cast<CaptureExpr*>(stage.get())) {
                    TypeInfo expected = typeFromAnnotation(context, capture->typeName);
                    requireAssignable(context, current, expected);
                    context.defineVariable(capture->name, capture->typeName ? expected : current, true);
                    current = TypeInfo::named("null");
                } else if (auto* identifier = dynamic_cast<IdentifierExpr*>(stage.get())) {
                    if (auto info = context.lookupVariable(identifier->name)) {
                        requireAssignable(context, current, (*info)->type);
                        context.assignVariable(identifier->name, (*info)->type.isUnknown() ? current : (*info)->type, true);
                    } else {
                        current = analyzeCall(identifier->name, flattenType(current), context);
                    }
                } else if (auto* block = dynamic_cast<BlockExpr*>(stage.get())) {
                    current = analyzeBlock(*block, current, context);
                } else if (auto* fork = dynamic_cast<ForkExpr*>(stage.get())) {
                    current = analyzeFork(*fork, current, context);
                } else if (auto* loop = dynamic_cast<LoopExpr*>(stage.get())) {
                    current = analyzeLoop(*loop, current, context);
                } else {
                    current = inferExpression(*stage, context);
                }
            }
            return current;
        }

        if (auto* block = dynamic_cast<BlockExpr*>(&expr)) {
            return analyzeBlock(*block, TypeInfo::named("null"), context);
        }

        if (auto* fork = dynamic_cast<ForkExpr*>(&expr)) {
            return analyzeFork(*fork, TypeInfo::named("null"), context);
        }

        if (auto* loop = dynamic_cast<LoopExpr*>(&expr)) {
            return analyzeLoop(*loop, TypeInfo::named("null"), context);
        }

        if (auto* capture = dynamic_cast<CaptureExpr*>(&expr)) {
            if (auto info = context.lookupVariable(capture->name)) return (*info)->type;
            return typeFromAnnotation(context, capture->typeName);
        }

        if (dynamic_cast<WildcardExpr*>(&expr)) return TypeInfo::unknown();

        throw std::runtime_error("unsupported expression type");
    };

    TypeInfo type = inferInternal(expression);
    expression.inferredType = type;
    return type;
}

TypeInfo analyzeBlock(BlockExpr& block, const TypeInfo& input, SemanticContext& context) {
    SemanticContext local{&context};
    TypeInfo boundType = typeFromAnnotation(context, block.inputTypeName);
    requireAssignable(context, input, boundType);
    local.defineVariable(block.inputName.value_or("__input"), block.inputTypeName ? boundType : input, true);
    TypeInfo result = TypeInfo::named("null");
    for (auto& statement : block.body) {
        result = analyzeStatement(*statement, local);
    }
    return result;
}

TypeInfo analyzeFork(ForkExpr& fork, const TypeInfo& input, SemanticContext& context) {
    SemanticContext local{&context};
    local.defineVariable("__input", input, true);
    TypeInfo result = TypeInfo::unknown();
    bool first = true;
    for (auto& arm : fork.arms) {
        if (!dynamic_cast<WildcardExpr*>(arm.condition.get())) {
             if (auto ident = dynamic_cast<IdentifierExpr*>(arm.condition.get())) {
                 if (ident->name != "@ok" && ident->name != "@err") {
                      requireAssignable(local, inferExpression(*arm.condition, local), TypeInfo::named("bool"));
                 }
             } else {
                  requireAssignable(local, inferExpression(*arm.condition, local), TypeInfo::named("bool"));
             }
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

TypeInfo analyzeLoop(LoopExpr& loop, const TypeInfo& input, SemanticContext& context) {
    SemanticContext local{&context};
    local.defineVariable("__input", input, true);
    requireAssignable(local, inferExpression(*loop.condition, local), TypeInfo::named("bool"));
    return inferExpression(*loop.body, local);
}

TypeInfo analyzeStatement(Statement& statement, SemanticContext& context) {
    if (auto* decl = dynamic_cast<StreamDecl*>(&statement)) {
        for (const auto& name : decl->names) {
            if (isReservedNamespaceRoot(name)) {
                throw std::runtime_error("reserved stdlib namespace: " + name);
            }
        }
        TypeInfo declaredType = typeFromAnnotation(context, decl->typeName);
        if (decl->names.size() > 1 && decl->initializers.size() == 1) {
            TypeInfo grouped = inferExpression(*decl->initializers.front(), context);
            if (grouped.kind == TypeInfo::Kind::List && grouped.elements.size() == decl->names.size()) {
                for (size_t i = 0; i < decl->names.size(); ++i) {
                    requireAssignable(context, grouped.elements[i], declaredType);
                    context.defineVariable(decl->names[i], grouped.elements[i], true);
                }
                return TypeInfo::named("null");
            }
        }

        for (size_t i = 0; i < decl->names.size(); ++i) {
            TypeInfo valueType = TypeInfo::unknown();
            if (i < decl->initializers.size()) {
                valueType = inferExpression(*decl->initializers[i], context);
                requireAssignable(context, valueType, declaredType);
            }
            context.defineVariable(decl->names[i], declaredType.isUnknown() ? valueType : declaredType, i < decl->initializers.size());
        }
        return TypeInfo::named("null");
    }

    if (auto* typeDecl = dynamic_cast<TypeDecl*>(&statement)) {
        return TypeInfo::named("null");
    }

    if (auto* funcDecl = dynamic_cast<FunctionDecl*>(&statement)) {
        if (!funcDecl->body) {
             return TypeInfo::named("null");
        }
        SemanticContext local{&context};
        for (const auto& param : funcDecl->params) {
            local.defineVariable(param.name, typeFromAnnotation(context, param.typeName), true);
        }
        TypeInfo bodyType = inferExpression(*funcDecl->body, local);
        requireAssignable(local, bodyType, typeFromAnnotation(context, funcDecl->resultType));
        return TypeInfo::named("null");
    }

    if (auto* exprStmt = dynamic_cast<ExprStmt*>(&statement)) {
        return inferExpression(*exprStmt->expression, context);
    }
    
    if (auto* dirStmt = dynamic_cast<DirectiveStmt*>(&statement)) {
        return TypeInfo::named("null");
    }

    if (dynamic_cast<UseDecl*>(&statement)) {
        throw std::runtime_error("@use requires file-based module loading");
    }

    throw std::runtime_error("unknown statement type");
}

} // namespace

void analyzeProgram(Program& program) {
    SemanticContext root;
    for (auto& statement : program.statements) {
        if (auto* type = dynamic_cast<TypeDecl*>(statement.get())) {
            if (root.types.contains(type->name)) throw std::runtime_error("duplicate type: " + type->name);
            root.types[type->name] = type;
        } else if (auto* func = dynamic_cast<FunctionDecl*>(statement.get())) {
            if (root.functions.contains(func->name)) throw std::runtime_error("duplicate function: " + func->name);
            root.functions[func->name] = func;
        }
    }
    for (auto& statement : program.statements) {
        analyzeStatement(*statement, root);
    }
}

} // namespace dagger
