#include "driver.h"
#include "regalloc.h"
#include "codegen.h"
#include <cstdlib>
#include <fstream>
#include <iostream>
#include <optional>
#include <string>
#include <termios.h>
#include <unistd.h>
#include <vector>

namespace {

class RawTerminalMode {
public:
    RawTerminalMode() {
        enabled_ = ::isatty(STDIN_FILENO) != 0;
        if (!enabled_) {
            return;
        }
        if (::tcgetattr(STDIN_FILENO, &original_) != 0) {
            enabled_ = false;
            return;
        }

        termios raw = original_;
        raw.c_iflag &= static_cast<unsigned long>(~(BRKINT | ICRNL | INPCK | ISTRIP | IXON));
        raw.c_oflag &= static_cast<unsigned long>(~(OPOST));
        raw.c_cflag |= CS8;
        raw.c_lflag &= static_cast<unsigned long>(~(ECHO | ICANON | IEXTEN | ISIG));
        raw.c_cc[VMIN] = 1;
        raw.c_cc[VTIME] = 0;

        if (::tcsetattr(STDIN_FILENO, TCSAFLUSH, &raw) != 0) {
            enabled_ = false;
        }
    }

    ~RawTerminalMode() {
        if (enabled_) {
            ::tcsetattr(STDIN_FILENO, TCSAFLUSH, &original_);
        }
    }

