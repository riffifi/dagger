#pragma once

#include "sir.h"
#include <map>
#include <string>
#include <vector>

namespace dagger {

enum class Register {
    None,
    RAX, RBX, RCX, RDX, RSI, RDI, RBP, RSP,
    R8, R9, R10, R11, R12, R13, R14, R15
};

std::string registerName(Register reg);

struct AllocationResult {
    // Virtual register name -> physical register
    std::map<std::string, Register> assignments;
    // Virtual register name -> stack offset
    std::map<std::string, int> stackSlots;
    int totalStackSpace = 0;
};

class RegisterAllocator {
public:
    virtual ~RegisterAllocator() = default;
    virtual AllocationResult allocate(const SIRFunction& func) = 0;
};

class GreedyRegisterAllocator : public RegisterAllocator {
public:
    AllocationResult allocate(const SIRFunction& func) override;
};

} // namespace dagger
