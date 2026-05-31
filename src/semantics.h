#pragma once

#include "ast.h"
#include "types.h"
#include <map>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace dagger {

void analyzeProgram(Program& program);

} // namespace dagger
