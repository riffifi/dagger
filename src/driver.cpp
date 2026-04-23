#include "driver.h"
#include <exception>
#include <iostream>
#include <sstream>

namespace dagger {

std::vector<Token> lexSource(std::string_view source) {
    Lexer lexer(source);
    return lexer.tokenize();
}

std::unique_ptr<Program> parseSource(std::string_view source) {
    Parser parser(lexSource(source));
    return parser.parse();
}

RunResult runSource(std::string_view source, Interpreter& interpreter, bool printResult) {
    RunResult result;
    std::ostringstream stdoutBuffer;
    std::ostringstream stderrBuffer;

    auto* oldCout = std::cout.rdbuf(stdoutBuffer.rdbuf());
    auto* oldCerr = std::cerr.rdbuf(stderrBuffer.rdbuf());

    try {
        auto program = parseSource(source);
        if (!program) {
            result.errorMessage = "parse failed";
        } else {
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

RunResult runSource(std::string_view source, bool printResult) {
    Interpreter interpreter;
    return runSource(source, interpreter, printResult);
}

} // namespace dagger
