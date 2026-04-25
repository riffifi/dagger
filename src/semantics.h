#pragma once

#include "ast.h"
#include <map>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace dagger {

struct TypeInfo {
    enum class Kind { Unknown, Named, ObjectLiteral, List } kind = Kind::Unknown;
    std::string name;
    std::map<std::string, TypeInfo> fields;
    std::vector<TypeInfo> elements;

    static TypeInfo unknown();
    static TypeInfo named(std::string name);
    static TypeInfo object(std::map<std::string, TypeInfo> fields);
    static TypeInfo list(std::vector<TypeInfo> elements);

    bool isUnknown() const;
    bool isNamed(std::string_view expected) const;
    std::string describe() const;
};

void analyzeProgram(const Program& program);

} // namespace dagger