    bool enabled() const {
        return enabled_;
    }

private:
    bool enabled_ = false;
    termios original_{};
};

std::optional<std::string> historyFilePath() {
    const char* home = std::getenv("HOME");
    if (!home || *home == '\0') {
        return std::nullopt;
    }
    return std::string(home) + "/.dagger_history";
}

std::vector<std::string> loadHistory() {
    std::vector<std::string> history;
    auto path = historyFilePath();
    if (!path) {
        return history;
    }

    std::ifstream input(*path);
    std::string line;
    while (std::getline(input, line)) {
        if (!line.empty()) {
            history.push_back(line);
        }
    }
    return history;
}

void appendHistoryLine(const std::string& line) {
    if (line.empty()) {
        return;
    }
    auto path = historyFilePath();
    if (!path) {
        return;
    }

    std::ofstream output(*path, std::ios::app);
    if (output) {
        output << line << '\n';
    }
}

void refreshReplLine(const std::string& prompt, const std::string& buffer, size_t cursor) {
    std::cout << '\r' << prompt << buffer << "\x1b[K";
    const size_t tail = buffer.size() - cursor;
    if (tail > 0) {
        std::cout << "\x1b[" << tail << 'D';
    }
    std::cout.flush();
}

std::optional<std::string> readReplLine(const std::string& prompt, std::vector<std::string>& history) {
    if (::isatty(STDIN_FILENO) == 0) {
        std::cout << prompt;
        std::cout.flush();
        std::string line;
        if (!std::getline(std::cin, line)) {
            return std::nullopt;
        }
        return line;
    }

    RawTerminalMode rawMode;
    if (!rawMode.enabled()) {
        std::cout << prompt;
        std::cout.flush();
        std::string line;
        if (!std::getline(std::cin, line)) {
            return std::nullopt;
        }
        return line;
    }

    std::string buffer;
    size_t cursor = 0;
    size_t historyIndex = history.size();
    std::string draft;

    std::cout << prompt;
    std::cout.flush();

    while (true) {
        char c = '\0';
        const ssize_t readCount = ::read(STDIN_FILENO, &c, 1);
        if (readCount <= 0) {
            std::cout << '\n';
            return std::nullopt;
        }

        if (c == '\r' || c == '\n') {
            std::cout << '\r' << prompt << buffer << "\x1b[K\n";
            return buffer;
        }

        if (c == 4) {
            if (buffer.empty()) {
                std::cout << '\n';
                return std::nullopt;
            }
            continue;
        }

        if (c == 127 || c == 8) {
            if (cursor > 0) {
                buffer.erase(cursor - 1, 1);
                cursor--;
                refreshReplLine(prompt, buffer, cursor);
            }
            continue;
        }

        if (c == '\x1b') {
            char seq[3] = {'\0', '\0', '\0'};
            if (::read(STDIN_FILENO, &seq[0], 1) <= 0 || ::read(STDIN_FILENO, &seq[1], 1) <= 0) {
                continue;
            }
            if (seq[0] == '[') {
                if (seq[1] == 'A') {
                    if (history.empty()) {
                        continue;
                    }
                    if (historyIndex == history.size()) {
                        draft = buffer;
                    }
                    if (historyIndex > 0) {
                        historyIndex--;
                        buffer = history[historyIndex];
                        cursor = buffer.size();
                        refreshReplLine(prompt, buffer, cursor);
                    }
                    continue;
                }
                if (seq[1] == 'B') {
                    if (historyIndex < history.size()) {
                        historyIndex++;
                        if (historyIndex == history.size()) {
                            buffer = draft;
                        } else {
                            buffer = history[historyIndex];
                        }
                        cursor = buffer.size();
                        refreshReplLine(prompt, buffer, cursor);
                    }
                    continue;
                }
                if (seq[1] == 'C') {
                    if (cursor < buffer.size()) {
                        cursor++;
                        refreshReplLine(prompt, buffer, cursor);
                    }
                    continue;
                }
                if (seq[1] == 'D') {
                    if (cursor > 0) {
                        cursor--;
                        refreshReplLine(prompt, buffer, cursor);
                    }
                    continue;
                }
                if (seq[1] == 'H') {
                    cursor = 0;
                    refreshReplLine(prompt, buffer, cursor);
                    continue;
                }
                if (seq[1] == 'F') {
                    cursor = buffer.size();
                    refreshReplLine(prompt, buffer, cursor);
                    continue;
                }
                if (seq[1] == '3') {
                    if (::read(STDIN_FILENO, &seq[2], 1) > 0 && seq[2] == '~' && cursor < buffer.size()) {
                        buffer.erase(cursor, 1);
                        refreshReplLine(prompt, buffer, cursor);
                    }
                    continue;
                }
            }
            continue;
        }

        if (static_cast<unsigned char>(c) < 32) {
            continue;
        }

        buffer.insert(buffer.begin() + static_cast<std::ptrdiff_t>(cursor), c);
        cursor++;
        historyIndex = history.size();
        refreshReplLine(prompt, buffer, cursor);
    }
}

static void runFile(const std::string& path) {
    auto result = dagger::runFile(path, false);
    std::cout << result.stdoutText;
    std::cerr << result.stderrText;
    if (!result.ok && !result.errorMessage.empty()) {
        std::cerr << "dagc: " << result.errorMessage << "\n";
    }
}

static void repl() {
    dagger::Interpreter interpreter;
    std::vector<std::string> history = loadHistory();

    std::cout << "Dagger REPL (type :quit or :exit to leave)\n";
    while (true) {
        auto maybeLine = readReplLine("> ", history);
        if (!maybeLine) {
            break;
        }

        std::string line = *maybeLine;
        if (line.empty()) {
            continue;
        }

        if (history.empty() || history.back() != line) {
            history.push_back(line);
            appendHistoryLine(line);
        }

        if (line == ":quit" || line == ":exit" || line == ":q") {
            break;
        }

        if (line == ":help" || line == "?") {
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

} // namespace

int main(int argc, char** argv) {
    if (argc == 1 || (argc == 2 && (std::string(argv[1]) == "--repl" || std::string(argv[1]) == "-r"))) {
        repl();
        return 0;
    }

    if (argc == 2) {
        runFile(argv[1]);
        return 0;
    }

    if (argc == 3 && std::string(argv[1]) == "-S") {
        auto program = dagger::parseFile(argv[2]);
        if (program) {
            auto sir = dagger::lowerProgram(*program);
            dagger::GreedyRegisterAllocator regAlloc;
            std::map<std::string, dagger::AllocationResult> allocations;
            for (const auto& func : sir.functions) {
                allocations[func.name] = regAlloc.allocate(func);
            }
            dagger::CodeGenerator codegen;
            std::cout << codegen.generate(sir, allocations);
        }
        return 0;
    }

    if (argc == 3 && std::string(argv[1]) == "--emit-sir") {
        auto program = dagger::parseFile(argv[2]);
        if (program) {
            auto sir = dagger::lowerProgram(*program);
            dagger::printSIR(sir);
            
            std::cout << "--- Register Allocation ---\n";
            dagger::GreedyRegisterAllocator regAlloc;
            for (const auto& func : sir.functions) {
                std::cout << "fn " << func.name << ":\n";
                auto allocation = regAlloc.allocate(func);
                for (const auto& [vreg, reg] : allocation.assignments) {
                    std::cout << "  %" << vreg << " -> " << dagger::registerName(reg) << "\n";
                }
                for (const auto& [vreg, slot] : allocation.stackSlots) {
                    std::cout << "  %" << vreg << " -> stack[" << slot << "]\n";
                }
                std::cout << "  Total stack space: " << allocation.totalStackSpace << "\n";
            }
        }
        return 0;
    }

    std::cout << "Usage: dagc [--repl] [--emit-sir] <source.dag>\n";
    return 1;
}
