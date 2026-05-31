#pragma once

#include "sir.h"
#include "regalloc.h"
#include <string>
#include <vector>
#include <map>

namespace dagger {

class CodeGenerator {
public:
    std::string generate(const SIRProgram& program, const std::map<std::string, AllocationResult>& allocations);

private:
    std::string generateFunction(const SIRFunction& func, const AllocationResult& allocation);
    std::string generateInstruction(const SIRInstruction& inst, const AllocationResult& allocation, const SIRFunction& func);
    
    std::string operandToString(const SIRValue& val, const AllocationResult& allocation);
};

} // namespace dagger
