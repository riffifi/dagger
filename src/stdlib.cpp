#include "stdlib.h"
#include <set>
#include <string>

namespace dagger {

namespace {

const std::set<std::string, std::less<>>& stdlibFunctions() {
    static const std::set<std::string, std::less<>> functions{
        "@error",
        "add",
        "and",
        "assert",
        "const",
        "div",
        "eq",
        "gt",
        "gte",
        "id",
        "lt",
        "lte",
        "mod",
        "mul",
        "neg",
        "neq",
        "not",
        "or",
        "out.write",
        "out.write_err",
        "out.writeln",
        "sub",
        "tee",
        "text.from",
        "text.join",
        "text.len",
    };
    return functions;
}

const std::set<std::string, std::less<>>& reservedNamespaceRoots() {
    static const std::set<std::string, std::less<>> roots{
        "bit",
        "cast",
        "file",
        "in",
        "math",
        "mem",
        "out",
        "text",
    };
    return roots;
}

} // namespace

bool isStdlibFunctionName(std::string_view name) {
    return stdlibFunctions().contains(std::string(name));
}

bool isReservedNamespaceRoot(std::string_view name) {
    return reservedNamespaceRoots().contains(std::string(name));
}

bool hasReservedNamespaceRoot(std::string_view name) {
    const size_t dot = name.find('.');
    if (dot == std::string_view::npos) {
        return isReservedNamespaceRoot(name);
    }
    return isReservedNamespaceRoot(name.substr(0, dot));
}

} // namespace dagger
