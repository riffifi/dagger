#include "driver.h"
#include "semantics.h"
#include "stdlib.h"
#include "lowering.h"
#include <exception>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <set>
#include <sstream>
#include <stdexcept>
#include <utility>

namespace dagger {

namespace fs = std::filesystem;

namespace {

struct ModuleSymbols {
    std::set<std::string> functions;
    std::set<std::string> types;
    std::set<std::string> streams;

    bool contains(std::string_view name) const {
        std::string key(name);
        return functions.contains(key) || types.contains(key) || streams.contains(key);
    }

    bool isType(std::string_view name) const {
        return types.contains(std::string(name));
    }
};

std::string readFileText(const fs::path& path) {
    std::ifstream input(path);
    if (!input) {
        throw std::runtime_error("unable to open " + path.string());
    }
    return std::string(std::istreambuf_iterator<char>(input), std::istreambuf_iterator<char>());
}

std::string qualifyName(std::string_view moduleName, std::string_view localName) {
    return std::string(moduleName) + "." + std::string(localName);
}

fs::path modulePathForName(std::string_view moduleName) {
    fs::path path;
    std::string current;
    for (char c : moduleName) {
        if (c == '.') {
            if (!current.empty()) {
                path /= current;
                current.clear();
            }
        } else {
            current.push_back(c);
        }
    }
    if (!current.empty()) {
        path /= current;
    }
    path += ".dag";
    return path;
}

fs::path resolveModulePath(std::string_view moduleName, const fs::path& importerPath) {
    const fs::path relative = modulePathForName(moduleName);
    const fs::path localPath = importerPath.parent_path() / relative;
    if (fs::exists(localPath)) {
        return fs::weakly_canonical(localPath);
    }

#ifdef DAGGER_STDLIB_DIR
    const fs::path stdlibPath = fs::path(DAGGER_STDLIB_DIR) / relative;
    if (fs::exists(stdlibPath)) {
        return fs::weakly_canonical(stdlibPath);
    }
#endif

    // Default to current directory
    return fs::weakly_canonical(relative);
}

ModuleSymbols collectModuleSymbols(const Program& program) {
    ModuleSymbols symbols;
    for (const auto& statement : program.statements) {
        if (auto func = dynamic_cast<const FunctionDecl*>(statement.get())) {
            symbols.functions.insert(func->name);
        } else if (auto type = dynamic_cast<const TypeDecl*>(statement.get())) {
            symbols.types.insert(type->name);
        } else if (auto stream = dynamic_cast<const StreamDecl*>(statement.get())) {
            for (const auto& name : stream->names) {
                symbols.streams.insert(name);
            }
        }
    }
    return symbols;
}

void rewriteTypeName(std::optional<std::string>& typeName, const ModuleSymbols& symbols, std::string_view moduleName) {
    if (typeName && symbols.isType(*typeName)) {
        *typeName = qualifyName(moduleName, *typeName);
    }
}

bool shouldRewriteIdentifier(std::string_view name,
                             const std::set<std::string>& localNames,
                             const ModuleSymbols& symbols) {
    if (symbols.contains(name)) {
        if (localNames.contains(std::string(name))) {
            return false;
        }
        const size_t dot = name.find('.');
        if (dot != std::string_view::npos && localNames.contains(std::string(name.substr(0, dot)))) {
            return false;
        }
        if (isStdlibFunctionName(name) || hasReservedNamespaceRoot(name)) {
            return false;
        }
        return true;
    }
    return false;
}

void rewriteExpression(Expression& expression,
                       std::set<std::string>& localNames,
                       const ModuleSymbols& symbols,
                       std::string_view moduleName);

void rewriteStatement(Statement& statement,
                      std::set<std::string>& localNames,
                      const ModuleSymbols& symbols,
                      std::string_view moduleName,
                      bool topLevel) {
    if (auto stream = dynamic_cast<StreamDecl*>(&statement)) {
        rewriteTypeName(stream->typeName, symbols, moduleName);
        for (auto& initializer : stream->initializers) {
            rewriteExpression(*initializer, localNames, symbols, moduleName);
        }
        if (topLevel) {
            for (auto& name : stream->names) {
                name = qualifyName(moduleName, name);
            }
        } else {
            for (const auto& name : stream->names) {
                localNames.insert(name);
            }
        }
        return;
    }

    if (auto type = dynamic_cast<TypeDecl*>(&statement)) {
        for (auto& field : type->fields) {
            rewriteTypeName(field.typeName, symbols, moduleName);
        }
        if (topLevel) {
            type->name = qualifyName(moduleName, type->name);
        }
        return;
    }

    if (auto func = dynamic_cast<FunctionDecl*>(&statement)) {
        for (auto& param : func->params) {
            rewriteTypeName(param.typeName, symbols, moduleName);
        }
        rewriteTypeName(func->resultType, symbols, moduleName);
        std::set<std::string> funcLocals;
        for (const auto& param : func->params) {
            funcLocals.insert(param.name);
        }
        rewriteExpression(*func->body, funcLocals, symbols, moduleName);
        if (topLevel) {
            func->name = qualifyName(moduleName, func->name);
        }
        return;
    }

    if (auto exprStmt = dynamic_cast<ExprStmt*>(&statement)) {
        rewriteExpression(*exprStmt->expression, localNames, symbols, moduleName);
        return;
    }
    
    if (auto dirStmt = dynamic_cast<DirectiveStmt*>(&statement)) {
        for (auto& arg : dirStmt->arguments) {
            rewriteExpression(*arg, localNames, symbols, moduleName);
        }
        return;
    }
}

void rewriteBlockBody(std::vector<std::unique_ptr<Statement>>& body,
                      std::set<std::string> localNames,
                      const ModuleSymbols& symbols,
                      std::string_view moduleName) {
    for (auto& statement : body) {
        rewriteStatement(*statement, localNames, symbols, moduleName, false);
    }
}

void rewriteExpression(Expression& expression,
                       std::set<std::string>& localNames,
                       const ModuleSymbols& symbols,
                       std::string_view moduleName) {
    if (auto identifier = dynamic_cast<IdentifierExpr*>(&expression)) {
        if (shouldRewriteIdentifier(identifier->name, localNames, symbols)) {
            identifier->name = qualifyName(moduleName, identifier->name);
        }
        return;
    }

    if (auto prefix = dynamic_cast<PrefixExpr*>(&expression)) {
        rewriteExpression(*prefix->right, localNames, symbols, moduleName);
        return;
    }

    if (auto compare = dynamic_cast<ProbeCompareExpr*>(&expression)) {
        rewriteExpression(*compare->right, localNames, symbols, moduleName);
        return;
    }

    if (auto list = dynamic_cast<ListExpr*>(&expression)) {
        for (auto& item : list->items) {
            rewriteExpression(*item, localNames, symbols, moduleName);
        }
        return;
    }

    if (auto object = dynamic_cast<StructLiteralExpr*>(&expression)) {
        for (auto& field : object->fields) {
            rewriteExpression(*field.value, localNames, symbols, moduleName);
        }
        return;
    }

    if (auto call = dynamic_cast<CallExpr*>(&expression)) {
        rewriteExpression(*call->callee, localNames, symbols, moduleName);
        for (auto& arg : call->arguments) {
            rewriteExpression(*arg, localNames, symbols, moduleName);
        }
        return;
    }

    if (auto route = dynamic_cast<RouteExpr*>(&expression)) {
        rewriteExpression(*route->source, localNames, symbols, moduleName);
        std::set<std::string> routeLocals = localNames;
        for (auto& stage : route->stages) {
            rewriteExpression(*stage, routeLocals, symbols, moduleName);
            if (auto capture = dynamic_cast<CaptureExpr*>(stage.get())) {
                routeLocals.insert(capture->name);
            }
        }
        return;
    }

    if (auto block = dynamic_cast<BlockExpr*>(&expression)) {
        rewriteTypeName(block->inputTypeName, symbols, moduleName);
        std::set<std::string> blockLocals = localNames;
        if (block->inputName) {
            blockLocals.insert(*block->inputName);
        }
        rewriteBlockBody(block->body, std::move(blockLocals), symbols, moduleName);
        return;
    }

    if (auto fork = dynamic_cast<ForkExpr*>(&expression)) {
        for (auto& arm : fork->arms) {
            std::set<std::string> branchLocals = localNames;
            rewriteExpression(*arm.condition, branchLocals, symbols, moduleName);
            rewriteExpression(*arm.body, branchLocals, symbols, moduleName);
        }
        return;
    }

    if (auto loop = dynamic_cast<LoopExpr*>(&expression)) {
        std::set<std::string> loopLocals = localNames;
        rewriteExpression(*loop->condition, loopLocals, symbols, moduleName);
        rewriteExpression(*loop->body, loopLocals, symbols, moduleName);
        return;
    }
    
    if (auto each = dynamic_cast<EachExpr*>(&expression)) {
        std::set<std::string> eachLocals = localNames;
        rewriteExpression(*each->source, eachLocals, symbols, moduleName);
        eachLocals.insert(each->itemName);
        rewriteExpression(*each->body, eachLocals, symbols, moduleName);
        return;
    }

    if (auto capture = dynamic_cast<CaptureExpr*>(&expression)) {
        rewriteTypeName(capture->typeName, symbols, moduleName);
        return;
    }
}

void prefixModuleProgram(Program& program, std::string_view moduleName) {
    const ModuleSymbols symbols = collectModuleSymbols(program);
    std::set<std::string> topLevelLocals;
    for (auto& statement : program.statements) {
        rewriteStatement(*statement, topLevelLocals, symbols, moduleName, true);
    }
}

class ModuleResolver {
public:
    std::unique_ptr<Program> load(const fs::path& path, bool isRoot, std::optional<std::string> moduleName = std::nullopt) {
        const fs::path canonical = fs::exists(path) ? fs::weakly_canonical(path) : path;
        const std::string key = canonical.string();

        if (!isRoot && loaded_.contains(key)) {
            return std::make_unique<Program>();
        }
        loaded_.insert(key);

        if (!fs::exists(canonical)) {
             return std::make_unique<Program>();
        }

        auto rawProgram = parseSource(readFileText(canonical));
        auto program = std::make_unique<Program>();

        std::vector<std::unique_ptr<Statement>> ownStatements;
        for (auto& statement : rawProgram->statements) {
            if (auto use = dynamic_cast<UseDecl*>(statement.get())) {
                const fs::path importPath = resolveModulePath(use->moduleName, canonical);
                auto imported = load(importPath, false, use->moduleName);
                for (auto& importedStmt : imported->statements) {
                    program->statements.push_back(std::move(importedStmt));
                }
            } else {
                ownStatements.push_back(std::move(statement));
            }
        }

        auto ownProgram = std::make_unique<Program>();
        ownProgram->statements = std::move(ownStatements);
        if (moduleName) {
            prefixModuleProgram(*ownProgram, *moduleName);
        }

        for (auto& statement : ownProgram->statements) {
            program->statements.push_back(std::move(statement));
        }

        return program;
    }

private:
    std::set<std::string> loaded_;
};

RunResult executeProgram(std::unique_ptr<Program> program, Interpreter& interpreter, bool printResult) {
    RunResult result;
    std::ostringstream stdoutBuffer;
    std::ostringstream stderrBuffer;

    auto* oldCout = std::cout.rdbuf(stdoutBuffer.rdbuf());
    auto* oldCerr = std::cerr.rdbuf(stderrBuffer.rdbuf());

    try {
        if (!program) {
            result.errorMessage = "parse failed";
        } else {
            analyzeProgram(*program);
            result.lastValue = interpreter.execute(*program, printResult);
            result.ok = true;
        }
    } catch (const std::exception& ex) {
        result.errorMessage = ex.what();
    }

    std::cout.rdbuf(oldCout);
    std::cerr.rdbuf(oldCerr);
    result.stdoutText = stdoutBuffer.str();
    result.stderrText = stderrBuffer.str();
    return result;
}

} // namespace

std::vector<Token> lexSource(std::string_view source) {
    Lexer lexer(source);
    return lexer.tokenize();
}

std::unique_ptr<Program> parseSource(std::string_view source) {
    Parser parser(lexSource(source));
    return parser.parse();
}

std::unique_ptr<Program> parseFile(const fs::path& path) {
    ModuleResolver resolver;
    return resolver.load(path, true);
}

SIRProgram lowerProgram(Program& program) {
    analyzeProgram(program);
    Lowerer lowerer(program);
    return lowerer.lower();
}

RunResult runSource(std::string_view source, Interpreter& interpreter, bool printResult) {
    try {
        return executeProgram(parseSource(source), interpreter, printResult);
    } catch (const std::exception& ex) {
        RunResult result;
        result.errorMessage = ex.what();
        return result;
    }
}

RunResult runSource(std::string_view source, bool printResult) {
    Interpreter interpreter;
    return runSource(source, interpreter, printResult);
}

RunResult runFile(const fs::path& path, Interpreter& interpreter, bool printResult) {
    try {
        return executeProgram(parseFile(path), interpreter, printResult);
    } catch (const std::exception& ex) {
        RunResult result;
        result.errorMessage = ex.what();
        return result;
    }
}

RunResult runFile(const fs::path& path, bool printResult) {
    Interpreter interpreter;
    return runFile(path, interpreter, printResult);
}

} // namespace dagger
