#pragma once

#include <string_view>

namespace dagger {

bool isStdlibFunctionName(std::string_view name);
bool isReservedNamespaceRoot(std::string_view name);
bool hasReservedNamespaceRoot(std::string_view name);

} // namespace dagger
