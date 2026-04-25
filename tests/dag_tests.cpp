#include "driver.h"
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <string>

namespace fs = std::filesystem;

static std::string readFile(const fs::path& path) {
    std::ifstream input(path);
    if (!input) {
        throw std::runtime_error("unable to open " + path.string());
    }
    return std::string(std::istreambuf_iterator<char>(input), std::istreambuf_iterator<char>());
}

int main() {
    try {
        const fs::path root = fs::path(DAGGER_TEST_PROGRAMS_DIR);
        size_t count = 0;

        for (const auto& entry : fs::directory_iterator(root)) {
            if (!entry.is_regular_file() || entry.path().extension() != ".dag") {
                continue;
            }

            const fs::path dagPath = entry.path();
            const fs::path outPath = dagPath.parent_path() / (dagPath.stem().string() + ".out");
            const std::string expected = readFile(outPath);
            const auto result = dagger::runFile(dagPath);

            if (!result.ok) {
                throw std::runtime_error(dagPath.filename().string() + " failed: " + result.errorMessage);
            }
            if (result.stdoutText != expected) {
                throw std::runtime_error(
                    dagPath.filename().string() + " output mismatch\nexpected: " + expected + "\nactual: " + result.stdoutText);
            }
            ++count;
        }

        if (count == 0) {
            throw std::runtime_error("no .dag tests found");
        }

        std::cout << "dag tests passed: " << count << "\n";
        return EXIT_SUCCESS;
    } catch (const std::exception& ex) {
        std::cerr << "dag_tests failed: " << ex.what() << "\n";
        return EXIT_FAILURE;
    }
}
