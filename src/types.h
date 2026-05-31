#pragma once

#include <map>
#include <string>
#include <string_view>
#include <vector>

namespace dagger {

struct TypeInfo {
    enum class Kind { Unknown, Named, ObjectLiteral, List } kind = Kind::Unknown;
    std::string name;
    std::map<std::string, TypeInfo> fields;
    std::vector<TypeInfo> elements;

    static TypeInfo unknown() { return {Kind::Unknown}; }
    static TypeInfo named(std::string name) { TypeInfo t; t.kind = Kind::Named; t.name = std::move(name); return t; }
    static TypeInfo object(std::map<std::string, TypeInfo> fields) { TypeInfo t; t.kind = Kind::ObjectLiteral; t.fields = std::move(fields); return t; }
    static TypeInfo list(std::vector<TypeInfo> elements) { TypeInfo t; t.kind = Kind::List; t.elements = std::move(elements); return t; }

    bool isUnknown() const { return kind == Kind::Unknown; }
    bool isNamed(std::string_view expected) const { return kind == Kind::Named && name == expected; }
    
    std::string describe() const {
        switch (kind) {
            case Kind::Unknown: return "unknown";
            case Kind::Named: return name;
            case Kind::ObjectLiteral: {
                std::string s = "[";
                bool first = true;
                for (const auto& [n, t] : fields) {
                    if (!first) s += ", ";
                    first = false;
                    s += n + " :: " + t.describe();
                }
                s += "]";
                return s;
            }
            case Kind::List: {
                std::string s = "[";
                for (size_t i = 0; i < elements.size(); ++i) {
                    if (i > 0) s += ", ";
                    s += elements[i].describe();
                }
                s += "]";
                return s;
            }
        }
        return "";
    }
};

} // namespace dagger
