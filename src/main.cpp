#include "driver.h"
#include <fstream>
#include <iostream>
#include <sstream>

static void runFile(const std::string& path) {
    std::ifstream input(path);
    if (!input) {
        std::cerr << "error: unable to open file '" << path << "'\n";
        return;
    }

    std::ostringstream buffer;
    buffer << input.rdbuf();
    auto result = dagger::runSource(buffer.str(), false);
    std::cout << result.stdoutText;
    std::cerr << result.stderrText;
    if (!result.ok && !result.errorMessage.empty()) {
        std::cerr << "dagc: " << result.errorMessage << "\n";
    }
}

static void repl() {
    dagger::Interpreter interpreter;
    std::string line;

    std::cout << "Dagger REPL (type :quit or :exit to leave)\n";
    while (true) {
        std::cout << "> ";
        if (!std::getline(std::cin, line)) {
            break;
        }

        if (line.empty()) {
            continue;
        }

        if (line == ":quit" || line == ":exit" || line == ":q") {
            break;
        }

        if (line == ":help" || line == "?" ) {
            std::cout << "Dagger REPL commands:\n";
            std::cout << "  :quit, :exit, :q   exit the REPL\n";
            std::cout << "  :help, ?           show this help text\n";
            continue;
        }

        auto result = dagger::runSource(line, interpreter, true);
        std::cout << result.stdoutText;
        std::cerr << result.stderrText;
        if (!result.ok && !result.errorMessage.empty()) {
            std::cerr << "dagc: " << result.errorMessage << "\n";
        }
    }
}

int main(int argc, char** argv) {
    if (argc == 1 || (argc == 2 && (std::string(argv[1]) == "--repl" || std::string(argv[1]) == "-r"))) {
        repl();
        return 0;
    }

    if (argc == 2) {
        runFile(argv[1]);
        return 0;
    }

    std::cout << "Usage: dagc [--repl] <source.dag>\n";
    return 1;
}
