#pragma once

#include "sir.h"
#include "parser.h"
#include "runtime.h"
#include "lexer.h"
#include <memory>
#include <filesystem>
#include <string>
#include <string_view>

namespace dagger {

struct RunResult {
    bool ok = false;
    Value lastValue;
    std::string stdoutText;
    std::string stderrText;
    std::string errorMessage;
};

std::vector<Token> lexSource(std::string_view source);
std::unique_ptr<Program> parseSource(std::string_view source);
std::unique_ptr<Program> parseFile(const std::filesystem::path& path);
SIRProgram lowerProgram(Program& program);
RunResult runSource(std::string_view source, Interpreter& interpreter, bool printResult = false);
RunResult runSource(std::string_view source, bool printResult = false);
RunResult runFile(const std::filesystem::path& path, Interpreter& interpreter, bool printResult = false);
RunResult runFile(const std::filesystem::path& path, bool printResult = false);

} // namespace dagger
